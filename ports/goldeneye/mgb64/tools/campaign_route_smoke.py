#!/usr/bin/env python3
"""Run native campaign route contracts and audit trace evidence."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


SCHEMA = "mgb64.campaign_route.v1"
ROUTE_DIR = Path(__file__).resolve().parent / "campaign_routes"
ORACLE_ROUTE_TOOL = Path(__file__).resolve().parent / "rom_oracle_route.py"
DEFAULT_ROUTES = (
    "dam_spawn_input_traversal",
    "dam_native_multiwaypoint_input_traversal",
    "dam_native_mission_composite_contract",
    "dam_guard_pressure_contract",
    "dam_player_fire_guard_contract",
    "facility_spawn_input_traversal",
    "facility_spawn_obj159_door_traversal_contract",
    "facility_spawn_obj159_obj155_door_chain_contract",
    "facility_door_open_close_contract",
    "surface1_spawn_input_traversal",
    "surface1_native_multiwaypoint_input_traversal",
    "surface1_key_pickup_contract",
    "bunker1_spawn_input_traversal",
    "bunker1_spawn_door_collect_contract",
    "bunker1_spawn_two_door_collect_contract",
    "bunker1_datathief_equipment_contract",
    "frigate_spawn_input_traversal",
    "frigate_native_multiwaypoint_input_traversal",
    "statue_spawn_input_traversal",
    "statue_native_multiwaypoint_input_traversal",
    "statue_spawn_door_traversal_contract",
    "train_spawn_input_traversal",
    "train_native_multiwaypoint_input_traversal",
    "control_spawn_input_traversal",
    "control_spawn_door_traversal_contract",
    "caverns_spawn_input_traversal",
    "caverns_spawn_door_traversal_contract",
    "cradle_spawn_input_traversal",
    "cradle_native_multiwaypoint_input_traversal",
    "cradle_spawn_armour_collect_contract",
    "dam_objective_contract",
    "dam_mission_report_contract",
    "surface2_native_multiwaypoint_input_traversal",
    "surface2_final_exit_contract",
)
DL_KEYS = (
    "dl_fail",
    "vtx_fail",
    "mtx_fail",
    "movemem_fail",
    "texture_fail",
    "settimg_fail",
    "non_dl_skip_pc",
    "non_dl_skip_n64",
    "unregistered_skip",
)
INPUT_AUTOMATION_ENV_KEYS = {
    "GE007_AUTO_A",
    "GE007_AUTO_AIM",
    "GE007_AUTO_B",
    "GE007_AUTO_BACK",
    "GE007_AUTO_CDOWN",
    "GE007_AUTO_CLEFT",
    "GE007_AUTO_CRIGHT",
    "GE007_AUTO_CROUCH",
    "GE007_AUTO_CUP",
    "GE007_AUTO_DPAD_DOWN",
    "GE007_AUTO_DPAD_LEFT",
    "GE007_AUTO_DPAD_RIGHT",
    "GE007_AUTO_DPAD_UP",
    "GE007_AUTO_FIRE",
    "GE007_AUTO_FORWARD",
    "GE007_AUTO_FRONTEND_DOWN",
    "GE007_AUTO_FRONTEND_LEFT",
    "GE007_AUTO_FRONTEND_RIGHT",
    "GE007_AUTO_FRONTEND_UP",
    "GE007_AUTO_L",
    "GE007_AUTO_LEFT",
    "GE007_AUTO_LOOK_DOWN",
    "GE007_AUTO_LOOK_LEFT",
    "GE007_AUTO_LOOK_RIGHT",
    "GE007_AUTO_LOOK_UP",
    "GE007_AUTO_MENU_DOWN",
    "GE007_AUTO_MENU_LEFT",
    "GE007_AUTO_MENU_RIGHT",
    "GE007_AUTO_MENU_UP",
    "GE007_AUTO_R",
    "GE007_AUTO_RELOAD",
    "GE007_AUTO_RIGHT",
    "GE007_AUTO_START",
    "GE007_AUTO_WEAPON_NEXT",
    "GE007_AUTO_WEAPON_PREV",
}
UTILITY_AUTOMATION_ENV_KEYS = {
    "GE007_AUTO_EXIT_FRAME",
}
INPUT_SEGMENT_KEYS = {
    "name",
    "start",
    "duration",
    "inputs",
}
INPUT_SEGMENT_ENV = {
    "a": "GE007_AUTO_A",
    "aim": "GE007_AUTO_AIM",
    "b": "GE007_AUTO_B",
    "back": "GE007_AUTO_BACK",
    "c_down": "GE007_AUTO_CDOWN",
    "c_left": "GE007_AUTO_CLEFT",
    "c_right": "GE007_AUTO_CRIGHT",
    "c_up": "GE007_AUTO_CUP",
    "cdown": "GE007_AUTO_CDOWN",
    "cleft": "GE007_AUTO_CLEFT",
    "cright": "GE007_AUTO_CRIGHT",
    "crouch": "GE007_AUTO_CROUCH",
    "cup": "GE007_AUTO_CUP",
    "dpad_down": "GE007_AUTO_DPAD_DOWN",
    "dpad_left": "GE007_AUTO_DPAD_LEFT",
    "dpad_right": "GE007_AUTO_DPAD_RIGHT",
    "dpad_up": "GE007_AUTO_DPAD_UP",
    "fire": "GE007_AUTO_FIRE",
    "forward": "GE007_AUTO_FORWARD",
    "frontend_down": "GE007_AUTO_FRONTEND_DOWN",
    "frontend_left": "GE007_AUTO_FRONTEND_LEFT",
    "frontend_right": "GE007_AUTO_FRONTEND_RIGHT",
    "frontend_up": "GE007_AUTO_FRONTEND_UP",
    "l": "GE007_AUTO_L",
    "left": "GE007_AUTO_LEFT",
    "look_down": "GE007_AUTO_LOOK_DOWN",
    "look_left": "GE007_AUTO_LOOK_LEFT",
    "look_right": "GE007_AUTO_LOOK_RIGHT",
    "look_up": "GE007_AUTO_LOOK_UP",
    "menu_down": "GE007_AUTO_MENU_DOWN",
    "menu_left": "GE007_AUTO_MENU_LEFT",
    "menu_right": "GE007_AUTO_MENU_RIGHT",
    "menu_up": "GE007_AUTO_MENU_UP",
    "r": "GE007_AUTO_R",
    "reload": "GE007_AUTO_RELOAD",
    "right": "GE007_AUTO_RIGHT",
    "start": "GE007_AUTO_START",
    "weapon_next": "GE007_AUTO_WEAPON_NEXT",
    "weapon_prev": "GE007_AUTO_WEAPON_PREV",
    "z": "GE007_AUTO_FIRE",
}
SCRIPTED_EVENTS_KEYS = {
    "add_items",
    "damage_tags",
    "debug_dump",
    "equip_items",
    "exit_on_title",
    "face_coord",
    "force_player",
    "mission_end",
    "set_chr_ai",
    "set_stage_flags",
    "unset_stage_flags",
    "warp_chrs",
    "warps",
}
SCRIPTED_EVENT_LIST_KEYS = {
    "add_items": {"name", "frame", "item"},
    "damage_tags": {"name", "frame", "tag", "amount"},
    "equip_items": {"name", "frame", "item"},
    "face_coord": {"name", "frame", "end_frame", "x", "y", "z", "yaw_offset", "pitch_offset"},
    "force_player": {"name", "frame", "end_frame", "x", "y", "z", "yaw", "pitch", "camera_height", "pad"},
    "set_chr_ai": {"name", "frame", "chr", "ailist"},
    "set_stage_flags": {"name", "frame", "flags"},
    "unset_stage_flags": {"name", "frame", "flags"},
    "warp_chrs": {"name", "frame", "chr", "distance", "angle"},
    "warps": {"name", "frame", "pad", "right_offset", "forward_offset", "y_offset"},
}
SCRIPTED_EVENT_OBJECT_KEYS = {
    "debug_dump": {"frame"},
    "exit_on_title": {"delay"},
    "mission_end": {"frame", "result"},
}
SCRIPTED_EVENT_ENV_KEYS = {
    "GE007_AUTO_ADD_ITEM",
    "GE007_AUTO_ADD_ITEM_FRAME",
    "GE007_AUTO_DAMAGE_TAG_SCRIPT",
    "GE007_AUTO_DEBUG_DUMP_FRAME",
    "GE007_AUTO_EQUIP_ITEM_SCRIPT",
    "GE007_AUTO_EXIT_ON_TITLE",
    "GE007_AUTO_EXIT_ON_TITLE_DELAY",
    "GE007_AUTO_FACE_COORD_SCRIPT",
    "GE007_AUTO_FORCE_PLAYER_SCRIPT",
    "GE007_AUTO_MISSION_END_FRAME",
    "GE007_AUTO_MISSION_END_RESULT",
    "GE007_AUTO_SET_CHR_AI_SCRIPT",
    "GE007_AUTO_SET_STAGE_FLAGS_SCRIPT",
    "GE007_AUTO_UNSET_STAGE_FLAGS_SCRIPT",
    "GE007_AUTO_WARP_CHR_SCRIPT",
    "GE007_AUTO_WARP_SCRIPT",
}
MISSION_END_RESULTS = {"success", "fail", "failed", "abort", "aborted", "kia", "killed"}
POSITION_MILESTONE_KEYS = {
    "name",
    "frame_at_or_after",
    "min_horizontal_delta",
    "max_horizontal_delta",
    "min_abs_x_delta",
    "min_abs_z_delta",
}
OBJECTIVE_MILESTONE_KEYS = {
    "name",
    "frame_at_or_after",
    "statuses",
}
STATE_MILESTONE_KEYS = {
    "name",
    "frame_at_or_after",
    "checks",
}
SETUP_TARGET_MILESTONE_KEYS = {
    "name",
    "frame_at_or_after",
    "target",
    "max_horizontal_distance",
    "max_distance",
    "max_abs_y_delta",
}
SETUP_TARGET_KEYS = {
    "index",
    "kind",
    "obj",
    "pad",
    "room",
    "tag",
    "target_obj",
    "target_pad",
    "target_type",
    "target_type_name",
    "type",
    "type_name",
}
STATE_CHECK_KEYS = {
    "path",
    "op",
    "value",
}
STATE_CHECK_OPS = {
    "==",
    "!=",
    ">",
    ">=",
    "<",
    "<=",
    "contains",
    "exists",
    "missing",
    "truthy",
    "falsy",
}
LOG_COUNT_ASSERTION_KEYS = {
    "name",
    "pattern",
    "min_count",
    "max_count",
}
SAVE_COMPLETION_KEYS = {
    "folder",
    "level",
    "difficulty",
}
RELOAD_SAVE_COMPLETION_KEYS = SAVE_COMPLETION_KEYS | {
    "frames",
    "auto_start",
    "require_title",
}
MISSING = object()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--rom", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--route", action="append", default=[])
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--list", action="store_true")
    return parser.parse_args()


def resolve_route(name_or_path: str) -> Path:
    candidate = Path(name_or_path)
    if candidate.exists():
        return candidate.resolve()
    if candidate.suffix == ".json":
        candidate = ROUTE_DIR / candidate.name
    else:
        candidate = ROUTE_DIR / f"{name_or_path}.json"
    if candidate.exists():
        return candidate.resolve()
    raise SystemExit(f"FAIL: campaign route not found: {name_or_path}")


def load_route(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    if route.get("schema") != SCHEMA:
        raise SystemExit(f"FAIL: unsupported route schema in {path}: {route.get('schema')!r}")
    if not isinstance(route.get("name"), str) or not route["name"]:
        raise SystemExit(f"FAIL: route missing non-empty name: {path}")
    has_oracle_route = isinstance(route.get("rom_oracle_route"), str) and bool(route["rom_oracle_route"])
    if "level" in route:
        if int_from_any(route.get("level"), -999999) == -999999:
            raise SystemExit(f"FAIL: route {route.get('name', path)} level must be an integer")
    elif not has_oracle_route:
        raise SystemExit(f"FAIL: route {route.get('name', path)} must define level or rom_oracle_route")
    if not isinstance(route.get("env", {}), dict):
        raise SystemExit(f"FAIL: route {route['name']} env must be an object")
    validate_input_segments(route)
    validate_scripted_events(route)
    if not isinstance(route.get("config", {}), dict):
        raise SystemExit(f"FAIL: route {route['name']} config must be an object")
    if not isinstance(route.get("assertions", {}), dict):
        raise SystemExit(f"FAIL: route {route['name']} assertions must be an object")
    if "frames" in route and int_from_any(route.get("frames")) <= 0:
        raise SystemExit(f"FAIL: route {route['name']} frames must be a positive integer")
    validate_route_assertions(route)
    route["_path"] = str(path)
    return route


def normalize_input_name(value: Any) -> str:
    if not isinstance(value, str):
        return ""
    return value.strip().lower().replace("-", "_").replace(" ", "_")


def validate_input_segments(route: dict[str, Any]) -> None:
    route_name = route.get("name", "<unnamed>")
    segments = route.get("input_segments", [])
    if not isinstance(segments, list):
        raise SystemExit(f"FAIL: route {route_name} input_segments must be an array")
    for index, segment in enumerate(segments):
        if not isinstance(segment, dict):
            raise SystemExit(f"FAIL: route {route_name} input_segments[{index}] must be an object")
        unknown_keys = sorted(set(segment) - INPUT_SEGMENT_KEYS)
        if unknown_keys:
            raise SystemExit(
                f"FAIL: route {route_name} input_segments[{index}] has unsupported keys: "
                + ", ".join(unknown_keys)
            )
        if "name" in segment and (not isinstance(segment.get("name"), str) or not segment["name"]):
            raise SystemExit(f"FAIL: route {route_name} input_segments[{index}].name must be non-empty when set")
        if int_from_any(segment.get("start"), -1) < 0:
            raise SystemExit(f"FAIL: route {route_name} input_segments[{index}].start must be >= 0")
        if int_from_any(segment.get("duration"), 0) <= 0:
            raise SystemExit(f"FAIL: route {route_name} input_segments[{index}].duration must be positive")
        inputs = segment.get("inputs")
        if not isinstance(inputs, list) or not inputs:
            raise SystemExit(f"FAIL: route {route_name} input_segments[{index}].inputs must be a non-empty array")
        seen_inputs: set[str] = set()
        seen_env_keys: set[str] = set()
        for input_index, raw_input in enumerate(inputs):
            input_name = normalize_input_name(raw_input)
            if input_name not in INPUT_SEGMENT_ENV:
                raise SystemExit(
                    f"FAIL: route {route_name} input_segments[{index}].inputs[{input_index}] "
                    f"unknown input {raw_input!r}"
                )
            if input_name in seen_inputs:
                raise SystemExit(
                    f"FAIL: route {route_name} input_segments[{index}].inputs[{input_index}] "
                    f"duplicates input {raw_input!r}"
                )
            seen_inputs.add(input_name)
            env_key = INPUT_SEGMENT_ENV[input_name]
            if env_key in seen_env_keys:
                raise SystemExit(
                    f"FAIL: route {route_name} input_segments[{index}].inputs[{input_index}] "
                    f"duplicates native input {env_key}"
                )
            seen_env_keys.add(env_key)


def compile_input_segments(route: dict[str, Any]) -> dict[str, str]:
    compiled: dict[str, list[str]] = {}
    for segment in route.get("input_segments", []):
        start = int_from_any(segment.get("start"))
        duration = int_from_any(segment.get("duration"))
        window = f"{start}:{duration}"
        for raw_input in segment.get("inputs", []):
            env_key = INPUT_SEGMENT_ENV[normalize_input_name(raw_input)]
            compiled.setdefault(env_key, []).append(window)
    return {key: ",".join(windows) for key, windows in sorted(compiled.items())}


def validate_scripted_events(route: dict[str, Any]) -> None:
    route_name = route.get("name", "<unnamed>")
    scripted = route.get("scripted_events", {})
    if not isinstance(scripted, dict):
        raise SystemExit(f"FAIL: route {route_name} scripted_events must be an object")
    unknown_keys = sorted(set(scripted) - SCRIPTED_EVENTS_KEYS)
    if unknown_keys:
        raise SystemExit(
            f"FAIL: route {route_name} scripted_events has unsupported keys: "
            + ", ".join(unknown_keys)
        )

    for key, allowed_keys in SCRIPTED_EVENT_LIST_KEYS.items():
        events = scripted.get(key, [])
        if not isinstance(events, list):
            raise SystemExit(f"FAIL: route {route_name} scripted_events.{key} must be an array")
        for index, event in enumerate(events):
            validate_scripted_event_object(route_name, key, index, event, allowed_keys)
            validate_scripted_event_fields(route_name, key, index, event)

    if len(scripted.get("add_items", [])) > 1:
        raise SystemExit(f"FAIL: route {route_name} scripted_events.add_items supports one event")

    for key, allowed_keys in SCRIPTED_EVENT_OBJECT_KEYS.items():
        if key not in scripted:
            continue
        event = scripted[key]
        if not isinstance(event, dict):
            raise SystemExit(f"FAIL: route {route_name} scripted_events.{key} must be an object")
        unknown_event_keys = sorted(set(event) - allowed_keys)
        if unknown_event_keys:
            raise SystemExit(
                f"FAIL: route {route_name} scripted_events.{key} has unsupported keys: "
                + ", ".join(unknown_event_keys)
            )
        validate_scripted_event_fields(route_name, key, 0, event)


def validate_scripted_event_object(route_name: str,
                                   key: str,
                                   index: int,
                                   event: Any,
                                   allowed_keys: set[str]) -> None:
    if not isinstance(event, dict):
        raise SystemExit(f"FAIL: route {route_name} scripted_events.{key}[{index}] must be an object")
    unknown_event_keys = sorted(set(event) - allowed_keys)
    if unknown_event_keys:
        raise SystemExit(
            f"FAIL: route {route_name} scripted_events.{key}[{index}] has unsupported keys: "
            + ", ".join(unknown_event_keys)
        )
    if "name" in event and (not isinstance(event.get("name"), str) or not event["name"]):
        raise SystemExit(f"FAIL: route {route_name} scripted_events.{key}[{index}].name must be non-empty when set")


def validate_scripted_event_fields(route_name: str, key: str, index: int, event: dict[str, Any]) -> None:
    label = f"scripted_events.{key}" if key in SCRIPTED_EVENT_OBJECT_KEYS else f"scripted_events.{key}[{index}]"
    required: tuple[str, ...]
    numeric_nonnegative: tuple[str, ...]
    numeric_any: tuple[str, ...]

    required_by_key: dict[str, tuple[str, ...]] = {
        "add_items": ("frame", "item"),
        "damage_tags": ("frame", "tag", "amount"),
        "debug_dump": ("frame",),
        "equip_items": ("frame", "item"),
        "exit_on_title": (),
        "face_coord": ("frame", "x", "y", "z"),
        "force_player": ("frame", "x", "y", "z", "yaw", "pitch"),
        "mission_end": ("frame", "result"),
        "set_chr_ai": ("frame", "chr", "ailist"),
        "set_stage_flags": ("frame", "flags"),
        "unset_stage_flags": ("frame", "flags"),
        "warp_chrs": ("frame", "chr"),
        "warps": ("frame", "pad"),
    }
    required = required_by_key[key]
    for field in required:
        if field not in event:
            raise SystemExit(f"FAIL: route {route_name} {label} missing {field}")

    numeric_nonnegative = ("frame", "end_frame", "item", "tag", "amount", "pad", "chr", "distance", "ailist", "camera_height", "delay")
    numeric_any = ("right_offset", "forward_offset", "y_offset", "x", "y", "z", "yaw_offset", "pitch_offset", "yaw", "pitch", "angle")

    for field in numeric_nonnegative:
        if field not in event:
            continue
        value = number_from_value(event.get(field))
        if value is None or value < 0:
            raise SystemExit(f"FAIL: route {route_name} {label}.{field} must be >= 0")
    for field in numeric_any:
        if field not in event:
            continue
        value = number_from_value(event.get(field))
        if value is None:
            raise SystemExit(f"FAIL: route {route_name} {label}.{field} must be a number")
    if "end_frame" in event and int_from_any(event.get("end_frame"), -1) < int_from_any(event.get("frame"), -1):
        raise SystemExit(f"FAIL: route {route_name} {label}.end_frame must be >= frame")
    if key == "mission_end":
        result = event.get("result")
        if not isinstance(result, str) or result.strip().lower() not in MISSION_END_RESULTS:
            raise SystemExit(f"FAIL: route {route_name} {label}.result must be one of {sorted(MISSION_END_RESULTS)}")
    for flag_key in ("set_stage_flags", "unset_stage_flags"):
        if key == flag_key and int_from_any(event.get("flags"), -1) < 0:
            raise SystemExit(f"FAIL: route {route_name} {label}.flags must be >= 0")


def script_int(value: Any) -> str:
    return str(int_from_any(value))


def script_flags(value: Any) -> str:
    if isinstance(value, str):
        return value
    return str(int_from_any(value))


def script_number(value: Any) -> str:
    number = number_from_value(value)
    if number is None:
        return str(value)
    if math.isclose(number, round(number), rel_tol=0.0, abs_tol=0.000001):
        return str(int(round(number)))
    text = f"{number:.6f}".rstrip("0").rstrip(".")
    return text if text else "0"


def script_frame_range(event: dict[str, Any]) -> str:
    start = script_int(event.get("frame"))
    if "end_frame" not in event:
        return start
    return f"{start}-{script_int(event.get('end_frame'))}"


def compile_scripted_events(route: dict[str, Any]) -> dict[str, str]:
    scripted = route.get("scripted_events", {})
    if not isinstance(scripted, dict):
        return {}
    compiled: dict[str, str] = {}

    if scripted.get("warps"):
        values: list[str] = []
        for event in scripted["warps"]:
            value = f"{script_int(event.get('frame'))}:{script_int(event.get('pad'))}"
            has_offset = any(field in event for field in ("right_offset", "forward_offset", "y_offset"))
            if has_offset:
                value += ":" + script_number(event.get("right_offset", 0))
                value += ":" + script_number(event.get("forward_offset", 0))
                if "y_offset" in event:
                    value += ":" + script_number(event.get("y_offset", 0))
            values.append(value)
        compiled["GE007_AUTO_WARP_SCRIPT"] = " ".join(values)

    if scripted.get("warp_chrs"):
        values = []
        for event in scripted["warp_chrs"]:
            value = f"{script_int(event.get('frame'))}:{script_int(event.get('chr'))}"
            if "distance" in event:
                value += ":" + script_number(event.get("distance"))
                if "angle" in event:
                    value += ":" + script_number(event.get("angle"))
            elif "angle" in event:
                value += ":96:" + script_number(event.get("angle"))
            values.append(value)
        compiled["GE007_AUTO_WARP_CHR_SCRIPT"] = " ".join(values)

    if scripted.get("face_coord"):
        values = []
        for event in scripted["face_coord"]:
            value = (
                f"{script_frame_range(event)}:"
                f"{script_number(event.get('x'))}:"
                f"{script_number(event.get('y'))}:"
                f"{script_number(event.get('z'))}"
            )
            if "yaw_offset" in event or "pitch_offset" in event:
                value += ":" + script_number(event.get("yaw_offset", 0))
                if "pitch_offset" in event:
                    value += ":" + script_number(event.get("pitch_offset", 0))
            values.append(value)
        compiled["GE007_AUTO_FACE_COORD_SCRIPT"] = " ".join(values)

    if scripted.get("force_player"):
        values = []
        for event in scripted["force_player"]:
            value = (
                f"{script_frame_range(event)}:"
                f"{script_number(event.get('x'))}:"
                f"{script_number(event.get('y'))}:"
                f"{script_number(event.get('z'))}:"
                f"{script_number(event.get('yaw'))}:"
                f"{script_number(event.get('pitch'))}"
            )
            if "camera_height" in event or "pad" in event:
                value += ":" + script_number(event.get("camera_height", 0))
                if "pad" in event:
                    value += ":" + script_int(event.get("pad"))
            values.append(value)
        compiled["GE007_AUTO_FORCE_PLAYER_SCRIPT"] = " ".join(values)

    for key, env_key in (
        ("damage_tags", "GE007_AUTO_DAMAGE_TAG_SCRIPT"),
        ("set_stage_flags", "GE007_AUTO_SET_STAGE_FLAGS_SCRIPT"),
        ("unset_stage_flags", "GE007_AUTO_UNSET_STAGE_FLAGS_SCRIPT"),
        ("set_chr_ai", "GE007_AUTO_SET_CHR_AI_SCRIPT"),
    ):
        if not scripted.get(key):
            continue
        values = []
        for event in scripted[key]:
            if key == "damage_tags":
                values.append(
                    f"{script_int(event.get('frame'))}:{script_int(event.get('tag'))}:{script_number(event.get('amount'))}"
                )
            elif key in {"set_stage_flags", "unset_stage_flags"}:
                values.append(f"{script_int(event.get('frame'))}:{script_flags(event.get('flags'))}")
            else:
                values.append(
                    f"{script_int(event.get('frame'))}:{script_int(event.get('chr'))}:{script_int(event.get('ailist'))}"
                )
        compiled[env_key] = ",".join(values)

    if scripted.get("equip_items"):
        compiled["GE007_AUTO_EQUIP_ITEM_SCRIPT"] = ",".join(
            f"{script_int(event.get('frame'))}:{script_int(event.get('item'))}"
            for event in scripted["equip_items"]
        )

    add_items = scripted.get("add_items", [])
    if add_items:
        compiled["GE007_AUTO_ADD_ITEM_FRAME"] = script_int(add_items[0].get("frame"))
        compiled["GE007_AUTO_ADD_ITEM"] = script_int(add_items[0].get("item"))

    debug_dump = scripted.get("debug_dump")
    if isinstance(debug_dump, dict):
        compiled["GE007_AUTO_DEBUG_DUMP_FRAME"] = script_int(debug_dump.get("frame"))

    mission_end = scripted.get("mission_end")
    if isinstance(mission_end, dict):
        compiled["GE007_AUTO_MISSION_END_FRAME"] = script_int(mission_end.get("frame"))
        compiled["GE007_AUTO_MISSION_END_RESULT"] = str(mission_end.get("result")).strip().lower()

    exit_on_title = scripted.get("exit_on_title")
    if isinstance(exit_on_title, dict):
        compiled["GE007_AUTO_EXIT_ON_TITLE"] = "1"
        if "delay" in exit_on_title:
            compiled["GE007_AUTO_EXIT_ON_TITLE_DELAY"] = script_int(exit_on_title.get("delay"))

    return {key: compiled[key] for key in sorted(compiled)}


def validate_frame_milestone(route_name: str,
                             label: str,
                             milestone: Any,
                             index: int,
                             allowed_keys: set[str]) -> None:
    if not isinstance(milestone, dict):
        raise SystemExit(f"FAIL: route {route_name} {label}[{index}] must be an object")
    if "frame" in milestone and "frame_at_or_after" not in milestone:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{index}] uses unsupported key 'frame'; "
            "use 'frame_at_or_after'"
        )
    unknown_keys = sorted(set(milestone) - allowed_keys)
    if unknown_keys:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{index}] has unsupported keys: "
            + ", ".join(unknown_keys)
        )
    if "frame_at_or_after" not in milestone:
        raise SystemExit(f"FAIL: route {route_name} {label}[{index}] missing frame_at_or_after")
    if int_from_any(milestone.get("frame_at_or_after"), -1) < 0:
        raise SystemExit(f"FAIL: route {route_name} {label}[{index}] frame_at_or_after must be >= 0")


def validate_route_assertions(route: dict[str, Any]) -> None:
    assertions = route.get("assertions", {})
    route_name = route.get("name", "<unnamed>")

    for label, allowed_keys in (
        ("position_milestones", POSITION_MILESTONE_KEYS),
        ("objective_milestones", OBJECTIVE_MILESTONE_KEYS),
        ("state_milestones", STATE_MILESTONE_KEYS),
        ("setup_target_milestones", SETUP_TARGET_MILESTONE_KEYS),
    ):
        milestones = assertions.get(label, [])
        if not isinstance(milestones, list):
            raise SystemExit(f"FAIL: route {route_name} {label} must be an array")
        for index, milestone in enumerate(milestones):
            validate_frame_milestone(route_name, label, milestone, index, allowed_keys)
            if label == "objective_milestones" and not isinstance(milestone.get("statuses"), list):
                raise SystemExit(f"FAIL: route {route_name} {label}[{index}] statuses must be an array")
            if label == "state_milestones":
                checks = milestone.get("checks")
                if not isinstance(checks, list) or not checks:
                    raise SystemExit(f"FAIL: route {route_name} {label}[{index}] checks must be a non-empty array")
                for check_index, check in enumerate(checks):
                    validate_state_check(route_name, label, index, check_index, check)
            if label == "setup_target_milestones":
                validate_setup_target_milestone(route_name, index, milestone)

    required_log_patterns = assertions.get("required_log_patterns", [])
    if not isinstance(required_log_patterns, list):
        raise SystemExit(f"FAIL: route {route_name} required_log_patterns must be an array")
    for index, pattern in enumerate(required_log_patterns):
        if not isinstance(pattern, str) or not pattern:
            raise SystemExit(f"FAIL: route {route_name} required_log_patterns[{index}] must be a non-empty string")

    log_count_assertions = assertions.get("log_count_assertions", [])
    if not isinstance(log_count_assertions, list):
        raise SystemExit(f"FAIL: route {route_name} log_count_assertions must be an array")
    for index, assertion in enumerate(log_count_assertions):
        validate_log_count_assertion(route_name, index, assertion)

    for key, allowed_keys in (
        ("save_completion", SAVE_COMPLETION_KEYS),
        ("reload_save_completion", RELOAD_SAVE_COMPLETION_KEYS),
    ):
        if key in assertions:
            validate_save_completion_assertion(route_name, key, assertions[key], allowed_keys)


def validate_save_completion_assertion(route_name: str,
                                       key: str,
                                       assertion: Any,
                                       allowed_keys: set[str]) -> None:
    if not isinstance(assertion, dict):
        raise SystemExit(f"FAIL: route {route_name} {key} must be an object")
    unknown_keys = sorted(set(assertion) - allowed_keys)
    if unknown_keys:
        raise SystemExit(
            f"FAIL: route {route_name} {key} has unsupported keys: "
            + ", ".join(unknown_keys)
        )
    for required in ("folder", "level", "difficulty"):
        if required not in assertion:
            raise SystemExit(f"FAIL: route {route_name} {key} missing {required}")
        if int_from_any(assertion.get(required), -1) < 0:
            raise SystemExit(f"FAIL: route {route_name} {key}.{required} must be >= 0")
    if "frames" in assertion and int_from_any(assertion.get("frames"), 0) <= 0:
        raise SystemExit(f"FAIL: route {route_name} {key}.frames must be a positive integer")
    if "auto_start" in assertion and not isinstance(assertion.get("auto_start"), str):
        raise SystemExit(f"FAIL: route {route_name} {key}.auto_start must be a string")


def validate_setup_target_milestone(route_name: str, index: int, milestone: dict[str, Any]) -> None:
    target = milestone.get("target")
    if not isinstance(target, dict) or not target:
        raise SystemExit(f"FAIL: route {route_name} setup_target_milestones[{index}].target must be a non-empty object")
    unknown_target_keys = sorted(set(target) - SETUP_TARGET_KEYS)
    if unknown_target_keys:
        raise SystemExit(
            f"FAIL: route {route_name} setup_target_milestones[{index}].target has unsupported keys: "
            + ", ".join(unknown_target_keys)
        )
    has_distance_limit = any(
        key in milestone
        for key in ("max_horizontal_distance", "max_distance", "max_abs_y_delta")
    )
    if not has_distance_limit:
        raise SystemExit(
            f"FAIL: route {route_name} setup_target_milestones[{index}] must define a distance limit"
        )
    for key in ("max_horizontal_distance", "max_distance", "max_abs_y_delta"):
        if key in milestone:
            value = number_from_value(milestone.get(key))
            if value is None or value < 0:
                raise SystemExit(f"FAIL: route {route_name} setup_target_milestones[{index}].{key} must be >= 0")


def validate_log_count_assertion(route_name: str, index: int, assertion: Any) -> None:
    if not isinstance(assertion, dict):
        raise SystemExit(f"FAIL: route {route_name} log_count_assertions[{index}] must be an object")
    unknown_keys = sorted(set(assertion) - LOG_COUNT_ASSERTION_KEYS)
    if unknown_keys:
        raise SystemExit(
            f"FAIL: route {route_name} log_count_assertions[{index}] has unsupported keys: "
            + ", ".join(unknown_keys)
        )
    name = assertion.get("name")
    if not isinstance(name, str) or not name:
        raise SystemExit(f"FAIL: route {route_name} log_count_assertions[{index}] name must be non-empty")
    pattern = assertion.get("pattern")
    if not isinstance(pattern, str) or not pattern:
        raise SystemExit(f"FAIL: route {route_name} log_count_assertions[{index}] pattern must be non-empty")
    try:
        re.compile(pattern)
    except re.error as exc:
        raise SystemExit(
            f"FAIL: route {route_name} log_count_assertions[{index}] invalid regex {pattern!r}: {exc}"
        ) from exc
    has_min = "min_count" in assertion
    has_max = "max_count" in assertion
    if not has_min and not has_max:
        raise SystemExit(
            f"FAIL: route {route_name} log_count_assertions[{index}] must define min_count or max_count"
        )
    for key in ("min_count", "max_count"):
        if key in assertion and int_from_any(assertion.get(key), -1) < 0:
            raise SystemExit(
                f"FAIL: route {route_name} log_count_assertions[{index}] {key} must be >= 0"
            )


def validate_state_check(route_name: str,
                         label: str,
                         milestone_index: int,
                         check_index: int,
                         check: Any) -> None:
    if not isinstance(check, dict):
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{milestone_index}].checks[{check_index}] must be an object"
        )
    unknown_keys = sorted(set(check) - STATE_CHECK_KEYS)
    if unknown_keys:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{milestone_index}].checks[{check_index}] has unsupported keys: "
            + ", ".join(unknown_keys)
        )
    path = check.get("path")
    if not isinstance(path, str) or not path:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{milestone_index}].checks[{check_index}] path must be non-empty"
        )
    op = check.get("op", "==")
    if op not in STATE_CHECK_OPS:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{milestone_index}].checks[{check_index}] unsupported op {op!r}"
        )
    if op not in {"exists", "missing", "truthy", "falsy"} and "value" not in check:
        raise SystemExit(
            f"FAIL: route {route_name} {label}[{milestone_index}].checks[{check_index}] missing value"
        )


def env_value(value: Any) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if value is None:
        return ""
    return str(value)


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def parse_key_value_lines(text: str, label: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if "=" not in line:
            raise SystemExit(f"FAIL: malformed {label} line: {line!r}")
        key, value = line.split("=", 1)
        if not key:
            raise SystemExit(f"FAIL: malformed {label} key in line: {line!r}")
        values[key] = value
    return values


def oracle_route_command(command: str, route_name: str, *extra: str) -> str:
    cmd = [sys.executable, str(ORACLE_ROUTE_TOOL), command, route_name, *extra]
    result = subprocess.run(cmd, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SystemExit(f"FAIL: rom oracle route {command} failed for {route_name}: {detail}")
    return result.stdout.strip()


def load_oracle_route_runtime(route: dict[str, Any]) -> dict[str, Any]:
    route_name = route.get("rom_oracle_route")
    if not route_name:
        return {}
    if not isinstance(route_name, str):
        raise SystemExit(f"FAIL: route {route['name']} rom_oracle_route must be a string")

    oracle_route_command("validate", route_name)
    env = parse_key_value_lines(oracle_route_command("native-env", route_name), "native env")
    speedframes = oracle_route_command("field", route_name, "native_speedframes")
    if speedframes:
        env["GE007_DETERMINISTIC_SPEEDFRAMES"] = speedframes

    return {
        "name": route_name,
        "path": oracle_route_command("resolve", route_name),
        "level": int_from_any(oracle_route_command("field", route_name, "native_level")),
        "frames": int_from_any(oracle_route_command("field", route_name, "native_frames")),
        "env": env,
        "config": parse_key_value_lines(oracle_route_command("native-config", route_name), "native config"),
    }


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            raw = raw.strip()
            if not raw:
                continue
            data = json.loads(raw)
            if isinstance(data, dict):
                records.append(data)
    return records


def base_validation_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("GE007_DEBUG", None)
    env.update(
        {
            "SDL_AUDIODRIVER": env.get("GE007_VALIDATION_SDL_AUDIODRIVER", "dummy"),
            "GE007_MUTE": "1",
            "GE007_DETERMINISTIC_STABLE_COUNT": "1",
            "GE007_NO_VSYNC": "1",
            "GE007_BACKGROUND": "1",
            "GE007_NO_INPUT_GRAB": "1",
        }
    )
    return env


def int_from_any(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def max_render_counters(records: list[dict[str, Any]]) -> tuple[dict[str, int], int, int, int]:
    max_dl = {key: 0 for key in DL_KEYS}
    max_bad_cmds = 0
    max_crashes = 0
    max_nan = 0
    for record in records:
        dl = record.get("dl") if isinstance(record.get("dl"), dict) else {}
        for key in DL_KEYS:
            max_dl[key] = max(max_dl[key], int_from_any(dl.get(key)))
        max_bad_cmds = max(max_bad_cmds, int_from_any(record.get("bad_cmds")))
        max_crashes = max(max_crashes, int_from_any(record.get("crashes")))
        max_nan = max(max_nan, int_from_any(record.get("nan")))
    return max_dl, max_bad_cmds, max_crashes, max_nan


def objective_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if isinstance(record.get("obj"), dict)]


def front_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if isinstance(record.get("front"), dict)]


def statuses(record: dict[str, Any] | None) -> list[int] | None:
    if not record:
        return None
    obj = record.get("obj")
    if not isinstance(obj, dict):
        return None
    raw = obj.get("statuses")
    if not isinstance(raw, list):
        return None
    return [int_from_any(item) for item in raw]


def obj_flags(record: dict[str, Any] | None) -> int:
    if not record:
        return 0
    obj = record.get("obj")
    if not isinstance(obj, dict):
        return 0
    return int_from_any(obj.get("flags"))


def horizontal_delta(records: list[dict[str, Any]]) -> float:
    positions: list[tuple[float, float]] = []
    for record in records:
        pos = record.get("pos")
        if not isinstance(pos, list) or len(pos) < 3:
            continue
        try:
            x = float(pos[0])
            z = float(pos[2])
        except (TypeError, ValueError):
            continue
        if math.isfinite(x) and math.isfinite(z):
            positions.append((x, z))
    if len(positions) < 2:
        return 0.0
    start = positions[0]
    return max(math.hypot(x - start[0], z - start[1]) for x, z in positions)


def moving_records(records: list[dict[str, Any]]) -> int:
    count = 0
    for record in records:
        move = record.get("move")
        if not isinstance(move, dict):
            continue
        speed = move.get("speed")
        if not isinstance(speed, list) or len(speed) < 2:
            continue
        try:
            speed_x = float(speed[0])
            speed_z = float(speed[1])
        except (TypeError, ValueError):
            continue
        if abs(speed_x) > 0.001 or abs(speed_z) > 0.001:
            count += 1
    return count


def sorted_int_matches(pattern: str, text: str) -> list[int]:
    return sorted({int(match.group(1), 10) for match in re.finditer(pattern, text, re.MULTILINE)})


def sorted_hex_matches(pattern: str, text: str) -> list[str]:
    values = {
        int(match.group(1), 16)
        for match in re.finditer(pattern, text, re.MULTILINE)
    }
    return [f"0x{value:08X}" for value in sorted(values)]


def objective_status_sets(records: list[dict[str, Any]]) -> list[list[int]]:
    status_sets: list[list[int]] = []
    seen: set[tuple[int, ...]] = set()
    for record in objective_records(records):
        current = statuses(record)
        if current is None:
            continue
        key = tuple(current)
        if key in seen:
            continue
        seen.add(key)
        status_sets.append(current)
    return status_sets


def inventory_keyflag_values(records: list[dict[str, Any]]) -> list[str]:
    values: set[int] = set()
    for record in records:
        inv = record.get("inv")
        if not isinstance(inv, dict):
            continue
        value = number_from_value(inv.get("keyflags"))
        if value is not None:
            values.add(int(value))
    return [f"0x{value:08X}" for value in sorted(values)]


def collect_types_from_logs(log_text: str, event: str) -> list[int]:
    pattern = rf"^\[INTERACT_TRACE\] collect {event} .*?\bobjtype=(\d+)\b"
    return sorted_int_matches(pattern, log_text)


def route_has_direct_setup(summary: dict[str, Any]) -> bool:
    if summary.get("direct_automation_keys"):
        return True
    if summary.get("scripted_event_env"):
        return True
    return False


def state_milestone_names_with_paths(summary: dict[str, Any], path_prefixes: tuple[str, ...]) -> list[str]:
    hits: list[str] = []
    milestones = summary.get("state_milestones")
    if not isinstance(milestones, dict):
        return hits
    for name, milestone in milestones.items():
        if not isinstance(milestone, dict):
            continue
        checks = milestone.get("checks")
        if not isinstance(checks, list):
            continue
        for check in checks:
            if not isinstance(check, dict):
                continue
            path = str(check.get("path", ""))
            if any(path.startswith(prefix) for prefix in path_prefixes):
                hits.append(str(name))
                break
    return sorted(set(hits))


def route_evidence_summary(route: dict[str, Any],
                           summary: dict[str, Any],
                           records: list[dict[str, Any]],
                           log_text: str) -> dict[str, Any]:
    door_allow_objs = sorted_int_matches(r"^\[INTERACT_TRACE\] door allow obj=(\d+)\b", log_text)
    door_open_objs = sorted_int_matches(r"^\[DOOR_TRACE\] frame=\d+ obj=(\d+) state obj=\d+ 0->1\b", log_text)
    door_finish_open_objs = sorted_int_matches(r"^\[DOOR_TRACE\] frame=\d+ obj=(\d+) finish open obj=\d+\b", log_text)
    locked_door_denied_objs = sorted_int_matches(
        r"^\[INTERACT_TRACE\] door deny obj=(\d+) keyflags=0x(?!00000000)[0-9A-Fa-f]+ .*hudmsg=1\b",
        log_text,
    )
    collect_begin_types = collect_types_from_logs(log_text, "begin")
    collect_free_types = collect_types_from_logs(log_text, "free")
    collect_success_types = collect_types_from_logs(log_text, "success")
    collect_keyflag_values = sorted_hex_matches(
        r"^\[INTERACT_TRACE\] collect begin .*?\bkeyflags=0x([0-9A-Fa-f]+)\b",
        log_text,
    )
    keyflag_values = sorted(
        set(collect_keyflag_values)
        | set(inventory_keyflag_values(records))
    )
    nonzero_keyflags = [value for value in keyflag_values if value != "0x00000000"]
    status_sets = objective_status_sets(records)
    stageflag_after = list(summary.get("stageflag_after") or [])
    save_completion = summary.get("save_completion") if isinstance(summary.get("save_completion"), dict) else None
    reload_completion = (
        summary.get("reload_save_completion")
        if isinstance(summary.get("reload_save_completion"), dict)
        else None
    )
    combat_state_hits = [
        name for name in summary.get("state_milestones", {})
        if any(token in name.lower() for token in ("guard", "combat", "damage", "shot", "fire", "knife"))
    ]
    combat_log_hits = [
        name for name in summary.get("log_count_assertions", {})
        if any(token in name.lower() for token in ("guard", "combat", "damage", "shot", "fire", "knife"))
    ]
    equipment_state_hits = state_milestone_names_with_paths(
        summary,
        ("inv.", "watch.", "wr_raw.weaponnum"),
    )

    interaction_count = (
        len(set(door_allow_objs) | set(door_open_objs))
        + len(set(collect_begin_types) | set(collect_free_types) | set(collect_success_types))
        + (1 if combat_state_hits or combat_log_hits else 0)
        + (1 if equipment_state_hits else 0)
    )
    has_traversal = bool(
        summary.get("input_automation_keys")
        and float(summary.get("horizontal_delta") or 0.0) > 25.0
        and int_from_any(summary.get("moving_records")) > 0
    )
    has_single_interaction = interaction_count >= 1
    has_chained_interaction = interaction_count >= 2
    has_mission_state = bool(
        nonzero_keyflags
        or stageflag_after
        or len(status_sets) > 1
        or summary.get("first_objective_all_complete_frame") is not None
        or save_completion
    )
    has_stage_loop = bool(
        summary.get("first_success_report_frame") is not None
        or summary.get("first_title_frame") is not None and summary.get("first_objective_all_complete_frame") is not None
        or reload_completion
    )

    if has_stage_loop:
        tier = "T5"
        tier_name = "stage_loop"
    elif has_mission_state:
        tier = "T4"
        tier_name = "mission_state"
    elif has_chained_interaction:
        tier = "T3"
        tier_name = "chained_interaction"
    elif has_single_interaction:
        tier = "T2"
        tier_name = "single_interaction"
    elif has_traversal:
        tier = "T1"
        tier_name = "traversal"
    else:
        tier = "T0"
        tier_name = "boot_trace"

    reasons: list[str] = []
    if has_traversal:
        reasons.append(
            "input traversal: horizontal_delta={:.2f} moving_records={}".format(
                float(summary.get("horizontal_delta") or 0.0),
                int_from_any(summary.get("moving_records")),
            )
        )
    if door_open_objs:
        reasons.append("door open: " + ",".join(str(obj) for obj in door_open_objs))
    elif door_allow_objs:
        reasons.append("door allow: " + ",".join(str(obj) for obj in door_allow_objs))
    if collect_success_types:
        reasons.append("collect success types: " + ",".join(str(value) for value in collect_success_types))
    elif collect_begin_types:
        reasons.append("collect begin types: " + ",".join(str(value) for value in collect_begin_types))
    if locked_door_denied_objs:
        reasons.append("locked door deny: " + ",".join(str(obj) for obj in locked_door_denied_objs))
    if nonzero_keyflags:
        reasons.append("nonzero keyflags: " + ",".join(nonzero_keyflags))
    if stageflag_after:
        reasons.append("stage flags: " + ",".join(str(value) for value in stageflag_after))
    if summary.get("first_objective_all_complete_frame") is not None:
        reasons.append(f"objective all-complete frame {summary.get('first_objective_all_complete_frame')}")
    if summary.get("first_success_report_frame") is not None:
        reasons.append(f"success report frame {summary.get('first_success_report_frame')}")
    if save_completion:
        reasons.append(f"save completion frame {save_completion.get('first_frame')}")
    if reload_completion:
        reasons.append(f"reload save frame {reload_completion.get('first_save_completion_frame')}")
    if combat_state_hits or combat_log_hits:
        reasons.append("combat evidence: " + ",".join(sorted(set(combat_state_hits + combat_log_hits))))
    if equipment_state_hits:
        reasons.append("equipment state: " + ",".join(equipment_state_hits))

    return {
        "tier": tier,
        "tier_name": tier_name,
        "route_class": route.get("route_class", "unknown"),
        "stock_input_only": bool(summary.get("input_automation_keys")) and not route_has_direct_setup(summary),
        "direct_setup_used": route_has_direct_setup(summary),
        "events": {
            "door_allow_objs": door_allow_objs,
            "door_open_objs": door_open_objs,
            "door_finish_open_objs": door_finish_open_objs,
            "locked_door_denied_objs": locked_door_denied_objs,
            "collect_begin_types": collect_begin_types,
            "collect_free_types": collect_free_types,
            "collect_success_types": collect_success_types,
            "keyflag_values": keyflag_values,
            "nonzero_keyflags": nonzero_keyflags,
            "objective_status_sets": status_sets,
            "stageflag_after": stageflag_after,
            "combat_state_milestones": combat_state_hits,
            "combat_log_assertions": combat_log_hits,
            "equipment_state_milestones": equipment_state_hits,
        },
        "reasons": reasons,
    }


def evidence_line(summary: dict[str, Any]) -> str:
    evidence = summary.get("evidence")
    if not isinstance(evidence, dict):
        return "tier=?"
    events = evidence.get("events") if isinstance(evidence.get("events"), dict) else {}

    def joined_ints(key: str) -> str:
        values = events.get(key)
        if not isinstance(values, list) or not values:
            return "-"
        return ",".join(str(value) for value in values)

    def joined_values(key: str) -> str:
        values = events.get(key)
        if not isinstance(values, list) or not values:
            return "-"
        return ",".join(str(value) for value in values)

    return (
        "tier={tier}/{name} stock_input={stock} setup={setup} "
        "hdelta={hdelta:.2f} moving={moving} doors={doors} collect={collect} "
        "keyflags={keyflags} report={report} reload={reload}"
    ).format(
        tier=evidence.get("tier", "?"),
        name=evidence.get("tier_name", "unknown"),
        stock=1 if evidence.get("stock_input_only") else 0,
        setup=1 if evidence.get("direct_setup_used") else 0,
        hdelta=float(summary.get("horizontal_delta") or 0.0),
        moving=int_from_any(summary.get("moving_records")),
        doors=joined_ints("door_open_objs"),
        collect=joined_ints("collect_success_types") if joined_ints("collect_success_types") != "-" else joined_ints("collect_begin_types"),
        keyflags=joined_values("nonzero_keyflags"),
        report=summary.get("first_success_report_frame") if summary.get("first_success_report_frame") is not None else "-",
        reload=(
            summary.get("reload_save_completion", {}).get("first_save_completion_frame")
            if isinstance(summary.get("reload_save_completion"), dict)
            and summary.get("reload_save_completion", {}).get("first_save_completion_frame") is not None
            else "-"
        ),
    )


def position_from_record(record: dict[str, Any] | None) -> tuple[float, float, float] | None:
    if record is None:
        return None
    pos = record.get("pos")
    if not isinstance(pos, list) or len(pos) < 3:
        return None
    try:
        x = float(pos[0])
        y = float(pos[1])
        z = float(pos[2])
    except (TypeError, ValueError):
        return None
    if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
        return None
    return x, y, z


def position_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if position_from_record(record) is not None]


def first_position_at_or_after(records: list[dict[str, Any]], frame: int) -> dict[str, Any] | None:
    for record in position_records(records):
        if int_from_any(record.get("f"), -1) >= frame:
            return record
    return None


def first_record_at_or_after(records: list[dict[str, Any]], frame: int) -> dict[str, Any] | None:
    for record in records:
        if int_from_any(record.get("f"), -1) >= frame:
            return record
    return None


def path_value(record: dict[str, Any], path: str) -> Any:
    current: Any = record
    for raw_part in path.split("."):
        if not raw_part:
            return MISSING
        part = raw_part
        indexes: list[int] = []
        while "[" in part:
            prefix, suffix = part.split("[", 1)
            index_text, close, remainder = suffix.partition("]")
            if close != "]":
                return MISSING
            try:
                indexes.append(int(index_text, 10))
            except ValueError:
                return MISSING
            part = prefix + remainder
        if part:
            if not isinstance(current, dict) or part not in current:
                return MISSING
            current = current[part]
        for index in indexes:
            if not isinstance(current, list) or index < 0 or index >= len(current):
                return MISSING
            current = current[index]
    return current


def value_for_summary(value: Any) -> Any:
    if value is MISSING:
        return "<missing>"
    return value


def number_from_value(value: Any) -> float | None:
    if value is MISSING or isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        result = float(value)
        return result if math.isfinite(result) else None
    if isinstance(value, str):
        try:
            if value.lower().startswith("0x"):
                return float(int(value, 16))
            result = float(value)
            return result if math.isfinite(result) else None
        except ValueError:
            return None
    return None


def values_equal(actual: Any, expected: Any) -> bool:
    actual_num = number_from_value(actual)
    expected_num = number_from_value(expected)
    if actual_num is not None and expected_num is not None:
        return math.isclose(actual_num, expected_num, rel_tol=0.0, abs_tol=0.0001)
    return actual == expected


def state_check_passes(record: dict[str, Any], check: dict[str, Any]) -> bool:
    actual = path_value(record, str(check["path"]))
    op = check.get("op", "==")
    expected = check.get("value")

    if op == "exists":
        return actual is not MISSING
    if op == "missing":
        return actual is MISSING
    if op == "truthy":
        return actual is not MISSING and bool(actual)
    if op == "falsy":
        return actual is MISSING or not bool(actual)
    if op == "==":
        return actual is not MISSING and values_equal(actual, expected)
    if op == "!=":
        return actual is MISSING or not values_equal(actual, expected)
    if op == "contains":
        if actual is MISSING:
            return False
        if isinstance(actual, dict):
            return expected in actual
        if isinstance(actual, (list, tuple)):
            return expected in actual
        if isinstance(actual, str):
            return str(expected) in actual
        return False

    actual_num = number_from_value(actual)
    expected_num = number_from_value(expected)
    if actual_num is None or expected_num is None:
        return False
    if op == ">":
        return actual_num > expected_num
    if op == ">=":
        return actual_num >= expected_num
    if op == "<":
        return actual_num < expected_num
    if op == "<=":
        return actual_num <= expected_num
    return False


def state_check_summary(record: dict[str, Any] | None, check: dict[str, Any]) -> dict[str, Any]:
    actual = MISSING if record is None else path_value(record, str(check["path"]))
    summary = {
        "path": check["path"],
        "op": check.get("op", "=="),
        "actual": value_for_summary(actual),
        "passed": False if record is None else state_check_passes(record, check),
    }
    if "value" in check:
        summary["value"] = check["value"]
    return summary


def find_state_milestone(records: list[dict[str, Any]],
                         frame: int,
                         checks: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in records:
        if int_from_any(record.get("f"), -1) < frame:
            continue
        if all(state_check_passes(record, check) for check in checks):
            return record
    return None


def evaluate_log_count_assertions(assertions: list[dict[str, Any]],
                                  log_text: str,
                                  failures: list[str]) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for assertion in assertions:
        name = str(assertion["name"])
        pattern = str(assertion["pattern"])
        count = len(re.findall(pattern, log_text, re.MULTILINE))
        result: dict[str, Any] = {
            "count": count,
            "pattern": pattern,
        }
        if "min_count" in assertion:
            min_count = int_from_any(assertion.get("min_count"))
            result["min_count"] = min_count
            if count < min_count:
                failures.append(f"log count assertion {name} count {count} < {min_count}")
        if "max_count" in assertion:
            max_count = int_from_any(assertion.get("max_count"))
            result["max_count"] = max_count
            if count > max_count:
                failures.append(f"log count assertion {name} count {count} > {max_count}")
        results[name] = result
    return results


def find_objective_status_at_or_after(records: list[dict[str, Any]], frame: int, expected: list[int]) -> dict[str, Any] | None:
    for record in objective_records(records):
        if int_from_any(record.get("f"), -1) >= frame and statuses(record) == expected:
            return record
    return None


def first_title_record(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in front_records(records):
        front = record["front"]
        if int_from_any(front.get("loaded_stage"), -1) == 90 and int_from_any(front.get("active_stage"), -1) == 90:
            return record
    return None


def first_success_report(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in front_records(records):
        front = record["front"]
        if (
            int_from_any(front.get("menu"), -999) == 12
            and int_from_any(front.get("all_obj_complete")) == 1
            and int_from_any(front.get("mission_failed")) == 0
            and int_from_any(front.get("bond_kia")) == 0
        ):
            return record
    return None


def first_objective_all_complete(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in objective_records(records):
        obj = record["obj"]
        if int_from_any(obj.get("all_complete")) == 1:
            return record
    return None


def save_completion_frames(records: list[dict[str, Any]],
                           folder: int,
                           level_id: int,
                           difficulty_id: int) -> list[int]:
    hits: list[int] = []
    for record in records:
        save = record.get("save")
        if not isinstance(save, dict):
            continue
        valid = save.get("valid")
        level = save.get("level")
        difficulty = save.get("difficulty")
        if not (isinstance(valid, list) and isinstance(level, list) and isinstance(difficulty, list)):
            continue
        if folder >= len(valid) or folder >= len(level) or folder >= len(difficulty):
            continue
        if (
            int_from_any(valid[folder], -1) == 1
            and int_from_any(level[folder], -1) == level_id
            and int_from_any(difficulty[folder], -1) == difficulty_id
        ):
            hits.append(int_from_any(record.get("f"), -1))
    return hits


def save_completion_expectation(assertion: dict[str, Any]) -> tuple[int, int, int]:
    return (
        int_from_any(assertion.get("folder"), -1),
        int_from_any(assertion.get("level"), -1),
        int_from_any(assertion.get("difficulty"), -1),
    )


def route_requires_setup_dump(route: dict[str, Any]) -> bool:
    assertions = route.get("assertions", {})
    return bool(assertions.get("setup_target_milestones"))


def setup_row_has_position(row: dict[str, Any]) -> bool:
    pos = row.get("pos")
    return (
        isinstance(pos, list)
        and len(pos) >= 3
        and number_from_value(pos[0]) is not None
        and number_from_value(pos[1]) is not None
        and number_from_value(pos[2]) is not None
    )


def setup_row_position(row: dict[str, Any]) -> tuple[float, float, float] | None:
    if not setup_row_has_position(row):
        return None
    pos = row["pos"]
    return float(pos[0]), float(pos[1]), float(pos[2])


def setup_target_matches(row: dict[str, Any], target: dict[str, Any]) -> bool:
    for key, expected in target.items():
        actual = row.get(key)
        if actual is None:
            return False
        if not values_equal(actual, expected):
            return False
    return True


def setup_row_summary(row: dict[str, Any]) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    for key in ("kind", "index", "type", "type_name", "obj", "pad", "room", "tag", "target_obj", "target_pad", "target_type_name"):
        if key in row:
            summary[key] = row[key]
    pos = setup_row_position(row)
    if pos is not None:
        summary["pos"] = [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)]
    return summary


def setup_target_rows(setup_records: list[dict[str, Any]], target: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        row for row in setup_records
        if setup_row_has_position(row) and setup_target_matches(row, target)
    ]


def setup_target_distance_summary(record: dict[str, Any],
                                  target_row: dict[str, Any]) -> dict[str, Any]:
    pos = position_from_record(record)
    target_pos = setup_row_position(target_row)
    if pos is None or target_pos is None:
        return {}
    dx = pos[0] - target_pos[0]
    dy = pos[1] - target_pos[1]
    dz = pos[2] - target_pos[2]
    return {
        "frame": record.get("f"),
        "pos": [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)],
        "target": setup_row_summary(target_row),
        "delta": {
            "x": round(dx, 2),
            "y": round(dy, 2),
            "z": round(dz, 2),
            "horizontal": round(math.hypot(dx, dz), 2),
            "distance": round(math.sqrt(dx * dx + dy * dy + dz * dz), 2),
        },
    }


def setup_target_distance_passes(summary: dict[str, Any], milestone: dict[str, Any]) -> bool:
    delta = summary.get("delta")
    if not isinstance(delta, dict):
        return False
    max_horizontal = milestone.get("max_horizontal_distance")
    if max_horizontal is not None and float(delta.get("horizontal", math.inf)) > float(max_horizontal):
        return False
    max_distance = milestone.get("max_distance")
    if max_distance is not None and float(delta.get("distance", math.inf)) > float(max_distance):
        return False
    max_abs_y = milestone.get("max_abs_y_delta")
    if max_abs_y is not None and abs(float(delta.get("y", math.inf))) > float(max_abs_y):
        return False
    return True


def find_setup_target_milestone(records: list[dict[str, Any]],
                                target_rows: list[dict[str, Any]],
                                milestone: dict[str, Any]) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    frame = int_from_any(milestone.get("frame_at_or_after"))
    closest: dict[str, Any] | None = None
    closest_distance = math.inf
    for record in position_records(records):
        if int_from_any(record.get("f"), -1) < frame:
            continue
        for target_row in target_rows:
            summary = setup_target_distance_summary(record, target_row)
            if not summary:
                continue
            delta = summary.get("delta")
            if not isinstance(delta, dict):
                continue
            distance = float(delta.get("distance", math.inf))
            if distance < closest_distance:
                closest = summary
                closest_distance = distance
            if setup_target_distance_passes(summary, milestone):
                return summary, closest
    return None, closest


def audit_route(route: dict[str, Any],
                level: int,
                effective_env: dict[str, str],
                records: list[dict[str, Any]],
                log_text: str,
                setup_records: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    assertions = route.get("assertions", {})
    setup_records = setup_records or []
    failures: list[str] = []
    obj_rows = objective_records(records)
    front_rows = front_records(records)
    pos_rows = position_records(records)
    max_dl, max_bad_cmds, max_crashes, max_nan = max_render_counters(records)
    title_record = first_title_record(records)
    success_report = first_success_report(records)
    all_complete_record = first_objective_all_complete(records)
    active_input_env = sorted(
        key for key, value in effective_env.items()
        if key in INPUT_AUTOMATION_ENV_KEYS and value not in ("", "0")
    )
    stageflag_after = [
        int(match.group(1), 16)
        for match in re.finditer(r"^\[STAGEFLAG_TRACE\].*after=0x([0-9A-Fa-f]+)", log_text, re.MULTILINE)
    ]
    allowed_automation = INPUT_AUTOMATION_ENV_KEYS | UTILITY_AUTOMATION_ENV_KEYS
    active_direct_env = sorted(
        key for key, value in effective_env.items()
        if key.startswith("GE007_AUTO_")
        and key not in allowed_automation
        and value not in ("", "0")
    )

    if assertions.get("require_input_automation") and not active_input_env:
        failures.append("no input automation env keys were active")
    if assertions.get("no_direct_state_automation"):
        if active_direct_env:
            failures.append("direct state automation env keys were active: " + ", ".join(active_direct_env))

    if "[GEASSERT]" in log_text:
        failures.append("GEASSERT appeared in log")
    if assertions.get("expect_frame_exit") and "deterministic frame exit observed" not in log_text:
        failures.append("deterministic frame-exit marker was missing")
    if assertions.get("expect_title_exit") and "deterministic title return observed" not in log_text:
        failures.append("deterministic title-return marker was missing")
    for pattern in assertions.get("required_log_patterns", []):
        if pattern not in log_text:
            failures.append(f"required log pattern was missing: {pattern!r}")
    log_count_assertions = assertions.get("log_count_assertions", [])
    log_count_results = evaluate_log_count_assertions(log_count_assertions, log_text, failures)

    min_records = int_from_any(assertions.get("min_records"))
    if min_records and len(records) < min_records:
        failures.append(f"trace record count below threshold: {len(records)} < {min_records}")

    if assertions.get("no_render_failures"):
        for key, value in max_dl.items():
            if value:
                failures.append(f"display-list resolve counter {key} reached {value}")
        if max_bad_cmds:
            failures.append(f"bad_cmds reached {max_bad_cmds}")
        if max_crashes:
            failures.append(f"crashes reached {max_crashes}")
        if max_nan:
            failures.append(f"nan counter reached {max_nan}")

    min_horizontal_delta = assertions.get("min_horizontal_delta")
    if min_horizontal_delta is not None and horizontal_delta(records) < float(min_horizontal_delta):
        failures.append(f"horizontal delta below threshold: {horizontal_delta(records):.2f} < {float(min_horizontal_delta):.2f}")

    min_moving_records = int_from_any(assertions.get("min_moving_records"))
    if min_moving_records and moving_records(records) < min_moving_records:
        failures.append(f"moving records below threshold: {moving_records(records)} < {min_moving_records}")

    position_milestone_hits: dict[str, dict[str, Any]] = {}
    for milestone in assertions.get("position_milestones", []):
        if not isinstance(milestone, dict):
            failures.append(f"malformed position milestone: {milestone!r}")
            continue
        name = str(milestone.get("name", "unnamed"))
        frame = int_from_any(milestone.get("frame_at_or_after"))
        hit = first_position_at_or_after(records, frame)
        start_pos = position_from_record(pos_rows[0] if pos_rows else None)
        hit_pos = position_from_record(hit)
        if hit is None or start_pos is None or hit_pos is None:
            failures.append(f"missing position milestone {name}: frame>={frame}")
            continue
        dx = hit_pos[0] - start_pos[0]
        dy = hit_pos[1] - start_pos[1]
        dz = hit_pos[2] - start_pos[2]
        horizontal = math.hypot(dx, dz)
        min_delta = milestone.get("min_horizontal_delta")
        if min_delta is not None and horizontal < float(min_delta):
            failures.append(f"position milestone {name} horizontal delta {horizontal:.2f} < {float(min_delta):.2f}")
        max_delta = milestone.get("max_horizontal_delta")
        if max_delta is not None and horizontal > float(max_delta):
            failures.append(f"position milestone {name} horizontal delta {horizontal:.2f} > {float(max_delta):.2f}")
        min_abs_x = milestone.get("min_abs_x_delta")
        if min_abs_x is not None and abs(dx) < float(min_abs_x):
            failures.append(f"position milestone {name} abs dx {abs(dx):.2f} < {float(min_abs_x):.2f}")
        min_abs_z = milestone.get("min_abs_z_delta")
        if min_abs_z is not None and abs(dz) < float(min_abs_z):
            failures.append(f"position milestone {name} abs dz {abs(dz):.2f} < {float(min_abs_z):.2f}")
        position_milestone_hits[name] = {
            "frame": hit.get("f"),
            "pos": [round(hit_pos[0], 2), round(hit_pos[1], 2), round(hit_pos[2], 2)],
            "delta": {
                "x": round(dx, 2),
                "y": round(dy, 2),
                "z": round(dz, 2),
                "horizontal": round(horizontal, 2),
            },
        }

    milestone_hits: dict[str, dict[str, Any]] = {}
    for milestone in assertions.get("objective_milestones", []):
        if not isinstance(milestone, dict):
            failures.append(f"malformed objective milestone: {milestone!r}")
            continue
        name = str(milestone.get("name", "unnamed"))
        frame = int_from_any(milestone.get("frame_at_or_after"))
        expected_statuses = [int_from_any(item) for item in milestone.get("statuses", [])]
        hit = find_objective_status_at_or_after(records, frame, expected_statuses)
        if hit is None:
            failures.append(f"missing objective milestone {name}: frame>={frame} statuses={expected_statuses}")
        else:
            milestone_hits[name] = hit

    state_milestone_hits: dict[str, dict[str, Any]] = {}
    for milestone in assertions.get("state_milestones", []):
        if not isinstance(milestone, dict):
            failures.append(f"malformed state milestone: {milestone!r}")
            continue
        name = str(milestone.get("name", "unnamed"))
        frame = int_from_any(milestone.get("frame_at_or_after"))
        checks = milestone.get("checks", [])
        if not isinstance(checks, list):
            failures.append(f"malformed state milestone {name}: checks must be an array")
            continue
        hit = find_state_milestone(records, frame, checks)
        if hit is None:
            probe = first_record_at_or_after(records, frame)
            check_details = [state_check_summary(probe, check) for check in checks if isinstance(check, dict)]
            failures.append(f"missing state milestone {name}: frame>={frame} checks={check_details}")
        else:
            state_milestone_hits[name] = {
                "frame": hit.get("f"),
                "checks": [state_check_summary(hit, check) for check in checks if isinstance(check, dict)],
            }

    setup_target_milestone_hits: dict[str, dict[str, Any]] = {}
    for milestone in assertions.get("setup_target_milestones", []):
        if not isinstance(milestone, dict):
            failures.append(f"malformed setup target milestone: {milestone!r}")
            continue
        name = str(milestone.get("name", "unnamed"))
        target = milestone.get("target", {})
        frame = int_from_any(milestone.get("frame_at_or_after"))
        if not setup_records:
            failures.append(f"missing setup dump for setup target milestone {name}")
            continue
        if not isinstance(target, dict):
            failures.append(f"malformed setup target milestone {name}: target must be an object")
            continue
        target_rows = setup_target_rows(setup_records, target)
        if not target_rows:
            failures.append(f"missing setup target rows for milestone {name}: target={target}")
            continue
        hit, closest = find_setup_target_milestone(records, target_rows, milestone)
        if hit is None:
            failures.append(
                f"missing setup target milestone {name}: frame>={frame} "
                f"target={target} closest={closest}"
            )
        else:
            setup_target_milestone_hits[name] = hit

    final_objective = assertions.get("final_objective")
    if isinstance(final_objective, dict):
        final_record = obj_rows[-1] if obj_rows else None
        expected_statuses = final_objective.get("statuses")
        if isinstance(expected_statuses, list) and statuses(final_record) != [int_from_any(item) for item in expected_statuses]:
            failures.append(f"final objective statuses mismatch: {statuses(final_record)} != {expected_statuses}")
        flags = obj_flags(final_record)
        include = final_objective.get("flags_include")
        if include is not None and (flags & int_from_any(include)) != int_from_any(include):
            failures.append(f"final objective flags missing {include}: 0x{flags:08X}")
        exclude = final_objective.get("flags_exclude")
        if exclude is not None and (flags & int_from_any(exclude)) != 0:
            failures.append(f"final objective flags include excluded mask {exclude}: 0x{flags:08X}")

    for expected in assertions.get("required_stageflag_after", []):
        expected_int = int_from_any(expected)
        if expected_int not in stageflag_after:
            failures.append(f"missing stage flag trace after=0x{expected_int:08X}")

    if assertions.get("success_report") and success_report is None:
        failures.append("success mission-report menu state was not observed")
    if assertions.get("title_return") and title_record is None:
        failures.append("title/menu return was not observed")
    if assertions.get("objective_all_complete") and all_complete_record is None:
        failures.append("objective all_complete=1 was not observed")
    if assertions.get("title_after_objective_all_complete"):
        if all_complete_record is None or title_record is None:
            failures.append("cannot prove objective completion before title return")
        elif int_from_any(all_complete_record.get("f")) >= int_from_any(title_record.get("f")):
            failures.append(
                f"objective completion did not precede title return: obj={all_complete_record.get('f')} title={title_record.get('f')}"
            )
    if assertions.get("no_front_failure"):
        for record in front_rows:
            front = record["front"]
            if int_from_any(front.get("mission_failed")) or int_from_any(front.get("bond_kia")):
                failures.append(f"mission_failed/bond_kia set at frame {record.get('f')}")
                break

    initial_statuses = assertions.get("initial_objective_statuses")
    if isinstance(initial_statuses, list):
        expected = [int_from_any(item) for item in initial_statuses]
        if not any(statuses(record) == expected for record in obj_rows):
            failures.append(f"initial objective statuses were not observed: {expected}")

    save_completion = assertions.get("save_completion")
    save_completion_summary = None
    if isinstance(save_completion, dict):
        folder, level_id, difficulty_id = save_completion_expectation(save_completion)
        hits = save_completion_frames(records, folder, level_id, difficulty_id)
        save_completion_summary = {
            "folder": folder,
            "level": level_id,
            "difficulty": difficulty_id,
            "frames": hits,
            "first_frame": hits[0] if hits else None,
        }
        if not hits:
            failures.append(
                "save completion was not observed: "
                f"folder={folder} level={level_id} difficulty={difficulty_id}"
            )

    final_statuses = assertions.get("final_statuses")
    if isinstance(final_statuses, list):
        expected = [int_from_any(item) for item in final_statuses]
        if not obj_rows or statuses(obj_rows[-1]) != expected:
            failures.append(f"final statuses mismatch: {statuses(obj_rows[-1] if obj_rows else None)} != {expected}")

    return {
        "status": "fail" if failures else "pass",
        "route": route["name"],
        "route_class": route.get("route_class", "unknown"),
        "rom_oracle_route": route.get("rom_oracle_route"),
        "level": level,
        "records": len(records),
        "objective_records": len(obj_rows),
        "front_records": len(front_rows),
        "position_records": len(pos_rows),
        "max_dl": max_dl,
        "max_bad_cmds": max_bad_cmds,
        "max_crashes": max_crashes,
        "max_nan": max_nan,
        "moving_records": moving_records(records),
        "horizontal_delta": horizontal_delta(records),
        "input_automation_keys": active_input_env,
        "direct_automation_keys": active_direct_env,
        "first_success_report_frame": success_report.get("f") if success_report else None,
        "first_objective_all_complete_frame": all_complete_record.get("f") if all_complete_record else None,
        "first_title_frame": title_record.get("f") if title_record else None,
        "objective_milestones": {
            name: {
                "frame": hit.get("f"),
                "statuses": statuses(hit),
                "flags": (hit.get("obj") or {}).get("flags"),
            }
            for name, hit in sorted(milestone_hits.items())
        },
        "state_milestones": {
            name: hit for name, hit in sorted(state_milestone_hits.items())
        },
        "log_count_assertions": {
            name: hit for name, hit in sorted(log_count_results.items())
        },
        "position_milestones": {
            name: hit for name, hit in sorted(position_milestone_hits.items())
        },
        "setup_target_milestones": {
            name: hit for name, hit in sorted(setup_target_milestone_hits.items())
        },
        "stageflag_after": [f"0x{value:08X}" for value in stageflag_after],
        "save_completion": save_completion_summary,
        "failures": failures,
    }


def run_reload_save_completion(binary: Path,
                               rom: Path,
                               case_dir: Path,
                               save_dir: Path,
                               timeout: int,
                               route: dict[str, Any],
                               assertion: dict[str, Any]) -> dict[str, Any]:
    reload_trace_path = case_dir / "reload_trace.jsonl"
    reload_log_path = case_dir / "reload.log"
    reload_screenshot = case_dir / "campaign_reload.bmp"
    for path in (reload_trace_path, reload_log_path, reload_screenshot):
        path.unlink(missing_ok=True)

    frames = int_from_any(assertion.get("frames"), 240)
    auto_start = str(assertion.get("auto_start", "20:3,80:3,140:3,200:3"))
    require_title = bool(assertion.get("require_title", True))
    folder, level_id, difficulty_id = save_completion_expectation(assertion)
    failures: list[str] = []

    eeprom_path = save_dir / "ge007_eeprom.bin"
    if not eeprom_path.is_file() or eeprom_path.stat().st_size == 0:
        return {
            "status": "fail",
            "trace": str(reload_trace_path),
            "log": str(reload_log_path),
            "failures": [f"persisted EEPROM was not written: {eeprom_path}"],
        }

    env = base_validation_env()
    for key in list(env):
        if key.startswith("GE007_AUTO_"):
            env.pop(key, None)
    env["GE007_TRACE_FLOW_ONLY"] = "1"
    if auto_start:
        env["GE007_AUTO_START"] = auto_start

    cmd = [
        str(binary),
        "--savedir",
        str(save_dir),
        "--rom",
        str(rom),
        "--deterministic",
        "--trace-state",
        str(reload_trace_path),
        "--screenshot-frame",
        str(frames),
        "--screenshot-label",
        "campaign_reload",
        "--screenshot-exit",
    ]

    with reload_log_path.open("w", encoding="utf-8") as log_file:
        try:
            result = subprocess.run(
                cmd,
                cwd=case_dir,
                env=env,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return {
                "status": "fail",
                "trace": str(reload_trace_path),
                "log": str(reload_log_path),
                "failures": [f"reload process timed out after {timeout}s"],
            }

    log_text = reload_log_path.read_text(encoding="utf-8", errors="replace")
    if result.returncode != 0:
        failures.append(f"reload process exited with code {result.returncode}")
    if "[GEASSERT]" in log_text:
        failures.append("GEASSERT appeared in reload log")
    if not reload_trace_path.is_file() or reload_trace_path.stat().st_size == 0:
        failures.append(f"reload trace was not written: {reload_trace_path}")
        records: list[dict[str, Any]] = []
    else:
        records = load_jsonl(reload_trace_path)

    title_record = first_title_record(records)
    if require_title and title_record is None:
        failures.append("reload trace did not observe title/menu state")

    hits = save_completion_frames(records, folder, level_id, difficulty_id)
    if not hits:
        last_save = None
        for record in reversed(records):
            if isinstance(record.get("save"), dict):
                last_save = record.get("save")
                break
        failures.append(
            "reload trace did not report persisted save completion: "
            f"folder={folder} level={level_id} difficulty={difficulty_id} last_save={last_save}"
        )

    summary = {
        "status": "fail" if failures else "pass",
        "route": route["name"],
        "trace": str(reload_trace_path),
        "log": str(reload_log_path),
        "records": len(records),
        "front_records": len(front_records(records)),
        "first_title_frame": title_record.get("f") if title_record else None,
        "folder": folder,
        "level": level_id,
        "difficulty": difficulty_id,
        "frames": hits,
        "first_save_completion_frame": hits[0] if hits else None,
        "failures": failures,
    }
    return summary


def run_route(binary: Path, rom: Path, out_dir: Path, timeout: int, route: dict[str, Any]) -> dict[str, Any]:
    oracle_runtime = load_oracle_route_runtime(route)
    level = int_from_any(route.get("level", oracle_runtime.get("level")), -999999)
    frames = int_from_any(route.get("frames", oracle_runtime.get("frames")), 0)
    case_dir = out_dir / safe_name(route["name"])
    save_dir = case_dir / "save"
    trace_path = case_dir / "trace.jsonl"
    log_path = case_dir / "run.log"
    summary_path = case_dir / "summary.json"
    stage_dump_path = case_dir / "stage_pads.jsonl"
    case_dir.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(save_dir, ignore_errors=True)
    save_dir.mkdir(parents=True, exist_ok=True)
    for path in (trace_path, log_path, summary_path, stage_dump_path, case_dir / "reload_trace.jsonl", case_dir / "reload.log"):
        path.unlink(missing_ok=True)

    env = base_validation_env()
    effective_env: dict[str, str] = {}
    for key, value in oracle_runtime.get("env", {}).items():
        env[key] = env_value(value)
        effective_env[key] = env_value(value)
    if frames:
        env.setdefault("GE007_AUTO_EXIT_FRAME", str(frames))
        effective_env.setdefault("GE007_AUTO_EXIT_FRAME", str(frames))
    if route_requires_setup_dump(route):
        env["GE007_DUMP_STAGE_PADS"] = str(stage_dump_path)
        env["GE007_DUMP_STAGE_PADS_FRAME"] = "2"
    route_env = route.get("env", {})
    segment_env = compile_input_segments(route)
    scripted_event_env = compile_scripted_events(route)
    for key in segment_env:
        if key in route_env and env_value(route_env[key]) not in ("", "0"):
            raise SystemExit(
                f"FAIL: route {route['name']} defines both input_segments and env.{key}; "
                "use one input-authoring path for that key"
            )
    for key in scripted_event_env:
        if key in route_env and env_value(route_env[key]) not in ("", "0"):
            raise SystemExit(
                f"FAIL: route {route['name']} defines both scripted_events and env.{key}; "
                "use one route-authoring path for that key"
            )
        existing = effective_env.get(key)
        if existing and existing not in ("", "0"):
            raise SystemExit(
                f"FAIL: route {route['name']} scripted_events collide with inherited {key}; "
                "use one route-authoring path for that key"
            )
    for key, value in segment_env.items():
        existing = effective_env.get(key)
        if existing and existing not in ("", "0"):
            value = f"{existing},{value}"
        env[key] = value
        effective_env[key] = value
    for key, value in scripted_event_env.items():
        env[key] = value
        effective_env[key] = value
    for key, value in route_env.items():
        if not isinstance(key, str) or not key:
            raise SystemExit(f"FAIL: route {route['name']} env key must be a non-empty string")
        env[key] = env_value(value)
        effective_env[key] = env_value(value)

    config: dict[str, Any] = {}
    config.update(oracle_runtime.get("config", {}))
    config.update(route.get("config") or {})

    cmd = [
        str(binary),
        "--savedir",
        str(save_dir),
        "--rom",
        str(rom),
        "--level",
        str(level),
        "--deterministic",
        "--trace-state",
        str(trace_path),
    ]
    for key, value in config.items():
        cmd.extend(["--config-override", f"{key}={env_value(value)}"])

    with log_path.open("w", encoding="utf-8") as log_file:
        try:
            result = subprocess.run(
                cmd,
                cwd=case_dir,
                env=env,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return {
                "status": "fail",
                "route": route["name"],
                "route_class": route.get("route_class", "unknown"),
                "level": level,
                "artifact": str(case_dir),
                "failures": [f"process timed out after {timeout}s"],
            }

    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    if result.returncode != 0:
        return {
            "status": "fail",
            "route": route["name"],
            "route_class": route.get("route_class", "unknown"),
            "level": level,
            "artifact": str(case_dir),
            "failures": [f"process exited with code {result.returncode}"],
            "log_tail": "\n".join(log_text.splitlines()[-40:]),
        }
    if not trace_path.is_file() or trace_path.stat().st_size == 0:
        return {
            "status": "fail",
            "route": route["name"],
            "route_class": route.get("route_class", "unknown"),
            "level": level,
            "artifact": str(case_dir),
            "failures": [f"trace was not written: {trace_path}"],
            "log_tail": "\n".join(log_text.splitlines()[-40:]),
        }

    records = load_jsonl(trace_path)
    setup_records = load_jsonl(stage_dump_path) if stage_dump_path.is_file() else []
    summary = audit_route(route, level, effective_env, records, log_text, setup_records)
    summary.update(
        {
            "artifact": str(case_dir),
            "route_path": route.get("_path"),
            "oracle_route_path": oracle_runtime.get("path"),
            "trace": str(trace_path),
            "log": str(log_path),
            "input_segment_env": segment_env,
            "scripted_event_env": scripted_event_env,
        }
    )
    if route_requires_setup_dump(route):
        summary["stage_dump"] = str(stage_dump_path)
        summary["stage_dump_records"] = len(setup_records)

    reload_save_completion = route.get("assertions", {}).get("reload_save_completion")
    if summary["status"] == "pass" and isinstance(reload_save_completion, dict):
        reload_summary = run_reload_save_completion(
            binary,
            rom,
            case_dir,
            save_dir,
            timeout,
            route,
            reload_save_completion,
        )
        summary["reload_save_completion"] = reload_summary
        if reload_summary["status"] != "pass":
            summary["status"] = "fail"
            summary.setdefault("failures", []).extend(reload_summary.get("failures", []))

    summary["evidence"] = route_evidence_summary(route, summary, records, log_text)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def tier_counts(results: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for result in results:
        evidence = result.get("evidence")
        tier = evidence.get("tier") if isinstance(evidence, dict) else "unknown"
        counts[str(tier)] = counts.get(str(tier), 0) + 1
    return {key: counts[key] for key in sorted(counts)}


def route_class_counts(results: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for result in results:
        route_class = str(result.get("route_class", "unknown"))
        counts[route_class] = counts.get(route_class, 0) + 1
    return {key: counts[key] for key in sorted(counts)}


def main() -> int:
    args = parse_args()
    if args.list:
        for path in sorted(ROUTE_DIR.glob("*.json")):
            route = load_route(path)
            print(f"{route['name']}\t{route.get('route_class', '')}\t{path}")
        return 0

    binary = Path(args.binary).resolve()
    rom = Path(args.rom).resolve()
    out_dir = Path(args.out_dir).resolve()
    if not binary.is_file():
        raise SystemExit(f"FAIL: binary not found: {binary}")
    if not rom.is_file():
        raise SystemExit(f"FAIL: ROM not found: {rom}")
    if args.timeout <= 0:
        raise SystemExit("FAIL: --timeout must be positive")

    out_dir.mkdir(parents=True, exist_ok=True)
    routes = [load_route(resolve_route(item)) for item in (args.route or DEFAULT_ROUTES)]
    results: list[dict[str, Any]] = []

    print("=== Campaign Route Smoke ===")
    print(f"  out-dir: {out_dir}")
    print(f"  binary:  {binary}")
    print(f"  ROM:     {rom}")
    print("  routes:  " + " ".join(route["name"] for route in routes))

    for route in routes:
        print(f"\n=== Campaign Route: {route['name']} ===")
        summary = run_route(binary, rom, out_dir, args.timeout, route)
        results.append(summary)
        if summary["status"] == "pass":
            reload_summary = summary.get("reload_save_completion")
            reload_detail = ""
            if isinstance(reload_summary, dict):
                reload_detail = " reload_save={}".format(
                    reload_summary.get("first_save_completion_frame")
                )
            print(
                "PASS: {route} records={records} obj={obj} title={title}{reload_detail} {evidence} artifact={artifact}".format(
                    route=summary["route"],
                    records=summary.get("records"),
                    obj=summary.get("objective_records"),
                    title=summary.get("first_title_frame"),
                    reload_detail=reload_detail,
                    evidence=evidence_line(summary),
                    artifact=summary.get("artifact"),
                )
            )
        else:
            print(f"FAIL: {summary['route']} artifact={summary.get('artifact')}")
            for failure in summary.get("failures", []):
                print(f"  {failure}")
            if summary.get("log_tail"):
                print("  log tail:")
                for line in str(summary["log_tail"]).splitlines():
                    print(f"    {line}")

    failures = [result for result in results if result.get("status") != "pass"]
    top_summary = {
        "status": "fail" if failures else "pass",
        "out_dir": str(out_dir),
        "counts": {
            "routes": len(results),
            "passed": len(results) - len(failures),
            "failed": len(failures),
            "tiers": tier_counts(results),
            "route_classes": route_class_counts(results),
        },
        "results": results,
    }
    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(top_summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("\n=== Campaign Route Summary ===")
    print(f"  passed: {len(results) - len(failures)}")
    print(f"  failed: {len(failures)}")
    print("  tiers:  " + " ".join(f"{key}={value}" for key, value in tier_counts(results).items()))
    print(f"  summary: {summary_path}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
