#include "titan_game_runtime.h"

#include "titan_audio.h"
#include "titan_storage_io.h"
#include "titan_storage_scheduler.h"
#include "titan_time.h"
#include "../graphics/titan_video.h"
#include "../graphics/titan_video_game_math.h"
#include "../platform/ps2/titan_input.h"

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#include <delaythread.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_GAME_HEAP_DEFAULT_BYTES (12u * 1024u * 1024u)
#define TITAN_GAME_HEAP_MIN_BYTES     (4u * 1024u * 1024u)
#define TITAN_GAME_HEAP_MAX_BYTES     (16u * 1024u * 1024u)
/* P162.4.1: allocation ownership is self-describing in the header placed
 * immediately before the returned payload.  This removes the fixed live-slot
 * ceiling without changing P162.4's proven durable chunk policy. */
#define TITAN_GAME_ALLOCATION_MAGIC 0x5447414Cu
#define TITAN_GAME_HEAP_CHUNK_DEFAULT_BYTES (256u * 1024u)
#define TITAN_GAME_HEAP_CHUNK_MIN_BYTES     (64u * 1024u)
#define TITAN_GAME_HEAP_MAX_CHUNKS          128u
#define TITAN_GAME_HEAP_BLOCK_MAGIC         0x5447424Cu
#define TITAN_GAME_STORAGE_PARK_TIMEOUT_MS 2000u
#define TITAN_GAME_STORAGE_PATH_CAPACITY 768u

/* P10.57 internal kernel bridge.  Public TitanGameApiV1 stays unchanged; this
 * only prevents the runtime from exploding one producer batch into per-triangle
 * renderer calls. */
#if defined(__EE__) || defined(_EE)
extern int TitanVideo_DrawGameMaterialTrianglesBatch(
    const TitanGameMaterialVertex *vertices,
    uint32_t vertex_count,
    uint32_t texture0_handle,
    uint32_t texture1_handle
);
#endif

typedef struct TitanGameHeapBlock TitanGameHeapBlock;

typedef struct TitanGameHeapChunk
{
    void *raw;
    unsigned char *base;
    uint32_t bytes;
    TitanGameHeapBlock *first;
} TitanGameHeapChunk;

struct TitanGameHeapBlock
{
    uint32_t magic;
    uint32_t total_bytes;
    uint32_t is_free;
    uint32_t chunk_index;
    TitanGameHeapBlock *prev_phys;
    TitanGameHeapBlock *next_phys;
    TitanGameHeapBlock *prev_free;
    TitanGameHeapBlock *next_free;
};

typedef struct TitanGameAllocationHeader
{
    uint32_t magic;
    uint32_t bytes;
    TitanGameHeapBlock *block;
} TitanGameAllocationHeader;

typedef struct TitanGameRuntimeState
{
    int active;
    int teardown_pending;
    int storage_exclusive;
    int audio_active;
    int exit_requested;
    int frame_open;
    uint32_t width;
    uint32_t height;
    uint32_t heap_limit;
    uint32_t heap_used;
    uint32_t allocation_live;
    uint32_t allocation_peak;
    uint32_t heap_chunk_count;
    uint32_t heap_chunk_bytes;
    TitanGameHeapBlock *heap_free_head;
    TitanGameHeapChunk heap_chunks[TITAN_GAME_HEAP_MAX_CHUNKS];
    float mvp[16];
    TitanGameRenderState render_state;
    TitanGamePadState pads[TITAN_GAME_MAX_PADS];
    char storage_root[TITAN_GAME_STORAGE_PATH_CAPACITY];
} TitanGameRuntimeState;

static TitanGameRuntimeState g_game;
static char g_pending_storage_root[TITAN_GAME_STORAGE_PATH_CAPACITY];
const volatile char g_p16241_runtime_tag[] = "P16241_REGISTRYLESS_P1624_CHUNKS";

static int TitanGameRuntime_CopyStorageRoot(const char *root_path)
{
    unsigned int length;
    if (root_path == 0 || root_path[0] == '\0') return 0;
    length = (unsigned int)strlen(root_path);
    while (length > 1u && root_path[length - 1u] == '/') length--;
    if (length == 0u || length >= TITAN_GAME_STORAGE_PATH_CAPACITY) return 0;
    memcpy(g_pending_storage_root, root_path, length);
    g_pending_storage_root[length] = '\0';
    return 1;
}

int TitanGameRuntime_SetStorageRoot(const char *root_path)
{
    if (g_game.active || g_game.teardown_pending || g_game.storage_exclusive)
        return 0;
    memset(g_pending_storage_root, 0, sizeof(g_pending_storage_root));
    return TitanGameRuntime_CopyStorageRoot(root_path);
}

static int TitanGameRuntime_SafeRelativePath(const char *path)
{
    const char *cursor;
    if (path == 0 || path[0] == '\0' || path[0] == '/' ||
        strchr(path, ':') != 0 || strchr(path, '\\') != 0)
        return 0;
    cursor = path;
    while (*cursor != '\0')
    {
        const char *segment = cursor;
        unsigned int length;
        while (*cursor != '\0' && *cursor != '/')
        {
            if ((unsigned char)*cursor < 0x20u) return 0;
            cursor++;
        }
        length = (unsigned int)(cursor - segment);
        if (length == 0u || (length == 1u && segment[0] == '.') ||
            (length == 2u && segment[0] == '.' && segment[1] == '.'))
            return 0;
        if (*cursor == '/') cursor++;
    }
    return 1;
}

static int TitanGameRuntime_ResolveStoragePath(
    const char *path, int write_access,
    char output[TITAN_GAME_STORAGE_PATH_CAPACITY]
)
{
    const char *relative;
    unsigned int root_length;
    int length;
    if (!g_game.active || g_game.storage_root[0] == '\0' || path == 0)
        return 0;
    root_length = (unsigned int)strlen(g_game.storage_root);
    if (strncmp(path, g_game.storage_root, root_length) == 0 &&
        path[root_length] == '/')
        relative = path + root_length + 1u;
    else
        relative = path;
    if (!TitanGameRuntime_SafeRelativePath(relative)) return 0;
    if (write_access && strncmp(relative, "data/", 5u) != 0 &&
        strcmp(relative, "data") != 0)
        return 0;
    length = snprintf(output, TITAN_GAME_STORAGE_PATH_CAPACITY,
                      "%s/%s", g_game.storage_root, relative);
    return length >= 0 && length < (int)TITAN_GAME_STORAGE_PATH_CAPACITY;
}

