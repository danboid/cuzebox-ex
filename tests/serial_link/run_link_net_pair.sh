#!/usr/bin/env bash
# Internet (relay) and LAN link regression: two processes exchange framed,
# checksummed packets through the real cu_esp_net.c/cu_uart.c code.
#  1. internet mode through tools/uzenet_relay_mock.py with direct UDP allowed
#  2. internet mode relay-only with 10% packet loss and 30-60 ms delay
#  3. LAN mode (UDP discovery + automatic TCP pairing)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-link-net-$$"
CC_BIN="${CC:-gcc}"
RPORT="${CUZEBOX_TEST_RELAY_PORT:-45810}"
TICKS="${CUZEBOX_LINK_TICKS:-3000}"
RELAY_PID=""
cleanup(){ [ -n "$RELAY_PID" ] && kill "$RELAY_PID" 2>/dev/null || true; rm -rf "$TMP"; }
trap cleanup EXIT
mkdir -p "$TMP/include/SDL2"
: > "$TMP/include/SDL2/SDL.h"
COMMON=(-std=gnu17 -O2 -ffunction-sections -fdata-sections -DTARGET_LINUX -DENABLE_ESP=1 -DFLAG_NOCONSOLE=1 -DHEADLESS=1 -I"$TMP/include" -I"$ROOT")
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_esp_net.c" -o "$TMP/cu_esp_net.o"
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_uart.c" -o "$TMP/cu_uart.o"
"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/serial_link/uart_tcp_pair_test.c" "$TMP/cu_esp_net.o" "$TMP/cu_uart.o" -Wl,--gc-sections -pthread -o "$TMP/pair"

pair(){ # name mode port
    local name="$1" mode="$2" port="$3" a b ra rb
    "$TMP/pair" "$mode" "$port" "$TICKS" 0 >"$TMP/a.log" 2>&1 & a=$!
    "$TMP/pair" "$mode" "$port" "$TICKS" 1 >"$TMP/b.log" 2>&1 & b=$!
    set +e; wait "$a"; ra=$?; wait "$b"; rb=$?; set -e
    cat "$TMP/a.log" "$TMP/b.log"
    if [[ $ra -ne 0 || $rb -ne 0 ]]; then echo "$name FAILED ($ra/$rb)" >&2; exit 1; fi
    echo "$name OK"
}
relay(){
    [ -n "$RELAY_PID" ] && kill "$RELAY_PID" 2>/dev/null || true
    python3 "$ROOT/tools/uzenet_relay_mock.py" --port "$RPORT" --bind 127.0.0.1 --quiet "$@" & RELAY_PID=$!
    sleep 0.4
}
relay
pair "internet (direct UDP)" internet "$RPORT"
relay --no-direct --loss 0.10 --delay-ms 30 --jitter-ms 30
pair "internet (relay only, 10% loss)" internet "$RPORT"
kill "$RELAY_PID"; RELAY_PID=""
pair "LAN discovery" lan "${CUZEBOX_TEST_PORT:-45741}"
echo "link network regression OK"
