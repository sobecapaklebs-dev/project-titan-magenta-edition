#!/usr/bin/env python3
"""Join stock center-pixel output with stock/native material evidence.

This is a read-only handoff analyzer for the Dam pad10092 glass work.  It keeps
the stock Parallel-RDP pixel probe as the authoritative post-draw pixel source,
then compares that center-pixel stream with the coarser stock command-stream
region model and the native SETTEX material summary.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
from typing import Any


SETTEX_PIXEL_RE = re.compile(r"\[SETTEX-PIXEL\]\s+(?P<payload>\{.*\})")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            text = line.strip()
            if not text:
                continue
            try:
                records.append(json.loads(text))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSONL row: {exc}") from exc
    return records


def normalize_hex(value: Any, width: int | None = None) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    try:
        number = int(text, 0)
    except ValueError:
        return text
    if width is None:
        return f"0x{number:x}"
    return f"0x{number:0{width}x}"


def luma(rgb: list[Any] | None) -> float | None:
    if not isinstance(rgb, list) or len(rgb) < 3:
        return None
    try:
        r, g, b = (float(rgb[0]), float(rgb[1]), float(rgb[2]))
    except (TypeError, ValueError):
        return None
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def parse_int_or_none(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def point_in_rect(point: list[int], rect: Any) -> bool:
    if not isinstance(rect, list) or len(rect) != 4:
        return False
    try:
        x0, y0, x1, y1 = [float(item) for item in rect]
    except (TypeError, ValueError):
        return False
    return x0 <= point[0] < x1 and y0 <= point[1] < y1


def sample_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("type", "sample") == "sample"]


def stats_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("type") == "stats"]


def compact_sample(record: dict[str, Any] | None) -> dict[str, Any] | None:
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
    out = {key: record[key] for key in keys if key in record}
    out["luma"] = luma(record.get("rgba"))
    return out


def compact_stats(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    keys = (
        "frame_context",
        "command_sequence",
        "draw_sequence",
        "x",
        "y",
        "fb_addr",
        "fb_width",
        "fb_fmt",
        "fb_size",
        "target_hits",
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


def selected_sample_index(samples: list[dict[str, Any]],
                          selected_sample: dict[str, Any] | None) -> int | None:
    if selected_sample is None:
        return None
    for index, record in enumerate(samples):
        if record is selected_sample:
            return index
    return None


def stock_framebuffer_input_summary(samples: list[dict[str, Any]],
                                    selected_sample: dict[str, Any] | None) -> dict[str, Any]:
    index = selected_sample_index(samples, selected_sample)
    prior_candidate = samples[index - 1] if index is not None and index > 0 else None
    prior = None
    reason = "none"
    if prior_candidate is not None and selected_sample is not None:
        if prior_candidate.get("frame_context") == selected_sample.get("frame_context"):
            prior = prior_candidate
            reason = "previous_emitted_same_frame_sample"
        else:
            reason = "previous_emitted_sample_from_different_frame"
    selected_rgba = selected_sample.get("rgba") if selected_sample else None
    prior_rgba = prior.get("rgba") if prior else None
    selected_raw = selected_sample.get("raw") if selected_sample else None
    selected_hidden = selected_sample.get("hidden") if selected_sample else None
    prior_raw = prior.get("raw") if prior else None
    prior_hidden = prior.get("hidden") if prior else None

    hidden_delta = None
    prior_hidden_int = parse_int_or_none(prior_hidden)
    selected_hidden_int = parse_int_or_none(selected_hidden)
    if prior_hidden_int is not None and selected_hidden_int is not None:
        hidden_delta = selected_hidden_int - prior_hidden_int

    return {
        "selected_sample_index": index,
        "framebuffer_input_reason": reason,
        "framebuffer_input_sample": compact_sample(prior),
        "framebuffer_input_vs_selected_rgb": rgb_delta(selected_rgba, prior_rgba),
        "raw_transition": {
            "before": prior_raw,
            "after": selected_raw,
        },
        "hidden_transition": {
            "before": prior_hidden,
            "after": selected_hidden,
            "delta": hidden_delta,
        },
    }


def top_counter(counter: Counter[tuple[Any, ...]], limit: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for key, count in counter.most_common(limit):
        rows.append({"key": list(key), "records": count})
    return rows


def sample_matches_filter(record: dict[str, Any],
                          frame_context: int | None,
                          fb_addr: str | None,
                          texture_image: str | None) -> bool:
    if frame_context is not None and record.get("frame_context") != frame_context:
        return False
    if fb_addr is not None and normalize_hex(record.get("fb_addr")) != normalize_hex(fb_addr):
        return False
    if texture_image is not None and normalize_hex(record.get("texture_image")) != normalize_hex(texture_image):
        return False
    return True


def stock_pixel_summary(
    records: list[dict[str, Any]],
    top: int,
    selected_frame_context: int | None = None,
    selected_fb_addr: str | None = None,
    selected_texture_image: str | None = None,
) -> dict[str, Any]:
    samples = sample_records(records)
    stats = stats_records(records)
    changed = [record for record in samples if record.get("changed", 1)]
    warnings: list[str] = []
    texture_counter: Counter[tuple[Any, ...]] = Counter()
    raw_counter: Counter[tuple[Any, ...]] = Counter()
    fb_counter: Counter[tuple[Any, ...]] = Counter()
    for record in changed:
        texture_counter[
            (
                normalize_hex(record.get("texture_image")),
                record.get("texture_fmt"),
                record.get("texture_size"),
                record.get("texture_width"),
            )
        ] += 1
        raw_counter[(record.get("raw"), record.get("hidden"))] += 1
        fb_counter[(record.get("fb_addr"), record.get("fb_width"), record.get("fb_fmt"), record.get("fb_size"))] += 1

    last_sample = samples[-1] if samples else None
    last_stats = stats[-1] if stats else None
    selection_base = changed if changed else samples
    filters_active = (
        selected_frame_context is not None or
        selected_fb_addr is not None or
        selected_texture_image is not None
    )
    filtered = (
        [
            record for record in selection_base
            if sample_matches_filter(record,
                                     selected_frame_context,
                                     selected_fb_addr,
                                     selected_texture_image)
        ]
        if filters_active
        else selection_base
    )
    selected_sample = filtered[-1] if filtered else (selection_base[-1] if selection_base else None)
    if filters_active and filtered:
        selection_reason = "filtered_last_changed_sample" if changed else "filtered_last_sample"
    else:
        selection_reason = "last_changed_sample" if changed else "last_sample"
        if selection_base and filters_active:
            warnings.append("stock sample filters matched no rows; fell back to default selection")

    target = None
    target_source = selected_sample or last_sample
    if target_source and "x" in target_source and "y" in target_source:
        target = [int(target_source["x"]), int(target_source["y"])]
    elif last_stats and "x" in last_stats and "y" in last_stats:
        target = [int(last_stats["x"]), int(last_stats["y"])]

    selected_frame_rows = []
    selected_frame_changed_rows = []
    selected_frame_fb_counter: Counter[tuple[Any, ...]] = Counter()
    if selected_sample is not None and selected_sample.get("frame_context") is not None:
        selected_frame = selected_sample.get("frame_context")
        selected_frame_rows = [
            record for record in samples
            if record.get("frame_context") == selected_frame
        ]
        selected_frame_changed_rows = [
            record for record in changed
            if record.get("frame_context") == selected_frame
        ]
        selected_frame_fb_counter = Counter(
            (
                normalize_hex(record.get("fb_addr")),
                record.get("fb_width"),
                record.get("fb_fmt"),
                record.get("fb_size"),
            )
            for record in selected_frame_rows
        )

    framebuffer_input = stock_framebuffer_input_summary(samples, selected_sample)

    return {
        "total_rows": len(records),
        "sample_rows": len(samples),
        "stats_rows": len(stats),
        "changed_sample_rows": len(changed),
        "target": target,
        "last_sample": compact_sample(last_sample),
        "last_stats": compact_stats(last_stats),
        "selected_sample": compact_sample(selected_sample),
        "selected_sample_index": framebuffer_input["selected_sample_index"],
        "selected_framebuffer_input_reason": framebuffer_input["framebuffer_input_reason"],
        "selected_framebuffer_input": framebuffer_input["framebuffer_input_sample"],
        "selected_framebuffer_input_vs_selected_rgb": framebuffer_input["framebuffer_input_vs_selected_rgb"],
        "selected_raw_transition": framebuffer_input["raw_transition"],
        "selected_hidden_transition": framebuffer_input["hidden_transition"],
        "selection_reason": selection_reason,
        "selection_filters": {
            "frame_context": selected_frame_context,
            "fb_addr": normalize_hex(selected_fb_addr),
            "texture_image": normalize_hex(selected_texture_image),
        },
        "selected_frame_sample_rows": len(selected_frame_rows),
        "selected_frame_changed_rows": len(selected_frame_changed_rows),
        "selected_frame_framebuffer_states": top_counter(selected_frame_fb_counter, top),
        "top_changed_texture_states": top_counter(texture_counter, top),
        "top_raw_values": top_counter(raw_counter, top),
        "top_framebuffer_states": top_counter(fb_counter, top),
        "warnings": warnings,
    }


def command_region_summary(path: Path, region: str, target: list[int] | None,
                           final_texture: str | None) -> dict[str, Any]:
    data = load_json(path)
    region_payload = ((data.get("draw_ops") or {}).get("regions") or {}).get(region)
    if not isinstance(region_payload, dict):
        return {"path": str(path), "region": region, "missing": True}

    last_hits = region_payload.get("last_hits") or []
    covering = [
        hit for hit in last_hits
        if target is not None and point_in_rect(target, hit.get("coverage_bbox") or hit.get("bbox"))
    ]
    matching_texture = [
        hit for hit in last_hits
        if final_texture is not None and normalize_hex(hit.get("image")) == final_texture
    ]
    later_covering_other = []
    final_sequence = None
    if final_texture is not None:
        final_sequences = [
            hit.get("sequence") for hit in matching_texture
            if isinstance(hit.get("sequence"), int)
        ]
        if final_sequences:
            final_sequence = max(final_sequences)
    if final_sequence is not None:
        for hit in covering:
            sequence = hit.get("sequence")
            if isinstance(sequence, int) and sequence > final_sequence:
                if normalize_hex(hit.get("image")) != final_texture:
                    later_covering_other.append(hit)

    def compact_hit(hit: dict[str, Any]) -> dict[str, Any]:
        return {
            "sequence": hit.get("sequence"),
            "frame": hit.get("frame"),
            "image": normalize_hex(hit.get("image")),
            "image_kseg0": normalize_hex(hit.get("image_kseg0")),
            "fmt": hit.get("fmt"),
            "siz": hit.get("siz"),
            "tile": hit.get("tile"),
            "combine": hit.get("combine"),
            "other": hit.get("other"),
            "env": hit.get("env"),
            "pixel_count": hit.get("pixel_count"),
            "bbox": hit.get("bbox"),
            "coverage_bbox": hit.get("coverage_bbox"),
        }

    return {
        "path": str(path),
        "region": region,
        "coverage_model": region_payload.get("coverage_model"),
        "region_area": region_payload.get("region_area"),
        "final_owner_pixels": region_payload.get("final_owner_pixels"),
        "final_owner_pct": region_payload.get("final_owner_pct"),
        "final_owner_states": region_payload.get("final_owner_states"),
        "ordered_hit_count": region_payload.get("ordered_hit_count"),
        "last_hits_covering_target": [compact_hit(hit) for hit in covering],
        "last_hits_matching_final_texture": [compact_hit(hit) for hit in matching_texture],
        "later_covering_hits_with_different_texture": [
            compact_hit(hit) for hit in later_covering_other
        ],
    }


def native_settex_summary(path: Path, region: str) -> dict[str, Any]:
    data = load_json(path)
    region_payload = (data.get("regions") or {}).get(region)
    if not isinstance(region_payload, dict):
        return {"path": str(path), "region": region, "missing": True}
    sample = region_payload.get("sample_summary") or {}
    return {
        "path": str(path),
        "region": region,
        "status": data.get("status"),
        "frame_selection": data.get("frame_selection"),
        "filters": data.get("filters"),
        "matched_rows": region_payload.get("matched_rows"),
        "coverage_pixels": region_payload.get("coverage_pixels"),
        "coverage_pct": region_payload.get("coverage_pct"),
        "signature_counts": region_payload.get("signature_counts"),
        "shaderL_frag": sample.get("shaderL_frag"),
        "shaderL_comb": sample.get("shaderL_comb"),
        "t0l": sample.get("t0l"),
        "t1l": sample.get("t1l"),
        "fogc": sample.get("fogc"),
        "examples": region_payload.get("examples", [])[:2],
    }


def load_native_pixel_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            match = SETTEX_PIXEL_RE.search(line)
            if not match:
                continue
            try:
                row = json.loads(match.group("payload"))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid SETTEX-PIXEL JSON: {exc}") from exc
            row["line"] = line_no
            rows.append(row)
    return rows


def rgb_delta(a: list[Any] | None, b: list[Any] | None) -> dict[str, Any] | None:
    if not isinstance(a, list) or not isinstance(b, list) or len(a) < 3 or len(b) < 3:
        return None
    try:
        delta = [int(a[index]) - int(b[index]) for index in range(3)]
    except (TypeError, ValueError):
        return None
    return {
        "delta": delta,
        "mean_abs_rgb": sum(abs(item) for item in delta) / 3.0,
        "luma_delta": luma([a[0], a[1], a[2]]) - luma([b[0], b[1], b[2]]),
    }


def parse_xy(text: str) -> list[int]:
    parts = text.split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected X,Y")
    try:
        return [int(parts[0], 0), int(parts[1], 0)]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected integer X,Y") from exc


def target_tuple(value: Any) -> tuple[int, int] | None:
    if not isinstance(value, list) or len(value) != 2:
        return None
    try:
        return (int(value[0]), int(value[1]))
    except (TypeError, ValueError):
        return None


def compact_native_pixel_row(row: dict[str, Any] | None) -> dict[str, Any] | None:
    if row is None:
        return None
    keys = (
        "line",
        "status",
        "frame",
        "tri",
        "serial",
        "target",
        "fb",
        "gl",
        "inside",
        "bary",
        "drawclass",
        "dl_room",
        "dl",
        "cmd",
        "cc",
        "effcc",
        "opts",
        "raw",
        "effmode",
        "omh",
        "geom",
        "depth",
        "mode",
        "blend",
        "api_blend",
        "rect",
        "texnum",
        "wh",
        "screen_bbox",
        "src_valid",
        "sample_valid",
        "lod",
        "uv0",
        "xy0",
        "t0n",
        "t0l",
        "t0p",
        "uv1",
        "xy1",
        "t1n",
        "t1l",
        "t1p",
        "shade",
        "fog",
        "shaderN_comb",
        "shaderN_frag",
        "shaderL_comb",
        "shaderL_frag",
        "shaderP_comb",
        "shaderP_frag",
        "pre",
        "post",
        "pred_alpha",
        "post_delta_alpha",
        "pred_rdp",
        "post_delta_rdp",
        "delta",
        "changed",
    )
    return {key: row[key] for key in keys if key in row}


def native_pixel_summary(
    path: Path,
    stock_target: list[int] | None,
    native_target: list[int] | None,
    native_frame: int | None,
    stock_rgb: list[Any] | None,
) -> dict[str, Any]:
    rows = load_native_pixel_rows(path)
    target_counts: Counter[tuple[int, int]] = Counter()
    frame_counts: Counter[int] = Counter()
    for row in rows:
        key = target_tuple(row.get("target"))
        if key is not None:
            target_counts[key] += 1
        try:
            frame_counts[int(row.get("frame"))] += 1
        except (TypeError, ValueError):
            pass

    requested_key = target_tuple(native_target)
    stock_key = target_tuple(stock_target)
    selected_key: tuple[int, int] | None = None
    selection_reason = "none"
    if requested_key is not None:
        selected_key = requested_key
        selection_reason = "explicit_native_target"
    elif stock_key is not None and target_counts.get(stock_key, 0) > 0:
        selected_key = stock_key
        selection_reason = "stock_target_match"
    elif len(target_counts) == 1:
        selected_key = next(iter(target_counts))
        selection_reason = "single_native_target"
    elif rows:
        selection_reason = "all_native_targets"

    target_rows = [
        row for row in rows
        if selected_key is None or target_tuple(row.get("target")) == selected_key
    ]
    if native_frame is not None:
        frame_rows = [
            row for row in target_rows
            if row.get("frame") == native_frame
        ]
    else:
        frame_rows = target_rows
    ok_rows = [row for row in frame_rows if row.get("status") == "ok"]
    inside_rows = [row for row in ok_rows if row.get("inside")]
    changed_rows = [row for row in inside_rows if row.get("changed")]
    last_ok = ok_rows[-1] if ok_rows else None
    last_inside = inside_rows[-1] if inside_rows else None
    last_changed = changed_rows[-1] if changed_rows else None
    final_row = last_changed or last_inside or last_ok
    stock_rgb3 = stock_rgb[:3] if isinstance(stock_rgb, list) and len(stock_rgb) >= 3 else None
    final_post = final_row.get("post") if final_row else None

    return {
        "path": str(path),
        "total_rows": len(rows),
        "stock_target": stock_target,
        "requested_native_target": native_target,
        "selected_native_target": list(selected_key) if selected_key is not None else None,
        "selection_reason": selection_reason,
        "selected_native_frame": native_frame,
        "native_target_counts": [
            {"target": list(key), "records": count}
            for key, count in target_counts.most_common()
        ],
        "native_frame_counts": [
            {"frame": frame, "records": count}
            for frame, count in frame_counts.most_common()
        ],
        "target_rows": len(target_rows),
        "selected_frame_rows": len(frame_rows),
        "status_counts": dict(Counter(str(row.get("status")) for row in frame_rows)),
        "ok_rows": len(ok_rows),
        "inside_rows": len(inside_rows),
        "changed_inside_rows": len(changed_rows),
        "last_ok": compact_native_pixel_row(last_ok),
        "last_inside": compact_native_pixel_row(last_inside),
        "last_changed": compact_native_pixel_row(last_changed),
        "selected_final": compact_native_pixel_row(final_row),
        "selected_post_vs_stock_rgb": rgb_delta(final_post, stock_rgb3),
        "examples": [compact_native_pixel_row(row) for row in frame_rows[:6]],
    }


def build_interpretation(payload: dict[str, Any]) -> tuple[list[str], list[str]]:
    notes: list[str] = []
    warnings: list[str] = []
    stock = payload["stock_pixel"]
    selected_sample = stock.get("selected_sample") or {}
    last_sample = stock.get("last_sample") or {}
    last_stats = stock.get("last_stats") or {}
    final_texture = normalize_hex(selected_sample.get("texture_image"))
    warnings.extend(stock.get("warnings") or [])
    notes.append(
        "stock pixel probe recorded "
        f"{stock.get('read_ok', last_stats.get('read_ok')) or last_stats.get('read_ok')} "
        f"successful readbacks and {stock['changed_sample_rows']} changed samples"
    )
    notes.append(
        "authoritative selected stock pixel sample: "
        f"selection={stock.get('selection_reason')} texture={final_texture} "
        f"raw={selected_sample.get('raw')} hidden={selected_sample.get('hidden')} "
        f"rgba={selected_sample.get('rgba')}"
    )
    framebuffer_input = stock.get("selected_framebuffer_input") or {}
    framebuffer_delta = stock.get("selected_framebuffer_input_vs_selected_rgb") or {}
    if framebuffer_input:
        notes.append(
            "stock framebuffer input candidate before selected sample: "
            f"reason={stock.get('selected_framebuffer_input_reason')} "
            f"texture={normalize_hex(framebuffer_input.get('texture_image'))} "
            f"raw={framebuffer_input.get('raw')} hidden={framebuffer_input.get('hidden')} "
            f"rgba={framebuffer_input.get('rgba')} "
            f"delta_to_selected={framebuffer_delta.get('delta')}"
        )
    if last_sample and selected_sample and last_sample != selected_sample:
        notes.append(
            "last emitted stock sample differs from the selected authority: "
            f"last_texture={normalize_hex(last_sample.get('texture_image'))} "
            f"last_rgba={last_sample.get('rgba')}"
        )

    command = payload.get("stock_command_region") or {}
    if command and not command.get("missing"):
        later = command.get("later_covering_hits_with_different_texture") or []
        if later:
            warnings.append(
                "command-stream span/bbox model has later different-texture hits "
                "covering the target; use the pixel probe as final-pixel authority"
            )
        matching = command.get("last_hits_matching_final_texture") or []
        if matching:
            seqs = [hit.get("sequence") for hit in matching if hit.get("sequence") is not None]
            notes.append(
                f"command-stream region contains {len(matching)} final-texture hits "
                f"for {final_texture} (sequences {seqs[:6]})"
            )

    native = payload.get("native_settex") or {}
    shader = native.get("shaderL_frag") or {}
    if shader:
        notes.append(
            "native texnum 654 fragment source over the same ROI is not final "
            f"framebuffer output: shaderL_frag luma={shader.get('luma')} "
            f"alpha_counts={shader.get('alpha_counts')}"
        )
        notes.append(
            "next implementation target is exact translucent composition from "
            "native fragment source plus framebuffer/coverage, checked against "
            "the stock post-draw pixel probe"
        )
    native_pixel = payload.get("native_pixel") or {}
    if native_pixel:
        final_row = native_pixel.get("selected_final") or {}
        post_delta = native_pixel.get("selected_post_vs_stock_rgb")
        stock_target = native_pixel.get("stock_target")
        selected_target = native_pixel.get("selected_native_target")
        if selected_target and stock_target and selected_target != stock_target:
            notes.append(
                "native settex pixel probe used a mapped logical target "
                f"{selected_target} for stock/aligned target {stock_target}"
            )
        notes.append(
            "native settex pixel probe recorded "
            f"{native_pixel.get('inside_rows')} target-covering ok rows "
            f"on native frame {native_pixel.get('selected_native_frame')} and "
            f"{native_pixel.get('changed_inside_rows')} changed rows"
        )
        if final_row:
            notes.append(
                "selected native target pixel row: "
                f"frame={final_row.get('frame')} tri={final_row.get('tri')} "
                f"pre={final_row.get('pre')} post={final_row.get('post')} "
                f"changed={final_row.get('changed')}"
            )
            if final_row.get("pred_alpha") or final_row.get("pred_rdp"):
                notes.append(
                    "selected native post versus reconstructed source predictions: "
                    f"alpha={final_row.get('pred_alpha')} "
                    f"delta_alpha={final_row.get('post_delta_alpha')} "
                    f"rdp={final_row.get('pred_rdp')} "
                    f"delta_rdp={final_row.get('post_delta_rdp')}"
                )
        if post_delta:
            notes.append(
                "selected native post pixel vs stock final RGB: "
                f"delta={post_delta.get('delta')} "
                f"mean_abs_rgb={post_delta.get('mean_abs_rgb'):.3f}"
            )
    return notes, warnings


def summarize(args: argparse.Namespace) -> dict[str, Any]:
    records = load_jsonl(args.stock_probe)
    stock = stock_pixel_summary(
        records,
        args.top,
        getattr(args, "stock_frame_context", None),
        getattr(args, "stock_fb_addr", None),
        getattr(args, "stock_texture_image", None),
    )
    selected_sample = stock.get("selected_sample") or {}
    final_texture = normalize_hex(selected_sample.get("texture_image"))
    target = stock.get("target")

    payload: dict[str, Any] = {
        "status": "pass",
        "stock_pixel": stock,
    }
    if args.stock_command_summary:
        payload["stock_command_region"] = command_region_summary(
            args.stock_command_summary,
            args.region,
            target,
            final_texture,
        )
    if args.native_settex_summary:
        payload["native_settex"] = native_settex_summary(args.native_settex_summary, args.region)
    if args.native_pixel_log:
        selected_native_frame = getattr(args, "native_pixel_frame", None)
        if selected_native_frame is None:
            native_settex = payload.get("native_settex") or {}
            frame_selection = native_settex.get("frame_selection") or {}
            try:
                selected_native_frame = int(frame_selection.get("selected"))
            except (TypeError, ValueError):
                selected_native_frame = None
        payload["native_pixel"] = native_pixel_summary(
            args.native_pixel_log,
            target,
            getattr(args, "native_pixel_target", None),
            selected_native_frame,
            selected_sample.get("rgba"),
        )
    interpretation, warnings = build_interpretation(payload)
    payload["interpretation"] = interpretation
    payload["warnings"] = warnings
    return payload


def print_human(payload: dict[str, Any]) -> None:
    stock = payload["stock_pixel"]
    selected_sample = stock.get("selected_sample") or {}
    last_sample = stock.get("last_sample") or {}
    last_stats = stock.get("last_stats") or {}
    print(
        f"stock pixel: samples={stock['sample_rows']} changed={stock['changed_sample_rows']} "
        f"stats={stock['stats_rows']} target={stock.get('target')}"
    )
    print(
        "selected sample: "
        f"selection={stock.get('selection_reason')} "
        f"index={stock.get('selected_sample_index')} "
        f"frame_context={selected_sample.get('frame_context')} "
        f"frame_draw={selected_sample.get('frame_draw_sequence')} "
        f"fb={selected_sample.get('fb_addr')} "
        f"texture={selected_sample.get('texture_image')} raw={selected_sample.get('raw')} "
        f"hidden={selected_sample.get('hidden')} rgba={selected_sample.get('rgba')}"
    )
    if selected_sample.get("combiner") or selected_sample.get("depth_flags") or selected_sample.get("raster_flags"):
        print(
            "selected stock state: "
            f"other={selected_sample.get('other')} "
            f"combine={selected_sample.get('combine')} "
            f"env={selected_sample.get('env')} "
            f"tile={selected_sample.get('draw_tile')} "
            f"raster={selected_sample.get('raster_flags')} "
            f"depth={selected_sample.get('depth_flags')} "
            f"coverage={selected_sample.get('coverage_mode')} "
            f"z={selected_sample.get('z_mode')} "
            f"blend={selected_sample.get('blend_cycles')} "
            f"combiner={selected_sample.get('combiner')} "
            f"draw_words={selected_sample.get('draw_word_count')}"
        )
    framebuffer_input = stock.get("selected_framebuffer_input") or {}
    if framebuffer_input:
        framebuffer_delta = stock.get("selected_framebuffer_input_vs_selected_rgb") or {}
        hidden_transition = stock.get("selected_hidden_transition") or {}
        print(
            "selected framebuffer input: "
            f"reason={stock.get('selected_framebuffer_input_reason')} "
            f"frame_context={framebuffer_input.get('frame_context')} "
            f"frame_draw={framebuffer_input.get('frame_draw_sequence')} "
            f"texture={framebuffer_input.get('texture_image')} "
            f"raw={framebuffer_input.get('raw')} hidden={framebuffer_input.get('hidden')} "
            f"rgba={framebuffer_input.get('rgba')} "
            f"delta_to_selected={framebuffer_delta.get('delta')} "
            f"hidden_delta={hidden_transition.get('delta')}"
        )
    print(
        "last sample: "
        f"frame_context={last_sample.get('frame_context')} "
        f"frame_draw={last_sample.get('frame_draw_sequence')} "
        f"texture={last_sample.get('texture_image')} raw={last_sample.get('raw')} "
        f"hidden={last_sample.get('hidden')} rgba={last_sample.get('rgba')}"
    )
    print(
        "last stats: "
        f"read_ok={last_stats.get('read_ok')} "
        f"read_fail_no_maps={last_stats.get('read_fail_no_maps')} "
        f"suppressed_unchanged={last_stats.get('suppressed_unchanged')} "
        f"fb={last_stats.get('fb_addr')}/{last_stats.get('fb_width')}/"
        f"{last_stats.get('fb_fmt')}/{last_stats.get('fb_size')}"
    )
    command = payload.get("stock_command_region") or {}
    if command and not command.get("missing"):
        print(
            "command region: "
            f"final_owner_pixels={command.get('final_owner_pixels')} "
            f"states={command.get('final_owner_states')} "
            f"matching_final_texture_hits={len(command.get('last_hits_matching_final_texture') or [])} "
            f"later_other_covering_hits={len(command.get('later_covering_hits_with_different_texture') or [])}"
        )
    native = payload.get("native_settex") or {}
    if native and not native.get("missing"):
        shader = native.get("shaderL_frag") or {}
        print(
            "native settex: "
            f"matched_rows={native.get('matched_rows')} "
            f"coverage={native.get('coverage_pct')} "
            f"shaderL_frag_luma={(shader.get('luma') or {})} "
            f"alpha_counts={shader.get('alpha_counts')}"
        )
    native_pixel = payload.get("native_pixel") or {}
    if native_pixel:
        final_row = native_pixel.get("selected_final") or {}
        post_delta = native_pixel.get("selected_post_vs_stock_rgb") or {}
        print(
            "native pixel: "
            f"stock_target={native_pixel.get('stock_target')} "
            f"native_target={native_pixel.get('selected_native_target')} "
            f"frame={native_pixel.get('selected_native_frame')} "
            f"selection={native_pixel.get('selection_reason')} "
            f"rows={native_pixel.get('selected_frame_rows')}/"
            f"{native_pixel.get('target_rows')} "
            f"inside={native_pixel.get('inside_rows')} "
            f"changed={native_pixel.get('changed_inside_rows')} "
            f"selected_post={final_row.get('post')} "
            f"pred_alpha={final_row.get('pred_alpha')} "
            f"pred_rdp={final_row.get('pred_rdp')} "
            f"stock_delta={post_delta.get('delta')}"
        )
    if payload.get("warnings"):
        print("warnings:")
        for warning in payload["warnings"]:
            print(f"  {warning}")
    print("interpretation:")
    for item in payload.get("interpretation") or []:
        print(f"  {item}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock-probe", type=Path, required=True)
    parser.add_argument("--stock-command-summary", type=Path)
    parser.add_argument("--native-settex-summary", type=Path)
    parser.add_argument("--native-pixel-log", type=Path)
    parser.add_argument(
        "--native-pixel-target",
        type=parse_xy,
        help="Native logical X,Y target for SETTEX-PIXEL rows when it differs from stock/aligned output.",
    )
    parser.add_argument(
        "--native-pixel-frame",
        type=int,
        help="Native frame to select from SETTEX-PIXEL rows; defaults to native settex frame_selection.selected.",
    )
    parser.add_argument(
        "--stock-frame-context",
        type=int,
        help="Restrict authoritative stock sample selection to this frame_context.",
    )
    parser.add_argument(
        "--stock-fb-addr",
        help="Restrict authoritative stock sample selection to this color-image/framebuffer address.",
    )
    parser.add_argument(
        "--stock-texture-image",
        help="Restrict authoritative stock sample selection to this texture image address.",
    )
    parser.add_argument("--region", default="projected_impact")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    payload = summarize(args)
    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print_human(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