static void TitanGameRuntime_SetIdentityMvp(void)
{
    unsigned int index;
    for (index = 0u; index < 16u; index++)
        g_game.mvp[index] = 0.0f;
    g_game.mvp[0] = 1.0f;
    g_game.mvp[5] = 1.0f;
    g_game.mvp[10] = 1.0f;
    g_game.mvp[15] = 1.0f;
}

static int TitanGameRuntime_IsPowerOfTwo(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static void TitanGameRuntime_InitAllocationRegistry(void)
{
    /* Lifecycle name retained, but there is no fixed registry anymore. */
    g_game.allocation_live = 0u;
    g_game.allocation_peak = 0u;
}

static uintptr_t TitanGameRuntime_AlignUp(uintptr_t value, uint32_t alignment)
{
    return (value + (uintptr_t)alignment - 1u) &
        ~((uintptr_t)alignment - 1u);
}

static uint32_t TitanGameRuntime_BlockHeaderBytes(void)
{
    return (uint32_t)TitanGameRuntime_AlignUp(
        (uintptr_t)sizeof(TitanGameHeapBlock), 16u
    );
}

static void TitanGameRuntime_FreeListRemove(TitanGameHeapBlock *block)
{
    if (block == 0 || !block->is_free) return;
    if (block->prev_free != 0)
        block->prev_free->next_free = block->next_free;
    else if (g_game.heap_free_head == block)
        g_game.heap_free_head = block->next_free;
    if (block->next_free != 0)
        block->next_free->prev_free = block->prev_free;
    block->prev_free = 0;
    block->next_free = 0;
}

static void TitanGameRuntime_FreeListPush(TitanGameHeapBlock *block)
{
    if (block == 0) return;
    block->is_free = 1u;
    block->prev_free = 0;
    block->next_free = g_game.heap_free_head;
    if (g_game.heap_free_head != 0)
        g_game.heap_free_head->prev_free = block;
    g_game.heap_free_head = block;
}

static TitanGameHeapBlock *TitanGameRuntime_CreateHeapChunk(uint32_t required)
{
    uint32_t floor_bytes;
    uint32_t candidate;
    uint32_t index;
    void *raw = 0;
    uintptr_t aligned;
    uint32_t usable;
    TitanGameHeapChunk *chunk;
    TitanGameHeapBlock *block;

    if (g_game.heap_chunk_count >= TITAN_GAME_HEAP_MAX_CHUNKS)
        return 0;

    floor_bytes = required;
    if (floor_bytes < TITAN_GAME_HEAP_CHUNK_MIN_BYTES)
        floor_bytes = TITAN_GAME_HEAP_CHUNK_MIN_BYTES;
    floor_bytes = (floor_bytes + 15u) & ~15u;
    candidate = floor_bytes > TITAN_GAME_HEAP_CHUNK_DEFAULT_BYTES
        ? floor_bytes : TITAN_GAME_HEAP_CHUNK_DEFAULT_BYTES;

    /* P162.4: ask newlib for a small number of durable chunks instead of one
     * malloc/free pair per game allocation. If the EE heap is already
     * fragmented, gracefully fall back 256K -> 128K -> 64K (or the exact
     * request floor) rather than failing a 32-byte allocation with MiB free. */
    for (;;)
    {
        raw = malloc((size_t)candidate + 15u);
        if (raw != 0) break;
        if (candidate == floor_bytes) return 0;
        candidate /= 2u;
        if (candidate < floor_bytes) candidate = floor_bytes;
    }

    aligned = TitanGameRuntime_AlignUp((uintptr_t)raw, 16u);
    usable = candidate - (uint32_t)(aligned - (uintptr_t)raw);
    usable &= ~15u;
    if (usable < required)
    {
        free(raw);
        return 0;
    }

    index = g_game.heap_chunk_count++;
    chunk = &g_game.heap_chunks[index];
    chunk->raw = raw;
    chunk->base = (unsigned char *)aligned;
    chunk->bytes = usable;
    block = (TitanGameHeapBlock *)chunk->base;
    memset(block, 0, sizeof(*block));
    block->magic = TITAN_GAME_HEAP_BLOCK_MAGIC;
    block->total_bytes = usable;
    block->chunk_index = index;
    block->is_free = 1u;
    chunk->first = block;
    g_game.heap_chunk_bytes += usable;
    TitanGameRuntime_FreeListPush(block);
    return block;
}

static int TitanGameRuntime_BlockFit(
    TitanGameHeapBlock *block, uint32_t bytes, uint32_t alignment,
    uintptr_t *payload_out, uint32_t *consumed_out
)
{
    uint32_t header_bytes;
    uintptr_t begin;
    uintptr_t payload;
    uintptr_t end;
    uintptr_t consumed;

    if (block == 0 || !block->is_free ||
        block->magic != TITAN_GAME_HEAP_BLOCK_MAGIC)
        return 0;
    header_bytes = TitanGameRuntime_BlockHeaderBytes();
    begin = (uintptr_t)block;
    payload = TitanGameRuntime_AlignUp(
        begin + header_bytes + sizeof(TitanGameAllocationHeader), alignment
    );
    end = payload + (uintptr_t)bytes;
    if (end < payload) return 0;
    consumed = TitanGameRuntime_AlignUp(end - begin, 16u);
    if (consumed > block->total_bytes || consumed > 0xffffffffu)
        return 0;
    if (payload_out != 0) *payload_out = payload;
    if (consumed_out != 0) *consumed_out = (uint32_t)consumed;
    return 1;
}

static TitanGameHeapBlock *TitanGameRuntime_FindHeapBlock(
    uint32_t bytes, uint32_t alignment,
    uintptr_t *payload_out, uint32_t *consumed_out
)
{
    TitanGameHeapBlock *block = g_game.heap_free_head;
    while (block != 0)
    {
        if (TitanGameRuntime_BlockFit(
                block, bytes, alignment, payload_out, consumed_out))
            return block;
        block = block->next_free;
    }
    return 0;
}

/* P162.4.1: if the intrusive free-list loses a free block, recover it by
 * walking each durable chunk's physical chain before asking newlib for more
 * memory.  The P162.4 chunk sizes and allocation policy remain unchanged. */
static TitanGameHeapBlock *TitanGameRuntime_FindHeapBlockPhysical(
    uint32_t bytes, uint32_t alignment,
    uintptr_t *payload_out, uint32_t *consumed_out)
{
    uint32_t ci;
    for (ci = 0u; ci < g_game.heap_chunk_count; ci++)
    {
        TitanGameHeapChunk *chunk = &g_game.heap_chunks[ci];
        TitanGameHeapBlock *block = chunk->first;
        uint32_t guard = 0u;
        while (block != 0 && guard++ < 100000u)
        {
            uintptr_t b = (uintptr_t)block;
            uintptr_t begin = (uintptr_t)chunk->base;
            uintptr_t end = begin + (uintptr_t)chunk->bytes;
            if (b < begin || b >= end ||
                block->magic != TITAN_GAME_HEAP_BLOCK_MAGIC ||
                block->total_bytes == 0u ||
                b + (uintptr_t)block->total_bytes > end)
                break;
            if (TitanGameRuntime_BlockFit(
                    block, bytes, alignment, payload_out, consumed_out))
                return block;
            block = block->next_phys;
        }
    }
    return 0;
}

static void TitanGameRuntime_SplitHeapBlock(
    TitanGameHeapBlock *block, uint32_t consumed
)
{
    uint32_t header_bytes = TitanGameRuntime_BlockHeaderBytes();
    uint32_t minimum_tail = header_bytes +
        (uint32_t)sizeof(TitanGameAllocationHeader) + 32u;
    uint32_t remaining;
    TitanGameHeapBlock *tail;

    if (block == 0 || consumed > block->total_bytes) return;
    remaining = block->total_bytes - consumed;
    if (remaining < minimum_tail) return;

    tail = (TitanGameHeapBlock *)((unsigned char *)block + consumed);
    memset(tail, 0, sizeof(*tail));
    tail->magic = TITAN_GAME_HEAP_BLOCK_MAGIC;
    tail->total_bytes = remaining;
    tail->is_free = 1u;
    tail->chunk_index = block->chunk_index;
    tail->prev_phys = block;
    tail->next_phys = block->next_phys;
    if (tail->next_phys != 0) tail->next_phys->prev_phys = tail;
    block->next_phys = tail;
    block->total_bytes = consumed;
    TitanGameRuntime_FreeListPush(tail);
}

static TitanGameHeapBlock *TitanGameRuntime_CoalesceHeapBlock(
    TitanGameHeapBlock *block)
{
    TitanGameHeapBlock *other;
    if (block == 0) return 0;

    other = block->prev_phys;
    if (other != 0 && other->is_free &&
        other->magic == TITAN_GAME_HEAP_BLOCK_MAGIC)
    {
        TitanGameRuntime_FreeListRemove(other);
        other->total_bytes += block->total_bytes;
        other->next_phys = block->next_phys;
        if (other->next_phys != 0) other->next_phys->prev_phys = other;
        block = other;
    }

    other = block->next_phys;
    if (other != 0 && other->is_free &&
        other->magic == TITAN_GAME_HEAP_BLOCK_MAGIC)
    {
        TitanGameRuntime_FreeListRemove(other);
        block->total_bytes += other->total_bytes;
        block->next_phys = other->next_phys;
        if (block->next_phys != 0) block->next_phys->prev_phys = block;
    }
    return block;
}

static void *TitanGameApi_Alloc(uint32_t bytes, uint32_t alignment)
{
    uint32_t consumed = 0u;
    uint32_t required;
    uintptr_t payload = 0u;
    TitanGameAllocationHeader *header;
    TitanGameHeapBlock *block;

    if (!g_game.active || bytes == 0u)
        return 0;
    if (alignment == 0u) alignment = 16u;
    if (alignment < 16u || alignment > 4096u ||
        !TitanGameRuntime_IsPowerOfTwo(alignment))
        return 0;
    if (bytes > g_game.heap_limit - g_game.heap_used)
        return 0;

    block = TitanGameRuntime_FindHeapBlock(
        bytes, alignment, &payload, &consumed);
    if (block == 0)
        block = TitanGameRuntime_FindHeapBlockPhysical(
            bytes, alignment, &payload, &consumed);
    if (block == 0)
    {
        required = TitanGameRuntime_BlockHeaderBytes() +
            (uint32_t)sizeof(TitanGameAllocationHeader) +
            (alignment - 1u) + bytes + 16u;
        if (required < bytes) return 0;
        block = TitanGameRuntime_CreateHeapChunk(required);
        if (block == 0 || !TitanGameRuntime_BlockFit(
                block, bytes, alignment, &payload, &consumed))
            return 0;
    }

    TitanGameRuntime_FreeListRemove(block);
    TitanGameRuntime_SplitHeapBlock(block, consumed);
    block->is_free = 0u;
    block->prev_free = 0;
    block->next_free = 0;

    header = (TitanGameAllocationHeader *)(
        payload - sizeof(TitanGameAllocationHeader));
    header->magic = TITAN_GAME_ALLOCATION_MAGIC;
    header->bytes = bytes;
    header->block = block;

    g_game.heap_used += bytes;
    g_game.allocation_live++;
    if (g_game.allocation_live > g_game.allocation_peak)
        g_game.allocation_peak = g_game.allocation_live;
    return (void *)payload;
}

static void TitanGameApi_Release(void *pointer)
{
    TitanGameAllocationHeader *header;
    TitanGameHeapBlock *block;
    uint32_t bytes;

    if (pointer == 0) return;
    header = (TitanGameAllocationHeader *)(
        (uintptr_t)pointer - sizeof(TitanGameAllocationHeader));
    if (header->magic != TITAN_GAME_ALLOCATION_MAGIC) return;
    block = header->block;
    bytes = header->bytes;
    if (block == 0 || block->magic != TITAN_GAME_HEAP_BLOCK_MAGIC ||
        block->is_free) return;

    header->magic = 0u;
    header->block = 0;
    if (g_game.heap_used >= bytes)
        g_game.heap_used -= bytes;
    else
        g_game.heap_used = 0u;
    if (g_game.allocation_live > 0u) g_game.allocation_live--;

    block->is_free = 1u;
    block = TitanGameRuntime_CoalesceHeapBlock(block);
    TitanGameRuntime_FreeListPush(block);
}

static void TitanGameRuntime_ReleaseAllAllocations(void)
{
    uint32_t index;
    for (index = 0u; index < g_game.heap_chunk_count; index++)
    {
        if (g_game.heap_chunks[index].raw != 0)
            free(g_game.heap_chunks[index].raw);
    }
    memset(g_game.heap_chunks, 0, sizeof(g_game.heap_chunks));
    g_game.heap_chunk_count = 0u;
    g_game.heap_chunk_bytes = 0u;
    g_game.heap_free_head = 0;
    g_game.heap_used = 0u;
    TitanGameRuntime_InitAllocationRegistry();
}

static void TitanGameApi_RequestExit(void) { g_game.exit_requested = 1; }
void TitanGameRuntime_RequestExit(void) { g_game.exit_requested = 1; }
static int TitanGameApi_ExitRequested(void) { return g_game.exit_requested; }
static uint64_t TitanGameApi_UptimeMs(void) { return TitanTime_NowMilliseconds(); }
static uint64_t TitanGameApi_UptimeUs(void) { return TitanTime_NowMicroseconds(); }
static uint32_t TitanGameApi_HeapUsed(void) { return g_game.heap_used; }
static uint32_t TitanGameApi_HeapLimit(void) { return g_game.heap_limit; }

static void TitanGameApi_PollInput(void)
{
    uint32_t index;

    if (!g_game.active) return;

    /* The kernel main thread is the only producer that calls TitanInput_Update().
     * A native game worker only snapshots that state here. This avoids two EE
     * threads entering PAD/Unified Input backends concurrently while still
     * giving the game edge-triggered pressed/released values once per poll. */
    for (index = 0u; index < TITAN_GAME_MAX_PADS; index++)
    {
        const TitanInputPadState *source = TitanInput_GetPadState(index);
        TitanGamePadState *destination = &g_game.pads[index];
        uint16_t previous = destination->held;
        uint16_t held = source != 0 && source->connected
            ? source->buttons : 0u;

        memset(destination, 0, sizeof(*destination));
        destination->held = held;
        destination->pressed = (uint16_t)(held & (uint16_t)~previous);
        destination->released = (uint16_t)(previous & (uint16_t)~held);
        if (source != 0)
        {
            destination->connected = source->connected ? 1u : 0u;
            destination->port = source->port >= 0 ? (uint8_t)source->port : 0xffu;
            destination->slot = source->slot >= 0 ? (uint8_t)source->slot : 0xffu;
            destination->left_stick_x = source->left_stick_x;
            destination->left_stick_y = source->left_stick_y;
            destination->right_stick_x = source->right_stick_x;
            destination->right_stick_y = source->right_stick_y;
        }
        else
        {
            destination->port = 0xffu;
            destination->slot = 0xffu;
            destination->left_stick_x = 128u;
            destination->left_stick_y = 128u;
            destination->right_stick_x = 128u;
            destination->right_stick_y = 128u;
        }
    }
}

static int TitanGameApi_GetPad(uint32_t index, TitanGamePadState *state)
{
    if (!g_game.active || state == 0 || index >= TITAN_GAME_MAX_PADS)
        return 0;
    *state = g_game.pads[index];
    return 1;
}

static uint32_t TitanGameApi_ConnectedPadCount(void)
{
    uint32_t index;
    uint32_t count = 0u;
    if (!g_game.active) return 0u;
    for (index = 0u; index < TITAN_GAME_MAX_PADS; index++)
        if (g_game.pads[index].connected) count++;
    return count;
}

static int TitanGameApi_SetPadRumble(
    uint32_t index,
    uint8_t small_motor,
    uint8_t large_motor
)
{
    return g_game.active
        ? TitanInput_SetRumble(index, small_motor, large_motor)
        : 0;
}

static int TitanGameApi_AudioBegin(void)
{
    if (!g_game.active)
        return 0;
    if (g_game.audio_active)
        return 1;
    if (!TitanAudio_BeginExternalStream())
        return 0;
    g_game.audio_active = 1;
    return 1;
}

static uint32_t TitanGameApi_AudioSubmit(
    const int16_t *samples,
    uint32_t frames
)
{
    if (!g_game.active || !g_game.audio_active)
        return 0u;
    return TitanAudio_SubmitExternalPcm((const short *)samples, frames);
}

static uint32_t TitanGameApi_AudioFreeFrames(void)
{
    return g_game.active && g_game.audio_active
        ? TitanAudio_GetExternalFreeFrames()
        : 0u;
}

static void TitanGameApi_AudioEnd(void)
{
    if (!g_game.audio_active)
        return;
    TitanAudio_EndExternalStream();
    g_game.audio_active = 0;
}

static int TitanGameApi_StorageOpen(const char *path, uint32_t flags)
{
    int native_flags = 0;
    char resolved[TITAN_GAME_STORAGE_PATH_CAPACITY];
    int write_access;
    if (!g_game.active || path == 0 || path[0] == '\0') return -1;
    write_access = (flags & (TITAN_GAME_STORAGE_WRITE |
        TITAN_GAME_STORAGE_CREATE | TITAN_GAME_STORAGE_TRUNCATE |
        TITAN_GAME_STORAGE_EXCLUSIVE)) != 0u;
    if (!TitanGameRuntime_ResolveStoragePath(path, write_access, resolved))
        return -1;
    if (flags & TITAN_GAME_STORAGE_READ) native_flags |= TITAN_STORAGE_IO_READ;
    if (flags & TITAN_GAME_STORAGE_WRITE) native_flags |= TITAN_STORAGE_IO_WRITE;
    if (flags & TITAN_GAME_STORAGE_CREATE) native_flags |= TITAN_STORAGE_IO_CREATE;
    if (flags & TITAN_GAME_STORAGE_TRUNCATE) native_flags |= TITAN_STORAGE_IO_TRUNCATE;
    if (flags & TITAN_GAME_STORAGE_EXCLUSIVE) native_flags |= TITAN_STORAGE_IO_EXCLUSIVE;
    return TitanStorageIo_Open(resolved, native_flags);
}
static int TitanGameApi_StorageClose(int descriptor)
{
    return TitanStorageIo_Close(descriptor);
}
static int64_t TitanGameApi_StorageRead(int descriptor, void *buffer, uint32_t bytes)
{
    return (int64_t)TitanStorageIo_ReadSome(descriptor, buffer, (size_t)bytes);
}
static int64_t TitanGameApi_StorageWrite(int descriptor, const void *buffer, uint32_t bytes)
{
    return (int64_t)TitanStorageIo_WriteSome(descriptor, buffer, (size_t)bytes);
}
static int64_t TitanGameApi_StorageSeek(int descriptor, int64_t offset, int origin)
{
    return (int64_t)TitanStorageIo_Seek64(descriptor, offset, origin);
}
static int TitanGameApi_StorageStat(const char *path, TitanGameStorageEntry *entry)
{
    TitanStorageIoSize size = 0u;
    int directory = 0;
    char resolved[TITAN_GAME_STORAGE_PATH_CAPACITY];
    if (path == 0 || entry == 0 ||
        !TitanGameRuntime_ResolveStoragePath(path, 0, resolved) ||
        !TitanStorageIo_PathInfo64(resolved, &directory, &size))
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->size = size;
    if (directory) entry->flags |= TITAN_GAME_STORAGE_ENTRY_DIRECTORY;
    return 1;
}
static int TitanGameApi_StorageList(
    const char *path, uint32_t start_index,
    TitanGameStorageEntry *entries, uint32_t capacity
)
{
    TitanStorageIoEntry temporary[16];
    uint32_t produced = 0u;
    uint32_t cursor = start_index;
    char resolved[TITAN_GAME_STORAGE_PATH_CAPACITY];
    if (path == 0 || entries == 0 || capacity == 0u) return -1;
    if (!TitanGameRuntime_ResolveStoragePath(path, 0, resolved)) return -1;
    while (produced < capacity)
    {
        uint32_t chunk = capacity - produced;
        int result;
        uint32_t index;
        if (chunk > 16u) chunk = 16u;
        result = TitanStorageIo_ListDirectory(resolved, cursor, temporary, chunk);
        if (result < 0) return produced != 0u ? (int)produced : -1;
        if (result == 0) break;
        for (index = 0u; index < (uint32_t)result; index++)
        {
            TitanGameStorageEntry *destination = &entries[produced + index];
            memset(destination, 0, sizeof(*destination));
            strncpy(destination->name, temporary[index].name,
                    sizeof(destination->name) - 1u);
            destination->size = temporary[index].size;
            if (temporary[index].flags & TITAN_STORAGE_IO_ENTRY_DIRECTORY)
                destination->flags |= TITAN_GAME_STORAGE_ENTRY_DIRECTORY;
        }
        produced += (uint32_t)result;
        cursor += (uint32_t)result;
        if ((uint32_t)result < chunk) break;
    }
    return (int)produced;
}
static int TitanGameApi_StorageMkdir(const char *path)
{
    char resolved[TITAN_GAME_STORAGE_PATH_CAPACITY];
    return TitanGameRuntime_ResolveStoragePath(path, 1, resolved)
        ? TitanStorageIo_CreateDirectory(resolved) : -1;
}
static int TitanGameApi_StorageRemove(const char *path)
{
    char resolved[TITAN_GAME_STORAGE_PATH_CAPACITY];
    return TitanGameRuntime_ResolveStoragePath(path, 1, resolved)
        ? TitanStorageIo_RemoveAny(resolved) : -1;
}
static int TitanGameApi_StorageRename(const char *source, const char *destination)
{
    char resolved_source[TITAN_GAME_STORAGE_PATH_CAPACITY];
    char resolved_destination[TITAN_GAME_STORAGE_PATH_CAPACITY];
    if (!TitanGameRuntime_ResolveStoragePath(source, 1, resolved_source) ||
        !TitanGameRuntime_ResolveStoragePath(destination, 1, resolved_destination))
        return -1;
    return TitanStorageIo_Rename(resolved_source, resolved_destination);
}

static int TitanGameApi_VideoGetInfo(TitanGameVideoInfo *info)
{
    if (!g_game.active || info == 0) return 0;
    memset(info, 0, sizeof(*info));
    info->width = g_game.width;
    info->height = g_game.height;
    info->framebuffer_count = TitanVideo_GetFramebufferCount();
    info->zbuffer_enabled = TitanVideo_GetGameZBufferAddress() != 0u;
    info->texture_bytes_free = TitanVideo_GetGameTextureBytesFree();
    info->feature_flags = TITAN_GAME_FEATURE_Z16 |
        TITAN_GAME_FEATURE_COLORED_TRIANGLES |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_P16 |
        TITAN_GAME_FEATURE_TEXTURED_TRIANGLES |
        TITAN_GAME_FEATURE_FOUR_PAD_ABI |
        TITAN_GAME_FEATURE_CLIPSPACE_TRIANGLES |
        TITAN_GAME_FEATURE_RENDER_STATE |
        TITAN_GAME_FEATURE_PERSPECTIVE_STQ |
        TITAN_GAME_FEATURE_TWO_TEXTURE_MATERIAL |
        TITAN_GAME_FEATURE_VIEWPORT_SCISSOR |
        TITAN_GAME_FEATURE_HAPTICS |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32 |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8;
    return 1;
}

static int TitanGameApi_VideoBeginFrame(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!g_game.active || g_game.frame_open) return 0;
    if (TitanVideo_BeginFrame() != TITAN_RESULT_OK) return 0;
    g_game.frame_open = 1;
    TitanVideo_Clear(red, green, blue);
    return 1;
}

