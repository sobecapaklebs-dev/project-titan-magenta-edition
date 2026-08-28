#include "titan_video_game_texture.h"

#include <string.h>

#define TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS 11u
#define TITAN_VIDEO_GAME_TEXTURE_INDEX_MASK \
    ((1u << TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS) - 1u)
#define TITAN_VIDEO_GAME_TEXTURE_GENERATION_MAX \
    ((1u << (32u - TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS)) - 1u)

static int TitanVideoGameTexturePool_BlockIsUsed(
    const TitanVideoGameTexturePool *pool,
    uint32_t block
)
{
    return (pool->used_blocks[block >> 5u] &
        (1u << (block & 31u))) != 0u;
}

static void TitanVideoGameTexturePool_MarkBlocks(
    TitanVideoGameTexturePool *pool,
    uint32_t first,
    uint32_t count,
    int used
)
{
    uint32_t block;

    for (block = first; block < first + count; block++)
    {
        uint32_t mask = 1u << (block & 31u);
        if (used)
            pool->used_blocks[block >> 5u] |= mask;
        else
            pool->used_blocks[block >> 5u] &= ~mask;
    }
}

static int TitanVideoGameTexturePool_FindBlocks(
    const TitanVideoGameTexturePool *pool,
    uint32_t required,
    uint32_t *first
)
{
    uint32_t block;
    uint32_t run_start = 0u;
    uint32_t run_length = 0u;

    if (required == 0u || required > TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT)
        return 0;
    for (block = 0u; block < TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT; block++)
    {
        if (TitanVideoGameTexturePool_BlockIsUsed(pool, block))
        {
            run_length = 0u;
            continue;
        }
        if (run_length == 0u) run_start = block;
        run_length++;
        if (run_length >= required)
        {
            *first = run_start;
            return 1;
        }
    }
    return 0;
}

static void TitanVideoGameTexturePool_Reclaim(
    TitanVideoGameTexturePool *pool,
    TitanVideoGameTexture *texture
)
{
    uint32_t first;
    uint32_t count;
    uint32_t generation;

    if (texture == 0 || texture->state == TITAN_VIDEO_GAME_TEXTURE_FREE)
        return;
    first = texture->offset_bytes / TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES;
    count = texture->storage_bytes / TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES;
    generation = texture->generation;
    TitanVideoGameTexturePool_MarkBlocks(pool, first, count, 0);
    if (pool->allocated_blocks >= count) pool->allocated_blocks -= count;
    memset(texture, 0, sizeof(*texture));
    texture->generation = generation;
}

void TitanVideoGameTexturePool_Initialize(TitanVideoGameTexturePool *pool)
{
    if (pool != 0) memset(pool, 0, sizeof(*pool));
}

uint32_t TitanVideoGameTexturePool_GetBufferWidth(uint32_t width)
{
    return TitanVideoGameTexturePool_GetBufferWidthForFormat(
        width, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16
    );
}

