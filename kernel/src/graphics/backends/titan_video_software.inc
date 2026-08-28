#include "../titan_pikavr.h"
#include "titan_video.h"

#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <graph_vram.h>
#include <gs_psm.h>
#include <kernel.h>
#include <packet2.h>
#include <tamtypes.h>

#include <string.h>

#define TITAN_VIDEO_PACKET_QWORDS 64

#define TITAN_VIDEO_PIXEL_COUNT \
    (TITAN_VIDEO_WIDTH * TITAN_VIDEO_HEIGHT)

#define TITAN_VIDEO_FRAME_BYTES \
    (TITAN_VIDEO_PIXEL_COUNT * sizeof(u16))

typedef struct TitanVideoState
{
    int initialized;
    int background_ready;

    packet2_t *upload_packet;
    unsigned int framebuffer_addresses[2];
    unsigned int displayed_buffer;
    unsigned int drawing_buffer;

    u16 pixels[TITAN_VIDEO_PIXEL_COUNT]
        __attribute__((aligned(64)));

    u16 background[TITAN_VIDEO_PIXEL_COUNT]
        __attribute__((aligned(64)));

    u16 cached_layer[
        TITAN_VIDEO_CACHED_LAYER_WIDTH *
        TITAN_VIDEO_CACHED_LAYER_HEIGHT
    ] __attribute__((aligned(64)));

    int cached_layer_ready;

    u16 avatar_atlas[
        TITAN_VIDEO_AVATAR_ATLAS_WIDTH *
        TITAN_VIDEO_AVATAR_ATLAS_HEIGHT
    ] __attribute__((aligned(64)));

    int avatar_atlas_ready;

    u32 font_atlas[
        TITAN_VIDEO_FONT_ATLAS_WIDTH *
        TITAN_VIDEO_FONT_ATLAS_HEIGHT
    ] __attribute__((aligned(64)));

    int font_atlas_ready;
    int progressive_output;

    float transform_scale;
    float transform_origin_x;
    float transform_origin_y;
    float transform_offset_x;
    float transform_offset_y;
} TitanVideoState;

static TitanVideoState g_video;

static unsigned short g_video_media_psm16[
    TITAN_VIDEO_MEDIA_SURFACE_WIDTH * TITAN_VIDEO_MEDIA_SURFACE_HEIGHT
];

static unsigned short TitanVideo_Rgb565ToPsm16(unsigned short pixel)
{
    unsigned int red = (pixel >> 11u) & 0x1fu;
    unsigned int green = ((pixel >> 5u) & 0x3fu);
    unsigned int blue = pixel & 0x1fu;
    green = (green + 1u) >> 1u;
    if (green > 0x1fu) green = 0x1fu;
    return (unsigned short)(red | (green << 5u) | (blue << 10u) | 0x8000u);
}

static int TitanVideo_ClampInt(
    int value,
    int minimum,
    int maximum
)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

static float TitanVideo_GetSoftwareLogicalScale(void)
{
    float scale_x;
    float scale_y;

    scale_x = (float)TITAN_VIDEO_WIDTH / (float)TITAN_UI_WIDTH;
    scale_y = (float)TITAN_VIDEO_HEIGHT / (float)TITAN_UI_HEIGHT;
    return scale_x < scale_y ? scale_x : scale_y;
}

static float TitanVideo_GetSoftwareLogicalOffsetY(void)
{
    float scale;

    scale = TitanVideo_GetSoftwareLogicalScale();
    return ((float)TITAN_VIDEO_HEIGHT -
        ((float)TITAN_UI_HEIGHT * scale)) * 0.5f;
}

int TitanVideo_MapLogicalX(
    float logical_x
)
{
    float transformed_x;
    float physical_x;

    transformed_x =
        g_video.transform_origin_x +
        ((logical_x - g_video.transform_origin_x) * g_video.transform_scale) +
        g_video.transform_offset_x;
    physical_x = transformed_x * TitanVideo_GetSoftwareLogicalScale();
    return (int)(physical_x + (physical_x >= 0.0f ? 0.5f : -0.5f));
}

int TitanVideo_MapLogicalY(
    float logical_y
)
{
    float transformed_y;
    float physical_y;

    transformed_y =
        g_video.transform_origin_y +
        ((logical_y - g_video.transform_origin_y) * g_video.transform_scale) +
        g_video.transform_offset_y;
    physical_y = TitanVideo_GetSoftwareLogicalOffsetY() +
        (transformed_y * TitanVideo_GetSoftwareLogicalScale());
    return (int)(physical_y + (physical_y >= 0.0f ? 0.5f : -0.5f));
}

float TitanVideo_GetTransformScale(void)
{
    return g_video.transform_scale;
}

float TitanVideo_GetLogicalScaleX(void)
{
    return TitanVideo_GetSoftwareLogicalScale();
}

float TitanVideo_GetLogicalScaleY(void)
{
    return TitanVideo_GetSoftwareLogicalScale();
}

float TitanVideo_GetLogicalOffsetY(void)
{
    return TitanVideo_GetSoftwareLogicalOffsetY();
}

void TitanVideo_SetTransform(
    float scale,
    float origin_x,
    float origin_y,
    float offset_x,
    float offset_y
)
{
    if (scale < 0.05f)
    {
        scale = 0.05f;
    }

    if (scale > 4.0f)
    {
        scale = 4.0f;
    }

    g_video.transform_scale = scale;
    g_video.transform_origin_x = origin_x;
    g_video.transform_origin_y = origin_y;
    g_video.transform_offset_x = offset_x;
    g_video.transform_offset_y = offset_y;
}

void TitanVideo_ResetTransform(void)
{
    g_video.transform_scale = 1.0f;
    g_video.transform_origin_x = 0.0f;
    g_video.transform_origin_y = 0.0f;
    g_video.transform_offset_x = 0.0f;
    g_video.transform_offset_y = 0.0f;
}

static int TitanVideo_MapLogicalRadiusX(
    float logical_radius
)
{
    int radius;

    radius = (int)(
        (
            logical_radius *
            g_video.transform_scale *
            (float)TITAN_VIDEO_WIDTH /
            (float)TITAN_UI_WIDTH
        ) +
        0.5f
    );

    if (radius < 1)
    {
        radius = 1;
    }

    return radius;
}

static u16 TitanVideo_PackColor(
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    return (u16)(
        ((u16)(red >> 3)) |
        ((u16)(green >> 3) << 5) |
        ((u16)(blue >> 3) << 10) |
        0x8000
    );
}

