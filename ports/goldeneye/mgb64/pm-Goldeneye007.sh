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

source "$controlfolder/control.txt"   # AUDIT-0038: quote (path may contain spaces)
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR=/$directory/ports/Goldeneye007
CONFDIR="$GAMEDIR/conf"

mkdir -p "$CONFDIR"
# AUDIT-0038: a failed cd would otherwise leave us in the wrong directory and run
# ./ge007 against whatever happens to be there. Fail loudly if the game dir or the
# binary is missing instead of masking it behind PortMaster cleanup.
cd "$GAMEDIR" || { echo "MGB64: game directory $GAMEDIR is unavailable" >&2; pm_finish; exit 1; }
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1
if [ ! -x "$GAMEDIR/ge007" ]; then
  # AUDIT-0038: hand back to the PortMaster frontend (return to the gamelist)
  # instead of leaving it hanging on a broken install.
  echo "MGB64: ge007 binary missing or not executable in $GAMEDIR" >&2
  pm_finish
  exit 1
fi

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"

export MGB64_PORTMASTER=1
export MGB64_APP_SAVEDIR="$CONFDIR"

$GPTOKEYB "ge007" &
pm_platform_helper "$GAMEDIR/ge007"

# Route saves into conf/ explicitly. This script cd's to $GAMEDIR, so without an
# explicit --savedir the eeprom/ini would land beside the binary instead of the
# persisted conf dir (AUDIT-0033). The bare engine also honors MGB64_APP_SAVEDIR
# as a fallback (main_pc.c), but passing --savedir is unambiguous and wins.
./ge007 --rom "$GAMEDIR/baserom.u.z64" --savedir "$CONFDIR"

pm_finish
