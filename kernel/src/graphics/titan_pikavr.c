#include "titan_pikavr.h"
#include "titan_video_game_math.h"
#include "titan_diagnostics.h"
#include "titan_vlombiscore.h"

#include <limits.h>
#include <string.h>

#if defined(__GNUC__)
extern void TitanVlombisCore_RecordWork(uint32_t capability_mask, TitanVlombisEvent event, uint32_t work_units, uint32_t bytes_avoided) __attribute__((weak));
#endif

typedef struct TitanPikaVrTextureEntry
{
    TitanPikaVrFingerprint fingerprint;
    uint32_t handle;
    uint32_t reference_bytes;
    uint32_t resident_bytes;
    uint32_t refs;
    uint32_t clut_bytes;
    uint8_t active;
    uint8_t palettized;
    uint8_t reserved[2];
} TitanPikaVrTextureEntry;

typedef struct TitanPikaVrSamplerKey
{
    uint32_t handle;
    uint8_t sampler_index;
    uint8_t wrap_s;
    uint8_t wrap_t;
    uint8_t linear_filter;
    uint8_t valid;
} TitanPikaVrSamplerKey;

typedef struct TitanPikaVrState
{
    TitanPikaVrTextureEntry entries[TITAN_PIKAVR_TEXTURE_CAPACITY];
    TitanPikaVrSamplerKey sampler_key[2];
    TitanPikaVrTelemetry telemetry;
    uint32_t current_draw_calls;
    uint32_t current_primitives;
    uint32_t current_material_passes;
    uint32_t current_ribbon_path1_qwords;
    uint32_t vlombis_last_sampler_hits;
    uint32_t vlombis_last_vram_saved;
    int optimized_path;
} TitanPikaVrState;

static TitanPikaVrState g_pikavr;

static uint32_t g_pikavr_palette_keys[512];
static uint16_t g_pikavr_palette_values[512];
static uint8_t g_pikavr_palette_used[512];

static uint32_t TitanPikaVr_ClutStorageIndex(uint32_t index)
{
    return (index & ~0x18u) |
        ((index & 0x08u) << 1u) |
        ((index & 0x10u) >> 1u);
}

static uint64_t TitanPikaVr_MixA(uint64_t hash, uint8_t value)
{
    hash ^= (uint64_t)value;
    return hash * UINT64_C(1099511628211);
}

static uint64_t TitanPikaVr_MixB(uint64_t hash, uint8_t value)
{
    hash ^= (uint64_t)value + UINT64_C(0x9d);
    hash *= UINT64_C(0x100000001b3);
    hash ^= hash >> 29u;
    return hash;
}

static void TitanPikaVr_MixU32(
    uint64_t *hash_a,
    uint64_t *hash_b,
    uint32_t value
)
{
    uint32_t shift;
    for (shift = 0u; shift < 32u; shift += 8u)
    {
        uint8_t byte = (uint8_t)(value >> shift);
        *hash_a = TitanPikaVr_MixA(*hash_a, byte);
        *hash_b = TitanPikaVr_MixB(*hash_b, byte);
    }
}

static void TitanPikaVr_BeginFingerprint(
    uint32_t width,
    uint32_t height,
    uint8_t source_format,
    TitanPikaVrFingerprint *fingerprint
)
{
    uint64_t hash_a = UINT64_C(1469598103934665603);
    uint64_t hash_b = UINT64_C(7809847782465536322);

    memset(fingerprint, 0, sizeof(*fingerprint));
    TitanPikaVr_MixU32(&hash_a, &hash_b, width);
    TitanPikaVr_MixU32(&hash_a, &hash_b, height);
    hash_a = TitanPikaVr_MixA(hash_a, source_format);
    hash_b = TitanPikaVr_MixB(hash_b, source_format);
    fingerprint->hash_a = hash_a;
    fingerprint->hash_b = hash_b;
    fingerprint->width = width;
    fingerprint->height = height;
    fingerprint->source_format = source_format;
}

static int TitanPikaVr_FingerprintEqual(
    const TitanPikaVrFingerprint *left,
    const TitanPikaVrFingerprint *right
)
{
    return left->hash_a == right->hash_a &&
        left->hash_b == right->hash_b &&
        left->width == right->width &&
        left->height == right->height &&
        left->source_format == right->source_format;
}

static uint32_t TitanPikaVr_SaturateU32(uint64_t value)
{
    return value > UINT_MAX ? UINT_MAX : (uint32_t)value;
}

