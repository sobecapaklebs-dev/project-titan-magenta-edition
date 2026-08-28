#ifndef _UNK_0C0A70_H_
#define _UNK_0C0A70_H_
#include <ultra64.h>

extern s32 lastFrameCounter;
extern s32 currentFrameCounter;
extern s32 speedgraphframes;

#if defined (BUGFIX_R1)
extern f32 jpD_800484CC;
extern f32 jpD_800484D0;
#endif

extern s32 previousFrameCounter;
extern s32 halfFrameCounter;
extern s32 isFrameCounterOdd;
extern s32 halfMinusPreviousCounter;
extern u32 copy_of_osgetcount_value_0;
extern u32 copy_of_osgetcount_value_1;
extern s32 frameDelay;

void store_osgetcount(void);
void waitForNextFrame(void);
void updateFrameCounters(s32 deltaFrames);

#ifdef NATIVE_PORT
/* FID-0033 0-tick purity fuzz (see docs/design/UNCAPPED_FPS_PLAN.md Task 4).
 * Set to 1 for the current frame when GE007_UNCAP_FUZZ armed it as a
 * render-only (0 sim-tick) frame; 0 otherwise (and always 0 in normal play).
 * Read by sim code that must not advance on render-only frames. */
extern s32 g_pcUncapRenderOnlyFrame;
#endif

#endif
