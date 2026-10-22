#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
untool="${1:?UNTool source directory required}"
image="${2:?Supported factory BIN required}"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
cc -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 -DENABLE_ESP_ROM -ffunction-sections -fdata-sections -I"$root" -I"$untool/tests/fake" -I"$untool" \
 "$root/tests/esp_at/rom_loader_regression.c" "$root/cu_esp_rom.c" "$untool/esp_rom.c" \
 -Wl,--gc-sections -o "$temp/test"
"$temp/test" "$temp/player.cfg" "$image"
