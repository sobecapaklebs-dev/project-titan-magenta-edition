#!/bin/bash
#
# movement_oracle_capture.sh -- Capture and compare ROM-oracle traces.
#
# Native captures are self-contained. Stock-ROM captures require an instrumented
# ares binary prepared with tools/prepare_ares_movement_oracle_build.sh.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ROUTE="dam_forward_stop"
OUT_DIR="/tmp/mgb64_movement_oracle_$$"
DO_BUILD=1
TIMEOUT_SECONDS=60
ARES_BIN=""
STOCK_TRACE=""
NATIVE_ONLY=0
NO_COMPARE=0
NATIVE_FULL_TRACE=0
COMPARE_ALIGN=""
SEED_EEPROM=""
EXTRA_NATIVE_CONFIG_OVERRIDES=()
# FID-0135: every native-vs-retail oracle shares the retail 4:3 presentation
# boundary. Keep these last in the override order so a route or ad-hoc A/B
# cannot silently turn the screenshot (and aspect-coupled sim) widescreen.
FIDELITY_CAPTURE_CONFIG_OVERRIDES=(
    "Video.WindowWidth=640"
    "Video.WindowHeight=480"
    "Video.WindowMode=windowed"
    "Video.HiDPI=0"
)

usage() {
    cat <<'USAGE'
Usage: tools/movement_oracle_capture.sh [options]

Options:
  --route NAME|PATH     route spec (default: dam_forward_stop)
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --ares-bin PATH       instrumented ares binary for stock-ROM capture
  --stock-trace PATH    compare against an existing stock/emulator JSONL trace
  --seed-eeprom PATH    copy this EEPROM save image (e.g. one built by
                       tools/make_unlocked_eeprom.py) into the ares saves
                       directory before the stock capture boots, so its
                       frontend menu navigation can reach missions beyond
                       Dam (a fresh save only unlocks mission 1). No-op
                       without --ares-bin.
  --native-only         only capture native trace
  --no-compare          capture traces but do not run the route comparator
  --native-full-trace   emit the full native --trace-state record (incl. the
                       combat_oracle namespace) instead of the slim flow-only
                       movement trace; required for combat/floor oracle capture
                       (FID-0032). The ares tracer always emits combat_oracle.
  --align MODE          comparator alignment (default: route compare_align)
  --native-config-override KEY=VALUE
                       append a native --config-override after route config;
                       may be repeated for renderer/config A/B probes
  --timeout SECONDS     per-process timeout (default: 60)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, saves, or emulator output.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --route) ROUTE="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --stock-trace) STOCK_TRACE="$2"; shift 2 ;;
        --seed-eeprom) SEED_EEPROM="$2"; shift 2 ;;
        --native-only) NATIVE_ONLY=1; shift ;;
        --no-compare) NO_COMPARE=1; shift ;;
        --native-full-trace) NATIVE_FULL_TRACE=1; shift ;;
        --align) COMPARE_ALIGN="$2"; shift 2 ;;
        --native-config-override) EXTRA_NATIVE_CONFIG_OVERRIDES+=("$2"); shift 2 ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
if [[ -n "$ARES_BIN" ]]; then
    validation_require_binary "$ARES_BIN"
fi
if [[ -n "$STOCK_TRACE" ]]; then
    validation_require_file "$STOCK_TRACE" "stock trace"
fi
if [[ -n "$SEED_EEPROM" ]]; then
    validation_require_file "$SEED_EEPROM" "seed EEPROM image"
fi

ROUTE_PATH="$(python3 tools/rom_oracle_route.py resolve "$ROUTE")"
python3 tools/rom_oracle_route.py validate "$ROUTE_PATH"
ROUTE_NAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" name)"
LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" level)"
NATIVE_LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_level)"
STOCK_LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_level)"
NATIVE_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_frames)"
STOCK_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_frames)"
STOCK_SCREENSHOT_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_screenshot_frame)"
NATIVE_SCREENSHOT_GAME_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_screenshot_game_timer)"
STOCK_SCREENSHOT_GAME_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_screenshot_game_timer)"
NATIVE_SPEEDFRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_speedframes)"
STOCK_SPEEDFRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_speedframes)"
STOCK_GAMEPLAY_START_GLOBAL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_gameplay_start_global)"
STOCK_MENU_CLOSE_ON_PLAYER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_menu_close_on_player)"
NATIVE_RENDER_AUDIT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_render_audit)"
NATIVE_MENU_BOOT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_menu_boot)"
NATIVE_MIN_MOVING_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_min_moving_records)"
STOCK_MIN_MOVING_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_moving_records)"
STOCK_MIN_GAMEPLAY_INPUT_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_gameplay_input_records)"
STOCK_MAX_SUPPRESSED_MENU_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_max_suppressed_menu_records)"
STOCK_MIN_MENU_TO_GAMEPLAY_GAP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_menu_to_gameplay_gap)"
STOCK_MIN_FORCE_PLAYER_APPLIES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_force_player_applies)"
STOCK_MIN_FORCE_PLAYER_STAN_APPLIES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_force_player_stan_applies)"
STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_require_first_gameplay_global)"
STOCK_REQUIRE_NATIVE_SELECTED_CAMERA="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_require_native_selected_camera)"
if [[ -z "$COMPARE_ALIGN" ]]; then
    COMPARE_ALIGN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_align)"
fi
COMPARE_PROFILE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_profile)"
COMPARE_KIND="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_kind)"
COMPARE_ACTOR_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_actor_frame)"
COMPARE_REQUIRE_HEALTH_MATCH="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_health_match)"
COMPARE_HEALTH_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_health_tolerance)"
COMPARE_DAMAGE_SHOW_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_damage_show_tolerance)"
COMPARE_MAX_ALIGNED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_max_aligned)"
COMPARE_MIN_ALIGNED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_min_aligned)"
COMPARE_REQUIRE_ACTIVE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_active)"
COMPARE_REQUIRE_HASH_MATCH="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_hash_match)"
COMPARE_FIRST_ACTIVE_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_first_active_tolerance)"
COMPARE_MAX_ACTIVE_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_max_active_tolerance)"
COMPARE_FIRST_POSITION_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_first_position_tolerance)"
COMPARE_FIRST_SAMPLE_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_first_sample_tolerance)"
COMPARE_REQUIRE_PROP_DESTROYED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_prop_destroyed)"
COMPARE_REQUIRE_IMPACT_ACTIVE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_impact_active)"
COMPARE_REQUIRE_IMPACT_MATCH="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_impact_match)"
COMPARE_IMPACT_POSITION_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_impact_position_tolerance)"
COMPARE_IMPACT_POSITION_POINTS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_impact_position_points)"
COMPARE_PROP_POSITION_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_prop_position_tolerance)"
COMPARE_MAX_BUFFER_LEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_max_buffer_len)"
COMPARE_NORMALIZE_POSITION="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_normalize_position)"
COMPARE_GAMEPLAY_WINDOWS="$(python3 tools/rom_oracle_route.py gameplay-windows "$ROUTE_PATH")"
COMPARE_CAMERA_MODES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_camera_modes)"
COMPARE_START_ACTIVE_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_start_active_frame)"
COMPARE_START_INTRO_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_start_intro_timer)"
COMPARE_END_INTRO_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_end_intro_timer)"
COMPARE_SAMPLE_STEP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_sample_step)"
COMPARE_VECTOR_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_vector_tolerance)"
COMPARE_DIRECTION_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_direction_tolerance)"
COMPARE_SCALAR_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_scalar_tolerance)"
COMPARE_ANIM_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_anim_tolerance)"
COMPARE_STATE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_state)"
COMPARE_SELECTED_CAMERA="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_selected_camera)"
COMPARE_INTRO_SETUP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_intro_setup)"
COMPARE_BOND_ANIM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_bond_anim)"
COMPARE_BOND_ANIM_ONSET_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_bond_anim_onset_tolerance)"
COMPARE_BOND_IDLE_ONSET_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_bond_idle_onset_tolerance)"
COMPARE_EXCLUDE_FIELDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_exclude_fields)"
COMPARE_REQUIRE_FROZEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_frozen)"
COMPARE_EXPECT_MODE_DURATIONS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_expect_mode_durations)"
COMPARE_WAIVERS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_waivers)"
NATIVE_INTRO_AUDIT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_audit)"
NATIVE_INTRO_CAMERA_MODES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_camera_modes)"
NATIVE_INTRO_REQUIRE_FROZEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_frozen)"
NATIVE_INTRO_REQUIRE_PLAYER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_player)"
NATIVE_INTRO_REQUIRE_BOND_PRESENT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_present)"
NATIVE_INTRO_REQUIRE_BOND_ONSCREEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_onscreen)"
NATIVE_INTRO_REQUIRE_BOND_MODEL_MTX="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_model_mtx)"
NATIVE_INTRO_REQUIRE_BOND_RENDERED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_rendered)"
NATIVE_INTRO_REQUIRE_BOND_ANIM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_anim)"
NATIVE_INTRO_REQUIRE_BOND_ANIM_HASH="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_anim_hash)"
NATIVE_INTRO_REQUIRE_H17_SWIRL_FACING="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_h17_swirl_facing)"
NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_right_item)"
NATIVE_INTRO_MIN_ACTIVE_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_active_records)"
NATIVE_INTRO_MIN_PRESENT_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_present_frames)"
NATIVE_INTRO_MIN_ONSCREEN_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_onscreen_frames)"
NATIVE_INTRO_MIN_MODEL_MTX_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_model_mtx_frames)"
NATIVE_INTRO_MIN_RENDERED_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_rendered_frames)"
NATIVE_INTRO_MIN_RENDER_COUNT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_render_count)"
NATIVE_INTRO_MIN_ANIM_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_frames)"
NATIVE_INTRO_MIN_ANIM_HASH_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_hash_frames)"
NATIVE_INTRO_MIN_ANIM_ADVANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_advance)"
NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_right_item_frames)"
NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_max_first_present_frame)"
NATIVE_INTRO_MAX_FIRST_RENDER_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_max_first_render_frame)"

