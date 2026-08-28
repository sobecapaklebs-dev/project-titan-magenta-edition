#ifndef PROJECT_TITAN_PIKAVR_H
#define PROJECT_TITAN_PIKAVR_H

#include <stdint.h>

#include "titan_game_sdk.h"

#define TITAN_PIKAVR_API_VERSION 1u

#define TITAN_PIKAVR_TEXTURE_CAPACITY 1024u

#define TITAN_PIKAVR_SOURCE_PSM16 1u
#define TITAN_PIKAVR_SOURCE_RGBA32 2u

typedef struct TitanPikaVrFingerprint
{
    uint64_t hash_a;
    uint64_t hash_b;
    uint32_t width;
    uint32_t height;
    uint8_t source_format;
    uint8_t reserved[3];
} TitanPikaVrFingerprint;

typedef struct TitanPikaVrTelemetry
{
    uint32_t vram_total;
    uint32_t vram_used;
    uint32_t vram_peak;
    uint32_t vram_saved;
    uint32_t texture_budget;
    uint32_t texture_used;
    uint32_t textures_resident;
    uint32_t textures_evicted;
    uint32_t textures_deduplicated;
    uint32_t textures_palettized;
    uint32_t clut_bytes;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t sampler_state_hits;
    uint32_t sampler_state_misses;
    uint32_t draw_calls;
    uint32_t primitives;
    uint32_t path3_qwords;
    uint32_t material_passes;
    uint32_t ribbon_path1_qwords;
    /* BuildFix23 CP04 append-only GS accounting. reserved_headroom is a
     * policy floor and is never counted as theme/UI budget. */
    uint32_t vram_framebuffers;
    uint32_t vram_textures;
    uint32_t vram_clut;
    uint32_t vram_theme;
    uint32_t vram_transient;
    uint32_t vram_alignment;
    uint32_t vram_reserved;
    uint32_t vram_free;
    uint32_t palettized_bytes_saved;
    uint32_t dedupe_bytes_saved;
} TitanPikaVrTelemetry;

void TitanPikaVr_Initialize(uint32_t vram_total);
void TitanPikaVr_SetOptimizedPath(int enabled);
int TitanPikaVr_IsOptimizedPathActive(void);

void TitanPikaVr_FingerprintPsm16(
    const uint16_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    TitanPikaVrFingerprint *fingerprint
);
void TitanPikaVr_FingerprintRgba32(
    const uint8_t *rgba,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes,
    TitanPikaVrFingerprint *fingerprint
);

/* A hit retains the existing logical texture reference.  Content residency is
 * sampler-neutral; POINT/LINEAR/wrap semantics are independently keyed by the
 * sampler-state cache below so sharing pixels can never inherit stale filters. */
int TitanPikaVr_TryReuseTexture(
    const TitanPikaVrFingerprint *fingerprint,
    uint32_t *texture_handle
);
int TitanPikaVr_RegisterTexture(
    const TitanPikaVrFingerprint *fingerprint,
    uint32_t texture_handle,
    uint32_t reference_bytes,
    uint32_t resident_bytes,
    int palettized,
    uint32_t clut_bytes
);

/* Returns 1 when the backing GS allocation has no logical aliases remaining
 * and may be released, 0 while another alias still owns it. Unknown handles
 * conservatively return 1 so pre-pikaVR resources retain legacy behavior. */
int TitanPikaVr_ReleaseTexture(uint32_t texture_handle);
void TitanPikaVr_ResetTextures(void);
void TitanPikaVr_RecordBackingReclaimed(uint32_t count);
void TitanPikaVr_UpdateVramUsed(uint32_t used_bytes);
void TitanPikaVr_SetVramAccounting(
    uint32_t framebuffers, uint32_t textures, uint32_t clut,
    uint32_t theme, uint32_t transient, uint32_t alignment,
    uint32_t reserved_headroom
);
void TitanPikaVr_SetTextureBudget(uint32_t budget_bytes);
void TitanPikaVr_UpdateTextureUsed(uint32_t used_bytes);

/* Lossless creation-time palette builder. Returns 0 when the source requires
 * more than 256 distinct GS colors or arguments are invalid. Alpha is packed
 * through the same TGR/GS conversion as the PSM32 reference path. */
int TitanPikaVr_BuildExactPaletteRgba32(
    const uint8_t *rgba,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes,
    uint8_t *indices,
    uint32_t index_stride,
    uint32_t *clut,
    uint32_t clut_entries
);

/* Returns 1 for an exact sampler-state cache hit.  Texture handle, sampler
 * lane, wrap S/T and filtering are all part of the key. */
int TitanPikaVr_RecordSamplerState(
    uint32_t texture_handle,
    uint32_t sampler_index,
    const TitanGameSamplerState *sampler
);

void TitanPikaVr_BeginRendererFrame(void);
void TitanPikaVr_RecordDraw(uint32_t primitives);
void TitanPikaVr_RecordMaterialPass(void);
void TitanPikaVr_RecordRibbonPath1Qwords(uint32_t qwords);
void TitanPikaVr_EndRendererFrame(uint32_t path3_qwords);

void TitanPikaVr_GetTelemetry(TitanPikaVrTelemetry *telemetry);

#endif
