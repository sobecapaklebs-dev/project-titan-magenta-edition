#!/usr/bin/env python3
"""ROM-free regression checks for stock RDP command-stream analysis."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import analyze_stock_rdp_command_stream as analyzer


def triangle_setup(x0: int, x1: int, y0: int, y1: int) -> dict[str, int]:
    fixed_x0 = x0 << 15
    fixed_x1 = x1 << 15
    sub_y0 = y0 << 2
    sub_y1 = y1 << 2
    return {
        "xh": fixed_x1,
        "xm": fixed_x0,
        "xl": fixed_x0,
        "yh": sub_y0,
        "ym": sub_y0,
        "yl": sub_y1,
        "dxhdy": 0,
        "dxmdy": 0,
        "dxldy": 0,
        "flags": 0,
    }


def draw_op(sequence: int, image: str, bbox: list[int], setup: dict[str, int]) -> dict[str, object]:
    return {
        "addr": f"0x{0x100000 + sequence * 0x10:06x}",
        "op": "0x0f",
        "texture_serial": sequence,
        "image": image,
        "image_kseg0": f"0x80{int(image, 16):06x}",
        "fmt": 3,
        "siz": 2,
        "tile": 6,
        "width": 1,
        "height": 1,
        "combine": ["0xfc26a004", "0x1f1493ff"],
        "other": ["0xef992c6f", "0xc81049d8"],
        "env": "0x000000ff",
        "color_image": "0x034f0000",
        "bbox": bbox,
        "valid": 1,
        "setup": setup,
    }


def write_fixture(root: Path) -> tuple[Path, Path]:
    sidecar = root / "rdp_command_stream.jsonl"
    route = root / "route.json"
    route.write_text(
        json.dumps(
            {
                "visual_regions": [
                    {"name": "target", "roi": [10, 10, 10, 10]},
                ]
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    records = [
        draw_op(1, "0x010000", [10, 10, 20, 20], triangle_setup(10, 19, 10, 20)),
        draw_op(2, "0x020000", [15, 10, 20, 20], triangle_setup(15, 19, 10, 20)),
        # Deliberate false positive: bbox overlaps the target, but the real
        # triangle spans are outside it. The span model must not let this late
        # draw become the final owner.
        draw_op(3, "0x030000", [10, 10, 20, 20], triangle_setup(30, 39, 30, 40)),
    ]
    with sidecar.open("w", encoding="utf-8") as handle:
        for index, op in enumerate(records, start=1):
            handle.write(
                json.dumps(
                    {
                        "frame": 1,
                        "render_serial": index,
                        "source": 0,
                        "commands": 1,
                        "truncated": 0,
                        "summary_truncated": 0,
                        "draw_op_truncated": 0,
                        "scissor": [0, 0, 1280, 960],
                        "op_counts": [{"op": "0x0f", "count": 1}],
                        "draw_state": [],
                        "draw_ops": [op],
                    }
                )
                + "\n"
            )
    return sidecar, route


def owner_pixels(summary: dict[str, object], image: str) -> int:
    region = summary["draw_ops"]["regions"]["target"]  # type: ignore[index]
    for owner in region["top_final_owners"]:  # type: ignore[index]
        if owner["image_kseg0"] == image:
            return int(owner["final_pixels"])
    return 0


def write_ppm(path: Path, width: int, height: int, pixels: list[tuple[int, int, int]]) -> None:
    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        for r, g, b in pixels:
            handle.write(bytes((r, g, b)))


def check_active_frame_screenshot_mapping(root: Path) -> bool:
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("SKIP: screenshot frame sampler regression requires Pillow")
        return False

    width = 8
    height = 8
    pixels = [(0, 0, 0) for _ in range(width * height)]
    cells = {
        (0, 0): (100, 10, 10),
        (1, 0): (10, 110, 10),
        (0, 1): (10, 10, 120),
        (1, 1): (130, 130, 10),
    }
    for logical_y in range(2):
        for logical_x in range(2):
            color = cells[(logical_x, logical_y)]
            for y in range(2 + logical_y * 2, 4 + logical_y * 2):
                for x in range(2 + logical_x * 2, 4 + logical_x * 2):
                    pixels[y * width + x] = color

    image_path = root / "active_frame.ppm"
    write_ppm(image_path, width, height, pixels)
    sampler = analyzer.load_screenshot_sampler(image_path, (2, 2), frame_mode="active")
    assert sampler is not None
    assert sampler["frame_bbox"] == (2, 2, 4, 4)
    assert analyzer.sample_logical_pixel(sampler, 0) == (100.0, 10.0, 10.0)
    assert analyzer.sample_logical_pixel(sampler, 1) == (10.0, 110.0, 10.0)
    assert analyzer.sample_logical_pixel(sampler, 4096) == (10.0, 10.0, 120.0)
    assert analyzer.sample_logical_pixel(sampler, 4097) == (130.0, 130.0, 10.0)
    print("PASS: active-frame screenshot sampler regression")
    return True


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_rdp_analyzer_regression_") as tmp:
        root = Path(tmp)
        sidecar, route = write_fixture(root)
        regions = analyzer.route_regions(route)
        bbox = analyzer.load_summary(
            sidecar,
            known_images=set(),
            top=8,
            min_draws=1,
            stack_limit=8,
            coverage_model="bbox",
            regions=regions,
        )
        span = analyzer.load_summary(
            sidecar,
            known_images=set(),
            top=8,
            min_draws=1,
            stack_limit=8,
            coverage_model="span",
            regions=regions,
        )
        check_active_frame_screenshot_mapping(root)

    bbox_region = bbox["draw_ops"]["regions"]["target"]  # type: ignore[index]
    span_region = span["draw_ops"]["regions"]["target"]  # type: ignore[index]

    assert bbox_region["final_owner_pixels"] == 100
    assert owner_pixels(bbox, "0x80030000") == 100

    assert span_region["final_owner_pixels"] == 100
    assert span_region["final_owner_states"] == 2
    assert owner_pixels(span, "0x80010000") == 50
    assert owner_pixels(span, "0x80020000") == 50
    assert owner_pixels(span, "0x80030000") == 0

    print("PASS: stock RDP command-stream span-owner regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