static void TitanGameApi_VideoSetMvp(const float matrix16[16])
{
    if (matrix16 == 0)
        TitanGameRuntime_SetIdentityMvp();
    else
        memcpy(g_game.mvp, matrix16, sizeof(g_game.mvp));
}

static int TitanGameRuntime_TransformVertex(
    const TitanGameVertex3D *source,
    float *screen_x, float *screen_y, unsigned int *screen_z
)
{
    const float *m = g_game.mvp;
    float x = source->x, y = source->y, z = source->z;
    float clip_x = m[0]*x + m[1]*y + m[2]*z + m[3];
    float clip_y = m[4]*x + m[5]*y + m[6]*z + m[7];
    float clip_z = m[8]*x + m[9]*y + m[10]*z + m[11];
    float clip_w = m[12]*x + m[13]*y + m[14]*z + m[15];
    TitanVideoGameScreenVertex screen;

    if (!TitanVideoGame_MapClipVertex(
            &g_game.render_state,
            clip_x,
            clip_y,
            clip_z,
            clip_w,
            &screen))
        return 0;
    *screen_x = screen.x;
    *screen_y = screen.y;
    *screen_z = screen.z;
    return 1;
}

static int TitanGameApi_VideoDrawIndexedTriangles(
    const TitanGameVertex3D *vertices, uint32_t vertex_count,
    const uint16_t *indices, uint32_t index_count,
    uint32_t texture_handle
)
{
    uint32_t index;
    uint32_t drawn = 0u;
    if (!g_game.active || !g_game.frame_open || vertices == 0 || indices == 0 ||
        vertex_count == 0u || index_count == 0u || index_count % 3u != 0u)
        return -1;
    for (index = 0u; index < index_count; index += 3u)
    {
        uint16_t i0 = indices[index];
        uint16_t i1 = indices[index + 1u];
        uint16_t i2 = indices[index + 2u];
        float x0,y0,x1,y1,x2,y2;
        unsigned int z0,z1,z2;
        uint32_t rgba0;
        uint32_t rgba1;
        uint32_t rgba2;
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
            return -1;
        if (!TitanGameRuntime_TransformVertex(&vertices[i0], &x0,&y0,&z0) ||
            !TitanGameRuntime_TransformVertex(&vertices[i1], &x1,&y1,&z1) ||
            !TitanGameRuntime_TransformVertex(&vertices[i2], &x2,&y2,&z2))
            continue;
        rgba0 = vertices[i0].rgba;
        rgba1 = vertices[i1].rgba;
        rgba2 = vertices[i2].rgba;
        if (texture_handle != 0u)
        {
            if (!TitanVideo_DrawGameTexturedTrianglePhysical(
                    x0,y0,z0,vertices[i0].u,vertices[i0].v,
                    x1,y1,z1,vertices[i1].u,vertices[i1].v,
                    x2,y2,z2,vertices[i2].u,vertices[i2].v,
                    (uint8_t)(rgba0 >> 24),(uint8_t)(rgba0 >> 16),
                    (uint8_t)(rgba0 >> 8),(uint8_t)rgba0,
                    (uint8_t)(rgba1 >> 24),(uint8_t)(rgba1 >> 16),
                    (uint8_t)(rgba1 >> 8),(uint8_t)rgba1,
                    (uint8_t)(rgba2 >> 24),(uint8_t)(rgba2 >> 16),
                    (uint8_t)(rgba2 >> 8),(uint8_t)rgba2,
                    texture_handle))
                return drawn != 0u ? (int)drawn : -2;
        }
        else
        {
            TitanVideo_DrawGameTrianglePhysical(
                x0,y0,z0,x1,y1,z1,x2,y2,z2,
                (uint8_t)(rgba0 >> 24), (uint8_t)(rgba0 >> 16),
                (uint8_t)(rgba0 >> 8), (uint8_t)rgba0
            );
        }
        drawn++;
    }
    return (int)drawn;
}

