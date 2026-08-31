#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
project=root.parent

def req(c,m):
    if not c: raise SystemExit(m)

sdk=(root/'include/titan_game_sdk.h').read_text()
rt=(root/'src/services/titan_game_runtime.c').read_text()
make=(root/'Makefile').read_text()
video=(root/'src/graphics/titan_video.c').read_text()
input_h=(root/'src/platform/ps2/titan_input.h').read_text()
storage_h=(root/'src/services/titan_storage_scheduler.h').read_text()
adapter=(project/'ports/goldeneye/titan_ge_platform.c').read_text()
renderer=(project/'ports/goldeneye/mgb64/src/platform/fast3d/gfx_titan.c').read_text()
readme=(project/'ports/goldeneye/README.md').read_text()
math_h=(root/'src/graphics/titan_video_game_math.h').read_text()
texture_h=(root/'src/graphics/titan_video_game_texture.h').read_text()
texture_c=(root/'src/graphics/titan_video_game_texture.c').read_text()
software_video=(root/'src/graphics/backends/titan_video_software.inc').read_text()

req('#define TITAN_GAME_API_VERSION 2u' in sdk and 'TitanGameApiV1' in sdk, 'public TGR ABI missing')
req('TITAN_GAME_MAX_PADS 4u' in sdk and 'TitanGamePadState' in sdk, 'four-pad game ABI missing')
req('TITAN_GAME_HEAP_DEFAULT_BYTES (12u * 1024u * 1024u)' in rt, 'game heap was not separated from .tmod limits')
req('TITAN_GAME_MAX_ALLOCATIONS' not in rt and
    'TitanGameAllocationHeader' in rt and
    'TITAN_GAME_ALLOCATION_MAGIC' in rt and
    'header->bytes = bytes' in rt and
    'header->block = block' in rt and
    'block = header->block' in rt and
    'allocation_free_head' not in rt,
    'P162.5.4 fixed live-allocation ceiling was not removed')
req('TitanStorageScheduler_SetTitanGameExclusiveMode(1)' in rt and 'TitanStorageScheduler_IsTitanGameWorkerParked' in rt, 'single-owner game storage lane missing')
poll_start=rt.index('static void TitanGameApi_PollInput(void)')
poll_end=rt.index('static int TitanGameApi_GetPad(', poll_start)
req('TitanInput_Update();' not in rt[poll_start:poll_end] and
    'TitanInput_GetPadState(index)' in rt[poll_start:poll_end],
    'game worker must snapshot kernel input, never become a second input producer')
req('TitanVideo_BeginGameMode' in rt and 'TitanVideo_EndGameMode' in rt, 'GS game session lifecycle missing')
req('int TitanGameRuntime_End(void)' in rt and
    'TitanVideo_EndGameMode() != TITAN_RESULT_OK' in rt and
    rt.index('TitanVideo_EndGameMode() != TITAN_RESULT_OK') < rt.index('TitanStorageScheduler_SetTitanGameExclusiveMode(0)', rt.index('int TitanGameRuntime_End(void)')),
    'game teardown can release shell/storage ownership after failed video/VIF teardown')
req('teardown_pending' in rt and '*api = 0;' in rt and
    'g_game.teardown_pending = 1;' in rt and
    'g_game.active || g_game.teardown_pending' in rt,
    'failed game begin cannot preserve unpublished video/VIF teardown ownership')
req('GS_PSMZ_16' in video and 'game-zbuffer' in video, 'dedicated Z16 game surface missing')
req('TitanWaveVu1_RebindVideoResources' in video, 'shell VU1 resources are not restored after game exit')
req('TITAN_INPUT_MAX_PADS 4u' in input_h and 'TitanInput_GetPadState' in input_h, 'kernel input API was not extended for games')
req('SetTitanGameExclusiveMode' in storage_h, 'storage scheduler public game reservation missing')
req('titan_game_runtime.o' in make, 'TGR not linked into kernel')
req('TitanGePlatform_Attach' in adapter and 'video_draw_indexed_triangles' in adapter, 'GoldenEye platform adapter missing')
req('does **not** emulate the N64' in readme and 'GBI commands' in readme, 'GoldenEye port boundary undocumented')
req('TITAN_GAME_FEATURE_TEXTURED_TRIANGLES' in sdk and 'video_release_texture' in sdk and
    'TitanVideo_DrawGameTexturedTrianglePhysical' in rt and
    'TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES (1024u * 1024u)' in
        texture_h and
    'TITAN_VIDEO_GAME_TEXTURE_RETIRED' in
        texture_c,
    'bounded textured-triangle backend missing')
