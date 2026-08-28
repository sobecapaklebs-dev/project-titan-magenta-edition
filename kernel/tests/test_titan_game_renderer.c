#include "titan_video_game_math.h"
#include "titan_video_game_texture.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int nearly_equal(float left, float right)
{
    return fabsf(left - right) < 0.0001f;
}

static void test_alpha_and_packing(void)
{
    TitanVideoGamePackedQword packed;

    assert(TitanVideoGame_ToGsAlpha(0u) == 0u);
    assert(TitanVideoGame_ToGsAlpha(127u) == 64u);
    assert(TitanVideoGame_ToGsAlpha(128u) == 64u);
    assert(TitanVideoGame_ToGsAlpha(255u) == 128u);
    assert(TitanVideoGame_PackPsm32(0xffu,0x11u,0x22u,0x7fu) ==
        0x402211ffu);

    packed = TitanVideoGame_PackSt(1.0f, -2.0f, 0.5f);
    assert(packed.lane[0] == 0x3f800000u);
    assert(packed.lane[1] == 0xc0000000u);
    assert(packed.lane[2] == 0x3f000000u);
    assert(packed.lane[3] == 0u);

    packed = TitanVideoGame_PackRgba(0x12u, 0x34u, 0x56u, 0xffu);
    assert(packed.lane[0] == 0x12u);
    assert(packed.lane[1] == 0x34u);
    assert(packed.lane[2] == 0x56u);
    assert(packed.lane[3] == 0x80u);

    packed = TitanVideoGame_PackXyz2(0.0f, 448.0f, 0x12345678u);
    assert(packed.lane[0] == 0x8000u);
    assert(packed.lane[1] == 0x9c00u);
    assert(packed.lane[2] == 0x12345678u);
    assert(packed.lane[3] == 0u);
    assert(TitanVideoGame_XyFixed(-2048.0f) == 0u);
    assert(TitanVideoGame_XyFixed(2048.0f) == 65535u);
}

static void test_viewport_mapping(void)
{
    TitanGameRenderState state;
    TitanGameRenderState normalized;
    TitanVideoGameScreenVertex output;

    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    state.viewport_x = 320;
    state.viewport_y = 0;
    state.viewport_width = 320u;
    state.viewport_height = 224u;

    state.scissor_x = 600;
    state.scissor_y = 400;
    state.scissor_width = 100u;
    state.scissor_height = 100u;
    assert(TitanVideoGame_NormalizeRenderState(
        &state, 640u, 448u, &normalized
    ));
    assert(normalized.scissor_width == 40u);
    assert(normalized.scissor_height == 48u);

    assert(TitanVideoGame_MapClipVertex(
        &state, -1.0f, 1.0f, -1.0f, 1.0f, &output
    ));
    assert(nearly_equal(output.x, 320.0f));
    assert(nearly_equal(output.y, 0.0f));
    assert(output.z == 65535u);
    assert(nearly_equal(output.reciprocal_w, 1.0f));

    assert(TitanVideoGame_MapClipVertex(
        &state, 1.0f, -1.0f, 1.0f, 1.0f, &output
    ));
    assert(nearly_equal(output.x, 640.0f));
    assert(nearly_equal(output.y, 224.0f));
    assert(output.z == 0u);

    state.viewport_y = 224;
    assert(TitanVideoGame_MapClipVertex(
        &state, 0.0f, 0.0f, 0.0f, 1.0f, &output
    ));
    assert(nearly_equal(output.x, 480.0f));
    assert(nearly_equal(output.y, 336.0f));
    assert(output.z == 32768u);

    assert(nearly_equal(TitanVideoGame_ResolveQ(0.25f, 0.5f), 0.25f));
    assert(nearly_equal(TitanVideoGame_ResolveQ(0.0f, 0.5f), 0.5f));
    assert(nearly_equal(TitanVideoGame_ResolveQ(NAN, 0.5f), 0.5f));
    assert(nearly_equal(
        TitanVideoGame_PrepareSt(0.75f, 32u, 0.25f) / 0.25f,
        0.75f
    ));
    assert(nearly_equal(
        (TitanVideoGame_PrepareSt(1.0f, 5u, 1.0f) / 1.0f) * 8.0f,
        5.0f
    ));
}

