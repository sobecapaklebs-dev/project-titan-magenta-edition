#ifndef _BOOT_H_
#define _BOOT_H_
#include <ultra64.h>

#ifdef NATIVE_PORT
uintptr_t get_csegmentSegmentStart(void);
uintptr_t get_cdataSegmentRomStart(void);
uintptr_t get_cdataSegmentRomEnd(void);
uintptr_t get_inflateSegmentRomStart(void);
uintptr_t get_inflateSegmentRomEnd(void);
u32 jump_decompressfile(uintptr_t source, uintptr_t target, uintptr_t buffer);
#else
u32 get_csegmentSegmentStart(void);
u32 get_cdataSegmentRomStart(void);
u32 get_cdataSegmentRomEnd(void);
u32 get_inflateSegmentRomStart(void);
u32 get_inflateSegmentRomEnd(void);
u32 jump_decompressfile(u32 source, u32 target, u32 buffer);
#endif

#endif
