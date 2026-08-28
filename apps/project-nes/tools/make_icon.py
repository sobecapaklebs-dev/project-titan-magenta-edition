#!/usr/bin/env python3
"""Create Project Nes 64x64 RGB565 tile icon."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "source" / "icon.rgb565"
W = H = 64
pixels = [[(16,14,24) for _ in range(W)] for _ in range(H)]
magenta=(222,43,82); white=(244,246,250)
# simple rounded-ish border
for y in range(3,61):
    for x in range(3,61):
        border = x in (3,4,5,58,59,60) or y in (3,4,5,58,59,60)
        if border and not ((x<8 and y<8) or (x>55 and y<8) or (x<8 and y>55) or (x>55 and y>55)):
            pixels[y][x]=magenta
font={
'P':[0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000],
'N':[0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001],
}
scale=3
for ch,ox in [('P',13),('N',36)]:
    for gy,row in enumerate(font[ch]):
        for gx in range(5):
            if row & (1<<(4-gx)):
                for yy in range(gy*scale,gy*scale+scale):
                    for xx in range(gx*scale,gx*scale+scale):
                        pixels[20+yy][ox+xx]=white
for y in range(46,50):
    for x in range(14,51): pixels[y][x]=magenta
buf=bytearray()
for row in pixels:
    for r,g,b in row:
        value=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
        buf.extend(struct.pack('<H',value))
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_bytes(buf)
print(f"created {OUT} ({len(buf)} bytes)")
