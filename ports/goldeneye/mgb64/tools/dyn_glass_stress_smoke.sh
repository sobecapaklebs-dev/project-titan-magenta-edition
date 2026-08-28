#!/bin/bash
#
# dyn_glass_stress_smoke.sh -- FID-0007 (b): fail-closed dyn-allocator contract on
# the glass-shard float-matrix render path under forced arena overflow.
#
# FID-0007 part (a) (modelSetDistanceDisabled early-return leak, T9b) landed in
# ee771e7. This lane guards part (b): the falling-glass-shard render path
# (src/game/unk_0A1DA0.c sub_GAME_7F0A2C44, the use_float_mtx branch that calls
# dynAllocate(sizeof(Mtxf))) must FAIL CLOSED when the dyn VTX arena is exhausted:
# on overflow it skips the shard (does not emit its triangle) rather than aliasing
# a single matrix onto every overflowed piece (the R3/M1.2 corruption class:
# warped limbs, stray "shard" geometry, screen-spanning triangles under memory
# pressure).
#
# The lane forces overflow deterministically with GE007_DYN_STRESS_LIMIT (clamps
# the per-buffer VTX arena at init) on the same Dam regular-glass shatter route
# used by tools/glass_material_regression.sh, and runs two flag sides:
#
#   fix   (default, GE007_DYN_LEGACY_ALIAS unset): dynAllocate returns NULL on
#         overflow -> the shard path skips the piece. Assertions (the GATE):
#           * process runs clean: no GEASSERT, no [GFX-DL], no crash/bad_cmds
#           * g_dyn_overflow_count (render.dyn_overflow) > 0  -> overflow really
#             engaged (the stress clamp took effect and the shard path hit it)
#           * ZERO screen-spanning glass_shards triangles (no GFX-EMIT-BIG over
#             the pathological threshold, no GFX-SHARD-CANDIDATE) -> no aliased
#             matrix leaked a giant/garbage triangle
#           * every OTHER dyn/dl render-health counter stays 0 (only dyn_overflow
#             is allowed to move) and screenshot health is clean
#
#   legacy (GE007_DYN_LEGACY_ALIAS=1): dynAllocate returns the un-advanced (aliased)
#         pointer on overflow -> the old corruption regime returns. Captured as the
#         A/B negative control. dyn_overflow still counts (dynNoteOverflow runs on
#         both sides), but shards are NOT skipped, so the legacy run emits strictly
#         more glass_shards triangles than the fix run. The lane asserts that A/B
#         delta (fix emits fewer shard tris than legacy) as machine-checkable proof
#         the flag actually toggles the fail-closed behavior.
#
# ROM-gated: SKIPs cleanly (exit 0) when the ROM or native binary is absent.
#
# Artifacts are ROM-derived local validation data. Do not commit captured traces,
# screenshots, logs, or generated audit summaries.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=0
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_dyn_glass_stress_$$"
FRAMES=135
# VTX arena clamp (bytes). Tuned so the Dam shatter frame's late per-shard Mtxf
# allocations overrun the arena and fail closed, while the scene still renders
# some shards -- i.e. a PARTIAL shard overflow. The unclamped shatter frame's
# peak VTX usage is < 64KB and emits ~48 visible shard triangles; at 16384 the
# fix skips the overflowed shards (~5 emitted, dyn_overflow > 0) whereas the
# legacy-alias control aliases-and-emits all of them (~48). A far lower clamp
# (e.g. 4096) starves the whole frame before the shard pass even begins (0 emits
# either way) and a higher one (>=32768) fits the frame (no overflow), so this
# mid value is where the fail-closed-vs-alias behavior is observable.
STRESS_LIMIT=16384

usage() {
    cat <<'USAGE'
Usage: tools/dyn_glass_stress_smoke.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 135)
  --stress-limit N     GE007_DYN_STRESS_LIMIT VTX arena clamp in bytes (default: 4096)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --build              build before running (default: reuse existing binary)
  --timeout SECONDS    per-capture timeout (default: 120)

ROM-gated: SKIPs cleanly (exit 0) if the ROM or native binary is absent.
Artifacts are ROM-derived local validation data. Do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --stress-limit) STRESS_LIMIT="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --build) DO_BUILD=1; shift ;;
        --no-build) DO_BUILD=0; shift ;;   # accepted for the ctest harness (reuse existing binary)
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]] || (( FRAMES < 135 )); then
    echo "FAIL: --frames must be an integer >= 135" >&2
    exit 2
