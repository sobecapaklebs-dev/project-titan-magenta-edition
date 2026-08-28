#!/usr/bin/env bash
#
# pack_qa.sh — in-game per-level QA gate for an HD texture pack (W2.E7.T2).
#
# The second (in-game) half of the pack QA harness described in
# docs/design/remaster-aaa/02-hd-asset-pipeline.md §4.8. validate_pack.py (W2.E7.T1) is the
# offline structural gate; this script boots the port headless + deterministic and
# proves, per level, that the pack:
#
#   (0) validate_pack   — offline structural pre-leg (E7.T1), when a manifest is available.
#   (a) identity (R3)   — pack OFF, run TWICE (base/base2); the two BMPs must be
#                         byte-identical. This is THE gate: it proves the baseline is
#                         deterministic and that "pack unset == stock, byte-for-byte".
#   (b) feature         — pack ON, once on the default (GL) backend and once on Metal
#                         (GE007_RENDERER=metal, macOS-only — SKIPs, never fails, elsewhere).
#   (c) health+diff     — audit_screenshot_health.py (non-black/non-garbage) + the
#                         compare_screenshots.py 60% ceiling (the tool's own exit 1).
#   (d) floor+tone      — computed FROM qa.json: changed_pct < 5 => "pack didn't load"
#                         (fail loudly); per-channel |mean_rgb.test-baseline| > 25 => tone drift.
#   (e) settex events   — grep the feature-leg stderr (GE007_TRACE_SETTEX=1) for
#                         [SETTEX-UPLOAD-FAIL]/[SETTEX-MISS] (gfx_log_settex_event, gfx_pc.c).
#   (f) render trace    — audit_render_trace.py trace_hd.jsonl (bad cmds / NaN / crashes).
#   (g) seam + perf     — a scripted close-up warp (GE007_AUTO_WARP_PAD) with a 3x3
#                         region grid (tile borders must not step) and the perf_census.sh
#                         single-level leg (pack-on mean work_ms <= 111% of pack-off).
#
# Output contract:  PACK_QA PASS level=<L>            (exit 0)
#                   PACK_QA FAIL level=<L> check=<name> (exit 1)
#                   PACK_QA ERROR level=<L> ...          (exit 2 — setup/env, e.g. GL hang)
#
# ROM / R2: every artifact this script produces (screenshots, traces, ge007.ini) is
# ROM-derived and lands in a throwaway $WORK dir that is removed on exit. Nothing it
# writes is ever committable. Screenshots are screenshot_<label>.bmp in the process CWD
# (platform_sdl.c), so all captures run cd'd into $WORK — the repo tree stays clean and
# no ge007.ini drifts into it (G2).
#
# Usage:
#   tools/texpack/pack_qa.sh --level dam --pack ~/ge007_hd [--manifest CSV] [--dump DIR]
#
# Env knobs (all optional):
#   PACK_QA_RENDERER=metal   force the PRIMARY captures onto Metal (G1: dodge GL hangs while
#                            iterating; the identity/floor results are byte-identical either way).
#   PACK_QA_DIFF_CEILING=60  max changed_pct for leg (c). The ceiling catches garbage frames
#                            (black screen, exploded geometry); a curated FULL-coverage pack
#                            (e.g. the Surface showcase: ground+banks+treelines+creek own
#                            ~70% of the spawn frame) legitimately exceeds the default —
#                            raise it deliberately, with the health/tone/trace legs as the
#                            real garbage guards.
#   PACK_QA_FRAME=300        screenshot frame (spec default 300).
#   PACK_QA_TIMEOUT=120      per-capture wall cap, seconds.
#   PACK_QA_WARP_PAD=N       hero-surface pad for the seam leg (default 0; empty => skip seam).
#   PACK_QA_SKIP_SEAM=1      skip the seam leg.
#   PACK_QA_SKIP_PERF=1      skip the perf leg.
#   GE007_BIN / GE007_ROM    override binary / ROM path.
#
set -u

# ---------------------------------------------------------------------------- args
LEVEL=""; PACK=""; MANIFEST=""; DUMP=""
while [ $# -gt 0 ]; do
  case "$1" in
    --level)    LEVEL="${2:-}"; shift 2 ;;
    --pack)     PACK="${2:-}"; shift 2 ;;
    --manifest) MANIFEST="${2:-}"; shift 2 ;;
    --dump)     DUMP="${2:-}"; shift 2 ;;
    -h|--help)  grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "pack_qa.sh: unknown arg '$1'" >&2; exit 2 ;;
  esac