for value_name in LEVEL NATIVE_LEVEL STOCK_LEVEL; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: route ${value_name} must be an integer LEVELID: $value" >&2
        exit 2
    fi
done
if [[ ! "$NATIVE_FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route native_frames must be a positive integer: $NATIVE_FRAMES" >&2
    exit 2
fi
if [[ ! "$STOCK_FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_frames must be a positive integer: $STOCK_FRAMES" >&2
    exit 2
fi
if [[ ! "$STOCK_SCREENSHOT_FRAME" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_screenshot_frame must be a positive integer: $STOCK_SCREENSHOT_FRAME" >&2
    exit 2
fi
if [[ -n "$NATIVE_SCREENSHOT_GAME_TIMER" && ! "$NATIVE_SCREENSHOT_GAME_TIMER" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route native_screenshot_game_timer must be a positive integer when set: $NATIVE_SCREENSHOT_GAME_TIMER" >&2
    exit 2
fi
if [[ -n "$STOCK_SCREENSHOT_GAME_TIMER" && ! "$STOCK_SCREENSHOT_GAME_TIMER" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_screenshot_game_timer must be a positive integer when set: $STOCK_SCREENSHOT_GAME_TIMER" >&2
    exit 2
fi
if [[ -n "$NATIVE_SPEEDFRAMES" && ! "$NATIVE_SPEEDFRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route native_speedframes must be a positive integer when set: $NATIVE_SPEEDFRAMES" >&2
    exit 2
fi
if [[ -n "$STOCK_SPEEDFRAMES" && ! "$STOCK_SPEEDFRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_speedframes must be a positive integer when set: $STOCK_SPEEDFRAMES" >&2
    exit 2
fi
if [[ -n "$STOCK_GAMEPLAY_START_GLOBAL" && ! "$STOCK_GAMEPLAY_START_GLOBAL" =~ ^-?[0-9]+$ ]]; then
    echo "FAIL: route stock_gameplay_start_global must be an integer when set: $STOCK_GAMEPLAY_START_GLOBAL" >&2
    exit 2
fi
for value_name in \
    NATIVE_MIN_MOVING_RECORDS \
    STOCK_MIN_MOVING_RECORDS \
    STOCK_MIN_GAMEPLAY_INPUT_RECORDS \
    STOCK_MAX_SUPPRESSED_MENU_RECORDS \
    STOCK_MIN_MENU_TO_GAMEPLAY_GAP \
    STOCK_MIN_FORCE_PLAYER_APPLIES \
    STOCK_MIN_FORCE_PLAYER_STAN_APPLIES \
    STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL
do
    value="${!value_name}"
    if [[ -n "$value" && ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: route ${value_name} must be a non-negative integer when set: $value" >&2
        exit 2
    fi
done
for value_name in \
    NATIVE_MIN_MOVING_RECORDS \
    STOCK_MIN_MOVING_RECORDS \
    STOCK_MIN_GAMEPLAY_INPUT_RECORDS
do
    value="${!value_name}"
    if [[ "$value" == "0" ]]; then
        echo "FAIL: route ${value_name} must be positive when set: $value" >&2
        exit 2
    fi
done
case "$STOCK_MENU_CLOSE_ON_PLAYER" in
    1|true|True|TRUE|yes|YES|on|ON) STOCK_MENU_CLOSE_ON_PLAYER=1 ;;
    0|false|False|FALSE|no|NO|off|OFF) STOCK_MENU_CLOSE_ON_PLAYER=0 ;;
    *)
        echo "FAIL: route stock_menu_close_on_player must be boolean: $STOCK_MENU_CLOSE_ON_PLAYER" >&2
        exit 2
        ;;
esac
case "$STOCK_REQUIRE_NATIVE_SELECTED_CAMERA" in
    1|true|True|TRUE|yes|YES|on|ON) STOCK_REQUIRE_NATIVE_SELECTED_CAMERA=1 ;;
    ""|0|false|False|FALSE|no|NO|off|OFF) STOCK_REQUIRE_NATIVE_SELECTED_CAMERA=0 ;;
    *)
        echo "FAIL: route stock_require_native_selected_camera must be boolean: $STOCK_REQUIRE_NATIVE_SELECTED_CAMERA" >&2
        exit 2
        ;;
esac
case "$COMPARE_KIND" in
    movement|intro|visual|glass) ;;
    *)
        echo "FAIL: route compare_kind must be movement, intro, visual, or glass: $COMPARE_KIND" >&2
        exit 2
        ;;
esac
if [[ "$COMPARE_KIND" == "movement" ]]; then
    case "$COMPARE_ALIGN" in
        global|frame|index|move|move-global|gameplay-frame) ;;
        *)
            echo "FAIL: movement --align must be global, frame, index, move, move-global, or gameplay-frame: $COMPARE_ALIGN" >&2
            exit 2
            ;;
    esac
    case "$COMPARE_PROFILE" in
        full|dynamics|scalar-speed|timing) ;;
        *)
            echo "FAIL: movement compare_profile must be full, dynamics, scalar-speed, or timing: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
elif [[ "$COMPARE_KIND" == "intro" ]]; then
    case "$COMPARE_ALIGN" in
    active-index|global|frame|intro-timer|per-mode) ;;
        *)
        echo "FAIL: intro --align must be active-index, global, frame, intro-timer, or per-mode: $COMPARE_ALIGN" >&2
        exit 2
        ;;
esac
    case "$COMPARE_PROFILE" in
        path|scalar|state|full) ;;
        *)
            echo "FAIL: intro compare_profile must be path, scalar, state, or full: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
elif [[ "$COMPARE_KIND" == "visual" ]]; then
    case "$COMPARE_ALIGN" in
        global|frame|index) ;;
        *)
            echo "FAIL: visual --align must be global, frame, or index: $COMPARE_ALIGN" >&2
            exit 2
            ;;
    esac
    case "$COMPARE_PROFILE" in
        full|screenshot|active-normalized|logical-viewport) ;;
        *)
            echo "FAIL: visual compare_profile must be full, screenshot, active-normalized, or logical-viewport: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
else
    case "$COMPARE_ALIGN" in
        global|frame|index) ;;
        *)
            echo "FAIL: glass --align must be global, frame, or index: $COMPARE_ALIGN" >&2
            exit 2
            ;;
    esac
    case "$COMPARE_PROFILE" in
        full|state|shatter-state) ;;
        *)
            echo "FAIL: glass compare_profile must be full, state, or shatter-state: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
