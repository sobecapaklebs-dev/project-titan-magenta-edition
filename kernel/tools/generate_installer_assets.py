#!/usr/bin/env python3
"""Compile the curated installer illustrations to A8.

The installer artwork is intentionally sourced from the approved PS2 FAT,
SCPH-70000-family and SCPH-90000-family line-art PNGs already stored under
assets/ui/installer/. This tool never invents replacement console art.
"""
from pathlib import Path
import argparse
from PIL import Image

SIZE = (256, 256)
NAMES = (
    "titan_installer_fat",
    "titan_installer_slim70",
    "titan_installer_slim90",
)


def compile_one(base: Path, name: str) -> None:
    png = base / f"{name}.png"
    raw = base / f"{name}.a8"
    if not png.exists():
        raise SystemExit(f"missing curated installer asset: {png}")
    image = Image.open(png).convert("RGBA")
    if image.size != SIZE:
        raise SystemExit(f"{png.name}: expected {SIZE[0]}x{SIZE[1]}, got {image.size[0]}x{image.size[1]}")
    alpha = image.getchannel("A")
    raw.write_bytes(alpha.tobytes())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    base = Path(args.output)
    for name in NAMES:
        compile_one(base, name)
    print("installer-assets: OK")


if __name__ == "__main__":
    main()
