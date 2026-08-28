#include "titan_video_memory.h"
#include "titan_vram_boot_guard.h"

#ifdef TITAN_VIDEO_RENDERER_GS

#include <graph_vram.h>
#include <gs_psm.h>

#include <limits.h>
#include <string.h>

typedef struct TitanVideoMemoryState
{
    int initialized;
    unsigned int reserved_bytes;
    unsigned int allocation_count;
    TitanVideoMemoryAllocation allocations[
        TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS
    ];
} TitanVideoMemoryState;

static TitanVideoMemoryState g_video_memory;

static int TitanVideoMemory_AlignUp(
    unsigned int value,
    unsigned int alignment,
    unsigned int *result
)
{
    unsigned int mask;

    if (result == 0)
    {
        return 0;
    }
    if (alignment == 0u)
    {
        *result = value;
        return 1;
    }
    if ((alignment & (alignment - 1u)) != 0u)
    {
        return 0;
    }

    mask = alignment - 1u;
    if (value > UINT_MAX - mask)
    {
        return 0;
    }

    *result = (value + mask) & ~mask;
    return 1;
}

static unsigned int TitanVideoMemory_GetWordsPerRow(
    unsigned int width,
    int psm
)
{
    unsigned int aligned_width;

    aligned_width = width;

    if (
        width > 16u &&
        !TitanVideoMemory_AlignUp(
            width,
            psm == GS_PSM_8 ? 128u : 64u,
            &aligned_width
        )
    )
    {
        return 0u;
    }

    switch (psm)
    {
        case GS_PSM_32:
        case GS_PSM_24:
            return aligned_width;

        case GS_PSM_16:
        case GS_PSM_16S:
        case GS_PSMZ_16:
            return aligned_width >> 1u;

        case GS_PSM_8:
            return aligned_width >> 2u;

        default:
            return 0u;
    }
}

static unsigned int TitanVideoMemory_CalculateBytes(
    unsigned int width,
    unsigned int height,
    int psm,
    int alignment
)
{
    unsigned int words_per_row;
    unsigned int word_alignment;
    unsigned int aligned_words;
    unsigned long long words;
    unsigned long long bytes;

    words_per_row = TitanVideoMemory_GetWordsPerRow(width, psm);
    if (words_per_row == 0u || height == 0u)
    {
        return 0u;
    }

    words = (unsigned long long)words_per_row * (unsigned long long)height;
    if (words > UINT_MAX)
    {
        return 0u;
    }

    word_alignment = alignment == TITAN_VIDEO_MEMORY_ALIGN_PAGE
        ? 2048u
        : 64u;
    if (!TitanVideoMemory_AlignUp(
        (unsigned int)words,
        word_alignment,
        &aligned_words
    ))
    {
        return 0u;
    }

    bytes = (unsigned long long)aligned_words * 4ull;
    if (bytes > UINT_MAX || bytes > TITAN_VIDEO_MEMORY_TOTAL_BYTES)
    {
        return 0u;
    }

    return (unsigned int)bytes;
}

TitanResult TitanVideoMemory_Initialize(void)
{
    memset(&g_video_memory, 0, sizeof(g_video_memory));
    graph_vram_clear();
    g_video_memory.initialized = 1;
    return TITAN_RESULT_OK;
}

