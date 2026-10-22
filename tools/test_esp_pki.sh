#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=emulator-test \
 -keyout "$temp/key.pem" -out "$temp/cert.pem" >/dev/null 2>&1
cc -std=gnu17 -O1 -g -DENABLE_ESP -DENABLE_ESP_ROM -DENABLE_OPENSSL -DTARGET_LINUX \
 -ffunction-sections -fdata-sections -fsanitize=address,undefined -I"$root" $(pkg-config --cflags sdl2) \
 "$root/tests/esp_at/rom_pki_regression.c" "$root/cu_esp_rom.c" -Wl,--gc-sections -lssl -lcrypto -pthread -o "$temp/test"
"$temp/test" "$temp/player.cfg" "$temp/cert.pem" "$temp/key.pem"
cc -std=gnu17 -O1 -g -DENABLE_ESP -DENABLE_ESP_ROM -DTARGET_LINUX \
 -ffunction-sections -fdata-sections -fsanitize=address,undefined -I"$root" $(pkg-config --cflags sdl2) \
 "$root/tests/esp_at/rom_at_regression.c" "$root/cu_esp_rom.c" -Wl,--gc-sections -pthread -o "$temp/at-test"
"$temp/at-test" "$temp/at.cfg"
