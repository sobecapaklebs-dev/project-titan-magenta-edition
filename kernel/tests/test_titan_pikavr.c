#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "titan_pikavr.h"
#include "titan_video_game_math.h"
#include "titan_diagnostics.h"

static uint32_t g_diag_events;
static uint32_t g_diag_pikavr_events;

void TitanDiagnostics_Event(TitanDiagLevel level, TitanDiagSubsystem subsystem, uint16_t event, int32_t result, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    (void)level; (void)event; (void)result; (void)a; (void)b; (void)c; (void)d;
    g_diag_events++;
    if (subsystem == TITAN_DIAG_PIKAVR) g_diag_pikavr_events++;
}

static void test_vram_saved_is_current_not_refresh_accumulator(void)
{
    static const uint8_t rgba[16] = {
        1,2,3,255, 4,5,6,128, 7,8,9,64, 10,11,12,0
    };
    TitanPikaVrFingerprint fp;
    TitanPikaVrTelemetry telemetry;
    uint32_t handle;

    TitanPikaVr_Initialize(1024u * 1024u);
    TitanPikaVr_FingerprintRgba32(rgba, 2u, 2u, 8u, &fp);
    assert(!TitanPikaVr_TryReuseTexture(&fp, &handle));
    assert(TitanPikaVr_RegisterTexture(&fp, 17u, 512u, 256u, 1, 64u));
    TitanPikaVr_UpdateVramUsed(256u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.vram_saved == 256u);
    assert(telemetry.textures_palettized == 1u);
    assert(telemetry.clut_bytes == 64u);

    assert(TitanPikaVr_TryReuseTexture(&fp, &handle));
    assert(handle == 17u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.vram_saved == 768u);
    assert(telemetry.textures_deduplicated == 1u);

    /* A refresh pattern retains a new alias then drops the old one. Savings
     * must return to the one-resource value instead of accumulating forever. */
    assert(!TitanPikaVr_ReleaseTexture(handle));
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.vram_saved == 256u);
    assert(telemetry.textures_deduplicated == 0u);

    assert(TitanPikaVr_ReleaseTexture(handle));
    TitanPikaVr_UpdateVramUsed(0u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.vram_saved == 0u);
    assert(telemetry.textures_resident == 0u);
    /* Logical release alone is not an eviction: the pool may keep the
     * allocation RETIRED until FINISH. */
    assert(telemetry.textures_evicted == 0u);
    TitanPikaVr_RecordBackingReclaimed(1u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.textures_evicted == 1u);
}

static void test_sampler_key_distinguishes_point_and_linear(void)
{
    TitanGameSamplerState sampler;
    TitanPikaVrTelemetry telemetry;

    TitanPikaVr_Initialize(1024u);
    memset(&sampler, 0, sizeof(sampler));
    sampler.wrap_s = TITAN_GAME_WRAP_CLAMP;
    sampler.wrap_t = TITAN_GAME_WRAP_REPEAT;
    sampler.linear_filter = 0u;
    assert(!TitanPikaVr_RecordSamplerState(9u, 0u, &sampler));
    assert(TitanPikaVr_RecordSamplerState(9u, 0u, &sampler));
    sampler.linear_filter = 1u;
    assert(!TitanPikaVr_RecordSamplerState(9u, 0u, &sampler));
    assert(TitanPikaVr_RecordSamplerState(9u, 0u, &sampler));
    sampler.wrap_t = TITAN_GAME_WRAP_CLAMP;
    assert(!TitanPikaVr_RecordSamplerState(9u, 0u, &sampler));

    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.sampler_state_hits == 2u);
    assert(telemetry.sampler_state_misses == 3u);
}

static void test_reference_path_disables_optimization(void)
{
    static const uint16_t pixels[4] = { 1u, 2u, 3u, 4u };
    TitanPikaVrFingerprint fp;
    TitanPikaVrTelemetry telemetry;
    uint32_t handle = 0u;

    TitanPikaVr_Initialize(4096u);
    TitanPikaVr_SetOptimizedPath(0);
    TitanPikaVr_FingerprintPsm16(pixels, 2u, 2u, 2u, &fp);
    assert(!TitanPikaVr_TryReuseTexture(&fp, &handle));
    assert(!TitanPikaVr_RegisterTexture(&fp, 1u, 256u, 256u, 0, 0u));
    assert(TitanPikaVr_ReleaseTexture(1u));
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.cache_hits == 0u);
    assert(telemetry.cache_misses == 0u);
    assert(telemetry.vram_saved == 0u);
}

static void test_renderer_counters_snapshot_real_work(void)
{
    TitanPikaVrTelemetry telemetry;

    TitanPikaVr_Initialize(4096u);
    TitanPikaVr_BeginRendererFrame();
    TitanPikaVr_RecordMaterialPass();
    TitanPikaVr_RecordDraw(1u);
    TitanPikaVr_RecordMaterialPass();
    TitanPikaVr_RecordDraw(1u);
    TitanPikaVr_RecordRibbonPath1Qwords(33u);
    TitanPikaVr_EndRendererFrame(77u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.draw_calls == 2u);
    assert(telemetry.primitives == 2u);
    assert(telemetry.material_passes == 2u);
    assert(telemetry.path3_qwords == 77u);
    assert(telemetry.ribbon_path1_qwords == 33u);
}


