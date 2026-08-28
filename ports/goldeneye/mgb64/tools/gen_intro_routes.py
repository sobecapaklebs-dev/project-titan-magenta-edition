#!/usr/bin/env python3
"""T4 -- generate per-stage full-window intro oracle routes for all 20 solo
stages from a small table, so the 40 route files (minus Dam, kept
hand-maintained/canonical) stay consistent and regenerable.

Emits into tools/rom_oracle_routes/<slug>_intro_camera_path.json and
<slug>_intro_swirl_bond_anim.json. Dam is NEVER (re)written by this tool --
the T2 pass hardened dam_intro_camera_path.json
and dam_intro_swirl_bond_anim.json by hand and they remain the canonical
template; this generator explicitly skips "dam" with a note.

Mission order / stage IDs are src/platform/main_pc.c's kPcStartStages table
(same order the brief's coverage table is a re-statement of). Per-stage
camera/swirl-point counts are from the T3 parse-digest coverage sweep.

Stock-side menu navigation: a fresh EEPROM save only unlocks Dam
(mission 1); tools/make_unlocked_eeprom.py + movement_oracle_capture.sh's
--seed-eeprom pre-seed an all-unlocked save so the ares oracle's OWN
frontend can navigate to any mission. This generator emits the
navigation as `stock_events`: the same START mash Dam's route uses, a
bounded DOWN-press burst sized to (mission_num - 1) presses, then the
same A-confirm mash Dam's route uses to click through the remaining
menu screens (file-select confirm, mission confirm, difficulty confirm
[default Agent], start).

Camera-pin discovery is NOT automatic: run the STOCK capture first, read
which camera index the stock deterministic boot selects (the trace's
`intro.selected_camera`/`selected_intro_camera*` fields), map it to the
native camera list (GE007_VERBOSE=1 `[INTRO_CAMERA] idx=N pos=...` dump or
the T3 digest tool), then pass `--camera-pin SLUG=N` (or the `camera_pins`
dict to write_routes()) to bake the discovered pin in. Until discovered,
routes pin index 0 (native's own deterministic-mode default, matching the
port's current D28-affected behavior) as a safe placeholder -- native_frames
is generous enough (1800 ticks) that a wrong pin still produces a
comparable (if divergent) run rather than a crash.

CLI:
    tools/gen_intro_routes.py --out-dir tools/rom_oracle_routes
    tools/gen_intro_routes.py --out-dir tools/rom_oracle_routes \\
        --camera-pin facility=1 --camera-pin statue=0
"""
from __future__ import annotations

import argparse
import copy
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROUTE_DIR = Path(__file__).resolve().parent / "rom_oracle_routes"
SCHEMA = "mgb64.rom_oracle.route.v1"

# Dam is hand-maintained (T2-hardened canonical template) and never
# regenerated/overwritten by this tool.
CANONICAL_SLUGS = {"dam"}


@dataclass(frozen=True)
class Stage:
    stage_id: int      # raw LEVELID (matches src/platform/main_pc.c kPcStartStages)
    slug: str          # --level slug
    mission_num: int   # 1-based solo campaign order (== SP_LEVEL_* index + 1)
    cameras: int       # authored INTROTYPE_CAMERA count (T3 digest sweep)
    swirl_pts: int     # authored INTROTYPE_SWIRL control-point count
    notes: str = ""


