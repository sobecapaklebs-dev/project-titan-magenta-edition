#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/graphics/titan_pikavr.h').read_text()
c=(root/'src/graphics/titan_pikavr.c').read_text()
for f in ('vram_framebuffers','vram_textures','vram_clut','vram_theme','vram_transient','vram_alignment','vram_reserved','vram_free'):
    assert f in h, f+' missing'
assert 'TitanPikaVr_SetVramAccounting' in h and 'TitanPikaVr_SetVramAccounting' in c
assert 'used + reserved_headroom' in c
assert 'vram_free' in c
print('pikaVR-720p-vram-headroom-contract: OK explicit reserved headroom excluded from free budget')
