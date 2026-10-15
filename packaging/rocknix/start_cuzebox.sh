#!/bin/bash
# Standalone ROCKNIX launcher. Environment overrides also support staged tests.
set -e
profile_file="${CUZEBOX_SYSTEM_PROFILE:-/etc/profile}"
[[ ! -r "$profile_file" ]] || source "$profile_file"

binary="${CUZEBOX_BINARY:-/usr/bin/cuzebox-ex}"
config_dir="${CUZEBOX_CONFIG_HOME:-/storage/.config/cuzebox-ex}"
defaults="${CUZEBOX_DEFAULT_CONFIG:-/usr/config/cuzebox-ex/config.cfg}"
controller_db="${CUZEBOX_CONTROLLER_DB:-/usr/config/SDL-GameControllerDB/gamecontrollerdb.txt}"
rom=""
if [[ -n "${1:-}" ]]; then
    [[ -f "$1" ]] || { echo "ROM not found: $1" >&2; exit 1; }
    rom=$(readlink -f -- "$1")
fi
[[ -x "$binary" ]] || { echo "CUzeBox executable missing: $binary" >&2; exit 1; }
mkdir -p "$config_dir"
if [[ ! -f "$config_dir/config.cfg" ]]; then
    cp "$defaults" "$config_dir/config.cfg"
fi
if [[ -r "$controller_db" ]]; then
    export SDL_GAMECONTROLLERCONFIG_FILE="$controller_db"
fi
# Native Start+Select and SIGTERM use normal emulator shutdown, preserving saves.
if declare -F set_kill >/dev/null; then set_kill set "-TERM cuzebox-ex"; fi
cd "$config_dir"
if [[ -n "$rom" ]]; then exec "$binary" "$rom"; fi
exec "$binary"
