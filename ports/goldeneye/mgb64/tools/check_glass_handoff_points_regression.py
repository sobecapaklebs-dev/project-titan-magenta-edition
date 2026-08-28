#!/usr/bin/env python3
"""ROM-free regression checks for multi-point glass handoff summaries."""

from __future__ import annotations

import json
import math
from pathlib import Path
import tempfile

import summarize_glass_handoff_points as summary


def write_point(path: Path,
                stock_target: list[int],
                native_target: list[int],
                stock_rgb: list[int],
                native_post: list[int],
                framebuffer_input: list[int],
                hidden_before: str,
                hidden_after: str) -> None:
    payload = {
        "status": "pass",
        "stock_pixel": {
            "target": stock_target,
            "selection_reason": "last_changed_sample",
            "selected_sample": {
                "texture_image": "0x12f2f0",
                "raw": "0x00002109",
                "hidden": hidden_after,
                "rgba": stock_rgb + [224],
            },
            "selected_framebuffer_input_reason": "previous_emitted_same_frame_sample",
            "selected_framebuffer_input": {
                "texture_image": "0x12b150",
                "raw": "0x000018c7",
                "hidden": hidden_before,
                "rgba": framebuffer_input + [224],
            },
            "selected_framebuffer_input_vs_selected_rgb": {
                "delta": [stock_rgb[i] - framebuffer_input[i] for i in range(3)],
                "mean_abs_rgb": sum(abs(stock_rgb[i] - framebuffer_input[i]) for i in range(3)) / 3.0,
                "luma_delta": float(stock_rgb[0] - framebuffer_input[0]),
            },
            "selected_hidden_transition": {
                "before": hidden_before,
                "after": hidden_after,
                "delta": int(hidden_after, 0) - int(hidden_before, 0),
            },
        },
        "native_pixel": {
            "selected_native_target": native_target,
            "selected_native_frame": 122,
            "inside_rows": 2,
            "changed_inside_rows": 1,
            "selected_final": {
                "pre": framebuffer_input,
                "post": native_post,
                "shaderL_frag": [10, 10, 10, 102],
                "changed": int(native_post != framebuffer_input),
            },
        },
        "warnings": [],
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_glass_handoff_points_") as tmp:
        root = Path(tmp)
        near = root / "near.json"
        off = root / "off.json"
        write_point(
            near,
            [183, 165],
            [92, 93],
            [24, 24, 24],
            [25, 25, 25],
            [20, 20, 20],
            "0x00000002",
            "0x00000003",
        )
        write_point(
            off,
            [188, 170],
            [94, 95],
            [32, 32, 32],
            [11, 11, 11],
            [7, 7, 7],
            "0x00000003",
            "0x00000003",
        )
        payload = summary.summarize([
            ("near", near),
            ("off", off),
        ])

    assert payload["status"] == "pass"
    assert payload["summary"]["points"] == 2
    assert payload["summary"]["points_with_stock_and_native"] == 2
    assert payload["summary"]["points_with_framebuffer_input"] == 2
    assert payload["summary"]["near_points_le_1"] == 1
    assert payload["summary"]["off_points_gt_1"] == 1
    assert payload["summary"]["mean_abs_rgb"]["min"] == 1.0
    assert payload["summary"]["mean_abs_rgb"]["mean"] == 11.0
    assert payload["summary"]["mean_abs_rgb"]["max"] == 21.0
    assert payload["points"][0]["stock_framebuffer_input_rgba"] == [20, 20, 20, 224]
    assert payload["points"][1]["native_source_rgba"] == [10, 10, 10, 102]
    assert payload["points"][1]["native_post_vs_stock_rgb"]["delta"] == [-21, -21, -21]
    assert payload["points"][0]["native_required_source_from_pre_post_alpha"] == [32.5, 32.5, 32.5]
    assert payload["points"][0]["native_pre_vs_stock_framebuffer_input_rgb"]["mean_abs_rgb"] == 0.0
    assert math.isclose(
        payload["points"][0]["native_required_source_vs_reconstructed_rgb"]["mean_abs_rgb"],
        22.5,
    )
    assert payload["summary"]["native_required_source_vs_reconstructed_rgb"]["points"] == 2
    assert math.isclose(
        payload["summary"]["native_required_source_vs_reconstructed_rgb"]["max"],
        22.5,
    )
    assert payload["summary"]["native_pre_vs_stock_framebuffer_input_rgb"]["max"] == 0.0
    assert payload["summary"]["hidden_transitions"][0]["points"] == 1
    assert any("off points >1: 1" in item for item in payload["interpretation"])
    assert any("requires source-vs-reconstructed" in item for item in payload["interpretation"])

    print("PASS: glass handoff points summary regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
