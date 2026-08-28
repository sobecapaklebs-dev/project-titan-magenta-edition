#!/usr/bin/env python3
"""ROM-free regression checks for glass center-pixel handoff analysis."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import analyze_glass_center_handoff as handoff


def write_jsonl(path: Path, rows: list[dict]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row) + "\n")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_glass_center_handoff_") as tmp:
        root = Path(tmp)
        stock_probe = root / "rdp_pixel_probe.jsonl"
        command_summary = root / "rdp_command_summary.json"
        native_summary = root / "native_settex_summary.json"
        native_pixel_log = root / "native_pixel.log"

        write_jsonl(
            stock_probe,
            [
                {
                    "type": "stats",
                    "frame_context": 9,
                    "x": 12,
                    "y": 22,
                    "fb_addr": "0x3da800",
                    "fb_width": 320,
                    "fb_fmt": 0,
                    "fb_size": 2,
                    "target_hits": 4,
                    "sample_attempts": 4,
                    "read_ok": 4,
                    "read_fail_no_maps": 0,
                    "suppressed_unchanged": 1,
                    "records": 2,
                    "max_records": 128,
                },
                {
                    "type": "sample",
                    "frame_context": 9,
                    "frame_draw_sequence": 2,
                    "texture_image": "0x00aaa0",
                    "texture_fmt": 4,
                    "texture_size": 2,
                    "texture_width": 1,
                    "x": 12,
                    "y": 22,
                    "fb_addr": "0x3da800",
                    "fb_width": 320,
                    "fb_fmt": 0,
                    "fb_size": 2,
                    "bbox": [10, 20, 14, 24],
                    "raw": "0x00002948",
                    "hidden": "0x00000003",
                    "rgba": [40, 40, 40, 224],
                    "changed": 1,
                },
                {
                    "type": "sample",
                    "frame_context": 9,
                    "frame_draw_sequence": 3,
                    "texture_image": "0x00bbb0",
                    "texture_fmt": 4,
                    "texture_size": 2,
                    "texture_width": 1,
                    "x": 12,
                    "y": 22,
                    "fb_addr": "0x3da800",
                    "fb_width": 320,
                    "fb_fmt": 0,
                    "fb_size": 2,
                    "texture_serial": 4,
                    "other": ["0xef992c2f", "0xc8102078"],
                    "combine": ["0xfc26a004", "0x1f1493ff"],
                    "env": "0x0000007f",
                    "scissor": [0, 0, 1280, 960],
                    "raster_flags": "0x01804008",
                    "raster_dither": 0,
                    "static_texture_fmt": 4,
                    "static_texture_size": 2,
                    "depth_flags": "0x000001d8",
                    "coverage_mode": 0,
                    "z_mode": 2,
                    "blend_cycles": [[0, 0, 1, 0], [0, 0, 1, 0]],
                    "combiner": {
                        "rgb": [[2, 0, 6, 1], [2, 0, 6, 1]],
                        "alpha": [[7, 0, 4, 1], [7, 0, 4, 1]],
                    },
                    "draw_tile": 1,
                    "tile_state": {
                        "valid": 1,
                        "fmt": 4,
                        "size": 2,
                        "line": 7,
                        "tmem": 0,
                        "palette": 0,
                        "cms": 1,
                        "cmt": 1,
                        "masks": 0,
                        "maskt": 0,
                        "shifts": 0,
                        "shiftt": 0,
                        "uls": 0,
                        "ult": 0,
                        "lrs": 212,
                        "lrt": 212,
                    },
                    "bbox": [10, 20, 14, 24],
                    "draw_word_count": 44,
                    "draw_words_truncated": 0,
                    "draw_words": ["0x0f800002", "0x00000001", "0x00000002"],
                    "raw": "0x000018c7",
                    "hidden": "0x00000003",
                    "rgba": [24, 24, 24, 224],
                    "changed": 1,
                },
                {
                    "type": "sample",
                    "frame_context": 9,
                    "frame_draw_sequence": 4,
                    "texture_image": "0x00ddd0",
                    "texture_fmt": 4,
                    "texture_size": 2,
                    "texture_width": 1,
                    "x": 12,
                    "y": 22,
                    "fb_addr": "0x3da800",
                    "fb_width": 320,
                    "fb_fmt": 0,
                    "fb_size": 2,
                    "bbox": [10, 20, 14, 24],
                    "raw": "0x00006318",
                    "hidden": "0x00000003",
                    "rgba": [99, 99, 99, 224],
                    "changed": 0,
                },
            ],
        )

        command_summary.write_text(
            json.dumps(
                {
                    "draw_ops": {
                        "regions": {
                            "target": {
                                "coverage_model": "span",
                                "region_area": 16,
                                "final_owner_pixels": 16,
                                "final_owner_pct": 100.0,
                                "final_owner_states": 2,
                                "ordered_hit_count": 3,
                                "last_hits": [
                                    {
                                        "sequence": 10,
                                        "frame": 99,
                                        "image": "0x00bbb0",
                                        "image_kseg0": "0x8000bbb0",
                                        "fmt": 4,
                                        "siz": 2,
                                        "tile": 6,
                                        "combine": ["0xfc26a004", "0x1ffc93fc"],
                                        "other": ["0xef992c2f", "0xc8102078"],
                                        "env": "0x000000ff",
                                        "pixel_count": 16,
                                        "bbox": [10, 20, 14, 24],
                                        "coverage_bbox": [10, 20, 14, 24],
                                    },
                                    {
                                        "sequence": 11,
                                        "frame": 99,
                                        "image": "0x00ccc0",
                                        "image_kseg0": "0x8000ccc0",
                                        "fmt": 4,
                                        "siz": 2,
                                        "tile": 6,
                                        "combine": ["0xfc26a004", "0x1ffc93fc"],
                                        "other": ["0xef992c2f", "0xc8102078"],
                                        "env": "0x000000ff",
                                        "pixel_count": 16,
                                        "bbox": [10, 20, 14, 24],
                                        "coverage_bbox": [10, 20, 14, 24],
                                    },
                                ],
                            }
                        }
                    }
                },
                indent=2,
            ),
            encoding="utf-8",
        )

        native_summary.write_text(
            json.dumps(
                {
                    "status": "pass",
                    "frame_selection": {"selected": 122},
                    "filters": {"texnum": [654]},
                    "regions": {
                        "target": {
                            "matched_rows": 4,
                            "coverage_pixels": 16,
                            "coverage_pct": 100.0,
                            "signature_counts": {"texnum": {"654": 4}},
                            "sample_summary": {
                                "shaderL_frag": {
                                    "count": 4,
                                    "luma": {"min": 0.0, "mean": 11.0, "max": 23.0},
                                    "alpha_counts": {"102": 4},
                                }
                            },
                            "examples": [],
                        }
                    },
                },
                indent=2,
            ),
            encoding="utf-8",
        )

        native_pixel_log.write_text(
            "\n".join(
                [
                    "ordinary log row",
                    (
                        '[SETTEX-PIXEL] {"status":"ok","frame":122,"tri":40,'
                        '"serial":0,"target":[6,11],"fb":[24,44],"gl":[24,195],'
                        '"inside":1,"bary":[0.25,0.50,0.25],'
                        '"cc":"0x00738e4f020a2d12",'
                        '"effcc":"0x00738e4f020a2d12",'
                        '"opts":"0x00043C13","raw":"0xC41049D8",'
                        '"blend":"alpha","api_blend":"alpha",'
                        '"texnum":654,"wh":[54,54],'
                        '"screen_bbox":[10.0,20.0,14.0,24.0],'
                        '"src_valid":1,"sample_valid":[1,1],'
                        '"shaderL_frag":[20,21,22,102],'
                        '"pre":[10,11,12],"post":[24,24,24],'
                        '"delta":[14,13,12],"changed":1}'
                    ),
                    (
                        '[SETTEX-PIXEL] {"status":"ok","frame":123,"tri":41,'
                        '"serial":1,"target":[6,11],"fb":[24,44],"gl":[24,195],'
                        '"inside":1,"bary":[0.25,0.50,0.25],'
                        '"cc":"0x00738e4f020a2d12",'
                        '"effcc":"0x00738e4f020a2d12",'
                        '"opts":"0x00043C13","raw":"0xC41049D8",'
                        '"blend":"alpha","api_blend":"alpha",'
                        '"texnum":654,"wh":[54,54],'
                        '"screen_bbox":[10.0,20.0,14.0,24.0],'
                        '"pre":[10,11,12],"post":[32,32,32],'
                        '"delta":[22,21,20],"changed":1}'
                    ),
                ]
            )
            + "\n",
            encoding="utf-8",
        )

        payload = handoff.summarize(
            type(
                "Args",
                (),
                {
                    "stock_probe": stock_probe,
                    "stock_command_summary": command_summary,
                    "native_settex_summary": native_summary,
                    "native_pixel_log": native_pixel_log,
                    "native_pixel_target": None,
                    "native_pixel_frame": None,
                    "stock_frame_context": None,
                    "stock_fb_addr": None,
                    "stock_texture_image": None,
                    "region": "target",
                    "top": 4,
                },
            )()
        )

    assert payload["status"] == "pass"
    assert payload["stock_pixel"]["sample_rows"] == 3
    assert payload["stock_pixel"]["changed_sample_rows"] == 2
    assert payload["stock_pixel"]["target"] == [12, 22]
    assert payload["stock_pixel"]["selected_sample"]["texture_image"] == "0x00bbb0"
    assert payload["stock_pixel"]["selected_sample"]["luma"] == 24.0
    assert payload["stock_pixel"]["selected_sample"]["rgba"] == [24, 24, 24, 224]
    assert payload["stock_pixel"]["selected_sample"]["raster_flags"] == "0x01804008"
    assert payload["stock_pixel"]["selected_sample"]["depth_flags"] == "0x000001d8"
    assert payload["stock_pixel"]["selected_sample"]["other"] == ["0xef992c2f", "0xc8102078"]
    assert payload["stock_pixel"]["selected_sample"]["combine"] == ["0xfc26a004", "0x1f1493ff"]
    assert payload["stock_pixel"]["selected_sample"]["tile_state"]["fmt"] == 4
    assert payload["stock_pixel"]["selected_sample"]["combiner"]["alpha"][0] == [7, 0, 4, 1]
    assert payload["stock_pixel"]["selected_sample"]["draw_word_count"] == 44
    assert payload["stock_pixel"]["selected_sample_index"] == 1
    assert payload["stock_pixel"]["selection_reason"] == "last_changed_sample"
    assert payload["stock_pixel"]["selected_framebuffer_input_reason"] == "previous_emitted_same_frame_sample"
    assert payload["stock_pixel"]["selected_framebuffer_input"]["texture_image"] == "0x00aaa0"
    assert payload["stock_pixel"]["selected_framebuffer_input"]["rgba"] == [40, 40, 40, 224]
    assert payload["stock_pixel"]["selected_framebuffer_input_vs_selected_rgb"]["delta"] == [-16, -16, -16]
    assert payload["stock_pixel"]["selected_raw_transition"] == {
        "before": "0x00002948",
        "after": "0x000018c7",
    }
    assert payload["stock_pixel"]["selected_hidden_transition"] == {
        "before": "0x00000003",
        "after": "0x00000003",
        "delta": 0,
    }
    assert payload["stock_pixel"]["last_sample"]["texture_image"] == "0x00ddd0"
    assert payload["stock_pixel"]["last_sample"]["luma"] == 99.0
    assert payload["stock_command_region"]["final_owner_states"] == 2
    assert len(payload["stock_command_region"]["last_hits_matching_final_texture"]) == 1
    assert len(payload["stock_command_region"]["later_covering_hits_with_different_texture"]) == 1
    assert payload["native_settex"]["shaderL_frag"]["alpha_counts"] == {"102": 4}
    assert payload["native_pixel"]["stock_target"] == [12, 22]
    assert payload["native_pixel"]["selected_native_target"] == [6, 11]
    assert payload["native_pixel"]["selection_reason"] == "single_native_target"
    assert payload["native_pixel"]["selected_native_frame"] == 122
    assert payload["native_pixel"]["target_rows"] == 2
    assert payload["native_pixel"]["selected_frame_rows"] == 1
    assert payload["native_pixel"]["inside_rows"] == 1
    assert payload["native_pixel"]["changed_inside_rows"] == 1
    assert payload["native_pixel"]["selected_final"]["src_valid"] == 1
    assert payload["native_pixel"]["selected_final"]["shaderL_frag"] == [20, 21, 22, 102]
    assert payload["native_pixel"]["selected_final"]["post"] == [24, 24, 24]
    assert payload["native_pixel"]["selected_post_vs_stock_rgb"]["mean_abs_rgb"] == 0.0
    assert any("pixel probe as final-pixel authority" in item for item in payload["warnings"])
    assert any("stock framebuffer input candidate" in item for item in payload["interpretation"])
    assert any("native texnum 654 fragment source" in item for item in payload["interpretation"])
    assert any("native settex pixel probe" in item for item in payload["interpretation"])

    print("PASS: glass center-pixel handoff analyzer regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