static void TitanPikaVr_RecalculateCurrentSavings(void)
{
    uint64_t saved = 0u;
    uint64_t clut_bytes = 0u;
    uint64_t palettized_saved = 0u;
    uint64_t dedupe_saved = 0u;
    uint32_t resident = 0u;
    uint32_t deduplicated = 0u;
    uint32_t palettized = 0u;
    uint32_t index;

    for (index = 0u; index < TITAN_PIKAVR_TEXTURE_CAPACITY; index++)
    {
        const TitanPikaVrTextureEntry *entry = &g_pikavr.entries[index];
        uint64_t reference_total;

        if (!entry->active || entry->refs == 0u)
            continue;
        resident++;
        if (entry->refs > 1u)
            deduplicated += entry->refs - 1u;
        if (entry->palettized)
        {
            palettized++;
            if (entry->reference_bytes > entry->resident_bytes)
                palettized_saved += entry->reference_bytes - entry->resident_bytes;
        }
        if (entry->refs > 1u)
            dedupe_saved += (uint64_t)entry->reference_bytes * (entry->refs - 1u);
        clut_bytes += entry->clut_bytes;

        reference_total = (uint64_t)entry->reference_bytes * entry->refs;
        if (reference_total > entry->resident_bytes)
            saved += reference_total - entry->resident_bytes;
    }

    g_pikavr.telemetry.vram_saved = TitanPikaVr_SaturateU32(saved);
    g_pikavr.telemetry.textures_resident = resident;
    g_pikavr.telemetry.textures_deduplicated = deduplicated;
    g_pikavr.telemetry.textures_palettized = palettized;
    g_pikavr.telemetry.clut_bytes = TitanPikaVr_SaturateU32(clut_bytes);
    g_pikavr.telemetry.palettized_bytes_saved = TitanPikaVr_SaturateU32(palettized_saved);
    g_pikavr.telemetry.dedupe_bytes_saved = TitanPikaVr_SaturateU32(dedupe_saved);
}

void TitanPikaVr_Initialize(uint32_t vram_total)
{
    memset(&g_pikavr, 0, sizeof(g_pikavr));
    g_pikavr.telemetry.vram_total = vram_total;
    g_pikavr.optimized_path = 1;
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_PIKAVR, 0x600u, 0, vram_total, 1u, 0u, 0u);
}

void TitanPikaVr_SetOptimizedPath(int enabled)
{
    int new_value = enabled != 0;
    if (g_pikavr.optimized_path != new_value)
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_PIKAVR, 0x601u, 0, (uint32_t)new_value, 0u, 0u, 0u);
    g_pikavr.optimized_path = new_value;
}

int TitanPikaVr_IsOptimizedPathActive(void)
{
    return g_pikavr.optimized_path;
}

void TitanPikaVr_FingerprintPsm16(
    const uint16_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    TitanPikaVrFingerprint *fingerprint
)
{
    uint32_t row;
    uint32_t column;

    if (fingerprint == 0)
        return;
    TitanPikaVr_BeginFingerprint(
        width, height, TITAN_PIKAVR_SOURCE_PSM16, fingerprint
    );
    if (pixels == 0 || stride < width)
        return;
    for (row = 0u; row < height; row++)
    {
        for (column = 0u; column < width; column++)
        {
            uint16_t value = pixels[row * stride + column];
            fingerprint->hash_a = TitanPikaVr_MixA(
                fingerprint->hash_a, (uint8_t)value
            );
            fingerprint->hash_b = TitanPikaVr_MixB(
                fingerprint->hash_b, (uint8_t)value
            );
            fingerprint->hash_a = TitanPikaVr_MixA(
                fingerprint->hash_a, (uint8_t)(value >> 8u)
            );
            fingerprint->hash_b = TitanPikaVr_MixB(
                fingerprint->hash_b, (uint8_t)(value >> 8u)
            );
        }
    }
}

void TitanPikaVr_FingerprintRgba32(
    const uint8_t *rgba,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes,
    TitanPikaVrFingerprint *fingerprint
)
{
    uint32_t row;
    uint32_t byte;

    if (fingerprint == 0)
        return;
    TitanPikaVr_BeginFingerprint(
        width, height, TITAN_PIKAVR_SOURCE_RGBA32, fingerprint
    );
    if (rgba == 0 || stride_bytes < width * 4u)
        return;
    for (row = 0u; row < height; row++)
    {
        const uint8_t *source = &rgba[row * stride_bytes];
        for (byte = 0u; byte < width * 4u; byte++)
        {
            fingerprint->hash_a = TitanPikaVr_MixA(
                fingerprint->hash_a, source[byte]
            );
            fingerprint->hash_b = TitanPikaVr_MixB(
                fingerprint->hash_b, source[byte]
            );
        }
    }
}