uint32_t TitanVideoGameTexturePool_GetBufferWidthForFormat(
    uint32_t width,
    uint8_t format
)
{
    if (width == 0u || width > TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH) return 0u;
    if (format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        return (width + 127u) & ~127u;
    if (format != TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16 &&
        format != TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32)
        return 0u;
    return (width + 63u) & ~63u;
}

uint32_t TitanVideoGameTexturePool_GetStorageBytes(
    uint32_t width,
    uint32_t height
)
{
    return TitanVideoGameTexturePool_GetStorageBytesForFormat(
        width,
        height,
        TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16
    );
}

uint32_t TitanVideoGameTexturePool_GetTextureBytesForFormat(
    uint32_t width,
    uint32_t height,
    uint8_t format
)
{
    uint32_t buffer_width;
    uint32_t padded_height;
    uint32_t bytes_per_pixel;

    if (height == 0u || height > TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT)
        return 0u;
    buffer_width = TitanVideoGameTexturePool_GetBufferWidthForFormat(
        width, format
    );
    if (buffer_width == 0u) return 0u;
    if (format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
    {
        /* PSM8 TBW is 128 pixels. Sixteen-row padding keeps the following
         * RGBA32 CLUT block aligned while remaining deterministic on host. */
        padded_height = (height + 15u) & ~15u;
        return buffer_width * padded_height;
    }
    bytes_per_pixel = format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16
        ? 2u : 4u;
    padded_height = (height + 7u) & ~7u;
    return buffer_width * padded_height * bytes_per_pixel;
}

uint32_t TitanVideoGameTexturePool_GetStorageBytesForFormat(
    uint32_t width,
    uint32_t height,
    uint8_t format
)
{
    uint32_t texture_bytes = TitanVideoGameTexturePool_GetTextureBytesForFormat(
        width, height, format
    );
    if (texture_bytes == 0u) return 0u;
    if (format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        return texture_bytes + TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES;
    return texture_bytes;
}

int TitanVideoGameTexturePool_Allocate(
    TitanVideoGameTexturePool *pool,
    uint32_t width,
    uint32_t height,
    uint32_t *handle,
    TitanVideoGameTexture **texture
)
{
    return TitanVideoGameTexturePool_AllocateFormat(
        pool,
        width,
        height,
        TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16,
        handle,
        texture
    );
}

int TitanVideoGameTexturePool_AllocateFormat(
    TitanVideoGameTexturePool *pool,
    uint32_t width,
    uint32_t height,
    uint8_t format,
    uint32_t *handle,
    TitanVideoGameTexture **texture
)
{
    uint32_t descriptor;
    uint32_t storage_bytes;
    uint32_t required_blocks;
    uint32_t first_block;
    uint32_t generation;

    if (pool == 0 || handle == 0 || texture == 0) return 0;
    storage_bytes = TitanVideoGameTexturePool_GetStorageBytesForFormat(
        width,
        height,
        format
    );
    if (storage_bytes == 0u) return 0;
    for (descriptor = 0u;
         descriptor < TITAN_VIDEO_GAME_TEXTURE_CAPACITY;
         descriptor++)
    {
        if (pool->textures[descriptor].state == TITAN_VIDEO_GAME_TEXTURE_FREE)
            break;
    }
    if (descriptor == TITAN_VIDEO_GAME_TEXTURE_CAPACITY) return 0;
    required_blocks = storage_bytes / TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES;
    if (!TitanVideoGameTexturePool_FindBlocks(pool, required_blocks, &first_block))
        return 0;

    generation = pool->serial + 1u;
    if (generation == 0u ||
        generation > TITAN_VIDEO_GAME_TEXTURE_GENERATION_MAX)
        generation = 1u;
    pool->serial = generation;
    *texture = &pool->textures[descriptor];
    memset(*texture, 0, sizeof(**texture));
    (*texture)->offset_bytes =
        first_block * TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES;
    (*texture)->storage_bytes = storage_bytes;
    (*texture)->width = width;
    (*texture)->height = height;
    (*texture)->buffer_width =
        TitanVideoGameTexturePool_GetBufferWidthForFormat(width, format);
    (*texture)->texture_bytes =
        TitanVideoGameTexturePool_GetTextureBytesForFormat(width, height, format);
    (*texture)->clut_offset_bytes = (*texture)->texture_bytes;
    (*texture)->clut_bytes = format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT
        ? TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES : 0u;
    (*texture)->generation = generation;
    (*texture)->state = TITAN_VIDEO_GAME_TEXTURE_LIVE;
    (*texture)->format = format;
    TitanVideoGameTexturePool_MarkBlocks(
        pool,
        first_block,
        required_blocks,
        1
    );
    pool->allocated_blocks += required_blocks;
    *handle = (generation << TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS) |
        (descriptor + 1u);
    (*texture)->handle = *handle;
    return 1;
}

TitanVideoGameTexture *TitanVideoGameTexturePool_Lookup(
    TitanVideoGameTexturePool *pool,
    uint32_t handle
)
{
    uint32_t encoded_index;
    uint32_t generation;
    TitanVideoGameTexture *texture;

    if (pool == 0 || handle == 0u) return 0;
    encoded_index = handle & TITAN_VIDEO_GAME_TEXTURE_INDEX_MASK;
    generation = handle >> TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS;
    if (encoded_index == 0u ||
        encoded_index > TITAN_VIDEO_GAME_TEXTURE_CAPACITY ||
        generation == 0u)
        return 0;
    texture = &pool->textures[encoded_index - 1u];
    if (texture->state != TITAN_VIDEO_GAME_TEXTURE_LIVE ||
        texture->generation != generation)
        return 0;
    return texture;
}

const TitanVideoGameTexture *TitanVideoGameTexturePool_LookupConst(
    const TitanVideoGameTexturePool *pool,
    uint32_t handle
)
{
    return TitanVideoGameTexturePool_Lookup(
        (TitanVideoGameTexturePool *)pool,
        handle
    );
}

int TitanVideoGameTexturePool_Release(
    TitanVideoGameTexturePool *pool,
    uint32_t handle,
    int defer_reuse
)
{
    TitanVideoGameTexture *texture =
        TitanVideoGameTexturePool_Lookup(pool, handle);

    if (texture == 0) return 0;
    if (defer_reuse)
        texture->state = TITAN_VIDEO_GAME_TEXTURE_RETIRED;
    else
        TitanVideoGameTexturePool_Reclaim(pool, texture);
    return 1;
}

uint32_t TitanVideoGameTexturePool_ReclaimRetiredCount(
    TitanVideoGameTexturePool *pool
)
{
    uint32_t index;
    uint32_t reclaimed = 0u;

    if (pool == 0) return 0u;
    for (index = 0u; index < TITAN_VIDEO_GAME_TEXTURE_CAPACITY; index++)
    {
        if (pool->textures[index].state == TITAN_VIDEO_GAME_TEXTURE_RETIRED)
        {
            TitanVideoGameTexturePool_Reclaim(pool, &pool->textures[index]);
            reclaimed++;
        }
    }
    return reclaimed;
}

void TitanVideoGameTexturePool_ReclaimRetired(TitanVideoGameTexturePool *pool)
{
    (void)TitanVideoGameTexturePool_ReclaimRetiredCount(pool);
}

int TitanVideoGameTexturePool_HasRetired(
    const TitanVideoGameTexturePool *pool
)
{
    uint32_t index;

    if (pool == 0) return 0;
    for (index = 0u; index < TITAN_VIDEO_GAME_TEXTURE_CAPACITY; index++)
    {
        if (pool->textures[index].state == TITAN_VIDEO_GAME_TEXTURE_RETIRED)
            return 1;
    }
    return 0;
}

void TitanVideoGameTexturePool_Reset(TitanVideoGameTexturePool *pool)
{
    uint32_t serial;

    if (pool == 0) return;
    serial = pool->serial;
    memset(pool, 0, sizeof(*pool));
    pool->serial = serial;
}

uint32_t TitanVideoGameTexturePool_GetFreeBytes(
    const TitanVideoGameTexturePool *pool
)
{
    if (pool == 0 ||
        pool->allocated_blocks > TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT)
        return 0u;
    return (TITAN_VIDEO_GAME_TEXTURE_BLOCK_COUNT - pool->allocated_blocks) *
        TITAN_VIDEO_GAME_TEXTURE_BLOCK_BYTES;
}

uint32_t TitanVideoGameTexturePool_GetAllocatedTextureCount(
    const TitanVideoGameTexturePool *pool
)
{
    uint32_t index;
    uint32_t count = 0u;

    if (pool == 0) return 0u;
    for (index = 0u; index < TITAN_VIDEO_GAME_TEXTURE_CAPACITY; index++)
    {
        if (pool->textures[index].state != TITAN_VIDEO_GAME_TEXTURE_FREE)
            count++;
    }
    return count;
}
