#!/bin/bash
#
# ammo_hud_smoke.sh -- Validate the ammo HUD for every distinct ammo-icon type
# (backlog M2.6 / audit R6): per type, assert both the icon and the ammo digits
# render, on OpenGL and (on macOS) Metal, plus the icon-fault negative controls.
#
# Per ammo type, three deterministic Dam runs that differ in exactly one
# variable (see tools/audit_ammo_hud_capture.py for the isolation argument):
#   E   equip weapon, mag=3/reserve=12
#   E2  equip weapon, mag=8/reserve=25
#   F   equip weapon, mag=8/reserve=25, GE007_AMMO_ICON_FAULT=<type>
# icon  = diff(E2,F) >= min-icon-diff; digits = diff(E,E2) >= min-digit-diff.
# E/E2 must report zero hud_image_fault (audit_render_trace.py); F must report
# a nonzero counter and the [HUD][RENDER-HEALTH] log line.
#
# Global negative controls (run once, on the 9mm type):
#   digits-under-fault  diff(F(3,12), F(8,25)) >= min-digit-diff
#   invalid-entry       GE007_AMMO_ICON_FAULT_INVALID exercises
#                       portValidateImageEntry (poisoned dims/index); the
#                       placeholder must render and the log must say
#                       "image entry invalid".
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=60
OUT_DIR="/tmp/mgb64_ammo_hud_smoke_$$"
LEVEL=33
EQUIP_FRAME=60
AMMO_FRAME=80
FRAMES=150
MIN_ICON_DIFF=150
MIN_DIGIT_DIFF=40
BACKENDS="gl"
if [[ "$(uname -s)" == "Darwin" ]]; then
    BACKENDS="gl metal"
fi

# Every distinct ammo-icon type (portGetAmmoImage) with a weapon that selects
# it: AMMOTYPE:ITEM_ID:label. AMMO_9MM_2 has no icon by design and is excluded.
AMMO_COVERAGE="
1:4:9mm_pp7
3:8:rifle_kf7
4:15:shotgun
5:26:grenade
6:25:rocket
7:29:remote_mine
8:28:prox_mine
9:27:timed_mine
10:3:throwing_knife
11:24:grenade_round
12:18:magnum_ruger
13:19:golden_gun
28:32:tank_shell
"

usage() {
    cat <<'USAGE'
Usage: tools/ammo_hud_smoke.sh [options]

Options:
  --backends LIST       backends to run, quoted (default: "gl" or "gl metal" on macOS)
  --level N             raw LEVELID (default: 33 = Dam)
  --frames N            screenshot/exit frame (default: 150)
  --min-icon-diff N     minimum icon-vs-placeholder diff pixels (default: 150)
  --min-digit-diff N    minimum ammo-digit diff pixels (default: 40)
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --timeout SECONDS     per-run timeout (default: 60)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backends) BACKENDS="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --min-icon-diff) MIN_ICON_DIFF="$2"; shift 2 ;;
        --min-digit-diff) MIN_DIGIT_DIFF="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in LEVEL FRAMES TIMEOUT_SECONDS MIN_ICON_DIFF MIN_DIGIT_DIFF; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: ${value_name} must be a positive integer: $value" >&2
        exit 2
    fi
done
for backend in $BACKENDS; do
    if [[ "$backend" != "gl" && "$backend" != "metal" ]]; then
        echo "FAIL: unknown backend: $backend (expected gl or metal)" >&2
        exit 2
    fi
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

SUMMARY_FILE="$OUT_DIR/summary.tsv"
printf 'backend\tammo_type\tlabel\tstatus\ticon_diff\tdigit_diff\n' >"$SUMMARY_FILE"

FAILED=0

# run_capture LABEL BACKEND ITEM MAG RESERVE EXTRA_ENV...
run_capture() {
    local label="$1" backend="$2" item="$3" mag="$4" reserve="$5"
    shift 5
    local shot_label="ammo_hud_${label}_$$"
    # Empty value selects the default GL backend (gfx_backend.c matches the
    # literal "metal" only); avoids empty-array-under-set-u on macOS bash 3.2.
    local renderer=""
    if [[ "$backend" == "metal" ]]; then
        renderer="metal"
    fi

    rm -f "$OUT_DIR/${label}.log" "$OUT_DIR/${label}.jsonl" "$OUT_DIR/${label}.bmp"
    if ! (
        cd "$OUT_DIR"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
                GE007_MUTE=1 \
                GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 \
                GE007_NO_INPUT_GRAB=1 \
                GE007_DISABLE_LEVEL_INTRO=1 \
                GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
                GE007_AUTO_EQUIP_ITEM="$item" \
                GE007_AUTO_SET_HAND_AMMO_FRAME="$AMMO_FRAME" \
                GE007_AUTO_SET_HAND_AMMO_HAND=0 \
                GE007_AUTO_SET_HAND_AMMO_MAG="$mag" \
                GE007_AUTO_SET_HAND_AMMO_RESERVE="$reserve" \
                GE007_RENDERER="$renderer" \
                "$@" \
                "$BINARY" \
                --rom "$ROM" \
                --level "$LEVEL" \
                --deterministic \
                --trace-state "$OUT_DIR/${label}.jsonl" \
                --screenshot-frame "$FRAMES" \
                --screenshot-label "$shot_label" \
                --screenshot-exit
    ) >"$OUT_DIR/${label}.log" 2>&1; then
        echo "    run ${label}: FAIL (process)"
        tail -10 "$OUT_DIR/${label}.log" | sed 's/^/      /'
        return 1
    fi
    if [[ ! -f "$OUT_DIR/screenshot_${shot_label}.bmp" ]]; then
        echo "    run ${label}: FAIL (no screenshot)"
        return 1
    fi
    mv "$OUT_DIR/screenshot_${shot_label}.bmp" "$OUT_DIR/${label}.bmp"
    return 0
}