static u16 TitanVideo_BlendColor(
    u16 destination,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    unsigned int source_red;
    unsigned int source_green;
    unsigned int source_blue;

    unsigned int destination_red;
    unsigned int destination_green;
    unsigned int destination_blue;

    unsigned int alpha4;
    unsigned int inverse_alpha4;

    unsigned int output_red;
    unsigned int output_green;
    unsigned int output_blue;

    if (alpha == 0)
    {
        return destination;
    }

    if (alpha >= 248)
    {
        return TitanVideo_PackColor(
            red,
            green,
            blue
        );
    }

    source_red = red >> 3;
    source_green = green >> 3;
    source_blue = blue >> 3;

    destination_red = destination & 0x1Fu;
    destination_green = (destination >> 5) & 0x1Fu;
    destination_blue = (destination >> 10) & 0x1Fu;

    alpha4 = (alpha + 8u) >> 4;

    if (alpha4 > 15u)
    {
        alpha4 = 15u;
    }

    inverse_alpha4 = 15u - alpha4;

    output_red =
        (
            (source_red * alpha4) +
            (destination_red * inverse_alpha4) +
            7u
        ) / 15u;

    output_green =
        (
            (source_green * alpha4) +
            (destination_green * inverse_alpha4) +
            7u
        ) / 15u;

    output_blue =
        (
            (source_blue * alpha4) +
            (destination_blue * inverse_alpha4) +
            7u
        ) / 15u;

    return (u16)(
        output_red |
        (output_green << 5) |
        (output_blue << 10) |
        0x8000
    );
}

static void TitanVideo_FillPhysicalSpan(
    int y,
    int x0,
    int x1,
    u16 color
)
{
    u16 *row;
    int x;

    if (
        y < 0 ||
        y >= TITAN_VIDEO_HEIGHT
    )
    {
        return;
    }

    x0 = TitanVideo_ClampInt(
        x0,
        0,
        TITAN_VIDEO_WIDTH
    );

    x1 = TitanVideo_ClampInt(
        x1,
        0,
        TITAN_VIDEO_WIDTH
    );

    if (x0 >= x1)
    {
        return;
    }

    row = &g_video.pixels[
        y * TITAN_VIDEO_WIDTH
    ];

    for (x = x0; x < x1; x++)
    {
        row[x] = color;
    }
}

static void TitanVideo_BlendPhysicalSpan(
    int y,
    int x0,
    int x1,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    u16 *row;
    int x;

    if (
        y < 0 ||
        y >= TITAN_VIDEO_HEIGHT ||
        alpha == 0
    )
    {
        return;
    }

    x0 = TitanVideo_ClampInt(
        x0,
        0,
        TITAN_VIDEO_WIDTH
    );

    x1 = TitanVideo_ClampInt(
        x1,
        0,
        TITAN_VIDEO_WIDTH
    );

    if (x0 >= x1)
    {
        return;
    }

    if (alpha >= 248)
    {
        TitanVideo_FillPhysicalSpan(
            y,
            x0,
            x1,
            TitanVideo_PackColor(
                red,
                green,
                blue
            )
        );

        return;
    }

    row = &g_video.pixels[
        y * TITAN_VIDEO_WIDTH
    ];

    for (x = x0; x < x1; x++)
    {
        row[x] = TitanVideo_BlendColor(
            row[x],
            red,
            green,
            blue,
            alpha
        );
    }
}