fi
if [[ -n "$COMPARE_MAX_ALIGNED" && ! "$COMPARE_MAX_ALIGNED" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_max_aligned must be a positive integer when set: $COMPARE_MAX_ALIGNED" >&2
    exit 2
fi
if [[ -n "$COMPARE_MIN_ALIGNED" && ! "$COMPARE_MIN_ALIGNED" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_min_aligned must be a positive integer when set: $COMPARE_MIN_ALIGNED" >&2
    exit 2
fi
for value_name in \
    COMPARE_FIRST_ACTIVE_TOLERANCE \
    COMPARE_MAX_ACTIVE_TOLERANCE
do
    value="${!value_name}"
    if [[ -n "$value" && ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: route ${value_name} must be a non-negative integer when set: $value" >&2
        exit 2
    fi
done
if [[ -n "$COMPARE_MAX_BUFFER_LEN" && ! "$COMPARE_MAX_BUFFER_LEN" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route COMPARE_MAX_BUFFER_LEN must be a positive integer when set: $COMPARE_MAX_BUFFER_LEN" >&2
    exit 2
fi
if [[ -n "$COMPARE_FIRST_POSITION_TOLERANCE" ]]; then
    python3 - "$COMPARE_FIRST_POSITION_TOLERANCE" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    print(f"FAIL: route COMPARE_FIRST_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
if value <= 0.0:
    print(f"FAIL: route COMPARE_FIRST_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
PY
fi
if [[ -n "$COMPARE_PROP_POSITION_TOLERANCE" ]]; then
    python3 - "$COMPARE_PROP_POSITION_TOLERANCE" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    print(f"FAIL: route COMPARE_PROP_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
if value <= 0.0:
    print(f"FAIL: route COMPARE_PROP_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
PY
fi
if [[ -n "$COMPARE_IMPACT_POSITION_TOLERANCE" ]]; then
    python3 - "$COMPARE_IMPACT_POSITION_TOLERANCE" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    print(f"FAIL: route COMPARE_IMPACT_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
if value <= 0.0:
    print(f"FAIL: route COMPARE_IMPACT_POSITION_TOLERANCE must be a positive number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
PY
fi
if [[ -n "$COMPARE_FIRST_SAMPLE_TOLERANCE" ]]; then
    python3 - "$COMPARE_FIRST_SAMPLE_TOLERANCE" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    print(f"FAIL: route COMPARE_FIRST_SAMPLE_TOLERANCE must be a non-negative number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
if value < 0.0:
    print(f"FAIL: route COMPARE_FIRST_SAMPLE_TOLERANCE must be a non-negative number when set: {sys.argv[1]}", file=sys.stderr)
    raise SystemExit(2)
PY
fi
if [[ -n "$COMPARE_START_ACTIVE_FRAME" && ! "$COMPARE_START_ACTIVE_FRAME" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_start_active_frame must be a positive integer when set: $COMPARE_START_ACTIVE_FRAME" >&2
    exit 2
fi
if [[ -n "$COMPARE_SAMPLE_STEP" && ! "$COMPARE_SAMPLE_STEP" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_sample_step must be a positive integer when set: $COMPARE_SAMPLE_STEP" >&2
    exit 2
fi
for value_name in \
    NATIVE_INTRO_MIN_ACTIVE_RECORDS \
    NATIVE_INTRO_MIN_PRESENT_FRAMES \
    NATIVE_INTRO_MIN_ONSCREEN_FRAMES \
    NATIVE_INTRO_MIN_MODEL_MTX_FRAMES \
    NATIVE_INTRO_MIN_RENDERED_FRAMES \
    NATIVE_INTRO_MIN_RENDER_COUNT \
    NATIVE_INTRO_MIN_ANIM_FRAMES \
    NATIVE_INTRO_MIN_ANIM_HASH_FRAMES \
    NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES \
    NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME \
    NATIVE_INTRO_MAX_FIRST_RENDER_FRAME
do
    value="${!value_name}"
    if [[ -n "$value" && ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: route ${value_name} must be a positive integer when set: $value" >&2
        exit 2
    fi
done
if [[ -n "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" && ! "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" =~ ^[0-9]+$ ]]; then
    echo "FAIL: route native_intro_require_bond_right_item must be a non-negative integer when set: $NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" >&2
    exit 2
fi
if [[ -n "$NATIVE_INTRO_MIN_ANIM_ADVANCE" && ! "$NATIVE_INTRO_MIN_ANIM_ADVANCE" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "FAIL: route native_intro_min_anim_advance must be a positive number when set: $NATIVE_INTRO_MIN_ANIM_ADVANCE" >&2
    exit 2
fi

if [[ "$STOCK_FRAMES" -lt "$NATIVE_FRAMES" ]]; then
    echo "FAIL: route stock_frames must be >= native_frames" >&2
    exit 2
fi
if [[ "$STOCK_SCREENSHOT_FRAME" -gt "$STOCK_FRAMES" ]]; then
    echo "FAIL: route stock_screenshot_frame must be <= stock_frames" >&2
    exit 2
fi

mkdir -p "$OUT_DIR"
OUT_DIR="$(python3 - "$OUT_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"

NATIVE_TRACE="$OUT_DIR/native_${ROUTE_NAME}.jsonl"
NATIVE_LOG="$OUT_DIR/native_${ROUTE_NAME}.log"
COMPARE_JSON="$OUT_DIR/compare_${ROUTE_NAME}.json"
ACTOR_COMPARE_JSON="$OUT_DIR/actor_compare_${ROUTE_NAME}.json"
SUMMARY_COMPARE_JSON="$OUT_DIR/summary_compare_${ROUTE_NAME}.json"
VISUAL_COMPARE_JSON="$OUT_DIR/visual_compare_${ROUTE_NAME}.json"
VISUAL_COMPARE_TXT="$OUT_DIR/visual_compare_${ROUTE_NAME}.txt"
VISUAL_COMPARE_HEATMAP="$OUT_DIR/visual_compare_${ROUTE_NAME}.png"
HEALTH_COMPARE_JSON="$OUT_DIR/combat_health_compare_${ROUTE_NAME}.json"
CAPTURE_SUMMARY_JSON="$OUT_DIR/summary_${ROUTE_NAME}.json"
NATIVE_SCREENSHOT_JSON="$OUT_DIR/native_${ROUTE_NAME}.screenshot.json"
NATIVE_RENDER_JSON="$OUT_DIR/native_${ROUTE_NAME}.render.json"
NATIVE_MOVEMENT_JSON="$OUT_DIR/native_${ROUTE_NAME}.movement.json"
NATIVE_INTRO_SUMMARY_JSON="$OUT_DIR/native_${ROUTE_NAME}.intro-summary.json"
NATIVE_INTRO_AUDIT_JSON="$OUT_DIR/native_${ROUTE_NAME}.intro-audit.json"
NATIVE_SAVE_DIR="$OUT_DIR/native_savedir"
STOCK_SCREENSHOT="$OUT_DIR/stock_${ROUTE_NAME}.ppm"
STOCK_SCREENSHOT_JSON="$OUT_DIR/stock_${ROUTE_NAME}.screenshot.json"
STOCK_AUDIT_JSON="$OUT_DIR/stock_${ROUTE_NAME}.audit.json"
STOCK_OUT_TRACE="${STOCK_TRACE:-$OUT_DIR/stock_${ROUTE_NAME}.jsonl}"
STOCK_LOG="$OUT_DIR/stock_${ROUTE_NAME}.log"
ARES_INPUT="$OUT_DIR/${ROUTE_NAME}.ares-input"
SETTINGS_FILE="$OUT_DIR/ares_settings.bml"
SAVES_DIR="$OUT_DIR/ares_saves"
SCREENSHOT_LABEL="rom_oracle_${ROUTE_NAME}_$$"
SCREENSHOT_SRC="screenshot_${SCREENSHOT_LABEL}.bmp"
SCREENSHOT_DST="$OUT_DIR/native_${ROUTE_NAME}.bmp"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

audit_native_effective_config() {
    local config_file="$NATIVE_SAVE_DIR/ge007.ini"
    local native_config=()

    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        native_config+=("$line")
    done < <(python3 tools/rom_oracle_route.py native-config "$ROUTE_PATH")
    if [[ "${#EXTRA_NATIVE_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
        native_config+=("${EXTRA_NATIVE_CONFIG_OVERRIDES[@]}")
    fi
    native_config+=("${FIDELITY_CAPTURE_CONFIG_OVERRIDES[@]}")

    if [[ "${#native_config[@]}" -eq 0 ]]; then
        return 0
    fi

    python3 - "$config_file" "${native_config[@]}" <<'PY'
import math
import sys
from pathlib import Path

config_path = Path(sys.argv[1])
overrides = sys.argv[2:]

if not config_path.is_file():
    print(f"FAIL: missing generated native config for override audit: {config_path}")
    raise SystemExit(1)

values = {}
section = None
for raw in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or line.startswith(";"):
        continue
    if line.startswith("[") and line.endswith("]"):
        section = line[1:-1].strip()
        continue
    if section and "=" in line:
        key, value = line.split("=", 1)
        values[f"{section}.{key.strip()}"] = value.strip()


def equal_value(expected: str, actual: str) -> bool:
    if expected == actual:
        return True
    try:
        e = float(expected)
        a = float(actual)
    except ValueError:
        return False
    return math.isfinite(e) and math.isfinite(a) and abs(e - a) <= 1e-6


failures = []
expected_by_key = {}
for override in overrides:
    if "=" not in override:
        failures.append(f"malformed override: {override}")
        continue
    full_key, expected = override.split("=", 1)
    expected_by_key[full_key] = expected

for full_key, expected in expected_by_key.items():
    # The validation default can use -1 as a center-window request. The runtime
    # persists the resolved screen position, so this request is not stable
    # evidence that the binary ignored the override path.
    if full_key in ("Video.WindowX", "Video.WindowY") and expected == "-1":
        continue

    actual = values.get(full_key)
    if actual is None:
        failures.append(f"{full_key}: missing from generated config")
    elif not equal_value(expected, actual):
        failures.append(f"{full_key}: expected {expected!r}, got {actual!r}")

if failures:
    print("FAIL: native config override audit")
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)

print("PASS: native config override audit")
PY
}

run_native_capture() {
    local env_cmd=()
    local native_env=()
    local native_config_args=()
    local native_timing_env=()
    local native_screenshot_args=()

    while IFS= read -r line; do
        native_env+=("$line")
    done < <(python3 tools/rom_oracle_route.py native-env "$ROUTE_PATH")
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        native_config_args+=(--config-override "$line")
    done < <(python3 tools/rom_oracle_route.py native-config "$ROUTE_PATH")
    if [[ "${#EXTRA_NATIVE_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
        for line in "${EXTRA_NATIVE_CONFIG_OVERRIDES[@]}"; do
            native_config_args+=(--config-override "$line")
        done
    fi
    for line in "${FIDELITY_CAPTURE_CONFIG_OVERRIDES[@]}"; do
        native_config_args+=(--config-override "$line")
    done
    if [[ -n "$NATIVE_SPEEDFRAMES" ]]; then
        native_timing_env+=(GE007_DETERMINISTIC_SPEEDFRAMES="$NATIVE_SPEEDFRAMES")
    fi
    rm -rf "$NATIVE_SAVE_DIR"
    mkdir -p "$NATIVE_SAVE_DIR"
    rm -f "$NATIVE_TRACE" "$NATIVE_LOG" "$SCREENSHOT_SRC" "$SCREENSHOT_DST"
    rm -f "$NATIVE_SCREENSHOT_JSON" "$NATIVE_RENDER_JSON" "$NATIVE_MOVEMENT_JSON"
    rm -f "$NATIVE_INTRO_SUMMARY_JSON" "$NATIVE_INTRO_AUDIT_JSON" "$CAPTURE_SUMMARY_JSON"

    env_cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1)
    if [[ "$COMPARE_KIND" == "movement" && "$NATIVE_FULL_TRACE" != "1" ]]; then
        env_cmd+=(GE007_TRACE_FLOW_ONLY=1)
    fi
    if [[ "${#native_timing_env[@]}" -gt 0 ]]; then
        env_cmd+=("${native_timing_env[@]}")
    fi
    if [[ "${#native_env[@]}" -gt 0 ]]; then
        env_cmd+=("${native_env[@]}")
    fi
    env_cmd+=(GE007_FIDELITY_CAPTURE=1)
    if [[ -n "$NATIVE_SCREENSHOT_GAME_TIMER" ]]; then
        native_screenshot_args=(--screenshot-game-timer "$NATIVE_SCREENSHOT_GAME_TIMER")
    else
        native_screenshot_args=(--screenshot-frame "$NATIVE_FRAMES")
    fi

    env_cmd+=("$BINARY")
    if [[ "${#native_config_args[@]}" -gt 0 ]]; then
        env_cmd+=("${native_config_args[@]}")
    fi
    env_cmd+=(--savedir "$NATIVE_SAVE_DIR" --rom "$ROM")
    case "$NATIVE_MENU_BOOT" in
        1|true|True|TRUE|yes|YES|on|ON)
            # D30 menu-boot lane: omit --level entirely so the native binary
            # boots to the frontend (g_pcStartLevel stays -1, main_pc.c) and
            # is driven to the target mission by scripted GE007_AUTO_START/A
            # frontend input (route native_events), the same way a player
            # reaches gameplay -- never a direct-boot pin.
            ;;
        *)
            env_cmd+=(--level "$NATIVE_LEVEL")
            ;;
    esac
    env_cmd+=(
        --deterministic
        --trace-state "$NATIVE_TRACE"
        "${native_screenshot_args[@]}"
        --screenshot-label "$SCREENSHOT_LABEL"
        --screenshot-exit)

    if [[ -n "$NATIVE_SCREENSHOT_GAME_TIMER" ]]; then
        echo "  native: route=${ROUTE_NAME} level=${NATIVE_LEVEL} frames=${NATIVE_FRAMES} screenshot_game_timer=${NATIVE_SCREENSHOT_GAME_TIMER}"
    else
        echo "  native: route=${ROUTE_NAME} level=${NATIVE_LEVEL} frames=${NATIVE_FRAMES}"
    fi
    case "$NATIVE_MENU_BOOT" in
        1|true|True|TRUE|yes|YES|on|ON)
            echo "  native: menu-boot (no --level; scripted frontend input drives the boot)"
            ;;
    esac
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" "${env_cmd[@]}" >"$NATIVE_LOG" 2>&1; then
        echo "FAIL: native oracle capture failed" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$NATIVE_TRACE" ]]; then
        echo "FAIL: native oracle trace was not written: $NATIVE_TRACE" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$SCREENSHOT_SRC" ]]; then
        echo "FAIL: native oracle screenshot was not written: $SCREENSHOT_SRC" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    mv "$SCREENSHOT_SRC" "$SCREENSHOT_DST"
    python3 tools/audit_screenshot_health.py \
        --label "native ${ROUTE_NAME} screenshot" \
        --expect-size 640x480 \
        --json-out "$NATIVE_SCREENSHOT_JSON" \
        "$SCREENSHOT_DST"
    audit_native_effective_config
}

# ares' Pak::saveLocation() (mia/pak/pak.cpp) resolves an EEPROM save path as
# `{Paths/Saves setting}{Medium::saveName()}/{Location::prefix(rom_path)}.eeprom`
# -- note NO separator between the Paths/Saves value and saveName(): for the
# N64 core, Medium::saveName() is the hardcoded string "Nintendo 64"
# (mia/medium/nintendo-64.cpp's `name()` override), so with
# `--setting Paths/Saves=$SAVES_DIR` the literal on-disk directory is
# "${SAVES_DIR}Nintendo 64" (yes, concatenated, not a sibling of "ares_saves"
# despite appearances -- confirmed empirically: a stock capture always
# creates this exact directory name even with an empty save). Location::
# prefix() strips only the ROM's last extension (baserom.u.z64 -> baserom.u).
# Seeding this file before ares boots is how an all-unlocked save reaches the
# stock oracle without any ares source patch.
stock_eeprom_seed_path() {
    local rom_basename rom_prefix
    rom_basename="$(basename "$ROM")"
    rom_prefix="${rom_basename%.*}"
    printf '%s\n' "${SAVES_DIR}Nintendo 64/${rom_prefix}.eeprom"
}

seed_stock_eeprom() {
    local target
    [[ -n "$SEED_EEPROM" ]] || return 0
    target="$(stock_eeprom_seed_path)"
    mkdir -p "$(dirname "$target")"
    cp "$SEED_EEPROM" "$target"
    echo "  seeded EEPROM: $SEED_EEPROM -> $target"
}

run_stock_capture() {
    local ares_pid=""
    local elapsed=0
    local trace_lines=0
    local stock_env=()
    local stock_timing_env=()
    local stock_cmd=()
    local shot_prev=-1
    local shot_cur=0
    local shot_waited=0

    python3 tools/rom_oracle_route.py ares-input "$ROUTE_PATH" >"$ARES_INPUT"
    while IFS= read -r line; do
        stock_env+=("$line")
    done < <(python3 tools/rom_oracle_route.py stock-env "$ROUTE_PATH")
    mkdir -p "$SAVES_DIR"
    seed_stock_eeprom
    rm -f "$STOCK_OUT_TRACE" "$STOCK_LOG" "$STOCK_SCREENSHOT" "$STOCK_SCREENSHOT_JSON"

    if [[ -n "$STOCK_SPEEDFRAMES" ]]; then
        stock_timing_env+=(MGB64_ARES_GAMEPLAY_SPEEDFRAMES="$STOCK_SPEEDFRAMES")
    fi
    if [[ -n "$STOCK_GAMEPLAY_START_GLOBAL" ]]; then
        stock_timing_env+=(MGB64_ARES_GAMEPLAY_START_GLOBAL="$STOCK_GAMEPLAY_START_GLOBAL")
    fi
    stock_timing_env+=(MGB64_ARES_CLOSE_MENU_ON_PLAYER="$STOCK_MENU_CLOSE_ON_PLAYER")

    if [[ -n "$STOCK_SCREENSHOT_GAME_TIMER" ]]; then
        echo "  stock:  route=${ROUTE_NAME} level=${STOCK_LEVEL} frames=${STOCK_FRAMES} screenshot_game_timer=${STOCK_SCREENSHOT_GAME_TIMER}"
    else
        echo "  stock:  route=${ROUTE_NAME} level=${STOCK_LEVEL} frames=${STOCK_FRAMES} screenshot_frame=${STOCK_SCREENSHOT_FRAME}"
    fi
    stock_cmd=(env
        MGB64_ARES_ORACLE_TRACE="$STOCK_OUT_TRACE" \
        MGB64_ARES_MOVEMENT_TRACE="$STOCK_OUT_TRACE" \
        MGB64_ARES_INPUT_SCRIPT="$ARES_INPUT" \
        MGB64_ARES_FRAME_LIMIT="$STOCK_FRAMES" \
        MGB64_ARES_SCREENSHOT_PATH="$STOCK_SCREENSHOT" \
        MGB64_ARES_SCREENSHOT_FRAME="$STOCK_SCREENSHOT_FRAME" \
        MGB64_ARES_TARGET_STAGE="$STOCK_LEVEL")
    if [[ -n "$STOCK_SCREENSHOT_GAME_TIMER" ]]; then
        stock_cmd+=(MGB64_ARES_SCREENSHOT_GAME_TIMER="$STOCK_SCREENSHOT_GAME_TIMER")
    fi
    if [[ "${#stock_env[@]}" -gt 0 ]]; then
        stock_cmd+=("${stock_env[@]}")
    fi
    if [[ "${#stock_timing_env[@]}" -gt 0 ]]; then
        stock_cmd+=("${stock_timing_env[@]}")
    fi
    stock_cmd+=(
        "$ARES_BIN" \
        --kiosk \
        --settings-file "$SETTINGS_FILE" \
        --setting Audio/Mute=true \
        --setting Audio/Blocking="${MGB64_ARES_AUDIO_BLOCKING:-false}" \
        --setting Audio/Dynamic="${MGB64_ARES_AUDIO_DYNAMIC:-false}" \
        --setting Input/Defocus=Allow \
        --setting General/AutoSaveMemory=false \
        --setting Paths/Saves="$SAVES_DIR" \
        --setting Boot/Fast=true \
        --setting Video/Blocking="${MGB64_ARES_VIDEO_BLOCKING:-false}" \
        --system "Nintendo 64" \
        --no-file-prompt \
        "$ROM")
    "${stock_cmd[@]}" >"$STOCK_LOG" 2>&1 &
    ares_pid="$!"

    while kill -0 "$ares_pid" 2>/dev/null; do
        if [[ -f "$STOCK_OUT_TRACE" ]]; then
            trace_lines="$(wc -l < "$STOCK_OUT_TRACE" | tr -d '[:space:]')"
            if [[ "$trace_lines" =~ ^[0-9]+$ && "$trace_lines" -ge "$STOCK_FRAMES" ]]; then
                break
            fi
        fi
        if [[ "$elapsed" -ge "$TIMEOUT_SECONDS" ]]; then
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    # The wait loop above breaks as soon as the trace reaches STOCK_FRAMES, but
    # the screenshot (often scheduled at that very frame) can still be mid-flush
    # -- killing ares immediately truncates the .ppm and fails the screenshot
    # health audit (observed intermittently: "image file is truncated"). Give
    # the file a bounded window to exist and reach a stable non-zero size
    # before terminating.
    if kill -0 "$ares_pid" 2>/dev/null; then
        while [[ "$shot_waited" -lt 15 ]]; do
            if [[ -f "$STOCK_SCREENSHOT" ]]; then
                shot_cur="$(wc -c < "$STOCK_SCREENSHOT" 2>/dev/null | tr -d '[:space:]')"
                [[ "$shot_cur" =~ ^[0-9]+$ ]] || shot_cur=0
                if [[ "$shot_cur" -gt 0 && "$shot_cur" -eq "$shot_prev" ]]; then
                    break
                fi
                shot_prev="$shot_cur"
            fi
            if ! kill -0 "$ares_pid" 2>/dev/null; then
                break
            fi
            sleep 1
            shot_waited=$((shot_waited + 1))
        done
    fi

    if kill -0 "$ares_pid" 2>/dev/null; then
        kill "$ares_pid" 2>/dev/null || true
        sleep 1
        if kill -0 "$ares_pid" 2>/dev/null; then
            kill -9 "$ares_pid" 2>/dev/null || true
        fi
        wait "$ares_pid" 2>/dev/null || true
    else
        wait "$ares_pid" 2>/dev/null || true
    fi

    if [[ ! -s "$STOCK_OUT_TRACE" ]]; then
        echo "FAIL: stock oracle trace was not written: $STOCK_OUT_TRACE" >&2
        echo "This usually means the selected ares binary lacks MGB64 oracle instrumentation." >&2
        tail -60 "$STOCK_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    trace_lines="$(wc -l < "$STOCK_OUT_TRACE" | tr -d '[:space:]')"
    if [[ ! "$trace_lines" =~ ^[0-9]+$ || "$trace_lines" -lt "$STOCK_FRAMES" ]]; then
        echo "FAIL: stock oracle trace stopped at ${trace_lines:-0}/${STOCK_FRAMES} frame(s)" >&2
        tail -60 "$STOCK_LOG" | sed 's/^/  /' >&2
        exit 1
    fi

    if [[ ! -s "$STOCK_SCREENSHOT" ]]; then
        echo "FAIL: stock oracle screenshot was not written: $STOCK_SCREENSHOT" >&2
        echo "This usually means the selected ares binary lacks the screen-dump oracle hook." >&2
        tail -60 "$STOCK_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    python3 tools/audit_screenshot_health.py \
        --label "stock ${ROUTE_NAME} screenshot" \
        --expect-size 640x480 \
        --json-out "$STOCK_SCREENSHOT_JSON" \
        "$STOCK_SCREENSHOT"
}

stock_first_gameplay_global() {
    python3 - "$STOCK_OUT_TRACE" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    handle = path.open("r", encoding="utf-8", errors="replace")
except OSError:
    sys.exit(0)

with handle:
    for line in handle:
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        oracle = record.get("oracle")
        if not isinstance(oracle, dict):
            continue
        inp = oracle.get("input")
        if not isinstance(inp, dict):
            continue
        gameplay_frame = inp.get("gameplay_frame")
        try:
            gameplay_frame = int(gameplay_frame)
        except (TypeError, ValueError):
            continue
        if gameplay_frame <= 0:
            continue
        move = record.get("move")
        if isinstance(move, dict) and move.get("global") is not None:
            print(move.get("global"))
        break
PY
}

# Print the first resolved intro camera pad (intro.selected_camera.pad, ignoring
# the sentinel -1 "not yet picked" value) in a JSONL oracle trace. Used to gate
# the stock capture on landing the same authored intro camera the native side
# pinned via GE007_INTRO_CAMERA_INDEX -- GoldenEye's level-intro camera is
# RNG-picked at boot (faithful N64 behaviour), so an unpinned stock capture can
# land on a different camera than the pinned native one, which then diverges the
# whole authored camera path (BUG-3 run-to-run oracle variance).
selected_camera_pad_of() {
    python3 - "$1" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    handle = path.open("r", encoding="utf-8", errors="replace")
except OSError:
    sys.exit(0)

with handle:
    for line in handle:
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        intro = record.get("intro")
        if not isinstance(intro, dict):
            continue
        selected = intro.get("selected_camera")
        if not isinstance(selected, dict):
            continue
        pad = selected.get("pad")
        try:
            pad = int(pad)
        except (TypeError, ValueError):
            continue
        if pad < 0:
            continue
        print(pad)
        break
PY
}

preserve_stock_attempt() {
    local attempt="$1"
    local suffix=".attempt${attempt}"

    [[ -s "$STOCK_OUT_TRACE" ]] && cp "$STOCK_OUT_TRACE" "${STOCK_OUT_TRACE}${suffix}"
    [[ -s "$STOCK_LOG" ]] && cp "$STOCK_LOG" "${STOCK_LOG}${suffix}"
    [[ -s "$STOCK_SCREENSHOT" ]] && cp "$STOCK_SCREENSHOT" "${STOCK_SCREENSHOT}${suffix}"
    [[ -s "$STOCK_SCREENSHOT_JSON" ]] && cp "$STOCK_SCREENSHOT_JSON" "${STOCK_SCREENSHOT_JSON}${suffix}"
}

run_stock_capture_with_route_control_retries() {
    local max_attempts=1
    local attempt=1
    local first_global=""
    local native_pad=""
    local stock_pad=""
    local reason=""

    if [[ -n "$STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL" || "$STOCK_REQUIRE_NATIVE_SELECTED_CAMERA" -eq 1 ]]; then
        max_attempts="${MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES:-4}"
        if [[ ! "$max_attempts" =~ ^[1-9][0-9]*$ ]]; then
            echo "FAIL: MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES must be a positive integer: $max_attempts" >&2
            exit 2
        fi
    fi

    # The camera-match gate compares against the pinned native pick, so the
    # native capture must have already produced its trace.
    if [[ "$STOCK_REQUIRE_NATIVE_SELECTED_CAMERA" -eq 1 ]]; then
        native_pad="$(selected_camera_pad_of "$NATIVE_TRACE" | tr -d '[:space:]')"
        if [[ ! "$native_pad" =~ ^[0-9]+$ ]]; then
            echo "FAIL: stock_require_native_selected_camera set but native trace has no resolved intro camera pad" >&2
            exit 1
        fi
    fi

    while [[ "$attempt" -le "$max_attempts" ]]; do
        if [[ "$max_attempts" -gt 1 ]]; then
            echo "  stock attempt ${attempt}/${max_attempts}"
        fi
        run_stock_capture

        reason=""
        if [[ -n "$STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL" ]]; then
            first_global="$(stock_first_gameplay_global | tr -d '[:space:]')"
            if [[ "$first_global" != "$STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL" ]]; then
                reason="first gameplay global ${first_global:-missing}, expected ${STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL}"
            fi
        fi
        if [[ -z "$reason" && "$STOCK_REQUIRE_NATIVE_SELECTED_CAMERA" -eq 1 ]]; then
            stock_pad="$(selected_camera_pad_of "$STOCK_OUT_TRACE" | tr -d '[:space:]')"
            if [[ "$stock_pad" != "$native_pad" ]]; then
                reason="stock intro camera pad ${stock_pad:-missing}, expected native-pinned ${native_pad} (RNG intro-camera pick)"
            fi
        fi

        if [[ -z "$reason" ]]; then
            return 0
        fi

        echo "  stock attempt ${attempt}/${max_attempts}: ${reason}" >&2
        preserve_stock_attempt "$attempt"
        attempt=$((attempt + 1))
    done

    echo "FAIL: stock route control did not converge after ${max_attempts} attempt(s): ${reason}" >&2
    exit 1
}

echo "=== ROM Oracle Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  route:   $ROUTE_PATH"
echo "  native:  $BINARY"
echo "  ROM:     $ROM"

run_native_capture

    case "$NATIVE_RENDER_AUDIT" in
    1|true|True|TRUE|yes|YES|on|ON)
        echo ""
        python3 tools/audit_render_trace.py \
            --label "native ${ROUTE_NAME}" \
            --json-out "$NATIVE_RENDER_JSON" \
            "$NATIVE_TRACE"
        ;;
esac

if [[ "$COMPARE_KIND" == "movement" && -n "$NATIVE_MIN_MOVING_RECORDS" ]]; then
    echo ""
    native_audit_args=(--kind movement --label "native ${ROUTE_NAME}" --require-stage "$NATIVE_LEVEL" --require-target-player)
    native_audit_args+=(--min-moving-records "$NATIVE_MIN_MOVING_RECORDS")
    native_audit_args+=(--json-out "$NATIVE_MOVEMENT_JSON")
    python3 tools/audit_oracle_trace.py "${native_audit_args[@]}" "$NATIVE_TRACE"
fi

intro_summary_common_args=()
intro_summary_profile=""
if [[ "$COMPARE_KIND" == "intro" ]]; then
    intro_summary_common_args=()
    if [[ -n "$COMPARE_CAMERA_MODES" ]]; then
        intro_summary_common_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
    fi
    if [[ -n "$COMPARE_START_INTRO_TIMER" ]]; then
        intro_summary_common_args+=(--start-intro-timer "$COMPARE_START_INTRO_TIMER")
    fi
    if [[ -n "$COMPARE_END_INTRO_TIMER" ]]; then
        intro_summary_common_args+=(--end-intro-timer "$COMPARE_END_INTRO_TIMER")
    fi
    case "$COMPARE_REQUIRE_FROZEN" in
        1|true|True|TRUE|yes|YES)
            intro_summary_common_args+=(--require-frozen)
            ;;
    esac

    case "$COMPARE_INTRO_SETUP" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},setup"
            ;;
    esac
    case "$COMPARE_SELECTED_CAMERA" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},selected-camera"
            ;;
    esac
    case "$COMPARE_BOND_ANIM" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},bond-anim"
            ;;
    esac
    intro_summary_profile="${intro_summary_profile#,}"
    if [[ -z "$intro_summary_profile" ]]; then
        intro_summary_profile="setup,selected-camera"
    fi

    echo ""
    python3 tools/intro_trace_summary.py \
        "${intro_summary_common_args[@]}" \
        --json-out "$NATIVE_INTRO_SUMMARY_JSON" \
        "$NATIVE_TRACE"
fi

case "$NATIVE_INTRO_AUDIT" in
    1|true|True|TRUE|yes|YES|on|ON)
        echo ""
        intro_audit_args=(--label "native ${ROUTE_NAME}")
        if [[ -n "$NATIVE_INTRO_CAMERA_MODES" ]]; then
            intro_audit_args+=(--camera-modes "$NATIVE_INTRO_CAMERA_MODES")
        elif [[ -n "$COMPARE_CAMERA_MODES" ]]; then
            intro_audit_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
        fi
        case "${NATIVE_INTRO_REQUIRE_FROZEN:-$COMPARE_REQUIRE_FROZEN}" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-frozen)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_PLAYER" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-player)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_PRESENT" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-present)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ONSCREEN" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-onscreen)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_MODEL_MTX" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-model-mtx)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_RENDERED" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-rendered)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ANIM" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-anim)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ANIM_HASH" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-anim-hash)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_H17_SWIRL_FACING" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-h17-swirl-facing)
                ;;
        esac
        if [[ -n "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" ]]; then
            intro_audit_args+=(--require-right-item "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ACTIVE_RECORDS" ]]; then
            intro_audit_args+=(--min-active-records "$NATIVE_INTRO_MIN_ACTIVE_RECORDS")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_PRESENT_FRAMES" ]]; then
            intro_audit_args+=(--min-present-frames "$NATIVE_INTRO_MIN_PRESENT_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ONSCREEN_FRAMES" ]]; then
            intro_audit_args+=(--min-onscreen-frames "$NATIVE_INTRO_MIN_ONSCREEN_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_MODEL_MTX_FRAMES" ]]; then
            intro_audit_args+=(--min-model-mtx-frames "$NATIVE_INTRO_MIN_MODEL_MTX_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RENDERED_FRAMES" ]]; then
            intro_audit_args+=(--min-rendered-frames "$NATIVE_INTRO_MIN_RENDERED_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RENDER_COUNT" ]]; then
            intro_audit_args+=(--min-render-count "$NATIVE_INTRO_MIN_RENDER_COUNT")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_FRAMES" ]]; then
            intro_audit_args+=(--min-anim-frames "$NATIVE_INTRO_MIN_ANIM_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_HASH_FRAMES" ]]; then
            intro_audit_args+=(--min-anim-hash-frames "$NATIVE_INTRO_MIN_ANIM_HASH_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_ADVANCE" ]]; then
            intro_audit_args+=(--min-anim-advance "$NATIVE_INTRO_MIN_ANIM_ADVANCE")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES" ]]; then
            intro_audit_args+=(--min-right-item-frames "$NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME" ]]; then
            intro_audit_args+=(--max-first-present-frame "$NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME")
        fi
        if [[ -n "$NATIVE_INTRO_MAX_FIRST_RENDER_FRAME" ]]; then
            intro_audit_args+=(--max-first-render-frame "$NATIVE_INTRO_MAX_FIRST_RENDER_FRAME")
        fi
        intro_audit_args+=(--json-out "$NATIVE_INTRO_AUDIT_JSON")
        python3 tools/audit_intro_trace.py "${intro_audit_args[@]}" "$NATIVE_TRACE"
        ;;
esac

if [[ "$NATIVE_ONLY" -eq 0 && -n "$ARES_BIN" && -z "$STOCK_TRACE" ]]; then
    run_stock_capture_with_route_control_retries
fi

echo ""
echo "artifacts:"
echo "  native trace: $NATIVE_TRACE"
if [[ -n "$STOCK_TRACE" || -n "$ARES_BIN" ]]; then
    echo "  stock trace:  $STOCK_OUT_TRACE"
    if [[ -s "$STOCK_SCREENSHOT" ]]; then
        echo "  stock shot:   $STOCK_SCREENSHOT"
    fi
fi

if [[ "$NATIVE_ONLY" -eq 0 && ( -n "$STOCK_TRACE" || -n "$ARES_BIN" ) ]]; then
    echo ""
    stock_audit_kind="$COMPARE_KIND"
    if [[ "$stock_audit_kind" == "visual" || "$stock_audit_kind" == "glass" ]]; then
        stock_audit_kind="movement"
    fi
    audit_args=(--kind "$stock_audit_kind" --label "stock ${ROUTE_NAME}" --require-stage "$STOCK_LEVEL" --require-target-player)
    if [[ "$COMPARE_KIND" == "movement" || "$COMPARE_KIND" == "glass" ]]; then
        audit_args+=(--require-gameplay-input)
        if [[ -n "$STOCK_MIN_GAMEPLAY_INPUT_RECORDS" ]]; then
            audit_args+=(--min-gameplay-input-records "$STOCK_MIN_GAMEPLAY_INPUT_RECORDS")
        fi
        if [[ "$COMPARE_KIND" == "movement" && -n "$STOCK_MIN_MOVING_RECORDS" ]]; then
            audit_args+=(--min-moving-records "$STOCK_MIN_MOVING_RECORDS")
        fi
    fi
    if [[ -n "$STOCK_MAX_SUPPRESSED_MENU_RECORDS" ]]; then
        audit_args+=(--max-suppressed-menu-records "$STOCK_MAX_SUPPRESSED_MENU_RECORDS")
    fi
    if [[ -n "$STOCK_MIN_MENU_TO_GAMEPLAY_GAP" ]]; then
        audit_args+=(--min-menu-to-gameplay-gap "$STOCK_MIN_MENU_TO_GAMEPLAY_GAP")
    fi
    if [[ -n "$STOCK_MIN_FORCE_PLAYER_APPLIES" ]]; then
        audit_args+=(--min-force-player-applies "$STOCK_MIN_FORCE_PLAYER_APPLIES")
    fi
    if [[ -n "$STOCK_MIN_FORCE_PLAYER_STAN_APPLIES" ]]; then
        audit_args+=(--min-force-player-stan-applies "$STOCK_MIN_FORCE_PLAYER_STAN_APPLIES")
    fi
    if [[ -n "$STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL" ]]; then
        audit_args+=(--require-first-gameplay-global "$STOCK_REQUIRE_FIRST_GAMEPLAY_GLOBAL")
    fi
    audit_args+=(--json-out "$STOCK_AUDIT_JSON")
    python3 tools/audit_oracle_trace.py "${audit_args[@]}" "$STOCK_OUT_TRACE"
fi

if [[ "$NO_COMPARE" -eq 0 && "$NATIVE_ONLY" -eq 0 && ( -n "$STOCK_TRACE" || -n "$ARES_BIN" ) ]]; then
    echo ""
    ROUTE_EXIT_STATUS=0
    compare_args=(--align "$COMPARE_ALIGN" --profile "$COMPARE_PROFILE")
    actor_compare_args=()
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        actor_compare_args+=("$line")
    done < <(python3 tools/rom_oracle_route.py actor-compare-args "$ROUTE_PATH")
    visual_precompare_status=0

    if [[ "$COMPARE_KIND" == "visual" ]]; then
        health_compare_args=(
            --baseline-label "stock ${ROUTE_NAME}"
            --test-label "native ${ROUTE_NAME}"
            --json-out "$HEALTH_COMPARE_JSON"
        )
        if [[ -n "$STOCK_SCREENSHOT_GAME_TIMER" ]]; then
            health_compare_args+=(--baseline-global "$STOCK_SCREENSHOT_GAME_TIMER")
        else
            health_compare_args+=(--baseline-frame "$STOCK_SCREENSHOT_FRAME")
        fi
        if [[ -n "$NATIVE_SCREENSHOT_GAME_TIMER" ]]; then
            health_compare_args+=(--test-global "$NATIVE_SCREENSHOT_GAME_TIMER")
        else
            health_compare_args+=(--test-frame "$NATIVE_FRAMES")
        fi
        if [[ -n "$COMPARE_HEALTH_TOLERANCE" ]]; then
            health_compare_args+=(--health-tolerance "$COMPARE_HEALTH_TOLERANCE")
        fi
        if [[ -n "$COMPARE_DAMAGE_SHOW_TOLERANCE" ]]; then
            health_compare_args+=(--damage-show-tolerance "$COMPARE_DAMAGE_SHOW_TOLERANCE")
        fi
        case "$COMPARE_REQUIRE_HEALTH_MATCH" in
            1|true|True|TRUE|yes|YES)
                health_compare_args+=(--require-match)
                ;;
        esac
        if ! python3 tools/compare_combat_health_trace.py \
            "${health_compare_args[@]}" \
            "$STOCK_OUT_TRACE" \
            "$NATIVE_TRACE"
        then
            visual_precompare_status=1
        fi
        echo ""
    fi

    if [[ "$COMPARE_KIND" == "visual" && "${#actor_compare_args[@]}" -gt 0 ]]; then
        visual_actor_compare_args=("${actor_compare_args[@]}")
        actor_baseline_frame=""
        actor_test_frame=""
        if [[ "$COMPARE_ACTOR_FRAME" == "screenshot" && -s "$HEALTH_COMPARE_JSON" ]]; then
            actor_checkpoint_frames="$(python3 - "$HEALTH_COMPARE_JSON" <<'PY' || true
import json
import sys
from pathlib import Path

try:
    report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    baseline = report.get("baseline_checkpoint", {}).get("frame")
    test = report.get("test_checkpoint", {}).get("frame")
    if isinstance(baseline, int) and baseline > 0 and isinstance(test, int) and test > 0:
        print(f"{baseline} {test}")
except Exception:
    pass
PY
)"
            if [[ "$actor_checkpoint_frames" =~ ^[0-9]+[[:space:]][0-9]+$ ]]; then
                actor_baseline_frame="${actor_checkpoint_frames%%[[:space:]]*}"
                actor_test_frame="${actor_checkpoint_frames##*[[:space:]]}"
            fi
        fi
        if [[ -n "$actor_baseline_frame" ]]; then
            visual_actor_compare_args+=(--actor-baseline-frame "$actor_baseline_frame")
        elif [[ -n "$STOCK_SCREENSHOT_GAME_TIMER" ]]; then
            visual_actor_compare_args+=(--actor-baseline-global "$STOCK_SCREENSHOT_GAME_TIMER")
        else
            visual_actor_compare_args+=(--actor-baseline-frame "$STOCK_SCREENSHOT_FRAME")
        fi
        if [[ -n "$actor_test_frame" ]]; then
            visual_actor_compare_args+=(--actor-test-frame "$actor_test_frame")
        elif [[ -n "$NATIVE_SCREENSHOT_GAME_TIMER" ]]; then
            visual_actor_compare_args+=(--actor-test-global "$NATIVE_SCREENSHOT_GAME_TIMER")
        else
            native_actor_frame=$((NATIVE_FRAMES - 1))
            if [[ "$native_actor_frame" -lt 1 ]]; then
                native_actor_frame=1
            fi
            visual_actor_compare_args+=(--actor-test-frame "$native_actor_frame")
        fi
        if ! python3 tools/compare_glass_trace.py \
            --json-out "$ACTOR_COMPARE_JSON" \
            "${visual_actor_compare_args[@]}" \
            "$STOCK_OUT_TRACE" \
            "$NATIVE_TRACE"
        then
            visual_precompare_status=1
        fi
        echo ""
    fi

    visual_glass_compare_args=()
    visual_glass_compare_requested=0
    case "$COMPARE_REQUIRE_ACTIVE" in
        1|true|True|TRUE|yes|YES)
            visual_glass_compare_args+=(--require-active)
            visual_glass_compare_requested=1
            ;;
    esac
    case "$COMPARE_REQUIRE_HASH_MATCH" in
        1|true|True|TRUE|yes|YES)
            visual_glass_compare_args+=(--require-hash-match)
            visual_glass_compare_requested=1
            ;;
    esac
    case "$COMPARE_REQUIRE_PROP_DESTROYED" in
        1|true|True|TRUE|yes|YES)
            visual_glass_compare_args+=(--require-prop-destroyed)
            visual_glass_compare_requested=1
            ;;
    esac
    case "$COMPARE_REQUIRE_IMPACT_ACTIVE" in
        1|true|True|TRUE|yes|YES)
            visual_glass_compare_args+=(--require-impact-active)
            visual_glass_compare_requested=1
            ;;
    esac
    case "$COMPARE_REQUIRE_IMPACT_MATCH" in
        1|true|True|TRUE|yes|YES)
            visual_glass_compare_args+=(--require-impact-match)
            visual_glass_compare_requested=1
            ;;
    esac
    if [[ -n "$COMPARE_FIRST_ACTIVE_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--first-active-tolerance "$COMPARE_FIRST_ACTIVE_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_MAX_ACTIVE_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--max-active-tolerance "$COMPARE_MAX_ACTIVE_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_FIRST_POSITION_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--first-position-tolerance "$COMPARE_FIRST_POSITION_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_FIRST_SAMPLE_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--first-sample-tolerance "$COMPARE_FIRST_SAMPLE_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_PROP_POSITION_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--prop-position-tolerance "$COMPARE_PROP_POSITION_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_IMPACT_POSITION_TOLERANCE" ]]; then
        visual_glass_compare_args+=(--impact-position-tolerance "$COMPARE_IMPACT_POSITION_TOLERANCE")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_IMPACT_POSITION_POINTS" ]]; then
        visual_glass_compare_args+=(--impact-position-points "$COMPARE_IMPACT_POSITION_POINTS")
        visual_glass_compare_requested=1
    fi
    if [[ -n "$COMPARE_MAX_BUFFER_LEN" ]]; then
        visual_glass_compare_args+=(--max-buffer-len "$COMPARE_MAX_BUFFER_LEN")
        visual_glass_compare_requested=1
    fi
    if [[ "$COMPARE_KIND" == "visual" && "$visual_glass_compare_requested" -eq 1 ]]; then
        if ! python3 tools/compare_glass_trace.py \
            --json-out "$COMPARE_JSON" \
            "${visual_glass_compare_args[@]}" \
            "$STOCK_OUT_TRACE" \
            "$NATIVE_TRACE"
        then
            visual_precompare_status=1
        fi
        echo ""
    fi

    if [[ -n "$COMPARE_MAX_ALIGNED" ]]; then
        compare_args+=(--max-aligned "$COMPARE_MAX_ALIGNED")
    fi
    if [[ "$COMPARE_KIND" == "glass" ]]; then
        glass_compare_args=(--json-out "$COMPARE_JSON")
        case "$COMPARE_REQUIRE_ACTIVE" in
            1|true|True|TRUE|yes|YES)
                glass_compare_args+=(--require-active)
                ;;
        esac
        case "$COMPARE_REQUIRE_HASH_MATCH" in
            1|true|True|TRUE|yes|YES)
                glass_compare_args+=(--require-hash-match)
                ;;
        esac
        case "$COMPARE_REQUIRE_PROP_DESTROYED" in
            1|true|True|TRUE|yes|YES)
                glass_compare_args+=(--require-prop-destroyed)
                ;;
        esac
        case "$COMPARE_REQUIRE_IMPACT_ACTIVE" in
            1|true|True|TRUE|yes|YES)
                glass_compare_args+=(--require-impact-active)
                ;;
        esac
        case "$COMPARE_REQUIRE_IMPACT_MATCH" in
            1|true|True|TRUE|yes|YES)
                glass_compare_args+=(--require-impact-match)
                ;;
        esac
        if [[ -n "$COMPARE_FIRST_ACTIVE_TOLERANCE" ]]; then
            glass_compare_args+=(--first-active-tolerance "$COMPARE_FIRST_ACTIVE_TOLERANCE")
        fi
        if [[ -n "$COMPARE_MAX_ACTIVE_TOLERANCE" ]]; then
            glass_compare_args+=(--max-active-tolerance "$COMPARE_MAX_ACTIVE_TOLERANCE")
        fi
        if [[ -n "$COMPARE_FIRST_POSITION_TOLERANCE" ]]; then
            glass_compare_args+=(--first-position-tolerance "$COMPARE_FIRST_POSITION_TOLERANCE")
        fi
        if [[ -n "$COMPARE_FIRST_SAMPLE_TOLERANCE" ]]; then
            glass_compare_args+=(--first-sample-tolerance "$COMPARE_FIRST_SAMPLE_TOLERANCE")
        fi
        if [[ -n "$COMPARE_PROP_POSITION_TOLERANCE" ]]; then
            glass_compare_args+=(--prop-position-tolerance "$COMPARE_PROP_POSITION_TOLERANCE")
        fi
        if [[ -n "$COMPARE_IMPACT_POSITION_TOLERANCE" ]]; then
            glass_compare_args+=(--impact-position-tolerance "$COMPARE_IMPACT_POSITION_TOLERANCE")
        fi
        if [[ -n "$COMPARE_IMPACT_POSITION_POINTS" ]]; then
            glass_compare_args+=(--impact-position-points "$COMPARE_IMPACT_POSITION_POINTS")
        fi
        if [[ -n "$COMPARE_MAX_BUFFER_LEN" ]]; then
            glass_compare_args+=(--max-buffer-len "$COMPARE_MAX_BUFFER_LEN")
        fi
        python3 tools/compare_glass_trace.py "${glass_compare_args[@]}" "$STOCK_OUT_TRACE" "$NATIVE_TRACE"
    elif [[ "$COMPARE_KIND" == "movement" ]]; then
        compare_args+=(--baseline-stage "$STOCK_LEVEL" --test-stage "$NATIVE_LEVEL")
        if [[ -n "$COMPARE_GAMEPLAY_WINDOWS" ]]; then
            while IFS= read -r window; do
                if [[ -n "$window" ]]; then
                    compare_args+=(--gameplay-window "$window")
                fi
            done <<< "$COMPARE_GAMEPLAY_WINDOWS"
        fi
        if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
            compare_args+=(--min-aligned "$COMPARE_MIN_ALIGNED")
        fi
        case "$COMPARE_NORMALIZE_POSITION" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--normalize-position)
                ;;
        esac
        compare_args+=(--json-out "$COMPARE_JSON")
        python3 tools/compare_movement_trace.py "${compare_args[@]}" "$STOCK_OUT_TRACE" "$NATIVE_TRACE"
    elif [[ "$COMPARE_KIND" == "intro" ]]; then
        if [[ -n "$COMPARE_CAMERA_MODES" ]]; then
            compare_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
        fi
        if [[ -n "$COMPARE_START_ACTIVE_FRAME" ]]; then
            compare_args+=(--start-active-frame "$COMPARE_START_ACTIVE_FRAME")
        fi
        if [[ -n "$COMPARE_START_INTRO_TIMER" ]]; then
            compare_args+=(--start-intro-timer "$COMPARE_START_INTRO_TIMER")
        fi
        if [[ -n "$COMPARE_END_INTRO_TIMER" ]]; then
            compare_args+=(--end-intro-timer "$COMPARE_END_INTRO_TIMER")
        fi
        if [[ -n "$COMPARE_SAMPLE_STEP" ]]; then
            compare_args+=(--sample-step "$COMPARE_SAMPLE_STEP")
        fi
        if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
            compare_args+=(--min-aligned "$COMPARE_MIN_ALIGNED")
        fi
        if [[ -n "$COMPARE_VECTOR_TOLERANCE" ]]; then
            compare_args+=(--vector-tolerance "$COMPARE_VECTOR_TOLERANCE")
        fi
        if [[ -n "$COMPARE_DIRECTION_TOLERANCE" ]]; then
            compare_args+=(--direction-tolerance "$COMPARE_DIRECTION_TOLERANCE")
        fi
        if [[ -n "$COMPARE_SCALAR_TOLERANCE" ]]; then
            compare_args+=(--scalar-tolerance "$COMPARE_SCALAR_TOLERANCE")
        fi
        if [[ -n "$COMPARE_ANIM_TOLERANCE" ]]; then
            compare_args+=(--anim-tolerance "$COMPARE_ANIM_TOLERANCE")
        fi
        if [[ -n "$COMPARE_EXCLUDE_FIELDS" ]]; then
            compare_args+=(--exclude-fields "$COMPARE_EXCLUDE_FIELDS")
        fi
        case "$COMPARE_STATE" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-state)
                ;;
        esac
        case "$COMPARE_SELECTED_CAMERA" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-selected-camera)
                ;;
        esac
        case "$COMPARE_INTRO_SETUP" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-setup)
                ;;
        esac
        case "$COMPARE_BOND_ANIM" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-bond-anim)
                ;;
        esac
        if [[ -n "$COMPARE_BOND_ANIM_ONSET_TOLERANCE" ]]; then
            compare_args+=(--bond-anim-onset-tolerance "$COMPARE_BOND_ANIM_ONSET_TOLERANCE")
        fi
        if [[ -n "$COMPARE_BOND_IDLE_ONSET_TOLERANCE" ]]; then
            compare_args+=(--bond-idle-onset-tolerance "$COMPARE_BOND_IDLE_ONSET_TOLERANCE")
        fi
        case "$COMPARE_REQUIRE_FROZEN" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--require-frozen)
                ;;
        esac
        if [[ -n "$COMPARE_EXPECT_MODE_DURATIONS" ]]; then
            compare_args+=(--expect-mode-durations "$COMPARE_EXPECT_MODE_DURATIONS")
        fi
        if [[ -n "$COMPARE_WAIVERS" && "$COMPARE_WAIVERS" != "{}" ]]; then
            compare_args+=(--waivers "$COMPARE_WAIVERS")
        fi
        compare_args+=(--json-out "$COMPARE_JSON")
        python3 tools/compare_intro_trace.py "${compare_args[@]}" "$STOCK_OUT_TRACE" "$NATIVE_TRACE"

        if [[ "$COMPARE_ALIGN" == "intro-timer" ]]; then
            summary_compare_args=(
                --baseline "$STOCK_OUT_TRACE"
                --test "$NATIVE_TRACE"
                --baseline-label "stock ${ROUTE_NAME}"
                --test-label "native ${ROUTE_NAME}"
                --compare-profile "$intro_summary_profile"
                --json-out "$SUMMARY_COMPARE_JSON"
            )
            summary_compare_args+=("${intro_summary_common_args[@]}")
            if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
                summary_compare_args+=(--min-matched-timers "$COMPARE_MIN_ALIGNED")
            fi
            if [[ -n "$COMPARE_VECTOR_TOLERANCE" ]]; then
                summary_compare_args+=(--vector-tolerance "$COMPARE_VECTOR_TOLERANCE")
            fi
            if [[ -n "$COMPARE_DIRECTION_TOLERANCE" ]]; then
                summary_compare_args+=(--direction-tolerance "$COMPARE_DIRECTION_TOLERANCE")
            fi
            if [[ -n "$COMPARE_SCALAR_TOLERANCE" ]]; then
                summary_compare_args+=(--scalar-tolerance "$COMPARE_SCALAR_TOLERANCE")
            fi
            if [[ -n "$COMPARE_ANIM_TOLERANCE" ]]; then
                summary_compare_args+=(--anim-tolerance "$COMPARE_ANIM_TOLERANCE")
            fi
            summary_compare_args+=(--integer-timer-keys)
            python3 tools/intro_trace_summary.py "${summary_compare_args[@]}"
        else
            echo "Skipping timer-summary comparison for ${COMPARE_ALIGN} intro alignment; strict intro comparator already ran."
        fi
    else
        if [[ "$visual_precompare_status" -ne 0 ]]; then
            echo "FAIL: visual pre-pixel guard(s) failed; refusing screenshot pixel comparison" >&2
            ROUTE_EXIT_STATUS=1
        else
            if [[ ! -s "$STOCK_SCREENSHOT" ]]; then
                echo "FAIL: visual comparison requires a stock screenshot; rerun with --ares-bin or use --no-compare with --stock-trace" >&2
                exit 1
            fi
            visual_compare_args=(
                --per-channel \
                --heatmap "$VISUAL_COMPARE_HEATMAP" \
                --json-out "$VISUAL_COMPARE_JSON" \
                "$STOCK_SCREENSHOT" \
                "$SCREENSHOT_DST"
            )
            if [[ "$COMPARE_PROFILE" == "active-normalized" ]]; then
                visual_compare_args=(--normalize-active "${visual_compare_args[@]}")
            fi
            if [[ "$COMPARE_PROFILE" == "logical-viewport" ]]; then
                logical_args=()
                while IFS= read -r line; do
                    [[ -n "$line" ]] || continue
                    logical_args+=("$line")
                done < <(python3 tools/rom_oracle_route.py visual-logical-args "$ROUTE_PATH")
                visual_compare_args=("${logical_args[@]}" "${visual_compare_args[@]}")
            fi
            while IFS= read -r line; do
                [[ -n "$line" ]] || continue
                visual_compare_args+=(--region "$line")
            done < <(python3 tools/rom_oracle_route.py visual-regions "$ROUTE_PATH")
            while IFS= read -r line; do
                [[ -n "$line" ]] || continue
                visual_compare_args+=(--exclude-region "$line")
            done < <(python3 tools/rom_oracle_route.py visual-exclude-regions "$ROUTE_PATH")
            python3 tools/compare_screenshots.py "${visual_compare_args[@]}" | tee "$VISUAL_COMPARE_TXT"
        fi

    fi

    if [[ "$COMPARE_KIND" != "visual" && "${#actor_compare_args[@]}" -gt 0 ]]; then
        echo ""
        python3 tools/compare_glass_trace.py \
            --json-out "$ACTOR_COMPARE_JSON" \
            "${actor_compare_args[@]}" \
            "$STOCK_OUT_TRACE" \
            "$NATIVE_TRACE"
    fi