int TitanPikaVr_TryReuseTexture(
    const TitanPikaVrFingerprint *fingerprint,
    uint32_t *texture_handle
)
{
    uint32_t index;

    if (!g_pikavr.optimized_path || fingerprint == 0 || texture_handle == 0)
        return 0;
    for (index = 0u; index < TITAN_PIKAVR_TEXTURE_CAPACITY; index++)
    {
        TitanPikaVrTextureEntry *entry = &g_pikavr.entries[index];
        if (!entry->active || entry->refs == 0u ||
            !TitanPikaVr_FingerprintEqual(&entry->fingerprint, fingerprint))
            continue;
        if (entry->refs == UINT_MAX)
            return 0;
        entry->refs++;
        *texture_handle = entry->handle;
        g_pikavr.telemetry.cache_hits++;
        TitanPikaVr_RecalculateCurrentSavings();
        TitanDiagnostics_Event(TITAN_DIAG_DEBUG, TITAN_DIAG_PIKAVR, 0x602u, 0, entry->handle, entry->refs, entry->resident_bytes, entry->reference_bytes);
        return 1;
    }
    g_pikavr.telemetry.cache_misses++;
    return 0;
}

int TitanPikaVr_RegisterTexture(
    const TitanPikaVrFingerprint *fingerprint,
    uint32_t texture_handle,
    uint32_t reference_bytes,
    uint32_t resident_bytes,
    int palettized,
    uint32_t clut_bytes
)
{
    uint32_t index;
    TitanPikaVrTextureEntry *entry;

    if (!g_pikavr.optimized_path || fingerprint == 0 || texture_handle == 0u ||
        reference_bytes == 0u || resident_bytes == 0u)
        return 0;
    for (index = 0u; index < TITAN_PIKAVR_TEXTURE_CAPACITY; index++)
    {
        if (!g_pikavr.entries[index].active)
            break;
    }
    if (index == TITAN_PIKAVR_TEXTURE_CAPACITY)
        return 0;
    entry = &g_pikavr.entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->fingerprint = *fingerprint;
    entry->handle = texture_handle;
    entry->reference_bytes = reference_bytes;
    entry->resident_bytes = resident_bytes;
    entry->refs = 1u;
    entry->clut_bytes = clut_bytes;
    entry->active = 1u;
    entry->palettized = palettized != 0;
    TitanPikaVr_RecalculateCurrentSavings();
    TitanDiagnostics_Event(TITAN_DIAG_DEBUG, TITAN_DIAG_PIKAVR, 0x603u, 0, texture_handle, resident_bytes, reference_bytes, (palettized != 0) ? clut_bytes : 0u);
    return 1;
}

int TitanPikaVr_ReleaseTexture(uint32_t texture_handle)
{
    uint32_t index;

    if (!g_pikavr.optimized_path || texture_handle == 0u)
        return 1;
    for (index = 0u; index < TITAN_PIKAVR_TEXTURE_CAPACITY; index++)
    {
        TitanPikaVrTextureEntry *entry = &g_pikavr.entries[index];
        if (!entry->active || entry->handle != texture_handle)
            continue;
        if (entry->refs > 1u)
        {
            entry->refs--;
            TitanPikaVr_RecalculateCurrentSavings();
            return 0;
        }
        memset(entry, 0, sizeof(*entry));
        TitanPikaVr_RecalculateCurrentSavings();
        return 1;
    }
    return 1;
}

void TitanPikaVr_ResetTextures(void)
{
    /* Reset is a logical registry operation. Backing storage may still be
     * RETIRED and in-flight until FINISH, so eviction telemetry is updated
     * only by TitanPikaVr_RecordBackingReclaimed(). */
    memset(g_pikavr.entries, 0, sizeof(g_pikavr.entries));
    memset(g_pikavr.sampler_key, 0, sizeof(g_pikavr.sampler_key));
    TitanPikaVr_RecalculateCurrentSavings();
}

void TitanPikaVr_RecordBackingReclaimed(uint32_t count)
{
    uint64_t total = (uint64_t)g_pikavr.telemetry.textures_evicted + count;
    g_pikavr.telemetry.textures_evicted = TitanPikaVr_SaturateU32(total);
    if (count != 0u)
        TitanDiagnostics_Event(TITAN_DIAG_DEBUG, TITAN_DIAG_PIKAVR, 0x605u, 0, count, g_pikavr.telemetry.textures_evicted, g_pikavr.telemetry.texture_used, g_pikavr.telemetry.vram_used);
}