TitanResult TitanVideo_Initialize(void)
{
    int framebuffer_address[2];

    if (g_video.initialized)
    {
        return TITAN_RESULT_OK;
    }

    dma_reset();

    if (
        dma_channel_initialize(
            DMA_CHANNEL_GIF,
            0,
            0
        ) < 0
    )
    {
        return TITAN_ERROR_DMA_INITIALIZATION;
    }

    graph_vram_clear();

    framebuffer_address[0] = graph_vram_allocate(
        TITAN_VIDEO_WIDTH,
        TITAN_VIDEO_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_PAGE
    );

    framebuffer_address[1] = graph_vram_allocate(
        TITAN_VIDEO_WIDTH,
        TITAN_VIDEO_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_PAGE
    );

    if (
        framebuffer_address[0] < 0 ||
        framebuffer_address[1] < 0
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_video.framebuffer_addresses[0] =
        (unsigned int)framebuffer_address[0];
    g_video.framebuffer_addresses[1] =
        (unsigned int)framebuffer_address[1];
    g_video.displayed_buffer = 0u;
    g_video.drawing_buffer = 1u;

    graph_initialize(
        g_video.framebuffer_addresses[g_video.displayed_buffer],
        TITAN_VIDEO_WIDTH,
        TITAN_VIDEO_HEIGHT,
        GS_PSM_16,
        0,
        0
    );

    g_video.upload_packet = packet2_create(
        TITAN_VIDEO_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        0
    );

    if (g_video.upload_packet == 0)
    {
        return TITAN_ERROR_PACKET_ALLOCATION;
    }

    g_video.initialized = 1;
    g_video.background_ready = 0;
    g_video.cached_layer_ready = 0;
    g_video.avatar_atlas_ready = 0;
    g_video.font_atlas_ready = 0;
    g_video.progressive_output = 0;
    TitanVideo_ResetTransform();

    TitanVideo_BuildBackgroundGradient(
        82,
        82,
        88,
        82,
        82,
        88
    );

    TitanVideo_RestoreBackground();

    graph_wait_vsync();
    graph_wait_vsync();

    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_SetProgressiveOutput(int enabled)
{
    int mode;

    if (!g_video.initialized)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    graph_disable_output();

    if (enabled)
    {
        mode = GRAPH_MODE_HDTV_720P;
        graph_set_mode(
            GRAPH_MODE_NONINTERLACED,
            mode,
            GRAPH_MODE_FRAME,
            GRAPH_DISABLE
        );
    }
    else
    {
        mode = graph_get_region();
        graph_set_mode(
            GRAPH_MODE_INTERLACED,
            mode,
            GRAPH_MODE_FIELD,
            GRAPH_ENABLE
        );
    }

    graph_set_screen(
        0,
        enabled ? 0 : ((graph_get_region() == GRAPH_MODE_PAL ? 512 : 448) - TITAN_VIDEO_HEIGHT) / 2,
        TITAN_VIDEO_WIDTH,
        TITAN_VIDEO_HEIGHT
    );
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(
        (int)g_video.framebuffer_addresses[g_video.displayed_buffer],
        TITAN_VIDEO_WIDTH,
        GS_PSM_16,
        0,
        0
    );
    graph_enable_output();
    graph_wait_vsync();

    g_video.progressive_output = enabled != 0;
    return TITAN_RESULT_OK;
}

int TitanVideo_IsProgressiveOutput(void)
{
    return g_video.progressive_output;
}

unsigned int TitanVideo_GetVramBytesReserved(void)
{
    unsigned int words_per_frame;

    words_per_frame =
        (unsigned int)TITAN_VIDEO_WIDTH *
        (((unsigned int)TITAN_VIDEO_HEIGHT + 1u) >> 1u);
    words_per_frame = (words_per_frame + 2047u) & ~2047u;
    return words_per_frame * 2u * 4u;
}

unsigned int TitanVideo_GetVramBytesFree(void)
{
    unsigned int reserved;
    const unsigned int total = 1048576u * 4u;

    reserved = TitanVideo_GetVramBytesReserved();
    return reserved < total ? total - reserved : 0u;
}

unsigned int TitanVideo_GetFramebufferWidth(void)
{
    return TITAN_VIDEO_WIDTH;
}

unsigned int TitanVideo_GetFramebufferHeight(void)
{
    return TITAN_VIDEO_HEIGHT;
}

unsigned int TitanVideo_GetFramebufferCount(void)
{
    return 2u;
}

int TitanVideo_GetFramebufferPsm(void)
{
    return GS_PSM_16;
}

unsigned int TitanVideo_GetLastFrameGsLoadPercent(void)
{
    return 0u;
}

TitanResult TitanVideo_BeginFrame(void)
{
    if (
        !g_video.initialized ||
        g_video.upload_packet == 0
    )
    {
        return TITAN_ERROR_FRAME_BEGIN;
    }

    return TITAN_RESULT_OK;
}

void TitanVideo_DrawPixelPhysical(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    if (
        x < 0 ||
        y < 0 ||
        x >= TITAN_VIDEO_WIDTH ||
        y >= TITAN_VIDEO_HEIGHT
    )
    {
        return;
    }

    g_video.pixels[
        (y * TITAN_VIDEO_WIDTH) + x
    ] = TitanVideo_PackColor(
        red,
        green,
        blue
    );
}

void TitanVideo_DrawPixelPhysicalAlpha(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    u16 *pixel;

    if (
        x < 0 ||
        y < 0 ||
        x >= TITAN_VIDEO_WIDTH ||
        y >= TITAN_VIDEO_HEIGHT ||
        alpha == 0
    )
    {
        return;
    }

    pixel = &g_video.pixels[
        (y * TITAN_VIDEO_WIDTH) + x
    ];

    *pixel = TitanVideo_BlendColor(
        *pixel,
        red,
        green,
        blue,
        alpha
    );
}

void TitanVideo_Clear(
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    u16 color;
    u64 packed_color;
    u64 *destination;

    unsigned int index;
    unsigned int packed_count;

    color = TitanVideo_PackColor(
        red,
        green,
        blue
    );

    packed_color =
        ((u64)color) |
        ((u64)color << 16) |
        ((u64)color << 32) |
        ((u64)color << 48);

    destination = (u64 *)g_video.pixels;
    packed_count = TITAN_VIDEO_PIXEL_COUNT / 4;

    for (index = 0; index < packed_count; index++)
    {
        destination[index] = packed_color;
    }
}

void TitanVideo_BuildBackgroundGradient(
    unsigned char top_red,
    unsigned char top_green,
    unsigned char top_blue,
    unsigned char bottom_red,
    unsigned char bottom_green,
    unsigned char bottom_blue
)
{
    int y;
    int x;

    unsigned int factor;
    unsigned int inverse_factor;

    unsigned char red;
    unsigned char green;
    unsigned char blue;

    u16 color;
    u16 *row;

    for (y = 0; y < TITAN_VIDEO_HEIGHT; y++)
    {
        factor =
            ((unsigned int)y * 255u) /
            (TITAN_VIDEO_HEIGHT - 1);

        inverse_factor = 255u - factor;

        red = (unsigned char)(
            (
                ((unsigned int)top_red * inverse_factor) +
                ((unsigned int)bottom_red * factor) +
                127u
            ) / 255u
        );

        green = (unsigned char)(
            (
                ((unsigned int)top_green * inverse_factor) +
                ((unsigned int)bottom_green * factor) +
                127u
            ) / 255u
        );

        blue = (unsigned char)(
            (
                ((unsigned int)top_blue * inverse_factor) +
                ((unsigned int)bottom_blue * factor) +
                127u
            ) / 255u
        );

        color = TitanVideo_PackColor(
            red,
            green,
            blue
        );

        row = &g_video.background[
            y * TITAN_VIDEO_WIDTH
        ];

        for (x = 0; x < TITAN_VIDEO_WIDTH; x++)
        {
            row[x] = color;
        }
    }

    g_video.background_ready = 1;
}

void TitanVideo_RestoreBackground(void)
{
    if (!g_video.background_ready)
    {
        TitanVideo_Clear(
            82,
            82,
            88
        );

        return;
    }

    memcpy(
        g_video.pixels,
        g_video.background,
        TITAN_VIDEO_FRAME_BYTES
    );
}

void TitanVideo_DrawPixelAlpha(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    TitanVideo_DrawPixelPhysicalAlpha(
        TitanVideo_MapLogicalX(
            (float)x
        ),
        TitanVideo_MapLogicalY((float)y),
        red,
        green,
        blue,
        alpha
    );
}

void TitanVideo_DrawRectAlpha(
    float x,
    float y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int current_y;

    if (
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0
    )
    {
        return;
    }

    x0 = TitanVideo_MapLogicalX(x);
    x1 = TitanVideo_MapLogicalX(x + width);

    y0 = TitanVideo_ClampInt(
        TitanVideo_MapLogicalY(y),
        0,
        TITAN_VIDEO_HEIGHT
    );

    y1 = TitanVideo_ClampInt(
        TitanVideo_MapLogicalY(y + height),
        0,
        TITAN_VIDEO_HEIGHT
    );

    if (x1 < x0)
    {
        int temporary;

        temporary = x0;
        x0 = x1;
        x1 = temporary;
    }

    if (x0 == x1)
    {
        x1 = x0 + 1;
    }

    if (y0 >= y1)
    {
        return;
    }

    if (alpha >= 248)
    {
        u16 color;

        color = TitanVideo_PackColor(
            red,
            green,
            blue
        );

        for (
            current_y = y0;
            current_y < y1;
            current_y++
        )
        {
            TitanVideo_FillPhysicalSpan(
                current_y,
                x0,
                x1,
                color
            );
        }

        return;
    }

    for (
        current_y = y0;
        current_y < y1;
        current_y++
    )
    {
        TitanVideo_BlendPhysicalSpan(
            current_y,
            x0,
            x1,
            red,
            green,
            blue,
            alpha
        );
    }
}

void TitanVideo_DrawRect(
    float x,
    float y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanVideo_DrawRectAlpha(
        x,
        y,
        width,
        height,
        red,
        green,
        blue,
        255
    );
}

void TitanVideo_DrawCircleAlpha(
    float center_x,
    float center_y,
    float radius,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int physical_center_x;
    int physical_center_y;
    int radius_x;
    int radius_y;

    int y;
    int x;

    int x0;
    int y0;
    int x1;
    int y1;

    int delta_x;
    int delta_y;

    int radius_x_squared;
    int radius_y_squared;
    int test;

    if (radius <= 0.0f || alpha == 0)
    {
        return;
    }

    physical_center_x =
        TitanVideo_MapLogicalX(
            center_x
        );

    physical_center_y =
        TitanVideo_MapLogicalY(
            center_y
        );

    radius_x =
        TitanVideo_MapLogicalRadiusX(
            radius
        );

    radius_y = (int)(
        (radius * g_video.transform_scale) +
        0.5f
    );

    if (radius_y < 1)
    {
        radius_y = 1;
    }

    x0 = physical_center_x - radius_x;
    x1 = physical_center_x + radius_x + 1;

    y0 = physical_center_y - radius_y;
    y1 = physical_center_y + radius_y + 1;

    radius_x_squared = radius_x * radius_x;
    radius_y_squared = radius_y * radius_y;

    for (y = y0; y < y1; y++)
    {
        delta_y = y - physical_center_y;

        for (x = x0; x < x1; x++)
        {
            delta_x = x - physical_center_x;

            test =
                (delta_x * delta_x * radius_y_squared) +
                (delta_y * delta_y * radius_x_squared);

            if (
                test <=
                (radius_x_squared * radius_y_squared)
            )
            {
                TitanVideo_DrawPixelPhysicalAlpha(
                    x,
                    y,
                    red,
                    green,
                    blue,
                    alpha
                );
            }
        }
    }
}

void TitanVideo_DrawRoundedRectAlpha(
    float x,
    float y,
    float width,
    float height,
    float radius,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int x0;
    int y0;
    int x1;
    int y1;

    int radius_x;
    int radius_y;

    int current_y;
    int local_y;
    int delta_y;
    int inset_x;

    int test_x;
    int radius_x_squared;
    int radius_y_squared;

    if (
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0
    )
    {
        return;
    }

    x0 = TitanVideo_MapLogicalX(x);
    x1 = TitanVideo_MapLogicalX(x + width);
    y0 = TitanVideo_MapLogicalY(y);
    y1 = TitanVideo_MapLogicalY(y + height);

    if (x1 < x0)
    {
        int temporary;

        temporary = x0;
        x0 = x1;
        x1 = temporary;
    }

    x0 = TitanVideo_ClampInt(
        x0,
        0,
        TITAN_VIDEO_WIDTH
    );

    x1 = TitanVideo_ClampInt(
        x1,
        0,
        TITAN_VIDEO_WIDTH
    );

    y0 = TitanVideo_ClampInt(
        y0,
        0,
        TITAN_VIDEO_HEIGHT
    );

    y1 = TitanVideo_ClampInt(
        y1,
        0,
        TITAN_VIDEO_HEIGHT
    );

    if (x0 >= x1 || y0 >= y1)
    {
        return;
    }

    radius_x = TitanVideo_MapLogicalRadiusX(
        radius
    );

    radius_y = (int)(
        (radius * g_video.transform_scale) +
        0.5f
    );

    if (radius_x > (x1 - x0) / 2)
    {
        radius_x = (x1 - x0) / 2;
    }

    if (radius_y > (y1 - y0) / 2)
    {
        radius_y = (y1 - y0) / 2;
    }

    if (radius_x <= 0 || radius_y <= 0)
    {
        TitanVideo_DrawRectAlpha(
            x,
            y,
            width,
            height,
            red,
            green,
            blue,
            alpha
        );

        return;
    }

    radius_x_squared = radius_x * radius_x;
    radius_y_squared = radius_y * radius_y;

    for (
        current_y = y0;
        current_y < y1;
        current_y++
    )
    {
        local_y = current_y - y0;
        inset_x = 0;

        if (local_y < radius_y)
        {
            delta_y = radius_y - local_y - 1;
        }
        else if (local_y >= (y1 - y0) - radius_y)
        {
            delta_y =
                local_y -
                ((y1 - y0) - radius_y);
        }
        else
        {
            delta_y = 0;
        }

        if (delta_y > 0)
        {
            for (
                test_x = 0;
                test_x < radius_x;
                test_x++
            )
            {
                if (
                    (test_x * test_x * radius_y_squared) +
                    (delta_y * delta_y * radius_x_squared) <=
                    (radius_x_squared * radius_y_squared)
                )
                {
                    inset_x = radius_x - test_x;
                    break;
                }
            }
        }

        TitanVideo_BlendPhysicalSpan(
            current_y,
            x0 + inset_x,
            x1 - inset_x,
            red,
            green,
            blue,
            alpha
        );
    }
}

static void TitanVideo_DrawPhysicalLine(
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int delta_x;
    int delta_y;
    int step_x;
    int step_y;
    int error;
    int error2;

    int half;
    int draw_y;
    u16 color;

    if (thickness < 1)
    {
        thickness = 1;
    }

    color = TitanVideo_PackColor(
        red,
        green,
        blue
    );

    delta_x = x1 > x0
        ? x1 - x0
        : x0 - x1;

    delta_y = y1 > y0
        ? y1 - y0
        : y0 - y1;

    step_x = x0 < x1 ? 1 : -1;
    step_y = y0 < y1 ? 1 : -1;

    error = delta_x - delta_y;
    half = thickness / 2;

    for (;;)
    {
        for (
            draw_y = y0 - half;
            draw_y < y0 - half + thickness;
            draw_y++
        )
        {
            if (alpha >= 248)
            {
                TitanVideo_FillPhysicalSpan(
                    draw_y,
                    x0 - half,
                    x0 - half + thickness,
                    color
                );
            }
            else
            {
                TitanVideo_BlendPhysicalSpan(
                    draw_y,
                    x0 - half,
                    x0 - half + thickness,
                    red,
                    green,
                    blue,
                    alpha
                );
            }
        }

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        error2 = error * 2;

        if (error2 > -delta_y)
        {
            error -= delta_y;
            x0 += step_x;
        }

        if (error2 < delta_x)
        {
            error += delta_x;
            y0 += step_y;
        }
    }
}

void TitanVideo_DrawLineAlpha(
    float x0,
    float y0,
    float x1,
    float y1,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int physical_thickness;

    if (thickness <= 0.0f || alpha == 0)
    {
        return;
    }

    physical_thickness =
        TitanVideo_MapLogicalRadiusX(
            thickness
        );

    TitanVideo_DrawPhysicalLine(
        TitanVideo_MapLogicalX(x0),
        TitanVideo_MapLogicalY(y0),
        TitanVideo_MapLogicalX(x1),
        TitanVideo_MapLogicalY(y1),
        physical_thickness,
        red,
        green,
        blue,
        alpha
    );
}

void TitanVideo_DrawLine(
    float x0,
    float y0,
    float x1,
    float y1,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanVideo_DrawLineAlpha(
        x0,
        y0,
        x1,
        y1,
        thickness,
        red,
        green,
        blue,
        255
    );
}

static int TitanVideo_CeilFloat(
    float value
)
{
    int integer_value;

    integer_value = (int)value;

    if ((float)integer_value < value)
    {
        integer_value++;
    }

    return integer_value;
}

static void TitanVideo_AddTriangleIntersection(
    float scan_y,
    int x0,
    int y0,
    int x1,
    int y1,
    float *intersections,
    int *intersection_count
)
{
    float amount;

    if (
        intersection_count == 0 ||
        intersections == 0 ||
        *intersection_count >= 3 ||
        y0 == y1
    )
    {
        return;
    }

    if (
        !(
            (scan_y >= (float)y0 && scan_y < (float)y1) ||
            (scan_y >= (float)y1 && scan_y < (float)y0)
        )
    )
    {
        return;
    }

    amount =
        (scan_y - (float)y0) /
        (float)(y1 - y0);

    intersections[*intersection_count] =
        (float)x0 + ((float)(x1 - x0) * amount);

    (*intersection_count)++;
}

static void TitanVideo_DrawPhysicalTriangle(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    u16 color
)
{
    int minimum_y;
    int maximum_y;
    int y;
    int intersection_count;
    int span_start;
    int span_end;
    float scan_y;
    float intersections[3];
    float minimum_x;
    float maximum_x;
    float temporary;

    minimum_y = y0;

    if (y1 < minimum_y)
    {
        minimum_y = y1;
    }

    if (y2 < minimum_y)
    {
        minimum_y = y2;
    }

    maximum_y = y0;

    if (y1 > maximum_y)
    {
        maximum_y = y1;
    }

    if (y2 > maximum_y)
    {
        maximum_y = y2;
    }

    minimum_y = TitanVideo_ClampInt(
        minimum_y,
        0,
        TITAN_VIDEO_HEIGHT
    );

    maximum_y = TitanVideo_ClampInt(
        maximum_y + 1,
        0,
        TITAN_VIDEO_HEIGHT
    );

    for (y = minimum_y; y < maximum_y; y++)
    {
        intersection_count = 0;
        scan_y = (float)y + 0.5f;

        TitanVideo_AddTriangleIntersection(
            scan_y,
            x0,
            y0,
            x1,
            y1,
            intersections,
            &intersection_count
        );

        TitanVideo_AddTriangleIntersection(
            scan_y,
            x1,
            y1,
            x2,
            y2,
            intersections,
            &intersection_count
        );

        TitanVideo_AddTriangleIntersection(
            scan_y,
            x2,
            y2,
            x0,
            y0,
            intersections,
            &intersection_count
        );

        if (intersection_count < 2)
        {
            continue;
        }

        minimum_x = intersections[0];
        maximum_x = intersections[1];

        if (minimum_x > maximum_x)
        {
            temporary = minimum_x;
            minimum_x = maximum_x;
            maximum_x = temporary;
        }

        if (intersection_count == 3)
        {
            if (intersections[2] < minimum_x)
            {
                minimum_x = intersections[2];
            }
            else if (intersections[2] > maximum_x)
            {
                maximum_x = intersections[2];
            }
        }

        span_start = TitanVideo_CeilFloat(minimum_x);
        span_end = TitanVideo_CeilFloat(maximum_x);

        if (span_end <= span_start)
        {
            span_end = span_start + 1;
        }

        TitanVideo_FillPhysicalSpan(
            y,
            span_start,
            span_end,
            color
        );
    }
}

static void TitanVideo_DrawPhysicalTriangleAlpha(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int minimum_y;
    int maximum_y;
    int y;
    int intersection_count;
    int span_start;
    int span_end;
    float scan_y;
    float intersections[3];
    float minimum_x;
    float maximum_x;
    float temporary;

    if (alpha == 0u)
    {
        return;
    }

    minimum_y = y0;
    if (y1 < minimum_y) minimum_y = y1;
    if (y2 < minimum_y) minimum_y = y2;
    maximum_y = y0;
    if (y1 > maximum_y) maximum_y = y1;
    if (y2 > maximum_y) maximum_y = y2;

    minimum_y = TitanVideo_ClampInt(minimum_y, 0, TITAN_VIDEO_HEIGHT);
    maximum_y = TitanVideo_ClampInt(maximum_y + 1, 0, TITAN_VIDEO_HEIGHT);

    for (y = minimum_y; y < maximum_y; y++)
    {
        intersection_count = 0;
        scan_y = (float)y + 0.5f;

        TitanVideo_AddTriangleIntersection(scan_y, x0, y0, x1, y1, intersections, &intersection_count);
        TitanVideo_AddTriangleIntersection(scan_y, x1, y1, x2, y2, intersections, &intersection_count);
        TitanVideo_AddTriangleIntersection(scan_y, x2, y2, x0, y0, intersections, &intersection_count);

        if (intersection_count < 2)
        {
            continue;
        }

        minimum_x = intersections[0];
        maximum_x = intersections[1];
        if (minimum_x > maximum_x)
        {
            temporary = minimum_x;
            minimum_x = maximum_x;
            maximum_x = temporary;
        }

        if (intersection_count == 3)
        {
            if (intersections[2] < minimum_x)
            {
                minimum_x = intersections[2];
            }
            else if (intersections[2] > maximum_x)
            {
                maximum_x = intersections[2];
            }
        }

        span_start = TitanVideo_CeilFloat(minimum_x);
        span_end = TitanVideo_CeilFloat(maximum_x);
        if (span_end <= span_start)
        {
            span_end = span_start + 1;
        }

        TitanVideo_BlendPhysicalSpan(
            y,
            span_start,
            span_end,
            red,
            green,
            blue,
            alpha
        );
    }
}

void TitanVideo_DrawSolidQuad(
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanVideo_DrawSolidQuadAlpha(
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        x3,
        y3,
        red,
        green,
        blue,
        255u
    );
}

void TitanVideo_DrawSolidQuadAlpha(
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int physical_x0;
    int physical_x1;
    int physical_x2;
    int physical_x3;
    int physical_y0;
    int physical_y1;
    int physical_y2;
    int physical_y3;

    physical_x0 = TitanVideo_MapLogicalX(x0);
    physical_x1 = TitanVideo_MapLogicalX(x1);
    physical_x2 = TitanVideo_MapLogicalX(x2);
    physical_x3 = TitanVideo_MapLogicalX(x3);

    physical_y0 = TitanVideo_MapLogicalY(y0);
    physical_y1 = TitanVideo_MapLogicalY(y1);
    physical_y2 = TitanVideo_MapLogicalY(y2);
    physical_y3 = TitanVideo_MapLogicalY(y3);

    TitanVideo_DrawPhysicalTriangleAlpha(
        physical_x0,
        physical_y0,
        physical_x1,
        physical_y1,
        physical_x2,
        physical_y2,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawPhysicalTriangleAlpha(
        physical_x0,
        physical_y0,
        physical_x2,
        physical_y2,
        physical_x3,
        physical_y3,
        red,
        green,
        blue,
        alpha
    );
}

TitanResult TitanVideo_UploadCachedLayer16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    if (
        !g_video.initialized ||
        pixels == 0 ||
        width != TITAN_VIDEO_CACHED_LAYER_WIDTH ||
        height != TITAN_VIDEO_CACHED_LAYER_HEIGHT ||
        stride != TITAN_VIDEO_CACHED_LAYER_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    memcpy(
        g_video.cached_layer,
        pixels,
        sizeof(g_video.cached_layer)
    );

    g_video.cached_layer_ready = 1;
    return TITAN_RESULT_OK;
}

void TitanVideo_DrawCachedLayer16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
)
{
    int physical_x0;
    int physical_y0;
    int physical_x1;
    int physical_y1;
    int physical_width;
    int physical_height;
    int destination_x;
    int destination_y;
    unsigned int source_x;
    unsigned int source_y;
    u16 source;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    u16 *destination;

    if (
        !g_video.cached_layer_ready ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_width > TITAN_VIDEO_CACHED_LAYER_WIDTH ||
        source_height > TITAN_VIDEO_CACHED_LAYER_HEIGHT ||
        alpha == 0u
    )
    {
        return;
    }

    physical_x0 = TitanVideo_MapLogicalX(x);
    physical_y0 = TitanVideo_MapLogicalY(y);
    physical_x1 = TitanVideo_MapLogicalX(x + width);
    physical_y1 = TitanVideo_MapLogicalY(y + height);
    physical_width = physical_x1 - physical_x0;
    physical_height = physical_y1 - physical_y0;

    if (physical_width <= 0 || physical_height <= 0)
    {
        return;
    }

    for (destination_y = 0; destination_y < physical_height; destination_y++)
    {
        int screen_y;

        screen_y = physical_y0 + destination_y;

        if (screen_y < 0 || screen_y >= TITAN_VIDEO_HEIGHT)
        {
            continue;
        }

        source_y =
            ((unsigned int)destination_y * source_height) /
            (unsigned int)physical_height;

        if (source_y >= source_height)
        {
            source_y = source_height - 1u;
        }

        for (destination_x = 0; destination_x < physical_width; destination_x++)
        {
            int screen_x;

            screen_x = physical_x0 + destination_x;

            if (screen_x < 0 || screen_x >= TITAN_VIDEO_WIDTH)
            {
                continue;
            }

            source_x =
                ((unsigned int)destination_x * source_width) /
                (unsigned int)physical_width;

            if (source_x >= source_width)
            {
                source_x = source_width - 1u;
            }

            source = g_video.cached_layer[
                (source_y * TITAN_VIDEO_CACHED_LAYER_WIDTH) +
                source_x
            ];

            red = (unsigned char)((source & 0x1Fu) << 3u);
            green = (unsigned char)(((source >> 5u) & 0x1Fu) << 3u);
            blue = (unsigned char)(((source >> 10u) & 0x1Fu) << 3u);
            destination = &g_video.pixels[
                (screen_y * TITAN_VIDEO_WIDTH) + screen_x
            ];

            if (alpha >= 248u)
            {
                *destination = source;
            }
            else
            {
                *destination = TitanVideo_BlendColor(
                    *destination,
                    red,
                    green,
                    blue,
                    alpha
                );
            }
        }
    }
}


TitanResult TitanVideo_DrawMediaImage16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    float x,
    float y,
    float draw_width,
    float draw_height,
    unsigned char alpha
)
{
    TitanResult result;
    unsigned int row;
    unsigned int column;

    if (pixels == 0 || width != TITAN_VIDEO_MEDIA_SURFACE_WIDTH ||
        height != TITAN_VIDEO_MEDIA_SURFACE_HEIGHT || stride < width)
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    for (row = 0u; row < height; row++)
    {
        const unsigned short *source_row = &pixels[row * stride];
        unsigned short *destination_row = &g_video_media_psm16[row * width];
        for (column = 0u; column < width; column++)
        {
            destination_row[column] = TitanVideo_Rgb565ToPsm16(source_row[column]);
        }
    }

    result = TitanVideo_UploadCachedLayer16(
        g_video_media_psm16, width, height, width
    );
    if (result != TITAN_RESULT_OK)
    {
        return result;
    }
    TitanVideo_DrawCachedLayer16(
        x, y, draw_width, draw_height, width, height, alpha
    );
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_UploadTransientAtlasPsm16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    unsigned int generation
)
{
    (void)pixels; (void)width; (void)height; (void)stride; (void)generation;
    /* Software/debug renderer intentionally falls back to the existing game
     * placeholder. Asset textures are a GS fast path and must never make host
     * rendering depend on a second resident atlas. */
    return TITAN_ERROR_VIDEO_INITIALIZATION;
}

void TitanVideo_DrawTransientAtlasRegionPsm16(
    float x, float y, float width, float height,
    unsigned int source_x, unsigned int source_y,
    unsigned int source_width, unsigned int source_height,
    unsigned char alpha
)
{
    (void)x; (void)y; (void)width; (void)height;
    (void)source_x; (void)source_y; (void)source_width; (void)source_height;
    (void)alpha;
}

TitanResult TitanVideo_DrawAdaptiveImagePsm16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    float x, float y, float draw_width, float draw_height,
    unsigned char alpha
)
{
    (void)pixels; (void)width; (void)height; (void)stride;
    (void)x; (void)y; (void)draw_width; (void)draw_height; (void)alpha;
    return TITAN_ERROR_VIDEO_INITIALIZATION;
}