# expect_zero_faults LABEL — normal-run health: counter zero, no fault log.
expect_zero_faults() {
    local label="$1"
    if grep -qF "[HUD][RENDER-HEALTH]" "$OUT_DIR/${label}.log"; then
        echo "    health ${label}: FAIL (unexpected [HUD][RENDER-HEALTH] log)"
        return 1
    fi
    if ! python3 tools/audit_render_trace.py \
        --label "ammo HUD ${label}" \
        "$OUT_DIR/${label}.jsonl" >"$OUT_DIR/${label}.render.txt" 2>&1; then
        echo "    health ${label}: FAIL (render trace audit)"
        sed -n '1,8p' "$OUT_DIR/${label}.render.txt" | sed 's/^/      /'
        return 1
    fi
    return 0
}

# expect_faults LABEL PATTERN — fault-run health: counter nonzero + log line.
expect_faults() {
    local label="$1" pattern="$2"
    if ! grep -qF "$pattern" "$OUT_DIR/${label}.log"; then
        echo "    health ${label}: FAIL (missing log: $pattern)"
        return 1
    fi
    if ! python3 - "$OUT_DIR/${label}.jsonl" <<'PY'
import json, sys
peak = 0
with open(sys.argv[1], encoding="utf-8", errors="replace") as handle:
    for line in handle:
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        dl = record.get("dl")
        if isinstance(dl, dict) and isinstance(dl.get("hud_image_fault"), int):
            peak = max(peak, dl["hud_image_fault"])
raise SystemExit(0 if peak > 0 else 1)
PY
    then
        echo "    health ${label}: FAIL (hud_image_fault stayed zero)"
        return 1
    fi
    return 0
}

