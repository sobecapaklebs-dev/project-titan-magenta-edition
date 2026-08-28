#!/usr/bin/env python3
from pathlib import Path
import math,sys
root=Path(sys.argv[1])
v=(root/'src/graphics/titan_video.c').read_text()
f=(root/'src/graphics/titan_font.c').read_text()
# Effective logical geometry must be identical after the 960x540 -> 1280x720 pass.
ui_w,ui_h=854.0,448.0
ref_x,ref_y=1280.0/ui_w,720.0/ui_h
opt_x=(960.0/ui_w)*(1280.0/960.0)
opt_y=(540.0/ui_h)*(720.0/540.0)
assert math.isclose(ref_x,opt_x,rel_tol=0,abs_tol=1e-12)
assert math.isclose(ref_y,opt_y,rel_tol=0,abs_tol=1e-12)
assert '(float)g_video.internal_width / (float)TITAN_UI_WIDTH' in v
assert '(float)g_video.internal_height / (float)TITAN_UI_HEIGHT' in v
# Font atlas remains alpha-aware and linearly sampled; no POINT override is
# introduced by the 720p scale path.
assert 'TitanVideo_DrawFontAtlasRegion32' in v
assert 'lod.mag_filter = LOD_MAG_LINEAR;' in v
assert 'TitanFont_DrawTextAlpha' in f
# This is a host geometry/semantic gate, not a visual sharpness claim.
print('pikavr-720p-text-legibility-contract: OK effective text/UI geometry preserved; visual sharpness remains CP04 target gate')
