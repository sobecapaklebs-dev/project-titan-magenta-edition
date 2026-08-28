#ifndef PROJECT_TITAN_VIDEO_MEMORY_H
#define PROJECT_TITAN_VIDEO_MEMORY_H

#include "titan_result.h"

#define TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS 16u
#define TITAN_VIDEO_MEMORY_TOTAL_BYTES (4u * 1024u * 1024u)
#define TITAN_VIDEO_MEMORY_NAME_CAPACITY 32u

#define TITAN_VIDEO_MEMORY_ALIGN_BLOCK 0
#define TITAN_VIDEO_MEMORY_ALIGN_PAGE 1

typedef struct TitanVideoMemoryAllocation
{
    char name[TITAN_VIDEO_MEMORY_NAME_CAPACITY];
    unsigned int address;
    unsigned int bytes;
    int active;
} TitanVideoMemoryAllocation;

TitanResult TitanVideoMemory_Initialize(void);

int TitanVideoMemory_Allocate(
    const char *name,
    unsigned int width,
    unsigned int height,
    int psm,
    int alignment
);

void TitanVideoMemory_Free(unsigned int address);

/* Finds a resident allocation without mutating the stack-style GS allocator. */
int TitanVideoMemory_FindAddress(const char *name);

unsigned int TitanVideoMemory_GetReservedBytes(void);
unsigned int TitanVideoMemory_GetFreeBytes(void);
unsigned int TitanVideoMemory_GetAllocationCount(void);

const TitanVideoMemoryAllocation *TitanVideoMemory_GetAllocation(
    unsigned int index
);

#endif
