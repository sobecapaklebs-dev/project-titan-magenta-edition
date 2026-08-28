#!/usr/bin/env bash
# gpu_budget_gate.sh — W3.E1 §4.8 standing GPU-budget gate (Metal only).
#
# Asserts a W3 feature's (scene+post) GPU cost, ON-minus-OFF, stays within the
# W3 budget on jungle+dam: < 2.0 ms @1080p-class, < 4.0 ms @4K-class. Cost is the
# per-frame (scene_ms+post_ms) from the GE007_METAL_GPU_TRACE lane (== whole-frame
# GPU time), averaged over post-warmup frames. CPU work_ms is NOT a valid gate,
# which is why this measures GPU-ms.
#
# The feature is toggled via --config-override <flag>=1/0 under --remaster (so the
# remaster post chain is present in both runs and only the feature branch differs).
#
# Usage:
#   tools/gpu_budget_gate.sh --feature Video.Ssao
#   tools/gpu_budget_gate.sh --feature Video.SsaoMode --on hemisphere --off 0 \
#                            --levels jungle,dam --profile 1080p
#
# Exit 0 = within budget on every level; 1 = over budget; 2 = usage error.
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$REPO/build/ge007}"
FRAMES="${GPU_GATE_FRAMES:-160}"       # frames before auto-exit
AFTER="${GPU_GATE_AFTER:-90}"          # first sampled [PERF-GPU] frame (skip warmup)
TIMEOUT_S="${GPU_GATE_TIMEOUT:-30}"    # hard per-run wall cap (seconds)

FEATURE=""; LEVELS="jungle,dam"; PROFILE="1080p"; ON_VAL="1"; OFF_VAL="0"
while [ $# -gt 0 ]; do
  case "$1" in
    --feature) FEATURE="$2"; shift 2;;
    --levels)  LEVELS="$2";  shift 2;;
    --profile) PROFILE="$2"; shift 2;;
    --on)      ON_VAL="$2";  shift 2;;
    --off)     OFF_VAL="$2"; shift 2;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) echo "gpu_budget_gate: unknown arg '$1'" >&2; exit 2;;
  esac
done
[ -n "$FEATURE" ] || { echo "usage: gpu_budget_gate.sh --feature <Video.Key> [--levels jungle,dam] [--profile 1080p|4k] [--on V --off V]" >&2; exit 2; }
[ -x "$BIN" ]     || { echo "error: $BIN not built (see docs/BUILDING.md)" >&2; exit 1; }
[ "$(uname)" = Darwin ] || { echo "gpu_budget_gate: Metal GPU trace is macOS-only; skipping (no-op PASS)" >&2; exit 0; }

case "$PROFILE" in
  1080p) BUDGET_MS="2.0"; RSCALE=1;;
  4k|4K) BUDGET_MS="4.0"; RSCALE=2;;
  *) echo "gpu_budget_gate: unknown profile '$PROFILE' (use 1080p|4k)" >&2; exit 2;;
esac

# Reset the persisted config on exit (--config-override writes ge007.ini; G2).
cleanup() { [ -e "$REPO/baserom.u.z64" ] && ( cd "$REPO" && "$BIN" baserom.u.z64 --reset-config >/dev/null 2>&1 ) || true; }
trap cleanup EXIT

# Echo mean (scene+post) GPU ms for one feature value (post-warmup), or NA.
measure() { # lvl val log
  local lvl="$1" val="$2" log="$3"
  env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
      GE007_NO_VSYNC=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
      GE007_AUTO_EXIT_FRAME="$FRAMES" GE007_RENDERER=metal GE007_METAL_GPU_TRACE=1 \
      "$BIN" --remaster --level "$lvl" --deterministic --background --no-input-grab \
      --config-override "$FEATURE=$val" \
      --config-override "Video.RenderScale=$RSCALE" \
      > "$log" 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null; do
    sleep 1; waited=$((waited+1))
    [ "$waited" -ge "$TIMEOUT_S" ] && { kill "$pid" 2>/dev/null; break; }
  done
  wait "$pid" 2>/dev/null
  python3 - "$log" "$AFTER" <<'PY'
import sys, re
after = int(sys.argv[2]); v = []
for line in open(sys.argv[1]):
    g = re.search(r'\[PERF-GPU\] frame=(\d+) total_ms=[0-9.]+ scene_ms=([0-9.]+) post_ms=([0-9.]+)', line)
    if g and int(g.group(1)) >= after:
        v.append(float(g.group(2)) + float(g.group(3)))
print(f"{sum(v)/len(v):.4f}" if v else "NA")
PY
}

echo "GPU budget gate: feature=$FEATURE on=$ON_VAL off=$OFF_VAL profile=$PROFILE budget<${BUDGET_MS}ms levels=$LEVELS (Metal)"
IFS=',' read -ra LVLS <<< "$LEVELS"
fail=0
for lvl in "${LVLS[@]}"; do
  tmp="$(mktemp -d)"
  off=$(measure "$lvl" "$OFF_VAL" "$tmp/off.log")
  on=$(measure  "$lvl" "$ON_VAL"  "$tmp/on.log")
  rm -rf "$tmp"
  line=$(python3 - "$lvl" "$off" "$on" "$BUDGET_MS" <<'PY'
import sys
lvl, off, on, budget = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
try:
    d = float(on) - float(off)
except Exception:
    print(f"  {lvl:8s} off={off} on={on} -> FAIL (no GPU samples — GL hang or trace unsupported?)")
    sys.exit(3)
verdict = "PASS" if d < budget else "FAIL"
print(f"  {lvl:8s} off={float(off):7.3f}ms on={float(on):7.3f}ms delta={d:+7.3f}ms  budget<{budget}ms  {verdict}")
sys.exit(0 if verdict == "PASS" else 3)
PY
)
  rc=$?
  echo "$line"
  [ $rc -eq 0 ] || fail=1
done

if [ $fail -eq 0 ]; then echo "GPU budget gate: PASS"; exit 0
else echo "GPU budget gate: FAIL"; exit 1; fi
