#!/usr/bin/env python3
"""Generate Project Titan's anti-aliased VU1 silk-ribbon texture.

The asset is raw GS PSMCT32 data in little-endian RGBA byte order. Alpha uses
the native GS 0..128 range. The U axis is seamless and intentionally quiet;
shape and motion come from VU1 geometry rather than a visible tiled pattern.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image

WIDTH = 128
HEIGHT = 32


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    amount = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return amount * amount * (3.0 - 2.0 * amount)


def build_pixels() -> bytes:
    payload = bytearray()

    for y in range(HEIGHT):
        cross = ((y + 0.5) / HEIGHT) * 2.0 - 1.0
        veil = math.exp(-((cross / 0.68) ** 2) * 1.65)
        pearl = math.exp(-(((cross + 0.16) / 0.115) ** 2) * 1.70)
        lower_glow = math.exp(-(((cross - 0.28) / 0.23) ** 2) * 1.90)
        edge = smoothstep(0.0, 0.22, 1.0 - abs(cross))

        for x in range(WIDTH):
            phase = (2.0 * math.pi * x) / WIDTH
            longitudinal = (
                0.985
                + math.sin(phase) * 0.010
                + math.sin(phase * 2.0 + 0.9) * 0.006
            )

            alpha = (
                veil * 0.34
                + pearl * 0.52
                + lower_glow * 0.10
            ) * edge * longitudinal
            alpha = clamp(alpha, 0.0, 1.0)

            luminance = int(round(
                128.0
                + veil * 8.0
                + pearl * 52.0
                + lower_glow * 9.0
            ))
            luminance = max(0, min(255, luminance))
            gs_alpha = max(0, min(128, int(round(alpha * 128.0))))
            payload.extend((luminance, luminance, luminance, gs_alpha))

    return bytes(payload)


def write_preview(payload: bytes, path: Path) -> None:
    image = Image.frombytes("RGBA", (WIDTH, HEIGHT), payload)
    pixels = [
        (red, green, blue, min(255, alpha * 2))
        for red, green, blue, alpha in image.getdata()
    ]
    image.putdata(pixels)
    image = image.resize((WIDTH * 6, HEIGHT * 10), Image.Resampling.BILINEAR)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--raw",
        type=Path,
        default=Path("assets/ui/titan_wave_silk.rgba32"),
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=Path("assets/ui/titan_wave_silk_preview.png"),
    )
    args = parser.parse_args()

    payload = build_pixels()
    args.raw.parent.mkdir(parents=True, exist_ok=True)
    args.raw.write_bytes(payload)
    write_preview(payload, args.preview)
    print(f"generated {args.raw} ({len(payload)} bytes)")
    print(f"generated {args.preview}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