# Table order mirrors src/platform/main_pc.c's kPcStartStages (== the brief's
# coverage table order). Kept as source-of-truth here so route generation is
# a pure function of this table.
STAGES: tuple[Stage, ...] = (
    Stage(33, "dam", 1, 6, 11, "template (hand-maintained, not regenerated)"),
    Stage(34, "facility", 2, 4, 12, "only stage using swirl flag &4"),
    Stage(35, "runway", 3, 6, 10),
    Stage(36, "surface1", 4, 3, 16, "long swirl"),
    Stage(9, "bunker1", 5, 5, 9, "has INTROTYPE_ANIM (anim idx 1)"),
    Stage(20, "silo", 6, 1, 12, "single cinema cam"),
    Stage(26, "frigate", 7, 5, 10),
    Stage(43, "surface2", 8, 3, 10),
    Stage(27, "bunker2", 9, 3, 9),
    Stage(22, "statue", 10, 3, 11),
    Stage(24, "archives", 11, 2, 11),
    Stage(29, "streets", 12, 2, 9),
    Stage(30, "depot", 13, 3, 11),
    Stage(25, "train", 14, 1, 10),
    Stage(37, "jungle", 15, 3, 10),
    Stage(23, "control", 16, 2, 9),
    Stage(39, "caverns", 17, 4, 9),
    Stage(41, "cradle", 18, 2, 16, "long swirl"),
    Stage(28, "aztec", 19, 3, 9, "bonus stage -- proves unlock bits"),
    Stage(32, "egypt", 20, 2, 8, "bonus stage"),
)
STAGES_BY_SLUG: dict[str, Stage] = {s.slug: s for s in STAGES}

# Shared timing/tolerance defaults, lifted from the T2-hardened Dam routes.
NATIVE_FRAMES_DEFAULT = 1800
STOCK_FRAMES_DEFAULT = 6500
VECTOR_TOLERANCE = 0.05
DIRECTION_TOLERANCE = 0.005
SCALAR_TOLERANCE = 0.001
ANIM_VECTOR_TOLERANCE = 0.15
ANIM_TOLERANCE = 0.03
ANIM_MIN_ALIGNED = 20

# Dam's stock_events cadence, reused verbatim for every stage's START mash
# (settles the boot into the frontend). Everything downstream of that is
# EMPIRICAL, measured by instrumented diagnostic captures against the real
# stock ROM -- read off a sequence of screenshots taken at successively
# later stock_screenshot_frame values (see the T4 report's navigation
# timeline section for the actual images):
#
#   input_frame runs ~4x video "f" once past the boot transient (ares polls
#   all 4 controller ports every video frame; only port 1 carries our
#   scripted input) -- so route event start/every/until values (all in
#   input_frame units) land at roughly video_frame/4 of what a naive "1
#   tick per frame" reading would suggest.
#
#   The frontend cascade is: copyright/legal splash -> logo/title fade ->
#   SELECT FILE (three/four dossier folders; confirmed on-screen by
#   video_frame 650) -> a 5-COLUMN x 4-ROW MISSION GRID (all 20 missions
#   shown at once, Dam top-left) -> a per-mission "briefing document"
#   (mission name/checkmarks + a 4-row Agent/Secret Agent/00 Agent/007
#   difficulty list) -> an "M BRIEFING" text page -> gameplay.
#
#   ALL of these menus are navigated with the ANALOG STICK as a free 2D
#   cursor, NOT the D-pad or C-buttons (confirmed by screenshot: a button
#   tap moved nothing, a stick-right hold moved the cursor a proportional
#   distance). Cursor speed is constant (not accelerating) and screen-
#   specific: holding right/back on the mission grid moves exactly one
#   cell per 40 held frames (verified: 40f -> Facility, 80f -> Runway, a
#   single 40f "back" -> Silo directly below Dam); holding forward on the
#   difficulty list moves exactly one row per 20 held frames (verified: a
#   single 60f pulse from the default cursor lands exactly 3 rows up).
#
#   SELECT FILE defaults its cursor to the SECOND folder (not the first),
#   with no input at all. tools/make_unlocked_eeprom.py's active save
#   lives in FOLDER1 (the first/leftmost folder), so every seeded route
#   must tap left once before confirming file-select.
#
#   Because that same seeded save marks Agent/Secret Agent/00 Agent ALL
#   completed (per the brief, so the Aztec/Egypt bonus-stage gates are
#   satisfied too), the briefing's difficulty cursor defaults to the first
#   NOT-yet-completed row -- "4. 007" -- instead of Agent. Every generated
#   route must walk it back up 3 rows (a constant correction, since every
#   stage's save data is uniformly "all sub-007 difficulties complete").
#
#   The final "M BRIEFING" page is confirmed with START, not A (confirmed:
#   an A-only mash left `oracle.stage` stuck at the frontend sentinel (90)
#   for the full capture window; adding a trailing START mash reached the
#   target stage).
#
# Full sequence per generated (non-Dam) route: START mash (unchanged) ->
# wait for SELECT FILE -> tap LEFT (land on folder 1) -> tap A (confirm
# file, enter the mission grid on Dam's cell) -> hold RIGHT for
# col*GRID_STEP frames, then hold BACK for row*GRID_STEP frames (grid
# position (row, col) = divmod(mission_num - 1, 5)) -> tap A (confirm
# mission, enter the briefing) -> hold FORWARD for 60 frames (difficulty
# cursor 007 -> Agent) -> tap A (confirm difficulty, enter M BRIEFING) ->
# START mash (launch the mission).
_START_EVENT: dict[str, Any] = {
    "start": 100,
    "len": 3,
    "buttons": ["start"],
    "phase": "menu",
    "repeat": {"every": 240, "until": 2260},
}

