#!/usr/bin/env python3
"""Prepare the user-approved Project Titan boot logo for the PS2 GS."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image


OUTPUT_WIDTH = 640
OUTPUT_HEIGHT = 448
LOGO_WIDTH = 176
LOGO_CENTER_Y = 206
LOGO_THRESHOLD = 16


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--preview", required=True, type=Path)
    parser.add_argument("--raw", required=True, type=Path)
    return parser.parse_args()


def find_logo_bounds(image: Image.Image) -> tuple[int, int, int, int]:
    pixels = np.asarray(image.convert("RGB"), dtype=np.uint8)
    mask = pixels.max(axis=2) >= LOGO_THRESHOLD
    rows, columns = np.nonzero(mask)

    if rows.size == 0 or columns.size == 0:
        raise SystemExit("the source image does not contain a visible logo")

    return (
        int(columns.min()),
        int(rows.min()),
        int(columns.max()) + 1,
        int(rows.max()) + 1,
    )


def compose_medium_logo(image: Image.Image) -> tuple[Image.Image, tuple[int, int, int, int]]:
    bounds = find_logo_bounds(image)
    logo = image.crop(bounds)
    logo_height = round(logo.height * (LOGO_WIDTH / logo.width))
    resized = logo.resize(
        (LOGO_WIDTH, logo_height),
        Image.Resampling.LANCZOS,
    )
    prepared = Image.new("RGB", (OUTPUT_WIDTH, OUTPUT_HEIGHT), "black")
    left = (OUTPUT_WIDTH - LOGO_WIDTH) // 2
    top = LOGO_CENTER_Y - (logo_height // 2)
    prepared.paste(resized, (left, top))
    return prepared, bounds


def pack_psm16(image: Image.Image) -> bytes:
    pixels = np.asarray(image.convert("RGB"), dtype=np.uint16)
    red = pixels[:, :, 0] >> 3
    green = pixels[:, :, 1] >> 3
    blue = pixels[:, :, 2] >> 3
    packed = 0x8000 | red | (green << 5) | (blue << 10)
    return struct.pack(
        "<" + ("H" * packed.size),
        *packed.reshape(-1).tolist(),
    )


def main() -> int:
    args = parse_args()
    source = Image.open(args.source).convert("RGB")
    prepared, logo_bounds = compose_medium_logo(source)

    args.preview.parent.mkdir(parents=True, exist_ok=True)
    args.raw.parent.mkdir(parents=True, exist_ok=True)

    prepared.save(args.preview, optimize=True)
    args.raw.write_bytes(pack_psm16(prepared))

    expected_size = OUTPUT_WIDTH * OUTPUT_HEIGHT * 2

    if args.raw.stat().st_size != expected_size:
        raise SystemExit(
            f"unexpected raw size {args.raw.stat().st_size}; "
            f"expected {expected_size}"
        )

    print(f"source: {source.size[0]}x{source.size[1]}")
    print(
        "source logo bounds: "
        f"{logo_bounds[0]},{logo_bounds[1]}-"
        f"{logo_bounds[2]},{logo_bounds[3]}"
    )
    print(f"logo width: {LOGO_WIDTH}")
    print(f"output: {prepared.size[0]}x{prepared.size[1]}")
    print(f"raw bytes: {args.raw.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
