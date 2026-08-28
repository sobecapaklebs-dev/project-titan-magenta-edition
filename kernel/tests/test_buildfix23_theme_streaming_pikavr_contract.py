#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/services/titan_theme_scene_graph.h').read_text();c=(root/'src/services/titan_theme_scene_graph.c').read_text();ph=(root/'src/graphics/titan_pikavr.h').read_text();pc=(root/'src/graphics/titan_pikavr.c').read_text()
for f in ('vram_budget','ram_budget','prefetch_budget','upload_budget','reserved_headroom'):assert f in h
assert 'TITAN_THEME_STREAM_QUEUE_CAPACITY 16u' in h and 'TitanThemeStream_Request' in c
assert 'reserved_headroom' in c and 'end>total' in c
assert 'TitanThemeAtlas_ValidateRegion' in c and 'shared_clut_id' in h
assert 'TitanPikaVr_BuildExactPaletteRgba32' in ph
assert 'TitanPikaVr_TryReuseTexture' in ph and 'FingerprintEqual' in pc
assert 'filesystem' not in c.lower() and 'fopen' not in c and 'open(' not in c
print('theme-streaming-pikavr-contract: OK bounded nonblocking queue, reserved-headroom budget, atlas bounds, existing exact palette+dedupe')
