#!/bin/bash
# D43 intro regression check: capture one level's intro with the fix (default-on)
# and emit a single RESULT line. Native-only (no stock oracle). Usage:
#   scripts/intro_regression_check.sh <level_num> <out_dir>
set -u
cd "$(dirname "$0")/.."
LEVEL="$1"
OUT="${2:-/tmp/introreg}"
mkdir -p "$OUT"
TRACE="$OUT/intro_l${LEVEL}.jsonl"
SHOT="screenshot_introreg_l${LEVEL}"

SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_ENABLE_LEVEL_INTRO=1 \
  timeout 150 ./build/ge007 --rom baserom.u.z64 --level "$LEVEL" --deterministic --faithful \
  --trace-state "$TRACE" --screenshot-frame 300 --screenshot-label "introreg_l${LEVEL}" \
  --screenshot-exit >"$OUT/l${LEVEL}.log" 2>&1
EXIT=$?
rm -f "${SHOT}.bmp"

python3 - "$TRACE" "$LEVEL" "$EXIT" <<'PY'
import sys,json
trace,level,exit_code=sys.argv[1],sys.argv[2],int(sys.argv[3])
phase3=False; positions=set(); anim_ok=False; frames=0; crashed = (exit_code!=0)
# Position sanity: track Bond's full-intro Y range and max coordinate magnitude
# (cam==3, Bond present) to catch any level where unpinning phases 1-2 sends Bond
# to an insane place. Also track Y jump between consecutive onscreen frames.
ys=[]; maxmag=0.0; maxjump=0.0; prevpos=None
try:
    for line in open(trace):
        try:d=json.loads(line)
        except:continue
        frames+=1
        if d.get("cam")!=3:continue
        intro=d.get("intro") or {}; ba=intro.get("bond_anim") or {}
        h=ba.get("hash") or ""
        c=d.get("col") or d.get("pos")
        present = bool(h) and h!="0x0000000000000000"
        if present and c:
            ys.append(c[1])
            maxmag=max(maxmag, max(abs(c[0]),abs(c[1]),abs(c[2])))
            if prevpos is not None:
                maxjump=max(maxjump, abs(c[1]-prevpos))
            prevpos=c[1]
        if h.startswith("0x79F92FB0"):
            phase3=True; anim_ok=True
            if c: positions.add(tuple(round(x,1) for x in c[:3]))
except FileNotFoundError:
    pass
moved = len(positions)>1
yrange = (round(min(ys),1), round(max(ys),1)) if ys else (None,None)
# insane if Bond's coordinate magnitude is wildly off-map (dam coords are ~O(20000))
sane = (maxmag < 100000.0)
print(f"RESULT level={level} exit={exit_code} crashed={crashed} frames={frames} "
      f"phase3_fired={phase3} anim99_hash_ok={anim_ok} bond_moved={moved} "
      f"distinct_positions={len(positions)} y_range={yrange} max_coord_mag={round(maxmag,1)} "
      f"max_y_jump={round(maxjump,1)} position_sane={sane}")
PY