void TitanPikaVr_UpdateVramUsed(uint32_t used_bytes)
{
    uint64_t unavailable;
    if (used_bytes > g_pikavr.telemetry.vram_total)
        used_bytes = g_pikavr.telemetry.vram_total;
    g_pikavr.telemetry.vram_used = used_bytes;
    if (used_bytes > g_pikavr.telemetry.vram_peak)
        g_pikavr.telemetry.vram_peak = used_bytes;
    unavailable = (uint64_t)used_bytes + g_pikavr.telemetry.vram_reserved;
    g_pikavr.telemetry.vram_free = unavailable < g_pikavr.telemetry.vram_total
        ? g_pikavr.telemetry.vram_total - (uint32_t)unavailable
        : 0u;
}

void TitanPikaVr_SetVramAccounting(
    uint32_t framebuffers, uint32_t textures, uint32_t clut,
    uint32_t theme, uint32_t transient, uint32_t alignment,
    uint32_t reserved_headroom
)
{
    uint64_t used;
    uint64_t unavailable;
    g_pikavr.telemetry.vram_framebuffers = framebuffers;
    g_pikavr.telemetry.vram_textures = textures;
    g_pikavr.telemetry.vram_clut = clut;
    g_pikavr.telemetry.vram_theme = theme;
    g_pikavr.telemetry.vram_transient = transient;
    g_pikavr.telemetry.vram_alignment = alignment;
    g_pikavr.telemetry.vram_reserved = reserved_headroom;
    used = (uint64_t)framebuffers + textures + clut + theme + transient + alignment;
    unavailable = used + reserved_headroom;
    g_pikavr.telemetry.vram_used = TitanPikaVr_SaturateU32(used);
    if (g_pikavr.telemetry.vram_used > g_pikavr.telemetry.vram_peak)
        g_pikavr.telemetry.vram_peak = g_pikavr.telemetry.vram_used;
    g_pikavr.telemetry.vram_free = unavailable < g_pikavr.telemetry.vram_total
        ? g_pikavr.telemetry.vram_total - (uint32_t)unavailable : 0u;
}

void TitanPikaVr_SetTextureBudget(uint32_t budget_bytes)
{
    g_pikavr.telemetry.texture_budget = budget_bytes;
    if (g_pikavr.telemetry.texture_used > budget_bytes)
        g_pikavr.telemetry.texture_used = budget_bytes;
}

void TitanPikaVr_UpdateTextureUsed(uint32_t used_bytes)
{
    if (g_pikavr.telemetry.texture_budget != 0u &&
        used_bytes > g_pikavr.telemetry.texture_budget)
        used_bytes = g_pikavr.telemetry.texture_budget;
    g_pikavr.telemetry.texture_used = used_bytes;
}

int TitanPikaVr_BuildExactPaletteRgba32(
    const uint8_t *rgba,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes,
    uint8_t *indices,
    uint32_t index_stride,
    uint32_t *clut,
    uint32_t clut_entries
)
{
    uint32_t palette[256];
    uint32_t palette_count = 0u;
    uint32_t row;
    uint32_t column;

    if (rgba == 0 || indices == 0 || clut == 0 || width == 0u ||
        height == 0u || stride_bytes < width * 4u || index_stride < width ||
        clut_entries < 256u)
        return 0;

    memset(g_pikavr_palette_used, 0, sizeof(g_pikavr_palette_used));
    memset(clut, 0, 256u * sizeof(clut[0]));

    for (row = 0u; row < height; row++)
    {
        for (column = 0u; column < width; column++)
        {
            const uint8_t *source = &rgba[row * stride_bytes + column * 4u];
            uint32_t color = TitanVideoGame_PackPsm32(
                source[0], source[1], source[2], source[3]
            );
            uint32_t slot = (color * 2654435761u) >> 23u;
            uint32_t palette_index;

            while (g_pikavr_palette_used[slot])
            {
                if (g_pikavr_palette_keys[slot] == color)
                    break;
                slot = (slot + 1u) & 511u;
            }
            if (!g_pikavr_palette_used[slot])
            {
                if (palette_count >= 256u)
                {
                    TitanDiagnostics_Event(TITAN_DIAG_DEBUG, TITAN_DIAG_PIKAVR, 0x604u, 1, width, height, palette_count, 0u);
                    return 0;
                }
                g_pikavr_palette_used[slot] = 1u;
                g_pikavr_palette_keys[slot] = color;
                g_pikavr_palette_values[slot] = (uint16_t)palette_count;
                palette[palette_count] = color;
                palette_count++;
            }
            palette_index = g_pikavr_palette_values[slot];
            indices[row * index_stride + column] = (uint8_t)palette_index;
        }
    }

    for (column = 0u; column < palette_count; column++)
        clut[TitanPikaVr_ClutStorageIndex(column)] = palette[column];
    TitanDiagnostics_Event(TITAN_DIAG_DEBUG, TITAN_DIAG_PIKAVR, 0x604u, 0, width, height, palette_count, palette_count * 4u);
    return 1;
}

