#!/usr/bin/env python3
from pathlib import Path
import math, sys
root=Path(sys.argv[1])
h=(root/'src/graphics/titan_video.h').read_text()
v=(root/'src/graphics/titan_video.c').read_text()
m=(root/'src/graphics/titan_video_memory.h').read_text()
assert '#define TITAN_VIDEO_HD_WIDTH 1280u' in h
assert '#define TITAN_VIDEO_HD_HEIGHT 720u' in h
assert '#define TITAN_VIDEO_HD_STORAGE_HEIGHT 768u' in h
assert 'config.width = TITAN_VIDEO_HD_WIDTH;' in v
assert 'config.height = TITAN_VIDEO_HD_HEIGHT;' in v
assert 'config.storage_height = TITAN_VIDEO_HD_STORAGE_HEIGHT;' in v
assert 'config.count = 2u;' in v and 'config.psm = GS_PSM_16;' in v
assert '#define TITAN_VIDEO_MEMORY_TOTAL_BYTES (4u * 1024u * 1024u)' in m
# GS PSMCT16 page geometry: 64x64 pixels, 8192 bytes/page.
page_w,page_h,page_bytes=64,64,8192
visible_w,visible_h=1280,720
storage_h=math.ceil(visible_h/page_h)*page_h
assert storage_h == 768
pages_per_fb=(visible_w//page_w)*(storage_h//page_h)
assert pages_per_fb == 240
fb_bytes=pages_per_fb*page_bytes
assert fb_bytes == 1_966_080
framebuffers=2*fb_bytes
assert framebuffers == 3_932_160
compact_region=(4*1024*1024)-framebuffers
assert compact_region == 262_144
# Compact resources before the first page-aligned framebuffer:
# wave 128x32 PSM32 = 2 pages
# font 384x192 PSM8 = 9 pages
# UI 512x256 PSM8 = 16 pages
# CLUT 16x16 RGBA32 = 1024 bytes
# avatar scratch 256x64 PSM16 = 4 pages
payload=2*8192 + 9*8192 + 16*8192 + 1024 + 4*8192
assert payload == 254_976
assert compact_region-payload == 7_168  # page-alignment gap before framebuffer-0
print('pikavr-720p-reference-contract: OK 1280x720 visible, 1280x768 PSM16 GS-page storage')
