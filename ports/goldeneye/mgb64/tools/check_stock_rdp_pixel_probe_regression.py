#!/usr/bin/env python3
"""ROM-free regression checks for stock RDP pixel-probe analysis."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import analyze_stock_rdp_pixel_probe as analyzer


def write_fixture(root: Path) -> tuple[Path, Path]:
    route = root / "route.json"
    route.write_text(
        json.dumps(
            {
                "visual_logical_size": [320, 240],
                "visual_regions": [
                    {"name": "target", "roi": [10, 20, 4, 4]},
                ],
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    probe = root / "rdp_pixel_probe.jsonl"
    records = [
        {
            "type": "stats",
            "event": "begin_frame_context",
            "frame_context": 7,
            "command_sequence": 102,
            "draw_sequence": 12,
            "x": 12,
            "y": 22,
            "fb_addr": "0x034f0000",
            "fb_width": 320,
            "fb_fmt": 0,
            "fb_size": 2,
            "commands_seen": 110,
            "draw_ops_seen": 12,
            "bbox_ok": 12,
            "bbox_fail": 0,
            "bbox_target_miss": 9,
            "target_hits": 3,
            "forced_samples": 0,
            "sample_attempts": 3,
            "read_ok": 3,
            "read_fail_no_fb": 0,
            "read_fail_x_oob": 0,
            "read_fail_no_maps": 0,
            "read_fail_no_size": 0,
            "read_fail_oob": 0,
            "suppressed_unchanged": 0,
            "records": 3,
            "max_records": 128,
        },
        {
            "type": "sample",
            "frame_context": 7,
            "command_sequence": 100,
            "draw_sequence": 10,
            "frame_command_sequence": 1,
            "frame_draw_sequence": 1,
            "op": "0x0f",
            "x": 12,
            "y": 22,
            "fb_addr": "0x034f0000",
            "fb_width": 320,
            "fb_fmt": 0,
            "fb_size": 2,
            "texture_image": "0x0012b150",
            "texture_width": 1,
            "texture_fmt": 3,
            "texture_size": 2,
            "bbox": [10, 20, 14, 24],
            "raw": "0x00001234",
            "hidden": "0x00000001",
            "rgba": [16, 32, 48, 96],
            "changed": 1,
        },
        {
            "type": "sample",
            "frame_context": 7,
            "command_sequence": 101,
            "draw_sequence": 11,
            "frame_command_sequence": 2,
            "frame_draw_sequence": 2,
            "op": "0x0f",
            "x": 12,
            "y": 22,
            "fb_addr": "0x034f0000",
            "fb_width": 320,
            "fb_fmt": 0,
            "fb_size": 2,
            "texture_image": "0x0012b150",
            "texture_width": 1,
            "texture_fmt": 3,
            "texture_size": 2,
            "bbox": [10, 20, 14, 24],
            "raw": "0x00001234",
            "hidden": "0x00000001",
            "rgba": [16, 32, 48, 96],
            "changed": 0,
        },
        {
            "type": "sample",
            "frame_context": 7,
            "command_sequence": 102,
            "draw_sequence": 12,
            "frame_command_sequence": 3,
            "frame_draw_sequence": 3,
            "op": "0x0f",
            "x": 12,
            "y": 22,
            "fb_addr": "0x034f0000",
            "fb_width": 320,
            "fb_fmt": 0,
            "fb_size": 2,
            "texture_image": "0x00132c80",
            "texture_width": 1,
            "texture_fmt": 3,
            "texture_size": 2,
            "texture_serial": 4,
            "other": ["0xef992c2f", "0xc8102078"],
            "combine": ["0xfc26a004", "0x1f1493ff"],
            "env": "0x0000007f",
            "scissor": [0, 0, 1280, 960],
            "raster_flags": "0x01804008",
            "raster_dither": 0,
            "static_texture_fmt": 3,
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
                "fmt": 3,
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
            "raw": "0x00005678",
            "hidden": "0x00000003",
            "rgba": [80, 96, 112, 224],
            "changed": 1,
        },
    ]
    with probe.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record) + "\n")
    return route, probe


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_rdp_pixel_probe_regression_") as tmp:
        root = Path(tmp)
        route_path, probe_path = write_fixture(root)
        region = analyzer.route_region(route_path, "target")
        assert region is not None
        records = analyzer.load_records(probe_path)
        summary = analyzer.summarize(
            records,
            region,
            None,
            root / "suggested.jsonl",
            None,
            after_frame_context=7,
            before_frame_context=7,
            max_records=128,
            changed_only=True,
            sample_limit=8,
        )
        doubled_summary = analyzer.summarize(
            [],
            region,
            (640, 240),
            root / "suggested_640.jsonl",
            None,
            after_frame_context=None,
            before_frame_context=None,
            max_records=128,
            changed_only=False,
            sample_limit=8,
        )

    assert summary["records"] == 3
    assert summary["total_rows"] == 4
    assert summary["stats_records"] == 1
    assert summary["changed_records"] == 2
    assert summary["target_xy"] == [12, 22]
    assert summary["route_region"]["center"] == [12, 22]
    assert summary["route_region"]["target_xy_inside"] is True
    assert summary["frame_contexts"] == {"first": 7, "last": 7, "count": 1}
    assert summary["last_stats"]["x"] == 12
    assert summary["last_stats"]["fb_width"] == 320
    assert summary["last_stats"]["target_hits"] == 3
    assert summary["last_stats"]["bbox_target_miss"] == 9
    assert summary["last_stats"]["read_fail_no_maps"] == 0
    assert summary["last_record"]["raster_flags"] == "0x01804008"
    assert summary["last_record"]["depth_flags"] == "0x000001d8"
    assert summary["last_record"]["other"] == ["0xef992c2f", "0xc8102078"]
    assert summary["last_record"]["combine"] == ["0xfc26a004", "0x1f1493ff"]
    assert summary["last_record"]["draw_tile"] == 1
    assert summary["last_record"]["tile_state"]["lrs"] == 212
    assert summary["last_record"]["blend_cycles"] == [[0, 0, 1, 0], [0, 0, 1, 0]]
    assert summary["last_record"]["combiner"]["rgb"][0] == [2, 0, 6, 1]
    assert summary["last_record"]["draw_word_count"] == 44
    assert summary["last_record"]["draw_words"][-1] == "0x00000002"
    assert summary["top_changed_texture_states"][0]["texture_image"] == "0x0012b150"
    assert summary["top_changed_texture_states"][1]["texture_image"] == "0x00132c80"
    assert "MGB64_ARES_TRACE_RDP_PIXEL_PROBE_X=12" in summary["env"]
    assert "MGB64_ARES_TRACE_RDP_PIXEL_PROBE_CHANGED_ONLY=1" in summary["env"]
    assert not summary["warnings"]
    assert doubled_summary["target_xy"] == [24, 22]
    assert doubled_summary["route_region"]["rdp_center"] == [24, 22]
    assert doubled_summary["route_region"]["rdp_bounds"] == [20, 20, 28, 24]
    assert "MGB64_ARES_TRACE_RDP_PIXEL_PROBE_X=24" in doubled_summary["env"]

    print("PASS: stock RDP pixel-probe analyzer regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
