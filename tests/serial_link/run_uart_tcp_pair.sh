#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-uart-tcp-pair-$$"
CC_BIN="${CC:-gcc}"
PORT="${CUZEBOX_TEST_PORT:-45731}"
TICKS="${CUZEBOX_UART_PAIR_TICKS:-6000}"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/SDL2"
: > "$TMP/include/SDL2/SDL.h"
COMMON=(-std=gnu17 -O2 -ffunction-sections -fdata-sections -DTARGET_LINUX -DENABLE_ESP=1 -DFLAG_NOCONSOLE=1 -DHEADLESS=1 -I"$TMP/include" -I"$ROOT")
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_esp_net.c" -o "$TMP/cu_esp_net.o"
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_uart.c" -o "$TMP/cu_uart.o"
"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/serial_link/uart_tcp_pair_test.c" "$TMP/cu_esp_net.o" "$TMP/cu_uart.o" -Wl,--gc-sections -pthread -o "$TMP/uart_tcp_pair_test"
"$TMP/uart_tcp_pair_test" server "$PORT" "$TICKS" >"$TMP/server.log" 2>&1 &
spid=$!
sleep 0.03
set +e
"$TMP/uart_tcp_pair_test" client "$PORT" "$TICKS" >"$TMP/client.log" 2>&1
crc=$?
wait "$spid"; src=$?
set -e
cat "$TMP/server.log"; cat "$TMP/client.log"
if [[ $src -ne 0 || $crc -ne 0 ]]; then echo "UART TCP pair failed server=$src client=$crc" >&2; exit 1; fi
echo "cycle-accurate UART TCP pair regression OK"
