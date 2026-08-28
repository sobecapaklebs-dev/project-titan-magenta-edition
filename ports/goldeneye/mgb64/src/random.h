#ifndef _RANDOM_H_
#define _RANDOM_H_
#include <ultra64.h>

/* Definition (random.c: g_randomSeed is u64; the retail asm does a 64-bit
 * `daddiu`/`sd`) takes a u64. The prior u32 prototype was prototype drift:
 * benign on native (callers pass osGetCount()/a literal, both zero-extend to
 * the same value) but a hard function-signature trap under wasm's indirect-
 * call type checks. The definition is truth. */
void randomSetSeed(u64 param_1);
u32 randomGetNext(void);
u32 randomGetNextFrom(u64 *param_1);
#ifdef NATIVE_PORT
u64 pcRandomGetNextCallCount(void);
void pcRandomTraceSetRamromActive(s32 active);
#endif

// 4294967295 == UINT_MAX
#define RANDOMGETNEXT_F32() ((f32) (u32)randomGetNext() * (1.0f / 4294967295))

#endif
