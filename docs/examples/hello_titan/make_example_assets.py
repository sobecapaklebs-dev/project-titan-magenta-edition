#!/usr/bin/env python3
"""Create the example's 64x64 RGB565 tile icon without external dependencies."""

from pathlib import Path
import struct

SIZE = 64
OUT = Path(__file__).resolve().parent / "build" / "source" / "icon.rgb565"
OUT.parent.mkdir(parents=True, exist_ok=True)

pixels = bytearray()
for y in range(SIZE):
    for x in range(SIZE):
        # Dark tile, magenta inner frame, light T mark.
        r, g, b = 22, 20, 30
        if 6 <= x < 58 and 6 <= y < 58 and (x < 10 or x >= 54 or y < 10 or y >= 54):
            r, g, b = 222, 43, 82
        if (17 <= y < 24 and 18 <= x < 46) or (29 <= x < 36 and 18 <= y < 48):
            r, g, b = 240, 242, 248
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        pixels.extend(struct.pack("<H", value))
OUT.write_bytes(pixels)
print(f"created {OUT} ({len(pixels)} bytes)")