# SELECT FILE is confirmed on-screen by input_frame ~2260 (video_frame 650);
# give it margin so the correction never fires before the screen exists.
_FILE_LEFT_START = 2300
_FILE_LEFT_LEN = 3

_FILE_CONFIRM_GAP = 57
_FILE_CONFIRM_LEN = 3

_GRID_MOVE_GAP = 137
_GRID_AXIS_GAP = 60       # gap between the last right-hold and the back-hold
_GRID_COLUMNS = 5

# Grid-cursor speed is NOT linear in hold duration (measured directly,
# repeatedly, against the real stock ROM -- see the T4 report's navigation
# timeline for the screenshots): holding right for 40 frames lands exactly
# 1 cell over (Dam -> Facility); 80 frames lands exactly 2 (-> Runway); 120
# frames lands on the SAME cell as 80 (no further movement -- a plateau);
# 160 frames lands exactly 3 over (-> Surface); 200-400 frames all land on
# the last column (-> Bunker, col 4, hard-clamped at the grid edge -- the
# cursor visibly overshoots the tile and pins at the screen edge). Neither
# "continuous hold" nor "discrete released pulses spaced 30-110 frames
# apart" changed this curve -- both were tested and both undershot for 3-4
# cell moves. This table is the measured hold duration (single continuous
# hold, input-frames) that reaches EXACTLY N cells; back/up (row movement)
# was spot-checked at N=1 (40 -> Silo, directly below Dam) and N=2 (80 ->
# Archives, row 2 col 0) and matches the same curve, so it's reused for
# both axes. col=4 and row=3 are each their axis's last (edge) index, so a
# generous value that's merely "at least far enough" is safe there (no
# overshoot risk past a hard clamp); the interior values (1-3) are exact
# measurements, not extrapolated, because overshooting an interior cell
# lands on the WRONG mission with no way to detect it from the route alone.
_GRID_STEP_DURATIONS = {1: 40, 2: 80, 3: 160, 4: 220}

_GRID_CONFIRM_GAP = 60   # measured: right-pulse end (2540) -> grid-confirm A start (2600)
_GRID_CONFIRM_LEN = 3

_DIFFICULTY_FORWARD_GAP = 97
_DIFFICULTY_FORWARD_LEN = 60  # constant: always corrects the default 007 -> Agent

_DIFFICULTY_CONFIRM_GAP = 60
_DIFFICULTY_CONFIRM_LEN = 3
_DIFFICULTY_CONFIRM_EVERY = 40
_DIFFICULTY_CONFIRM_COUNT = 3  # proven-working mash, matches the launch step's margin

_LAUNCH_GAP = 97
_LAUNCH_EVERY = 40
_LAUNCH_COUNT = 5  # START mash that actually launches the mission from M BRIEFING