static int TitanGameApi_VideoUploadTexturePsm16(
    const uint16_t *pixels, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t *texture_handle
)
{
    return TitanVideo_GameUploadTexturePsm16(
        pixels, width, height, stride, texture_handle
    ) == TITAN_RESULT_OK;
}

static int TitanGameApi_VideoUploadTextureRgba32(
    const uint8_t *rgba,
    uint32_t width,
    uint32_t height,
    uint32_t stride_bytes,
    uint32_t *texture_handle
)
{
    return TitanVideo_GameUploadTextureRgba32(
        rgba, width, height, stride_bytes, texture_handle
    ) == TITAN_RESULT_OK;
}

static int TitanGameApi_VideoUploadTextureI8(
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    uint32_t *texture_handle
)
{
    return TitanVideo_GameUploadTextureI8(
        pixels, width, height, stride, texture_handle
    ) == TITAN_RESULT_OK;
}

static int TitanGameApi_VideoReleaseTexture(uint32_t texture_handle)
{
    return g_game.active
        ? TitanVideo_GameReleaseTexture(texture_handle)
        : 0;
}

static int TitanGameApi_VideoSetRenderState(
    const TitanGameRenderState *state
)
{
    TitanGameRenderState normalized;
    if (!g_game.active || state == 0 ||
        state->struct_size < offsetof(TitanGameRenderState, reserved))
        return 0;
    if (!TitanVideoGame_NormalizeRenderState(
            state,
            g_game.width,
            g_game.height,
            &normalized) ||
        !TitanVideo_GameSetRenderState(&normalized))
        return 0;
    g_game.render_state = normalized;
    return 1;
}

