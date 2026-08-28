#!/usr/bin/env bash
# R2 — Timing lock (remaster invariance rail).
#
# The only render->sim coupling in the port is frame timing (g_ClockTimer). To
# keep the simulation deterministic under remaster load, two invariants hold:
#   1. Only lvl.c and front.c may WRITE g_ClockTimer (no remaster TU may).
#   2. lvl.c must retain the 1-4 tick clamp AND the RAMROM-replay bypass
#      (the determinism anchor — recorded runs must not be re-clamped).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

ALLOWED='src/game/lvl.c src/game/front.c'
rogue=0

# (1) writers: assignment / compound-assign / inc / dec to g_ClockTimer, excluding
#     the declaration in lvl.c.
while IFS= read -r hit; do
  [ -z "$hit" ] && continue
  f="${hit%%:*}"
  case " $ALLOWED " in
    *" $f "*) : ;;                                  # allowed owner
    *) echo "R2 VIOLATION — unexpected writer of g_ClockTimer: $hit"; rogue=1 ;;
  esac
# Match ASSIGNMENT only: a bare '=' NOT part of '==' (comparison), or a compound
# assign / increment. Reads like 'g_ClockTimer == 0' must NOT be flagged.
done < <(grep -rnE 'g_ClockTimer[[:space:]]*(=[^=]|\+\+|--|[-+*/|&^]=)' src \
           --include='*.c' | grep -vE 's32[[:space:]]+g_ClockTimer[[:space:]]*=' || true)

# (2) clamp + RAMROM bypass survive in lvl.c
grep -q 'get_is_ramrom_flag' src/game/lvl.c \
  || { echo "R2 VIOLATION — RAMROM replay bypass (get_is_ramrom_flag) missing in lvl.c"; rogue=1; }
grep -qE 'g_ClockTimer[[:space:]]*=[[:space:]]*4;' src/game/lvl.c \
  || { echo "R2 VIOLATION — 4-tick clamp (g_ClockTimer = 4;) missing in lvl.c"; rogue=1; }

if [ "$rogue" -ne 0 ]; then
  echo "R2: FAIL — timing lock broken." >&2
  exit 1
fi
echo "R2: PASS — timing lock intact (writers confined to lvl.c/front.c; clamp+bypass present)."
