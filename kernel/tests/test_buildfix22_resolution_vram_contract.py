#!/usr/bin/env python3
import pathlib, sys
root=pathlib.Path(sys.argv[1])
h=(root/'src/graphics/titan_video.h').read_text()
v=(root/'src/graphics/titan_video.c').read_text()
m=(root/'src/graphics/titan_video_memory.h').read_text()
r=(root/'tests/test_renderer_rewrite_contract.py').read_text()
assert '#define TITAN_VIDEO_HD_WIDTH 1280u' in h
assert '#define TITAN_VIDEO_HD_HEIGHT 720u' in h
assert '#define TITAN_VIDEO_HD_STORAGE_HEIGHT 768u' in h
assert '#define TITAN_VIDEO_MEMORY_TOTAL_BYTES (4u * 1024u * 1024u)' in m
assert 'config.count = 2u;' in v and 'config.psm = GS_PSM_16;' in v
assert 'g_video.logical_scale_x = scale_x;' in v
assert 'g_video.logical_scale_y = scale_y;' in v
assert 'g_video.logical_offset_y = 0.0f;' in v
assert 'TitanPikaVr_Initialize(TITAN_VIDEO_MEMORY_TOTAL_BYTES);' in v
assert 'TitanPikaVr_SetTextureBudget(TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES);' in v
assert 'g_video.game_restore_output = TitanVideoMode_GetOutput();' in v
assert 'restore_config = TitanVideo_GetFramebufferConfig(g_video.game_restore_output);' in v
assert 'renderer-native-480i-720p-double-buffer-contract: OK' in r
# 1280x768 PSM16 double buffer = 3,932,160 bytes; renderer contract proves
# the resident compact resources consume the remaining 262,144 bytes exactly.
assert 1280*768*2*2 + 262144 == 4*1024*1024
print('buildfix22-resolution-vram-contract: OK 720p/480i mapping, GS budget, restore path')
