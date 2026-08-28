#!/usr/bin/env bash
#
# W3.E2.T5 — SSAO v2 (hemisphere) AO-field ROI gate runner.
#
# Scripts the W3.E2.T2 AO-field capture (GE007_SSAO_DEBUG=1 returns the raw AO
# field, mode 3) plus the W3.E2.T3 temporal-crawl capture, and asserts the ROI
# gates that were chosen and recorded in the T2/T3 commits:
#
#   T2 AO-field gates (255-occlusion "AO-visibility" convention, frame 120,
#   640x480 screenshot, hemisphere debug field; recorded in commit f31ca8f):
#     - flat  mid-ground        mean >= 247/255   (no false wash on open ground)
#     - crease / under-prop     mean <= 215/255   (genuine occluders register)
#     - far / grazing valley    mean >= 247/255   AND no speckle band
#       ("no speckle" encoded as >=90% of far-ROI pixels >= 240 — a dithered
#        under-sampled AO band would blanket a large fraction in mid-grey; a few
#        localised darker geometry-edge pixels do not.)
#
#   Feature change-budget (the CORRECTED §8 step-2 A/B; see W3.E2.T4 commit
#   d9f348b PLAN-BUG note): §8's inline example compares planar-v1 vs
#   hemisphere-v2 @ --max-changed-pct 12, which is UNSATISFIABLE — planar-v1 is
#   itself a ~30% broad wash that v2 removes, so v1-vs-v2 ~= 32%. The real
#   feature delta is hemisphere-v2 vs SSAO-OFF, which must stay <= 12% changed
#   (measured: jungle 5.1%, dam 10.0%, facility 0.0%). This gate encodes that.
#
#   T3 temporal gate (recorded in commit 891966b, dam only — the only level with
#   a recorded static-ground ROI): slow pan GE007_AUTO_LOOK_RIGHT=60:200 STEP=2,
#   frames 150 vs 151, flat-ground ROI (240,264)-(320,300). The ISOLATED AO
#   crawl |(v2-off)_150 - (v2-off)_151| (differencing the Ssao=0 baseline cancels
#   the scrolling-ground texture motion) must be <= 2/255. Levels without a
#   recorded temporal ROI run the AO-field gates only.
#
# Hemisphere SSAO v2 is Metal-only (the reconstruction math op-hangs Apple's
# GL-over-Metal translator; doc 03 §4.1.6), so this gate pins GE007_RENDERER=metal.
# It is a screenshot gate (R2: artifacts stay out of git; captured in an isolated
# temp CWD so no ge007.ini drift, G2).
#
# Usage: tools/ssao_gate.sh [level ...]        (default: dam jungle facility)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build/ge007}"
ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
FRAME="${GE007_SSAO_GATE_FRAME:-120}"

[ -x "$BIN" ] || { echo "ssao-gate: $BIN not built" >&2; exit 2; }
[ -e "$ROM" ] || { echo "ssao-gate: ROM $ROM not found (local gate)" >&2; exit 2; }