done

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${GE007_BIN:-$REPO/build/ge007}"
ROM="${GE007_ROM:-$REPO/baserom.u.z64}"
FRAME="${PACK_QA_FRAME:-300}"
TIMEOUT_S="${PACK_QA_TIMEOUT:-120}"
DIFF_CEILING="${PACK_QA_DIFF_CEILING:-60}"
WARP_PAD="${PACK_QA_WARP_PAD-0}"          # default 0; set empty to skip seam

err()  { echo "PACK_QA ERROR level=${LEVEL:-?} $*"; exit 2; }
fail() { echo "PACK_QA FAIL level=$LEVEL check=$1"; exit 1; }

[ -n "$LEVEL" ] || err "missing --level"
[ -n "$PACK" ]  || err "missing --pack"
[ -x "$BIN" ]   || err "binary not built: $BIN (cmake --build build)"
[ -e "$ROM" ]   || err "ROM not found: $ROM (local R2 gate)"
[ -d "$PACK" ]  || err "pack dir not found: $PACK"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"; rm -f "$REPO"/screenshot_*.bmp' EXIT

# ------------------------------------------------------------------------- capture
# capture <label> <renderer|""> <env KEY=VAL...> — runs ge007 headless+deterministic,
# writing screenshot_<label>.bmp and trace_<label>.jsonl into $WORK and stderr into
# <label>.log. Retries a GL hang (G1) up to 3x; Metal never hangs.
CANON_ENV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
           GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)
capture() {
  local label="$1" renderer="$2"; shift 2
  local extra=("$@") tries=1 max=1
  [ -z "$renderer" ] && max=3            # only the (hang-prone) default GL path retries
  local renv=(); [ -n "$renderer" ] && renv=(GE007_RENDERER="$renderer")
  while [ "$tries" -le "$max" ]; do
    rm -f "$WORK/screenshot_$label.bmp"
    ( cd "$WORK" && env "${CANON_ENV[@]}" "${renv[@]}" "${extra[@]}" \
        "$BIN" --rom "$ROM" --level "$LEVEL" --deterministic --background --no-input-grab \
        --config-override Video.RenderScale=1 --config-override Video.MSAA=0 \
        --screenshot-frame "$FRAME" --screenshot-label "$label" --screenshot-exit \
        --trace-state "$WORK/trace_$label.jsonl" >"$WORK/$label.log" 2>&1 ) &
    local pid=$! waited=0 timedout=0
    while kill -0 "$pid" 2>/dev/null; do
      sleep 1; waited=$((waited+1))
      if [ "$waited" -ge "$TIMEOUT_S" ]; then kill "$pid" 2>/dev/null; timedout=1; break; fi
    done
    wait "$pid" 2>/dev/null
    if [ "$timedout" -eq 0 ] && [ -s "$WORK/screenshot_$label.bmp" ]; then return 0; fi
    tries=$((tries+1))
  done
  return 1
}

# JSON field reader (python3 is a hard dep of every sibling tool).
jget() { python3 -c "import json,sys;print(eval('json.load(open(sys.argv[1]))'+sys.argv[2]))" "$1" "$2"; }

echo "=== pack_qa level=$LEVEL pack=$PACK frame=$FRAME primary=${PACK_QA_RENDERER:-gl} ==="

# ------------------------------------------------------- (0) validate_pack pre-leg
[ -z "$MANIFEST" ] && [ -f "$PACK/ge007.texmanifest.csv" ] && MANIFEST="$PACK/ge007.texmanifest.csv"
if [ -n "$MANIFEST" ] && [ -f "$MANIFEST" ]; then
  vargs=(--pack "$PACK" --manifest "$MANIFEST")
  [ -n "$DUMP" ] && vargs+=(--dump "$DUMP")
  echo "-- (0) validate_pack.py (structural)"
  if ! python3 "$REPO/tools/texpack/validate_pack.py" "${vargs[@]}"; then
    fail validate_pack
  fi
else
  echo "-- (0) validate_pack.py SKIP (no manifest; ROM-derived/local — pass --manifest to run)"