static int TitanGameApi_VideoDrawMaterialTriangles(
    const TitanGameMaterialVertex *vertices,
    uint32_t vertex_count,
    uint32_t texture0_handle,
    uint32_t texture1_handle
)
{
#if defined(__EE__) || defined(_EE)
    int drawn;

    if (!g_game.active || !g_game.frame_open || vertices == 0 ||
        vertex_count == 0u || vertex_count % 3u != 0u)
        return -1;

    /* P160.2: this internal batch bridge is PS2-target-only.
     *
     * Host runtime contracts deliberately link titan_game_runtime.c against
     * lightweight renderer stubs rather than the full GS titan_video.c.  The
     * P160 batch symbol therefore must not become a host link dependency.
     * On EE/R5900 the true material batch remains intact end-to-end. */
    drawn = TitanVideo_DrawGameMaterialTrianglesBatch(
        vertices,
        vertex_count,
        texture0_handle,
        texture1_handle
    );

    return drawn < 0 ? -2 : drawn;
#else

    uint32_t base;
    uint32_t drawn = 0u;
    const TitanGameRenderState *state = &g_game.render_state;
    if (!g_game.active || !g_game.frame_open || vertices == 0 ||
        vertex_count == 0u || vertex_count % 3u != 0u)
        return -1;
    for (base = 0u; base < vertex_count; base += 3u)
    {
        float sx[3], sy[3], reciprocal_w[3];
        unsigned int sz[3];
        unsigned int index;
        int visible = 1;
        for (index = 0u; index < 3u; index++)
        {
            const TitanGameMaterialVertex *vertex = &vertices[base + index];
            TitanVideoGameScreenVertex screen;
            if (!TitanVideoGame_MapClipVertex(
                    state,
                    vertex->x,
                    vertex->y,
                    vertex->z,
                    vertex->w,
                    &screen))
            {
                visible = 0;
                break;
            }
            sx[index] = screen.x;
            sy[index] = screen.y;
            sz[index] = screen.z;
            reciprocal_w[index] = screen.reciprocal_w;
        }
        if (!visible) continue;
        {
            const TitanGameMaterialVertex *a = &vertices[base];
            const TitanGameMaterialVertex *b = &vertices[base + 1u];
            const TitanGameMaterialVertex *c = &vertices[base + 2u];
            uint32_t ca = a->rgba, cb = b->rgba, cc = c->rgba;
            float q00 = TitanVideoGame_ResolveQ(a->q0, reciprocal_w[0]);
            float q01 = TitanVideoGame_ResolveQ(b->q0, reciprocal_w[1]);
            float q02 = TitanVideoGame_ResolveQ(c->q0, reciprocal_w[2]);
            float q10 = TitanVideoGame_ResolveQ(a->q1, reciprocal_w[0]);
            float q11 = TitanVideoGame_ResolveQ(b->q1, reciprocal_w[1]);
            float q12 = TitanVideoGame_ResolveQ(c->q1, reciprocal_w[2]);
            if (!TitanVideo_DrawGameMaterialTrianglePhysical(
                    sx[0],sy[0],sz[0],a->s0,a->t0,q00,a->s1,a->t1,q10,
                    sx[1],sy[1],sz[1],b->s0,b->t0,q01,b->s1,b->t1,q11,
                    sx[2],sy[2],sz[2],c->s0,c->t0,q02,c->s1,c->t1,q12,
                    (uint8_t)(ca>>24),(uint8_t)(ca>>16),(uint8_t)(ca>>8),(uint8_t)ca,
                    (uint8_t)(cb>>24),(uint8_t)(cb>>16),(uint8_t)(cb>>8),(uint8_t)cb,
                    (uint8_t)(cc>>24),(uint8_t)(cc>>16),(uint8_t)(cc>>8),(uint8_t)cc,
                    a->fog,b->fog,c->fog,
                    a->lod_fraction,b->lod_fraction,c->lod_fraction,
                    texture0_handle,texture1_handle))
                return drawn != 0u ? (int)drawn : -2;
        }
        drawn++;
    }
    return (int)drawn;

#endif
}

