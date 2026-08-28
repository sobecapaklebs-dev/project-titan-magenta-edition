#include "stage2_video.h"

#include "generated/titan_stage2_logo_data.h"

#include <dma.h>
#include <draw.h>
#include <draw2d.h>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include <graph.h>
#include <graph_vram.h>
#include <gs_psm.h>
#include <kernel.h>
#include <packet2.h>
#include <tamtypes.h>

#include <string.h>

#define TITAN_STAGE2_VIDEO_WIDTH 640
#define TITAN_STAGE2_VIDEO_HEIGHT 448
#define TITAN_STAGE2_LOGO_WIDTH 176
#define TITAN_STAGE2_LOGO_HEIGHT 33
#define TITAN_STAGE2_TEXTURE_WIDTH 256
#define TITAN_STAGE2_TEXTURE_HEIGHT 64
#define TITAN_STAGE2_DRAW_CONTEXT 0
#define TITAN_STAGE2_UPLOAD_QWORDS 32u
#define TITAN_STAGE2_DRAW_QWORDS 96u

static color_t TitanStage2Video_Color(
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    color_t color;

    color.r = red;
    color.g = green;
    color.b = blue;
    color.a = alpha;
    color.q = 1.0f;
    return color;
}

int TitanStage2Video_ShowLogo(void)
{
    framebuffer_t framebuffer;
    zbuffer_t zbuffer;
    texbuffer_t texture;
    clutbuffer_t clut;
    lod_t lod;
    texrect_t logo_rect;
    packet2_t *upload_packet;
    packet2_t *draw_packet;
    qword_t *cursor;
    int framebuffer_address;
    int texture_address;

    if (
        size_titan_stage2_logo !=
        TITAN_STAGE2_LOGO_WIDTH * TITAN_STAGE2_LOGO_HEIGHT * 2
    )
    {
        return -1;
    }

    dma_reset();
    if (dma_channel_initialize(DMA_CHANNEL_GIF, 0, 0) < 0)
    {
        return -2;
    }

    graph_vram_clear();
    framebuffer_address = graph_vram_allocate(
        TITAN_STAGE2_VIDEO_WIDTH,
        TITAN_STAGE2_VIDEO_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_PAGE
    );
    texture_address = graph_vram_allocate(
        TITAN_STAGE2_TEXTURE_WIDTH,
        TITAN_STAGE2_TEXTURE_HEIGHT,
        GS_PSM_16,
        GRAPH_ALIGN_BLOCK
    );

    if (framebuffer_address < 0 || texture_address < 0)
    {
        return -3;
    }

    if (
        graph_initialize(
            framebuffer_address,
            TITAN_STAGE2_VIDEO_WIDTH,
            TITAN_STAGE2_VIDEO_HEIGHT,
            GS_PSM_16,
            0,
            0
        ) < 0
    )
    {
        return -4;
    }
    graph_set_bgcolor(0u, 0u, 0u);

    upload_packet = packet2_create(
        (u16)TITAN_STAGE2_UPLOAD_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        0
    );
    if (upload_packet == 0)
    {
        return -5;
    }

    cursor = upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        titan_stage2_logo,
        TITAN_STAGE2_LOGO_WIDTH,
        TITAN_STAGE2_LOGO_HEIGHT,
        GS_PSM_16,
        texture_address,
        TITAN_STAGE2_TEXTURE_WIDTH
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(upload_packet, cursor);

    FlushCache(WRITEBACK_DCACHE);
    dma_channel_send_packet2(upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        packet2_free(upload_packet);
        return -6;
    }
    packet2_free(upload_packet);

    memset(&framebuffer, 0, sizeof(framebuffer));
    framebuffer.address = (unsigned int)framebuffer_address;
    framebuffer.width = TITAN_STAGE2_VIDEO_WIDTH;
    framebuffer.height = TITAN_STAGE2_VIDEO_HEIGHT;
    framebuffer.psm = GS_PSM_16;
    framebuffer.mask = 0u;

    memset(&zbuffer, 0, sizeof(zbuffer));
    zbuffer.enable = 0u;
    zbuffer.address = 0u;
    zbuffer.zsm = GS_PSMZ_16;
    zbuffer.mask = 1u;

    memset(&texture, 0, sizeof(texture));
    texture.address = (unsigned int)texture_address;
    texture.width = TITAN_STAGE2_TEXTURE_WIDTH;
    texture.psm = GS_PSM_16;
    texture.info.width = draw_log2(TITAN_STAGE2_TEXTURE_WIDTH);
    texture.info.height = draw_log2(TITAN_STAGE2_TEXTURE_HEIGHT);
    texture.info.components = TEXTURE_COMPONENTS_RGBA;
    texture.info.function = TEXTURE_FUNCTION_MODULATE;

    memset(&clut, 0, sizeof(clut));
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.load_method = CLUT_NO_LOAD;

    memset(&lod, 0, sizeof(lod));
    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    memset(&logo_rect, 0, sizeof(logo_rect));
    logo_rect.v0.x =
        (float)(TITAN_STAGE2_VIDEO_WIDTH - TITAN_STAGE2_LOGO_WIDTH) * 0.5f;
    logo_rect.v0.y =
        (float)(TITAN_STAGE2_VIDEO_HEIGHT - TITAN_STAGE2_LOGO_HEIGHT) * 0.5f;
    logo_rect.v0.z = 0u;
    logo_rect.t0.u = 0.0f;
    logo_rect.t0.v = 0.0f;
    logo_rect.v1.x = logo_rect.v0.x + (float)TITAN_STAGE2_LOGO_WIDTH;
    logo_rect.v1.y = logo_rect.v0.y + (float)TITAN_STAGE2_LOGO_HEIGHT;
    logo_rect.v1.z = 0u;
    logo_rect.t1.u = (float)TITAN_STAGE2_LOGO_WIDTH;
    logo_rect.t1.v = (float)TITAN_STAGE2_LOGO_HEIGHT;
    logo_rect.color = TitanStage2Video_Color(255u, 255u, 255u, 128u);

    draw_packet = packet2_create(
        (u16)TITAN_STAGE2_DRAW_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_NORMAL,
        0
    );
    if (draw_packet == 0)
    {
        return -7;
    }

    cursor = draw_packet->base;
    cursor = draw_setup_environment(
        cursor,
        TITAN_STAGE2_DRAW_CONTEXT,
        &framebuffer,
        &zbuffer
    );
    cursor = draw_clear(
        cursor,
        TITAN_STAGE2_DRAW_CONTEXT,
        0.0f,
        0.0f,
        (float)TITAN_STAGE2_VIDEO_WIDTH,
        (float)TITAN_STAGE2_VIDEO_HEIGHT,
        0u,
        0u,
        0u
    );
    cursor = draw_texture_sampling(cursor, TITAN_STAGE2_DRAW_CONTEXT, &lod);
    cursor = draw_texturebuffer(
        cursor,
        TITAN_STAGE2_DRAW_CONTEXT,
        &texture,
        &clut
    );
    cursor = draw_rect_textured(
        cursor,
        TITAN_STAGE2_DRAW_CONTEXT,
        &logo_rect
    );
    cursor = draw_finish(cursor);
    packet2_update(draw_packet, cursor);

    FlushCache(WRITEBACK_DCACHE);
    dma_channel_send_packet2(draw_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        packet2_free(draw_packet);
        return -8;
    }
    draw_wait_finish();
    packet2_free(draw_packet);

    graph_wait_vsync();
    graph_set_framebuffer_filtered(
        framebuffer_address,
        TITAN_STAGE2_VIDEO_WIDTH,
        GS_PSM_16,
        0,
        0
    );
    graph_enable_output();
    graph_wait_vsync();

    return 0;
}