static void test_texture_arena(void)
{
    TitanVideoGameTexturePool pool;
    TitanVideoGameTexture *texture;
    uint32_t handles[256];
    uint32_t replacement;
    uint32_t index;

    TitanVideoGameTexturePool_Initialize(&pool);
    assert(TitanVideoGameTexturePool_GetBufferWidth(5u) == 64u);
    assert(TitanVideoGameTexturePool_GetBufferWidth(65u) == 128u);
    assert(TitanVideoGameTexturePool_GetStorageBytes(5u, 12u) == 2048u);
    assert(TitanVideoGameTexturePool_GetStorageBytes(65u, 9u) == 4096u);
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) ==
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES);

    for (index = 0u; index < 32u; index++)
    {
        assert(TitanVideoGameTexturePool_Allocate(
            &pool, 128u, 128u, &handles[index], &texture
        ));
        assert(texture->storage_bytes == 32768u);
    }
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) == 0u);
    assert(!TitanVideoGameTexturePool_Allocate(
        &pool, 1u, 1u, &replacement, &texture
    ));

    assert(TitanVideoGameTexturePool_Release(&pool, handles[0], 1));
    assert(TitanVideoGameTexturePool_Lookup(&pool, handles[0]) == 0);
    assert(TitanVideoGameTexturePool_GetAllocatedTextureCount(&pool) == 32u);
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) == 0u);
    assert(!TitanVideoGameTexturePool_Release(&pool, handles[0], 1));
    assert(TitanVideoGameTexturePool_ReclaimRetiredCount(&pool) == 1u);
    assert(TitanVideoGameTexturePool_GetAllocatedTextureCount(&pool) == 31u);
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) == 32768u);
    assert(TitanVideoGameTexturePool_Allocate(
        &pool, 128u, 128u, &replacement, &texture
    ));
    assert(replacement != handles[0]);
    assert(TitanVideoGameTexturePool_Lookup(&pool, handles[0]) == 0);

    TitanVideoGameTexturePool_Reset(&pool);
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) ==
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES);
    assert(TitanVideoGameTexturePool_GetStorageBytesForFormat(
        32u, 32u, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32
    ) == 8192u);
    assert(TitanVideoGameTexturePool_AllocateFormat(
        &pool, 32u, 32u, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32,
        &replacement, &texture
    ));
    assert(texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32);
    assert(texture->storage_bytes == 8192u);
    assert(TitanVideoGameTexturePool_Release(&pool, replacement, 0));
    for (index = 0u; index < 256u; index++)
    {
        assert(TitanVideoGameTexturePool_Allocate(
            &pool, 32u, 32u, &handles[index], &texture
        ));
    }
    assert(!TitanVideoGameTexturePool_Allocate(
        &pool, 32u, 32u, &replacement, &texture
    ));
    assert(TitanVideoGameTexturePool_Release(&pool, handles[127], 0));
    assert(TitanVideoGameTexturePool_GetFreeBytes(&pool) == 4096u);
    assert(TitanVideoGameTexturePool_Allocate(
        &pool, 32u, 32u, &replacement, &texture
    ));
    assert(replacement != handles[127]);
}

int main(void)
{
    {
        TitanVideoGameTexturePool indexed_pool;
        TitanVideoGameTexture *indexed = 0;
        uint32_t indexed_handle = 0u;
        TitanVideoGameTexturePool_Initialize(&indexed_pool);
        assert(TitanVideoGameTexturePool_GetBufferWidthForFormat(
            32u, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT) == 128u);
        assert(TitanVideoGameTexturePool_AllocateFormat(
            &indexed_pool, 32u, 16u,
            TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT,
            &indexed_handle, &indexed));
        assert(indexed != 0);
        assert(indexed->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT);
        assert(indexed->texture_bytes == 2048u);
        assert(indexed->clut_offset_bytes == indexed->texture_bytes);
        assert(indexed->clut_bytes == TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES);
        assert(indexed->storage_bytes == 3072u);
        assert(TitanVideoGameTexturePool_Release(&indexed_pool, indexed_handle, 0));

        /* P162.4 gunbarrel aggregate: one 440x299 I8 texture must fit the
         * 1 MiB native arena as PSM8+CLUT with GS-compatible TBW=512. */
        assert(TitanVideoGameTexturePool_GetBufferWidthForFormat(
            440u, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT) == 512u);
        assert(TitanVideoGameTexturePool_GetStorageBytesForFormat(
            440u, 299u, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT) == 156672u);
        assert(TitanVideoGameTexturePool_AllocateFormat(
            &indexed_pool, 440u, 299u,
            TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT,
            &indexed_handle, &indexed));
        assert(indexed != 0 && indexed->buffer_width == 512u);
        assert(indexed->texture_bytes == 155648u);
        assert(indexed->storage_bytes == 156672u);
        assert(TitanVideoGameTexturePool_Release(&indexed_pool, indexed_handle, 0));
    }
    test_alpha_and_packing();
    test_viewport_mapping();
    test_texture_arena();
    puts("titan-game-renderer: OK texture lifetime, alpha, viewport, ST/Q/GS packing");
    return 0;
}