req('game-texture-arena' in video and 'game-tex-slot' not in video and
    'TitanVideoGameTexturePool_ReclaimRetired' in video and
    'TitanVideo_SubmitPacket(1)' in video and
    'packet2_get_qw_count(g_video.draw_packet) == 0u && !finish_frame' in video,
    'single-arena deferred texture lifetime contract missing')
# Alpha lane contract.
#
# Legacy TitanGePlatform selected RGBA32 only after an adapter-side
# has_intermediate_alpha scan.  P160 moves the format decision behind the
# negotiated RGBA32 ABI so the decoded Fast3D pixels go directly to the kernel,
# avoiding a temporary heap allocation/copy.  Accept either implementation,
# but the direct bridge is valid only when the kernel RGBA entry explicitly
# preserves full alpha via exact CLUT or PSM32 and never silently A1-collapses
# the RGBA API to PSM16.
rgba_video_start=video.index('TitanResult TitanVideo_GameUploadTextureRgba32(')
rgba_video_end=video.index('int TitanVideo_GameReleaseTexture(',rgba_video_start)
rgba_video=video[rgba_video_start:rgba_video_end]
adapter_rgba_start=adapter.index('int TitanGePlatform_UploadTextureRgba32(')
adapter_rgba_end=adapter.index('int TitanGePlatform_ReleaseTexture(',adapter_rgba_start)
adapter_rgba=adapter[adapter_rgba_start:adapter_rgba_end]
legacy_alpha_lane='has_intermediate_alpha' in adapter
p160_direct_alpha_lane=(
    'P10.59 direct texture bridge' in adapter_rgba and
    'video_upload_texture_rgba32' in adapter_rgba and
    'P160.3 alpha contract restoration' in rgba_video and
    'TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32' in rgba_video and
    'TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT' in rgba_video and
    'TitanPikaVr_BuildExactPaletteRgba32' in rgba_video and
    'use_psm16' not in rgba_video
)
req('TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32' in sdk and
    'video_upload_texture_rgba32' in sdk and
    'TitanVideo_GameUploadTextureRgba32' in rt and
    'GS_SET_TEXA(0u, 0u, 0x80u)' in video and
    'TitanVideo_ToGsAlpha(state->alpha_threshold)' in video and
    'TitanVideoGame_PackPsm32' in video and
    (legacy_alpha_lane or p160_direct_alpha_lane) and
    'TitanVideo_GameUploadTextureRgba32' in software_video,
    'alpha-preserving RGBA32 lane or PSM16 TEXA contract missing')
req('TitanVideoGame_MapClipVertex' in rt and
    'viewport_width' in math_h and 'viewport_height' in math_h and
    'TitanVideo_GameAppendMaterialState(0, 0)' in video and
    'TitanVideo_GameAppendMaterialState(game_texture, 0)' in video,
    'partial viewport/split-screen mapping is not shared by TGR paths')
req('TitanVideoGame_PrepareSt' in video and
    'TitanVideoGame_PackSt' in math_h and
    'TitanVideoGame_PackRgba' in math_h and
    'TitanVideoGame_PackXyz2' in math_h,
    'ST/Q or packed GS lane validation helper missing')
req('TITAN_TEXTURE_CAPACITY 2048u' in renderer and
    's_texture_live' in renderer and 'titan_delete_texture' in renderer,
    'GoldenEye logical texture IDs are not reusable')
print('titan-game-runtime-contract: OK')
