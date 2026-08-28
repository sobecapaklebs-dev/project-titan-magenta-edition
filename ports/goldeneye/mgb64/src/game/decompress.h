#ifndef _DECOMPRESS_H_
#define _DECOMPRESS_H_
#include <ultra64.h>
#include <inflate/inflate.h>


u32 decompressdata(u8 *src, u8 *dst, struct huft *hlist);
#ifdef NATIVE_PORT
u8 *rzipGetSomething(void);
#else
s32 rzipGetSomething(void);
#endif

#endif