# The launch mash intentionally outlives the moment gameplay actually
# starts (we don't know in advance which repeat lands the launch), so the
# ares harness's own menuClosed gate suppresses the trailing repeats once a
# real player exists -- confirmed on a real facility capture (4 suppressed
# of the 5-press launch mash, mission still loaded correctly). Dam's own
# hand-tuned route can afford stock_max_suppressed_menu_records=0 because
# its single short A-mash is precisely sized; this generator's mash-heavy,
# stage-agnostic sequence needs headroom instead.
STOCK_MAX_SUPPRESSED_MENU_RECORDS = 10


def mission_grid_position(stage: Stage) -> tuple[int, int]:
    """(row, col) on the 5-wide mission grid, 0-indexed from Dam (0, 0)."""
    index = stage.mission_num - 1
    return index // _GRID_COLUMNS, index % _GRID_COLUMNS


def down_press_count(stage: Stage) -> int:
    """Grid columns to the right of Dam's cell (kept for backward-compat
    call sites/tests; mission_grid_position() is the full picture)."""
    _, col = mission_grid_position(stage)
    return col


def _stock_events(stage: Stage) -> list[dict[str, Any]]:
    events = [copy.deepcopy(_START_EVENT)]

    events.append({"start": _FILE_LEFT_START, "len": _FILE_LEFT_LEN, "buttons": ["cleft"], "phase": "menu"})
    cursor = _FILE_LEFT_START + _FILE_LEFT_LEN + _FILE_CONFIRM_GAP

    file_confirm_start = cursor
    events.append({"start": file_confirm_start, "len": _FILE_CONFIRM_LEN, "buttons": ["a"], "phase": "menu"})
    cursor = file_confirm_start + _FILE_CONFIRM_LEN + _GRID_MOVE_GAP

    row, col = mission_grid_position(stage)
    last_movement_end = cursor  # if both col==0 and row==0 this can't happen: Dam is excluded from generation
    if col > 0:
        right_len = _GRID_STEP_DURATIONS[col]
        events.append({"start": cursor, "len": right_len, "buttons": [], "phase": "menu", "right": True})
        last_movement_end = cursor + right_len
        cursor = last_movement_end + _GRID_AXIS_GAP
    if row > 0:
        back_len = _GRID_STEP_DURATIONS[row]
        events.append({"start": cursor, "len": back_len, "buttons": [], "phase": "menu", "back": True})
        last_movement_end = cursor + back_len

    grid_confirm_start = last_movement_end + _GRID_CONFIRM_GAP
    events.append({"start": grid_confirm_start, "len": _GRID_CONFIRM_LEN, "buttons": ["a"], "phase": "menu"})
    cursor = grid_confirm_start + _GRID_CONFIRM_LEN + _DIFFICULTY_FORWARD_GAP

    forward_start = cursor
    events.append(
        {"start": forward_start, "len": _DIFFICULTY_FORWARD_LEN, "buttons": [], "phase": "menu", "forward": True}
    )
    cursor = forward_start + _DIFFICULTY_FORWARD_LEN + _DIFFICULTY_CONFIRM_GAP

    difficulty_confirm_start = cursor
    events.append(
        {
            "start": difficulty_confirm_start,
            "len": _DIFFICULTY_CONFIRM_LEN,
            "buttons": ["a"],
            "phase": "menu",
            "repeat": {"every": _DIFFICULTY_CONFIRM_EVERY, "count": _DIFFICULTY_CONFIRM_COUNT},
        }
    )
    difficulty_confirm_end = (
        difficulty_confirm_start + (_DIFFICULTY_CONFIRM_COUNT - 1) * _DIFFICULTY_CONFIRM_EVERY + _DIFFICULTY_CONFIRM_LEN
    )
    cursor = difficulty_confirm_end + _LAUNCH_GAP

    launch_start = cursor
    events.append(
        {
            "start": launch_start,
            "len": 3,
            "buttons": ["start"],
            "phase": "menu",
            "repeat": {"every": _LAUNCH_EVERY, "count": _LAUNCH_COUNT},
        }
    )
    return events