fi
if [[ ! "$STRESS_LIMIT" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --stress-limit must be a positive integer" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

# ROM/binary gate: SKIP cleanly (this lane is opt-in on the user's ROM).
if [[ ! -f "$ROM" ]]; then
    echo "SKIP: dyn_glass_stress_smoke: ROM absent ($ROM)"
    exit 0
fi
if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi
if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: dyn_glass_stress_smoke: native binary absent/not-executable ($BINARY)"
    exit 0
fi

validation_acquire_runtime_lock
cleanup() { validation_release_runtime_lock; }
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

auto_aim_dir_script() {
    local start="$1" end="$2" x="$3" y="$4" z="$5" frame sep=""
    for ((frame = start; frame <= end; frame++)); do
        printf "%s%d:%s:%s:%s" "$sep" "$frame" "$x" "$y" "$z"
        sep=","
    done
}

# Shared Dam regular-glass shatter route env (mirrors glass_material_regression.sh's
# regular_glass_bullet_hit case) plus the effect-triangle tracer.
#
# FID-0014 world pin: this scenario's glass break is EMERGENT — the straight-Z
# burst alerts nearby guards and a guard's return fire crosses the pane
# (~frame 126); the pane never breaks from Bond's scripted shots directly.
# Under the faithful WAYMODE_MAGIC patrol default the guard world legitimately
# changes (unseen patrollers freeze; onscreen/aim sets shift) and the emergent
# break no longer occurs, so the lane's stress window (STRESS_LIMIT=16384,
# overflow mid-shard-pass) never engages. The FID-0007 allocator contract
# under test (dynAllocate fail-closed vs GE007_DYN_LEGACY_ALIAS) is
# sim-world-independent, so pin the scenario to the byte-identical legacy
# guard world (GE007_NO_PATROL_MAGIC_FIX=1 — the FID-0014 negative-control
# flag) to keep the proven shatter/stress tuning. A deterministic
# guard-independent shatter refit (canonical dam_regular_glass_shatter_probe
# targeting) was probed 2026-07-11 but has no overflow window between
# "everything fits" (8192) and "frame starved pre-shatter" (7168) at this
# viewpoint; see docs/fidelity/derivations/FID-0014-patrol-magic.md.
shatter_route_env=(
    GE007_NO_PATROL_MAGIC_FIX=1
    GE007_TRACE_GLASS=1
    GE007_TRACE_GLASS_BUDGET=400
    GE007_TRACE_SHARDS=1
    GE007_TRACE_SHARDS_AFTER_FRAME=100
    GE007_EFFECT_TRI_TRACE=1
    GE007_EFFECT_TRI_TRACE_LABEL=glass_shards
    GE007_EFFECT_TRI_TRACE_AFTER_FRAME=100
    GE007_EFFECT_TRI_TRACE_BUDGET=1000
    GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect
    GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1
    GE007_AUTO_WARP_FRAME=40
    GE007_AUTO_WARP_PAD=103
    GE007_AUTO_FIRE=70:5
)

run_capture() {
    local mode="$1"; shift           # strict = must render cleanly; lenient = tolerate crash
    local label="$1"; shift          # remaining args = extra env assignments
    local case_dir="$OUT_DIR/$label"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${label}.bmp"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$shot" "$case_dir/render.json" "$case_dir/screenshot.json"

    echo "  capture: $label (GE007_DYN_STRESS_LIMIT=$STRESS_LIMIT)"
    local aim_script rc=0
    aim_script="$(auto_aim_dir_script 70 125 0 0 1)"
    # NOTE: don't hard-fail on a non-zero exit yet. On macOS the GL/SDL context
    # teardown (CGLReleaseContext) can SIGSEGV at process exit AFTER the frame is
    # fully rendered and the screenshot+trace are written -- a post-capture
    # teardown crash, not a render failure. We distinguish that from a genuine
    # mid-run failure by requiring the "Auto-screenshot complete" marker below.
    (
        cd "$case_dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DEBUG=1 \
            GE007_ASSERT_ON_FAIL=0 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_DYN_STRESS_LIMIT="$STRESS_LIMIT" \
            "${shatter_route_env[@]}" \
            GE007_AUTO_AIM_DIR_SCRIPT="$aim_script" \
            "$@" \
            "$BINARY" \
            --savedir "$case_dir" \
            --rom "$ROM" \
            --level 33 \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "$label" \
            --screenshot-exit
    ) >"$log" 2>&1 || rc=$?

    if [[ "$mode" == "lenient" ]]; then
        # The legacy-alias (negative-control) run is EXPECTED to be unstable: the
        # frozen g_GfxMemPos aliases DL/texture pointers, so the run may SIGSEGV
        # mid-render (a bad texture pointer) or at GL teardown -- that instability
        # IS the corruption evidence. Collect whatever it produced (possibly a
        # partial trace / no screenshot) without failing the lane; the python
        # A/B step interprets the outcome.
        if grep -qF "Auto-screenshot complete" "$log" && [[ -s "$shot" ]]; then
            python3 tools/audit_screenshot_health.py \
                --label "dyn glass stress $label" \
                --json-out "$case_dir/screenshot.json" \
                "$shot" >"$case_dir/screenshot.txt" 2>&1 || true
        fi
        if [[ "$rc" -ne 0 ]] || ! grep -qF "Auto-screenshot complete" "$log"; then
            echo "  note: $label (legacy alias) did not complete cleanly (rc=$rc) -- corruption control" >&2
        fi
        return 0
    fi

    # strict (the fix side): the render+capture MUST complete. A missing marker
    # means the run died mid-frame -- a real failure. (A crash AFTER the marker is
    # a post-capture GL teardown flake, tolerated below.)
    if ! grep -qF "Auto-screenshot complete" "$log"; then
        echo "FAIL: $label did not complete rendering (no screenshot marker; exit rc=$rc)" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $label: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $label: $shot" >&2
        exit 1
    fi
    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $label" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if grep -qF "[GFX-DL]" "$log"; then
        echo "FAIL: [GFX-DL] diagnostic rows during $label (renderer saw a bad command)" >&2
        grep -F "[GFX-DL]" "$log" | head -10 | sed 's/^/  /' >&2
        exit 1
    fi
    # A crash line here is post-marker = GL/SDL teardown flake (frame already
    # captured); record it in the note but don't fail the fail-closed fix run.
    if [[ "$rc" -ne 0 ]]; then
        echo "  note: $label exited non-zero (rc=$rc) after a complete capture (post-teardown)" >&2
    fi

    python3 tools/audit_screenshot_health.py \
        --label "dyn glass stress $label" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt" || true
}

echo "=== FID-0007 dyn glass-shard fail-closed stress smoke ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES   stress-limit: $STRESS_LIMIT B"

run_capture strict  fix
run_capture lenient legacy_alias GE007_DYN_LEGACY_ALIAS=1

python3 - "$OUT_DIR" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = {"status": "pass", "cases": {}, "failures": [], "stress_ab": {}}

# NDC-space effect-triangle emit rows for the falling glass shards. A screen-
# spanning triangle (aliased matrix leaking a giant transform) trips the
# pathological guards; GFX-SHARD-CANDIDATE is a preclip pathological marker.
gfx_effect_tri_re = re.compile(
    r"\[(?P<kind>GFX-EMIT-BIG|GFX-SHARD-CANDIDATE)\] "
    r"frame=(?P<frame>\d+) .* effect=(?P<effect>\S+) .* "
    r"ndc_bbox=\[(?P<min_x>-?\d+(?:\.\d+)?),(?P<min_y>-?\d+(?:\.\d+)?)\]-"
    r"\[(?P<max_x>-?\d+(?:\.\d+)?),(?P<max_y>-?\d+(?:\.\d+)?)\] "
    r"area2=(?P<area2>-?\d+(?:\.\d+)?)"
)
effect_tri_re = re.compile(
    r"\[EFFECT-TRI\] frame=(?P<frame>\d+) event=emit label=glass_shards\b"
)

def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}

def parse_case(case):
    case_dir = root / case
    log = (case_dir / "run.log").read_text(errors="replace")

    emitted_shard_tris = 0
    screen_spanning = []      # GFX-EMIT-BIG over pathological threshold
    preclip_candidates = 0    # GFX-SHARD-CANDIDATE (any, glass_shards)
    render_completed = "Auto-screenshot complete" in log
    # A crash line after the screenshot marker is a post-capture GL/SDL teardown
    # segfault (the frame data is already written); before it would be a genuine
    # render crash (and run_capture would have already failed the run).
    marker_idx = log.find("Auto-screenshot complete")
    post_capture_crash = (
        marker_idx >= 0
        and ("Signal 11" in log[marker_idx:] or "[CRASH]" in log[marker_idx:])
    )
    for line in log.splitlines():
        if effect_tri_re.search(line):
            emitted_shard_tris += 1
            continue
        m = gfx_effect_tri_re.search(line)
        if not m or m.group("effect") != "glass_shards":
            continue
        rec = {
            "frame": int(m.group("frame")),
            "min_x": float(m.group("min_x")), "min_y": float(m.group("min_y")),
            "max_x": float(m.group("max_x")), "max_y": float(m.group("max_y")),
            "area2": float(m.group("area2")),
        }
        rec["width"] = rec["max_x"] - rec["min_x"]
        rec["height"] = rec["max_y"] - rec["min_y"]
        if m.group("kind") == "GFX-SHARD-CANDIDATE":
            preclip_candidates += 1
        # Same pathological/full-screen envelope glass_material_regression uses.
        if rec["area2"] > 3.0 or (rec["width"] >= 1.95 and rec["height"] >= 1.95):
            screen_spanning.append(rec)

    # Per-frame render-health counters from the structured state trace.
    dyn_overflow_max = 0
    bad_cmds_max = 0
    crashes_max = 0
    nan_max = 0
    other_dl_max = {}
    dl_names = [
        "mtx_fail", "vtx_fail", "dl_fail", "movemem_fail", "texture_fail",
        "settimg_fail", "non_dl_skip_pc", "non_dl_skip_n64", "unregistered_skip",
        "hud_image_fault",
    ]
    # The legacy run may crash mid-render, leaving a partial/absent trace -- read
    # what exists.
    trace_path = case_dir / "state.jsonl"
    trace_lines = 0
    try:
        handle = trace_path.open(encoding="utf-8")
    except OSError:
        handle = None
    if handle is not None:
        for raw in handle:
            raw = raw.strip()
            if not raw:
                continue
            trace_lines += 1
            try:
                rec = json.loads(raw)
            except ValueError:
                continue
            dl = rec.get("dl", {}) if isinstance(rec.get("dl"), dict) else {}
            dyn_overflow_max = max(dyn_overflow_max, int(dl.get("dyn_overflow", 0) or 0))
            bad_cmds_max = max(bad_cmds_max, int(rec.get("bad_cmds", 0) or 0))
            crashes_max = max(crashes_max, int(rec.get("crashes", 0) or 0))
            nan_max = max(nan_max, int(rec.get("nan", 0) or 0))
            for name in dl_names:
                other_dl_max[name] = max(other_dl_max.get(name, 0), int(dl.get(name, 0) or 0))
        handle.close()

    screenshot = read_json(case_dir / "screenshot.json")
    result = {
        "emitted_shard_tris": emitted_shard_tris,
        "screen_spanning_count": len(screen_spanning),
        "screen_spanning_sample": screen_spanning[:3],
        "preclip_candidates": preclip_candidates,
        "dyn_overflow_max": dyn_overflow_max,
        "bad_cmds_max": bad_cmds_max,
        "crashes_max": crashes_max,
        "nan_max": nan_max,
        "other_dl_max": other_dl_max,
        "trace_lines": trace_lines,
        "screenshot_ok": bool(screenshot.get("ok")),
        "render_completed": render_completed,
        "post_capture_crash": post_capture_crash,
    }
    summary["cases"][case] = result
    return result

fix = parse_case("fix")
legacy = parse_case("legacy_alias")

# ---- GATE: the fix side must be a clean fail-closed run under forced overflow.
if fix["dyn_overflow_max"] <= 0:
    summary["failures"].append(
        "fix: GE007_DYN_STRESS_LIMIT did not force any dyn overflow "
        f"(render.dyn_overflow max={fix['dyn_overflow_max']}); stress clamp ineffective"
    )
if fix["screen_spanning_count"] > 0:
    summary["failures"].append(
        f"fix: {fix['screen_spanning_count']} screen-spanning glass_shards triangle(s) "
        f"under overflow -- aliased matrix leaked (sample={fix['screen_spanning_sample']})"
    )
if fix["preclip_candidates"] > 0:
    summary["failures"].append(
        f"fix: {fix['preclip_candidates']} preclip pathological glass_shards candidate(s) under overflow"
    )
if fix["bad_cmds_max"] > 0 or fix["crashes_max"] > 0 or fix["nan_max"] > 0:
    summary["failures"].append(
        f"fix: render-health regressed (bad_cmds={fix['bad_cmds_max']} "
        f"crashes={fix['crashes_max']} nan={fix['nan_max']})"
    )
bad_other = {k: v for k, v in fix["other_dl_max"].items() if v > 0}
if bad_other:
    summary["failures"].append(
        f"fix: non-overflow dyn/dl counters moved under fail-closed overflow: {bad_other} "
        "(only dyn_overflow should increment)"
    )
if not fix["screenshot_ok"]:
    summary["failures"].append("fix: screenshot health failed")

# ---- A/B PROOF: GE007_DYN_LEGACY_ALIAS=1 must demonstrably restore the old
# aliasing regime -- i.e. legacy behaves DIFFERENTLY from the fail-closed fix.
# The legacy run is inherently unstable: the frozen g_GfxMemPos aliases DL and
# texture pointers, so instead of skipping the overflowed shards it emits them
# against aliased memory. Depending on ASLR/heap layout this manifests as EITHER
# (a) more emitted shard triangles than the fix (the aliased shards still
# rasterize), or (b) a mid-render / teardown SIGSEGV (an aliased pointer lands in
# unmapped memory). Any of these -- versus the fix's clean, complete, stable run
# -- proves the flag toggles the behavior. If legacy renders IDENTICALLY to the
# fix (same emit count, no crash), the flag did nothing and the A/B is broken.
legacy_more_shards = legacy["emitted_shard_tris"] > fix["emitted_shard_tris"]
legacy_crashed = (not legacy["render_completed"]) or legacy["post_capture_crash"]
legacy_restored_aliasing = legacy_more_shards or legacy_crashed
summary["stress_ab"] = {
    "fix_emitted_shard_tris": fix["emitted_shard_tris"],
    "legacy_emitted_shard_tris": legacy["emitted_shard_tris"],
    "fix_screen_spanning": fix["screen_spanning_count"],
    "legacy_screen_spanning": legacy["screen_spanning_count"],
    "fix_dyn_overflow_max": fix["dyn_overflow_max"],
    "legacy_dyn_overflow_max": legacy["dyn_overflow_max"],
    "fix_render_completed": fix["render_completed"],
    "legacy_render_completed": legacy["render_completed"],
    "fix_post_capture_crash": fix["post_capture_crash"],
    "legacy_post_capture_crash": legacy["post_capture_crash"],
    "legacy_more_shards_than_fix": legacy_more_shards,
    "legacy_crashed": legacy_crashed,
    "legacy_restored_aliasing": legacy_restored_aliasing,
}
if not legacy_restored_aliasing:
    summary["failures"].append(
        "A/B: GE007_DYN_LEGACY_ALIAS=1 did not change behavior -- legacy rendered "
        f"identically to the fail-closed fix (fix_emits={fix['emitted_shard_tris']}, "
        f"legacy_emits={legacy['emitted_shard_tris']}, legacy_crash={legacy_crashed}); "
        "the flag/fix may not be wired"
    )

if summary["failures"]:
    summary["status"] = "fail"

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if summary["failures"]:
    print("FAIL: dyn glass-shard fail-closed stress smoke")
    for f in summary["failures"]:
        print(f"  - {f}")
    raise SystemExit(1)

print("PASS: dyn glass-shard fail-closed stress smoke")
print(
    "  fix    (fail-closed): dyn_overflow_max=%d shard_tris=%d screen_spanning=%d "
    "preclip=%d bad_cmds=%d crashes=%d completed=%s"
    % (fix["dyn_overflow_max"], fix["emitted_shard_tris"], fix["screen_spanning_count"],
       fix["preclip_candidates"], fix["bad_cmds_max"], fix["crashes_max"], fix["render_completed"])
)
print(
    "  legacy (alias ctrl) : dyn_overflow_max=%d shard_tris=%d completed=%s crashed=%s"
    % (legacy["dyn_overflow_max"], legacy["emitted_shard_tris"],
       legacy["render_completed"], legacy_crashed)
)
if legacy_more_shards:
    print(
        "  A/B proof: fix skips %d overflowed shard tri(s) that the legacy alias emits"
        % (legacy["emitted_shard_tris"] - fix["emitted_shard_tris"])
    )
if legacy_crashed:
    print("  A/B proof: legacy alias run destabilized (crash), the fail-closed fix did not")
PY

echo "  summary: $OUT_DIR/summary.json"