int TitanPikaVr_RecordSamplerState(
    uint32_t texture_handle,
    uint32_t sampler_index,
    const TitanGameSamplerState *sampler
)
{
    TitanPikaVrSamplerKey *key;

    if (!g_pikavr.optimized_path || sampler == 0 || sampler_index >= 2u)
        return 0;
    key = &g_pikavr.sampler_key[sampler_index];
    if (key->valid && key->handle == texture_handle &&
        key->sampler_index == sampler_index &&
        key->wrap_s == sampler->wrap_s &&
        key->wrap_t == sampler->wrap_t &&
        key->linear_filter == sampler->linear_filter)
    {
        g_pikavr.telemetry.sampler_state_hits++;
        return 1;
    }
    key->handle = texture_handle;
    key->sampler_index = (uint8_t)sampler_index;
    key->wrap_s = sampler->wrap_s;
    key->wrap_t = sampler->wrap_t;
    key->linear_filter = sampler->linear_filter;
    key->valid = 1u;
    g_pikavr.telemetry.sampler_state_misses++;
    return 0;
}

void TitanPikaVr_BeginRendererFrame(void)
{
    g_pikavr.current_draw_calls = 0u;
    g_pikavr.current_primitives = 0u;
    g_pikavr.current_material_passes = 0u;
    g_pikavr.current_ribbon_path1_qwords = 0u;
}

void TitanPikaVr_RecordDraw(uint32_t primitives)
{
    g_pikavr.current_draw_calls++;
    g_pikavr.current_primitives += primitives;
}

void TitanPikaVr_RecordMaterialPass(void)
{
    g_pikavr.current_material_passes++;
}

void TitanPikaVr_RecordRibbonPath1Qwords(uint32_t qwords)
{
    g_pikavr.current_ribbon_path1_qwords += qwords;
}

void TitanPikaVr_EndRendererFrame(uint32_t path3_qwords)
{
    uint32_t sampler_delta;
    uint32_t saved_delta;

    g_pikavr.telemetry.draw_calls = g_pikavr.current_draw_calls;
    g_pikavr.telemetry.primitives = g_pikavr.current_primitives;
    g_pikavr.telemetry.path3_qwords = path3_qwords;
    g_pikavr.telemetry.material_passes = g_pikavr.current_material_passes;
    g_pikavr.telemetry.ribbon_path1_qwords =
        g_pikavr.current_ribbon_path1_qwords;

    /* VlombisCore consumes aggregate deltas once per frame. Never instrument
     * each draw/state change: that would turn observability into renderer
     * overhead on the R5900. */
    sampler_delta = g_pikavr.telemetry.sampler_state_hits >= g_pikavr.vlombis_last_sampler_hits
        ? g_pikavr.telemetry.sampler_state_hits - g_pikavr.vlombis_last_sampler_hits
        : g_pikavr.telemetry.sampler_state_hits;
    saved_delta = g_pikavr.telemetry.vram_saved > g_pikavr.vlombis_last_vram_saved
        ? g_pikavr.telemetry.vram_saved - g_pikavr.vlombis_last_vram_saved
        : 0u;
#if defined(__GNUC__)
    if (TitanVlombisCore_RecordWork != 0)
    {
#endif
        if (sampler_delta != 0u)
            TitanVlombisCore_RecordWork(
                TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE | TITAN_VLOMBIS_CAP_PIKAVR,
                TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT, sampler_delta, 0u);
        if (g_pikavr.optimized_path && saved_delta != 0u)
            TitanVlombisCore_RecordWork(
                TITAN_VLOMBIS_CAP_PIKAVR | TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE,
                TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION, 1u, saved_delta);
#if defined(__GNUC__)
    }
#endif
    g_pikavr.vlombis_last_sampler_hits = g_pikavr.telemetry.sampler_state_hits;
    g_pikavr.vlombis_last_vram_saved = g_pikavr.telemetry.vram_saved;
}

void TitanPikaVr_GetTelemetry(TitanPikaVrTelemetry *telemetry)
{
    if (telemetry != 0)
        *telemetry = g_pikavr.telemetry;
}