LEVELS=("$@")
[ ${#LEVELS[@]} -gt 0 ] || LEVELS=(dam jungle facility)

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# Canonical headless capture envs + hemisphere v2 on Metal, AO-debug field.
BASE_ENV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
          GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)
# Master gates + hemisphere mode + pinned SSAA (matches the T2/T3 capture; the
# tuned v2 knobs Bias=0.15/Power=3.0/FarCutoff=800 are code defaults on main).
V2_OVERRIDES=(--config-override Video.RemasterFX=1 --config-override Video.Ssao=1
              --config-override Video.SsaoMode=hemisphere --config-override Video.RenderScale=2)

# ---- ROI table + gate logic (python; ROIs are crop boxes X0,Y0,X1,Y1) --------
cat > "$TMP/gate.py" <<'PY'
import sys, warnings
warnings.filterwarnings("ignore")
from PIL import Image

# Recovered from the W3.E2.T2 commit (f31ca8f) message. Boxes are PIL crop
# (X0,Y0,X1,Y1) in the fixed 640x480 screenshot space at frame 120.
ROI = {
    "dam":      {"flat": (192,302,352,336), "crease": (339,226,403,245), "far": (256,269,333,288)},
    "jungle":   {"flat": (430,360,560,410), "crease": (96,255,168,285),  "far": (180,270,300,295)},
    # facility frame-120 view is a flat close door with no creases -> flat-robustness only (T2).
    "facility": {"flat": (192,144,448,288)},
}
FLAT_MIN, CREASE_MAX, FAR_MIN = 247.0, 215.0, 247.0
FAR_SPECKLE_FLOOR, FAR_SPECKLE_FRAC = 240, 0.90   # >=90% of far pixels >= 240

def mean(im, box):
    d = list(im.crop(box).convert("L").getdata()); return sum(d)/len(d), d

def gate(level, path):
    rois = ROI.get(level)
    if rois is None:
        print("  ssao-gate: no ROI table for level '%s'" % level); return 1
    im = Image.open(path)
    if im.size != (640, 480):
        print("  FAIL %-8s screenshot is %sx%s, expected 640x480" % (level, *im.size)); return 1
    ok = True
    m, _ = mean(im, rois["flat"])
    r = m >= FLAT_MIN; ok &= r
    print("  %s %-8s flat   %s = %6.2f  (>= %.0f)" % ("PASS" if r else "FAIL", level, rois["flat"], m, FLAT_MIN))
    if "crease" in rois:
        m, _ = mean(im, rois["crease"]); r = m <= CREASE_MAX; ok &= r
        print("  %s %-8s crease %s = %6.2f  (<= %.0f)" % ("PASS" if r else "FAIL", level, rois["crease"], m, CREASE_MAX))
    if "far" in rois:
        m, d = mean(im, rois["far"]); r1 = m >= FAR_MIN
        frac = sum(1 for x in d if x >= FAR_SPECKLE_FLOOR) / len(d); r2 = frac >= FAR_SPECKLE_FRAC
        ok &= (r1 and r2)
        print("  %s %-8s far    %s = %6.2f  (>= %.0f) ; %.1f%% >= %d (need >= %.0f%%, no-speckle)"
              % ("PASS" if (r1 and r2) else "FAIL", level, rois["far"], m, FAR_MIN, 100*frac, FAR_SPECKLE_FLOOR, 100*FAR_SPECKLE_FRAC))
    return 0 if ok else 1

sys.exit(gate(sys.argv[1], sys.argv[2]))
PY

# Isolated AO-crawl temporal check (dam recorded ROI). 4 composite captures.
cat > "$TMP/temporal.py" <<'PY'
import sys, warnings
warnings.filterwarnings("ignore")
from PIL import Image
box = tuple(int(x) for x in sys.argv[1].split(","))   # X0,Y0,X1,Y1
v2a, offa, v2b, offb = (list(Image.open(p).convert("L").crop(box).getdata()) for p in sys.argv[2:6])
ao150 = [a-b for a, b in zip(v2a, offa)]
ao151 = [a-b for a, b in zip(v2b, offb)]
crawl = [abs(a-b) for a, b in zip(ao150, ao151)]
mx, mn = max(crawl), sum(crawl)/len(crawl)
ok = mx <= 2
print("  %s temporal AO crawl over %s : max=%d mean=%.3f  (<= 2/255)" % ("PASS" if ok else "FAIL", box, mx, mn))
sys.exit(0 if ok else 1)
PY

# Levels that have a recorded static-ground temporal ROI (T3): dam only.
temporal_roi() { case "$1" in dam) echo "240,264,320,300";; *) echo "";; esac; }

CHANGE_BUDGET_PCT="${GE007_SSAO_BUDGET_PCT:-12.0}"   # corrected §8 step-2 (v2-vs-off)

cap_scene() {  # $1=level $2=ssao(0/1) $3=out.bmp -> composite (no debug, no pan)
    local level="$1" ssao="$2" out="$3"; local dir; dir="$(dirname "$out")"; mkdir -p "$dir"
    ( cd "$dir" && env "${BASE_ENV[@]}" GE007_RENDERER=metal \
        "$BIN" --rom "$ROM" --level "$level" --deterministic \
        --config-override Video.RemasterFX=1 --config-override Video.Ssao="$ssao" \
        --config-override Video.SsaoMode=hemisphere --config-override Video.RenderScale=2 \
        --screenshot-frame "$FRAME" \
        --screenshot-label "$(basename "$out" .bmp | sed 's/^screenshot_//')" \
        --screenshot-exit >"$dir/log_$(basename "$out")" 2>&1 )
}

