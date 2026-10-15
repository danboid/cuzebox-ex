#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-bootloader-$$"
CC_BIN="${CC:-gcc}"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/SDL2"
cat > "$TMP/include/SDL2/SDL.h" <<'SDL'
#ifndef SDL_H_STUB
#define SDL_H_STUB
#include <stdint.h>
typedef uint32_t Uint32; typedef uint16_t Uint16; typedef uint8_t Uint8; typedef int32_t Sint32;
typedef int32_t SDL_JoystickID;
typedef union SDL_Event { Uint32 type; unsigned char pad[64]; } SDL_Event;
#define SDL_QUIT 0x100u
static inline int SDL_PushEvent(SDL_Event *e){ (void)e; return 1; }
#endif
SDL
read -r -a EXTRA <<< "${EXTRA_CFLAGS:-}"
COMMON=(-std=gnu17 -O2 -ffunction-sections -fdata-sections -DHEADLESS=1 -DFLAG_NOCONSOLE=1 -DFLAG_SELFCONT=1 -DENABLE_ESP=1 -I"$TMP/include" -I"$ROOT" "${EXTRA[@]}")
"$CC_BIN" "${COMMON[@]}" \
  "$ROOT/tests/bootloader/resident_bootloader_regression.c" \
  "$ROOT/cu_avr.c" "$ROOT/cu_avrc.c" "$ROOT/cu_avrfg.c" \
  -Wl,--gc-sections -o "$TMP/resident_bootloader_regression"
"$TMP/resident_bootloader_regression" "$ROOT/Bootloader.hex"

"$CC_BIN" -std=gnu17 -O2 -DHEADLESS=1 -DFLAG_NOCONSOLE=1 -I"$TMP/include" -I"$ROOT" \
  "$ROOT/tests/bootloader/eeprom_default_regression.c" "$ROOT/eepdump.c" "$ROOT/filesys.c" \
  -o "$TMP/eeprom_default_regression"
"$TMP/eeprom_default_regression"
