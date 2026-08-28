#!/usr/bin/env python3
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build'/'source'/'icon.rgb565'
W=H=64
bg=(14,15,22); white=(245,246,250); magenta=(222,43,82)
p=[[bg for _ in range(W)] for _ in range(H)]
# folder silhouette + wLE mark
for y in range(16,49):
    for x in range(9,55):
        if y>=22 or (y>=16 and 12<=x<31): p[y][x]=magenta
for y in range(25,45):
    for x in range(13,51): p[y][x]=(26,28,38)
font={
'w':[0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001],
'L':[0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111],
'E':[0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111],
}
scale=2
for ch,ox in [('w',16),('L',29),('E',41)]:
    for gy,row in enumerate(font[ch]):
        for gx in range(5):
            if row & (1<<(4-gx)):
                for yy in range(scale):
                    for xx in range(scale): p[28+gy*scale+yy][ox+gx*scale+xx]=white
b=bytearray()
for row in p:
    for r,g,bl in row:
        b.extend(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(bl>>3)))
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_bytes(b)
print(f'created {OUT} ({len(b)} bytes)')