else
    ROUTE_EXIT_STATUS=0
    echo ""
    if [[ "$NATIVE_ONLY" -eq 0 && ( -n "$STOCK_TRACE" || -n "$ARES_BIN" ) ]]; then
        echo "Captures complete. Comparison was disabled with --no-compare."
    else
        echo "Native capture complete. Add --stock-trace or --ares-bin to run ROM-vs-native comparison."
    fi
fi

python3 - \
    "$CAPTURE_SUMMARY_JSON" \
    "$ROUTE_NAME" \
    "$COMPARE_KIND" \
    "$ROUTE_PATH" \
    "$NATIVE_LEVEL" \
    "$STOCK_LEVEL" \
    "$NATIVE_FRAMES" \
    "$STOCK_FRAMES" \
    "$STOCK_SCREENSHOT_FRAME" \
    "$NATIVE_SCREENSHOT_GAME_TIMER" \
    "$STOCK_SCREENSHOT_GAME_TIMER" \
    "$NATIVE_TRACE" \
    "$STOCK_OUT_TRACE" \
    "$SCREENSHOT_DST" \
    "$NATIVE_SCREENSHOT_JSON" \
    "$NATIVE_RENDER_JSON" \
    "$NATIVE_MOVEMENT_JSON" \
    "$NATIVE_INTRO_SUMMARY_JSON" \
    "$NATIVE_INTRO_AUDIT_JSON" \
    "$STOCK_SCREENSHOT" \
    "$STOCK_SCREENSHOT_JSON" \
    "$STOCK_AUDIT_JSON" \
    "$COMPARE_JSON" \
    "$ACTOR_COMPARE_JSON" \
    "$SUMMARY_COMPARE_JSON" \
    "$VISUAL_COMPARE_JSON" \
    "$VISUAL_COMPARE_TXT" \
    "$VISUAL_COMPARE_HEATMAP" \
    "$HEALTH_COMPARE_JSON" \
    "$ROUTE_EXIT_STATUS" <<'PY'