def _native_env(stage: Stage, camera_pin: int) -> dict[str, str]:
    return {
        "GE007_ENABLE_LEVEL_INTRO": "1",
        "GE007_INTRO_CAMERA_INDEX": str(camera_pin),
    }


def _shared_native_intro_audit_fields() -> dict[str, Any]:
    # Structural smoke thresholds carried over from the Dam routes: Bond's
    # intro chr load/anim/render path is shared (not stage-gated) code, so
    # these should hold generically. Deliberately DROPS Dam's
    # native_intro_require_bond_right_item pin -- that specific weapon-hold
    # value has only been confirmed on Dam; asserting it unverified on 19
    # other stages risks a spurious native-side audit failure unrelated to
    # the actual camera-path/swirl comparison this task measures.
    return {
        "native_intro_audit": True,
        "native_intro_require_player": True,
        "native_intro_require_frozen": True,
        "native_intro_require_bond_present": True,
        "native_intro_require_bond_onscreen": True,
        "native_intro_require_bond_model_mtx": True,
        "native_intro_require_bond_rendered": True,
        "native_intro_require_bond_anim": True,
        "native_intro_require_bond_anim_hash": True,
        "native_intro_min_active_records": 180,
        "native_intro_min_present_frames": 180,
        "native_intro_min_onscreen_frames": 180,
        "native_intro_min_model_mtx_frames": 180,
        "native_intro_min_rendered_frames": 180,
        "native_intro_min_render_count": 180,
        "native_intro_min_anim_frames": 180,
        "native_intro_min_anim_hash_frames": 180,
        "native_intro_min_anim_advance": 1.0,
        "native_intro_max_first_present_frame": 700,
        "native_intro_max_first_render_frame": 700,
        # H17 (docs plan theory register): first-swirl-tick applied_view
        # must not still be the degenerate (1,0,0) seed.
        "native_intro_require_h17_swirl_facing": True,
    }


def build_camera_path_route(stage: Stage, camera_pin: int = 0) -> dict[str, Any]:
    route: dict[str, Any] = {
        "schema": SCHEMA,
        "name": f"{stage.slug}_intro_camera_path",
        "description": (
            f"{stage.slug.capitalize()} authored level-intro camera path comparison "
            "over active intro/fadeswirl/swirl frames."
        ),
        "level": str(stage.stage_id),
        "native_frames": NATIVE_FRAMES_DEFAULT,
        "stock_frames": STOCK_FRAMES_DEFAULT,
        "native_env": _native_env(stage, camera_pin),
        "native_render_audit": True,
        "compare_kind": "intro",
        "compare_align": "per-mode",
        "compare_profile": "path",
        "compare_state": True,
        "compare_selected_camera": True,
        "compare_intro_setup": True,
        "compare_bond_anim": False,
        "compare_camera_modes": "intro,fadeswirl,swirl",
        "compare_require_frozen": True,
        "compare_vector_tolerance": VECTOR_TOLERANCE,
        "compare_direction_tolerance": DIRECTION_TOLERANCE,
        "compare_scalar_tolerance": SCALAR_TOLERANCE,
        "compare_anim_tolerance": ANIM_TOLERANCE,
        # compare_expect_mode_durations intentionally absent: filled in AFTER
        # the first real capture per stage from the STOCK trace's measured
        # mode-record counts (zero tolerance, like Dam) -- see the report.
        "compare_waivers": {},
        **_shared_native_intro_audit_fields(),
        "stock_max_suppressed_menu_records": STOCK_MAX_SUPPRESSED_MENU_RECORDS,
        "events": [],
        "stock_events": _stock_events(stage),
    }
    return route