TitanResult TitanVideo_UploadAvatarAtlas16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    if (
        !g_video.initialized ||
        pixels == 0 ||
        width != TITAN_VIDEO_AVATAR_ATLAS_WIDTH ||
        height != TITAN_VIDEO_AVATAR_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_AVATAR_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    memcpy(
        g_video.avatar_atlas,
        pixels,
        sizeof(g_video.avatar_atlas)
    );

    g_video.avatar_atlas_ready = 1;
    return TITAN_RESULT_OK;
}

void TitanVideo_DrawAvatarAtlasRegion16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x_offset,
    unsigned int source_y_offset,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
)
{
    int physical_x0;
    int physical_y0;
    int physical_x1;
    int physical_y1;
    int physical_width;
    int physical_height;
    int destination_x;
    int destination_y;
    unsigned int source_x;
    unsigned int source_y;
    u16 source;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    u16 *destination;

    if (
        !g_video.avatar_atlas_ready ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_x_offset + source_width > TITAN_VIDEO_AVATAR_ATLAS_WIDTH ||
        source_y_offset + source_height > TITAN_VIDEO_AVATAR_ATLAS_HEIGHT ||
        alpha == 0u
    )
    {
        return;
    }

    physical_x0 = TitanVideo_MapLogicalX(x);
    physical_y0 = TitanVideo_MapLogicalY(y);
    physical_x1 = TitanVideo_MapLogicalX(x + width);
    physical_y1 = TitanVideo_MapLogicalY(y + height);
    physical_width = physical_x1 - physical_x0;
    physical_height = physical_y1 - physical_y0;

    if (physical_width <= 0 || physical_height <= 0)
    {
        return;
    }

    for (destination_y = 0; destination_y < physical_height; destination_y++)
    {
        int screen_y;

        screen_y = physical_y0 + destination_y;

        if (screen_y < 0 || screen_y >= TITAN_VIDEO_HEIGHT)
        {
            continue;
        }

        source_y = source_y_offset +
            (((unsigned int)destination_y * source_height) /
             (unsigned int)physical_height);

        if (source_y >= source_y_offset + source_height)
        {
            source_y = source_y_offset + source_height - 1u;
        }

        for (destination_x = 0; destination_x < physical_width; destination_x++)
        {
            int screen_x;

            screen_x = physical_x0 + destination_x;

            if (screen_x < 0 || screen_x >= TITAN_VIDEO_WIDTH)
            {
                continue;
            }

            source_x = source_x_offset +
                (((unsigned int)destination_x * source_width) /
                 (unsigned int)physical_width);

            if (source_x >= source_x_offset + source_width)
            {
                source_x = source_x_offset + source_width - 1u;
            }

            source = g_video.avatar_atlas[
                (source_y * TITAN_VIDEO_AVATAR_ATLAS_WIDTH) +
                source_x
            ];

            red = (unsigned char)((source & 0x1Fu) << 3u);
            green = (unsigned char)(((source >> 5u) & 0x1Fu) << 3u);
            blue = (unsigned char)(((source >> 10u) & 0x1Fu) << 3u);
            destination = &g_video.pixels[
                (screen_y * TITAN_VIDEO_WIDTH) + screen_x
            ];

            if (alpha >= 248u)
            {
                *destination = source;
            }
            else
            {
                *destination = TitanVideo_BlendColor(
                    *destination,
                    red,
                    green,
                    blue,
                    alpha
                );
            }
        }
    }
}