static int TitanGameApi_VideoDrawClipTriangles(
    const TitanGameClipVertex *vertices,
    uint32_t vertex_count,
    uint32_t texture_handle
)
{
    uint32_t base;
    uint32_t drawn = 0u;
    const TitanGameRenderState *state = &g_game.render_state;
    if (!g_game.active || !g_game.frame_open || vertices == 0 ||
        vertex_count == 0u || vertex_count % 3u != 0u)
        return -1;
    for (base = 0u; base < vertex_count; base += 3u)
    {
        float sx[3], sy[3];
        unsigned int sz[3];
        unsigned int i;
        int visible = 1;
        for (i = 0u; i < 3u; i++)
        {
            const TitanGameClipVertex *v = &vertices[base + i];
            TitanVideoGameScreenVertex screen;
            if (!TitanVideoGame_MapClipVertex(
                    state,
                    v->x,
                    v->y,
                    v->z,
                    v->w,
                    &screen))
            {
                visible = 0;
                break;
            }
            sx[i] = screen.x;
            sy[i] = screen.y;
            sz[i] = screen.z;
        }
        if (!visible) continue;
        if (texture_handle != 0u)
        {
            const TitanGameClipVertex *a = &vertices[base];
            const TitanGameClipVertex *b = &vertices[base + 1u];
            const TitanGameClipVertex *c = &vertices[base + 2u];
            if (!TitanVideo_DrawGameTexturedTrianglePhysical(
                    sx[0],sy[0],sz[0],a->u,a->v,
                    sx[1],sy[1],sz[1],b->u,b->v,
                    sx[2],sy[2],sz[2],c->u,c->v,
                    (uint8_t)(a->rgba >> 24),(uint8_t)(a->rgba >> 16),(uint8_t)(a->rgba >> 8),(uint8_t)a->rgba,
                    (uint8_t)(b->rgba >> 24),(uint8_t)(b->rgba >> 16),(uint8_t)(b->rgba >> 8),(uint8_t)b->rgba,
                    (uint8_t)(c->rgba >> 24),(uint8_t)(c->rgba >> 16),(uint8_t)(c->rgba >> 8),(uint8_t)c->rgba,
                    texture_handle))
                return drawn != 0u ? (int)drawn : -2;
        }
        else
        {
            uint32_t rgba = vertices[base].rgba;
            TitanVideo_DrawGameTrianglePhysical(
                sx[0],sy[0],sz[0],sx[1],sy[1],sz[1],sx[2],sy[2],sz[2],
                (uint8_t)(rgba >> 24),(uint8_t)(rgba >> 16),
                (uint8_t)(rgba >> 8),(uint8_t)rgba
            );
        }
        drawn++;
    }
    return (int)drawn;
}

