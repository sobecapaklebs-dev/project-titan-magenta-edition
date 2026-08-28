#!/usr/bin/env python3
"""Analyze stock RDP texture-image candidates against a native texture dump.

This is a diagnostic, not a renderer oracle. It validates that the ares stock
trace captured every G_SETTIMG payload for the selected rooms/frame, checks for
exact source-byte matches against a native dumped texture, and then runs a small
format-aware sanity score over decodable candidates. Low confidence here should
push work toward stock draw/pixel semantics, not toward guessing a texture map.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - preview output is optional.
    Image = None  # type: ignore[assignment]
    ImageDraw = None  # type: ignore[assignment]


FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3
HASH_WINDOWS = (64, 512, 2916, 3024, 3841, 4112)
ROOM_DL_BASE_SIDES = ("primary", "secondary")


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise TypeError(f"cannot parse integer from {value!r}")


def fnv64(data: bytes) -> str:
    value = FNV64_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"0x{value:016X}"


def prefix_hashes(data: bytes) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for window in HASH_WINDOWS:
        chunk = data[: min(window, len(data))]
        out[str(window)] = {"bytes": len(chunk), "hash": fnv64(chunk)}
    return out


def load_frame(path: Path, frame: int) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            record = json.loads(line)
            if record.get("f") == frame:
                return record
    raise ValueError(f"frame {frame} not found in {path}")


def rdp_dump_path(dump_dir: Path, frame: int, sample: dict[str, Any]) -> Path:
    addr = parse_int(sample["addr"])
    image = parse_int(sample["image"])
    index = int(sample["index"])
    return dump_dir / f"rdp_tex_f{frame:05d}_dl{addr:08x}_img{image:08x}_idx{index:04d}.bin"


def settilesize_dim(sample: dict[str, Any]) -> dict[str, int] | None:
    if sample.get("op") != "settilesize":
        return None
    uls = int(sample.get("uls", 0))
    ult = int(sample.get("ult", 0))
    lrs = int(sample.get("lrs", 0))
    lrt = int(sample.get("lrt", 0))
    if lrs < uls or lrt < ult:
        return None
    # RDP texture coordinates are 10.2 fixed-point values in these commands.
    width = (lrs - uls) // 4 + 1
    height = (lrt - ult) // 4 + 1
    if width <= 0 or height <= 0:
        return None
    return {"width": width, "height": height, "tile": int(sample.get("tile", 0))}


def collect_groups(record: dict[str, Any], rooms: set[int], dump_dir: Path, sides: tuple[str, ...]) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    frame = int(record["f"])
    for room in record.get("rooms", {}).get("dl", []):
        room_id = int(room.get("room", -1))
        if room_id not in rooms:
            continue
        for side in sides:
            side_record = room.get(side, {}) or {}
            samples = list(side_record.get("rdp_tex_sample", []))
            draw_states = list(side_record.get("draw_state", []))
            starts = [index for index, sample in enumerate(samples) if sample.get("op") == "settimg"]
            for group_index, start in enumerate(starts):
                end = starts[group_index + 1] if group_index + 1 < len(starts) else len(samples)
                group_samples = samples[start:end]
                sample = group_samples[0]
                texture_serial = group_index + 1
                dims = [dim for dim in (settilesize_dim(item) for item in group_samples) if dim]
                main_dim = max(dims, key=lambda item: item["width"] * item["height"]) if dims else None
                dump_path = rdp_dump_path(dump_dir, frame, sample)
                raw = dump_path.read_bytes() if dump_path.exists() else b""
                matching_draw_states = [
                    state
                    for state in draw_states
                    if int(state.get("texture_serial", -1)) == texture_serial
                    and state.get("image") == sample.get("image")
                ]
                draw_triangles = sum(int(state.get("triangles", 0)) for state in matching_draw_states)
                draw_tri1_cmds = sum(int(state.get("tri1_cmds", 0)) for state in matching_draw_states)
                draw_tri2_cmds = sum(int(state.get("tri2_cmds", 0)) for state in matching_draw_states)
                groups.append(
                    {
                        "room": room_id,
                        "side": side,
                        "texture_serial": texture_serial,
                        "index": int(sample["index"]),
                        "addr": sample["addr"],
                        "image": sample["image"],
                        "fmt": int(sample.get("fmt", -1)),
                        "siz": int(sample.get("siz", -1)),
                        "env": sample.get("env"),
                        "combine": sample.get("combine", []),
                        "dump_path": str(dump_path),
                        "dump_exists": dump_path.exists(),
                        "dump_bytes": len(raw),
                        "trace_dump_bytes": int(sample.get("dump_bytes", 0)),
                        "dims": dims,
                        "main_dim": main_dim,
                        "image_hashes": sample.get("image_hashes", {}),
                        "image_hash_bytes": sample.get("image_hash_bytes", {}),
                        "image_first_words": sample.get("image_first_words", []),
                        "draw_triangles": draw_triangles,
                        "draw_tri1_cmds": draw_tri1_cmds,
                        "draw_tri2_cmds": draw_tri2_cmds,
                        "draw_state_matches": matching_draw_states,
                        "byte_top": [[byte, count] for byte, count in Counter(raw[: min(4096, len(raw))]).most_common(8)],
                        "_raw": raw,
                    }
                )
    return groups


def orient(values: list[int], width: int, height: int, mode: str) -> tuple[list[int], int, int]:
    if mode == "id":
        return values, width, height
    if mode == "flipx":
        return [values[y * width + (width - 1 - x)] for y in range(height) for x in range(width)], width, height
    if mode == "flipy":
        return [values[(height - 1 - y) * width + x] for y in range(height) for x in range(width)], width, height
    if mode == "rot180":
        return [
            values[(height - 1 - y) * width + (width - 1 - x)]
            for y in range(height)
            for x in range(width)
        ], width, height
    if mode == "transpose":
        return [values[y * width + x] for x in range(width) for y in range(height)], height, width
    if mode == "rot90":
        return [values[(height - 1 - y) * width + x] for x in range(width) for y in range(height)], height, width
    if mode == "rot270":
        return [values[y * width + (width - 1 - x)] for x in range(width) for y in range(height)], height, width
    raise ValueError(f"unknown orientation {mode}")


def resize_nearest(values: list[int], width: int, height: int, target_width: int, target_height: int) -> list[int]:
    out: list[int] = []
    for y in range(target_height):
        source_y = min(height - 1, int((y + 0.5) * height / target_height))
        for x in range(target_width):
            source_x = min(width - 1, int((x + 0.5) * width / target_width))
            out.append(values[source_y * width + source_x])
    return out


def mean_abs_delta(a: list[int], b: bytes) -> float:
    return sum(abs(left - right) for left, right in zip(a, b)) / len(a) if a else 0.0


def correlation(a: list[int], b: bytes) -> float:
    if not a:
        return 0.0
    mean_a = sum(a) / len(a)
    mean_b = sum(b) / len(b)
    var_a = sum((value - mean_a) ** 2 for value in a)
    var_b = sum((value - mean_b) ** 2 for value in b)
    if var_a == 0.0 or var_b == 0.0:
        return 0.0
    cov = sum((left - mean_a) * (right - mean_b) for left, right in zip(a, b))
    return cov / math.sqrt(var_a * var_b)


def decode_ia8(raw: bytes, width: int, height: int) -> list[int] | None:
    needed = width * height
    if len(raw) < needed:
        return None
    return list(raw[:needed])


def decode_ia16_to_ia8(raw: bytes, width: int, height: int) -> list[int] | None:
    needed = width * height * 2
    if len(raw) < needed:
        return None
    return [((raw[index] >> 4) << 4) | (raw[index + 1] >> 4) for index in range(0, needed, 2)]


def decode_i_to_ia8(raw: bytes, width: int, height: int) -> list[int] | None:
    needed = width * height
    if len(raw) < needed:
        return None
    return [((raw[index] >> 4) << 4) | 0x0F for index in range(needed)]


def decoders_for(group: dict[str, Any]) -> list[tuple[str, list[int] | None]]:
    dim = group.get("main_dim")
    if not dim:
        return []
    width = int(dim["width"])
    height = int(dim["height"])
    raw = group["_raw"]
    fmt = int(group["fmt"])
    siz = int(group["siz"])
    if fmt == 3 and siz == 2:
        return [
            ("ia16_to_ia8", decode_ia16_to_ia8(raw, width, height)),
            ("raw_ia8", decode_ia8(raw, width, height)),
        ]
    if fmt == 3 and siz == 1:
        return [("raw_ia8", decode_ia8(raw, width, height))]
    if fmt == 4:
        return [("i_to_ia8", decode_i_to_ia8(raw, width, height))]
    if fmt == 2:
        return [("ci_index_placeholder", decode_i_to_ia8(raw, width, height))]
    return []


def score_group(group: dict[str, Any], native_source: bytes, native_width: int, native_height: int) -> dict[str, Any] | None:
    dim = group.get("main_dim")
    if not dim:
        return None
    width = int(dim["width"])
    height = int(dim["height"])
    best: tuple[float, float, str, str, int, int, list[int]] | None = None
    for kind, decoded in decoders_for(group):
        if decoded is None:
            continue
        for mode in ("id", "flipx", "flipy", "rot180", "transpose", "rot90", "rot270"):
            oriented, oriented_width, oriented_height = orient(decoded, width, height, mode)
            resized = resize_nearest(oriented, oriented_width, oriented_height, native_width, native_height)
            delta = mean_abs_delta(resized, native_source)
            corr = correlation(resized, native_source)
            candidate = (delta, corr, kind, mode, oriented_width, oriented_height, resized)
            if best is None or (delta, -corr) < (best[0], -best[1]):
                best = candidate
    if best is None:
        return None
    alpha_top = Counter(value & 0x0F for value in best[6]).most_common(6)
    intensity_top = Counter(value >> 4 for value in best[6]).most_common(6)
    return {
        "mean_abs_delta": best[0],
        "correlation": best[1],
        "decoder": best[2],
        "orientation": best[3],
        "oriented_width": best[4],
        "oriented_height": best[5],
        "alpha_nibble_top": [[value, count] for value, count in alpha_top],
        "intensity_nibble_top": [[value, count] for value, count in intensity_top],
        "_resized": best[6],
    }


def exact_matches(
    group: dict[str, Any],
    native_hashes: dict[str, dict[str, Any]],
    native_chain_hashes: dict[str, dict[str, Any]],
) -> dict[str, list[str]]:
    matches = {"native_source": [], "native_chain": []}
    hashes = group.get("image_hashes", {})
    bytes_by_window = group.get("image_hash_bytes", {})
    for window, entry in native_hashes.items():
        if str(hashes.get(window, "")).upper() == str(entry["hash"]).upper():
            if int(bytes_by_window.get(window, 0)) == int(entry["bytes"]):
                matches["native_source"].append(window)
    for window, entry in native_chain_hashes.items():
        if str(hashes.get(window, "")).upper() == str(entry["hash"]).upper():
            if int(bytes_by_window.get(window, 0)) == int(entry["bytes"]):
                matches["native_chain"].append(window)
    return matches


def strip_internal(group: dict[str, Any]) -> dict[str, Any]:
    out = {key: value for key, value in group.items() if not key.startswith("_")}
    if isinstance(out.get("score"), dict):
        out["score"] = {key: value for key, value in out["score"].items() if not key.startswith("_")}
    return out


def ia8_to_rgb(values: bytes | list[int], width: int, height: int) -> Image.Image:
    assert Image is not None
    pixels = []
    for y in range(height):
        for x in range(width):
            value = values[y * width + x]
            intensity = (value >> 4) * 17
            alpha = (value & 0x0F) * 17
            checker = 42 if ((x // 4) + (y // 4)) % 2 else 58
            out = (intensity * alpha + checker * (255 - alpha)) // 255
            pixels.append((out, out, out))
    image = Image.new("RGB", (width, height))
    image.putdata(pixels)
    return image


def write_preview(
    preview_dir: Path,
    native_source: bytes,
    native_width: int,
    native_height: int,
    groups: list[dict[str, Any]],
) -> str | None:
    if Image is None or ImageDraw is None:
        return None
    preview_dir.mkdir(parents=True, exist_ok=True)
    cells: list[Image.Image] = []

    def add_cell(image: Image.Image, label: str) -> None:
        scale = max(1, min(8, 192 // max(image.size)))
        scaled = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
        cell = Image.new("RGB", (max(300, scaled.width), scaled.height + 38), (18, 18, 18))
        cell.paste(scaled, ((cell.width - scaled.width) // 2, 0))
        draw = ImageDraw.Draw(cell)
        draw.text((4, scaled.height + 4), label[:52], fill=(230, 230, 230))
        cells.append(cell)

    add_cell(ia8_to_rgb(native_source, native_width, native_height), "native tex654 IA8")
    scored = [group for group in groups if group.get("score")]
    scored.sort(key=lambda item: item["score"]["mean_abs_delta"])
    for group in scored[:12]:
        score = group["score"]
        dim = group["main_dim"]
        values = score.get("_resized")
        if values is None:
            continue
        label = (
            f"r{group['room']} {group['side']} idx{group['index']} {group['image']} "
            f"{group['fmt']}/{group['siz']} {dim['width']}x{dim['height']} "
            f"tris={group.get('draw_triangles', 0)} {score['decoder']} mad={score['mean_abs_delta']:.1f}"
        )
        add_cell(ia8_to_rgb(values, native_width, native_height), label)

    if not cells:
        return None
    columns = 2
    rows = (len(cells) + columns - 1) // columns
    cell_width = max(cell.width for cell in cells)
    cell_height = max(cell.height for cell in cells)
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), (12, 12, 12))
    for index, cell in enumerate(cells):
        sheet.paste(cell, ((index % columns) * cell_width, (index // columns) * cell_height))
    sheet_path = preview_dir / "stock_rdp_texture_candidate_sheet.png"
    sheet.save(sheet_path)
    return str(sheet_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock-trace", type=Path, required=True)
    parser.add_argument("--dump-dir", type=Path, required=True)
    parser.add_argument("--native-source-bin", type=Path, required=True)
    parser.add_argument("--native-source-chain-bin", type=Path)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--rooms", default="132,136")
    parser.add_argument("--include-point-dl", action="store_true")
    parser.add_argument("--native-width", type=int, default=54)
    parser.add_argument("--native-height", type=int, default=54)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()

    rooms = {int(item.strip()) for item in args.rooms.split(",") if item.strip()}
    native_source = args.native_source_bin.read_bytes()
    native_chain = args.native_source_chain_bin.read_bytes() if args.native_source_chain_bin else b""
    native_hashes = prefix_hashes(native_source)
    native_chain_hashes = prefix_hashes(native_chain)

    record = load_frame(args.stock_trace, args.frame)
    sides = ("point_dl",) + ROOM_DL_BASE_SIDES if args.include_point_dl else ROOM_DL_BASE_SIDES
    groups = collect_groups(record, rooms, args.dump_dir, sides)
    missing = [group for group in groups if not group["dump_exists"] or group["dump_bytes"] == 0]
    incomplete_trace_dumps = [
        group for group in groups if int(group["trace_dump_bytes"]) != int(group["dump_bytes"])
    ]

    for group in groups:
        group["exact_hash_matches"] = exact_matches(group, native_hashes, native_chain_hashes)
        score = score_group(group, native_source, args.native_width, args.native_height)
        if score is not None:
            group["score"] = score

    preview_sheet = None
    if args.preview_dir:
        preview_sheet = write_preview(args.preview_dir, native_source, args.native_width, args.native_height, groups)

    exact_groups = [
        strip_internal(group)
        for group in groups
        if group["exact_hash_matches"]["native_source"] or group["exact_hash_matches"]["native_chain"]
    ]
    scored_groups = [group for group in groups if group.get("score")]
    scored_groups.sort(key=lambda item: item["score"]["mean_abs_delta"])
    best_scores = [strip_internal(group) for group in scored_groups[:12]]
    drawn_groups = [group for group in groups if int(group.get("draw_triangles", 0)) > 0]
    drawn_scored_groups = [group for group in drawn_groups if group.get("score")]
    drawn_scored_groups.sort(key=lambda item: item["score"]["mean_abs_delta"])
    best_drawn_scores = [strip_internal(group) for group in drawn_scored_groups[:12]]

    room_summary: dict[str, Any] = {}
    for room in record.get("rooms", {}).get("dl", []):
        room_id = int(room.get("room", -1))
        if room_id not in rooms:
            continue
        room_summary[str(room_id)] = {
            side: {
                "settex": int(room.get(side, {}).get("settex", 0)),
                "target_settex": int(room.get(side, {}).get("target_settex", 0)),
                "settimg": int(room.get(side, {}).get("settimg", 0)),
                "rdp_tex_samples": len(room.get(side, {}).get("rdp_tex_sample", [])),
                "rdp_tex_sample_truncated": int(room.get(side, {}).get("rdp_tex_sample_truncated", 0)),
                "draw_states": len(room.get(side, {}).get("draw_state", [])),
                "draw_state_truncated": int(room.get(side, {}).get("draw_state_truncated", 0)),
                "draw_triangles": sum(
                    int(state.get("triangles", 0)) for state in room.get(side, {}).get("draw_state", [])
                ),
            }
            for side in sides
        }

    result = {
        "status": "pass" if not missing and not incomplete_trace_dumps else "fail",
        "frame": args.frame,
        "rooms": sorted(rooms),
        "sides": list(sides),
        "native_source": {
            "path": str(args.native_source_bin),
            "bytes": len(native_source),
            "width": args.native_width,
            "height": args.native_height,
            "hashes": native_hashes,
            "byte_top": [[byte, count] for byte, count in Counter(native_source).most_common(8)],
            "alpha_nibble_top": [[value, count] for value, count in Counter(byte & 0x0F for byte in native_source).most_common(8)],
            "intensity_nibble_top": [[value, count] for value, count in Counter(byte >> 4 for byte in native_source).most_common(8)],
        },
        "native_source_chain": {
            "path": str(args.native_source_chain_bin) if args.native_source_chain_bin else None,
            "bytes": len(native_chain),
            "hashes": native_chain_hashes,
        },
        "room_summary": room_summary,
        "group_count": len(groups),
        "dumped_group_count": sum(1 for group in groups if group["dump_exists"] and group["dump_bytes"] > 0),
        "drawn_group_count": len(drawn_groups),
        "drawn_scored_group_count": len(drawn_scored_groups),
        "missing_dump_count": len(missing),
        "trace_dump_mismatch_count": len(incomplete_trace_dumps),
        "exact_match_count": len(exact_groups),
        "exact_matches": exact_groups,
        "best_scores": best_scores,
        "best_drawn_scores": best_drawn_scores,
        "groups": [strip_internal(group) for group in groups],
        "preview_sheet": preview_sheet,
        "interpretation": [
            f"captured {sum(1 for group in groups if group['dump_exists'] and group['dump_bytes'] > 0)}/{len(groups)} stock G_SETTIMG dumps",
            f"draw-associated texture groups: {len(drawn_groups)}/{len(groups)}",
            f"exact native source/hash matches: {len(exact_groups)}",
            "naive decoder scores are only a sanity check; weak scores imply the next oracle must trace stock draw/pixel semantics",
        ],
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")

    for line in result["interpretation"]:
        print(line)
    if best_scores:
        best = best_scores[0]
        print(
            "best naive score: "
            f"room={best['room']} {best['side']} idx={best['index']} image={best['image']} "
            f"decoder={best['score']['decoder']} mad={best['score']['mean_abs_delta']:.3f} "
            f"corr={best['score']['correlation']:.3f}"
        )
    if best_drawn_scores:
        best = best_drawn_scores[0]
        print(
            "best drawn naive score: "
            f"room={best['room']} {best['side']} idx={best['index']} image={best['image']} "
            f"triangles={best['draw_triangles']} decoder={best['score']['decoder']} "
            f"mad={best['score']['mean_abs_delta']:.3f} corr={best['score']['correlation']:.3f}"
        )
    if preview_sheet:
        print(f"preview_sheet: {preview_sheet}")
    print(f"summary_json: {args.out}")
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
