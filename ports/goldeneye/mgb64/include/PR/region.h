#ifndef _REGION_H_
#define _REGION_H_

/*
 * Clean-room compatibility declarations for libultra-style fixed-size memory
 * regions. Only the public layout, constants, and function prototypes live
 * here; matching-target implementations remain separate.
 */

#ifdef _LANGUAGE_C_PLUS_PLUS
extern "C" {
#endif

#include <PR/ultratypes.h>

#define ALIGNSZ (sizeof(long long))
#define ALIGNOFFST (ALIGNSZ - 1)
#define BUF_CTRL_SIZE ALIGNSZ

#define MAX_BUFCOUNT 0x8000
#define BUF_FREE_WO_NEXT 0x8000

#define OS_RG_ALIGN_2B 2
#define OS_RG_ALIGN_4B 4
#define OS_RG_ALIGN_8B 8
#define OS_RG_ALIGN_16B 16

#define OS_RG_ALIGN_DEFAULT OS_RG_ALIGN_8B

#define ALIGN(s, align) (((u32)(s) + ((align) - 1)) & ~((align) - 1))

typedef struct _Region_s {
    u8 *r_startBufferAddress;
    u8 *r_endAddress;
    s32 r_bufferSize;
    s32 r_bufferCount;
    u16 r_freeList;
    u16 r_alignSize;
} OSRegion;

#define RP(x) rp->r_##x

void *osCreateRegion(void *region, u32 start, u32 size, u32 bufferSize);
void *osMalloc(void *region);
void osFree(void *region, void *addr);
s32 osGetRegionBufCount(void *region);
s32 osGetRegionBufSize(void *region);

#ifdef _LANGUAGE_C_PLUS_PLUS
}
#endif

#endif
