#ifndef _MEMA_H_
#define _MEMA_H_

#include <ultra64.h>

#ifdef NATIVE_PORT
typedef uintptr_t mema_addr_t;
#else
typedef s32 mema_addr_t;
#endif

void memaInit(void);
void memaReset(void *heapaddr, u32 heapsize);
void memaSingleDefragPass(void);
void *memaAlloc(u32 size);
void memaFree(void *addr, s32 size);
void memaDumpPrePostMerge(void);
u32 memaGetLongestFree(void);
s32 memaRealloc(mema_addr_t addr, u32 oldsize, u32 newsize);

#endif
