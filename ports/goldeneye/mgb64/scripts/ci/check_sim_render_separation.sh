#!/usr/bin/env bash
# R1 — Static sim/render separation check (remaster invariance rail).
#
# Fails if any *simulation* translation unit (src/game/*.c) references a
# renderer-BACKEND / material / FBO / GL symbol. The display-list SUBMISSION API
# (gfx_run_dl, gfx_register_*, gfx_set_*, gfx_ptr_*, gfx_segment_table, ...) is
# ALLOWED — that is how the simulation submits geometry. What must never happen
# is the simulation reading render/material/FBO/GL *backend* state back into game
# logic; that is the coupling this gate forbids.
#
# Mechanism: nm the built game objects and reject undefined references matching
# the backend denylist. macOS nm prefixes symbols with '_'.
#
# Usage: check_sim_render_separation.sh [OBJDIR]
#   OBJDIR defaults to the CMake game-object dir. Build the port first.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

OBJDIR="${1:-build/CMakeFiles/ge007.dir/src/game}"
if [ ! -d "$OBJDIR" ]; then
  echo "R1: object dir '$OBJDIR' not found — build the native port first." >&2
  exit 2
fi

# Renderer backend / material / GL symbols. NOT a blanket gfx_* ban: the fast3d
# *interpreter* surface (gfx_pc.c: gfx_run_dl, gfx_register_*, gfx_set_*,
# gfx_ptr_*, gfx_segment_table) is the legitimate submission API.
#   _gfx_opengl_   backend (GL) entry points
#   _texture_pack_ HD texture-replacement loader
#   _g_pcTexturePack   active pack pointer
#   _gl[A-Z] / _glad_gl   raw GL calls
DENY='(_gfx_opengl_|_texture_pack_|_g_pcTexturePack|_gl[A-Z]|_glad_gl)'

violations=0
for o in "$OBJDIR"/*.c.o; do
  [ -e "$o" ] || continue
  hits=$(nm -u "$o" 2>/dev/null | awk '{print $NF}' | grep -E "$DENY" || true)
  if [ -n "$hits" ]; then
    echo "R1 VIOLATION — $(basename "$o") references renderer-backend symbols:"
    printf '%s\n' "$hits" | sort -u | sed 's/^/    /'
    violations=$((violations + 1))
  fi
done

if [ "$violations" -ne 0 ]; then
  echo ""
  echo "R1: FAIL — $violations simulation TU(s) read renderer-backend state." >&2
  echo "     Simulation must stay render-agnostic (gameplay-invariance rail)." >&2
  exit 1
fi
echo "R1: PASS — no simulation TU references renderer-backend state."
