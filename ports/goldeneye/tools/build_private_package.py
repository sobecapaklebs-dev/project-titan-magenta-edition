#!/usr/bin/env python3
"""Build the private TitanEYE TPKG from a user-supplied retail ROM dump."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

EXPECTED_SIZE = 0xC00000
KNOWN_US_SHA1 = "abe01e4aeb033b6c0836819f549c791b26cfde83"


def validate_rom(path: Path) -> None:
    data = path.read_bytes()
    if len(data) != EXPECTED_SIZE:
        raise ValueError(f"ROM must be exactly {EXPECTED_SIZE} bytes")
    if data[:4] != bytes.fromhex("80371240"):
        raise ValueError("private TitanEYE package requires a big-endian .z64 dump")
    if b"GOLDENEYE" not in data[0x20:0x35]:
        raise ValueError("ROM header is not GoldenEye 007")
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 != KNOWN_US_SHA1:
        raise ValueError(f"ROM is not the supported NTSC-U retail dump (SHA-1 {sha1})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--icon", type=Path, required=True)
    parser.add_argument("--tile", type=Path, required=True)
    parser.add_argument("--background", type=Path, required=False)
    parser.add_argument("--package-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    validate_rom(args.rom)
    if not args.module.is_file():
        raise ValueError(f"module not found: {args.module}")
    if args.icon.stat().st_size != 64 * 64 * 2:
        raise ValueError("icon must be 64x64 RGB565")
    if args.tile.stat().st_size != 256 * 256 * 2:
        raise ValueError("tile must be 256x256 RGB565")
    if args.background is not None and args.background.stat().st_size not in (640 * 480 * 2, 512 * 256 * 2):
        raise ValueError("background must be 640x480 RGB565 primary or 512x256 legacy fallback")

    source = args.package_source
    if source.exists():
        shutil.rmtree(source)

    abi_tool = Path(__file__).resolve().parents[3] / "tools" / "check_titan_native_n32.py"
    subprocess.run([sys.executable, str(abi_tool), str(args.module)], check=True)

    package_tool = Path(__file__).resolve().parents[3] / "kernel" / "tools" / "titan_pkg.py"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        (source / "rom").mkdir(parents=True)
        (source / "data").mkdir()
        shutil.copyfile(args.module, source / "titaneye.tmod")
        shutil.copyfile(args.rom, source / "rom" / "GoldenEye 007.z64")
        shutil.copyfile(args.icon, source / "icon.rgb565")
        shutil.copyfile(args.tile, source / "tile.rgb565")
        if args.background is not None:
            shutil.copyfile(args.background, source / "background.rgb565")

        subprocess.run(
            [
                sys.executable, str(package_tool), "build",
                "--source", str(source), "--output", str(args.output),
                "--title-id", "TITANEYE007", "--title", "TitanEYE 007",
                "--version", "1.0.1", "--type", "titan-game",
                "--entrypoint", "titaneye.tmod", "--author", "Project Titan",
                "--description", "Native GoldenEye 007 port for Project Titan",
            ],
            check=True,
        )
    finally:
        # The retail ROM is private input, never durable source-tree state.
        # Keep only the final private .tpkg; remove the raw staging directory
        # on success and failure so source/release snapshots cannot ingest it.
        if source.exists():
            shutil.rmtree(source)

    print(f"ROM SHA-256: {hashlib.sha256(args.rom.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"titaneye-package: FAIL: {exc}") from exc
