#!/usr/bin/env python3
"""Convert a PNG/JPEG image to Project Titan raw little-endian RGB565."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument(
        "--resize",
        action="store_true",
        help="resize the source to the requested dimensions instead of requiring an exact match",
    )
    args = parser.parse_args()

    if args.width <= 0 or args.height <= 0:
        raise SystemExit("width and height must be positive")

    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for image conversion: python3 -m pip install Pillow"
        ) from exc

    with Image.open(args.input) as image:
        image = image.convert("RGB")
        if image.size != (args.width, args.height):
            if not args.resize:
                raise SystemExit(
                    f"source is {image.width}x{image.height}; expected {args.width}x{args.height} "
                    "(pass --resize to convert it)"
                )
            image = image.resize((args.width, args.height), Image.Resampling.LANCZOS)

        output = bytearray()
        for red, green, blue in image.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            output.extend(struct.pack("<H", value))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"RGB565: {args.output} ({args.width}x{args.height}, {len(output)} bytes)")


if __name__ == "__main__":
    main()
