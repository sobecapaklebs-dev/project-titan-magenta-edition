#ifndef PROJECT_TITAN_VIDEO_MODE_H
#define PROJECT_TITAN_VIDEO_MODE_H

#include "titan_result.h"

typedef enum TitanVideoOutputMode
{
    TITAN_VIDEO_OUTPUT_LEGACY = 0,
    TITAN_VIDEO_OUTPUT_HDTV_720P,
    TITAN_VIDEO_OUTPUT_MODE_COUNT
} TitanVideoOutputMode;

typedef struct TitanVideoModeInfo
{
    TitanVideoOutputMode id;
    const char *name;
    unsigned int signal_width;
    unsigned int signal_height;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned int framebuffer_count;
    int framebuffer_psm;
    int progressive;
} TitanVideoModeInfo;

TitanResult TitanVideoMode_Initialize(
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
);

/* Stops both read circuits only after GIF/VIF1 are idle and a VSync boundary
 * has been reached. The renderer may then replace the framebuffer allocation
 * without the CRTC reading released VRAM. */
TitanResult TitanVideoMode_BeginReconfigure(void);

/* Commits a framebuffer that has already been allocated and cleared. */
TitanResult TitanVideoMode_Commit(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
);

/* Convenience wrapper for callers that do not need to replace VRAM. */
TitanResult TitanVideoMode_SetOutput(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
);

void TitanVideoMode_Present(unsigned int framebuffer_address);

TitanVideoOutputMode TitanVideoMode_GetOutput(void);
int TitanVideoMode_IsProgressive(void);
const TitanVideoModeInfo *TitanVideoMode_GetInfo(void);

#endif