static int TitanGameApi_VideoEndFrame(void)
{
    TitanResult result;
    if (!g_game.active || !g_game.frame_open) return 0;
    result = TitanVideo_EndFrame();
    g_game.frame_open = 0;
    return result == TITAN_RESULT_OK;
}

static const TitanGameApiV1 g_game_api = {
    TITAN_GAME_API_VERSION,
    sizeof(TitanGameApiV1),
    TITAN_GAME_FEATURE_Z16 |
        TITAN_GAME_FEATURE_COLORED_TRIANGLES |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_P16 |
        TITAN_GAME_FEATURE_TEXTURED_TRIANGLES |
        TITAN_GAME_FEATURE_FOUR_PAD_ABI |
        TITAN_GAME_FEATURE_CLIPSPACE_TRIANGLES |
        TITAN_GAME_FEATURE_RENDER_STATE |
        TITAN_GAME_FEATURE_PERSPECTIVE_STQ |
        TITAN_GAME_FEATURE_TWO_TEXTURE_MATERIAL |
        TITAN_GAME_FEATURE_VIEWPORT_SCISSOR |
        TITAN_GAME_FEATURE_HAPTICS |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32 |
        TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8,
    0u,
    TitanGameApi_RequestExit,
    TitanGameApi_ExitRequested,
    TitanGameApi_UptimeMs,
    TitanGameApi_UptimeUs,
    TitanGameApi_Alloc,
    TitanGameApi_Release,
    TitanGameApi_HeapUsed,
    TitanGameApi_HeapLimit,
    TitanGameApi_PollInput,
    TitanGameApi_GetPad,
    TitanGameApi_ConnectedPadCount,
    TitanGameApi_AudioBegin,
    TitanGameApi_AudioSubmit,
    TitanGameApi_AudioFreeFrames,
    TitanGameApi_AudioEnd,
    TitanGameApi_StorageOpen,
    TitanGameApi_StorageClose,
    TitanGameApi_StorageRead,
    TitanGameApi_StorageWrite,
    TitanGameApi_StorageSeek,
    TitanGameApi_StorageStat,
    TitanGameApi_StorageList,
    TitanGameApi_StorageMkdir,
    TitanGameApi_StorageRemove,
    TitanGameApi_StorageRename,
    TitanGameApi_VideoGetInfo,
    TitanGameApi_VideoBeginFrame,
    TitanGameApi_VideoSetMvp,
    TitanGameApi_VideoDrawIndexedTriangles,
    TitanGameApi_VideoUploadTexturePsm16,
    TitanGameApi_VideoEndFrame,
    TitanGameApi_VideoReleaseTexture,
    TitanGameApi_VideoDrawClipTriangles,
    TitanGameApi_VideoSetRenderState,
    TitanGameApi_VideoDrawMaterialTriangles,
    TitanGameApi_SetPadRumble,
    TitanGameApi_VideoUploadTextureRgba32,
    TitanGameApi_VideoUploadTextureI8
};