TitanResult TitanVideo_UploadFontAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    if (
        pixels == 0 ||
        width != TITAN_VIDEO_FONT_ATLAS_WIDTH ||
        height != TITAN_VIDEO_FONT_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_FONT_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    memcpy(
        g_video.font_atlas,
        pixels,
        sizeof(g_video.font_atlas)
    );

    g_video.font_atlas_ready = 1;
    return TITAN_RESULT_OK;
}

void TitanVideo_DrawFontAtlasRegion32(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x_offset,
    unsigned int source_y_offset,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int physical_x0;
    int physical_y0;
    int physical_x1;
    int physical_y1;
    int physical_width;
    int physical_height;
    int destination_x;
    int destination_y;
    unsigned int source_x;
    unsigned int source_y;
    u32 source;
    unsigned int source_alpha;
    unsigned int combined_alpha;
    u16 *destination;

    if (
        !g_video.font_atlas_ready ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_x_offset + source_width > TITAN_VIDEO_FONT_ATLAS_WIDTH ||
        source_y_offset + source_height > TITAN_VIDEO_FONT_ATLAS_HEIGHT ||
        alpha == 0u
    )
    {
        return;
    }

    physical_x0 = TitanVideo_MapLogicalX(x);
    physical_y0 = TitanVideo_MapLogicalY(y);
    physical_x1 = TitanVideo_MapLogicalX(x + width);
    physical_y1 = TitanVideo_MapLogicalY(y + height);
    physical_width = physical_x1 - physical_x0;
    physical_height = physical_y1 - physical_y0;

    if (physical_width <= 0 || physical_height <= 0)
    {
        return;
    }

    for (destination_y = 0; destination_y < physical_height; destination_y++)
    {
        int screen_y;

        screen_y = physical_y0 + destination_y;

        if (screen_y < 0 || screen_y >= TITAN_VIDEO_HEIGHT)
        {
            continue;
        }

        source_y = source_y_offset +
            (((unsigned int)destination_y * source_height) /
             (unsigned int)physical_height);

        if (source_y >= source_y_offset + source_height)
        {
            source_y = source_y_offset + source_height - 1u;
        }

        for (destination_x = 0; destination_x < physical_width; destination_x++)
        {
            int screen_x;

            screen_x = physical_x0 + destination_x;

            if (screen_x < 0 || screen_x >= TITAN_VIDEO_WIDTH)
            {
                continue;
            }

            source_x = source_x_offset +
                (((unsigned int)destination_x * source_width) /
                 (unsigned int)physical_width);

            if (source_x >= source_x_offset + source_width)
            {
                source_x = source_x_offset + source_width - 1u;
            }

            source = g_video.font_atlas[
                (source_y * TITAN_VIDEO_FONT_ATLAS_WIDTH) +
                source_x
            ];
            source_alpha = source >> 24u;

            if (source_alpha == 0u)
            {
                continue;
            }

            combined_alpha =
                ((source_alpha * (unsigned int)alpha) + 64u) /
                128u;

            if (combined_alpha > 255u)
            {
                combined_alpha = 255u;
            }

            destination = &g_video.pixels[
                (screen_y * TITAN_VIDEO_WIDTH) + screen_x
            ];

            *destination = TitanVideo_BlendColor(
                *destination,
                red,
                green,
                blue,
                (unsigned char)combined_alpha
            );
        }
    }
}

