#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

# Variables (Fixed double-slash bug)
GAMEDIR="$directory/ports/CUzeBox"
DEVICE_ARCH="${DEVICE_ARCH:-aarch64}"
GAME="cuzebox"

# CD and set permissions
cd $GAMEDIR
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1
rm -rf "$GAMEDIR/config/gamelog.txt"
$ESUDO chmod +x -R $GAMEDIR/*

# Set config dir
bind_directories ~/.$GAME $GAMEDIR/config

# Exports
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export TEXTINPUTPRESET=$CHEATS
export TEXTINPUTINTERACTIVE="Y"
export TEXTINPUTNUMBERSONLY="Y"

# Use compatibility binary if low glibc (Ensure your binary matches these names)
if [ $CFW_GLIBC -lt 234 ]; then
    GAME="$GAME.compat"
else
    GAME="$GAME.$DEVICE_ARCH"
fi

# Run game
$GPTOKEYB "$GAME" -c "config/joy.gptk" &
pm_platform_helper "$GAMEDIR/$GAME"

# Temporarily drop EmulationStation's display hold if it's blocking KMS/DRM
if command -v systemctl >/dev/null 2>&1; then
    sudo systemctl stop emulationstation 2>/dev/null || true
fi

./$GAME

# Restart EmulationStation when exiting CUzeBox so you don't get dropped to a blank screen
if command -v systemctl >/dev/null 2>&1; then
    sudo systemctl start emulationstation 2>/dev/null || true
fi

# Cleanup
pm_finish
