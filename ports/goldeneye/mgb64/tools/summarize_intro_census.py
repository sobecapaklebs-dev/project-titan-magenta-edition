#!/usr/bin/env python3
"""Summarize native level-intro coverage from JSONL state traces."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


CAMERA_MODES = {
    0: "none",
    1: "intro",
    2: "fadeswirl",
    3: "swirl",
    4: "fp",
    5: "death_sp",
    6: "death_mp",
    7: "posend",
    8: "fp_noinput",
    9: "mp",
    10: "fade_to_title",
}

INTRO_MODES = {1, 2, 3}

DL_COUNTERS = (
    "mtx_fail",
    "vtx_fail",
    "dl_fail",
    "movemem_fail",
    "texture_fail",
    "settimg_fail",
    "non_dl_skip_pc",
    "non_dl_skip_n64",
    "unregistered_skip",
)


@dataclass
class TraceSummary:
    path: Path
    label: str
    records: int = 0
    first_frame: int | None = None
    last_frame: int | None = None
    level: int | None = None
    active_intro_records: int = 0
    first_active_frame: int | None = None
    last_active_frame: int | None = None
    mode_counts: dict[int, int] = field(default_factory=dict)
    anim_index: int | None = None
    swirl_present: int | None = None
    swirl_count: int | None = None
    swirl_hash: str | None = None
    selected_camera_present: int | None = None
    selected_camera_index: int | None = None
    selected_camera_count: int | None = None
    selected_camera_pad: int | None = None
    bond_present: int = 0
    bond_rendered: int = 0
    bond_anim: int = 0
    first_bond_present_frame: int | None = None
    first_bond_rendered_frame: int | None = None
    min_bond_anim_frame: float | None = None
    max_bond_anim_frame: float | None = None
    bond_anim_hash_counts: dict[str, int] = field(default_factory=dict)
    right_item_counts: dict[int, int] = field(default_factory=dict)
    max_bad_cmds: int = 0
    max_crashes: int = 0
    max_nan: int = 0
    fallback_records: int = 0
    max_fallback_total: int = 0
    dl_max: dict[str, int] = field(default_factory=lambda: {name: 0 for name in DL_COUNTERS})


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                skipped += 1
                continue
            if isinstance(record, dict):
                records.append(record)
    if skipped:
        print(f"WARNING: skipped {skipped} corrupted JSONL line(s) in {path}", file=sys.stderr)
    return records


def parse_int(value: Any) -> int | None:
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
            return None
    return None


def parse_float(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(int(value))
    if isinstance(value, (int, float)):
        result = float(value)
        return result if math.isfinite(result) else None
    if isinstance(value, str):
        try:
            result = float(value)
        except ValueError:
            return None
        return result if math.isfinite(result) else None
    return None


def get_path(record: dict[str, Any], *path: str) -> Any:
    value: Any = record
    for part in path:
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def first_present_int(record: dict[str, Any], paths: tuple[tuple[str, ...], ...]) -> int | None:
    for path in paths:
        value = parse_int(get_path(record, *path))
        if value is not None:
            return value
    return None


def mode_counts_text(mode_counts: dict[int, int]) -> str:
    if not mode_counts:
        return "none"
    parts: list[str] = []
    for mode, count in sorted(mode_counts.items()):
        name = CAMERA_MODES.get(mode, str(mode))
        parts.append(f"{name}:{count}")
    return ", ".join(parts)


def int_or_unknown(value: int | None) -> str:
    return "?" if value is None else str(value)


def float_range_text(min_value: float | None, max_value: float | None) -> str:
    if min_value is None or max_value is None:
        return "none"
    return f"{min_value:.2f}..{max_value:.2f}"


def summarize_trace(path: Path, label: str | None = None) -> TraceSummary:
    records = load_jsonl(path)
    summary = TraceSummary(path=path, label=label or path.stem, records=len(records))

    for record in records:
        frame = parse_int(record.get("f"))
        if frame is not None:
            if summary.first_frame is None:
                summary.first_frame = frame
            summary.last_frame = frame

        level = first_present_int(
            record,
            (
                ("front", "active_stage"),
                ("front", "loaded_stage"),
                ("front", "stage"),
            ),
        )
        if level is not None:
            summary.level = level

        cam = parse_int(record.get("cam"))
        if cam is not None:
            summary.mode_counts[cam] = summary.mode_counts.get(cam, 0) + 1
            if cam in INTRO_MODES:
                summary.active_intro_records += 1
                if summary.first_active_frame is None:
                    summary.first_active_frame = frame
                summary.last_active_frame = frame

        setup = get_path(record, "intro", "setup")
        if isinstance(setup, dict):
            anim_index = parse_int(setup.get("anim_index"))
            if anim_index is not None:
                summary.anim_index = anim_index
            swirl = setup.get("swirl")
            if isinstance(swirl, dict):
                present = parse_int(swirl.get("present"))
                count = parse_int(swirl.get("count"))
                hash_value = swirl.get("hash")
                if present is not None:
                    summary.swirl_present = present
                if count is not None:
                    summary.swirl_count = count
                if isinstance(hash_value, str) and hash_value:
                    summary.swirl_hash = hash_value

        selected = get_path(record, "intro", "selected_camera")
        if isinstance(selected, dict):
            present = parse_int(selected.get("present"))
            index = parse_int(selected.get("index"))
            count = parse_int(selected.get("count"))
            pad = parse_int(selected.get("pad"))
            if present is not None:
                summary.selected_camera_present = present
            if index is not None:
                summary.selected_camera_index = index
            if count is not None:
                summary.selected_camera_count = count
            if pad is not None:
                summary.selected_camera_pad = pad

        intro = record.get("intro")
        if isinstance(intro, dict):
            if parse_int(intro.get("bond_present")):
                summary.bond_present += 1
                if summary.first_bond_present_frame is None:
                    summary.first_bond_present_frame = frame
            if parse_int(intro.get("bond_rendered")):
                summary.bond_rendered += 1
                if summary.first_bond_rendered_frame is None:
                    summary.first_bond_rendered_frame = frame
            if parse_int(get_path(record, "intro", "bond_anim", "valid")):
                summary.bond_anim += 1
                anim_hash = get_path(record, "intro", "bond_anim", "hash")
                if isinstance(anim_hash, str) and anim_hash and anim_hash != "0x0000000000000000":
                    summary.bond_anim_hash_counts[anim_hash] = (
                        summary.bond_anim_hash_counts.get(anim_hash, 0) + 1
                    )
                anim_frame = parse_float(get_path(record, "intro", "bond_anim", "frame"))
                if anim_frame is not None:
                    summary.min_bond_anim_frame = (
                        anim_frame
                        if summary.min_bond_anim_frame is None
                        else min(summary.min_bond_anim_frame, anim_frame)
                    )
                    summary.max_bond_anim_frame = (
                        anim_frame
                        if summary.max_bond_anim_frame is None
                        else max(summary.max_bond_anim_frame, anim_frame)
                    )
            right_item = parse_int(get_path(record, "intro", "bond_held", "right", "item"))
            if right_item is not None and right_item >= 0:
                summary.right_item_counts[right_item] = summary.right_item_counts.get(right_item, 0) + 1

        summary.max_bad_cmds = max(summary.max_bad_cmds, parse_int(record.get("bad_cmds")) or 0)
        summary.max_crashes = max(summary.max_crashes, parse_int(record.get("crashes")) or 0)
        summary.max_nan = max(summary.max_nan, parse_int(record.get("nan")) or 0)
        fallback_active = parse_int(get_path(record, "rooms", "fallback", "active")) or 0
        fallback_total = parse_int(get_path(record, "rooms", "fallback", "total")) or 0
        if fallback_active > 0:
            summary.fallback_records += 1
        summary.max_fallback_total = max(summary.max_fallback_total, fallback_total)
        for name in DL_COUNTERS:
            summary.dl_max[name] = max(summary.dl_max[name], parse_int(get_path(record, "dl", name)) or 0)

    return summary


def right_items_text(counts: dict[int, int]) -> str:
    if not counts:
        return "none"
    return ", ".join(f"{item}:{count}" for item, count in sorted(counts.items()))


def anim_hashes_text(counts: dict[str, int]) -> str:
    if not counts:
        return "none"
    return ", ".join(
        f"{hash_value}:{count}" for hash_value, count in sorted(counts.items())
    )


def summary_dict(summary: TraceSummary) -> dict[str, Any]:
    return {
        "path": str(summary.path),
        "label": summary.label,
        "records": summary.records,
        "frames": {
            "first": summary.first_frame,
            "last": summary.last_frame,
        },
        "level": summary.level,
        "active_intro_records": summary.active_intro_records,
        "active_frames": {
            "first": summary.first_active_frame,
            "last": summary.last_active_frame,
        },
        "mode_counts": {
            CAMERA_MODES.get(mode, str(mode)): count
            for mode, count in sorted(summary.mode_counts.items())
        },
        "setup": {
            "anim_index": summary.anim_index,
            "swirl_present": summary.swirl_present,
            "swirl_count": summary.swirl_count,
            "swirl_hash": summary.swirl_hash,
        },
        "selected_camera": {
            "present": summary.selected_camera_present,
            "index": summary.selected_camera_index,
            "count": summary.selected_camera_count,
            "pad": summary.selected_camera_pad,
        },
        "bond": {
            "present": summary.bond_present,
            "rendered": summary.bond_rendered,
            "anim": summary.bond_anim,
            "first_present_frame": summary.first_bond_present_frame,
            "first_rendered_frame": summary.first_bond_rendered_frame,
            "anim_frame": {
                "min": summary.min_bond_anim_frame,
                "max": summary.max_bond_anim_frame,
            },
            "anim_hash_counts": dict(sorted(summary.bond_anim_hash_counts.items())),
            "right_item_counts": {
                str(item): count for item, count in sorted(summary.right_item_counts.items())
            },
        },
        "render": {
            "bad_cmds": summary.max_bad_cmds,
            "crashes": summary.max_crashes,
            "nan": summary.max_nan,
            "fallback_records": summary.fallback_records,
            "fallback_total": summary.max_fallback_total,
            "dl_max": {name: summary.dl_max[name] for name in DL_COUNTERS},
        },
    }


def print_summary(summary: TraceSummary) -> None:
    print(f"intro-census: {summary.label}")
    print(
        "  "
        f"path={summary.path} level={int_or_unknown(summary.level)} "
        f"records={summary.records} frames={int_or_unknown(summary.first_frame)}..{int_or_unknown(summary.last_frame)}"
    )
    print(
        "  "
        f"active_intro={summary.active_intro_records} "
        f"active_frames={int_or_unknown(summary.first_active_frame)}..{int_or_unknown(summary.last_active_frame)} "
        f"modes={mode_counts_text(summary.mode_counts)}"
    )
    print(
        "  "
        f"setup anim_index={int_or_unknown(summary.anim_index)} "
        f"swirl_present={int_or_unknown(summary.swirl_present)} "
        f"swirl_count={int_or_unknown(summary.swirl_count)} "
        f"swirl_hash={summary.swirl_hash or '?'}"
    )
    print(
        "  "
        f"selected_camera present={int_or_unknown(summary.selected_camera_present)} "
        f"index={int_or_unknown(summary.selected_camera_index)} "
        f"count={int_or_unknown(summary.selected_camera_count)} "
        f"pad={int_or_unknown(summary.selected_camera_pad)}"
    )
    print(
        "  "
        f"bond present={summary.bond_present} rendered={summary.bond_rendered} "
        f"anim={summary.bond_anim} anim_frame={float_range_text(summary.min_bond_anim_frame, summary.max_bond_anim_frame)} "
        f"anim_hashes={anim_hashes_text(summary.bond_anim_hash_counts)} "
        f"right_items={right_items_text(summary.right_item_counts)}"
    )
    dl_text = ", ".join(f"{name}:{summary.dl_max[name]}" for name in DL_COUNTERS)
    print(
        "  "
        f"render bad_cmds={summary.max_bad_cmds} crashes={summary.max_crashes} "
        f"nan={summary.max_nan} fallback_records={summary.fallback_records} "
        f"fallback_total={summary.max_fallback_total} dl={dl_text}"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="+", help="JSONL trace(s) to summarize")
    parser.add_argument("--require-active-intro", action="store_true")
    parser.add_argument("--min-active-records", type=int, default=0)
    parser.add_argument("--require-swirl", action="store_true")
    parser.add_argument("--require-selected-camera", action="store_true")
    parser.add_argument("--require-bond-rendered", action="store_true")
    parser.add_argument("--require-bond-anim", action="store_true")
    parser.add_argument("--require-bond-anim-hash", action="store_true")
    parser.add_argument("--json-out", help="write structured summary data as JSON")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    failures: list[str] = []
    summaries: list[TraceSummary] = []

    for trace in args.trace:
        path = Path(trace)
        summary = summarize_trace(path)
        summaries.append(summary)
        print_summary(summary)

        if summary.records == 0:
            failures.append(f"{path}: no JSON records")
        if args.require_active_intro and summary.active_intro_records <= 0:
            failures.append(f"{path}: no active intro camera records")
        if args.min_active_records and summary.active_intro_records < args.min_active_records:
            failures.append(
                f"{path}: active intro records {summary.active_intro_records} < {args.min_active_records}"
            )
        if args.require_swirl and (summary.swirl_present != 1 or not summary.swirl_count):
            failures.append(f"{path}: no decoded swirl setup")
        if args.require_selected_camera and summary.selected_camera_present != 1:
            failures.append(f"{path}: no selected intro camera")
        if args.require_bond_rendered and summary.bond_rendered <= 0:
            failures.append(f"{path}: intro Bond was not rendered")
        if args.require_bond_anim and summary.bond_anim <= 0:
            failures.append(f"{path}: intro Bond animation was not observed")
        if args.require_bond_anim_hash and not summary.bond_anim_hash_counts:
            failures.append(f"{path}: intro Bond animation hash was not observed")

    if failures:
        print("FAIL: intro census summary checks failed")
        for failure in failures:
            print(f"  {failure}")
        if args.json_out:
            Path(args.json_out).write_text(
                json.dumps(
                    {
                        "status": "fail",
                        "trace_count": len(summaries),
                        "failures": failures,
                        "summaries": [summary_dict(summary) for summary in summaries],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
        return 1

    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps(
                {
                    "status": "pass",
                    "trace_count": len(summaries),
                    "failures": [],
                    "summaries": [summary_dict(summary) for summary in summaries],
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    print(f"PASS: summarized {len(args.trace)} intro trace(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