cap_ao() {  # $1=level $2=outdir -> AO-debug field screenshot at $2/screenshot_ao.bmp
    local level="$1" dir="$2"; mkdir -p "$dir"
    ( cd "$dir" && env "${BASE_ENV[@]}" GE007_RENDERER=metal GE007_SSAO_DEBUG=1 \
        "$BIN" --rom "$ROM" --level "$level" --deterministic "${V2_OVERRIDES[@]}" \
        --screenshot-frame "$FRAME" --screenshot-label ao --screenshot-exit >"$dir/log" 2>&1 )
}

cap_composite() {  # $1=level $2=frame $3=ssao(0/1) $4=outdir/label -> composite screenshot
    local level="$1" frame="$2" ssao="$3" out="$4"; local dir; dir="$(dirname "$out")"; mkdir -p "$dir"
    ( cd "$dir" && env "${BASE_ENV[@]}" GE007_RENDERER=metal \
        GE007_AUTO_LOOK_RIGHT=60:200 GE007_AUTO_LOOK_STEP=2 \
        "$BIN" --rom "$ROM" --level "$level" --deterministic \
        --config-override Video.RemasterFX=1 --config-override Video.Ssao="$ssao" \
        --config-override Video.SsaoMode=hemisphere --config-override Video.RenderScale=2 \
        --screenshot-frame "$frame" --screenshot-label "$(basename "$out" .bmp | sed 's/^screenshot_//')" \
        --screenshot-exit >"$dir/log_$(basename "$out")" 2>&1 )
}

echo "ssao-gate: hemisphere v2 (Metal), frame $FRAME, levels: ${LEVELS[*]}"
FAIL=0
for L in "${LEVELS[@]}"; do
    echo "--- $L ---"
    d="$TMP/$L"
    cap_ao "$L" "$d"
    bmp="$d/screenshot_ao.bmp"
    if [ ! -f "$bmp" ]; then
        echo "  FAIL $L — AO-debug capture produced no screenshot (see $d/log)"; FAIL=1; continue
    fi
    python3 "$TMP/gate.py" "$L" "$bmp" || FAIL=1

    # Feature change-budget: hemisphere-v2 vs SSAO-OFF composite <= 12% changed.
    bd="$d/budget"; mkdir -p "$bd"
    cap_scene "$L" 0 "$bd/screenshot_off.bmp"
    cap_scene "$L" 1 "$bd/screenshot_on.bmp"
    if python3 "$ROOT/tools/compare_screenshots.py" "$bd/screenshot_off.bmp" "$bd/screenshot_on.bmp" \
         --max-changed-pct "$CHANGE_BUDGET_PCT" >"$bd/cmp.log" 2>&1; then
        pct="$(grep -oE 'Changed pixels: [0-9]+/[0-9]+ \([0-9.]+%\)' "$bd/cmp.log" | grep -oE '[0-9.]+%' | head -1)"
        echo "  PASS $L      budget v2-vs-off changed=$pct (<= ${CHANGE_BUDGET_PCT}%)"
    else
        echo "  FAIL $L      budget v2-vs-off exceeds ${CHANGE_BUDGET_PCT}% (see $bd/cmp.log)"; FAIL=1
        grep -E 'Changed pixels|FAIL' "$bd/cmp.log" | sed 's/^/    /'
    fi

    troi="$(temporal_roi "$L")"
    if [ -n "$troi" ]; then
        td="$d/temporal"; mkdir -p "$td"
        cap_composite "$L" 150 1 "$td/screenshot_v2_150.bmp"
        cap_composite "$L" 150 0 "$td/screenshot_off_150.bmp"
        cap_composite "$L" 151 1 "$td/screenshot_v2_151.bmp"
        cap_composite "$L" 151 0 "$td/screenshot_off_151.bmp"
        if python3 "$TMP/temporal.py" "$troi" \
             "$td/screenshot_v2_150.bmp" "$td/screenshot_off_150.bmp" \
             "$td/screenshot_v2_151.bmp" "$td/screenshot_off_151.bmp"; then :; else FAIL=1; fi
    fi
done

if [ "$FAIL" -ne 0 ]; then
    echo "ssao-gate: FAIL — one or more ROI/temporal gates missed threshold." >&2
    exit 1
fi
echo "ssao-gate: PASS — all AO-field ROI (and temporal, where recorded) gates hold."