import json
import sys
from pathlib import Path

(
    summary_path,
    route_name,
    compare_kind,
    route_path,
    native_level,
    stock_level,
    native_frames,
    stock_frames,
    stock_screenshot_frame,
    native_screenshot_game_timer,
    stock_screenshot_game_timer,
    native_trace,
    stock_trace,
    native_screenshot,
    native_screenshot_json,
    native_render_json,
    native_movement_json,
    native_intro_summary_json,
    native_intro_audit_json,
    stock_screenshot,
    stock_screenshot_json,
    stock_audit_json,
    compare_json,
    actor_compare_json,
    summary_compare_json,
    visual_compare_json,
    visual_compare_txt,
    visual_compare_heatmap,
    health_compare_json,
    route_exit_status,
) = sys.argv[1:31]


def existing(path: str) -> str | None:
    return path if path and Path(path).exists() else None


def load_json(path: str) -> dict | None:
    if not path or not Path(path).is_file():
        return None
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data if isinstance(data, dict) else {"value": data}


artifacts = {
    "native_trace": existing(native_trace),
    "stock_trace": existing(stock_trace),
    "native_screenshot": existing(native_screenshot),
    "native_screenshot_health_json": existing(native_screenshot_json),
    "native_render_json": existing(native_render_json),
    "native_movement_json": existing(native_movement_json),
    "native_intro_summary_json": existing(native_intro_summary_json),
    "native_intro_audit_json": existing(native_intro_audit_json),
    "stock_screenshot": existing(stock_screenshot),
    "stock_screenshot_health_json": existing(stock_screenshot_json),
    "stock_audit_json": existing(stock_audit_json),
    "compare_json": existing(compare_json),
    "actor_compare_json": existing(actor_compare_json),
    "summary_compare_json": existing(summary_compare_json),
    "visual_compare_json": existing(visual_compare_json),
    "visual_compare_txt": existing(visual_compare_txt),
    "visual_compare_heatmap": existing(visual_compare_heatmap),
    "health_compare_json": existing(health_compare_json),
}
payload = {
    "status": "pass" if int(route_exit_status) == 0 else "fail",
    "exit_status": int(route_exit_status),
    "route": route_name,
    "route_path": route_path,
    "compare_kind": compare_kind,
    "native_level": int(native_level),
    "stock_level": int(stock_level),
    "native_frames": int(native_frames),
    "stock_frames": int(stock_frames),
    "stock_screenshot_frame": int(stock_screenshot_frame),
    "native_screenshot_game_timer": int(native_screenshot_game_timer) if native_screenshot_game_timer else None,
    "stock_screenshot_game_timer": int(stock_screenshot_game_timer) if stock_screenshot_game_timer else None,
    "artifacts": artifacts,
    "native_screenshot_health": load_json(native_screenshot_json),
    "native_render": load_json(native_render_json),
    "native_movement": load_json(native_movement_json),
    "native_intro_summary": load_json(native_intro_summary_json),
    "native_intro_audit": load_json(native_intro_audit_json),
    "stock_screenshot_health": load_json(stock_screenshot_json),
    "stock_audit": load_json(stock_audit_json),
    "comparison": load_json(compare_json),
    "actor_comparison": load_json(actor_compare_json),
    "summary_comparison": load_json(summary_compare_json),
    "visual_comparison": load_json(visual_compare_json),
    "health_comparison": load_json(health_compare_json),
}
with open(summary_path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
print(f"summary_json: {summary_path}")
PY

echo ""
if [[ "$ROUTE_EXIT_STATUS" -eq 0 ]]; then
    echo "=== ROM Oracle Capture: PASS ==="
else
    echo "=== ROM Oracle Capture: FAIL ==="
fi
exit "$ROUTE_EXIT_STATUS"
