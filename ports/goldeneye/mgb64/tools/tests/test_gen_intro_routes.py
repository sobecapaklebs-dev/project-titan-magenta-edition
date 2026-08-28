"""T4 -- unit tests for tools/gen_intro_routes.py.

Covers: the per-stage coverage table matches the task brief's 20-stage
table exactly; Dam is never regenerated/overwritten; every generated route
is schema-valid per tools/rom_oracle_route.py's OWN validator (reused
directly as the oracle, not reimplemented); menu-navigation down-press
counts match each stage's 1-based mission index; camera-pin overrides are
threaded through.

Nothing here touches a ROM, ares, or the native binary.

Run: python3 -m unittest tools.tests.test_gen_intro_routes
 or: python3 tools/tests/test_gen_intro_routes.py
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import gen_intro_routes as gir  # noqa: E402
from tools import rom_oracle_route as ror  # noqa: E402


class StageTableTest(unittest.TestCase):
    def test_has_all_20_stages(self) -> None:
        self.assertEqual(len(gir.STAGES), 20)
        self.assertEqual(len({s.slug for s in gir.STAGES}), 20)
        self.assertEqual(len({s.stage_id for s in gir.STAGES}), 20)

    def test_dam_is_mission_1_and_first(self) -> None:
        dam = gir.STAGES_BY_SLUG["dam"]
        self.assertEqual(dam.stage_id, 33)
        self.assertEqual(dam.mission_num, 1)

    def test_mission_numbers_are_1_through_20_matching_kpcstartstages_order(self) -> None:
        # Table order in tools/gen_intro_routes.py mirrors src/platform/main_pc.c's
        # kPcStartStages (dam, facility, runway, surface1, bunker1, silo,
        # frigate, surface2, bunker2, statue, archives, streets, depot,
        # train, jungle, control, caverns, cradle, aztec, egypt).
        expected_order = [
            "dam", "facility", "runway", "surface1", "bunker1", "silo", "frigate",
            "surface2", "bunker2", "statue", "archives", "streets", "depot", "train",
            "jungle", "control", "caverns", "cradle", "aztec", "egypt",
        ]
        self.assertEqual([s.slug for s in gir.STAGES], expected_order)
        self.assertEqual([s.mission_num for s in gir.STAGES], list(range(1, 21)))

    def test_brief_coverage_facts_spot_check(self) -> None:
        # Spot-check a handful of rows against the brief's coverage table
        # (stage id, cameras, swirl pts).
        checks = {
            "facility": (34, 4, 12),
            "silo": (20, 1, 12),
            "train": (25, 1, 10),
            "cradle": (41, 2, 16),
            "aztec": (28, 3, 9),
            "egypt": (32, 2, 8),
            "surface1": (36, 3, 16),
        }
        for slug, (stage_id, cameras, swirl_pts) in checks.items():
            with self.subTest(slug=slug):
                row = gir.STAGES_BY_SLUG[slug]
                self.assertEqual(row.stage_id, stage_id)
                self.assertEqual(row.cameras, cameras)
                self.assertEqual(row.swirl_pts, swirl_pts)


class MissionGridPositionTest(unittest.TestCase):
    """The mission-select screen is a 5-column x 4-row grid (Dam top-left),
    navigated with the analog stick as a free 2D cursor (verified
    empirically: a button tap moved nothing; a stick-right hold moved the
    cursor a distance proportional to how long it was held)."""

    def test_dam_is_top_left(self) -> None:
        self.assertEqual(gir.mission_grid_position(gir.STAGES_BY_SLUG["dam"]), (0, 0))

    def test_facility_is_one_column_right_of_dam(self) -> None:
        self.assertEqual(gir.mission_grid_position(gir.STAGES_BY_SLUG["facility"]), (0, 1))

    def test_silo_is_directly_below_dam(self) -> None:
        # mission 6 -> index 5 -> row 1, col 0 (confirmed empirically: a
        # single back-pulse from Dam's cell landed exactly on Silo).
        self.assertEqual(gir.mission_grid_position(gir.STAGES_BY_SLUG["silo"]), (1, 0))

    def test_runway_is_two_columns_right_of_dam(self) -> None:
        # confirmed empirically: an 80-frame right-hold (2x the calibrated
        # 40-frames-per-cell) landed exactly on Runway.
        self.assertEqual(gir.mission_grid_position(gir.STAGES_BY_SLUG["runway"]), (0, 2))

    def test_egypt_is_bottom_right(self) -> None:
        # mission 20 -> index 19 -> row 3, col 4 (last cell of a 5x4 grid).
        self.assertEqual(gir.mission_grid_position(gir.STAGES_BY_SLUG["egypt"]), (3, 4))

    def test_down_press_count_matches_grid_column(self) -> None:
        self.assertEqual(gir.down_press_count(gir.STAGES_BY_SLUG["dam"]), 0)
        self.assertEqual(gir.down_press_count(gir.STAGES_BY_SLUG["facility"]), 1)
        self.assertEqual(gir.down_press_count(gir.STAGES_BY_SLUG["egypt"]), 4)


class BuildRouteTest(unittest.TestCase):
    def test_camera_path_route_is_schema_valid(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["facility"])
        ror.validate_route(route)  # raises SystemExit on failure

    def test_swirl_bond_anim_route_is_schema_valid(self) -> None:
        route = gir.build_swirl_bond_anim_route(gir.STAGES_BY_SLUG["facility"])
        ror.validate_route(route)

    def test_all_20_stages_produce_schema_valid_routes(self) -> None:
        for stage in gir.STAGES:
            with self.subTest(slug=stage.slug):
                ror.validate_route(gir.build_camera_path_route(stage))
                ror.validate_route(gir.build_swirl_bond_anim_route(stage))

    def test_level_field_matches_stage_id(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["silo"])
        self.assertEqual(route["level"], "20")

    def test_camera_index_defaults_to_zero_pin(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["statue"])
        self.assertEqual(route["native_env"]["GE007_INTRO_CAMERA_INDEX"], "0")

    def test_camera_pin_override_is_applied(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["statue"], camera_pin=2)
        self.assertEqual(route["native_env"]["GE007_INTRO_CAMERA_INDEX"], "2")

    def test_camera_pin_is_shared_between_camera_path_and_swirl_routes(self) -> None:
        stage = gir.STAGES_BY_SLUG["train"]
        cam_route = gir.build_camera_path_route(stage, camera_pin=3)
        anim_route = gir.build_swirl_bond_anim_route(stage, camera_pin=3)
        self.assertEqual(
            cam_route["native_env"]["GE007_INTRO_CAMERA_INDEX"],
            anim_route["native_env"]["GE007_INTRO_CAMERA_INDEX"],
        )

    def _stick_events(self, route: dict, axis: str) -> list[dict]:
        return [e for e in route["stock_events"] if e.get(axis)]

    def test_right_hold_duration_matches_calibrated_table(self) -> None:
        # Grid-cursor speed is NOT linear in hold duration (measured
        # directly, repeatedly, against the real stock ROM): a single
        # continuous 160-frame hold (col=4, Statue) undershot by exactly
        # one cell (landed on Bunker 2 instead of Statue); discrete pulses
        # of the same total duration undershot identically. Only the
        # measured per-N table in _GRID_STEP_DURATIONS reproduces exact
        # landings, so the generator must use it verbatim, not a formula.
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["cradle"])  # (row=3, col=2)
        right_events = self._stick_events(route, "right")
        self.assertEqual(len(right_events), 1)
        self.assertEqual(right_events[0]["len"], gir._GRID_STEP_DURATIONS[2])
        self.assertNotIn("repeat", right_events[0])

    def test_back_hold_duration_matches_calibrated_table(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["cradle"])  # (row=3, col=2)
        back_events = self._stick_events(route, "back")
        self.assertEqual(len(back_events), 1)
        self.assertEqual(back_events[0]["len"], gir._GRID_STEP_DURATIONS[3])
        self.assertNotIn("repeat", back_events[0])

    def test_statue_reaches_column_4_via_the_calibrated_edge_value(self) -> None:
        # Statue is (row=1, col=4) -- the grid's last column, confirmed
        # reachable (hard-clamped at the screen edge) with a generous hold;
        # this is the one axis position where "at least far enough" is
        # actually safe, since there's no cell beyond it to overshoot into.
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["statue"])
        right_events = self._stick_events(route, "right")
        self.assertEqual(right_events[0]["len"], gir._GRID_STEP_DURATIONS[4])

    def test_dam_route_has_no_grid_movement_events(self) -> None:
        # Dam is CANONICAL_SLUGS (never generated), but build_camera_path_route()
        # is still a pure function of the Stage table -- Dam's own (0, 0)
        # position should legitimately need zero grid movement if ever called.
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["dam"])
        self.assertEqual(self._stick_events(route, "right"), [])
        self.assertEqual(self._stick_events(route, "back"), [])

    def test_facility_only_needs_right_movement_no_back(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["facility"])  # (row=0, col=1)
        self.assertEqual(len(self._stick_events(route, "right")), 1)
        self.assertEqual(self._stick_events(route, "back"), [])

    def test_silo_only_needs_back_movement_no_right(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["silo"])  # (row=1, col=0)
        self.assertEqual(self._stick_events(route, "right"), [])
        self.assertEqual(len(self._stick_events(route, "back")), 1)

    def test_every_generated_stage_has_a_bounded_difficulty_forward_correction(self) -> None:
        # Every seeded (non-Dam) route marks Agent/Secret/00 Agent complete,
        # which defaults the game's own difficulty cursor to "007" instead
        # of Agent -- every generated route must walk it back up exactly 3
        # rows (007 -> 00 Agent -> Secret Agent -> Agent), a CONSTANT
        # 60-frame stick-forward hold (confirmed empirically: 20
        # frames/row).
        for stage in gir.STAGES:
            if stage.slug == "dam":
                continue
            route = gir.build_camera_path_route(stage)
            forward_events = self._stick_events(route, "forward")
            with self.subTest(slug=stage.slug):
                self.assertEqual(len(forward_events), 1)
                self.assertEqual(forward_events[0]["len"], 60)

    def test_every_generated_stage_has_a_file_select_left_correction(self) -> None:
        # SELECT FILE defaults its cursor to the second folder with no
        # input at all (confirmed empirically) -- every generated route
        # must tap left once to land back on folder 1, where the active
        # save lives.
        for stage in gir.STAGES:
            if stage.slug == "dam":
                continue
            route = gir.build_camera_path_route(stage)
            left_events = [e for e in route["stock_events"] if "cleft" in e.get("buttons", [])]
            with self.subTest(slug=stage.slug):
                self.assertEqual(len(left_events), 1)

    def test_every_generated_stage_ends_with_a_start_launch_mash(self) -> None:
        # The final "M BRIEFING" page is confirmed with START, not A
        # (confirmed empirically: an A-only mash left the game stuck on the
        # frontend for the whole capture window).
        for stage in gir.STAGES:
            if stage.slug == "dam":
                continue
            route = gir.build_camera_path_route(stage)
            start_events = [e for e in route["stock_events"] if "start" in e.get("buttons", [])]
            with self.subTest(slug=stage.slug):
                self.assertEqual(len(start_events), 2, "boot mash + launch mash")
                launch_event = max(start_events, key=lambda e: e["start"])
                self.assertEqual(launch_event["repeat"]["count"], 5)

    def _event_end(self, event: dict) -> int:
        repeat = event.get("repeat", {})
        count = repeat.get("count")
        every = repeat.get("every", 0)
        start = event["start"]
        if count is not None:
            return start + (count - 1) * every + event.get("len", 1)
        return repeat.get("until", start) + event.get("len", 1)

    def test_navigation_events_are_strictly_sequenced_before_the_launch_mash(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["egypt"])  # (row=3, col=4) -- longest nav
        events = route["stock_events"]

        cleft_event = next(e for e in events if "cleft" in e.get("buttons", []))
        a_events = [e for e in events if "a" in e.get("buttons", [])]
        self.assertEqual(len(a_events), 3, "expected file-select confirm + grid confirm + difficulty confirm mash")
        file_confirm_event, grid_confirm_event, difficulty_confirm_event = sorted(a_events, key=lambda e: e["start"])

        right_event = next(e for e in events if e.get("right"))
        back_event = next(e for e in events if e.get("back"))
        forward_event = next(e for e in events if e.get("forward"))
        start_events = [e for e in events if "start" in e.get("buttons", [])]
        launch_event = max(start_events, key=lambda e: e["start"])

        self.assertLess(self._event_end(cleft_event), file_confirm_event["start"])
        self.assertLess(self._event_end(file_confirm_event), right_event["start"])
        self.assertLess(self._event_end(right_event), back_event["start"])
        self.assertLess(self._event_end(back_event), grid_confirm_event["start"])
        self.assertLess(self._event_end(grid_confirm_event), forward_event["start"])
        self.assertLess(self._event_end(forward_event), difficulty_confirm_event["start"])
        self.assertLess(self._event_end(difficulty_confirm_event), launch_event["start"])

    def test_no_stock_event_injects_start_during_gameplay_phase(self) -> None:
        # rom_oracle_route.validate_route already checks this, but assert it
        # explicitly here too since it's the single most route-breaking
        # mistake a generator table typo could make.
        for stage in gir.STAGES:
            route = gir.build_camera_path_route(stage)
            for event in route["stock_events"]:
                phase = str(event.get("phase", "gameplay"))
                if phase in ("gameplay", "global", "stage_global"):
                    self.assertNotIn("start", event.get("buttons", []))

    def test_swirl_route_has_bond_anim_comparison_enabled(self) -> None:
        route = gir.build_swirl_bond_anim_route(gir.STAGES_BY_SLUG["frigate"])
        self.assertTrue(route["compare_bond_anim"])
        self.assertEqual(route["compare_camera_modes"], "swirl")

    def test_non_dam_routes_start_with_empty_waivers(self) -> None:
        for stage in gir.STAGES:
            cam_route = gir.build_camera_path_route(stage)
            anim_route = gir.build_swirl_bond_anim_route(stage)
            self.assertEqual(cam_route.get("compare_waivers", {}), {})
            self.assertEqual(anim_route.get("compare_waivers", {}), {})

    def test_compare_expect_mode_durations_absent_until_measured(self) -> None:
        route = gir.build_camera_path_route(gir.STAGES_BY_SLUG["statue"])
        self.assertNotIn("compare_expect_mode_durations", route)


class WriteRoutesTest(unittest.TestCase):
    def test_writes_38_files_and_skips_dam(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            written, skipped = gir.write_routes(out_dir)
            self.assertEqual(len(written), 38)  # 19 non-dam stages x 2 routes
            self.assertEqual(skipped, ["dam"])
            self.assertFalse((out_dir / "dam_intro_camera_path.json").exists())
            self.assertFalse((out_dir / "dam_intro_swirl_bond_anim.json").exists())
            self.assertTrue((out_dir / "facility_intro_camera_path.json").exists())
            self.assertTrue((out_dir / "facility_intro_swirl_bond_anim.json").exists())

    def test_written_files_are_schema_valid_on_disk(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            gir.write_routes(out_dir)
            for path in out_dir.glob("*.json"):
                route = ror.load_route(str(path))
                ror.validate_route(route)

    def test_camera_pins_argument_overrides_specific_stages(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            gir.write_routes(out_dir, camera_pins={"facility": 2})
            route = ror.load_route(str(out_dir / "facility_intro_camera_path.json"))
            self.assertEqual(route["native_env"]["GE007_INTRO_CAMERA_INDEX"], "2")
            other = ror.load_route(str(out_dir / "silo_intro_camera_path.json"))
            self.assertEqual(other["native_env"]["GE007_INTRO_CAMERA_INDEX"], "0")

    def test_rerunning_write_routes_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            gir.write_routes(out_dir)
            first = (out_dir / "facility_intro_camera_path.json").read_text(encoding="utf-8")
            gir.write_routes(out_dir)
            second = (out_dir / "facility_intro_camera_path.json").read_text(encoding="utf-8")
            self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
