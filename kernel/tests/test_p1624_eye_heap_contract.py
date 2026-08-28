#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def need(cond, msg):
    if not cond:
        raise SystemExit('P1624_CONTRACT=FAIL ' + msg)

sdk = (root/'include/titan_game_sdk.h').read_text()
rt = (root/'src/services/titan_game_runtime.c').read_text()
video = (root/'src/graphics/titan_video.c').read_text()
video_h = (root/'src/graphics/titan_video.h').read_text()
tex_h = (root/'src/graphics/titan_video_game_texture.h').read_text()
tex_c = (root/'src/graphics/titan_video_game_texture.c').read_text()
title = (project/'ports/goldeneye/mgb64/src/game/title.c').read_text()
pc = (project/'ports/goldeneye/mgb64/src/platform/fast3d/gfx_pc.c').read_text()
gt = (project/'ports/goldeneye/mgb64/src/platform/fast3d/gfx_titan.c').read_text()
plat = (project/'ports/goldeneye/titan_ge_platform.c').read_text()
plat_h = (project/'ports/goldeneye/titan_ge_platform.h').read_text()

# ABI is append-only: v2 stays v2, a new feature bit announces the tail field,
# and old callers can continue to negotiate by struct_size.
need('#define TITAN_GAME_API_VERSION 2u' in sdk, 'TGR ABI version changed instead of append-only extension')
need('#define TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8     0x00001000u' in sdk,
     'I8 feature bit missing/changed')
need(sdk.index('video_upload_texture_rgba32') < sdk.index('video_upload_texture_i8'),
     'I8 callback is not appended after existing ABI tail')
need('TitanGameApi_VideoUploadTextureI8' in rt and
     'TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8' in rt and
     rt.index('TitanGameApi_VideoUploadTextureRgba32') < rt.index('TitanGameApi_VideoUploadTextureI8'),
     'runtime does not publish I8 lane append-only')
need('TITAN_GE_I8_API_BYTES' in plat and
     'api->struct_size < TITAN_GE_I8_API_BYTES' in plat and
     'TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8' in plat and
     'video_upload_texture_i8 == 0' in plat,
     'GoldenEye bridge does not negotiate I8 by feature+struct_size')
need('TitanGePlatform_UploadTextureI8' in plat_h and 'gfx_titan_upload_texture_i8' in gt,
     'I8 bridge is not wired through TitanEye renderer')

# Large dimensions are allowed only by indexed paths. Existing direct staging
# remains 128x128 so PSM16 can never overflow it.
need('#define TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH 512u' in tex_h and
     '#define TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT 320u' in tex_h and
     '#define TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH 128u' in tex_h and
     '#define TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT 128u' in tex_h,
     'large/indexed vs direct texture limits not separated')
psm16 = video[video.index('TitanResult TitanVideo_GameUploadTexturePsm16('):
              video.index('TitanResult TitanVideo_GameUploadTextureI8(')]
need('width > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH' in psm16 and
     'height > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT' in psm16,
     'PSM16 direct lane can overflow 128x128 staging')
i8 = video[video.index('TitanResult TitanVideo_GameUploadTextureI8('):
           video.index('TitanResult TitanVideo_GameUploadTextureRgba32(')]
need('width > TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH' in i8 and
     'height > TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT' in i8 and
     'g_video_game_texture_large_psm8' in i8 and
     'TitanVideo_GameWaitTextureUpload()' in i8 and
     'TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT' in i8,
     'large I8 lane missing bounds/fence/PSM8 backing')
rgba = video[video.index('TitanResult TitanVideo_GameUploadTextureRgba32('):
             video.index('int TitanVideo_GameReleaseTexture(')]
need('large_indexed_only' in rgba and
     'if (large_indexed_only && !use_palette)' in rgba and
     'return TITAN_ERROR_INVALID_ARGUMENT;' in rgba,
     'large RGBA can fall through to undersized PSM32 staging')
need('g_video_game_texture_large_psm8[' in video and
     'TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH *' in video and
     'TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT' in video,
     'single bounded large I8 scratch missing')