def build_swirl_bond_anim_route(stage: Stage, camera_pin: int = 0) -> dict[str, Any]:
    route: dict[str, Any] = {
        "schema": SCHEMA,
        "name": f"{stage.slug}_intro_swirl_bond_anim",
        "description": (
            f"{stage.slug.capitalize()} intro swirl camera and Bond animation comparison "
            "aligned by authored intro timer."
        ),
        "level": str(stage.stage_id),
        "native_frames": NATIVE_FRAMES_DEFAULT,
        "stock_frames": STOCK_FRAMES_DEFAULT,
        "native_env": _native_env(stage, camera_pin),
        "native_render_audit": True,
        "compare_kind": "intro",
        "compare_align": "per-mode",
        "compare_profile": "path",
        "compare_state": True,
        "compare_selected_camera": True,
        "compare_intro_setup": True,
        "compare_bond_anim": True,
        "compare_min_aligned": ANIM_MIN_ALIGNED,
        "compare_camera_modes": "swirl",
        "compare_require_frozen": True,
        "compare_vector_tolerance": ANIM_VECTOR_TOLERANCE,
        "compare_direction_tolerance": DIRECTION_TOLERANCE,
        "compare_scalar_tolerance": SCALAR_TOLERANCE,
        "compare_anim_tolerance": ANIM_TOLERANCE,
        "compare_waivers": {},
        **_shared_native_intro_audit_fields(),
        "stock_max_suppressed_menu_records": STOCK_MAX_SUPPRESSED_MENU_RECORDS,
        "events": [],
        "stock_events": _stock_events(stage),
    }
    return route


def write_routes(
    out_dir: Path, camera_pins: dict[str, int] | None = None, stages: tuple[Stage, ...] = STAGES
) -> tuple[list[Path], list[str]]:
    """Writes every stage's pair of route JSONs into out_dir, skipping
    CANONICAL_SLUGS (Dam). Returns (written_paths, skipped_slugs)."""
    camera_pins = camera_pins or {}
    out_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    skipped: list[str] = []

    for stage in stages:
        if stage.slug in CANONICAL_SLUGS:
            skipped.append(stage.slug)
            continue

        pin = camera_pins.get(stage.slug, 0)
        for builder, suffix in (
            (build_camera_path_route, "intro_camera_path"),
            (build_swirl_bond_anim_route, "intro_swirl_bond_anim"),
        ):
            route = builder(stage, camera_pin=pin)
            path = out_dir / f"{stage.slug}_{suffix}.json"
            path.write_text(json.dumps(route, indent=2) + "\n", encoding="utf-8")
            written.append(path)

    return written, skipped


def _parse_camera_pin(spec: str) -> tuple[str, int]:
    if "=" not in spec:
        raise argparse.ArgumentTypeError(f"--camera-pin must be SLUG=INDEX: {spec!r}")
    slug, _, index_text = spec.partition("=")
    slug = slug.strip()
    if slug not in STAGES_BY_SLUG:
        raise argparse.ArgumentTypeError(f"--camera-pin unknown stage slug: {slug!r}")
    try:
        index = int(index_text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"--camera-pin index must be an integer: {spec!r}") from None
    return slug, index


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", default=str(ROUTE_DIR), help="route output directory (default: tools/rom_oracle_routes)")
    parser.add_argument(
        "--camera-pin",
        action="append",
        default=[],
        metavar="SLUG=INDEX",
        help="pin a stage's GE007_INTRO_CAMERA_INDEX to the stock-discovered camera index; may be repeated",
    )
    args = parser.parse_args(argv)

    camera_pins = dict(_parse_camera_pin(spec) for spec in args.camera_pin)
    written, skipped = write_routes(Path(args.out_dir), camera_pins=camera_pins)

    for slug in skipped:
        print(f"skip: {slug} (canonical, hand-maintained -- not regenerated)")
    for path in written:
        print(f"wrote: {path}")
    print(f"wrote {len(written)} route file(s); skipped {len(skipped)} canonical stage(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