TitanResult TitanVideo_UploadUiAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    if (
        pixels == 0 ||
        width != TITAN_VIDEO_UI_ATLAS_WIDTH ||
        height != TITAN_VIDEO_UI_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_UI_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    return TITAN_RESULT_OK;
}

void TitanVideo_DrawUiAtlasRegion32(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)source_x;
    (void)source_y;
    (void)source_width;
    (void)source_height;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}

void TitanVideo_DrawUiAtlasRegion32Point(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)source_x;
    (void)source_y;
    (void)source_width;
    (void)source_height;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}

TitanResult TitanVideo_EndFrame(void)
{
    qword_t *cursor;

    if (
        !g_video.initialized ||
        g_video.upload_packet == 0
    )
    {
        return TITAN_ERROR_FRAME_END;
    }

    packet2_reset(
        g_video.upload_packet,
        0
    );

    cursor = g_video.upload_packet->base;

    cursor = draw_texture_transfer(
        cursor,
        g_video.pixels,
        TITAN_VIDEO_WIDTH,
        TITAN_VIDEO_HEIGHT,
        GS_PSM_16,
        (int)g_video.framebuffer_addresses[g_video.drawing_buffer],
        TITAN_VIDEO_WIDTH
    );

    cursor = draw_texture_flush(cursor);

    packet2_update(
        g_video.upload_packet,
        cursor
    );

    FlushCache(0);

    dma_channel_send_packet2(
        g_video.upload_packet,
        DMA_CHANNEL_GIF,
        1
    );

    if (
        dma_channel_wait(
            DMA_CHANNEL_GIF,
            0
        ) < 0
    )
    {
        return TITAN_ERROR_FRAME_END;
    }

    graph_wait_vsync();

    graph_set_framebuffer_filtered(
        (int)g_video.framebuffer_addresses[g_video.drawing_buffer],
        TITAN_VIDEO_WIDTH,
        GS_PSM_16,
        0,
        0
    );

    g_video.displayed_buffer = g_video.drawing_buffer;
    g_video.drawing_buffer ^= 1u;

    return TITAN_RESULT_OK;
}

