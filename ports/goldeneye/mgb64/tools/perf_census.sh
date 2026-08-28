#!/usr/bin/env bash
# perf_census.sh — deterministic headless per-level frame-time census for the
# MGB64 native port. Foundation of the M0 milestone in docs/design/PERFORMANCE_PLAN.md.
#
# For each direct-boot level it boots the port headless + deterministic, runs a
# fixed number of frames, and records mean per-frame `work_ms` from the built-in
# GE007_PERF_TRACE tracer. Two configs are measured per level:
#   default            — stock settings
#   xluoff             — GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1 (isolates the
#                        per-triangle framebuffer-copy defect; see PERFORMANCE_PLAN §4.1)
#
# Output: CSV (level,default_ms,default_fps,xluoff_ms,speedup) to stdout and to
# the path in $CENSUS_OUT (default: baselines/perf_census_latest.csv).
#
# Requirements: built build/ge007, a ROM (baserom.u.z64), python3, and a GUI
# session (macOS OpenGL still needs a window even when hidden). Bash, not sh.
#
# Usage:
#   tools/perf_census.sh                 # all 20 levels
#   tools/perf_census.sh jungle cradle   # a subset
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$REPO/build/ge007}"          # override with BIN=... for out-of-tree builds
CENSUS_OUT="${CENSUS_OUT:-$REPO/baselines/perf_census_latest.csv}"
FRAMES="${PERF_FRAMES:-180}"           # total frames before auto-exit
AFTER="${PERF_AFTER:-80}"              # start sampling after warmup
TIMEOUT_S="${PERF_TIMEOUT:-25}"        # hard per-run wall cap (seconds)

# Renderer lanes (W3.E1 §4.8): default `gl`; macOS also measures `metal`, adding
# metal_ms,metal_fps,metal_gpu_ms columns (GE007_METAL_GPU_TRACE lane). Override
# e.g. RENDERERS="gl metal" or RENDERERS="metal".
if [ -z "${RENDERERS:-}" ]; then
  if [ "$(uname)" = Darwin ]; then RENDERERS="gl metal"; else RENDERERS="gl"; fi
fi
gl_in=0; metal_in=0
for r in $RENDERERS; do
  [ "$r" = gl ]    && gl_in=1
  [ "$r" = metal ] && metal_in=1
done

ALL_LEVELS=(dam facility runway surface1 bunker1 silo frigate surface2 bunker2 \
            statue archives streets depot train jungle control caverns cradle aztec egypt)
LEVELS=("$@"); [ ${#LEVELS[@]} -eq 0 ] && LEVELS=("${ALL_LEVELS[@]}")

[ -x "$BIN" ] || { echo "error: $BIN not built (see docs/BUILDING.md)" >&2; exit 1; }

# Run one config; echo mean work_ms (or NA). Args after logfile are extra env (VAR=VAL).
runmeasure() {
  local lvl="$1" log="$2"; shift 2
  env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
      GE007_NO_VSYNC=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
      GE007_AUTO_EXIT_FRAME="$FRAMES" GE007_PERF_TRACE=1 \
      GE007_PERF_TRACE_AFTER_FRAME="$AFTER" GE007_PERF_TRACE_BUDGET=$((FRAMES)) \
      "$@" "$BIN" --level "$lvl" --deterministic --background --no-input-grab \
      > "$log" 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null; do
    sleep 1; waited=$((waited+1))
    [ "$waited" -ge "$TIMEOUT_S" ] && { kill "$pid" 2>/dev/null; break; }
  done
  wait "$pid" 2>/dev/null
  python3 - "$log" <<'PY'
import sys, re
vals = [float(m.group(1)) for line in open(sys.argv[1])
        for m in [re.search(r'work_ms=([0-9.]+)', line)] if m]
print(f"{sum(vals)/len(vals):.2f}" if vals else "NA")
PY
}

# Metal renderer lane (W3.E1 §4.8): GE007_RENDERER=metal + GE007_METAL_GPU_TRACE=1.
# Echoes two space-separated means: CPU work_ms and GPU total_ms (post-warmup
# [PERF-GPU] frames only), or "NA NA".
runmetal() {
  local lvl="$1" log="$2"
  env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
      GE007_NO_VSYNC=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
      GE007_AUTO_EXIT_FRAME="$FRAMES" GE007_PERF_TRACE=1 \
      GE007_PERF_TRACE_AFTER_FRAME="$AFTER" GE007_PERF_TRACE_BUDGET=$((FRAMES)) \
      GE007_RENDERER=metal GE007_METAL_GPU_TRACE=1 \
      "$BIN" --level "$lvl" --deterministic --background --no-input-grab \
      > "$log" 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null; do
    sleep 1; waited=$((waited+1))
    [ "$waited" -ge "$TIMEOUT_S" ] && { kill "$pid" 2>/dev/null; break; }
  done
  wait "$pid" 2>/dev/null
  python3 - "$log" "$AFTER" <<'PY'
import sys, re
after = int(sys.argv[2])
work, gpu = [], []
for line in open(sys.argv[1]):
    m = re.search(r'work_ms=([0-9.]+)', line)
    if m: work.append(float(m.group(1)))
    g = re.search(r'\[PERF-GPU\] frame=(\d+) total_ms=([0-9.]+)', line)
    if g and int(g.group(1)) >= after: gpu.append(float(g.group(2)))
w  = f"{sum(work)/len(work):.2f}" if work else "NA"
gg = f"{sum(gpu)/len(gpu):.2f}" if gpu else "NA"
print(f"{w} {gg}")
PY
}

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$(dirname "$CENSUS_OUT")"
header="level,default_ms,default_fps,xluoff_ms,speedup"
[ "$metal_in" = 1 ] && header="$header,metal_ms,metal_fps,metal_gpu_ms"
echo "$header" | tee "$CENSUS_OUT"
for lvl in "${LEVELS[@]}"; do
  if [ "$gl_in" = 1 ]; then
    d=$(runmeasure "$lvl" "$tmp/${lvl}_def.log")
    x=$(runmeasure "$lvl" "$tmp/${lvl}_xlu.log" GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1)
  else
    d=NA; x=NA
  fi
  m=NA; g=NA
  [ "$metal_in" = 1 ] && read -r m g < <(runmetal "$lvl" "$tmp/${lvl}_mtl.log")
  python3 - "$lvl" "$d" "$x" "$metal_in" "$m" "$g" <<'PY' | tee -a "$CENSUS_OUT"
import sys
lvl, d, x, metal_in, m, g = sys.argv[1:7]
def fps(v):
    try: return f"{1000/float(v):.0f}"
    except: return "NA"
try: spd = f"{float(d)/float(x):.2f}x"
except: spd = "NA"
row = f"{lvl},{d},{fps(d)},{x},{spd}"
if metal_in == "1":
    row += f",{m},{fps(m)},{g}"
print(row)
PY
done
echo "census written to $CENSUS_OUT" >&2
