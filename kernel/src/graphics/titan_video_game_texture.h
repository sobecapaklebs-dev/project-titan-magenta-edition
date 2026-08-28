#ifndef PROJECT_TITAN_VIDEO_GAME_TEXTURE_H
#define PROJECT_TITAN_VIDEO_GAME_TEXTURE_H

#include <stdint.h>

#define TITAN_VIDEO_GAME_TEXTURE_CAPACITY 1024u
#define TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH 512u
#define TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT 320u
#define TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH 128u
#define TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT 128u
#define TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES (1024u * 1024u)
#define TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES 256u
#define TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT \
    (TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES / TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES)

#define TITAN_VIDEO_GAME_TEXTURE_FREE 0u
#define TITAN_VIDEO_GAME_TEXTURE_LIVE 1u
#define TITAN_VIDEO_GAME_TEXTURE_RETIRED 2u
#define TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16 0u
#define TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32 1u
#define TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT 2u
#define TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES 1024u

typedef struct TitanVideoGameTexture
{
    uint32_t offset_bytes;
    uint32_t storage_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t buffer_width;
    uint32_t texture_bytes;
    uint32_t clut_offset_bytes;
    uint32_t clut_bytes;
    uint32_t generation;
    uint32_t handle;
    uint8_t state;
    uint8_t format;
    uint8_t reserved[2];
} TitanVideoGameTexture;

typedef struct TitanVideoGameTexturePool
{
    TitanVideoGameTexture textures[TITAN_VIDEO_GAME_TEXTURE_CAPACITY];
    uint32_t used_blocks[
        (TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT + 31u) / 32u
    ];
    uint32_t serial;
    uint32_t allocated_blocks;
} TitanVideoGameTexturePool;

void TitanVideoGameTexturePool_Initialize(TitanVideoGameTexturePool *pool);
uint32_t TitanVideoGameTexturePool_GetBufferWidth(uint32_t width);
uint32_t TitanVideoGameTexturePool_GetBufferWidthForFormat(
    uint32_t width,
    uint8_t format
);
uint32_t TitanVideoGameTexturePool_GetStorageBytes(
    uint32_t width,
    uint32_t height
);
uint32_t TitanVideoGameTexturePool_GetTextureBytesForFormat(
    uint32_t width,
    uint32_t height,
    uint8_t format
);
uint32_t TitanVideoGameTexturePool_GetStorageBytesForFormat(
    uint32_t width,
    uint32_t height,
    uint8_t format
);
int TitanVideoGameTexturePool_Allocate(
    TitanVideoGameTexturePool *pool,
    uint32_t width,
    uint32_t height,
    uint32_t *handle,
    TitanVideoGameTexture **texture
);
int TitanVideoGameTexturePool_AllocateFormat(
    TitanVideoGameTexturePool *pool,
    uint32_t width,
    uint32_t height,
    uint8_t format,
    uint32_t *handle,
    TitanVideoGameTexture **texture
);
TitanVideoGameTexture *TitanVideoGameTexturePool_Lookup(
    TitanVideoGameTexturePool *pool,
    uint32_t handle
);
const TitanVideoGameTexture *TitanVideoGameTexturePool_LookupConst(
    const TitanVideoGameTexturePool *pool,
    uint32_t handle
);
int TitanVideoGameTexturePool_Release(
    TitanVideoGameTexturePool *pool,
    uint32_t handle,
    int defer_reuse
);
void TitanVideoGameTexturePool_ReclaimRetired(TitanVideoGameTexturePool *pool);
uint32_t TitanVideoGameTexturePool_ReclaimRetiredCount(
    TitanVideoGameTexturePool *pool
);
int TitanVideoGameTexturePool_HasRetired(
    const TitanVideoGameTexturePool *pool
);
void TitanVideoGameTexturePool_Reset(TitanVideoGameTexturePool *pool);
uint32_t TitanVideoGameTexturePool_GetFreeBytes(
    const TitanVideoGameTexturePool *pool
);
uint32_t TitanVideoGameTexturePool_GetAllocatedTextureCount(
    const TitanVideoGameTexturePool *pool
);

#endif