static int g_video_software_game_mode = 0;
TitanResult TitanVideo_BeginGameMode(unsigned int width, unsigned int height)
{
    if (g_video_software_game_mode ||
        width != TITAN_VIDEO_LEGACY_WIDTH || height != TITAN_VIDEO_LEGACY_HEIGHT)
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    g_video_software_game_mode = 1;
    return TITAN_RESULT_OK;
}
TitanResult TitanVideo_EndGameMode(void)
{
    g_video_software_game_mode = 0;
    return TITAN_RESULT_OK;
}
int TitanVideo_IsGameMode(void) { return g_video_software_game_mode; }
unsigned int TitanVideo_GetGameZBufferAddress(void) { return g_video_software_game_mode ? 1u : 0u; }
unsigned int TitanVideo_GetGameTextureBytesFree(void) { return 0u; }
void TitanVideo_DrawGameTrianglePhysical(
    float x0,float y0,unsigned int z0,float x1,float y1,unsigned int z1,
    float x2,float y2,unsigned int z2,unsigned char r,unsigned char g,unsigned char b,unsigned char a)
{
    (void)z0; (void)z1; (void)z2;
    TitanVideo_DrawSolidQuadAlpha(x0,y0,x1,y1,x2,y2,x2,y2,r,g,b,a);
}
int TitanVideo_DrawGameTexturedTrianglePhysical(
    float x0,float y0,unsigned int z0,float u0,float v0,
    float x1,float y1,unsigned int z1,float u1,float v1,
    float x2,float y2,unsigned int z2,float u2,float v2,
    unsigned char r0,unsigned char g0,unsigned char b0,unsigned char a0,
    unsigned char r1,unsigned char g1,unsigned char b1,unsigned char a1,
    unsigned char r2,unsigned char g2,unsigned char b2,unsigned char a2,
    uint32_t texture_handle)
{
    (void)u0; (void)v0; (void)u1; (void)v1; (void)u2; (void)v2;
    (void)r1; (void)g1; (void)b1; (void)a1;
    (void)r2; (void)g2; (void)b2; (void)a2; (void)texture_handle;
    TitanVideo_DrawGameTrianglePhysical(
        x0,y0,z0,x1,y1,z1,x2,y2,z2,r0,g0,b0,a0
    );
    return 1;
}
int TitanVideo_GameSetRenderState(const TitanGameRenderState *state)
{
    return g_video_software_game_mode && state != 0 &&
        state->struct_size >= offsetof(TitanGameRenderState, reserved);
}
int TitanVideo_DrawGameMaterialTrianglePhysical(
    float x0,float y0,unsigned int z0,
    float s00,float t00,float q00,float s10,float t10,float q10,
    float x1,float y1,unsigned int z1,
    float s01,float t01,float q01,float s11,float t11,float q11,
    float x2,float y2,unsigned int z2,
    float s02,float t02,float q02,float s12,float t12,float q12,
    unsigned char r0,unsigned char g0,unsigned char b0,unsigned char a0,
    unsigned char r1,unsigned char g1,unsigned char b1,unsigned char a1,
    unsigned char r2,unsigned char g2,unsigned char b2,unsigned char a2,
    unsigned char fog0,unsigned char fog1,unsigned char fog2,
    unsigned char lod0,unsigned char lod1,unsigned char lod2,
    unsigned int texture0_handle,unsigned int texture1_handle)
{
    (void)s00;(void)t00;(void)q00;(void)s10;(void)t10;(void)q10;
    (void)s01;(void)t01;(void)q01;(void)s11;(void)t11;(void)q11;
    (void)s02;(void)t02;(void)q02;(void)s12;(void)t12;(void)q12;
    (void)r1;(void)g1;(void)b1;(void)a1;(void)r2;(void)g2;(void)b2;(void)a2;
    (void)fog0;(void)fog1;(void)fog2;(void)lod0;(void)lod1;(void)lod2;
    (void)texture0_handle;(void)texture1_handle;
    TitanVideo_DrawGameTrianglePhysical(
        x0,y0,z0,x1,y1,z1,x2,y2,z2,r0,g0,b0,a0
    );
    return 1;
}
TitanResult TitanVideo_GameUploadTexturePsm16(
    const unsigned short *pixels,unsigned int width,unsigned int height,
    unsigned int stride,uint32_t *texture_handle)
{
    (void)pixels; (void)width; (void)height; (void)stride;
    if (texture_handle != 0) *texture_handle = 0u;
    return TITAN_ERROR_VIDEO_INITIALIZATION;
}
TitanResult TitanVideo_GameUploadTextureRgba32(
    const unsigned char *rgba,unsigned int width,unsigned int height,
    unsigned int stride_bytes,uint32_t *texture_handle)
{
    (void)rgba; (void)width; (void)height; (void)stride_bytes;
    if (texture_handle != 0) *texture_handle = 0u;
    return TITAN_ERROR_VIDEO_INITIALIZATION;
}
int TitanVideo_GameReleaseTexture(uint32_t texture_handle)
{ (void)texture_handle; return 0; }
void TitanVideo_GameResetTextures(void) { }
void TitanVideo_SetPikaVrOptimizedPath(int enabled)
{
    TitanPikaVr_SetOptimizedPath(enabled);
}
void TitanVideo_GetPikaVrTelemetry(TitanPikaVrTelemetry *telemetry)
{
    TitanPikaVr_GetTelemetry(telemetry);
}