need('TITAN_VIDEO_GAME_I8_UPLOAD_REQUIRED_QWORDS' in video and
     'TitanVideoP1624I8PayloadFitsOneGifBlock' in video and
     'TitanVideoP1624UploadPacketCapacityProof' in video,
     'large I8 packet2 capacity is not compile-time proven')
need('if (format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)\n        return (width + 127u) & ~127u;' in tex_c,
     'PSM8 TBW does not round to 128-pixel GS units')
need('padded_height = (height + 15u) & ~15u;' in tex_c and
     'TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES' in tex_c,
     'PSM8 allocation/CLUT padding contract missing')

# GoldenEye gunbarrel eye aggregation. The source buffer is immutable during
# the intro and every original 440x1 strip must share one 440x299 cache entry.
need('gfx_register_intro_eye_i8_region(dword_CODE_bss_8006958C,' in title and
     '440u, 299u, 440u' in title and
     'gfx_unregister_intro_eye_i8_region' in title,
     'title does not register/unregister 440x299 eye lifetime')
for tok in ('struct GfxIntroEyeAggregateState', 's_p1624_intro_eye',
            'gfx_intro_eye_strip_info', 'p1624_eye_aggregate'):
    need(tok in pc, f'eye aggregate state missing: {tok}')
need('p1624_cache_key = (uintptr_t)s_p1624_intro_eye.base;' in pc and
     'p1624_source_key = (uintptr_t)s_p1624_intro_eye.base;' in pc and
     'uint64_t p154_content_signature = p1624_eye_aggregate ? 0u :' in pc,
     'eye strips do not collapse onto one static cache identity')
need('return gfx_titan_upload_texture_i8(' in pc and
     '(int)s_p1624_intro_eye.width' in pc and
     '(int)s_p1624_intro_eye.height' in pc and
     '(int)s_p1624_intro_eye.stride' in pc,
     'eye aggregate still expands through generic RGBA decode')
need('v += (float)p1624_eye_row;' in pc,
     'TEXRECT aggregate does not restore original source row in T')
need('tex_width[ti] = s_p1624_intro_eye.width;' in pc and
     'tex_height[ti] = s_p1624_intro_eye.height;' in pc,
     'material UV normalization does not use aggregate dimensions')

# Historical winding, not conventional winding, is the source of truth.
need('float n = q - p;' in pc and
     "GoldenEye's historical cull uses (v0-v1) x (v2-v1)" in pc,
     'homogeneous fast-cull sign is not historical GoldenEye sign')

# Chunk-backed game heap: only durable chunks call newlib malloc/free; game
# allocations split/coalesce inside those chunks while heap_used remains payload-only.
for tok in ('TITAN_GAME_HEAP_CHUNK_DEFAULT_BYTES (256u * 1024u)',
            'TITAN_GAME_HEAP_CHUNK_MIN_BYTES     (64u * 1024u)',
            'TITAN_GAME_HEAP_MAX_CHUNKS          128u',
            'g_p16241_runtime_tag',
            'TitanGameHeapChunk', 'TitanGameHeapBlock',
            'TitanGameRuntime_CreateHeapChunk', 'TitanGameRuntime_SplitHeapBlock',
            'TitanGameRuntime_CoalesceHeapBlock'):
    need(tok in rt, f'chunk heap component missing: {tok}')
alloc = rt[rt.index('static void *TitanGameApi_Alloc'):rt.index('static void TitanGameApi_Release')]
need('malloc(' not in alloc and 'free(' not in alloc and
     'TitanGameRuntime_FindHeapBlock' in alloc and 'TitanGameRuntime_CreateHeapChunk' in alloc,
     'per-allocation path still calls newlib directly')
release = rt[rt.index('static void TitanGameApi_Release'):rt.index('static void TitanGameRuntime_ReleaseAllAllocations')]
need('free(' not in release and 'TitanGameRuntime_CoalesceHeapBlock' in release,
     'per-release path still calls newlib or skips coalescing')
cleanup = rt[rt.index('static void TitanGameRuntime_ReleaseAllAllocations'):rt.index('static void TitanGameApi_RequestExit')]
need('free(g_game.heap_chunks[index].raw)' in cleanup,
     'session teardown does not return durable chunks to newlib')

print('P1624_EYE_HEAP_CONTRACT=PASS')
