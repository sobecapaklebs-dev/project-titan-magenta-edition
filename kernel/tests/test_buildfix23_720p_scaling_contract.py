#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
v=(root/'src/graphics/titan_video.c').read_text()
m=(root/'src/graphics/titan_video_mode.c').read_text()
h=(root/'src/graphics/titan_video.h').read_text()
for token in (
    'TitanVideo_ScaleOptimized720pToOutput',
    'g_video.composition_framebuffer.width != 960u',
    'g_video.composition_framebuffer.height != 540u',
    'g_video.framebuffers[0].width != 1280u',
    'g_video.framebuffers[0].height != 720u',
    'lod.mag_filter = LOD_MAG_LINEAR;',
    'lod.min_filter = LOD_MIN_LINEAR;',
    'rect.t0.u = 0.5f;', 'rect.t0.v = 0.5f;',
    'rect.t1.u = 959.5f;', 'rect.t1.v = 539.5f;',
    'GS_SET_CLAMP(1u, 1u, 0u, 959u, 0u, 539u)',
    'TitanVideo_GetActiveDrawFramebuffer()',
    'TitanVideoMode_Present(g_video.framebuffers[0].address);'):
    assert token in v, token
assert '(count == 1u || count == 2u)' in m
for token in ('TitanVideo_Set720pPath', 'TitanVideo_GetInternalWidth', 'TitanVideo_GetOutputWidth'):
    assert token in h
# BF30 hardware acceptance promoted the 960x540 composition path to the
# release/default 720p policy. Physical output remains 1280x720.
assert 'g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in v
# Game material sampler semantics remain independently selected POINT/LINEAR.
assert 'state->sampler[sampler_index].linear_filter' in v
assert 'LOD_MAG_NEAREST' in v and 'LOD_MAG_LINEAR' in v
print('pikavr-720p-scaling-contract: OK deterministic LINEAR composition scale, clamp, half-texel bounds, sampler semantics preserved')
