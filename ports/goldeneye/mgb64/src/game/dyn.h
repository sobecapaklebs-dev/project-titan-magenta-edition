#ifndef _DYN_H_
#define _DYN_H_

#include <ultra64.h>

void dynInit(void);
void dynInitMemory(void);
Gfx *dynGetMasterDisplayList(void);
s32 dynGetFreeGfx2(Gfx *gdl);
void *dynAllocate7F0BD6C4(s32 count);
Mtx *dynAllocateMatrix(void);
void *dynAllocate7F0BD6F8(s32 count);
void *dynAllocate(s32 size);
void dynSwapBuffers(void);
void dynRemovedFunc(Gfx *gdl);
s32 dynGetFreeGfx(Gfx *gdl);
s32 dynGetFreeVtx(void);
void dynDrawMembars(Gfx *gdl);

#ifdef NATIVE_PORT
/* Fail-closed dyn-allocator contract (M1.2). g_dyn_overflow_count is the
 * per-frame render-health counter (reset in dynGetMasterDisplayList); nonzero
 * means the arena was exhausted and a draw/matrix was dropped this frame.
 * dynIsOverflowMatrix() reports whether a pointer from dynAllocateMatrix() is the
 * shared overflow-scratch matrix so a caller can skip its draw. */
extern s32 g_dyn_overflow_count;
int dynIsOverflowMatrix(const void *m);
#endif

#endif
