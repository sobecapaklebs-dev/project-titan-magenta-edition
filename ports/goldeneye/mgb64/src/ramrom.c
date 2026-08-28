#include <ultra64.h>
#include "ramrom.h"
#include <macro.h>

/**
 * @file ramrom.c
 * This file contains code to handle reading and writing rom addresses.
 */

#ifdef NATIVE_PORT
/*
 * PC ROM I/O implementation.
 *
 * On N64, ROM is accessed via the PI (Parallel Interface) using DMA.
 * On PC, the entire .z64 ROM is loaded into g_romData and all DMA
 * requests become synchronous memcpy operations.
 *
 * The 'source' parameter to romCopy comes in 3 forms:
 *   1. Pointer into g_romData — from patched file_resource_table hw_address
 *   2. Small integer cast to void* — direct ROM offset (< ROM size)
 *   3. Pointer to a segment symbol variable — dereference to get ROM offset
 */
#include <string.h>
#include <stdio.h>
#include "platform/rom_io.h"

extern u8  *g_romData;
extern u32  g_romSize;

OSIoMesg memoryMesgMB;
OSMesg memoryMesg;
OSMesgQueue memoryMesgQueue;

void romCreateMesgQueue(void)
{
    osCreateMesgQueue(&memoryMesgQueue, &memoryMesg, 1);
}

static u32 resolveRomOffset(void *source) {
    uintptr_t addr = (uintptr_t)source;

    /* Case 1: pointer into the loaded ROM buffer */
    if (g_romData &&
        addr >= (uintptr_t)g_romData &&
        addr <  (uintptr_t)(g_romData + g_romSize)) {
        return (u32)(addr - (uintptr_t)g_romData);
    }

    /* Case 2: small integer = direct ROM offset */
    if (addr < (uintptr_t)g_romSize) {
        return (u32)addr;
    }

    /* Case 3: `source` is a pointer to a segment variable holding a stored ROM
     * offset — dereference it. This is a real in-process pointer from the port's
     * segment table, not attacker-supplied data, and doRomCopy() re-validates the
     * resulting offset against g_romSize before any copy. (Further hardening this
     * dereference against a hypothetical wild pointer is tracked separately and
     * must not regress the legitimate segment-variable path.) */
    return *(u32 *)source;
}

void doRomCopy(void *target, void *source, u32 size)
{
    if (size == 0) return;
    u32 offset = resolveRomOffset(source);
    /* Bounds-check without the addition: offset+size can wrap (both are u32 and
     * `size` is ROM-derived — e.g. music track / bank sizes read out of the
     * file), which would let a crafted image slip a copy past the end of the
     * loaded ROM buffer. */
    if (size <= g_romSize && offset <= g_romSize - size) {
        if (!platformRomRead(offset, target, size))
            fprintf(stderr, "[ROM] doRomCopy: backend read failed offset=0x%08x size=0x%x\n",
                    offset, size);
    } else {
        fprintf(stderr, "[ROM] doRomCopy: out of bounds offset=0x%08x size=0x%x romSize=0x%x\n",
                offset, size, g_romSize);
    }
}

void romReceiveMesg(void)
{
    /* No-op on PC — copy is synchronous */
}

void romCopy(void *target, void *source, u32 size)
{
    doRomCopy(target, source, size);
}

void *romCopyAligned(void *target, void *source, s32 length)
{
    /* On PC, DMA alignment constraints don't apply.
     * We still respect the alignment math for source/target offset
     * so callers get the expected return pointer. */
    uintptr_t src = (uintptr_t)source;
    uintptr_t src_aligned = src & ~(uintptr_t)0xF;
    uintptr_t src_off = src - src_aligned;
    uintptr_t tgt = (uintptr_t)target;
    uintptr_t tgt_aligned = (tgt + 0xF) & ~(uintptr_t)0xF;

    u32 copy_len = (u32)((src_off + length + 0xF) & ~(uintptr_t)0xF);
    romCopy((void *)tgt_aligned, (void *)src_aligned, copy_len);
    return (void *)(tgt_aligned + src_off);
}

void doRomWrite(void *source, void *target, u32 size)
{
    /* ROM write not applicable on PC */
    (void)source; (void)target; (void)size;
}

void romWrite(void *source, void *target, u32 size)
{
    (void)source; (void)target; (void)size;
}

#else /* !NATIVE_PORT — original N64 code */

OSIoMesg memoryMesgMB;
OSMesg memoryMesg;
OSMesgQueue memoryMesgQueue;

/**
 * 6760	70005B60
 * external
 * romCreateMesgQueue
 * creates a message queue
 */
void romCreateMesgQueue(void)
{
    osCreateMesgQueue(&memoryMesgQueue, &memoryMesg, 1);
}

/**
 * 6790	70005B90
 * doRomCopy
 * invalidate cache and do pi dma
 */
void doRomCopy(void *target, void *source, u32 size)
{
    osInvalDCache(target, size);
    osPiStartDma(&memoryMesgMB, OS_MESG_PRI_NORMAL, OS_READ, source, target, size, &memoryMesgQueue);
}

/**
 * 67F0	70005BF0
 * romReceiveMesg
 * receives a message queue
 */
void romReceiveMesg(void)
{
    osRecvMesg(&memoryMesgQueue, NULL, OS_MESG_BLOCK);
}

/**
 * 681C	70005C1C
 * external
 * romCopy
 * copy from rom to ram
 */
void romCopy(void *target, void *source, u32 size)
{
    doRomCopy(target, source, size);
    romReceiveMesg();
}

/**
 * 6844	70005C44
 * external
 * romCopyAligned
 * aligns data, does a romCopy(), then returns aligned pointer to target
 */
s32 romCopyAligned(void *target, void *source, s32 length)
{
    s32 target_offset;
    s32 *target_aligned;
    s32 *source_aligned;
    s32 *source_offset;

    source_aligned = align_addr_even((s32)source);
    source_offset = (s32)source - (s32)source_aligned;
    target_aligned = ALIGN16_a((s32)target);
    target_offset = source_offset;
    romCopy(target_aligned, source_aligned, ALIGN16_a((s32)source_offset + length));
    return ((s32)target_aligned + target_offset);
}

/**
 * 68A8	70005CA8
 * doRomWrite
 * actually writes to rom (buffer on Indy)
 */
void doRomWrite(void *source, void *target, u32 size)
{
    osWritebackDCache(source, size);
    osPiStartDma(&memoryMesgMB, OS_MESG_PRI_NORMAL, OS_WRITE, target, source, size, &memoryMesgQueue);
}

/**
 * 6908	70005D08
 * external
 * romWrite
 * let's write to the rom (buffer on Indy)
 */
void romWrite(void *source, void *target, u32 size)
{
    doRomWrite(source, target, size);
    romReceiveMesg();
}

#endif /* NATIVE_PORT */
