#!/usr/bin/env python3
"""Compile ROM-oracle route specs for native and emulator runners."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any


SCHEMA = "mgb64.rom_oracle.route.v1"
ROUTE_DIR = Path(__file__).resolve().parent / "rom_oracle_routes"

BUTTON_MASKS = {
    "a": 0x8000,
    "b": 0x4000,
    "fire": 0x2000,
    "z": 0x2000,
    "start": 0x1000,
    "dpad_up": 0x0800,
    "dpad_down": 0x0400,
    "dpad_left": 0x0200,
    "dpad_right": 0x0100,
    "l": 0x0020,
    "aim": 0x0010,
    "r": 0x0010,
    "cup": 0x0008,
    "c_up": 0x0008,
    "cdown": 0x0004,
    "c_down": 0x0004,
    "cleft": 0x0002,
    "c_left": 0x0002,
    "cright": 0x0001,
    "c_right": 0x0001,
}

BUTTON_NATIVE_ENV = {
    "a": "GE007_AUTO_A",
    "b": "GE007_AUTO_B",
    "fire": "GE007_AUTO_FIRE",
    "z": "GE007_AUTO_FIRE",
    "start": "GE007_AUTO_START",
    "l": "GE007_AUTO_L",
    "aim": "GE007_AUTO_AIM",
    "r": "GE007_AUTO_R",
    "cup": "GE007_AUTO_CUP",
    "c_up": "GE007_AUTO_CUP",
    "cdown": "GE007_AUTO_CDOWN",
    "c_down": "GE007_AUTO_CDOWN",
    "cleft": "GE007_AUTO_CLEFT",
    "c_left": "GE007_AUTO_CLEFT",
    "cright": "GE007_AUTO_CRIGHT",
    "c_right": "GE007_AUTO_CRIGHT",
    "dpad_up": "GE007_AUTO_DPAD_UP",
    "dpad_down": "GE007_AUTO_DPAD_DOWN",
    "dpad_left": "GE007_AUTO_DPAD_LEFT",
    "dpad_right": "GE007_AUTO_DPAD_RIGHT",
    "crouch": "GE007_AUTO_CROUCH",
}


def route_path(name_or_path: str) -> Path:
    candidate = Path(name_or_path)
    if candidate.exists():
        return candidate.resolve()
    if candidate.suffix != ".json":
        candidate = ROUTE_DIR / f"{name_or_path}.json"
    else:
        candidate = ROUTE_DIR / candidate.name
    if candidate.exists():
        return candidate.resolve()
    raise SystemExit(f"FAIL: route not found: {name_or_path}")


def load_route(name_or_path: str) -> dict[str, Any]:
    path = route_path(name_or_path)
    with path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    if route.get("schema") != SCHEMA:
        raise SystemExit(f"FAIL: unsupported route schema in {path}: {route.get('schema')!r}")
    if "level" not in route:
        raise SystemExit(f"FAIL: route missing level: {path}")
    for key in ("events", "native_events", "stock_events"):
        if key in route and not isinstance(route.get(key), list):
            raise SystemExit(f"FAIL: route {key} must be a list: {path}")
    if not isinstance(route.get("events"), list) and not isinstance(route.get("native_events"), list):
        raise SystemExit(f"FAIL: route must define events or native_events: {path}")
    route["_path"] = str(path)
    return route


def route_field(route: dict[str, Any], field: str) -> Any:
    if field == "native_frames":
        return route.get("native_frames", route.get("frames"))
    if field == "stock_frames":
        return route.get("stock_frames", route.get("frames"))
    if field == "stock_screenshot_frame":
        return route.get("stock_screenshot_frame", route_field(route, "stock_frames"))
    if field == "native_screenshot_game_timer":
        return route.get("native_screenshot_game_timer", "")
    if field == "stock_screenshot_game_timer":
        return route.get("stock_screenshot_game_timer", "")
    if field == "native_level":
        return route.get("native_level", route.get("level"))
    if field == "stock_level":
        return route.get("stock_level", route.get("level"))
    if field == "compare_align":
        return route.get("compare_align", "global")
    if field == "compare_kind":
        return route.get("compare_kind", "movement")
    if field == "compare_profile":
        return route.get("compare_profile", "full")
    if field == "compare_require_active":
        return route.get("compare_require_active", False)
    if field == "compare_require_hash_match":
        return route.get("compare_require_hash_match", False)
    if field == "compare_bond_anim_onset_tolerance":
        return route.get("compare_bond_anim_onset_tolerance", "")
    if field == "compare_bond_idle_onset_tolerance":
        return route.get("compare_bond_idle_onset_tolerance", "")
    if field == "compare_first_active_tolerance":
        return route.get("compare_first_active_tolerance", "")
    if field == "compare_max_active_tolerance":
        return route.get("compare_max_active_tolerance", "")
    if field == "compare_first_position_tolerance":
        return route.get("compare_first_position_tolerance", "")
    if field == "compare_first_sample_tolerance":
        return route.get("compare_first_sample_tolerance", "")
    if field == "compare_require_prop_destroyed":
        return route.get("compare_require_prop_destroyed", False)
    if field == "compare_require_impact_active":
        return route.get("compare_require_impact_active", False)
    if field == "compare_require_impact_match":
        return route.get("compare_require_impact_match", False)
    if field == "compare_impact_position_tolerance":
        return route.get("compare_impact_position_tolerance", "")
    if field == "compare_impact_position_points":
        value = route.get("compare_impact_position_points", "")
        if isinstance(value, list):
            return ",".join(str(item) for item in value)
        return value
    if field == "compare_prop_position_tolerance":
        return route.get("compare_prop_position_tolerance", "")
    if field == "compare_max_buffer_len":
        return route.get("compare_max_buffer_len", "")
    if field == "visual_logical_size":
        return route.get("visual_logical_size", "")
    if field == "visual_logical_viewport":
        return route.get("visual_logical_viewport", "")
    if field == "visual_baseline_logical_frame":
        return route.get("visual_baseline_logical_frame", "active")
    if field == "visual_test_logical_frame":
        return route.get("visual_test_logical_frame", "full")
    if field == "compare_actor_chrnums":
        return route.get("compare_actor_chrnums", [])
    if field == "compare_actor_fields":
        return route.get("compare_actor_fields", "")
    if field == "compare_actor_frame":
        return route.get("compare_actor_frame", "first-active")
    if field == "compare_actor_position_tolerance":
        return route.get("compare_actor_position_tolerance", "")
    if field == "compare_require_health_match":
        return route.get("compare_require_health_match", False)
    if field == "compare_health_tolerance":
        return route.get("compare_health_tolerance", "")
    if field == "compare_damage_show_tolerance":
        return route.get("compare_damage_show_tolerance", "")
    if field == "compare_max_aligned":
        return route.get("compare_max_aligned", "")
    if field == "compare_min_aligned":
        return route.get("compare_min_aligned", "")
    if field == "compare_camera_modes":
        return route.get("compare_camera_modes", "")
    if field == "compare_start_active_frame":
        return route.get("compare_start_active_frame", "")
    if field == "compare_start_intro_timer":
        return route.get("compare_start_intro_timer", "")
    if field == "compare_end_intro_timer":
        return route.get("compare_end_intro_timer", "")
    if field == "compare_sample_step":
        return route.get("compare_sample_step", "")
    if field == "compare_vector_tolerance":
        return route.get("compare_vector_tolerance", "")
    if field == "compare_direction_tolerance":
        return route.get("compare_direction_tolerance", "")
    if field == "compare_scalar_tolerance":
        return route.get("compare_scalar_tolerance", "")
    if field == "compare_anim_tolerance":
        return route.get("compare_anim_tolerance", "")
    if field == "compare_state":
        return route.get("compare_state", False)
    if field == "compare_selected_camera":
        return route.get("compare_selected_camera", False)
    if field == "compare_intro_setup":
        return route.get("compare_intro_setup", False)
    if field == "compare_bond_anim":
        return route.get("compare_bond_anim", False)
    if field == "compare_exclude_fields":
        return route.get("compare_exclude_fields", "")
    if field == "compare_require_frozen":
        return route.get("compare_require_frozen", False)
    if field == "compare_expect_mode_durations":
        return route.get("compare_expect_mode_durations", "")
    if field == "compare_waivers":
        return json.dumps(route.get("compare_waivers", {}))
    if field == "compare_gameplay_windows":
        return route.get("compare_gameplay_windows", [])
    if field == "compare_normalize_position":
        return route.get("compare_normalize_position", False)
    if field == "native_speedframes":
        return route.get("native_speedframes", "")
    if field == "native_render_audit":
        return route.get("native_render_audit", False)
    if field == "native_menu_boot":
        return route.get("native_menu_boot", False)
    if field == "gate":
        return route.get("gate", False)
    if field == "tape":
        return route.get("tape", "")
    if field == "stock_speedframes":
        return route.get("stock_speedframes", route.get("native_speedframes", ""))
    if field == "stock_gameplay_start_global":
        return route.get("stock_gameplay_start_global", "")
    if field == "stock_menu_close_on_player":
        return route.get("stock_menu_close_on_player", True)
    if field == "native_min_moving_records":
        return route.get("native_min_moving_records", "")
    if field == "stock_min_moving_records":
        return route.get("stock_min_moving_records", "")
    if field == "stock_min_gameplay_input_records":
        return route.get("stock_min_gameplay_input_records", "")
    if field == "stock_max_suppressed_menu_records":
        return route.get("stock_max_suppressed_menu_records", "")
    if field == "stock_min_menu_to_gameplay_gap":
        return route.get("stock_min_menu_to_gameplay_gap", "")
    if field == "stock_min_force_player_applies":
        return route.get("stock_min_force_player_applies", "")
    if field == "stock_min_force_player_stan_applies":
        return route.get("stock_min_force_player_stan_applies", "")
    if field == "native_intro_audit":
        return route.get("native_intro_audit", False)
    if field == "native_intro_camera_modes":
        return route.get("native_intro_camera_modes", "")
    if field == "native_intro_require_frozen":
        return route.get("native_intro_require_frozen", "")
    if field == "native_intro_require_player":
        return route.get("native_intro_require_player", True)
    if field == "native_intro_require_bond_present":
        return route.get("native_intro_require_bond_present", False)
    if field == "native_intro_require_bond_onscreen":
        return route.get("native_intro_require_bond_onscreen", False)
    if field == "native_intro_require_bond_model_mtx":
        return route.get("native_intro_require_bond_model_mtx", False)
    if field == "native_intro_require_bond_rendered":
        return route.get("native_intro_require_bond_rendered", False)
    if field == "native_intro_require_bond_anim":
        return route.get("native_intro_require_bond_anim", False)
    if field == "native_intro_require_bond_anim_hash":
        return route.get("native_intro_require_bond_anim_hash", False)
    if field == "native_intro_require_h17_swirl_facing":
        return route.get("native_intro_require_h17_swirl_facing", False)
    if field == "native_intro_require_bond_right_item":
        return route.get("native_intro_require_bond_right_item", "")
    if field == "native_intro_min_active_records":
        return route.get("native_intro_min_active_records", "")
    if field == "native_intro_min_present_frames":
        return route.get("native_intro_min_present_frames", "")
    if field == "native_intro_min_onscreen_frames":
        return route.get("native_intro_min_onscreen_frames", "")
    if field == "native_intro_min_model_mtx_frames":
        return route.get("native_intro_min_model_mtx_frames", "")
    if field == "native_intro_min_rendered_frames":
        return route.get("native_intro_min_rendered_frames", "")
    if field == "native_intro_min_render_count":
        return route.get("native_intro_min_render_count", "")
    if field == "native_intro_min_anim_frames":
        return route.get("native_intro_min_anim_frames", "")
    if field == "native_intro_min_anim_hash_frames":
        return route.get("native_intro_min_anim_hash_frames", "")
    if field == "native_intro_min_anim_advance":
        return route.get("native_intro_min_anim_advance", "")
    if field == "native_intro_min_right_item_frames":
        return route.get("native_intro_min_right_item_frames", "")
    if field == "native_intro_max_first_present_frame":
        return route.get("native_intro_max_first_present_frame", "")
    if field == "native_intro_max_first_render_frame":
        return route.get("native_intro_max_first_render_frame", "")
    if field == "stock_require_first_gameplay_global":
        return route.get("stock_require_first_gameplay_global", "")
    if field == "stock_require_native_selected_camera":
        return route.get("stock_require_native_selected_camera", False)
    if field not in route:
        raise SystemExit(f"FAIL: route has no field: {field}")
    return route[field]


def events_for(route: dict[str, Any], provider: str) -> list[dict[str, Any]]:
    if provider == "native":
        events = route.get("native_events", route.get("events", []))
    elif provider == "stock":
        events = route.get("stock_events", route.get("events", []))
    else:
        raise AssertionError(provider)
    return expand_events(events)


def expand_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    expanded: list[dict[str, Any]] = []
    for event in events:
        if not isinstance(event, dict):
            raise SystemExit(f"FAIL: route event must be an object: {event!r}")
        repeat = event.get("repeat")
        if repeat is None:
            expanded.append(copy.deepcopy(event))
            continue
        if not isinstance(repeat, dict):
            raise SystemExit(f"FAIL: route event repeat must be an object: {event!r}")
        try:
            every = int(repeat["every"])
        except (KeyError, TypeError, ValueError):
            raise SystemExit(f"FAIL: route repeat has invalid every: {event!r}") from None
        try:
            first_start = int(event["start"])
        except (KeyError, TypeError, ValueError):
            raise SystemExit(f"FAIL: route repeat has invalid start: {event!r}") from None
        if every < 1:
            raise SystemExit(f"FAIL: route repeat every must be positive: {event!r}")
        if "count" in repeat:
            try:
                count = int(repeat["count"])
            except (TypeError, ValueError):
                raise SystemExit(f"FAIL: route repeat has invalid count: {event!r}") from None
        elif "until" in repeat:
            try:
                until = int(repeat["until"])
            except (TypeError, ValueError):
                raise SystemExit(f"FAIL: route repeat has invalid until: {event!r}") from None
            count = ((until - first_start) // every) + 1
        else:
            raise SystemExit(f"FAIL: route repeat must define count or until: {event!r}")
        if count < 1:
            raise SystemExit(f"FAIL: route repeat count must be positive: {event!r}")
        for index in range(count):
            instance = copy.deepcopy(event)
            instance.pop("repeat", None)
            instance["start"] = first_start + every * index
            expanded.append(instance)
    expanded.sort(key=lambda item: int(item.get("start", 0)))
    return expanded


def clamp_axis(value: int) -> int:
    return max(-80, min(80, value))


def event_window(event: dict[str, Any]) -> tuple[int, int]:
    try:
        start = int(event["start"])
    except (KeyError, TypeError, ValueError):
        raise SystemExit(f"FAIL: route event has invalid start: {event!r}") from None
    duration_value = event.get("len", event.get("duration", 1))
    try:
        duration = int(duration_value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route event has invalid duration: {event!r}") from None
    if start < 1 or duration < 1:
        raise SystemExit(f"FAIL: route event start/duration must be positive: {event!r}")
    return start, duration


def event_axes(event: dict[str, Any]) -> tuple[int, int]:
    stick_x = int(event.get("stick_x", 0))
    stick_y = int(event.get("stick_y", 0))

    if event.get("left"):
        stick_x -= 80
    if event.get("right"):
        stick_x += 80
    if event.get("forward"):
        stick_y += 80
    if event.get("back"):
        stick_y -= 80

    return clamp_axis(stick_x), clamp_axis(stick_y)


def event_buttons(event: dict[str, Any]) -> list[str]:
    buttons = event.get("buttons", [])
    if isinstance(buttons, str):
        buttons = [buttons]
    if not isinstance(buttons, list):
        raise SystemExit(f"FAIL: route event buttons must be a string or list: {event!r}")
    normalized = []
    for button in buttons:
        key = str(button).strip().lower().replace("-", "_")
        if key not in BUTTON_MASKS and key not in BUTTON_NATIVE_ENV:
            raise SystemExit(f"FAIL: unknown route button {button!r} in event {event!r}")
        normalized.append(key)
    return normalized


def emit_native_env(route: dict[str, Any]) -> None:
    env: dict[str, list[str]] = {}
    direct_env = route.get("native_env", {})
    if direct_env in (None, ""):
        direct_env = {}
    if not isinstance(direct_env, dict):
        raise SystemExit("FAIL: route native_env must be an object when set")

    def env_value(value: Any) -> str:
        if isinstance(value, bool):
            return "1" if value else "0"
        if value is None:
            return ""
        return str(value)

    for event in events_for(route, "native"):
        start, duration = event_window(event)
        window = f"{start}:{duration}"
        stick_x, stick_y = event_axes(event)

        if stick_y > 0:
            if stick_y != 80:
                raise SystemExit("FAIL: native GE007_AUTO_FORWARD only supports full-scale +80")
            env.setdefault("GE007_AUTO_FORWARD", []).append(window)
        elif stick_y < 0:
            if stick_y != -80:
                raise SystemExit("FAIL: native GE007_AUTO_BACK only supports full-scale -80")
            env.setdefault("GE007_AUTO_BACK", []).append(window)

        if stick_x > 0:
            if stick_x != 80:
                raise SystemExit("FAIL: native GE007_AUTO_RIGHT only supports full-scale +80")
            env.setdefault("GE007_AUTO_RIGHT", []).append(window)
        elif stick_x < 0:
            if stick_x != -80:
                raise SystemExit("FAIL: native GE007_AUTO_LEFT only supports full-scale -80")
            env.setdefault("GE007_AUTO_LEFT", []).append(window)

        for button in event_buttons(event):
            native_env = BUTTON_NATIVE_ENV.get(button)
            if native_env:
                env.setdefault(native_env, []).append(window)

    for key in sorted(direct_env):
        if not isinstance(key, str) or not key:
            raise SystemExit(f"FAIL: native_env key must be a non-empty string: {key!r}")
        print(f"{key}={env_value(direct_env[key])}")

    for key in sorted(env):
        print(f"{key}={','.join(env[key])}")


def emit_stock_env(route: dict[str, Any]) -> None:
    direct_env = route.get("stock_env", {})
    if direct_env in (None, ""):
        direct_env = {}
    if not isinstance(direct_env, dict):
        raise SystemExit("FAIL: route stock_env must be an object when set")

    def env_value(value: Any) -> str:
        if isinstance(value, bool):
            return "1" if value else "0"
        if value is None:
            return ""
        return str(value)

    for key in sorted(direct_env):
        if not isinstance(key, str) or not key:
            raise SystemExit(f"FAIL: stock_env key must be a non-empty string: {key!r}")
        print(f"{key}={env_value(direct_env[key])}")


def emit_native_config(route: dict[str, Any]) -> None:
    config = route.get("native_config", {})
    if config in (None, ""):
        config = {}
    if not isinstance(config, dict):
        raise SystemExit("FAIL: route native_config must be an object when set")

    def config_value(value: Any) -> str:
        if isinstance(value, bool):
            return "1" if value else "0"
        if value is None:
            return ""
        return str(value)

    for key in sorted(config):
        if not isinstance(key, str) or not key:
            raise SystemExit(f"FAIL: native_config key must be a non-empty string: {key!r}")
        print(f"{key}={config_value(config[key])}")


def event_phase(event: dict[str, Any]) -> str:
    phase = str(event.get("phase", "gameplay")).strip().lower().replace("-", "_")
    if phase not in ("gameplay", "menu", "boot", "frontend", "global", "stage_global"):
        raise SystemExit(f"FAIL: route event has unsupported phase {phase!r}: {event!r}")
    if phase in ("boot", "frontend"):
        return "menu"
    if phase == "stage_global":
        return "global"
    return phase


def emit_ares_input(route: dict[str, Any]) -> None:
    print("# mgb64 ares input script v1")
    print("# start len buttons_hex stick_x stick_y phase")
    for event in events_for(route, "stock"):
        start, duration = event_window(event)
        stick_x, stick_y = event_axes(event)
        mask = 0
        for button in event_buttons(event):
            mask |= BUTTON_MASKS.get(button, 0)
        print(f"{start} {duration} 0x{mask:04x} {stick_x} {stick_y} {event_phase(event)}")


def emit_gameplay_windows(route: dict[str, Any]) -> None:
    windows = route.get("compare_gameplay_windows", [])
    if windows in (None, ""):
        return
    if not isinstance(windows, list):
        raise SystemExit("FAIL: compare_gameplay_windows must be a list")
    for window in windows:
        if not isinstance(window, dict):
            raise SystemExit(f"FAIL: compare_gameplay_windows entries must be objects: {window!r}")
        start, duration = event_window(window)
        print(f"{start}:{duration}")


def visual_region_arg(region: dict[str, Any]) -> str:
    name = region["name"]
    x, y, width, height = region["roi"]
    return f"{name}:{x},{y},{width},{height}"


def emit_visual_regions(route: dict[str, Any]) -> None:
    regions = route.get("visual_regions", [])
    if regions in (None, ""):
        return
    if not isinstance(regions, list):
        raise SystemExit("FAIL: visual_regions must be a list")
    for region in regions:
        if not isinstance(region, dict):
            raise SystemExit(f"FAIL: visual_regions entries must be objects: {region!r}")
        print(visual_region_arg(region))


def emit_visual_exclude_regions(route: dict[str, Any]) -> None:
    exclude_names = route.get("visual_mask_exclude_regions", [])
    if exclude_names in (None, ""):
        return
    if not isinstance(exclude_names, list):
        raise SystemExit("FAIL: visual_mask_exclude_regions must be a list")

    regions = route.get("visual_regions", [])
    if regions in (None, ""):
        regions = []
    if not isinstance(regions, list):
        raise SystemExit("FAIL: visual_regions must be a list")

    by_name = {
        region.get("name"): region
        for region in regions
        if isinstance(region, dict) and isinstance(region.get("name"), str)
    }
    for name in exclude_names:
        if not isinstance(name, str) or not name:
            raise SystemExit("FAIL: visual_mask_exclude_regions entries must be non-empty strings")
        region = by_name.get(name)
        if region is None:
            raise SystemExit(f"FAIL: visual_mask_exclude_regions references unknown region: {name}")
        print(visual_region_arg(region))


def route_int_list(route: dict[str, Any], field: str, count: int, errors: list[str]):
    value = route.get(field, "")
    if value in ("", None):
        return None
    if (
        not isinstance(value, list) or
        len(value) != count or
        any(not isinstance(item, int) for item in value)
    ):
        errors.append(f"route {field} must be a list of {count} integers")
        return None
    return value


def visual_logical_arg(value: list[int]) -> str:
    return ",".join(str(item) for item in value)


def emit_visual_logical_args(route: dict[str, Any]) -> None:
    errors: list[str] = []
    logical_size = route_int_list(route, "visual_logical_size", 2, errors)
    logical_viewport = route_int_list(route, "visual_logical_viewport", 4, errors)
    if errors:
        raise SystemExit("FAIL: " + "; ".join(errors))
    if logical_size is None or logical_viewport is None:
        return
    baseline_frame = str(route.get("visual_baseline_logical_frame", "active"))
    test_frame = str(route.get("visual_test_logical_frame", "full"))
    print("--logical-size")
    print(visual_logical_arg(logical_size))
    print("--logical-viewport")
    print(visual_logical_arg(logical_viewport))
    print("--baseline-logical-frame")
    print(baseline_frame)
    print("--test-logical-frame")
    print(test_frame)


def route_int_items(route: dict[str, Any], field: str, errors: list[str]) -> list[int]:
    value = route.get(field, [])
    if value in ("", None):
        return []
    if not isinstance(value, list):
        errors.append(f"route {field} must be a list of non-negative integers")
        return []

    items: list[int] = []
    for index, item in enumerate(value):
        if type(item) is not int or item < 0:
            errors.append(f"route {field}[{index}] must be a non-negative integer")
            continue
        items.append(item)
    return items


def route_actor_fields(route: dict[str, Any], errors: list[str]) -> list[str]:
    value = route.get("compare_actor_fields", "")
    if value in ("", None):
        return []
    if isinstance(value, str):
        fields = [field.strip() for field in value.split(",") if field.strip()]
    elif isinstance(value, list):
        fields = []
        for index, item in enumerate(value):
            if not isinstance(item, str) or not item.strip():
                errors.append(f"route compare_actor_fields[{index}] must be a non-empty string")
            else:
                fields.append(item.strip())
    else:
        errors.append("route compare_actor_fields must be a string or list of strings")
        return []
    if not fields:
        errors.append("route compare_actor_fields must name at least one field when set")
    return fields


def emit_actor_compare_args(route: dict[str, Any]) -> None:
    errors: list[str] = []
    chrnums = route_int_items(route, "compare_actor_chrnums", errors)
    fields = route_actor_fields(route, errors)
    frame = str(route.get("compare_actor_frame", "first-active"))
    position_tolerance = route.get("compare_actor_position_tolerance", "")
    if frame not in ("first-active", "last-active", "screenshot"):
        errors.append("route compare_actor_frame must be first-active, last-active, or screenshot")
    if position_tolerance not in ("", None):
        try:
            parsed = float(position_tolerance)
        except (TypeError, ValueError):
            errors.append("route compare_actor_position_tolerance must be a non-negative number")
        else:
            if parsed < 0.0:
                errors.append("route compare_actor_position_tolerance must be a non-negative number")
    if errors:
        raise SystemExit("FAIL: " + "; ".join(errors))
    if not chrnums:
        return

    for chrnum in chrnums:
        print("--require-actor-match")
        print(chrnum)
    if fields:
        print("--actor-fields")
        print(",".join(fields))
    print("--actor-frame")
    print(frame)
    if position_tolerance not in ("", None):
        print("--actor-position-tolerance")
        print(position_tolerance)


def route_bool(route: dict[str, Any], field: str, default: bool) -> bool:
    value = route.get(field, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        key = value.strip().lower()
        if key in ("1", "true", "yes", "on"):
            return True
        if key in ("0", "false", "no", "off"):
            return False
    raise SystemExit(f"FAIL: route {field} must be boolean when set")


def route_positive_int(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a positive integer when set") from None
    if parsed < 1:
        raise SystemExit(f"FAIL: route {field} must be a positive integer when set")


def route_required_positive_int_field(route: dict[str, Any], field: str) -> int:
    value = route_field(route, field)
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a positive integer") from None
    if parsed < 1:
        raise SystemExit(f"FAIL: route {field} must be a positive integer")
    return parsed


def route_nonnegative_int(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a non-negative integer when set") from None
    if parsed < 0:
        raise SystemExit(f"FAIL: route {field} must be a non-negative integer when set")


def route_requires_present(route: dict[str, Any], field: str, errors: list[str]) -> bool:
    value = route.get(field, "")
    if value in ("", None):
        errors.append(f"route must set {field}")
        return False
    return True


def route_positive_float(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a positive number when set") from None
    if parsed <= 0.0:
        raise SystemExit(f"FAIL: route {field} must be a positive number when set")


def route_nonnegative_float(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a non-negative number when set") from None
    if parsed < 0.0:
        raise SystemExit(f"FAIL: route {field} must be a non-negative number when set")


def route_impact_position_points(route: dict[str, Any]) -> None:
    value = route.get("compare_impact_position_points", "")
    if value in ("", None):
        return
    if isinstance(value, str):
        points = [point.strip() for point in value.split(",") if point.strip()]
    elif isinstance(value, list):
        points = value
    else:
        raise SystemExit("FAIL: route compare_impact_position_points must be a string or list")

    if not points:
        raise SystemExit("FAIL: route compare_impact_position_points must name at least one point")
    for point in points:
        if point not in ("center", "v0", "v1", "v2", "v3"):
            raise SystemExit(
                "FAIL: route compare_impact_position_points entries must be one of "
                "center, v0, v1, v2, v3"
            )


def validate_route(route: dict[str, Any]) -> None:
    compare_kind = str(route_field(route, "compare_kind"))
    compare_align = str(route_field(route, "compare_align"))
    compare_profile = str(route_field(route, "compare_profile"))
    stock_events = events_for(route, "stock")
    errors: list[str] = []

    if compare_kind not in ("movement", "intro", "visual", "glass"):
        errors.append(f"route compare_kind must be movement, intro, visual, or glass: {compare_kind}")
    elif compare_kind == "movement":
        if compare_align not in ("global", "frame", "index", "move", "move-global", "gameplay-frame"):
            errors.append(f"movement route compare_align is unsupported: {compare_align}")
        if compare_profile not in ("full", "dynamics", "scalar-speed", "timing"):
            errors.append(f"movement route compare_profile is unsupported: {compare_profile}")
    elif compare_kind == "intro":
        if compare_align not in ("active-index", "global", "frame", "intro-timer", "per-mode"):
            errors.append(f"intro route compare_align is unsupported: {compare_align}")
        if compare_profile not in ("path", "scalar", "state", "full"):
            errors.append(f"intro route compare_profile is unsupported: {compare_profile}")
        # Phase-3 onset event alignment (DAM_PARITY_DEEP_DIVE 2026-07-17 §3.3):
        # positive timer-unit tolerance, only meaningful with compare_bond_anim.
        route_positive_float(route, "compare_bond_anim_onset_tolerance")
        if route.get("compare_bond_anim_onset_tolerance") is not None and not route.get("compare_bond_anim"):
            errors.append("compare_bond_anim_onset_tolerance requires compare_bond_anim")
        route_positive_float(route, "compare_bond_idle_onset_tolerance")
        if route.get("compare_bond_idle_onset_tolerance") is not None and not route.get("compare_bond_anim"):
            errors.append("compare_bond_idle_onset_tolerance requires compare_bond_anim")
    elif compare_kind == "visual":
        if compare_align not in ("global", "frame", "index"):
            errors.append(f"visual route compare_align is unsupported: {compare_align}")
        if compare_profile not in ("full", "screenshot", "active-normalized", "logical-viewport"):
            errors.append(f"visual route compare_profile is unsupported: {compare_profile}")
        if compare_profile == "logical-viewport":
            logical_size = route_int_list(route, "visual_logical_size", 2, errors)
            logical_viewport = route_int_list(route, "visual_logical_viewport", 4, errors)
            if logical_size is not None:
                width, height = logical_size
                if width <= 0 or height <= 0:
                    errors.append("route visual_logical_size must contain positive width/height")
            if logical_size is not None and logical_viewport is not None:
                x, y, width, height = logical_viewport
                logical_width, logical_height = logical_size
                if x < 0 or y < 0 or width <= 0 or height <= 0:
                    errors.append("route visual_logical_viewport has invalid bounds")
                elif x + width > logical_width or y + height > logical_height:
                    errors.append("route visual_logical_viewport extends outside visual_logical_size")
            for field in ("visual_baseline_logical_frame", "visual_test_logical_frame"):
                value = str(route.get(field, "active" if field == "visual_baseline_logical_frame" else "full"))
                if value not in ("active", "full"):
                    errors.append(f"route {field} must be active or full")
        route_bool(route, "compare_require_impact_active", False)
        route_bool(route, "compare_require_impact_match", False)
        route_positive_float(route, "compare_impact_position_tolerance")
        route_impact_position_points(route)
    elif compare_kind == "glass":
        if compare_align not in ("global", "frame", "index"):
            errors.append(f"glass route compare_align is unsupported: {compare_align}")
        if compare_profile not in ("full", "state", "shatter-state"):
            errors.append(f"glass route compare_profile is unsupported: {compare_profile}")
        route_bool(route, "compare_require_active", False)
        route_bool(route, "compare_require_hash_match", False)
        route_nonnegative_int(route, "compare_first_active_tolerance")
        route_nonnegative_int(route, "compare_max_active_tolerance")
        route_positive_float(route, "compare_first_position_tolerance")
        route_bool(route, "compare_require_prop_destroyed", False)
        route_bool(route, "compare_require_impact_active", False)
        route_bool(route, "compare_require_impact_match", False)
        route_positive_float(route, "compare_impact_position_tolerance")
        route_impact_position_points(route)
        route_positive_float(route, "compare_prop_position_tolerance")
        route_positive_int(route, "compare_max_buffer_len")

    stock_has_gameplay_events = any(event_phase(event) in ("gameplay", "global") for event in stock_events)
    stock_has_menu_events = any(event_phase(event) == "menu" for event in stock_events)
    if compare_kind in ("movement", "glass") and stock_has_gameplay_events:
        if route_field(route, "stock_gameplay_start_global") in ("", None):
            errors.append(f"{compare_kind} stock gameplay routes must set stock_gameplay_start_global")
        if not route_bool(route, "stock_menu_close_on_player", True):
            errors.append(
                f"{compare_kind} stock gameplay routes must close menu input on target-player entry"
            )
        required_fields = ["stock_min_gameplay_input_records"]
        if compare_kind == "movement":
            required_fields = [
                "native_min_moving_records",
                "stock_min_moving_records",
                *required_fields,
            ]
        for field in required_fields:
            if route_requires_present(route, field, errors):
                route_positive_int(route, field)
        if stock_has_menu_events:
            if route_requires_present(route, "stock_max_suppressed_menu_records", errors):
                route_nonnegative_int(route, "stock_max_suppressed_menu_records")
            if route_requires_present(route, "stock_min_menu_to_gameplay_gap", errors):
                route_nonnegative_int(route, "stock_min_menu_to_gameplay_gap")

    if route_bool(route, "native_intro_audit", False):
        for field in (
            "native_intro_require_player",
            "native_intro_require_bond_present",
            "native_intro_require_bond_onscreen",
            "native_intro_require_bond_model_mtx",
            "native_intro_require_bond_rendered",
            "native_intro_require_bond_anim",
            "native_intro_require_bond_anim_hash",
            "native_intro_require_h17_swirl_facing",
        ):
            route_bool(route, field, bool(route_field(route, field)))
        for field in (
            "native_intro_min_active_records",
            "native_intro_min_present_frames",
            "native_intro_min_onscreen_frames",
            "native_intro_min_model_mtx_frames",
            "native_intro_min_rendered_frames",
            "native_intro_min_render_count",
            "native_intro_min_anim_frames",
            "native_intro_min_anim_hash_frames",
            "native_intro_min_right_item_frames",
            "native_intro_max_first_present_frame",
            "native_intro_max_first_render_frame",
        ):
            route_positive_int(route, field)
        route_nonnegative_int(route, "native_intro_require_bond_right_item")
        route_positive_float(route, "native_intro_min_anim_advance")

    route_bool(route, "compare_selected_camera", False)
    route_bool(route, "compare_intro_setup", False)
    route_bool(route, "compare_bond_anim", False)
    route_bool(route, "native_render_audit", False)
    route_bool(route, "native_menu_boot", False)
    # Phase B (FID-0031, D4): gate:true promotes a route into the hard verify gate
    # set. A gate:true route MUST carry a paired determinism tape
    # (baselines/tapes/<tape>.ge7tape + .expected.json) so the ares-free ratchet
    # (tools/fidelity/gate_routes_smoke.sh) can replay it; the "tape" field names
    # that baseline (defaults to the route name). Additive: routes without "gate"
    # default to False and are unaffected.
    if route_bool(route, "gate", False):
        tape_name = str(route.get("tape", route.get("name", "")))
        if not tape_name:
            errors.append("gate:true route must resolve a tape name (set 'tape' or 'name')")
    tape_field = route.get("tape", "")
    if tape_field not in ("", None) and not isinstance(tape_field, str):
        errors.append("route tape must be a string when set")
    for env_field in ("native_env", "stock_env", "native_config"):
        direct_env = route.get(env_field, {})
        if direct_env in (None, ""):
            continue
        if not isinstance(direct_env, dict):
            errors.append(f"route {env_field} must be an object when set")
            continue
        for key in direct_env:
            if not isinstance(key, str) or not key:
                errors.append(f"route {env_field} keys must be non-empty strings")
    visual_regions = route.get("visual_regions", [])
    if visual_regions in (None, ""):
        visual_regions = []
    seen_regions: set[str] = set()
    if not isinstance(visual_regions, list):
        errors.append("route visual_regions must be a list when set")
    else:
        for index, region in enumerate(visual_regions):
            if not isinstance(region, dict):
                errors.append(f"route visual_regions[{index}] must be an object")
                continue
            name = region.get("name")
            roi = region.get("roi")
            if not isinstance(name, str) or not name:
                errors.append(f"route visual_regions[{index}].name must be a non-empty string")
            elif ":" in name or "," in name:
                errors.append(f"route visual_regions[{index}].name must not contain ':' or ','")
            elif name in seen_regions:
                errors.append(f"route visual_regions name is duplicated: {name}")
            else:
                seen_regions.add(name)
            if (
                not isinstance(roi, list) or
                len(roi) != 4 or
                any(not isinstance(value, int) for value in roi)
            ):
                errors.append(f"route visual_regions[{index}].roi must be [x, y, width, height] integers")
            else:
                x, y, width, height = roi
                if x < 0 or y < 0 or width <= 0 or height <= 0:
                    errors.append(f"route visual_regions[{index}].roi has invalid bounds")

    visual_mask_exclude_regions = route.get("visual_mask_exclude_regions", [])
    if visual_mask_exclude_regions in (None, ""):
        visual_mask_exclude_regions = []
    if not isinstance(visual_mask_exclude_regions, list):
        errors.append("route visual_mask_exclude_regions must be a list when set")
    else:
        seen_excludes: set[str] = set()
        for index, name in enumerate(visual_mask_exclude_regions):
            if not isinstance(name, str) or not name:
                errors.append(f"route visual_mask_exclude_regions[{index}] must be a non-empty string")
            elif name in seen_excludes:
                errors.append(f"route visual_mask_exclude_regions is duplicated: {name}")
            elif name not in seen_regions:
                errors.append(f"route visual_mask_exclude_regions references unknown region: {name}")
            else:
                seen_excludes.add(name)

    route_int_items(route, "compare_actor_chrnums", errors)
    route_actor_fields(route, errors)
    compare_actor_frame = str(route.get("compare_actor_frame", "first-active"))
    if compare_actor_frame not in ("first-active", "last-active", "screenshot"):
        errors.append("route compare_actor_frame must be first-active, last-active, or screenshot")
    actor_position_tolerance = route.get("compare_actor_position_tolerance", "")
    if actor_position_tolerance not in ("", None):
        try:
            parsed_actor_position_tolerance = float(actor_position_tolerance)
        except (TypeError, ValueError):
            errors.append("route compare_actor_position_tolerance must be a non-negative number")
        else:
            if parsed_actor_position_tolerance < 0.0:
                errors.append("route compare_actor_position_tolerance must be a non-negative number")
    route_bool(route, "compare_require_health_match", False)
    route_nonnegative_float(route, "compare_health_tolerance")
    route_nonnegative_int(route, "compare_damage_show_tolerance")

    stock_frames = route_required_positive_int_field(route, "stock_frames")
    stock_screenshot_frame = route_required_positive_int_field(route, "stock_screenshot_frame")
    if stock_screenshot_frame > stock_frames:
        errors.append("route stock_screenshot_frame must be <= stock_frames")
    route_positive_int(route, "native_screenshot_game_timer")
    route_positive_int(route, "stock_screenshot_game_timer")
    route_positive_int(route, "native_level")
    route_positive_int(route, "stock_level")
    route_nonnegative_int(route, "stock_max_suppressed_menu_records")
    route_nonnegative_int(route, "stock_min_menu_to_gameplay_gap")
    route_positive_int(route, "stock_min_force_player_applies")
    route_positive_int(route, "stock_min_force_player_stan_applies")
    route_positive_float(route, "compare_anim_tolerance")
    route_positive_int(route, "compare_min_aligned")
    route_positive_float(route, "compare_start_intro_timer")
    route_positive_float(route, "compare_end_intro_timer")

    # Native events are assumed to script GAMEPLAY input (mid-level, where
    # START opens the pause menu) unless the route is a D30 menu-boot lane:
    # there, native events legitimately drive the pre-level-load FRONTEND
    # (native_menu_boot routes boot with no --level), so a native event's own
    # "phase" tag governs instead of the blanket "gameplay" assumption.
    native_menu_boot = route_bool(route, "native_menu_boot", False)
    for provider in ("native", "stock"):
        for event in events_for(route, provider):
            if provider == "stock" or native_menu_boot:
                phase = event_phase(event)
            else:
                phase = "gameplay"
            buttons = set(event_buttons(event))
            if phase in ("gameplay", "global") and "start" in buttons:
                errors.append(f"{provider} gameplay event injects START: {event!r}")

    if errors:
        print(f"FAIL: invalid route {route.get('name', route.get('_path', '<unknown>'))}")
        for error in errors:
            print(f"  {error}")
        raise SystemExit(2)


def list_routes() -> None:
    for path in sorted(ROUTE_DIR.glob("*.json")):
        try:
            route = load_route(str(path))
        except SystemExit:
            continue
        native_frames = route_field(route, "native_frames")
        stock_frames = route_field(route, "stock_frames")
        native_level = route_field(route, "native_level")
        stock_level = route_field(route, "stock_level")
        print(
            f"{path.stem}\tlevel={route.get('level')}"
            f"\tnative_level={native_level}\tstock_level={stock_level}"
            f"\tnative_frames={native_frames}\tstock_frames={stock_frames}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("resolve", help="print the resolved route path")
    p.add_argument("route")

    p = sub.add_parser("field", help="print one route metadata field")
    p.add_argument("route")
    p.add_argument("field")

    p = sub.add_parser("native-env", help="emit KEY=VALUE lines for native GE007_AUTO_* input")
    p.add_argument("route")

    p = sub.add_parser("native-config", help="emit KEY=VALUE lines for native --config-override")
    p.add_argument("route")

    p = sub.add_parser("stock-env", help="emit KEY=VALUE lines for stock ares oracle env")
    p.add_argument("route")

    p = sub.add_parser("ares-input", help="emit ares controller route script")
    p.add_argument("route")

    p = sub.add_parser("gameplay-windows", help="emit comparator gameplay windows as START:LEN lines")
    p.add_argument("route")

    p = sub.add_parser("visual-regions", help="emit visual comparison regions as NAME:X,Y,W,H lines")
    p.add_argument("route")

    p = sub.add_parser("visual-exclude-regions", help="emit masked visual excluded regions as NAME:X,Y,W,H lines")
    p.add_argument("route")

    p = sub.add_parser("visual-logical-args", help="emit compare_screenshots logical-viewport args")
    p.add_argument("route")

    p = sub.add_parser("actor-compare-args", help="emit compare_glass_trace actor guard args")
    p.add_argument("route")

    p = sub.add_parser("summary", help="print route metadata")
    p.add_argument("route")

    p = sub.add_parser("validate", help="validate route safety contracts")
    p.add_argument("route")

    sub.add_parser("list", help="list built-in route specs")

    args = parser.parse_args()

    if args.command == "list":
        list_routes()
        return 0

    route = load_route(args.route)
    if args.command == "resolve":
        print(route["_path"])
    elif args.command == "field":
        print(route_field(route, args.field))
    elif args.command == "native-env":
        emit_native_env(route)
    elif args.command == "native-config":
        emit_native_config(route)
    elif args.command == "stock-env":
        emit_stock_env(route)
    elif args.command == "ares-input":
        emit_ares_input(route)
    elif args.command == "gameplay-windows":
        emit_gameplay_windows(route)
    elif args.command == "visual-regions":
        emit_visual_regions(route)
    elif args.command == "visual-exclude-regions":
        emit_visual_exclude_regions(route)
    elif args.command == "visual-logical-args":
        emit_visual_logical_args(route)
    elif args.command == "actor-compare-args":
        emit_actor_compare_args(route)
    elif args.command == "summary":
        print(json.dumps({k: v for k, v in route.items() if not k.startswith("_")}, indent=2))
    elif args.command == "validate":
        validate_route(route)
        print(f"PASS: route validated: {route.get('name', route['_path'])}")
    else:
        raise AssertionError(args.command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
