#include "titan_video_mode.h"

#ifdef TITAN_VIDEO_RENDERER_GS

#include <dma.h>
#include <graph.h>
#include <gs_privileged.h>
#include <gs_psm.h>
#include <kernel.h>
#include <stdio.h>
#if defined(__EE__) || defined(_EE)
#include <fcntl.h>
#include <unistd.h>
#endif

#define TITAN_VIDEO_MODE_DMA_TIMEOUT 1000000
#define TITAN_VIDEO_LEGACY_WIDTH 640u
#define TITAN_VIDEO_LEGACY_HEIGHT 448u
#define TITAN_VIDEO_HD_WIDTH 1280u
#define TITAN_VIDEO_HD_HEIGHT 720u

/* The generic PS2SDK 720p window (420,40) is electrically valid, but the
 * physical-hardware test showed the complete 1280x720 scanout shifted down
 * and to the right. These coordinates match the hardware-centred 720p window
 * already used by the integrated Titan UL/GSM timing table. They affect only
 * the CRTC window; framebuffer allocation, page flipping and UI geometry are
 * unchanged. */
#define TITAN_VIDEO_HDTV_720P_DX 302
#define TITAN_VIDEO_HDTV_720P_DY 24

/* GS DISPLAY supports 1..16 horizontal and 1..4 vertical magnification. */
#define TITAN_VIDEO_MAX_MAGH 16u
#define TITAN_VIDEO_MAX_MAGV 4u