for backend in $BACKENDS; do
    echo "=== backend: $backend ==="
    for entry in $AMMO_COVERAGE; do
        ammo_type="${entry%%:*}"
        rest="${entry#*:}"
        item="${rest%%:*}"
        label="${rest#*:}"
        base="${backend}_${label}"
        echo "  [$backend] ammo type $ammo_type ($label, item $item)"

        ok=1
        run_capture "${base}_e" "$backend" "$item" 3 12 || ok=0
        [[ "$ok" -eq 1 ]] && { run_capture "${base}_e2" "$backend" "$item" 8 25 || ok=0; }
        [[ "$ok" -eq 1 ]] && { run_capture "${base}_f" "$backend" "$item" 8 25 \
            GE007_AMMO_ICON_FAULT="$ammo_type" || ok=0; }

        icon_diff="-"
        digit_diff="-"
        if [[ "$ok" -eq 1 ]]; then
            expect_zero_faults "${base}_e" || ok=0
            expect_zero_faults "${base}_e2" || ok=0
            expect_faults "${base}_f" "[HUD][RENDER-HEALTH]" || ok=0
        fi
        if [[ "$ok" -eq 1 ]]; then
            if python3 tools/audit_ammo_hud_capture.py \
                --label "$backend $label (ammo type $ammo_type)" \
                --shot-a "$OUT_DIR/${base}_e.bmp" \
                --shot-b "$OUT_DIR/${base}_e2.bmp" \
                --shot-fault "$OUT_DIR/${base}_f.bmp" \
                --min-icon-diff "$MIN_ICON_DIFF" \
                --min-digit-diff "$MIN_DIGIT_DIFF" \
                --json-out "$OUT_DIR/${base}.json" \
                >"$OUT_DIR/${base}.audit.txt" 2>&1; then
                icon_diff="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["icon"]["count"])' "$OUT_DIR/${base}.json")"
                digit_diff="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["digits"]["count"])' "$OUT_DIR/${base}.json")"
                sed -n '1p' "$OUT_DIR/${base}.audit.txt" | sed 's/^/    /'
            else
                ok=0
                sed -n '1,6p' "$OUT_DIR/${base}.audit.txt" | sed 's/^/    /'
            fi
        fi

        if [[ "$ok" -eq 1 ]]; then
            printf '%s\t%s\t%s\tpass\t%s\t%s\n' "$backend" "$ammo_type" "$label" "$icon_diff" "$digit_diff" >>"$SUMMARY_FILE"
        else
            printf '%s\t%s\t%s\tfail\t%s\t%s\n' "$backend" "$ammo_type" "$label" "$icon_diff" "$digit_diff" >>"$SUMMARY_FILE"
            FAILED=1
        fi
    done

    # Negative control: digits still render under an icon fault (9mm).
    echo "  [$backend] negative control: digits under fault"
    ok=1
    run_capture "${backend}_negctl_f2" "$backend" 4 3 12 GE007_AMMO_ICON_FAULT=1 || ok=0
    if [[ "$ok" -eq 1 ]]; then
        # ${backend}_9mm_pp7_f is the (8,25) fault run from the coverage loop.
        if ! python3 tools/audit_ammo_hud_capture.py \
            --label "$backend digits-under-fault" \
            --shot-a "$OUT_DIR/${backend}_negctl_f2.bmp" \
            --shot-b "$OUT_DIR/${backend}_9mm_pp7_f.bmp" \
            --shot-fault "$OUT_DIR/${backend}_9mm_pp7_e2.bmp" \
            --min-icon-diff "$MIN_ICON_DIFF" \
            --min-digit-diff "$MIN_DIGIT_DIFF" \
            --json-out "$OUT_DIR/${backend}_negctl.json" \
            >"$OUT_DIR/${backend}_negctl.audit.txt" 2>&1; then
            ok=0
            sed -n '1,6p' "$OUT_DIR/${backend}_negctl.audit.txt" | sed 's/^/    /'
        else
            sed -n '1p' "$OUT_DIR/${backend}_negctl.audit.txt" | sed 's/^/    /'
        fi
    fi
    if [[ "$ok" -eq 1 ]]; then
        printf '%s\t1\tnegctl_digits_under_fault\tpass\t-\t-\n' "$backend" >>"$SUMMARY_FILE"
    else
        printf '%s\t1\tnegctl_digits_under_fault\tfail\t-\t-\n' "$backend" >>"$SUMMARY_FILE"
        FAILED=1
    fi

    # Negative control: invalid-entry fault exercises portValidateImageEntry.
    echo "  [$backend] negative control: invalid image entry"
    ok=1
    run_capture "${backend}_negctl_inv" "$backend" 4 8 25 GE007_AMMO_ICON_FAULT_INVALID=1 || ok=0
    if [[ "$ok" -eq 1 ]]; then
        expect_faults "${backend}_negctl_inv" "image entry invalid" || ok=0
    fi
    if [[ "$ok" -eq 1 ]]; then
        if ! python3 tools/audit_ammo_hud_capture.py \
            --label "$backend invalid-entry placeholder" \
            --shot-a "$OUT_DIR/${backend}_9mm_pp7_e.bmp" \
            --shot-b "$OUT_DIR/${backend}_9mm_pp7_e2.bmp" \
            --shot-fault "$OUT_DIR/${backend}_negctl_inv.bmp" \
            --min-icon-diff "$MIN_ICON_DIFF" \
            --min-digit-diff "$MIN_DIGIT_DIFF" \
            --json-out "$OUT_DIR/${backend}_negctl_inv.json" \
            >"$OUT_DIR/${backend}_negctl_inv.audit.txt" 2>&1; then
            ok=0
            sed -n '1,6p' "$OUT_DIR/${backend}_negctl_inv.audit.txt" | sed 's/^/    /'
        else
            sed -n '1p' "$OUT_DIR/${backend}_negctl_inv.audit.txt" | sed 's/^/    /'
        fi
    fi
    if [[ "$ok" -eq 1 ]]; then
        printf '%s\t1\tnegctl_invalid_entry\tpass\t-\t-\n' "$backend" >>"$SUMMARY_FILE"
    else
        printf '%s\t1\tnegctl_invalid_entry\tfail\t-\t-\n' "$backend" >>"$SUMMARY_FILE"
        FAILED=1
    fi
done

echo ""
echo "=== summary ($SUMMARY_FILE) ==="
column -t <"$SUMMARY_FILE"

if [[ "$FAILED" -ne 0 ]]; then
    echo "FAIL: ammo HUD smoke found failures (artifacts in $OUT_DIR)"
    exit 1
fi
echo "PASS: ammo HUD smoke ($(echo "$BACKENDS" | wc -w | tr -d ' ') backend(s), 13 ammo-icon types each)"