static int TitanGameRuntime_WaitForStoragePark(void)
{
    uint64_t started = TitanTime_NowMilliseconds();
    while (!TitanStorageScheduler_IsTitanGameWorkerParked())
    {
        uint64_t now = TitanTime_NowMilliseconds();
        if (now >= started && now - started >= TITAN_GAME_STORAGE_PARK_TIMEOUT_MS)
            return 0;
#if defined(__EE__) || defined(_EE)
        DelayThread(1000);
#else
        /* Host contract tests provide an immediate parked acknowledgement. */
        break;
#endif
    }
    return TitanStorageScheduler_IsTitanGameWorkerParked();
}

int TitanGameRuntime_Begin(
    const TitanGameSessionConfig *config,
    const TitanGameApiV1 **api
)
{
    uint32_t width = TITAN_GAME_DEFAULT_WIDTH;
    uint32_t height = TITAN_GAME_DEFAULT_HEIGHT;
    uint32_t heap_limit = TITAN_GAME_HEAP_DEFAULT_BYTES;

    if (api == 0 || g_game.active || g_game.teardown_pending ||
        g_game.storage_exclusive || g_pending_storage_root[0] == '\0') return 0;
    *api = 0;
    if (config != 0)
    {
        if (config->struct_size < offsetof(TitanGameSessionConfig, reserved))
            return 0;
        if (config->width != 0u) width = config->width;
        if (config->height != 0u) height = config->height;
        if (config->heap_limit_bytes != 0u) heap_limit = config->heap_limit_bytes;
    }
    if (width != TITAN_GAME_DEFAULT_WIDTH || height != TITAN_GAME_DEFAULT_HEIGHT)
        return 0;
    if (heap_limit < TITAN_GAME_HEAP_MIN_BYTES) heap_limit = TITAN_GAME_HEAP_MIN_BYTES;
    if (heap_limit > TITAN_GAME_HEAP_MAX_BYTES) heap_limit = TITAN_GAME_HEAP_MAX_BYTES;

    memset(&g_game, 0, sizeof(g_game));
    TitanGameRuntime_InitAllocationRegistry();
    memcpy(g_game.storage_root, g_pending_storage_root,
           sizeof(g_game.storage_root));
    g_game.width = width;
    g_game.height = height;
    g_game.heap_limit = heap_limit;
    TitanGameRuntime_SetIdentityMvp();
    memset(&g_game.render_state, 0, sizeof(g_game.render_state));
    g_game.render_state.struct_size = sizeof(g_game.render_state);
    g_game.render_state.viewport_width = width;
    g_game.render_state.viewport_height = height;
    g_game.render_state.scissor_width = width;
    g_game.render_state.scissor_height = height;
    g_game.render_state.depth_test = 1u;
    g_game.render_state.depth_write = 1u;
    g_game.render_state.depth_compare = TITAN_GAME_DEPTH_GEQUAL;
    g_game.render_state.combine_mode = TITAN_GAME_COMBINE_TEX0_MODULATE;
    g_game.render_state.sampler[0].linear_filter = 1u;
    g_game.render_state.sampler[1].linear_filter = 1u;

    /* The shell remains in-process but gives the game a single-owner storage
     * lane and a dedicated GS layout. Network/diagnostics threads are not
     * destroyed, so later online/IGR integration can persist around a game. */
    TitanAudio_StopMusic();
    TitanStorageScheduler_SetTitanGameExclusiveMode(1);
    g_game.storage_exclusive = 1;
    if (!TitanGameRuntime_WaitForStoragePark())
        goto fail;
    if (TitanVideo_BeginGameMode(width, height) != TITAN_RESULT_OK)
        goto fail;
    TitanVideo_GameResetTextures();
    if (!TitanVideo_GameSetRenderState(&g_game.render_state))
        goto fail;

    g_game.active = 1;
    *api = &g_game_api;
    return 1;

fail:
    if (TitanVideo_IsGameMode() &&
        TitanVideo_EndGameMode() != TITAN_RESULT_OK)
    {
        /* Begin did not publish the API, but video ownership may already have
         * crossed to the game. Preserve a teardown-only session so AppRuntime
         * can retry instead of releasing shell/storage ownership underneath a
         * faulted VIF1/PATH1 stream. */
        g_game.active = 0;
        g_game.teardown_pending = 1;
        return 0;
    }
    if (g_game.storage_exclusive)
        TitanStorageScheduler_SetTitanGameExclusiveMode(0);
    memset(&g_game, 0, sizeof(g_game));
    memset(g_pending_storage_root, 0, sizeof(g_pending_storage_root));
    TitanAudio_StartMusic();
    return 0;
}

int TitanGameRuntime_End(void)
{
    if (!g_game.active && !g_game.teardown_pending &&
        !g_game.storage_exclusive) return 1;
    if (g_game.frame_open)
    {
        (void)TitanVideo_EndFrame();
        g_game.frame_open = 0;
    }

    /* P162 ownership is transactional.  Do not dismantle the game session,
     * release its storage lane, or restart shell audio until video teardown has
     * actually returned VIF1/VU1 ownership.  A faulted PATH1 stream may need a
     * later retry after the hardware becomes idle; preserving g_game here makes
     * that retry possible and prevents the shell from pretending it is safe. */
    if (TitanVideo_IsGameMode() &&
        TitanVideo_EndGameMode() != TITAN_RESULT_OK)
        return 0;

    TitanGameApi_AudioEnd();
    TitanInput_StopAllRumble();
    TitanGameRuntime_ReleaseAllAllocations();
    if (g_game.storage_exclusive)
        TitanStorageScheduler_SetTitanGameExclusiveMode(0);
    memset(&g_game, 0, sizeof(g_game));
    memset(g_pending_storage_root, 0, sizeof(g_pending_storage_root));
    TitanAudio_StartMusic();
    return 1;
}

int TitanGameRuntime_IsActive(void)
{
    return g_game.active || g_game.teardown_pending;
}
const TitanGameApiV1 *TitanGameRuntime_GetApi(void)
{
    return g_game.active ? &g_game_api : 0;
}