static void TitanVideoMode_SwitchProbe(const char *stage, int result)
{
#if defined(__EE__) || defined(_EE)
    const char *primary = "mass:/TITAN/video_switch_probe.log";
    const char *fallback = "mass:/video_switch_probe.log";
    char line[160];
    int fd;
    int length;

    if (stage == 0)
        return;

    fd = open(primary, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        fd = open(fallback, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;

    length = snprintf(line, sizeof(line), "mode-stage=%s result=%d\n", stage, result);
    if (length > 0)
    {
        if ((unsigned int)length >= sizeof(line))
            length = (int)sizeof(line) - 1;
        (void)write(fd, line, (size_t)length);
    }
    close(fd);
#else
    (void)stage;
    (void)result;
#endif
}

typedef struct TitanVideoTiming
{
    int graph_mode;
    int interlace;
    int ffmd;
    int flicker_filter;
    int default_x;
    int default_y;
    unsigned int active_width;
    unsigned int active_height;
    int filtered_read_circuits;
} TitanVideoTiming;

typedef struct TitanVideoModeState
{
    int initialized;
    int base_region;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned int framebuffer_count;
    int framebuffer_psm;
    TitanVideoOutputMode output;
    TitanVideoModeInfo info;
} TitanVideoModeState;

static TitanVideoModeState g_video_mode;

static TitanVideoTiming TitanVideoMode_GetTiming(
    TitanVideoOutputMode output
)
{
    TitanVideoTiming timing;

    if (output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        timing.graph_mode = GRAPH_MODE_HDTV_720P;
        timing.interlace = GRAPH_MODE_NONINTERLACED;
        timing.ffmd = GRAPH_MODE_FRAME;
        timing.flicker_filter = GRAPH_DISABLE;
        timing.default_x = TITAN_VIDEO_HDTV_720P_DX;
        timing.default_y = TITAN_VIDEO_HDTV_720P_DY;
        timing.active_width = 1280u;
        timing.active_height = 720u;
        timing.filtered_read_circuits = 0;
        return timing;
    }

    timing.graph_mode = g_video_mode.base_region;
    timing.interlace = GRAPH_MODE_INTERLACED;
    timing.ffmd = GRAPH_MODE_FIELD;
    timing.flicker_filter = GRAPH_ENABLE;
    timing.default_x = g_video_mode.base_region == GRAPH_MODE_PAL
        ? 680
        : 652;
    timing.default_y = g_video_mode.base_region == GRAPH_MODE_PAL
        ? 72
        : 50;
    timing.active_width = 2560u;
    timing.active_height = g_video_mode.base_region == GRAPH_MODE_PAL
        ? 512u
        : 448u;
    timing.filtered_read_circuits = 1;
    return timing;
}

static int TitanVideoMode_ValidateFramebuffer(
    TitanVideoOutputMode output,
    unsigned int width,
    unsigned int height,
    unsigned int count,
    int psm
)
{
    if (output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        return
            width == TITAN_VIDEO_HD_WIDTH &&
            height == TITAN_VIDEO_HD_HEIGHT &&
            (count == 1u || count == 2u) &&
            psm == GS_PSM_16;
    }

    return
        width == TITAN_VIDEO_LEGACY_WIDTH &&
        height == TITAN_VIDEO_LEGACY_HEIGHT &&
        count == 2u &&
        psm == GS_PSM_32;
}

static int TitanVideoMode_CanDisplay(
    const TitanVideoTiming *timing,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height
)
{
    unsigned int horizontal_magnification;
    unsigned int vertical_magnification;

    if (framebuffer_width == 0u || framebuffer_height == 0u)
    {
        return 0;
    }

    horizontal_magnification =
        timing->active_width / framebuffer_width;
    vertical_magnification =
        timing->active_height / framebuffer_height;

    /* PAL has 512 active interlaced lines while the renderer deliberately
     * keeps the same 640x448 surface as NTSC. Integer magnification plus
     * centering is valid here; exact divisibility is only incidental in 480i
     * and native 720p. */
    return
        horizontal_magnification >= 1u &&
        horizontal_magnification <= TITAN_VIDEO_MAX_MAGH &&
        vertical_magnification >= 1u &&
        vertical_magnification <= TITAN_VIDEO_MAX_MAGV &&
        framebuffer_width * horizontal_magnification <=
            timing->active_width &&
        framebuffer_height * vertical_magnification <=
            timing->active_height;
}

static void TitanVideoMode_ProgramFramebuffer(
    const TitanVideoTiming *timing,
    unsigned int framebuffer_address
)
{
    if (timing->filtered_read_circuits)
    {
        graph_set_framebuffer_filtered(
            (int)framebuffer_address,
            (int)g_video_mode.framebuffer_width,
            g_video_mode.framebuffer_psm,
            0,
            0
        );
        return;
    }

    graph_set_framebuffer(
        0,
        (int)framebuffer_address,
        (int)g_video_mode.framebuffer_width,
        g_video_mode.framebuffer_psm,
        0,
        0
    );
    graph_set_framebuffer(
        1,
        (int)framebuffer_address,
        (int)g_video_mode.framebuffer_width,
        g_video_mode.framebuffer_psm,
        0,
        0
    );
}

static int TitanVideoMode_ProgramDisplay(
    const TitanVideoTiming *timing
)
{
    unsigned int horizontal_magnification;
    unsigned int vertical_magnification;
    unsigned int displayed_width;
    unsigned int displayed_height;
    int display_x;
    int display_y;
    u64 display1;
    u64 display2;

    if (!TitanVideoMode_CanDisplay(
        timing,
        g_video_mode.framebuffer_width,
        g_video_mode.framebuffer_height
    ))
    {
        return 0;
    }

    horizontal_magnification =
        timing->active_width / g_video_mode.framebuffer_width;
    vertical_magnification =
        timing->active_height / g_video_mode.framebuffer_height;
    displayed_width =
        g_video_mode.framebuffer_width * horizontal_magnification;
    displayed_height =
        g_video_mode.framebuffer_height * vertical_magnification;

    display_x = timing->default_x +
        (int)((timing->active_width - displayed_width) / 2u);
    display_y = timing->default_y +
        (int)((timing->active_height - displayed_height) / 2u);

    display1 = GS_SET_DISPLAY(
        display_x,
        display_y,
        horizontal_magnification - 1u,
        vertical_magnification - 1u,
        displayed_width - 1u,
        displayed_height - 1u
    );
    display2 = display1;

    if (timing->filtered_read_circuits && displayed_height > 1u)
    {
        display2 = GS_SET_DISPLAY(
            display_x,
            display_y,
            horizontal_magnification - 1u,
            vertical_magnification - 1u,
            displayed_width - 1u,
            displayed_height - 2u
        );
    }

    *GS_REG_DISPLAY1 = display1;
    *GS_REG_DISPLAY2 = display2;
    EE_SYNCP();
    return 1;
}

static void TitanVideoMode_UpdateInfo(
    TitanVideoOutputMode output,
    const TitanVideoTiming *timing
)
{
    g_video_mode.info.id = output;
    g_video_mode.info.name = output == TITAN_VIDEO_OUTPUT_HDTV_720P
        ? "720p Progressive (native 1280x720)"
        : (g_video_mode.base_region == GRAPH_MODE_PAL
            ? "576i Interlaced (640x448 render)"
            : "480i Interlaced (640x448 render)");
    g_video_mode.info.signal_width = output == TITAN_VIDEO_OUTPUT_HDTV_720P
        ? 1280u
        : 640u;
    g_video_mode.info.signal_height = output == TITAN_VIDEO_OUTPUT_HDTV_720P
        ? 720u
        : (g_video_mode.base_region == GRAPH_MODE_PAL ? 576u : 480u);
    g_video_mode.info.framebuffer_width =
        g_video_mode.framebuffer_width;
    g_video_mode.info.framebuffer_height =
        g_video_mode.framebuffer_height;
    g_video_mode.info.framebuffer_count =
        g_video_mode.framebuffer_count;
    g_video_mode.info.framebuffer_psm =
        g_video_mode.framebuffer_psm;
    g_video_mode.info.progressive = timing->interlace ==
        GRAPH_MODE_NONINTERLACED;
}

static TitanResult TitanVideoMode_Apply(
    TitanVideoOutputMode output,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    TitanVideoTiming timing;

    if (
        !g_video_mode.initialized ||
        output < TITAN_VIDEO_OUTPUT_LEGACY ||
        output >= TITAN_VIDEO_OUTPUT_MODE_COUNT ||
        !TitanVideoMode_ValidateFramebuffer(
            output,
            framebuffer_width,
            framebuffer_height,
            framebuffer_count,
            framebuffer_psm
        )
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    timing = TitanVideoMode_GetTiming(output);
    if (!TitanVideoMode_CanDisplay(
        &timing,
        framebuffer_width,
        framebuffer_height
    ))
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_video_mode.framebuffer_width = framebuffer_width;
    g_video_mode.framebuffer_height = framebuffer_height;
    g_video_mode.framebuffer_count = framebuffer_count;
    g_video_mode.framebuffer_psm = framebuffer_psm;

    TitanVideoMode_SwitchProbe("graph-set-mode-enter", (int)output);
    graph_set_mode(
        timing.interlace,
        timing.graph_mode,
        timing.ffmd,
        timing.flicker_filter
    );
    TitanVideoMode_SwitchProbe("graph-set-mode-done", 0);

    TitanVideoMode_ProgramFramebuffer(&timing, framebuffer_address);
    TitanVideoMode_SwitchProbe("framebuffer-programmed", 0);
    if (!TitanVideoMode_ProgramDisplay(&timing))
    {
        TitanVideoMode_SwitchProbe("display-program-fail", -1);
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }
    TitanVideoMode_SwitchProbe("display-programmed", 0);

    graph_set_bgcolor(0, 0, 0);
    graph_enable_output();
    TitanVideoMode_SwitchProbe("output-enabled", 0);

    g_video_mode.output = output;
    TitanVideoMode_UpdateInfo(output, &timing);

    TitanVideoMode_SwitchProbe("vsync-1-enter", 0);
    graph_wait_vsync();
    TitanVideoMode_SwitchProbe("vsync-1-done", 0);
    if (output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        TitanVideoMode_SwitchProbe("vsync-2-enter", 0);
        graph_wait_vsync();
        TitanVideoMode_SwitchProbe("vsync-2-done", 0);
    }

    return TITAN_RESULT_OK;
}

TitanResult TitanVideoMode_Initialize(
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    g_video_mode.initialized = 0;
    g_video_mode.base_region = graph_get_region();
    g_video_mode.output = TITAN_VIDEO_OUTPUT_LEGACY;
    g_video_mode.initialized = 1;

    graph_disable_output();
    return TitanVideoMode_Apply(
        TITAN_VIDEO_OUTPUT_LEGACY,
        framebuffer_address,
        framebuffer_width,
        framebuffer_height,
        framebuffer_count,
        framebuffer_psm
    );
}

TitanResult TitanVideoMode_BeginReconfigure(void)
{
    if (!g_video_mode.initialized)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    if (dma_channel_wait(DMA_CHANNEL_GIF, TITAN_VIDEO_MODE_DMA_TIMEOUT) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }
    if (dma_channel_wait(DMA_CHANNEL_VIF1, TITAN_VIDEO_MODE_DMA_TIMEOUT) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    TitanVideoMode_SwitchProbe("quiesce-vsync-enter", 0);
    graph_wait_vsync();
    TitanVideoMode_SwitchProbe("quiesce-vsync-done", 0);
    graph_disable_output();
    TitanVideoMode_SwitchProbe("output-disabled", 0);
    return TITAN_RESULT_OK;
}

TitanResult TitanVideoMode_Commit(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    return TitanVideoMode_Apply(
        mode,
        framebuffer_address,
        framebuffer_width,
        framebuffer_height,
        framebuffer_count,
        framebuffer_psm
    );
}

TitanResult TitanVideoMode_SetOutput(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    TitanResult result;

    result = TitanVideoMode_BeginReconfigure();
    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    return TitanVideoMode_Commit(
        mode,
        framebuffer_address,
        framebuffer_width,
        framebuffer_height,
        framebuffer_count,
        framebuffer_psm
    );
}

void TitanVideoMode_Present(unsigned int framebuffer_address)
{
    TitanVideoTiming timing;

    if (!g_video_mode.initialized)
    {
        return;
    }

    timing = TitanVideoMode_GetTiming(g_video_mode.output);
    TitanVideoMode_ProgramFramebuffer(&timing, framebuffer_address);
}

TitanVideoOutputMode TitanVideoMode_GetOutput(void)
{
    return g_video_mode.output;
}

int TitanVideoMode_IsProgressive(void)
{
    return g_video_mode.info.progressive;
}

const TitanVideoModeInfo *TitanVideoMode_GetInfo(void)
{
    return g_video_mode.initialized ? &g_video_mode.info : 0;
}

#else

static TitanVideoModeInfo g_software_mode_info = {
    TITAN_VIDEO_OUTPUT_LEGACY,
    "Software",
    640u,
    480u,
    640u,
    448u,
    2u,
    0,
    0
};

TitanResult TitanVideoMode_Initialize(
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    (void)framebuffer_address;
    g_software_mode_info.framebuffer_width = framebuffer_width;
    g_software_mode_info.framebuffer_height = framebuffer_height;
    g_software_mode_info.framebuffer_count = framebuffer_count;
    g_software_mode_info.framebuffer_psm = framebuffer_psm;
    return TITAN_RESULT_OK;
}

TitanResult TitanVideoMode_BeginReconfigure(void)
{
    return TITAN_RESULT_OK;
}

TitanResult TitanVideoMode_Commit(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    (void)framebuffer_address;
    g_software_mode_info.id = mode;
    g_software_mode_info.progressive =
        mode == TITAN_VIDEO_OUTPUT_HDTV_720P;
    g_software_mode_info.signal_width = g_software_mode_info.progressive
        ? 1280u
        : 640u;
    g_software_mode_info.signal_height = g_software_mode_info.progressive
        ? 720u
        : 480u;
    g_software_mode_info.framebuffer_width = framebuffer_width;
    g_software_mode_info.framebuffer_height = framebuffer_height;
    g_software_mode_info.framebuffer_count = framebuffer_count;
    g_software_mode_info.framebuffer_psm = framebuffer_psm;
    return TITAN_RESULT_OK;
}

TitanResult TitanVideoMode_SetOutput(
    TitanVideoOutputMode mode,
    unsigned int framebuffer_address,
    unsigned int framebuffer_width,
    unsigned int framebuffer_height,
    unsigned int framebuffer_count,
    int framebuffer_psm
)
{
    return TitanVideoMode_Commit(
        mode,
        framebuffer_address,
        framebuffer_width,
        framebuffer_height,
        framebuffer_count,
        framebuffer_psm
    );
}

void TitanVideoMode_Present(unsigned int framebuffer_address)
{
    (void)framebuffer_address;
}

TitanVideoOutputMode TitanVideoMode_GetOutput(void)
{
    return g_software_mode_info.id;
}

int TitanVideoMode_IsProgressive(void)
{
    return g_software_mode_info.progressive;
}

const TitanVideoModeInfo *TitanVideoMode_GetInfo(void)
{
    return &g_software_mode_info;
}

#endif