static uint32_t test_clut_storage_index(uint32_t index)
{
    return (index & ~0x18u) |
        ((index & 0x08u) << 1u) |
        ((index & 0x10u) >> 1u);
}

static void test_exact_palette_preserves_gs_color_and_alpha(void)
{
    static const uint8_t rgba[16] = {
        10u,20u,30u,0u,
        40u,50u,60u,127u,
        70u,80u,90u,128u,
        100u,110u,120u,255u
    };
    uint8_t indices[16];
    uint32_t clut[256];
    uint32_t column;

    memset(indices, 0xcc, sizeof(indices));
    memset(clut, 0xa5, sizeof(clut));
    assert(TitanPikaVr_BuildExactPaletteRgba32(
        rgba, 4u, 1u, 16u, indices, 16u, clut, 256u
    ));
    for (column = 0u; column < 4u; column++)
    {
        const uint8_t *pixel = &rgba[column * 4u];
        uint32_t packed = TitanVideoGame_PackPsm32(
            pixel[0], pixel[1], pixel[2], pixel[3]
        );
        assert(clut[test_clut_storage_index(indices[column])] == packed);
    }
    /* Creation-time conversion may write logical texels only. Padding belongs
     * to the caller's staging policy and must not be touched implicitly. */
    for (column = 4u; column < 16u; column++)
        assert(indices[column] == 0xccu);
}

static void test_exact_palette_rejects_more_than_256_gs_colors(void)
{
    uint8_t rgba[17u * 16u * 4u];
    uint8_t indices[32u * 16u];
    uint32_t clut[256];
    uint32_t index;

    memset(rgba, 0, sizeof(rgba));
    memset(indices, 0, sizeof(indices));
    for (index = 0u; index < 257u; index++)
    {
        rgba[index * 4u + 0u] = (uint8_t)(index & 255u);
        rgba[index * 4u + 1u] = (uint8_t)(index >> 8u);
        rgba[index * 4u + 2u] = 33u;
        rgba[index * 4u + 3u] = 255u;
    }
    assert(!TitanPikaVr_BuildExactPaletteRgba32(
        rgba, 17u, 16u, 17u * 4u, indices, 32u, clut, 256u
    ));
}

static void test_vram_total_and_texture_budget_are_separate(void)
{
    TitanPikaVrTelemetry telemetry;

    TitanPikaVr_Initialize(4u * 1024u * 1024u);
    TitanPikaVr_SetTextureBudget(1024u * 1024u);
    TitanPikaVr_UpdateVramUsed(3u * 1024u * 1024u);
    TitanPikaVr_UpdateTextureUsed(256u * 1024u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.vram_total == 4u * 1024u * 1024u);
    assert(telemetry.vram_used == 3u * 1024u * 1024u);
    assert(telemetry.vram_peak == 3u * 1024u * 1024u);
    assert(telemetry.texture_budget == 1024u * 1024u);
    assert(telemetry.texture_used == 256u * 1024u);

    TitanPikaVr_UpdateTextureUsed(2u * 1024u * 1024u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.texture_used == telemetry.texture_budget);
}

static void test_eviction_counts_only_backing_reclaim(void)
{
    TitanPikaVrTelemetry telemetry;

    TitanPikaVr_Initialize(4096u);
    TitanPikaVr_RecordBackingReclaimed(2u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.textures_evicted == 2u);
    TitanPikaVr_ResetTextures();
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.textures_evicted == 2u);
    TitanPikaVr_RecordBackingReclaimed(3u);
    TitanPikaVr_GetTelemetry(&telemetry);
    assert(telemetry.textures_evicted == 5u);
}

static void test_diagnostics_are_creation_time_not_draw_time(void)
{
    uint32_t before;

    g_diag_events = 0u;
    g_diag_pikavr_events = 0u;
    TitanPikaVr_Initialize(4096u);
    assert(g_diag_pikavr_events == 1u);
    before = g_diag_events;
    TitanPikaVr_BeginRendererFrame();
    TitanPikaVr_RecordMaterialPass();
    TitanPikaVr_RecordDraw(3u);
    TitanPikaVr_RecordRibbonPath1Qwords(8u);
    TitanPikaVr_EndRendererFrame(12u);
    assert(g_diag_events == before);
}

int main(void)
{
    test_diagnostics_are_creation_time_not_draw_time();
    test_vram_saved_is_current_not_refresh_accumulator();
    test_sampler_key_distinguishes_point_and_linear();
    test_reference_path_disables_optimization();
    test_renderer_counters_snapshot_real_work();
    test_exact_palette_preserves_gs_color_and_alpha();
    test_exact_palette_rejects_more_than_256_gs_colors();
    test_vram_total_and_texture_budget_are_separate();
    test_eviction_counts_only_backing_reclaim();
    puts("titan-pikavr: OK savings, sampler, lossless CLUT/fallback, VRAM budget, telemetry");
    return 0;
}
