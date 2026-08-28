#!/bin/bash
#
# w1_interaction_matrix.sh -- W1.E4.T2 lighting-feature interaction matrix.
#
# Loops the 8 combos of {GE007_ENV_SMOOTH_NORMALS, GE007_PERPIXEL_LIGHT,
# GE007_SSAO} x {0,1}, captures the §8 canonical Dam frame per combo (label =
# combo bits, e.g. m110 = ENV=1 PPL=1 SSAO=0), diffs each against the all-off
# baseline (m000), and reads mean luma from the compare JSON's mean_rgb field.
#
# The load-bearing assertion is "no double-darkening": E4 (per-pixel dFdx sun)
# supersedes E1 (CPU smooth-normal relight) for a draw, so turning E1 on on top
# of E4 must NOT darken the frame again. It asserts
#   | luma(E1+E4) - luma(E4 alone) | / luma(E4 alone) < 2%
# i.e. mean-luma(m110) vs mean-luma(m010). If E1 failed to defer under E4 the
# relight would apply twice and the frame would visibly darken (>2%).
#
# Screenshots are ROM-derived local validation data (Tier B) — never commit them.
# Metal is the capture backend (GL-over-Metal intermittently hangs on repeated
# headless captures; the interaction being tested is backend-agnostic).
#
set -euo pipefail
cd "$(dirname "$0")/.."

ROM="${GE007_ROM:-baserom.u.z64}"
BINARY="${GE007_BINARY:-build/ge007}"
LEVEL="${GE007_LEVEL:-dam}"
FRAME="${GE007_FRAME:-120}"
RENDERER="${GE007_RENDERER:-metal}"
THRESHOLD_PCT="2.0"          # double-darkening tolerance (must NOT be loosened)
JSON_DIR="$(mktemp -d "${TMPDIR:-/tmp}/w1_matrix.XXXXXX")"

if [[ ! -x "$BINARY" ]]; then
    echo "FAIL: binary not found/executable: $BINARY (build first, or set GE007_BINARY)" >&2
    exit 2
fi
if [[ ! -e "$ROM" ]]; then
    echo "FAIL: ROM not found: $ROM (symlink baserom.u.z64 or set GE007_ROM)" >&2
    exit 2
fi

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN="gtimeout"; fi

capture() {
    # $1=label $2=env_smooth $3=perpixel $4=ssao
    local label="$1" e1="$2" e4="$3" ssao="$4"
    rm -f "screenshot_${label}.bmp"
    local cmd=(env -u GE007_DEBUG \
        SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
        GE007_RENDERER="$RENDERER" \
        GE007_ENV_SMOOTH_NORMALS="$e1" GE007_PERPIXEL_LIGHT="$e4" GE007_SSAO="$ssao" \
        "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic \
        --trace-state "$JSON_DIR/${label}.jsonl" \
        --screenshot-frame "$FRAME" --screenshot-label "$label" --screenshot-exit)
    if [[ -n "$TIMEOUT_BIN" ]]; then
        "$TIMEOUT_BIN" --kill-after=5 200 "${cmd[@]}" >"$JSON_DIR/${label}.log" 2>&1
    else
        "${cmd[@]}" >"$JSON_DIR/${label}.log" 2>&1
    fi
    if [[ ! -s "screenshot_${label}.bmp" ]]; then
        echo "FAIL: missing screenshot for combo ${label}" >&2
        tail -20 "$JSON_DIR/${label}.log" | sed 's/^/  /' >&2
        exit 1
    fi
}

# mean luma (Rec.601) of the "test" image in a compare JSON's mean_rgb field.
mean_luma() {
    python3 - "$1" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
r, g, b = d["mean_rgb"]["test"]
print("%.4f" % (0.299 * r + 0.587 * g + 0.114 * b))
PY
}

echo "W1 lighting interaction matrix (level=$LEVEL frame=$FRAME renderer=$RENDERER)"
echo "combo bits = m<ENV_SMOOTH_NORMALS><PERPIXEL_LIGHT><SSAO>"

# Baseline (all off) first so every combo diffs against it.
capture m000 0 0 0

# Per-combo mean luma is stashed in "$JSON_DIR/<label>.luma" (bash 3.2 has no
# associative arrays), read back for the assertion below.
for e1 in 0 1; do
  for e4 in 0 1; do
    for ssao in 0 1; do
      label="m${e1}${e4}${ssao}"
      [[ "$label" == "m000" ]] || capture "$label" "$e1" "$e4" "$ssao"
      python3 tools/compare_screenshots.py "screenshot_m000.bmp" "screenshot_${label}.bmp" \
          --json-out "$JSON_DIR/${label}.json" >/dev/null 2>&1 || true
      lum="$(mean_luma "$JSON_DIR/${label}.json")"
      printf '%s' "$lum" > "$JSON_DIR/${label}.luma"
      printf 'PASS %s  mean_luma=%s\n' "$label" "$lum"
    done
  done
done

# --- Load-bearing assertion: E1+E4 must not double-darken vs E4 alone. ---
e4_alone="$(cat "$JSON_DIR/m010.luma")"      # ENV=0 PPL=1 SSAO=0
e1_plus_e4="$(cat "$JSON_DIR/m110.luma")"    # ENV=1 PPL=1 SSAO=0
delta_pct="$(python3 - "$e4_alone" "$e1_plus_e4" <<'PY'
import sys
a = float(sys.argv[1]); b = float(sys.argv[2])
print("%.4f" % (abs(b - a) / a * 100.0 if a > 0 else 0.0))
PY
)"

echo "---"
printf 'E4-alone (m010) mean_luma = %s ; E1+E4 (m110) mean_luma = %s ; delta = %s%% (threshold %s%%)\n' \
    "$e4_alone" "$e1_plus_e4" "$delta_pct" "$THRESHOLD_PCT"

if python3 -c "import sys; sys.exit(0 if float('$delta_pct') < float('$THRESHOLD_PCT') else 1)"; then
    echo "PASS no-double-darkening: E1 defers under E4 (delta ${delta_pct}% < ${THRESHOLD_PCT}%)"
    rm -f screenshot_m0*.bmp screenshot_m1*.bmp
    rm -rf "$JSON_DIR"
    exit 0
else
    echo "FAIL double-darkening detected: E1 did NOT defer under E4 (delta ${delta_pct}% >= ${THRESHOLD_PCT}%)"
    echo "  screenshots kept in CWD, JSON in $JSON_DIR"
    exit 1
fi
