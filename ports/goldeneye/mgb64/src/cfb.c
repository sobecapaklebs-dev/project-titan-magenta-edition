#include <ultra64.h>
#include <bondgame.h>

#include "fr.h"

#if defined(NATIVE_PORT)
#define NATIVE_ZERO_INIT = {0}
#else
#define NATIVE_ZERO_INIT
#endif

/* SCREEN_HEIGHT #define changes based on version (PAL or NTSC) */
#ifdef PROJECT_TITAN_TGR
u8 cfb_16[NUM_VIDEO_FRAME_BUFFERS][64] NATIVE_ZERO_INIT;
#else
u8 cfb_16[NUM_VIDEO_FRAME_BUFFERS][SCREEN_WIDTH * SCREEN_HEIGHT * 2] NATIVE_ZERO_INIT;
#endif

#undef NATIVE_ZERO_INIT