int TitanVideoMemory_Allocate(
    const char *name,
    unsigned int width,
    unsigned int height,
    int psm,
    int alignment
)
{
    unsigned int bytes;
    unsigned int index;
    unsigned int allocation_end_bytes;
    int address;

    if (!g_video_memory.initialized)
    {
        return -1;
    }

    bytes = TitanVideoMemory_CalculateBytes(
        width,
        height,
        psm,
        alignment
    );

    if (
        bytes == 0u ||
        bytes > TITAN_VIDEO_MEMORY_TOTAL_BYTES ||
        g_video_memory.reserved_bytes >
            TITAN_VIDEO_MEMORY_TOTAL_BYTES - bytes
    )
    {
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    if (TitanVramBootGuard_ShouldInjectFailure(bytes))
    {
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    if (!TitanVramBootGuard_AllowAllocation(name, bytes))
    {
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    index = TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS;
    for (
        unsigned int candidate = 0u;
        candidate < TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS;
        candidate++
    )
    {
        if (!g_video_memory.allocations[candidate].active)
        {
            index = candidate;
            break;
        }
    }

    if (index >= TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS)
    {
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    address = graph_vram_allocate(
        width,
        height,
        psm,
        alignment == TITAN_VIDEO_MEMORY_ALIGN_PAGE
            ? GRAPH_ALIGN_PAGE
            : GRAPH_ALIGN_BLOCK
    );
    if (address < 0)
    {
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    if (
        (unsigned int)address > UINT_MAX / 4u ||
        bytes > UINT_MAX - ((unsigned int)address * 4u)
    )
    {
        graph_vram_free(address);
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    allocation_end_bytes = ((unsigned int)address * 4u) + bytes;
    if (allocation_end_bytes > TITAN_VIDEO_MEMORY_TOTAL_BYTES)
    {
        graph_vram_free(address);
        TitanVramBootGuard_RecordAllocation(name, bytes, 0);
        return -1;
    }

    if (name != 0)
    {
        strncpy(
            g_video_memory.allocations[index].name,
            name,
            TITAN_VIDEO_MEMORY_NAME_CAPACITY - 1u
        );
        g_video_memory.allocations[index]
            .name[TITAN_VIDEO_MEMORY_NAME_CAPACITY - 1u] = '\0';
    }
    else
    {
        g_video_memory.allocations[index].name[0] = '\0';
    }
    g_video_memory.allocations[index].address = (unsigned int)address;
    g_video_memory.allocations[index].bytes = bytes;
    g_video_memory.allocations[index].active = 1;
    g_video_memory.reserved_bytes = allocation_end_bytes;
    g_video_memory.allocation_count++;
    TitanVramBootGuard_RecordAllocation(name, bytes, 1);

    return address;
}

void TitanVideoMemory_Free(unsigned int address)
{
    unsigned int index;

    if (!g_video_memory.initialized)
    {
        return;
    }

    for (
        index = 0u;
        index < TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS;
        index++
    )
    {
        TitanVideoMemoryAllocation *allocation;

        allocation = &g_video_memory.allocations[index];
        if (!allocation->active || allocation->address != address)
        {
            continue;
        }

        graph_vram_free((int)address);
        TitanVramBootGuard_RecordReclaim(1u);
        g_video_memory.reserved_bytes = address * 4u;

        memset(allocation, 0, sizeof(*allocation));
        if (g_video_memory.allocation_count > 0u)
        {
            g_video_memory.allocation_count--;
        }
        return;
    }
}

int TitanVideoMemory_FindAddress(const char *name)
{
    unsigned int index;

    if (!g_video_memory.initialized || name == 0)
    {
        return -1;
    }

    for (index = 0u; index < TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS; index++)
    {
        const TitanVideoMemoryAllocation *allocation;

        allocation = &g_video_memory.allocations[index];
        if (
            allocation->active &&
            allocation->name[0] != '\0' &&
            strcmp(allocation->name, name) == 0
        )
        {
            return (int)allocation->address;
        }
    }

    return -1;
}

unsigned int TitanVideoMemory_GetReservedBytes(void)
{
    return g_video_memory.reserved_bytes;
}

unsigned int TitanVideoMemory_GetFreeBytes(void)
{
    if (g_video_memory.reserved_bytes >= TITAN_VIDEO_MEMORY_TOTAL_BYTES)
    {
        return 0u;
    }

    return TITAN_VIDEO_MEMORY_TOTAL_BYTES -
        g_video_memory.reserved_bytes;
}

unsigned int TitanVideoMemory_GetAllocationCount(void)
{
    return g_video_memory.allocation_count;
}

const TitanVideoMemoryAllocation *TitanVideoMemory_GetAllocation(
    unsigned int index
)
{
    if (
        index >= TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS ||
        !g_video_memory.allocations[index].active
    )
    {
        return 0;
    }

    return &g_video_memory.allocations[index];
}

#else

TitanResult TitanVideoMemory_Initialize(void)
{
    return TITAN_RESULT_OK;
}

int TitanVideoMemory_Allocate(
    const char *name,
    unsigned int width,
    unsigned int height,
    int psm,
    int alignment
)
{
    (void)name;
    (void)width;
    (void)height;
    (void)psm;
    (void)alignment;
    return 0;
}

void TitanVideoMemory_Free(unsigned int address)
{
    (void)address;
}

int TitanVideoMemory_FindAddress(const char *name)
{
    (void)name;
    return -1;
}

unsigned int TitanVideoMemory_GetReservedBytes(void)
{
    return 0u;
}

unsigned int TitanVideoMemory_GetFreeBytes(void)
{
    return TITAN_VIDEO_MEMORY_TOTAL_BYTES;
}

unsigned int TitanVideoMemory_GetAllocationCount(void)
{
    return 0u;
}

const TitanVideoMemoryAllocation *TitanVideoMemory_GetAllocation(
    unsigned int index
)
{
    (void)index;
    return 0;
}

#endif
