#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 CatacombsOfTheDamned.hex CatacombsOfTheDamned.elf" >&2
    exit 2
fi

HEX="$(realpath "$1")"
ELF="$(realpath "$2")"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-catacombs-pair-$$"
CC_BIN="${CC:-gcc}"
NM_BIN="${NM:-nm}"
PORT="${CUZEBOX_TEST_PORT:-45683}"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/SDL2"

cat > "$TMP/include/SDL2/SDL.h" <<'EOF'
#ifndef SDL_H_STUB
#define SDL_H_STUB
#include <stdint.h>
typedef uint32_t Uint32; typedef uint16_t Uint16; typedef uint8_t Uint8; typedef int32_t Sint32;
typedef int32_t SDL_JoystickID;
static inline Uint32 SDL_GetMouseState(int *x,int *y){ if(x)*x=0; if(y)*y=0; return 0; }
typedef union SDL_Event { Uint32 type; unsigned char pad[64]; } SDL_Event;
#define SDL_QUIT 0x100u
static inline int SDL_PushEvent(SDL_Event *e){ (void)e; return 1; }
#endif
EOF

sym_addr() {
    local name="$1" raw
    raw="$($NM_BIN -n "$ELF" | awk -v n="$name" '$3==n {print $1; exit}')"
    if [[ -z "$raw" ]]; then
        echo "missing ELF symbol: $name" >&2
        exit 2
    fi
    printf '0x%Xu' "$((16#$raw - 0x800000))"
}

DEFS=(
    "-DGAME_LOCAL_PLAYER_ADDR=$(sym_addr Game_localPlayerId)"
    "-DGAME_RUN_MODE_ADDR=$(sym_addr Game_runMode)"
    "-DGAME_STATE_ADDR=$(sym_addr Game_state)"
    "-DGAME_LOCAL_TOKEN_ADDR=$(sym_addr Game_localNetworkToken)"
    "-DGAME_REMOTE_TOKEN_ADDR=$(sym_addr Game_remoteNetworkToken)"
    "-DGAME_CONNECT_PHASE_ADDR=$(sym_addr Game_serialConnectPhase)"
    "-DGAME_TICK_FRAME_ADDR=$(sym_addr Game_globalTickFrame)"
    "-DGAME_LINK_LOST_FRAMES_ADDR=$(sym_addr Game_linkLostFrames)"
)

COMMON=(
    -std=gnu17 -O2 -ffunction-sections -fdata-sections
    -DHEADLESS=1 -DFLAG_NOCONSOLE=1 -DFLAG_SELFCONT=1 -DENABLE_ESP=1
    -I"$TMP/include" -I"$ROOT" "${DEFS[@]}"
)

"$CC_BIN" "${COMMON[@]}" \
    "$ROOT/tests/serial_link/catacombs_pair_headless.c" \
    "$ROOT/cu_avr.c" "$ROOT/cu_avrc.c" "$ROOT/cu_avrfg.c" \
    "$ROOT/cu_ctr.c" "$ROOT/cu_multitap.c" "$ROOT/cu_vdev.c" "$ROOT/cu_uart.c" \
    -Wl,--gc-sections -o "$TMP/catacombs-pair"

"$TMP/catacombs-pair" "$HEX" server "$PORT" >"$TMP/server.log" 2>&1 &
server_pid=$!
sleep 0.05
set +e
"$TMP/catacombs-pair" "$HEX" client "$PORT" >"$TMP/client.log" 2>&1
client_rc=$?
wait "$server_pid"
server_rc=$?
set -e

cat "$TMP/server.log"
cat "$TMP/client.log"
if [[ $server_rc -ne 0 || $client_rc -ne 0 ]]; then
    echo "Catacombs pair regression failed: server=$server_rc client=$client_rc" >&2
    exit 1
fi

grep -q 'server gameplay reached target tick=1000' "$TMP/server.log"
grep -q 'client gameplay reached target tick=1000' "$TMP/client.log"
echo "Catacombs two-instance UART gameplay regression OK"
