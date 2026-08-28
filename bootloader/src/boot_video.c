#include "boot_video.h"

#include "generated/titan_boot_screen_data.h"

#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <graph_vram.h>
#include <gs_psm.h>
#include <kernel.h>
#include <malloc.h>
#include <packet2.h>
#include <tamtypes.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_BOOT_VIDEO_WIDTH 640
#define TITAN_BOOT_VIDEO_HEIGHT 448
#define TITAN_BOOT_VIDEO_BYTES \
    (TITAN_BOOT_VIDEO_WIDTH * TITAN_BOOT_VIDEO_HEIGHT * 2)
#define TITAN_BOOT_UPLOAD_PACKET_QWORDS 32u
#define TITAN_BOOT_FADE_IN_FRAMES 14u
#define TITAN_BOOT_FADE_OUT_FRAMES 10u
#define TITAN_BOOT_PROGRESS_STEP 4u

#define TITAN_BOOT_BAR_X 200
#define TITAN_BOOT_BAR_Y 276
#define TITAN_BOOT_BAR_WIDTH 240
#define TITAN_BOOT_BAR_HEIGHT 8
#define TITAN_BOOT_BAR_INSET 2

static int titan_boot_framebuffers[2] = {-1, -1};
static int titan_boot_displayed_buffer;
static uint16_t *titan_boot_composite;
static unsigned int titan_boot_progress;
static int titan_boot_video_ready;

static uint16_t TitanBootVideo_PackColor(
    unsigned int red,
    unsigned int green,
    unsigned int blue
)
{
    return (uint16_t)(
        0x8000u |
        ((red >> 3u) & 0x1fu) |
        (((green >> 3u) & 0x1fu) << 5u) |
        (((blue >> 3u) & 0x1fu) << 10u)
    );
}

static void TitanBootVideo_FillRect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
)
{
    int row;
    int column;

    for (row = y; row < y + height; row++)
    {
        uint16_t *destination =
            titan_boot_composite +
            (row * TITAN_BOOT_VIDEO_WIDTH) + x;

        for (column = 0; column < width; column++)
        {
            destination[column] = color;
        }
    }
}

static void TitanBootVideo_ComposeProgress(
    unsigned int percent
)
{
    const int inner_width =
        TITAN_BOOT_BAR_WIDTH - (TITAN_BOOT_BAR_INSET * 2);
    const int inner_height =
        TITAN_BOOT_BAR_HEIGHT - (TITAN_BOOT_BAR_INSET * 2);
    int filled_width;

    memcpy(
        titan_boot_composite,
        titan_boot_screen,
        TITAN_BOOT_VIDEO_BYTES
    );

    /* At 100% keep only the centered logo visible while stage2 wipes EE
       memory and loads kernel.elf. This removes the long black handoff. */
    if (percent < 100u)
    {
        TitanBootVideo_FillRect(
            TITAN_BOOT_BAR_X,
            TITAN_BOOT_BAR_Y,
            TITAN_BOOT_BAR_WIDTH,
            TITAN_BOOT_BAR_HEIGHT,
            TitanBootVideo_PackColor(86u, 86u, 86u)
        );

        TitanBootVideo_FillRect(
            TITAN_BOOT_BAR_X + TITAN_BOOT_BAR_INSET,
            TITAN_BOOT_BAR_Y + TITAN_BOOT_BAR_INSET,
            inner_width,
            inner_height,
            TitanBootVideo_PackColor(18u, 18u, 18u)
        );

        filled_width = (int)(
            ((unsigned int)inner_width * percent) / 100u
        );

        if (percent > 0u && filled_width == 0)
        {
            filled_width = 1;
        }

        if (filled_width > 0)
        {
            TitanBootVideo_FillRect(
                TITAN_BOOT_BAR_X + TITAN_BOOT_BAR_INSET,
                TITAN_BOOT_BAR_Y + TITAN_BOOT_BAR_INSET,
                filled_width,
                inner_height,
                TitanBootVideo_PackColor(206u, 42u, 42u)
            );
        }
    }
}

static void TitanBootVideo_ApplyBrightness(
    unsigned int brightness
)
{
    uint16_t channel[32];
    size_t pixel_count;
    size_t index;

    if (brightness >= 255u)
    {
        return;
    }

    for (index = 0u; index < 32u; index++)
    {
        channel[index] = (uint16_t)(
            ((unsigned int)index * brightness) / 255u
        );
    }

    pixel_count =
        (size_t)TITAN_BOOT_VIDEO_WIDTH *
        (size_t)TITAN_BOOT_VIDEO_HEIGHT;

    for (index = 0u; index < pixel_count; index++)
    {
        const uint16_t source = titan_boot_composite[index];

        titan_boot_composite[index] = (uint16_t)(
            0x8000u |
            channel[source & 0x1fu] |
            (channel[(source >> 5u) & 0x1fu] << 5u) |
            (channel[(source >> 10u) & 0x1fu] << 10u)
        );
    }
}

