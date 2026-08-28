#!/usr/bin/env python3
"""ROM-free regression for the room-glass source reconstruction oracle."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

try:
    from PIL import Image
except ImportError:  # Pillow is an optional dev dep; skip cleanly (ctest SKIP_RETURN_CODE 125)
    import sys
    print("SKIP: room-glass source reconstruction regression requires Pillow (pip install pillow)")
    sys.exit(125)

import analyze_room_glass_source_reconstruction as recon


ROOM_GLASS_CC = "0x00738e4f020a2d12"


def write_ppm(path: Path, width: int, height: int, pixels: list[tuple[int, int, int]]) -> None:
    body = bytes(channel for pixel in pixels for channel in pixel)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + body)


def write_pgm(path: Path, width: int, height: int, pixels: list[int]) -> None:
    path.write_bytes(f"P5\n{width} {height}\n255\n".encode("ascii") + bytes(pixels))


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_room_glass_recon_") as tmp:
        root = Path(tmp)
        route_json = root / "route.json"
        log = root / "native.log"
        texture_rgba = root / "texture.ppm"
        texture_alpha = root / "texture_alpha.pgm"
        stock_image = root / "stock.ppm"
        default_image = root / "native.bmp"
        underlay_image = root / "underlay.bmp"

        route_json.write_text(
            json.dumps(
                {
                    "name": "synthetic_room_glass",
                    "visual_logical_size": [8, 8],
                    "visual_logical_viewport": [0, 0, 8, 8],
                    "visual_baseline_logical_frame": "full",
                    "visual_test_logical_frame": "full",
                    "visual_regions": [{"name": "target", "roi": [2, 2, 1, 1]}],
                }
            ),
            encoding="utf-8",
        )

        pixels = [(0, 0, 0)] * 64
        for y in (2, 3):
            for x in (2, 3):
                pixels[y * 8 + x] = (80, 80, 80)
        write_ppm(texture_rgba, 8, 8, pixels)
        write_pgm(texture_alpha, 8, 8, [102] * 64)

        write_ppm(stock_image, 8, 8, [(32, 32, 32)] * 64)
        Image.new("RGB", (8, 8), (32, 32, 32)).save(default_image)
        Image.new("RGB", (8, 8), (0, 0, 0)).save(underlay_image)

        log.write_text(
            (
                "[SETTEX-MATERIAL-CC] frame=7 tri=3 class=room "
                f"cc={ROOM_GLASS_CC} settexcc={ROOM_GLASS_CC} effcc={ROOM_GLASS_CC} "
                "opts=0x00043C13 texnum=654 wh=8x8 lod=255 tile0=(1,2,2,0,0,0,0,8,8) "
                "tile1=(1,2,2,1,1,0,0,4,4) tex_used=(1,1) blend=alpha "
                "alpha=1 fog=0 texedge=0 depth=(1,0,1) prim=(0,0,0,0) "
                "env=(255,255,255,255) fogrgba=(0,0,0,0) "
                "shade0=(255,255,255,255) shade1=(255,255,255,255) "
                "shade2=(255,255,255,255) oml_raw=0xC41049D8 "
                "vuv0=(192.00,192.00) vuv1=(192.00,192.00) vuv2=(192.00,192.00) "
                "vclip0=(-1.00,1.00,0.00,1.00) vclip1=(1.00,1.00,0.00,1.00) "
                "vclip2=(-1.00,-1.00,0.00,1.00) rgba_wh=8x8 "
                "uv0=(0.812500,0.812500) t0l=(0,0,0,102) t0p=(0,0,0,102) "
                "uv1=(0.437500,0.437500) t1l=(80,80,80,102) t1p=(80,80,80,102) "
                "shadec=(255,255,255,255) lodc=255 fogc=(0,0,0,0) "
                "shaderL_frag=(80,80,80,102) shaderP_frag=(80,80,80,102)\n"
            ),
            encoding="utf-8",
        )

        payload, status = recon.analyze(
            type(
                "Args",
                (),
                {
                    "log": log,
                    "route_json": route_json,
                    "texture_rgba": texture_rgba,
                    "texture_alpha": texture_alpha,
                    "base_case_dir": root,
                    "stock_image": stock_image,
                    "default_image": default_image,
                    "underlay_image": underlay_image,
                    "region": "target",
                    "frame": "latest",
                    "texnum": 654,
                    "effcc": ROOM_GLASS_CC,
                    "opts": "0x00043C13",
                    "oml_raw": "0xC41049D8",
                    "wh": "8x8",
                    "rgba_wh": "8x8",
                    "active_threshold": 1,
                    "align_to": "native",
                    "require_fb_capture": False,
                    "json_out": None,
                },
            )()
        )

    assert status == 0, json.dumps(payload, indent=2, sort_keys=True)
    assert payload["status"] == "pass"
    assert payload["center_validation"]["status"] == "pass"
    check = payload["center_validation"]["checks"][0]
    assert check["tex_linear"] == [0, 0, 0, 102]
    assert check["tex1_linear"] == [80, 80, 80, 102]
    assert check["shader_linear"] == [80, 80, 80, 102]
    assert payload["region"]["coverage_pct"] == 100.0
    assert payload["modes"]["linear"]["source_alpha_counts"] == {"102": 1}

    print("PASS: room-glass source reconstruction regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
