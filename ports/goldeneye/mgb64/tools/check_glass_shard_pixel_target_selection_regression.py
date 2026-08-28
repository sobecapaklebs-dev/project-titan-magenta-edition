#!/usr/bin/env python3
"""ROM-free regression checks for shard-owned RDP pixel target selection."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

try:
    from PIL import Image
except ImportError:  # Pillow is an optional dev dep; skip cleanly (ctest SKIP_RETURN_CODE 125)
    import sys
    print("SKIP: glass-shard pixel target selection regression requires Pillow (pip install pillow)")
    sys.exit(125)

import select_glass_shard_pixel_targets as selector


def projection_record() -> dict[str, object]:
    return {
        "f": 77,
        "glass_projection": {
            "present": 1,
            "source": "synthetic",
            "scale_mode": "unit_test",
            "active": 2,
            "projected": 2,
            "onscreen": 2,
            "behind": 0,
            "sample_all": 1,
            "sample_limit": 2,
            "sample_count": 2,
            "sample_truncated": 0,
            "viewport": [0.0, 0.0, 8.0, 8.0],
            "sample": [
                {
                    "index": 0,
                    "onscreen": 1,
                    "timer": 6,
                    "screen": [[2.0, 2.0], [5.0, 2.0], [2.0, 5.0]],
                    "screen_bbox": [2.0, 2.0, 5.0, 5.0],
                },
                {
                    "index": 1,
                    "onscreen": 1,
                    "timer": 6,
                    "screen": [[6.0, 6.0], [7.0, 6.0], [6.0, 7.0]],
                    "screen_bbox": [6.0, 6.0, 7.0, 7.0],
                },
            ],
        },
    }


def write_projection(path: Path) -> None:
    path.write_text(json.dumps(projection_record()) + "\n", encoding="utf-8")


def write_images(baseline_path: Path, test_path: Path) -> None:
    baseline = Image.new("RGB", (8, 8), (10, 10, 10))
    test = Image.new("RGB", (8, 8), (10, 10, 10))
    baseline_px = baseline.load()
    test_px = test.load()

    baseline_px[3, 3] = (0, 0, 0)
    test_px[3, 3] = (100, 80, 60)
    test_px[6, 6] = (80, 80, 80)
    test_px[2, 2] = (40, 40, 40)

    baseline.save(baseline_path)
    test.save(test_path)


def write_route(path: Path) -> None:
    route = {
        "visual_logical_size": [8, 8],
        "visual_logical_viewport": [0, 0, 8, 8],
        "visual_baseline_logical_frame": "full",
        "visual_test_logical_frame": "full",
        "visual_regions": [
            {
                "name": "focus",
                "roi": [3, 3, 1, 1],
            },
        ],
    }
    path.write_text(json.dumps(route, indent=2) + "\n", encoding="utf-8")


def run_selector(tmp: Path, *extra: str) -> dict[str, object]:
    out_path = tmp / f"targets_{len(list(tmp.glob('targets_*.json')))}.json"
    argv = [
        "--baseline-trace",
        str(tmp / "baseline.jsonl"),
        "--test-trace",
        str(tmp / "test.jsonl"),
        "--baseline-image",
        str(tmp / "baseline.png"),
        "--test-image",
        str(tmp / "test.png"),
        "--route",
        str(tmp / "route.json"),
        "--mask-mode",
        "bbox",
        "--mask-padding",
        "0",
        "--min-distance",
        "1",
        "--top",
        "2",
        "--json-out",
        str(out_path),
        *extra,
    ]
    assert selector.main(argv) == 0
    return json.loads(out_path.read_text(encoding="utf-8"))


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_shard_pixel_targets_") as tmp_name:
        tmp = Path(tmp_name)
        write_projection(tmp / "baseline.jsonl")
        write_projection(tmp / "test.jsonl")
        write_images(tmp / "baseline.png", tmp / "test.png")
        write_route(tmp / "route.json")

        payload = run_selector(tmp)
        assert payload["baseline_frame"] == 77
        assert payload["test_frame"] == 77
        assert payload["covered_pixels"] == 10
        assert payload["candidate_pixels"] == 3
        assert [item["target"] for item in payload["selected"]] == [[3, 3], [6, 6]]
        assert payload["selected"][0]["crop_xy"] == [3, 3]
        assert payload["selected"][0]["stock_rgb"] == [0, 0, 0]
        assert payload["selected"][0]["native_rgb"] == [100, 80, 60]
        assert payload["selected"][0]["abs_rgb_delta"] == 240
        assert payload["selected"][0]["pieces"] == [0]
        assert payload["selected"][1]["pieces"] == [1]

        focused = run_selector(tmp, "--region", "focus")
        assert focused["region_roi"] == [3, 3, 1, 1]
        assert focused["candidate_pixels"] == 1
        assert [item["target"] for item in focused["selected"]] == [[3, 3]]

    print("PASS: glass shard pixel target selection regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