fi

# --------------------------------------------------------------- (a) identity (R3)
echo "-- (a) identity (pack OFF, base vs base2)"
capture base  "${PACK_QA_RENDERER:-}" || err "identity: base capture failed/timed out (G1? try PACK_QA_RENDERER=metal)"
capture base2 "${PACK_QA_RENDERER:-}" || err "identity: base2 capture failed/timed out (G1?)"
if cmp -s "$WORK/screenshot_base.bmp" "$WORK/screenshot_base2.bmp"; then
  echo "   identity: base == base2 (byte-identical)"
else
  fail identity
fi

# ----------------------------------------------------------------- (b) feature GL
echo "-- (b) feature (pack ON): default backend"
capture hd "${PACK_QA_RENDERER:-}" GE007_TEXTURE_PACK="$PACK" GE007_TRACE_SETTEX=1 \
  || err "feature: hd capture failed/timed out (G1? try PACK_QA_RENDERER=metal)"

# ----------------------------------------------------------- (b) feature Metal leg
if [ "$(uname)" = "Darwin" ]; then
  echo "-- (b) feature (pack ON): Metal mirror (GE007_RENDERER=metal)"
  if capture hd_metal metal GE007_TEXTURE_PACK="$PACK" GE007_TRACE_SETTEX=1; then
    if ! python3 "$REPO/tools/audit_screenshot_health.py" "$WORK/screenshot_hd_metal.bmp" >/dev/null; then
      fail metal_health
    fi
    echo "   metal: renders pack, screenshot healthy"
  else
    err "metal feature capture failed"
  fi
else
  echo "-- (b) Metal mirror SKIP (macOS-only; gfx_backend.h)"
fi

# ---------------------------------------------------------- (c) health + ceiling
echo "-- (c) health + diff ceiling"
python3 "$REPO/tools/audit_screenshot_health.py" "$WORK/screenshot_hd.bmp" >/dev/null \
  || fail screenshot_health
python3 "$REPO/tools/compare_screenshots.py" \
  "$WORK/screenshot_base.bmp" "$WORK/screenshot_hd.bmp" \
  --max-changed-pct "$DIFF_CEILING" --json-out "$WORK/qa.json" >"$WORK/compare.log" 2>&1
cmp_exit=$?
[ -f "$WORK/qa.json" ] || err "compare produced no qa.json ($(tail -1 "$WORK/compare.log"))"
[ "$cmp_exit" -eq 1 ] && fail diff_ceiling            # tool enforces >60% => exit 1
[ "$cmp_exit" -ge 2 ] && err "compare error: $(tail -1 "$WORK/compare.log")"

# ------------------------------------------------------------- (d) floor + tone
changed_pct="$(jget "$WORK/qa.json" "['changed_pct']")"
echo "   changed_pct=$changed_pct (ceiling $DIFF_CEILING)"
below="$(python3 -c "print(1 if $changed_pct < 5 else 0)")"
if [ "$below" = "1" ]; then
  echo "   floor: pack didn't load — changed_pct=$changed_pct < 5"
  fail pack_didnt_load
fi
tone="$(python3 -c "
import json
d=json.load(open('$WORK/qa.json'))['mean_rgb']
b,t=d['baseline'],d['test']
worst=max(abs(t[i]-b[i]) for i in range(3))
print('%.2f %d'%(worst, 1 if worst>25 else 0))
")"
echo "   tone: worst per-channel |mean delta|=${tone% *} (limit 25)"
[ "${tone#* }" = "1" ] && fail tone_drift

# --------------------------------------------------------- (e) settex fail events
echo "-- (e) settex events"
if grep -q '\[SETTEX-UPLOAD-FAIL\]\|\[SETTEX-MISS\]' "$WORK/hd.log"; then
  grep -o '\[SETTEX-[A-Z-]*\]' "$WORK/hd.log" | sort | uniq -c | sed 's/^/     /'
  fail settex_events
fi
echo "   settex: no UPLOAD-FAIL / MISS events"

# --------------------------------------------------------------- (f) render trace
echo "-- (f) render-trace audit"
python3 "$REPO/tools/audit_render_trace.py" "$WORK/trace_hd.jsonl" >/dev/null \
  || fail render_trace
