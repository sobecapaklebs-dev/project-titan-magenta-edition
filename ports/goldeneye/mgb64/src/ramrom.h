#ifndef _RAMROM_H_
#define _RAMROM_H_
#include <ultra64.h>

void romCreateMesgQueue(void);
void romCopy(void *target, void *source, u32 size);
#ifdef NATIVE_PORT
void *romCopyAligned(void *target, void *source, s32 length);
#else
s32 romCopyAligned(void *target, void *source, s32 length);
#endif
void romWrite(void *source, void *target, u32 size);

#endif
