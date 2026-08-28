#!/usr/bin/env python3
"""Summarize stock Parallel-RDP post-draw pixel probe captures.

The JSONL input is produced by the dev-only ares hook injected by
tools/prepare_ares_movement_oracle_build.sh when
MGB64_ARES_TRACE_RDP_PIXEL_PROBE=1 is set. It samples one RDP framebuffer pixel
after each draw whose conservative bbox contains that pixel.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
from typing import Any


def parse_int(value: Any, fallback: int = 0) -> int:
    if value is None:
        return fallback
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return fallback
    return fallback


def parse_xy(value: str) -> tuple[int, int]:
    parts = value.lower().replace("x", ",").split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected X,Y")
    try:
        x, y = (int(part) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("X,Y must be integers") from exc
    if x < 0 or y < 0:
        raise argparse.ArgumentTypeError("X,Y must be non-negative")
    return x, y


def parse_size(value: str) -> tuple[int, int]:
    width, height = parse_xy(value)
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return width, height


def route_region(route_path: Path | None, name: str | None) -> dict[str, Any] | None:
    if route_path is None or name is None:
        return None
    with route_path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    for item in route.get("visual_regions", route.get("regions", [])) or []:
        if item.get("name") != name:
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            raise ValueError(f"{route_path}: region {name!r} has invalid roi")
        x, y, width, height = (int(value) for value in roi)
        if width <= 0 or height <= 0:
            raise ValueError(f"{route_path}: region {name!r} has non-positive roi")
        return {
            "name": name,
            "roi": [x, y, width, height],
            "bounds": [x, y, x + width, y + height],
            "center": [x + width // 2, y + height // 2],
            "logical_size": route.get("visual_logical_size"),
        }
    raise ValueError(f"{route_path}: region {name!r} not found")


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
    return records


def record_xy(record: dict[str, Any]) -> tuple[int, int]:
    return parse_int(record.get("x")), parse_int(record.get("y"))


def sample_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("type", "sample") == "sample"]


def stats_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("type") == "stats"]


def record_key(record: dict[str, Any], fields: tuple[str, ...]) -> tuple[Any, ...]:
    return tuple(record.get(field) for field in fields)


def compact_record(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    keys = (
        "frame_context",
        "command_sequence",
        "draw_sequence",
        "frame_command_sequence",
        "frame_draw_sequence",
        "op",
        "x",
        "y",
        "fb_addr",
        "fb_width",
        "fb_fmt",
        "fb_size",
        "texture_image",
        "texture_width",
        "texture_fmt",
        "texture_size",
        "texture_serial",
        "other",
        "combine",
        "env",
        "scissor",
        "raster_flags",
        "raster_dither",
        "static_texture_fmt",
        "static_texture_size",
        "depth_flags",
        "coverage_mode",
        "z_mode",
        "blend_cycles",
        "combiner",
        "draw_tile",
        "tile_state",
        "bbox",
        "draw_word_count",
        "draw_words_truncated",
        "draw_words",
        "raw",
        "hidden",
        "rgba",
        "changed",
    )
    return {key: record[key] for key in keys if key in record}


def compact_stats(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    keys = (
        "event",
        "frame_context",
        "command_sequence",
        "draw_sequence",
        "x",
        "y",
        "fb_addr",
        "fb_width",
        "fb_fmt",
        "fb_size",
        "commands_seen",
        "draw_ops_seen",
        "bbox_ok",
        "bbox_fail",
        "bbox_target_miss",
        "target_hits",
        "forced_samples",
        "sample_attempts",
        "read_ok",
        "read_fail_no_fb",
        "read_fail_x_oob",
        "read_fail_no_maps",
        "read_fail_no_size",
        "read_fail_oob",
        "suppressed_unchanged",
        "records",
        "max_records",
    )
    return {key: record[key] for key in keys if key in record}


def counter_rows(counter: Counter[tuple[Any, ...]], fields: tuple[str, ...], limit: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for values, count in counter.most_common(limit):
        row = {field: value for field, value in zip(fields, values)}
        row["records"] = count
        rows.append(row)
    return rows


def env_block(
    path: Path,
    x: int,
    y: int,
    after_frame_context: int | None,
    before_frame_context: int | None,
    max_records: int,
    changed_only: bool,
) -> list[str]:
    lines = [
        "MGB64_ARES_TRACE_RDP_PIXEL_PROBE=1",
        f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_PATH={path}",
        f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_X={x}",
        f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_Y={y}",
        f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_MAX_RECORDS={max_records}",
        f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_CHANGED_ONLY={1 if changed_only else 0}",
    ]
    if after_frame_context is not None:
        lines.append(f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_AFTER_FRAME_CONTEXT={after_frame_context}")
    if before_frame_context is not None:
        lines.append(f"MGB64_ARES_TRACE_RDP_PIXEL_PROBE_BEFORE_FRAME_CONTEXT={before_frame_context}")
    return lines


def route_target_xy(region: dict[str, Any] | None, rdp_size: tuple[int, int] | None) -> tuple[int, int] | None:
    if region is None:
        return None
    logical_xy = tuple(region["center"])
    logical_size = region.get("logical_size")
    if (
        rdp_size is None
        or not isinstance(logical_size, list)
        or len(logical_size) != 2
    ):
        return logical_xy  # type: ignore[return-value]
    logical_width, logical_height = (int(value) for value in logical_size)
    if logical_width <= 0 or logical_height <= 0:
        return logical_xy  # type: ignore[return-value]
    return (
        int(logical_xy[0] * rdp_size[0] / logical_width),
        int(logical_xy[1] * rdp_size[1] / logical_height),
    )


def summarize(
    records: list[dict[str, Any]],
    region: dict[str, Any] | None,
    rdp_size: tuple[int, int] | None,
    env_path: Path,
    env_xy: tuple[int, int] | None,
    after_frame_context: int | None,
    before_frame_context: int | None,
    max_records: int,
    changed_only: bool,
    sample_limit: int,
) -> dict[str, Any]:
    warnings: list[str] = []
    total_rows = len(records)
    stats = stats_records(records)
    records = sample_records(records)
    changed = [record for record in records if parse_int(record.get("changed")) != 0]
    probe_pixels = sorted({record_xy(record) for record in records})
    target_xy = env_xy
    if target_xy is None and records:
        target_xy = record_xy(records[0])
    if target_xy is None and region is not None:
        target_xy = route_target_xy(region, rdp_size)
    if target_xy is None:
        target_xy = (0, 0)

    if records and len(probe_pixels) != 1:
        warnings.append(f"probe JSONL contains {len(probe_pixels)} distinct target pixels")
    if records and probe_pixels and probe_pixels[0] != target_xy:
        warnings.append(f"probe records target {probe_pixels[0]}, but requested target is {target_xy}")

    route_summary = None
    if region is not None:
        route_center_xy = route_target_xy(region, rdp_size)
        x0, y0, x1, y1 = region["bounds"]
        if rdp_size is not None and isinstance(region.get("logical_size"), list):
            logical_width, logical_height = (int(value) for value in region["logical_size"])
            x0 = int(x0 * rdp_size[0] / logical_width)
            x1 = int(x1 * rdp_size[0] / logical_width)
            y0 = int(y0 * rdp_size[1] / logical_height)
            y1 = int(y1 * rdp_size[1] / logical_height)
        inside = x0 <= target_xy[0] < x1 and y0 <= target_xy[1] < y1
        if not inside:
            warnings.append(f"target pixel {target_xy} is outside route region {region['name']}")
        route_summary = dict(region)
        route_summary["rdp_size"] = list(rdp_size) if rdp_size is not None else None
        route_summary["rdp_bounds"] = [x0, y0, x1, y1]
        route_summary["rdp_center"] = list(route_center_xy) if route_center_xy is not None else None
        route_summary["target_xy_inside"] = inside

    frames = sorted({parse_int(record.get("frame_context"), -1) for record in records})
    fb_fields = ("fb_addr", "fb_width", "fb_fmt", "fb_size")
    tex_fields = ("texture_image", "texture_width", "texture_fmt", "texture_size")
    raw_fields = ("raw", "hidden")
    fb_counter = Counter(record_key(record, fb_fields) for record in records)
    tex_counter = Counter(record_key(record, tex_fields) for record in records)
    changed_tex_counter = Counter(record_key(record, tex_fields) for record in changed)
    raw_counter = Counter(record_key(record, raw_fields) for record in records)

    if not records:
        warnings.append("no probe records were loaded")
    elif not changed:
        warnings.append("no sampled draw changed the target raw framebuffer/coverage value")
    if len(fb_counter) > 1:
        warnings.append("probe records span multiple color image states")

    return {
        "total_rows": total_rows,
        "records": len(records),
        "stats_records": len(stats),
        "changed_records": len(changed),
        "unchanged_records": len(records) - len(changed),
        "target_xy": list(target_xy),
        "route_region": route_summary,
        "frame_contexts": {
            "first": frames[0] if frames else None,
            "last": frames[-1] if frames else None,
            "count": len(frames),
        },
        "first_record": compact_record(records[0] if records else None),
        "last_record": compact_record(records[-1] if records else None),
        "last_stats": compact_stats(stats[-1] if stats else None),
        "first_changed_records": [compact_record(record) for record in changed[:sample_limit]],
        "last_changed_records": [compact_record(record) for record in changed[-sample_limit:]],
        "framebuffer_states": counter_rows(fb_counter, fb_fields, sample_limit),
        "top_texture_states": counter_rows(tex_counter, tex_fields, sample_limit),
        "top_changed_texture_states": counter_rows(changed_tex_counter, tex_fields, sample_limit),
        "top_raw_values": counter_rows(raw_counter, raw_fields, sample_limit),
        "env": env_block(
            env_path,
            target_xy[0],
            target_xy[1],
            after_frame_context,
            before_frame_context,
            max_records,
            changed_only,
        ),
        "warnings": warnings,
    }


def print_human(summary: dict[str, Any]) -> None:
    print(
        "records: "
        f"{summary['records']} samples, {summary['stats_records']} stats rows "
        f"changed={summary['changed_records']} "
        f"target={summary['target_xy']}"
    )
    frames = summary["frame_contexts"]
    print(f"frame contexts: {frames['first']}..{frames['last']} ({frames['count']} unique)")
    route = summary.get("route_region")
    if route:
        print(
            "route region: "
            f"{route['name']} roi={route['roi']} center={route['center']} "
            f"target_inside={route['target_xy_inside']}"
        )
    if summary["last_record"]:
        last = summary["last_record"]
        print(
            "last sample: "
            f"frame_context={last.get('frame_context')} "
            f"frame_draw={last.get('frame_draw_sequence')} "
            f"texture={last.get('texture_image')} raw={last.get('raw')} "
            f"hidden={last.get('hidden')} rgba={last.get('rgba')}"
        )
        if last.get("combiner") or last.get("depth_flags") or last.get("raster_flags"):
            print(
                "last state: "
                f"other={last.get('other')} combine={last.get('combine')} "
                f"env={last.get('env')} tile={last.get('draw_tile')} "
                f"raster={last.get('raster_flags')} depth={last.get('depth_flags')} "
                f"coverage={last.get('coverage_mode')} z={last.get('z_mode')} "
                f"blend={last.get('blend_cycles')} combiner={last.get('combiner')} "
                f"draw_words={last.get('draw_word_count')}"
            )
    if summary.get("last_stats"):
        stats = summary["last_stats"]
        print(
            "last stats: "
            f"frame_context={stats.get('frame_context')} "
            f"target=({stats.get('x')},{stats.get('y')}) "
            f"fb={stats.get('fb_addr')}/{stats.get('fb_width')}/"
            f"{stats.get('fb_fmt')}/{stats.get('fb_size')} "
            f"commands={stats.get('commands_seen')} draws={stats.get('draw_ops_seen')} "
            f"bbox_ok={stats.get('bbox_ok')} bbox_fail={stats.get('bbox_fail')} "
            f"target_hits={stats.get('target_hits')} samples={stats.get('sample_attempts')} "
            f"read_ok={stats.get('read_ok')} "
            f"read_fail_no_fb={stats.get('read_fail_no_fb')} "
            f"read_fail_x_oob={stats.get('read_fail_x_oob')} "
            f"read_fail_no_maps={stats.get('read_fail_no_maps')} "
            f"read_fail_no_size={stats.get('read_fail_no_size')} "
            f"read_fail_oob={stats.get('read_fail_oob')} "
            f"suppressed_unchanged={stats.get('suppressed_unchanged')}"
        )
    if summary["top_changed_texture_states"]:
        print("top changed texture states:")
        for item in summary["top_changed_texture_states"]:
            print(
                "  "
                f"image={item['texture_image']} fmt/size={item['texture_fmt']}/{item['texture_size']} "
                f"width={item['texture_width']} records={item['records']}"
            )
    if summary["top_raw_values"]:
        print("top raw values:")
        for item in summary["top_raw_values"]:
            print(f"  raw={item['raw']} hidden={item['hidden']} records={item['records']}")
    if summary["warnings"]:
        print("warnings:")
        for warning in summary["warnings"]:
            print(f"  {warning}")
    print("env:")
    for line in summary["env"]:
        print(f"  {line}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path, nargs="?", help="rdp_pixel_probe.jsonl path")
    parser.add_argument("--route", type=Path, help="route JSON for target-region context")
    parser.add_argument("--region", help="visual region name in --route")
    parser.add_argument(
        "--rdp-size",
        type=parse_size,
        help="RDP color-image coordinate size, e.g. 640x240 for stock source=640x240 logs",
    )
    parser.add_argument("--target", type=parse_xy, help="target logical/RDP pixel as X,Y")
    parser.add_argument(
        "--env-path",
        type=Path,
        default=Path("/tmp/mgb64_rdp_pixel_probe.jsonl"),
        help="path to include in the suggested probe environment",
    )
    parser.add_argument("--after-frame-context", type=int, help="suggested first RDP frame context")
    parser.add_argument("--before-frame-context", type=int, help="suggested last RDP frame context")
    parser.add_argument("--max-records", type=int, default=4096, help="suggested probe record cap")
    parser.add_argument("--changed-only", action="store_true", help="suggest changed-only probe output")
    parser.add_argument("--sample-limit", type=int, default=8, help="rows to retain for summaries")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()

    region = route_region(args.route, args.region)
    records = load_records(args.probe) if args.probe is not None else []
    summary = summarize(
        records,
        region,
        args.rdp_size,
        args.env_path,
        args.target,
        args.after_frame_context,
        args.before_frame_context,
        args.max_records,
        args.changed_only,
        max(1, args.sample_limit),
    )
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_human(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