static int TitanBootVideo_Upload(
    int framebuffer_address
)
{
    packet2_t *packet;
    qword_t *cursor;

    packet = packet2_create(
        (u16)TITAN_BOOT_UPLOAD_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        0
    );

    if (packet == 0)
    {
        return -1;
    }

    cursor = packet->base;
    cursor = draw_texture_transfer(
        cursor,
        titan_boot_composite,
        TITAN_BOOT_VIDEO_WIDTH,
        TITAN_BOOT_VIDEO_HEIGHT,
        GS_PSM_16,
        framebuffer_address,
        TITAN_BOOT_VIDEO_WIDTH
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(packet, cursor);

    FlushCache(WRITEBACK_DCACHE);

    dma_channel_send_packet2(
        packet,
        DMA_CHANNEL_GIF,
        1
    );

    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        packet2_free(packet);
        return -2;
    }

    packet2_free(packet);
    return 0;
}

static int TitanBootVideo_PresentProgress(
    unsigned int percent,
    unsigned int brightness
)
{
    int hidden_buffer;
    int result;

    hidden_buffer = titan_boot_displayed_buffer ^ 1;
    TitanBootVideo_ComposeProgress(percent);
    TitanBootVideo_ApplyBrightness(brightness);

    result = TitanBootVideo_Upload(
        titan_boot_framebuffers[hidden_buffer]
    );

    if (result < 0)
    {
        return result;
    }

    graph_wait_vsync();
    graph_set_framebuffer_filtered(
        titan_boot_framebuffers[hidden_buffer],
        TITAN_BOOT_VIDEO_WIDTH,
        GS_PSM_16,
        0,
        0
    );
    titan_boot_displayed_buffer = hidden_buffer;
    return 0;
}

static int TitanBootVideo_Fade(
    unsigned int from,
    unsigned int to,
    unsigned int frames
)
{
    unsigned int frame;
    int result;

    if (frames == 0u)
    {
        return TitanBootVideo_PresentProgress(
            titan_boot_progress,
            to
        );
    }

    for (frame = 0u; frame <= frames; frame++)
    {
        const unsigned int brightness =
            from < to
                ? from + (((to - from) * frame) / frames)
                : from - (((from - to) * frame) / frames);

        result = TitanBootVideo_PresentProgress(
            titan_boot_progress,
            brightness
        );

        if (result < 0)
        {
            return result;
        }
    }

    return 0;
}

int TitanBootVideo_Show(void)
{
    int result;

    if (
        size_titan_boot_screen !=
        TITAN_BOOT_VIDEO_BYTES
    )
    {
        return -1;
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
        return -2;
    }

    graph_vram_clear();

    titan_boot_framebuffers[0] = graph_vram_allocate(
        TITAN_BOOT_VIDEO_WIDTH,
        TITAN_BOOT_VIDEO_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_PAGE
    );

    titan_boot_framebuffers[1] = graph_vram_allocate(
        TITAN_BOOT_VIDEO_WIDTH,
        TITAN_BOOT_VIDEO_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_PAGE
    );

    if (
        titan_boot_framebuffers[0] < 0 ||
        titan_boot_framebuffers[1] < 0
    )
    {
        return -3;
    }

    if (
        graph_initialize(
            titan_boot_framebuffers[0],
            TITAN_BOOT_VIDEO_WIDTH,
            TITAN_BOOT_VIDEO_HEIGHT,
            GS_PSM_16,
            0,
            0
        ) < 0
    )
    {
        return -4;
    }

    graph_set_bgcolor(0u, 0u, 0u);

    if (titan_boot_composite != 0)
    {
        free(titan_boot_composite);
        titan_boot_composite = 0;
    }
    titan_boot_video_ready = 0;

    titan_boot_composite = (uint16_t *)memalign(
        64u,
        TITAN_BOOT_VIDEO_BYTES
    );

    if (titan_boot_composite == 0)
    {
        return -5;
    }

    titan_boot_displayed_buffer = 0;
    titan_boot_progress = 0u;
    TitanBootVideo_ComposeProgress(0u);
    TitanBootVideo_ApplyBrightness(0u);
    result = TitanBootVideo_Upload(
        titan_boot_framebuffers[0]
    );

    if (result < 0)
    {
        free(titan_boot_composite);
        titan_boot_composite = 0;
        return -6;
    }

    titan_boot_video_ready = 1;
    result = TitanBootVideo_Fade(
        0u,
        255u,
        TITAN_BOOT_FADE_IN_FRAMES
    );

    if (result < 0)
    {
        titan_boot_video_ready = 0;
        free(titan_boot_composite);
        titan_boot_composite = 0;
        return -7;
    }

    return 0;
}

int TitanBootVideo_SetProgress(
    unsigned int percent
)
{
    int result;

    if (!titan_boot_video_ready)
    {
        return -1;
    }

    if (percent > 100u)
    {
        percent = 100u;
    }

    if (percent <= titan_boot_progress)
    {
        return 0;
    }

    while (titan_boot_progress < percent)
    {
        unsigned int next =
            titan_boot_progress + TITAN_BOOT_PROGRESS_STEP;

        if (next > percent)
        {
            next = percent;
        }

        result = TitanBootVideo_PresentProgress(next, 255u);

        if (result < 0)
        {
            return result;
        }

        titan_boot_progress = next;
    }

    return 0;
}

int TitanBootVideo_FadeOut(void)
{
    if (!titan_boot_video_ready)
    {
        return -1;
    }

    return TitanBootVideo_Fade(
        255u,
        0u,
        TITAN_BOOT_FADE_OUT_FRAMES
    );
}
