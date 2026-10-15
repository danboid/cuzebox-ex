#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-esp-at-$$"
CC_BIN="${CC:-gcc}"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/SDL2"
cat > "$TMP/include/SDL2/SDL.h" <<'EOF'
#ifndef SDL_H_
#define SDL_H_
#include <stdint.h>
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;
typedef union SDL_Event { uint32_t type; uint8_t padding[64]; } SDL_Event;
typedef int SDL_Keycode;
typedef int SDL_Scancode;
typedef int32_t SDL_JoystickID;
typedef uint16_t SDL_Keymod;
typedef struct SDL_Rect { int x,y,w,h; } SDL_Rect;
typedef struct SDL_AudioSpec { int freq; uint16_t format; uint8_t channels; uint8_t silence; uint16_t samples; uint16_t padding; uint32_t size; void (*callback)(void*,uint8_t*,int); void *userdata; } SDL_AudioSpec;
typedef uint32_t SDL_AudioDeviceID;
#define SDL_NUM_SCANCODES 512
#endif
EOF
read -r -a EXTRA <<< "${EXTRA_CFLAGS:-}"
COMMON=(-std=gnu17 -O2 -ffunction-sections -fdata-sections -DTARGET_LINUX -DENABLE_ESP=1 -DHEADLESS=0 -I"$TMP/include" -I"$ROOT" "${EXTRA[@]}")
"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/esp_at/at23_regression.c" -Wl,--gc-sections -pthread -o "$TMP/at23_regression"
"$TMP/at23_regression"
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_esp_net.c" -o "$TMP/cu_esp_net.o"
"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/esp_at/network_regression.c" "$TMP/cu_esp_net.o" -Wl,--gc-sections -pthread -o "$TMP/network_regression"
"$TMP/network_regression"
"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/esp_at/mqtt_regression.c" "$TMP/cu_esp_net.o" -Wl,--gc-sections -pthread -o "$TMP/mqtt_regression"
"$TMP/mqtt_regression"

"$CC_BIN" "${COMMON[@]}" "$ROOT/tests/esp_at/savestate_legacy_regression.c" "$ROOT/savestate.c" -Wl,--gc-sections -pthread -o "$TMP/savestate_legacy_regression"
"$TMP/savestate_legacy_regression"