echo "   trace: clean (no bad cmds / NaN / crashes)"

# -------------------------------------------------------------------- (g) seam A/B
if [ -n "$WARP_PAD" ] && [ -z "${PACK_QA_SKIP_SEAM:-}" ]; then
  echo "-- (g) seam A/B (warp pad=$WARP_PAD, 3x3 grid)"
  if capture seam_base "${PACK_QA_RENDERER:-}" GE007_AUTO_WARP_PAD="$WARP_PAD" \
     && capture seam_hd "${PACK_QA_RENDERER:-}" GE007_AUTO_WARP_PAD="$WARP_PAD" GE007_TEXTURE_PACK="$PACK"; then
    # 3x3 region grid from the image dimensions.
    read -r SW SH < <(python3 -c "from PIL import Image;i=Image.open('$WORK/screenshot_seam_base.bmp');print(i.width,i.height)")
    regions=()
    for gy in 0 1 2; do for gx in 0 1 2; do
      rx=$((SW*gx/3)); ry=$((SH*gy/3)); rw=$((SW/3)); rh=$((SH/3))
      regions+=(--region "r${gx}${gy}:${rx},${ry},${rw},${rh}")
    done; done
    python3 "$REPO/tools/compare_screenshots.py" \
      "$WORK/screenshot_seam_base.bmp" "$WORK/screenshot_seam_hd.bmp" \
      "${regions[@]}" --json-out "$WORK/seam.json" >"$WORK/seam.log" 2>&1
    seam_exit=$?
    [ "$seam_exit" -ge 2 ] && fail seam           # OOB region / error
    # Outlier heuristic: a single region changing far more than its neighbours is a
    # visible tile step. Flag when the worst region is both over the ceiling AND a
    # >2.5x outlier vs the median region (conservative — full-scene HD lifts all cells).
    seam_bad="$(python3 -c "
import json,statistics
r=json.load(open('$WORK/seam.json'))['regions']
p=sorted(x['changed_pct'] for x in r)
med=statistics.median(p) if p else 0.0
worst=p[-1] if p else 0.0
print('%.1f %.1f %d'%(worst,med,1 if (worst>60 and worst>2.5*max(med,1e-6)) else 0))
")"
    echo "   seam: worst region=${seam_bad%% *}% median=$(echo "$seam_bad"|cut -d' ' -f2)% (outlier gate)"
    [ "${seam_bad##* }" = "1" ] && fail seam
  else
    echo "   seam: SKIP (warp capture unavailable)"
  fi
else
  echo "-- (g) seam A/B SKIP (set PACK_QA_WARP_PAD to a hero pad to enable)"
fi

# -------------------------------------------------------------------- (g) perf leg
if [ -z "${PACK_QA_SKIP_PERF:-}" ]; then
  echo "-- (g) perf (pack-on mean work_ms <= 111% of off)"
  ( cd "$WORK" && CENSUS_OUT="$WORK/perf_off.csv" BIN="$BIN" \
      "$REPO/tools/perf_census.sh" "$LEVEL" >/dev/null 2>&1 )
  ( cd "$WORK" && CENSUS_OUT="$WORK/perf_on.csv" BIN="$BIN" GE007_TEXTURE_PACK="$PACK" \
      "$REPO/tools/perf_census.sh" "$LEVEL" >/dev/null 2>&1 )
  perf="$(python3 -c "
import csv
def ms(p):
    try:
        row=next(r for r in csv.DictReader(open(p)) if r['level']=='$LEVEL')
        return float(row['default_ms'])
    except Exception: return None
off,on=ms('$WORK/perf_off.csv'),ms('$WORK/perf_on.csv')
if off is None or on is None or off<=0:
    print('NA NA NA 0');
else:
    print('%.2f %.2f %.1f %d'%(off,on,100*on/off,1 if on>1.11*off else 0))
")"
  set -- $perf
  if [ "$1" = "NA" ]; then
    echo "   perf: SKIP (no work_ms samples — GUI/timeout)"
  else
    echo "   perf: off=${1}ms on=${2}ms (${3}% of off, limit 111%)"
    [ "$4" = "1" ] && fail perf
  fi
else
  echo "-- (g) perf leg SKIP (PACK_QA_SKIP_PERF)"
fi

echo "PACK_QA PASS level=$LEVEL"
exit 0
