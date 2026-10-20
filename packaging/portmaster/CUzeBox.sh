#!/bin/bash
# PortMaster integration adapted from Dan MacDonald's cuzebox-ex fork.
# Also works directly without PortMaster (including an optional ROM argument).
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)" || exit 1
APP_DIR="$SCRIPT_DIR/CUzeBox"
BIN="$APP_DIR/cuzebox"

if [ ! -x "$BIN" ]; then
	echo "CUzeboxEX binary not found: $BIN" >&2
	exit 1
fi

controlfolder=""
for candidate in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
    "${XDG_DATA_HOME:-$HOME/.local/share}/PortMaster" /roms/ports/PortMaster; do
    if [ -f "$candidate/control.txt" ]; then
        controlfolder="$candidate"
        break
    fi
done
if [ -n "$controlfolder" ]; then
    source "$controlfolder/control.txt"
    if [ -n "${CFW_NAME:-}" ] && [ -f "$controlfolder/mod_${CFW_NAME}.txt" ]; then
        source "$controlfolder/mod_${CFW_NAME}.txt"
    fi
    get_controls
    export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-${SDL_GAMECONTROLLERCONFIG:-}}"
fi

# SDL handles controllers itself; no missing joy.gptk or compat binary is needed.
# Keep config.cfg beside the binary, as in the standalone launcher.
export SDL_GAMECONTROLLERCONFIG_FILE="${SDL_GAMECONTROLLERCONFIG_FILE:-$APP_DIR/gamecontrollerdb.txt}"
export LD_LIBRARY_PATH="$APP_DIR/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$APP_DIR" || exit 1
if [ -n "$controlfolder" ]; then
    # Run cleanup even when the emulator returns an error.
    trap 'status=$?; trap - EXIT; pm_finish; exit "$status"' EXIT
    pm_platform_helper "$BIN"
    "$BIN" "$@"
    exit $?
fi
exec "$BIN" "$@"
