#ifdef TITAN_VIDEO_RENDERER_SOFTWARE

#include "backends/titan_video_software.inc"

#else

#include "titan_video.h"
#include "titan_video_game_math.h"
#include "titan_video_game_texture.h"
#include "titan_video_memory.h"
#include "titan_video_720p_plan.h"
#include "titan_video_720p_transaction.h"
#include "titan_video_mode.h"
#include "titan_pikavr.h"
#include "titan_vram_boot_guard.h"
#include "titan_wave_vu1.h"
#include "titan_game_vu1.h"

#include <dma.h>
#include <draw.h>
#include <draw2d.h>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <tamtypes.h>

/* P160.1 host/target cache-coherency prototype bridge.
 *
 * Real PS2SDK kernel.h declares SyncDCache(), but Titan's host syntax stubs do
 * not. Keep the exact official EE signature visible to both environments so
 * the PS2 DMA path can be syntax-checked without weakening -Werror. */
extern void SyncDCache(void *start, void *end);

#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(__EE__) || defined(_EE)
#include <fcntl.h>
#include <unistd.h>
#endif

#define TITAN_VIDEO_DRAW_CONTEXT 0
#define TITAN_VIDEO_PACKET_QWORDS 60000u
#define TITAN_VIDEO_PACKET_SAFETY_QWORDS 192u
#define TITAN_VIDEO_UPLOAD_PACKET_QWORDS 32u
/* P162.4 large-I8 upload proof. draw_texture_transfer() stores only DMA/GIF
 * metadata in packet2; image payload travels through DMATAG_REF. As long as
 * the maximum I8 image remains below one GIF_BLOCK_SIZE, each texture or
 * CLUT transfer needs at most 9 local qwords (6 setup + 3 REF chain), then
 * TEXFLUSH needs 3. Keep this compile-time guard next to the packet size. */
#define TITAN_VIDEO_GIF_IMAGE_BLOCK_QWORDS 0x7FFFu
#define TITAN_VIDEO_TEXTURE_TRANSFER_ONE_BLOCK_QWORDS 9u
#define TITAN_VIDEO_TEXTURE_FLUSH_QWORDS 3u
#define TITAN_VIDEO_GAME_I8_UPLOAD_REQUIRED_QWORDS \
    ((2u * TITAN_VIDEO_TEXTURE_TRANSFER_ONE_BLOCK_QWORDS) + \
     TITAN_VIDEO_TEXTURE_FLUSH_QWORDS)
typedef char TitanVideoP1624I8PayloadFitsOneGifBlock[
    (((TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH *
       TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT) >> 4u) <
      TITAN_VIDEO_GIF_IMAGE_BLOCK_QWORDS) ? 1 : -1
];
#if defined(GIF_BLOCK_SIZE)
typedef char TitanVideoP1624Ps2SdkGifBlockSizeProof[
    (GIF_BLOCK_SIZE == TITAN_VIDEO_GIF_IMAGE_BLOCK_QWORDS) ? 1 : -1
];
#endif
typedef char TitanVideoP1624UploadPacketCapacityProof[
    (TITAN_VIDEO_UPLOAD_PACKET_QWORDS >=
     TITAN_VIDEO_GAME_I8_UPLOAD_REQUIRED_QWORDS) ? 1 : -1
];
#define TITAN_VIDEO_BACKGROUND_BANDS 180u
#define TITAN_VIDEO_ELLIPSE_SEGMENTS 48u
#define TITAN_VIDEO_CORNER_SEGMENTS 12u
#define TITAN_VIDEO_Z 0x00000000u
#define TITAN_VIDEO_TEXTURE_NONE 0
#define TITAN_VIDEO_TEXTURE_CACHED_LAYER 1
#define TITAN_VIDEO_TEXTURE_AVATAR_ATLAS 2
#define TITAN_VIDEO_TEXTURE_FONT_ATLAS 3
#define TITAN_VIDEO_TEXTURE_UI_ATLAS 4
#define TITAN_VIDEO_TEXTURE_TRANSIENT_ATLAS 5
#define TITAN_VIDEO_TEXTURE_COMPOSITION 6
#define TITAN_VIDEO_OPTIMIZED_SURFACE_COUNT 1u
#define TITAN_VIDEO_TEXTURE_GAME_BASE 100
#define TITAN_VIDEO_HD_FONT_STORAGE_WIDTH 384u
#define TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT 192u
#define TITAN_VIDEO_ALPHA_CLUT_WIDTH 16u
#define TITAN_VIDEO_ALPHA_CLUT_HEIGHT 16u
#define TITAN_VIDEO_AVATAR_SCRATCH_WIDTH 256u
#define TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT 64u
#define TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT 32u
#define TITAN_VIDEO_AVATAR_CHUNK_INVALID 0xFFFFFFFFu

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_video_post_switch_probe_pending = 0;

/* P10.23 textured A/B diagnostics.  These handles are allocated once when
 * Titan Game mode begins, before GoldenEye starts its high-churn texture
 * workload, and remain live until game mode ends. */
static uint32_t g_p123_diag_psm16_handle = 0u;
static uint32_t g_p123_diag_psm8_handle = 0u;
static unsigned int g_p123_diag_texture_status = 0u;
static unsigned int g_p130_diag_heartbeat_frame = 0u;

#define TITAN_P123_DIAG_PSM16_READY 0x01u
#define TITAN_P123_DIAG_PSM8_READY  0x02u
#define TITAN_P123_DIAG_PSM8_EXACT  0x04u

static void TitanVideo_GameDiagnosticTextureInit(void);
static void TitanVideo_GameDiagnosticTextureReset(void);

#if defined(__EE__) || defined(_EE)
static void TitanVideo_SwitchProbe(const char *stage, int result)
{
    static int first = 1;
    const char *primary = "mass:/TITAN/video_switch_probe.log";
    const char *fallback = "mass:/video_switch_probe.log";
    char line[160];
    int flags;
    int fd;
    int length;

    if (stage == 0)
        return;

    flags = O_WRONLY | O_CREAT | (first ? O_TRUNC : O_APPEND);
    fd = open(primary, flags, 0666);
    if (fd < 0)
        fd = open(fallback, flags, 0666);
    if (fd < 0)
        return;

    length = snprintf(line, sizeof(line), "video-stage=%s result=%d\n", stage, result);
    if (length > 0)
    {
        if ((unsigned int)length >= sizeof(line))
            length = (int)sizeof(line) - 1;
        (void)write(fd, line, (size_t)length);
    }
    close(fd);
    first = 0;
}
#else
static void TitanVideo_SwitchProbe(const char *stage, int result)
{
    (void)stage;
    (void)result;
}
#endif

typedef struct TitanVideoColor
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} TitanVideoColor;

typedef struct TitanVideoFramebufferConfig
{
    TitanVideoOutputMode output;
    unsigned int width;
    unsigned int height;
    unsigned int storage_height;
    unsigned int count;
    int psm;
} TitanVideoFramebufferConfig;

typedef struct TitanVideoState
{
    int initialized;
    int game_mode;
    TitanVideoOutputMode game_restore_output;
    unsigned int game_zbuffer_address;
    unsigned int game_texture_arena_address;
    unsigned int bound_game_texture_handle;
    TitanVideoGameTexturePool game_texture_pool;
    TitanGameRenderState game_render_state;

    /* P10.47 material-state cache.  Fast3D commonly emits dozens of adjacent
     * triangles with identical TEST/ZBUF/ALPHA/SCISSOR/CLAMP/TEX state.
     * Re-emitting that six-qword state packet for every triangle roughly
     * doubles GIF traffic in the untextured lane. */
    int game_material_state_dirty;
    unsigned int game_material_texture_handle;
    unsigned int game_material_second_pass;

    /* P10.58 game texture DMA pipeline.
     *
     * One staging slot is filled by the EE while the prior slot is consumed
     * by GIF DMA.  A draw submission is the ordering fence: no GS primitive
     * referencing a newly uploaded texture can pass its texture transfer. */
    int game_texture_upload_pending;
    unsigned int game_texture_upload_handle;
    unsigned int game_texture_staging_index;

    /* P162 PS2-native geometry ownership. PATH3 work is fenced only when a
     * subsequent VU1/PATH1 batch could overtake it; consecutive VU1 batches
     * do not pay a per-draw FINISH. */
    int game_vu1_active;
    int game_path3_pending;

    int background_ready;
    int frame_open;
    int packet_error;
    int bound_texture;
    float logical_scale_x;
    float logical_scale_y;
    float logical_offset_y;
    unsigned int current_frame_qwords;
    unsigned int current_external_qwords;
    unsigned int last_frame_qwords;

    packet2_t *draw_packet;
    packet2_t *upload_packet;
    framebuffer_t framebuffers[2];
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned int framebuffer_count;
    int framebuffer_psm;
    unsigned int displayed_buffer;
    unsigned int drawing_buffer;
    TitanVideo720pPath hd_path_preference;
    int optimized_720p_active;
    unsigned int internal_width;
    unsigned int internal_height;
    framebuffer_t composition_framebuffer;
    texbuffer_t composition_texture;
    zbuffer_t zbuffer;
    texbuffer_t cached_layer;
    int cached_layer_ready;
    texbuffer_t avatar_atlas;
    int avatar_atlas_ready;
    texbuffer_t font_atlas;
    int font_atlas_ready;
    texbuffer_t ui_atlas;
    int ui_atlas_ready;
    texbuffer_t transient_atlas;
    unsigned int transient_atlas_uploaded_generation;
    unsigned int alpha_clut_address;
    int compact_resources;
    unsigned int avatar_cached_chunk;
    const unsigned short *cached_layer_source;
    const unsigned short *avatar_atlas_source;
    const unsigned int *font_atlas_source;
    const unsigned int *ui_atlas_source;
    unsigned int cached_layer_source_stride;
    unsigned int avatar_atlas_source_stride;
    unsigned int font_atlas_source_stride;
    unsigned int ui_atlas_source_stride;

    TitanVideoColor background_top;
    TitanVideoColor background_bottom;

    float transform_scale;
    float transform_origin_x;
    float transform_origin_y;
    float transform_offset_x;
    float transform_offset_y;
} TitanVideoState;

static TitanVideoState g_video;

static unsigned char g_video_font_indices[
    TITAN_VIDEO_HD_FONT_STORAGE_WIDTH *
    TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT
] __attribute__((aligned(64)));

static unsigned char g_video_ui_indices[
    TITAN_VIDEO_UI_ATLAS_WIDTH *
    TITAN_VIDEO_UI_ATLAS_HEIGHT
] __attribute__((aligned(64)));

static unsigned int g_video_alpha_clut[
    TITAN_VIDEO_ALPHA_CLUT_WIDTH *
    TITAN_VIDEO_ALPHA_CLUT_HEIGHT
] __attribute__((aligned(64)));

static unsigned short g_video_avatar_scratch[
    TITAN_VIDEO_AVATAR_SCRATCH_WIDTH *
    TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT
] __attribute__((aligned(64)));

/* Native 720p has no spare GS VRAM: two 1280x768 PSM16 scanout allocations
 * plus resident atlases consume the full 4 MiB. Media therefore reuses the
 * already-reserved avatar scratch texture and this EE-side staging buffer. */
static unsigned short g_video_media_scratch[
    TITAN_VIDEO_AVATAR_SCRATCH_WIDTH *
    TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT
] __attribute__((aligned(64)));

/* Public Titan App video is conventional RGB565. GS_PSM_16 is R5G5B5A1,
 * so raw uploads corrupt both color and alpha. Non-compact modes convert the
 * whole frame here; compact 720p converts each scratch tile in-place. */
static unsigned short g_video_media_psm16[
    TITAN_VIDEO_MEDIA_SURFACE_WIDTH * TITAN_VIDEO_MEDIA_SURFACE_HEIGHT
] __attribute__((aligned(64)));

/* draw_texture_transfer truncates sub-qword source sizes.  The game lane pads
 * every row to the GS TBW (64 or 128 pixels), which also makes all DMA payloads
 * qword exact while retaining the original logical width for sampling. */
typedef union TitanVideoGameTextureStaging
{
    unsigned short psm16[
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH *
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT
    ];
    unsigned int psm32[
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH *
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT
    ];
    unsigned char psm8[
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH *
        TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT
    ];
} TitanVideoGameTextureStaging;

/* P10.58: while GIF DMA reads slot N, the EE converts the next texture into
 * slot N^1.  The buffers are EE RAM, not additional GS VRAM. */
static TitanVideoGameTextureStaging
    g_video_game_texture_staging[2] __attribute__((aligned(64)));

static uint32_t
    g_video_game_texture_clut[2][256] __attribute__((aligned(64)));

/* P162.4: large native I8-style assets (the 440x299 gunbarrel eye) use an
 * indexed-only lane. One scratch is enough because the large path fences the
 * previous GIF upload before palette conversion; normal <=128 textures keep
 * the existing two-slot overlap. 512*320 = 160 KiB, not a pair of 640 KiB
 * RGBA staging buffers. */
static unsigned char g_video_game_texture_large_psm8[
    TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH *
    TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT
] __attribute__((aligned(64)));
static uint32_t g_video_game_texture_large_clut[256] __attribute__((aligned(64)));

static unsigned short TitanVideo_Rgb565ToPsm16(unsigned short pixel)
{
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    red = (pixel >> 11u) & 0x1fu;
    green = (pixel >> 5u) & 0x3fu;
    blue = pixel & 0x1fu;

    /* Round 6-bit green to 5 bits and force texture alpha opaque. */
    green = (green + 1u) >> 1u;
    if (green > 0x1fu) green = 0x1fu;

    return (unsigned short)(
        red | (green << 5u) | (blue << 10u) | 0x8000u
    );
}


static float TitanVideo_ClampFloat(
    float value,
    float minimum,
    float maximum
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

static unsigned char TitanVideo_ToGsAlpha(
    unsigned char alpha
)
{
    return TitanVideoGame_ToGsAlpha(alpha);
}

static color_t TitanVideo_MakeColor(
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
    color.a = TitanVideo_ToGsAlpha(alpha);
    color.q = 1.0f;

    return color;
}

static TitanResult TitanVideo_GameWaitTextureUpload(void)
{
    uint32_t failed_handle;

    if (!g_video.game_texture_upload_pending)
        return TITAN_RESULT_OK;

    failed_handle = g_video.game_texture_upload_handle;

    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        g_video.game_texture_upload_pending = 0;
        g_video.game_texture_upload_handle = 0u;
        g_video.packet_error = 1;

        /* Fail closed: a transfer whose DMA completion is unknown must never
         * leave apparently-live backing that a later draw could sample. */
        if (failed_handle != 0u)
        {
            (void)TitanPikaVr_ReleaseTexture(failed_handle);
            (void)TitanVideoGameTexturePool_Release(
                &g_video.game_texture_pool,
                failed_handle,
                0
            );
            TitanPikaVr_UpdateVramUsed(
                TitanVideoMemory_GetReservedBytes()
            );
            TitanPikaVr_UpdateTextureUsed(
                TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
                TitanVideoGameTexturePool_GetFreeBytes(
                    &g_video.game_texture_pool
                )
            );
        }

        return TITAN_ERROR_FRAME_END;
    }

    g_video.game_texture_upload_pending = 0;
    g_video.game_texture_upload_handle = 0u;
    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_SubmitPacket(
    int finish_frame
)
{
    qword_t *cursor;

    if (
        g_video.draw_packet == 0 ||
        g_video.packet_error
    )
    {
        return TITAN_ERROR_FRAME_END;
    }

    /* A FINISH request is also a fence for packets submitted earlier without
     * FINISH.  Never elide that fence merely because the current packet is
     * empty: texture retirement relies on it before reusing GS blocks. */
    if (packet2_get_qw_count(g_video.draw_packet) == 0u && !finish_frame)
    {
        return TITAN_RESULT_OK;
    }

    {
        unsigned int submitting_qwords =
            packet2_get_qw_count(g_video.draw_packet);
        g_video.current_frame_qwords += submitting_qwords;
        if (g_video.game_mode && submitting_qwords != 0u)
            g_video.game_path3_pending = 1;
    }

    /* Texture DMA is PATH3/GIF too.  The upload was queued before these draw
     * commands were built, but do not submit another GIF chain until its DMA
     * has completed.  This is the single ordering fence for the async game
     * upload pipeline. */
    if (g_video.game_texture_upload_pending &&
        TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
        return TITAN_ERROR_FRAME_END;

    if (finish_frame)
    {
        cursor = g_video.draw_packet->next;
        cursor = draw_finish(cursor);
        packet2_update(g_video.draw_packet, cursor);
    }

    /* The old game path flushed the entire EE data cache for every packet.
     * Only the packet chain itself is DMA input here. Keep the legacy global
     * flush outside game mode until the shell path is audited independently. */
    if (g_video.game_mode)
        SyncDCache(g_video.draw_packet->base, g_video.draw_packet->next);
    else
        FlushCache(0);

    dma_channel_send_packet2(
        g_video.draw_packet,
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
        g_video.packet_error = 1;
        return TITAN_ERROR_FRAME_END;
    }

    if (finish_frame)
    {
        draw_wait_finish();
        if (g_video.game_mode)
            g_video.game_path3_pending = 0;
    }

    packet2_reset(
        g_video.draw_packet,
        0
    );

    return TITAN_RESULT_OK;
}

static int TitanVideo_ReserveQwords(
    unsigned int required_qwords
)
{
    unsigned int current_qwords;

    if (
        !g_video.initialized ||
        g_video.draw_packet == 0 ||
        g_video.packet_error
    )
    {
        return 0;
    }

    if (
        required_qwords +
        TITAN_VIDEO_PACKET_SAFETY_QWORDS >=
        TITAN_VIDEO_PACKET_QWORDS
    )
    {
        g_video.packet_error = 1;
        return 0;
    }

    current_qwords =
        packet2_get_qw_count(
            g_video.draw_packet
        );

    if (
        current_qwords +
        required_qwords +
        TITAN_VIDEO_PACKET_SAFETY_QWORDS >=
        TITAN_VIDEO_PACKET_QWORDS
    )
    {
        if (
            TitanVideo_SubmitPacket(0) !=
            TITAN_RESULT_OK
        )
        {
            return 0;
        }
    }

    return 1;
}

static void TitanVideo_AppendRectPhysical(
    float x0,
    float y0,
    float x1,
    float y1,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    rect_t rect;
    qword_t *cursor;
    float temporary;

    if (alpha == 0u)
    {
        return;
    }

    if (x1 < x0)
    {
        temporary = x0;
        x0 = x1;
        x1 = temporary;
    }

    if (y1 < y0)
    {
        temporary = y0;
        y0 = y1;
        y1 = temporary;
    }

    x0 = TitanVideo_ClampFloat(
        x0,
        0.0f,
        (float)g_video.framebuffer_width
    );

    x1 = TitanVideo_ClampFloat(
        x1,
        0.0f,
        (float)g_video.framebuffer_width
    );

    y0 = TitanVideo_ClampFloat(
        y0,
        0.0f,
        (float)g_video.framebuffer_height
    );

    y1 = TitanVideo_ClampFloat(
        y1,
        0.0f,
        (float)g_video.framebuffer_height
    );

    if (x1 <= x0 || y1 <= y0)
    {
        return;
    }

    if (!TitanVideo_ReserveQwords(3u))
    {
        return;
    }

    rect.v0.x = x0;
    rect.v0.y = y0;
    rect.v0.z = TITAN_VIDEO_Z;
    rect.v1.x = x1;
    rect.v1.y = y1;
    rect.v1.z = TITAN_VIDEO_Z;
    rect.color = TitanVideo_MakeColor(
        red,
        green,
        blue,
        alpha
    );

    cursor = g_video.draw_packet->next;
    cursor = draw_rect_filled(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &rect
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}

static void TitanVideo_AppendTrianglePhysical(
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    triangle_t triangle;
    qword_t *cursor;

    if (alpha == 0u)
    {
        return;
    }

    if (!TitanVideo_ReserveQwords(4u))
    {
        return;
    }

    triangle.v0.x = x0;
    triangle.v0.y = y0;
    triangle.v0.z = TITAN_VIDEO_Z;
    triangle.v1.x = x1;
    triangle.v1.y = y1;
    triangle.v1.z = TITAN_VIDEO_Z;
    triangle.v2.x = x2;
    triangle.v2.y = y2;
    triangle.v2.z = TITAN_VIDEO_Z;
    triangle.color = TitanVideo_MakeColor(
        red,
        green,
        blue,
        alpha
    );

    cursor = g_video.draw_packet->next;
    cursor = draw_triangle_filled(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &triangle
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}

static void TitanVideo_AppendEllipseFanPhysical(
    float center_x,
    float center_y,
    float radius_x,
    float radius_y,
    float start_angle,
    float end_angle,
    unsigned int segments,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    unsigned int index;
    float angle0;
    float angle1;
    float x0;
    float y0;
    float x1;
    float y1;
    float amount0;
    float amount1;

    if (
        radius_x <= 0.0f ||
        radius_y <= 0.0f ||
        segments == 0u ||
        alpha == 0u
    )
    {
        return;
    }

    if (!TitanVideo_ReserveQwords(segments * 4u))
    {
        return;
    }

    for (index = 0u; index < segments; index++)
    {
        amount0 = (float)index / (float)segments;
        amount1 = (float)(index + 1u) / (float)segments;

        angle0 =
            start_angle +
            ((end_angle - start_angle) * amount0);

        angle1 =
            start_angle +
            ((end_angle - start_angle) * amount1);

        x0 = center_x + (cosf(angle0) * radius_x);
        y0 = center_y + (sinf(angle0) * radius_y);
        x1 = center_x + (cosf(angle1) * radius_x);
        y1 = center_y + (sinf(angle1) * radius_y);

        TitanVideo_AppendTrianglePhysical(
            center_x,
            center_y,
            x0,
            y0,
            x1,
            y1,
            red,
            green,
            blue,
            alpha
        );
    }
}

static TitanVideoFramebufferConfig TitanVideo_GetFramebufferConfig(
    TitanVideoOutputMode output
)
{
    TitanVideoFramebufferConfig config;

    config.output = output;
    if (output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        config.width = TITAN_VIDEO_HD_WIDTH;
        config.height = TITAN_VIDEO_HD_HEIGHT;
        config.storage_height = TITAN_VIDEO_HD_STORAGE_HEIGHT;
        config.count = 2u;
        config.psm = GS_PSM_16;
    }
    else
    {
        config.width = TITAN_VIDEO_LEGACY_WIDTH;
        config.height = TITAN_VIDEO_LEGACY_HEIGHT;
        config.storage_height = TITAN_VIDEO_LEGACY_HEIGHT;
        config.count = 2u;
        config.psm = GS_PSM_32;
    }

    return config;
}

static void TitanVideo_SetFramebufferDescriptor(
    framebuffer_t *framebuffer,
    unsigned int address,
    const TitanVideoFramebufferConfig *config
)
{
    framebuffer->address = address;
    framebuffer->width = config->width;
    framebuffer->height = config->height;
    framebuffer->psm = config->psm;
    framebuffer->mask = 0u;
}

static void TitanVideo_SetTextureDescriptor(
    texbuffer_t *texture,
    unsigned int address,
    unsigned int buffer_width,
    int psm,
    unsigned int logical_width,
    unsigned int logical_height
)
{
    texture->address = address;
    texture->width = buffer_width;
    texture->psm = psm;
    texture->info.width = draw_log2(logical_width);
    texture->info.height = draw_log2(logical_height);
    texture->info.components = TEXTURE_COMPONENTS_RGBA;
    texture->info.function = TEXTURE_FUNCTION_MODULATE;
}

static framebuffer_t *TitanVideo_GetActiveDrawFramebuffer(void)
{
    if (g_video.optimized_720p_active)
    {
        return &g_video.composition_framebuffer;
    }
    return &g_video.framebuffers[g_video.drawing_buffer];
}

static int TitanVideo_AllocateResourceLayout(
    const TitanVideoFramebufferConfig *config
)
{
    int wave_texture_address;
    int cached_layer_address;
    int avatar_atlas_address;
    int font_atlas_address;
    int ui_atlas_address;
    int alpha_clut_address;

    wave_texture_address = TitanVideoMemory_Allocate(
        TITAN_WAVE_TEXTURE_ALLOCATION_NAME,
        TITAN_WAVE_TEXTURE_WIDTH,
        TITAN_WAVE_TEXTURE_HEIGHT,
        GS_PSM_32,
        TITAN_VIDEO_MEMORY_ALIGN_BLOCK
    );
    if (wave_texture_address < 0)
    {
        return 0;
    }

    cached_layer_address = -1;
    alpha_clut_address = -1;

    if (config->output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        font_atlas_address = TitanVideoMemory_Allocate(
            "font-atlas-indexed",
            TITAN_VIDEO_HD_FONT_STORAGE_WIDTH,
            TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT,
            GS_PSM_8,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        ui_atlas_address = TitanVideoMemory_Allocate(
            "ui-atlas-indexed",
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT,
            GS_PSM_8,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        alpha_clut_address = TitanVideoMemory_Allocate(
            "alpha-clut",
            TITAN_VIDEO_ALPHA_CLUT_WIDTH,
            TITAN_VIDEO_ALPHA_CLUT_HEIGHT,
            GS_PSM_32,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        avatar_atlas_address = TitanVideoMemory_Allocate(
            "avatar-scratch",
            TITAN_VIDEO_AVATAR_SCRATCH_WIDTH,
            TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT,
            GS_PSM_16,
            TITAN_VIDEO_MEMORY_ALIGN_PAGE
        );

        if (
            font_atlas_address < 0 ||
            ui_atlas_address < 0 ||
            alpha_clut_address < 0 ||
            avatar_atlas_address < 0
        )
        {
            return 0;
        }

        memset(&g_video.cached_layer, 0, sizeof(g_video.cached_layer));
        TitanVideo_SetTextureDescriptor(
            &g_video.avatar_atlas,
            (unsigned int)avatar_atlas_address,
            TITAN_VIDEO_AVATAR_SCRATCH_WIDTH,
            GS_PSM_16,
            TITAN_VIDEO_AVATAR_SCRATCH_WIDTH,
            TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT
        );
        TitanVideo_SetTextureDescriptor(
            &g_video.font_atlas,
            (unsigned int)font_atlas_address,
            TITAN_VIDEO_HD_FONT_STORAGE_WIDTH,
            GS_PSM_8,
            TITAN_VIDEO_FONT_ATLAS_WIDTH,
            TITAN_VIDEO_FONT_ATLAS_HEIGHT
        );
        TitanVideo_SetTextureDescriptor(
            &g_video.ui_atlas,
            (unsigned int)ui_atlas_address,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            GS_PSM_8,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT
        );

        g_video.alpha_clut_address = (unsigned int)alpha_clut_address;
        g_video.compact_resources = 1;
    }
    else
    {
        cached_layer_address = TitanVideoMemory_Allocate(
            "cached-layer",
            TITAN_VIDEO_CACHED_LAYER_WIDTH,
            TITAN_VIDEO_CACHED_LAYER_HEIGHT,
            GS_PSM_16,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        avatar_atlas_address = TitanVideoMemory_Allocate(
            "avatar-atlas",
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            TITAN_VIDEO_AVATAR_ATLAS_HEIGHT,
            GS_PSM_16,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        font_atlas_address = TitanVideoMemory_Allocate(
            "font-atlas",
            TITAN_VIDEO_FONT_ATLAS_WIDTH,
            TITAN_VIDEO_FONT_ATLAS_HEIGHT,
            GS_PSM_32,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        ui_atlas_address = TitanVideoMemory_Allocate(
            "ui-atlas",
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT,
            GS_PSM_32,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );

        if (
            cached_layer_address < 0 ||
            avatar_atlas_address < 0 ||
            font_atlas_address < 0 ||
            ui_atlas_address < 0
        )
        {
            return 0;
        }

        TitanVideo_SetTextureDescriptor(
            &g_video.cached_layer,
            (unsigned int)cached_layer_address,
            TITAN_VIDEO_CACHED_LAYER_WIDTH,
            GS_PSM_16,
            TITAN_VIDEO_CACHED_LAYER_WIDTH,
            TITAN_VIDEO_CACHED_LAYER_HEIGHT
        );
        TitanVideo_SetTextureDescriptor(
            &g_video.avatar_atlas,
            (unsigned int)avatar_atlas_address,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            GS_PSM_16,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            TITAN_VIDEO_AVATAR_ATLAS_HEIGHT
        );
        TitanVideo_SetTextureDescriptor(
            &g_video.font_atlas,
            (unsigned int)font_atlas_address,
            TITAN_VIDEO_FONT_ATLAS_WIDTH,
            GS_PSM_32,
            TITAN_VIDEO_FONT_ATLAS_WIDTH,
            TITAN_VIDEO_FONT_ATLAS_HEIGHT
        );
        TitanVideo_SetTextureDescriptor(
            &g_video.ui_atlas,
            (unsigned int)ui_atlas_address,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            GS_PSM_32,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT
        );

        g_video.alpha_clut_address = 0u;
        g_video.compact_resources = 0;
    }

    if (config->output == TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        TitanVideo_SetTextureDescriptor(
            &g_video.transient_atlas,
            g_video.avatar_atlas.address,
            g_video.avatar_atlas.width,
            GS_PSM_16,
            256u,
            64u
        );
    }
    else
    {
        TitanVideo_SetTextureDescriptor(
            &g_video.transient_atlas,
            g_video.cached_layer.address,
            g_video.cached_layer.width,
            GS_PSM_16,
            256u,
            64u
        );
    }
    g_video.transient_atlas_uploaded_generation = 0u;
    g_video.cached_layer_ready = 0;
    g_video.avatar_atlas_ready = 0;
    g_video.font_atlas_ready = 0;
    g_video.ui_atlas_ready = 0;
    g_video.avatar_cached_chunk = TITAN_VIDEO_AVATAR_CHUNK_INVALID;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return 1;
}

static int TitanVideo_AllocateFramebuffers(
    const TitanVideoFramebufferConfig *config,
    framebuffer_t framebuffers[2]
)
{
    unsigned int index;

    for (index = 0u; index < 2u; index++)
    {
        framebuffers[index].address = 0u;
        framebuffers[index].width = 0u;
        framebuffers[index].height = 0u;
        framebuffers[index].psm = 0;
        framebuffers[index].mask = 0u;
    }

    for (index = 0u; index < config->count; index++)
    {
        int address;

        address = TitanVideoMemory_Allocate(
            index == 0u ? "framebuffer-0" : "framebuffer-1",
            config->width,
            config->storage_height,
            config->psm,
            TITAN_VIDEO_MEMORY_ALIGN_PAGE
        );
        if (address < 0)
        {
            while (index > 0u)
            {
                index--;
                TitanVideoMemory_Free(framebuffers[index].address);
            }
            return 0;
        }

        TitanVideo_SetFramebufferDescriptor(
            &framebuffers[index],
            (unsigned int)address,
            config
        );
    }

    return 1;
}

static int TitanVideo_AllocateOptimized720pSurfaces(
    framebuffer_t *output_framebuffer,
    framebuffer_t *composition_framebuffer
)
{
    TitanVideo720pPlan plan;
    TitanVideoFramebufferConfig output_config;
    TitanVideoFramebufferConfig composition_config;
    int composition_address;
    int output_address;

    if (output_framebuffer == 0 || composition_framebuffer == 0 ||
        !TitanVideo720pPlan_Build(TITAN_VIDEO_720P_PATH_OPTIMIZED, &plan))
    {
        return 0;
    }

    composition_address = TitanVideoMemory_Allocate(
        "composition-960x540",
        plan.internal_storage_width,
        plan.internal_storage_height,
        GS_PSM_16,
        TITAN_VIDEO_MEMORY_ALIGN_PAGE
    );
    if (composition_address < 0)
    {
        return 0;
    }

    output_address = TitanVideoMemory_Allocate(
        "framebuffer-0",
        plan.output_storage_width,
        plan.output_storage_height,
        GS_PSM_16,
        TITAN_VIDEO_MEMORY_ALIGN_PAGE
    );
    if (output_address < 0)
    {
        TitanVideoMemory_Free((unsigned int)composition_address);
        return 0;
    }

    memset(&output_config, 0, sizeof(output_config));
    output_config.output = TITAN_VIDEO_OUTPUT_HDTV_720P;
    output_config.width = plan.output_width;
    output_config.height = plan.output_height;
    output_config.storage_height = plan.output_storage_height;
    output_config.count = TITAN_VIDEO_OPTIMIZED_SURFACE_COUNT;
    output_config.psm = GS_PSM_16;
    TitanVideo_SetFramebufferDescriptor(
        output_framebuffer, (unsigned int)output_address, &output_config
    );

    memset(&composition_config, 0, sizeof(composition_config));
    composition_config.output = TITAN_VIDEO_OUTPUT_HDTV_720P;
    composition_config.width = plan.internal_width;
    composition_config.height = plan.internal_height;
    composition_config.storage_height = plan.internal_storage_height;
    composition_config.count = TITAN_VIDEO_OPTIMIZED_SURFACE_COUNT;
    composition_config.psm = GS_PSM_16;
    TitanVideo_SetFramebufferDescriptor(
        composition_framebuffer, (unsigned int)composition_address,
        &composition_config
    );
    return 1;
}

static void TitanVideo_InstallOptimized720pSurfaces(
    const framebuffer_t *output_framebuffer,
    const framebuffer_t *composition_framebuffer
)
{
    memset(g_video.framebuffers, 0, sizeof(g_video.framebuffers));
    g_video.framebuffers[0] = *output_framebuffer;
    g_video.composition_framebuffer = *composition_framebuffer;
    TitanVideo_SetTextureDescriptor(
        &g_video.composition_texture,
        composition_framebuffer->address,
        composition_framebuffer->width,
        GS_PSM_16,
        composition_framebuffer->width,
        composition_framebuffer->height
    );
    g_video.framebuffer_width = TITAN_VIDEO_HD_WIDTH;
    g_video.framebuffer_height = TITAN_VIDEO_HD_HEIGHT;
    g_video.framebuffer_count = 1u;
    g_video.framebuffer_psm = GS_PSM_16;
    g_video.displayed_buffer = 0u;
    g_video.drawing_buffer = 0u;
    g_video.optimized_720p_active = 1;
    g_video.internal_width = composition_framebuffer->width;
    g_video.internal_height = composition_framebuffer->height;
}

static void TitanVideo_InstallFramebuffers(
    const TitanVideoFramebufferConfig *config,
    const framebuffer_t framebuffers[2]
)
{
    unsigned int index;

    for (index = 0u; index < 2u; index++)
    {
        g_video.framebuffers[index] = framebuffers[index];
    }

    g_video.framebuffer_width = config->width;
    g_video.framebuffer_height = config->height;
    g_video.framebuffer_count = config->count;
    g_video.framebuffer_psm = config->psm;
    g_video.displayed_buffer = 0u;
    g_video.drawing_buffer = config->count > 1u ? 1u : 0u;
    g_video.optimized_720p_active = 0;
    memset(&g_video.composition_framebuffer, 0, sizeof(g_video.composition_framebuffer));
    memset(&g_video.composition_texture, 0, sizeof(g_video.composition_texture));
    g_video.internal_width = config->width;
    g_video.internal_height = config->height;
}

static unsigned int TitanVideo_GetClutStorageIndex(unsigned int index)
{
    return
        (index & ~0x18u) |
        ((index & 0x08u) << 1u) |
        ((index & 0x10u) >> 1u);
}

static TitanResult TitanVideo_UploadAlphaClut(void)
{
    qword_t *cursor;
    unsigned int index;

    if (
        !g_video.compact_resources ||
        g_video.upload_packet == 0
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    for (index = 0u; index < 256u; index++)
    {
        unsigned int alpha;
        unsigned int storage_index;

        alpha = index > 128u ? 128u : index;
        storage_index = TitanVideo_GetClutStorageIndex(index);
        g_video_alpha_clut[storage_index] =
            0x00808080u | (alpha << 24u);
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video_alpha_clut,
        TITAN_VIDEO_ALPHA_CLUT_WIDTH,
        TITAN_VIDEO_ALPHA_CLUT_HEIGHT,
        GS_PSM_32,
        (int)g_video.alpha_clut_address,
        64
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(
        g_video.upload_packet,
        DMA_CHANNEL_GIF,
        1
    );
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_ReuploadResidentResources(void)
{
    TitanResult result;

    if (g_video.compact_resources)
    {
        result = TitanVideo_UploadAlphaClut();
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    if (g_video.font_atlas_source != 0)
    {
        result = TitanVideo_UploadFontAtlas32(
            g_video.font_atlas_source,
            TITAN_VIDEO_FONT_ATLAS_WIDTH,
            TITAN_VIDEO_FONT_ATLAS_HEIGHT,
            g_video.font_atlas_source_stride
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    if (g_video.ui_atlas_source != 0)
    {
        result = TitanVideo_UploadUiAtlas32(
            g_video.ui_atlas_source,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT,
            g_video.ui_atlas_source_stride
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    if (g_video.avatar_atlas_source != 0)
    {
        result = TitanVideo_UploadAvatarAtlas16(
            g_video.avatar_atlas_source,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            TITAN_VIDEO_AVATAR_ATLAS_HEIGHT,
            g_video.avatar_atlas_source_stride
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    if (
        !g_video.compact_resources &&
        g_video.cached_layer_source != 0
    )
    {
        result = TitanVideo_UploadCachedLayer16(
            g_video.cached_layer_source,
            TITAN_VIDEO_CACHED_LAYER_WIDTH,
            TITAN_VIDEO_CACHED_LAYER_HEIGHT,
            g_video.cached_layer_source_stride
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_ClearFramebufferImmediate(
    framebuffer_t *framebuffer
)
{
    qword_t *cursor;

    if (g_video.draw_packet == 0 || framebuffer == 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    packet2_reset(g_video.draw_packet, 0);
    cursor = g_video.draw_packet->next;
    cursor = draw_setup_environment(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        framebuffer,
        &g_video.zbuffer
    );
    cursor = draw_clear(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        0.0f,
        0.0f,
        (float)framebuffer->width,
        (float)framebuffer->height,
        0u,
        0u,
        0u
    );
    cursor = draw_finish(cursor);
    packet2_update(g_video.draw_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(
        g_video.draw_packet,
        DMA_CHANNEL_GIF,
        1
    );
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }
    draw_wait_finish();
    packet2_reset(g_video.draw_packet, 0);
    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_ClearFramebuffersImmediate(void)
{
    unsigned int index;

    for (index = 0u; index < g_video.framebuffer_count; index++)
    {
        TitanResult result;

        result = TitanVideo_ClearFramebufferImmediate(
            &g_video.framebuffers[index]
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    if (g_video.optimized_720p_active)
    {
        TitanResult result = TitanVideo_ClearFramebufferImmediate(
            &g_video.composition_framebuffer
        );
        if (result != TITAN_RESULT_OK)
        {
            return result;
        }
    }

    return TITAN_RESULT_OK;
}

static void TitanVideo_ConfigureOutputMapping(
    TitanVideoOutputMode output
)
{
    float scale_x;
    float scale_y;

    scale_x =
        (float)g_video.internal_width / (float)TITAN_UI_WIDTH;
    scale_y =
        (float)g_video.internal_height / (float)TITAN_UI_HEIGHT;

    /* The shell logical canvas is authored for television/anamorphic mapping.
     * Map both axes to the complete active framebuffer in every output mode.
     * The former 720p uniform-scale path left ~24 physical pixels unused at
     * both top and bottom, producing visible black borders around the shell. */
    (void)output;
    g_video.logical_scale_x = scale_x;
    g_video.logical_scale_y = scale_y;
    g_video.logical_offset_y = 0.0f;
}

static float TitanVideo_MapLogicalFloatX(float logical_x)
{
    float transformed_x;

    transformed_x =
        g_video.transform_origin_x +
        ((logical_x - g_video.transform_origin_x) * g_video.transform_scale) +
        g_video.transform_offset_x;

    return transformed_x * g_video.logical_scale_x;
}

static float TitanVideo_MapLogicalFloatY(float logical_y)
{
    float transformed_y;

    transformed_y =
        g_video.transform_origin_y +
        ((logical_y - g_video.transform_origin_y) * g_video.transform_scale) +
        g_video.transform_offset_y;

    return g_video.logical_offset_y +
        (transformed_y * g_video.logical_scale_y);
}

int TitanVideo_MapLogicalX(
    float logical_x
)
{
    float physical_x;

    physical_x = TitanVideo_MapLogicalFloatX(logical_x);
    return (int)(physical_x + (physical_x >= 0.0f ? 0.5f : -0.5f));
}

int TitanVideo_MapLogicalY(
    float logical_y
)
{
    float physical_y;

    physical_y = TitanVideo_MapLogicalFloatY(logical_y);
    return (int)(physical_y + (physical_y >= 0.0f ? 0.5f : -0.5f));
}

float TitanVideo_GetTransformScale(void)
{
    return g_video.transform_scale;
}

float TitanVideo_GetLogicalScaleX(void)
{
    return g_video.logical_scale_x;
}

float TitanVideo_GetLogicalScaleY(void)
{
    return g_video.logical_scale_y;
}

float TitanVideo_GetLogicalOffsetY(void)
{
    return g_video.logical_offset_y;
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
            g_video.logical_scale_x
        ) +
        0.5f
    );

    if (radius < 1)
    {
        radius = 1;
    }

    return radius;
}

static int TitanVideo_MapLogicalRadiusY(
    float logical_radius
)
{
    int radius;

    radius = (int)(
        (
            logical_radius *
            g_video.transform_scale *
            g_video.logical_scale_y
        ) +
        0.5f
    );

    if (radius < 1)
    {
        radius = 1;
    }

    return radius;
}

static TitanResult TitanVideo_RestoreFramebufferConfig(
    const TitanVideoFramebufferConfig *config
)
{
    framebuffer_t restored[2];
    framebuffer_t composition;
    TitanResult result;
    int optimized;

    optimized = config->output == TITAN_VIDEO_OUTPUT_HDTV_720P &&
        TitanVideo720pPath_Sanitize(g_video.hd_path_preference) ==
        TITAN_VIDEO_720P_PATH_OPTIMIZED;

    if (
        TitanVideoMemory_Initialize() != TITAN_RESULT_OK ||
        !TitanVideo_AllocateResourceLayout(config)
    )
    {
        g_video.initialized = 0;
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    if (optimized)
    {
        if (!TitanVideo_AllocateOptimized720pSurfaces(&restored[0], &composition))
        {
            g_video.initialized = 0;
            return TITAN_ERROR_VIDEO_INITIALIZATION;
        }
        TitanVideo_InstallOptimized720pSurfaces(&restored[0], &composition);
    }
    else
    {
        if (!TitanVideo_AllocateFramebuffers(config, restored))
        {
            g_video.initialized = 0;
            return TITAN_ERROR_VIDEO_INITIALIZATION;
        }
        TitanVideo_InstallFramebuffers(config, restored);
    }

    TitanVideo_ConfigureOutputMapping(config->output);

    result = TitanVideo_ClearFramebuffersImmediate();
    if (result == TITAN_RESULT_OK)
    {
        TitanVideo_SwitchProbe("reupload-enter", 0);
        result = TitanVideo_ReuploadResidentResources();
        TitanVideo_SwitchProbe("reupload-done", (int)result);
    }
    if (result == TITAN_RESULT_OK)
    {
        TitanVideo_SwitchProbe("mode-commit-enter", 0);
        result = TitanVideoMode_Commit(
            config->output,
            g_video.framebuffers[0].address,
            g_video.framebuffer_width,
            g_video.framebuffer_height,
            g_video.framebuffer_count,
            g_video.framebuffer_psm
        );
        TitanVideo_SwitchProbe("mode-commit-done", (int)result);
    }

    if (result != TITAN_RESULT_OK)
    {
        g_video.initialized = 0;
        g_video.packet_error = 1;
    }
    else
    {
        g_video.initialized = 1;
        g_video.packet_error = 0;
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    }

    return result;
}

static TitanResult TitanVideo_SwitchFramebufferConfig(
    TitanVideoOutputMode output
)
{
    TitanVideoFramebufferConfig old_config;
    TitanVideoFramebufferConfig new_config;
    framebuffer_t replacement[2];
    framebuffer_t composition;
    TitanResult result;
    int optimized;

    TitanVideo_SwitchProbe("switch-enter", (int)output);
    old_config = TitanVideo_GetFramebufferConfig(
        TitanVideoMode_GetOutput()
    );
    new_config = TitanVideo_GetFramebufferConfig(output);

    if (old_config.output == new_config.output)
    {
        return TITAN_RESULT_OK;
    }

    if (g_video.frame_open)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    TitanVideo_SwitchProbe("begin-reconfigure-enter", 0);
    result = TitanVideoMode_BeginReconfigure();
    TitanVideo_SwitchProbe("begin-reconfigure-done", (int)result);
    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    optimized = new_config.output == TITAN_VIDEO_OUTPUT_HDTV_720P &&
        TitanVideo720pPath_Sanitize(g_video.hd_path_preference) ==
        TITAN_VIDEO_720P_PATH_OPTIMIZED;

    TitanVideo_SwitchProbe("vram-layout-enter", 0);
    if (
        TitanVideoMemory_Initialize() != TITAN_RESULT_OK ||
        !TitanVideo_AllocateResourceLayout(&new_config)
    )
    {
        TitanVideo_SwitchProbe("vram-layout-fail", -1);
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }
    TitanVideo_SwitchProbe("vram-layout-done", 0);

    if (optimized)
    {
        TitanVideo_SwitchProbe("optimized-surfaces-enter", 0);
        if (!TitanVideo_AllocateOptimized720pSurfaces(&replacement[0], &composition))
        {
            TitanVideo_SwitchProbe("optimized-surfaces-fail", -1);
            (void)TitanVideo_RestoreFramebufferConfig(&old_config);
            return TITAN_ERROR_VIDEO_INITIALIZATION;
        }
        TitanVideo_InstallOptimized720pSurfaces(&replacement[0], &composition);
        TitanVideo_SwitchProbe("optimized-surfaces-done", 0);
    }
    else
    {
        if (!TitanVideo_AllocateFramebuffers(&new_config, replacement))
        {
            (void)TitanVideo_RestoreFramebufferConfig(&old_config);
            return TITAN_ERROR_VIDEO_INITIALIZATION;
        }
        TitanVideo_InstallFramebuffers(&new_config, replacement);
    }

    TitanVideo_ConfigureOutputMapping(new_config.output);
    TitanVideo_SwitchProbe("mapping-done", 0);

    TitanVideo_SwitchProbe("clear-enter", 0);
    result = TitanVideo_ClearFramebuffersImmediate();
    TitanVideo_SwitchProbe("clear-done", (int)result);
    if (result == TITAN_RESULT_OK)
    {
        result = TitanVideo_ReuploadResidentResources();
    }
    if (result == TITAN_RESULT_OK)
    {
        result = TitanVideoMode_Commit(
            new_config.output,
            g_video.framebuffers[0].address,
            g_video.framebuffer_width,
            g_video.framebuffer_height,
            g_video.framebuffer_count,
            g_video.framebuffer_psm
        );
    }

    if (result != TITAN_RESULT_OK)
    {
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return result;
    }

    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    g_video.packet_error = 0;
    /* Video-memory ownership is rebuilt transactionally above.  Refresh both
     * telemetry and recovery policy from the new allocator high-water mark;
     * leaving pikaVR on the legacy value made optimized 720p report 0 bytes
     * free and kept the shell in minimum-recovery mode. */
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanVramBootGuard_ReevaluateAfterVideoModeChange();
    g_video_post_switch_probe_pending = optimized ? 1 : 0;
    if (optimized)
    {
        TitanVideo_SwitchProbe("post-switch-probe-armed", 1);
        TitanVideo_SwitchProbe("output-fb-address", (int)g_video.framebuffers[0].address);
        TitanVideo_SwitchProbe("composition-fb-address", (int)g_video.composition_framebuffer.address);
        TitanVideo_SwitchProbe("vram-reserved", (int)TitanVideoMemory_GetReservedBytes());
    }
    TitanVideo_SwitchProbe("switch-done", 0);
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_Initialize(void)
{
    TitanVideoFramebufferConfig legacy_config;
    framebuffer_t initial_framebuffers[2];

    if (g_video.initialized)
    {
        return TITAN_RESULT_OK;
    }

    /* BF30 HWFix2: the 960x540 composition path is the approved/default
     * 720p shell policy. Physical output remains 1280x720p. REFERENCE is
     * retained only as a DEX A/B fallback and must never be re-promoted by
     * stale validation-era cold-boot policy. */
    g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED;

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

    if (TitanVideoMemory_Initialize() != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    legacy_config = TitanVideo_GetFramebufferConfig(
        TITAN_VIDEO_OUTPUT_LEGACY
    );
    if (
        !TitanVideo_AllocateResourceLayout(&legacy_config) ||
        !TitanVideo_AllocateFramebuffers(
            &legacy_config,
            initial_framebuffers
        )
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }
    TitanVideo_InstallFramebuffers(
        &legacy_config,
        initial_framebuffers
    );

    g_video.zbuffer.enable = 0u;
    g_video.zbuffer.method = 0u;
    g_video.zbuffer.address = 0u;
    g_video.zbuffer.zsm = GS_PSMZ_16;
    g_video.zbuffer.mask = 1u;

    g_video.draw_packet = packet2_create(
        (u16)TITAN_VIDEO_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_NORMAL,
        0
    );
    if (g_video.draw_packet == 0)
    {
        return TITAN_ERROR_PACKET_ALLOCATION;
    }

    g_video.upload_packet = packet2_create(
        (u16)TITAN_VIDEO_UPLOAD_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        0
    );
    if (g_video.upload_packet == 0)
    {
        return TITAN_ERROR_PACKET_ALLOCATION;
    }

    TitanVideo_ConfigureOutputMapping(legacy_config.output);
    if (TitanVideo_ClearFramebuffersImmediate() != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    if (
        TitanVideoMode_Initialize(
            g_video.framebuffers[0].address,
            g_video.framebuffer_width,
            g_video.framebuffer_height,
            g_video.framebuffer_count,
            g_video.framebuffer_psm
        ) != TITAN_RESULT_OK
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_video.initialized = 1;
    g_video.background_ready = 0;
    g_video.frame_open = 0;
    g_video.packet_error = 0;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    g_video.current_frame_qwords = 0u;
    g_video.current_external_qwords = 0u;
    g_video.last_frame_qwords = 0u;
    g_video.game_texture_upload_pending = 0;
    g_video.game_texture_upload_handle = 0u;
    g_video.game_texture_staging_index = 0u;
    g_video.game_vu1_active = 0;
    g_video.game_path3_pending = 0;
    TitanPikaVr_Initialize(TITAN_VIDEO_MEMORY_TOTAL_BYTES);
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());

    TitanVideo_ResetTransform();
    draw_enable_blending();

    TitanVideo_BuildBackgroundGradient(
        0,
        0,
        0,
        0,
        0,
        0
    );

    if (TitanVideo_BeginFrame() != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    TitanVideo_RestoreBackground();

    if (TitanVideo_EndFrame() != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_BeginGameMode(unsigned int width, unsigned int height)
{
    TitanVideoFramebufferConfig old_config;
    TitanVideoFramebufferConfig game_config;
    framebuffer_t replacement[2];
    TitanResult result;
    int z_address;
    int texture_arena_address;

    if (!g_video.initialized || g_video.game_mode || g_video.frame_open ||
        width != TITAN_VIDEO_LEGACY_WIDTH || height != TITAN_VIDEO_LEGACY_HEIGHT)
        return TITAN_ERROR_VIDEO_INITIALIZATION;

    g_video.game_restore_output = TitanVideoMode_GetOutput();
    old_config = TitanVideo_GetFramebufferConfig(g_video.game_restore_output);
    game_config = TitanVideo_GetFramebufferConfig(TITAN_VIDEO_OUTPUT_LEGACY);

    result = TitanVideoMode_BeginReconfigure();
    if (result != TITAN_RESULT_OK) return result;

    if (TitanVideoMemory_Initialize() != TITAN_RESULT_OK ||
        !TitanVideo_AllocateFramebuffers(&game_config, replacement))
    {
        g_video.zbuffer.enable = 0u; g_video.zbuffer.mask = 1u;
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    z_address = TitanVideoMemory_Allocate(
        "game-zbuffer", width, height, GS_PSMZ_16, TITAN_VIDEO_MEMORY_ALIGN_PAGE
    );
    if (z_address < 0)
    {
        g_video.zbuffer.enable = 0u; g_video.zbuffer.mask = 1u;
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_video.zbuffer.enable = 1u;
    /* P10.42: the frame clear writes Z=0 before GoldenEye material state is
     * installed.  The environment used for that clear MUST use ZTST=ALWAYS.
     *
     * Using GEQUAL here is self-defeating: stale/non-zero depth from the prior
     * frame (or uninitialized VRAM on the first frame) rejects the Z=0 clear,
     * leaving a patchwork depth surface.  GoldenEye's later material packets
     * still program their requested GEQUAL/GREATER/ALWAYS TEST_1 state via
     * TitanVideo_GameAppendMaterialState(), so changing the environment clear
     * to ALWAYS does not weaken normal game depth testing. */
    g_video.zbuffer.method = 1u;
    g_video.zbuffer.address = (unsigned int)z_address;
    g_video.zbuffer.zsm = GS_PSMZ_16;
    g_video.zbuffer.mask = 0u;
    g_video.game_zbuffer_address = (unsigned int)z_address;
    texture_arena_address = TitanVideoMemory_Allocate(
        "game-texture-arena",
        1024u,
        512u,
        GS_PSM_16,
        TITAN_VIDEO_MEMORY_ALIGN_BLOCK
    );
    if (texture_arena_address < 0)
    {
        g_video.zbuffer.enable = 0u;
        g_video.zbuffer.address = 0u;
        g_video.zbuffer.mask = 1u;
        g_video.game_zbuffer_address = 0u;
        g_video.game_texture_arena_address = 0u;
        TitanVideoGameTexturePool_Initialize(&g_video.game_texture_pool);
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }
    g_video.game_texture_arena_address =
        (unsigned int)texture_arena_address;
    g_video.bound_game_texture_handle = 0u;
    TitanVideoGameTexturePool_Initialize(&g_video.game_texture_pool);
    TitanPikaVr_Initialize(TITAN_VIDEO_MEMORY_TOTAL_BYTES);
    TitanPikaVr_SetTextureBudget(TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES);
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanPikaVr_UpdateTextureUsed(0u);

    TitanVideo_InstallFramebuffers(&game_config, replacement);
    TitanVideo_ConfigureOutputMapping(TITAN_VIDEO_OUTPUT_LEGACY);
    g_video.cached_layer_ready = 0;
    g_video.avatar_atlas_ready = 0;
    g_video.font_atlas_ready = 0;
    g_video.ui_atlas_ready = 0;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;

    result = TitanVideo_ClearFramebuffersImmediate();
    if (result == TITAN_RESULT_OK)
        result = TitanVideoMode_Commit(
            TITAN_VIDEO_OUTPUT_LEGACY, g_video.framebuffers[0].address,
            game_config.width, game_config.height, game_config.count, game_config.psm
        );
    if (result != TITAN_RESULT_OK)
    {
        g_video.zbuffer.enable = 0u; g_video.zbuffer.address = 0u; g_video.zbuffer.mask = 1u;
        g_video.game_zbuffer_address = 0u;
        g_video.game_texture_arena_address = 0u;
        TitanVideoGameTexturePool_Initialize(&g_video.game_texture_pool);
        (void)TitanVideo_RestoreFramebufferConfig(&old_config);
        return result;
    }

    g_video.game_mode = 1;
    memset(&g_video.game_render_state, 0, sizeof(g_video.game_render_state));
    g_video.game_render_state.struct_size = sizeof(g_video.game_render_state);
    g_video.game_render_state.viewport_width = width;
    g_video.game_render_state.viewport_height = height;
    g_video.game_render_state.scissor_width = width;
    g_video.game_render_state.scissor_height = height;
    g_video.game_render_state.depth_test = 1u;
    g_video.game_render_state.depth_write = 1u;
    g_video.game_render_state.depth_compare = TITAN_GAME_DEPTH_GEQUAL;
    g_video.game_render_state.combine_mode = TITAN_GAME_COMBINE_TEX0_MODULATE;
    g_video.game_render_state.sampler[0].linear_filter = 1u;
    g_video.game_render_state.sampler[1].linear_filter = 1u;
    g_video.game_material_state_dirty = 1;
    g_video.game_material_texture_handle = 0xffffffffu;
    g_video.game_material_second_pass = 0xffffffffu;
    g_video.game_texture_upload_pending = 0;
    g_video.game_texture_upload_handle = 0u;
    g_video.game_texture_staging_index = 0u;
    g_video.game_path3_pending = 0;
    g_video.game_vu1_active = TitanGameVu1_Initialize();
    printf("P162_NATIVE_VU1_INIT=%s\n",
        g_video.game_vu1_active ? "PASS" : "FALLBACK_EE");
    TitanVideo_ResetTransform();
    TitanVideo_GameDiagnosticTextureInit();
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_EndGameMode(void)
{
    TitanVideoFramebufferConfig restore_config;
    TitanResult result;
    if (!g_video.initialized) return TITAN_ERROR_VIDEO_INITIALIZATION;
    if (!g_video.game_mode) return TITAN_RESULT_OK;
    if (g_video.frame_open) return TITAN_ERROR_VIDEO_INITIALIZATION;
    if (TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
        return TITAN_ERROR_FRAME_END;

    /* A faulted PATH1 owner cannot be forgotten merely because game mode is
     * ending.  Reset succeeds only after VIF1 DMAC, VIF execution/FIFO and
     * GIF/PATH1 are proven idle; until then the game retains ownership and the
     * shell waves are not allowed to touch VIF1. */
    if (!TitanGameVu1_Reset())
        return TITAN_ERROR_FRAME_END;
    g_video.game_vu1_active = 0;
    g_video.game_path3_pending = 0;

    result = TitanVideoMode_BeginReconfigure();
    if (result != TITAN_RESULT_OK) return result;
    restore_config = TitanVideo_GetFramebufferConfig(g_video.game_restore_output);
    g_video.zbuffer.enable = 0u;
    g_video.zbuffer.method = 0u;
    g_video.zbuffer.address = 0u;
    g_video.zbuffer.mask = 1u;
    g_video.game_zbuffer_address = 0u;
    result = TitanVideo_RestoreFramebufferConfig(&restore_config);
    if (result == TITAN_RESULT_OK)
    {
        g_video.game_mode = 0;
        g_video.game_texture_arena_address = 0u;
        g_video.bound_game_texture_handle = 0u;
        TitanVideoGameTexturePool_Initialize(&g_video.game_texture_pool);
        TitanVideo_GameDiagnosticTextureReset();
        TitanPikaVr_ResetTextures();
        TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
        TitanPikaVr_UpdateTextureUsed(0u);
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
        TitanVideo_ResetTransform();
        /* Game mode intentionally rebuilds GS VRAM without shell assets.
         * Rebind the existing VU1 program/packets to the freshly allocated
         * shell wave texture instead of leaking a second packet set. */
        (void)TitanWaveVu1_RebindVideoResources();
    }
    return result;
}

int TitanVideo_IsGameMode(void)
{
    return g_video.game_mode;
}

unsigned int TitanVideo_GetGameZBufferAddress(void)
{
    return g_video.game_mode ? g_video.game_zbuffer_address : 0u;
}

unsigned int TitanVideo_GetGameTextureBytesFree(void)
{
    return g_video.game_mode
        ? TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool)
        : 0u;
}

static int TitanVideo_GameAppendMaterialState(
    const TitanVideoGameTexture *texture,
    int second_pass
);

void TitanVideo_DrawGameTrianglePhysical(
    float x0, float y0, unsigned int z0,
    float x1, float y1, unsigned int z1,
    float x2, float y2, unsigned int z2,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    triangle_t triangle;
    qword_t *cursor;
    if (!g_video.game_mode || !g_video.frame_open || alpha == 0u) return;
    /* Basic indexed/clip draws share the same scissor, depth, alpha and blend
     * state as the material lane.  This is required when the first primitive
     * of a split-screen viewport is not a material triangle. */
    if (!TitanVideo_GameAppendMaterialState(0, 0)) return;
    if (!TitanVideo_ReserveQwords(4u)) return;
    triangle.v0.x=x0; triangle.v0.y=y0; triangle.v0.z=z0;
    triangle.v1.x=x1; triangle.v1.y=y1; triangle.v1.z=z1;
    triangle.v2.x=x2; triangle.v2.y=y2; triangle.v2.z=z2;
    triangle.color=TitanVideo_MakeColor(red,green,blue,alpha);
    cursor=g_video.draw_packet->next;
    cursor=draw_triangle_filled(cursor,TITAN_VIDEO_DRAW_CONTEXT,&triangle);
    packet2_update(g_video.draw_packet,cursor);
    TitanPikaVr_RecordDraw(1u);
}

static void TitanVideo_GameSetInt4(
    qword_t *destination,
    unsigned int x,
    unsigned int y,
    unsigned int z,
    unsigned int w
)
{
    u32 *values = (u32 *)destination;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
}

static unsigned int TitanVideo_GameUvFixed(float coordinate, unsigned int size)
{
    float texel;
    coordinate = TitanVideo_ClampFloat(coordinate, 0.0f, 1.0f);
    texel = 0.5f + coordinate * (float)(size > 0u ? size - 1u : 0u);
    return (unsigned int)(texel * 16.0f + 0.5f);
}

static unsigned int TitanVideo_GameXyzFixed(float coordinate)
{
    return TitanVideoGame_XyFixed(coordinate);
}

static int TitanVideo_GameTexturePsm(const TitanVideoGameTexture *texture)
{
    if (texture == 0) return GS_PSM_16;
    if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32)
        return GS_PSM_32;
    if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        return GS_PSM_8;
    return GS_PSM_16;
}

int TitanVideo_DrawGameTexturedTrianglePhysical(
    float x0, float y0, unsigned int z0, float u0, float v0,
    float x1, float y1, unsigned int z1, float u1, float v1,
    float x2, float y2, unsigned int z2, float u2, float v2,
    unsigned char r0, unsigned char g0, unsigned char b0, unsigned char a0,
    unsigned char r1, unsigned char g1, unsigned char b1, unsigned char a1,
    unsigned char r2, unsigned char g2, unsigned char b2, unsigned char a2,
    uint32_t texture_handle
)
{
    TitanVideoGameTexture *game_texture;
    qword_t *cursor;
    u64 *tag;

    if (!g_video.game_mode || !g_video.frame_open) return 0;
    game_texture = TitanVideoGameTexturePool_Lookup(
        &g_video.game_texture_pool,
        texture_handle
    );
    if (game_texture == 0 || game_texture->width == 0u ||
        game_texture->height == 0u)
        return 0;
    if (!TitanVideo_GameAppendMaterialState(game_texture, 0)) return 0;
    if (!TitanVideo_ReserveQwords(32u)) return 0;

    cursor = g_video.draw_packet->next;
    tag = (u64 *)cursor;
    tag[0] = GIF_SET_TAG(
        3u,
        1u,
        1u,
        GS_SET_PRIM(3u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u),
        GIF_FLG_PACKED,
        3u
    );
    tag[1] = (u64)GIF_REG_UV |
        ((u64)GIF_REG_RGBAQ << 4u) |
        ((u64)GIF_REG_XYZ2 << 8u);
    cursor++;

#define TITAN_VIDEO_GAME_VERTEX(X,Y,Z,U,V,R,G,B,A) \
    TitanVideo_GameSetInt4(cursor++, \
        TitanVideo_GameUvFixed((U), game_texture->width), \
        TitanVideo_GameUvFixed((V), game_texture->height), 0u, 0u); \
    TitanVideo_GameSetInt4(cursor++, (R), (G), (B), TitanVideo_ToGsAlpha((A))); \
    TitanVideo_GameSetInt4(cursor++, \
        TitanVideo_GameXyzFixed((X)), TitanVideo_GameXyzFixed((Y)), (Z), 0u)

    TITAN_VIDEO_GAME_VERTEX(x0,y0,z0,u0,v0,r0,g0,b0,a0);
    TITAN_VIDEO_GAME_VERTEX(x1,y1,z1,u1,v1,r1,g1,b1,a1);
    TITAN_VIDEO_GAME_VERTEX(x2,y2,z2,u2,v2,r2,g2,b2,a2);
#undef TITAN_VIDEO_GAME_VERTEX

    packet2_update(g_video.draw_packet, cursor);
    TitanPikaVr_RecordDraw(1u);
    return 1;
}

static qword_t *TitanVideo_GameSetAd(
    qword_t *destination,
    u64 value,
    u64 register_address
)
{
    u64 *words = (u64 *)destination;
    words[0] = value;
    words[1] = register_address;
    return destination + 1;
}

static void TitanVideo_GameSetFloat3(
    qword_t *destination,
    float x,
    float y,
    float z
)
{
    TitanVideoGamePackedQword packed = TitanVideoGame_PackSt(x, y, z);
    float *values = (float *)destination;
    u32 *lanes = (u32 *)values;

    lanes[0] = packed.lane[0];
    lanes[1] = packed.lane[1];
    lanes[2] = packed.lane[2];
    lanes[3] = packed.lane[3];
}

static void TitanVideo_GameSetPackedRgba(
    qword_t *destination,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    TitanVideoGamePackedQword packed = TitanVideoGame_PackRgba(
        red,
        green,
        blue,
        alpha
    );
    u32 *values = (u32 *)destination;

    /* In GIF packed mode Q belongs to the preceding ST payload.  RGBAQ is
     * expanded into four 32-bit colour lanes, unlike the 64-bit reglist form. */
    values[0] = packed.lane[0];
    values[1] = packed.lane[1];
    values[2] = packed.lane[2];
    values[3] = packed.lane[3];
}

static unsigned int TitanVideo_GameWrapMode(unsigned char mode)
{
    if (mode == TITAN_GAME_WRAP_CLAMP) return WRAP_CLAMP;
    /* GS region-repeat needs mask/fix fields that are not part of the stable
     * public sampler ABI.  Mirrored N64 tiles are normalized by the Fast3D
     * bridge, so the hardware side deliberately remains ordinary repeat. */
    return WRAP_REPEAT;
}

int TitanVideo_GameSetRenderState(const TitanGameRenderState *state)
{
    TitanGameRenderState validated;
    if (!g_video.game_mode ||
        !TitanVideoGame_NormalizeRenderState(
            state,
            g_video.framebuffer_width,
            g_video.framebuffer_height,
            &validated))
        return 0;
    g_video.game_render_state = validated;
    g_video.game_material_state_dirty = 1;
    return 1;
}

static int TitanVideo_GameAppendMaterialState(
    const TitanVideoGameTexture *texture,
    int second_pass
)
{
    const TitanGameRenderState *state = &g_video.game_render_state;
    qword_t *cursor;
    u64 *tag;
    unsigned int z_test;
    unsigned int alpha_test;
    unsigned int blend_mode;
    unsigned int x0 = (unsigned int)state->scissor_x;
    unsigned int y0 = (unsigned int)state->scissor_y;
    unsigned int x1 = x0 + state->scissor_width - 1u;
    unsigned int y1 = y0 + state->scissor_height - 1u;
    unsigned int sampler_index = second_pass ? 1u : 0u;
    unsigned int texture_handle = texture != 0 ? texture->handle : 0u;
    texbuffer_t buffer;
    clutbuffer_t clut;
    lod_t lod;

    if (!g_video.game_material_state_dirty &&
        g_video.game_material_texture_handle == texture_handle &&
        g_video.game_material_second_pass == (unsigned int)second_pass)
        return 1;

    if (!TitanVideo_ReserveQwords(18u)) return 0;
    z_test = state->depth_test
        ? (state->depth_compare == TITAN_GAME_DEPTH_GREATER ? 3u :
           state->depth_compare == TITAN_GAME_DEPTH_GEQUAL ? 2u : 1u)
        : 1u;
    alpha_test = state->alpha_compare ? 5u : 1u;
    (void)z_test;
    (void)alpha_test;
    (void)x1;
    (void)y1;

    cursor = g_video.draw_packet->next;
    tag = (u64 *)cursor;
    tag[0] = GIF_SET_TAG(5u, 0u, 0u, 0u, GIF_FLG_PACKED, 1u);
    tag[1] = GIF_REG_AD;
    cursor++;
    cursor = TitanVideo_GameSetAd(
        cursor,
        GS_SET_SCISSOR(x0, x1, y0, y1),
        GS_REG_SCISSOR_1
    );
    cursor = TitanVideo_GameSetAd(
        cursor,
        GS_SET_TEST(
            state->alpha_compare ? 1u : 0u,
            alpha_test,
            TitanVideo_ToGsAlpha(state->alpha_threshold),
            0u,
            0u,
            0u,
            state->depth_test ? 1u : 0u,
            z_test
        ),
        GS_REG_TEST_1
    );
    cursor = TitanVideo_GameSetAd(
        cursor,
        GS_SET_ZBUF(
            g_video.game_zbuffer_address >> 11u,
            GS_PSMZ_16,
            (!state->depth_write || second_pass) ? 1u : 0u
        ),
        GS_REG_ZBUF_1
    );
    if (second_pass || state->blend_mode == TITAN_GAME_BLEND_ALPHA)
        blend_mode = GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u);
    else if (state->blend_mode == TITAN_GAME_BLEND_ADDITIVE)
        blend_mode = GS_SET_ALPHA(0u, 2u, 0u, 1u, 0u);
    else if (state->blend_mode == TITAN_GAME_BLEND_MODULATE)
        blend_mode = GS_SET_ALPHA(0u, 2u, 1u, 2u, 0u);
    else
        blend_mode = GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u);
    cursor = TitanVideo_GameSetAd(cursor, blend_mode, GS_REG_ALPHA_1);
    cursor = TitanVideo_GameSetAd(
        cursor,
        GS_SET_CLAMP(
            TitanVideo_GameWrapMode(state->sampler[sampler_index].wrap_s),
            TitanVideo_GameWrapMode(state->sampler[sampler_index].wrap_t),
            0u, texture != 0 && texture->width != 0u ? texture->width - 1u : 0u,
            0u, texture != 0 && texture->height != 0u ? texture->height - 1u : 0u
        ),
        GS_REG_CLAMP_1
    );

    if (texture != 0)
    {
        memset(&buffer, 0, sizeof(buffer));
        memset(&clut, 0, sizeof(clut));
        memset(&lod, 0, sizeof(lod));
        buffer.address = g_video.game_texture_arena_address +
            texture->offset_bytes / 4u;
        buffer.width = texture->buffer_width;
        buffer.psm = TitanVideo_GameTexturePsm(texture);
        buffer.info.width = draw_log2(texture->width);
        buffer.info.height = draw_log2(texture->height);
        buffer.info.components = TEXTURE_COMPONENTS_RGBA;
        buffer.info.function = state->combine_mode == TITAN_GAME_COMBINE_TEX0_DECAL
            ? TEXTURE_FUNCTION_DECAL : TEXTURE_FUNCTION_MODULATE;
        if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        {
            clut.address = g_video.game_texture_arena_address +
                texture->clut_offset_bytes / 4u + texture->offset_bytes / 4u;
            clut.psm = GS_PSM_32;
            clut.storage_mode = CLUT_STORAGE_MODE1;
            clut.start = 0u;
            clut.load_method = CLUT_LOAD;
        }
        else
        {
            clut.storage_mode = CLUT_STORAGE_MODE1;
            clut.load_method = CLUT_NO_LOAD;
        }
        (void)TitanPikaVr_RecordSamplerState(
            texture->handle,
            sampler_index,
            &state->sampler[sampler_index]
        );
        lod.calculation = LOD_FORMULAIC;
        lod.max_level = 0u;
        lod.mag_filter = state->sampler[sampler_index].linear_filter
            ? LOD_MAG_LINEAR : LOD_MAG_NEAREST;
        lod.min_filter = state->sampler[sampler_index].linear_filter
            ? LOD_MIN_LINEAR : LOD_MIN_NEAREST;
        lod.mipmap_select = LOD_MIPMAP_REGISTER;
        cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
        cursor = draw_texturebuffer(
            cursor, TITAN_VIDEO_DRAW_CONTEXT, &buffer, &clut
        );
    }
    packet2_update(g_video.draw_packet, cursor);
    g_video.game_material_state_dirty = 0;
    g_video.game_material_texture_handle = texture_handle;
    g_video.game_material_second_pass = (unsigned int)second_pass;
    TitanPikaVr_RecordMaterialPass();
    return 1;
}

static void TitanVideo_GameApplyFog(
    unsigned char *red,
    unsigned char *green,
    unsigned char *blue,
    unsigned char fog
)
{
    const TitanGameRenderState *state = &g_video.game_render_state;
    unsigned int amount;
    unsigned int inverse;
    if (!state->fog_enabled || fog == 0u) return;
    amount = fog;
    inverse = 255u - amount;
    *red = (unsigned char)((*red * inverse +
        ((state->fog_rgba >> 24u) & 0xffu) * amount + 127u) / 255u);
    *green = (unsigned char)((*green * inverse +
        ((state->fog_rgba >> 16u) & 0xffu) * amount + 127u) / 255u);
    *blue = (unsigned char)((*blue * inverse +
        ((state->fog_rgba >> 8u) & 0xffu) * amount + 127u) / 255u);
}


#define TITAN_VIDEO_GAME_MATERIAL_BATCH_VERTICES 96u

typedef struct TitanVideoGameMaterialPhysicalVertex
{
    float x;
    float y;
    unsigned int z;
    float s0;
    float t0;
    float q0;
    float s1;
    float t1;
    float q1;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    unsigned char fog;
    unsigned char lod;
} TitanVideoGameMaterialPhysicalVertex;

/* EE-side scratch is static rather than stack-resident: 96 mapped vertices are
 * deliberately matched to TitanEye's P147 32-triangle producer batch. */
static TitanVideoGameMaterialPhysicalVertex
    g_video_game_material_batch[TITAN_VIDEO_GAME_MATERIAL_BATCH_VERTICES];

static int TitanVideo_GameAppendMaterialBatchPass(
    const TitanVideoGameTexture *texture,
    const TitanVideoGameMaterialPhysicalVertex *vertices,
    unsigned int vertex_count,
    int textured,
    int second_pass
)
{
    qword_t *cursor;
    u64 *tag;
    unsigned int index;
    unsigned int abe = second_pass ||
        g_video.game_render_state.blend_mode != TITAN_GAME_BLEND_OPAQUE;
    unsigned int registers_per_vertex = textured ? 3u : 2u;

    /* Host GS_SET_PRIM stubs intentionally collapse register packing and can
     * therefore hide this argument from -Wunused-variable. Target GS macros
     * still consume abe normally. */
    (void)abe;
    unsigned int required_qwords = 1u + vertex_count * registers_per_vertex;

    if (vertices == 0 || vertex_count == 0u ||
        vertex_count > TITAN_VIDEO_GAME_MATERIAL_BATCH_VERTICES ||
        vertex_count % 3u != 0u)
        return 0;

    /* Reserve enough room BEFORE material state.  Otherwise a large batch
     * could flush after TEST/ZBUF/TEX state was emitted and place the actual
     * primitive in a fresh packet without that state. */
    if (!TitanVideo_ReserveQwords(required_qwords + 32u))
        return 0;
    if (!TitanVideo_GameAppendMaterialState(texture, second_pass))
        return 0;

    cursor = g_video.draw_packet->next;
    tag = (u64 *)cursor;
    tag[0] = GIF_SET_TAG(
        vertex_count,
        1u,
        1u,
        GS_SET_PRIM(
            3u, 1u, textured ? 1u : 0u, 0u, abe,
            0u, 0u, 0u, 0u
        ),
        GIF_FLG_PACKED,
        registers_per_vertex
    );
    tag[1] = textured
        ? ((u64)GIF_REG_ST |
           ((u64)GIF_REG_RGBAQ << 4u) |
           ((u64)GIF_REG_XYZ2 << 8u))
        : ((u64)GIF_REG_RGBAQ |
           ((u64)GIF_REG_XYZ2 << 4u));
    cursor++;

    for (index = 0u; index < vertex_count; index++)
    {
        const TitanVideoGameMaterialPhysicalVertex *vertex =
            &vertices[index];
        unsigned char red = vertex->red;
        unsigned char green = vertex->green;
        unsigned char blue = vertex->blue;
        unsigned char alpha = vertex->alpha;
        float s = vertex->s0;
        float t = vertex->t0;
        float q = vertex->q0;

        if (second_pass)
        {
            red = 255u;
            green = 255u;
            blue = 255u;
            alpha = vertex->lod;
            s = vertex->s1;
            t = vertex->t1;
            q = vertex->q1;
        }
        else
        {
            TitanVideo_GameApplyFog(
                &red,
                &green,
                &blue,
                vertex->fog
            );
        }

        if (textured)
        {
            s = TitanVideoGame_PrepareSt(
                s,
                texture != 0 ? texture->width : 1u,
                q
            );
            t = TitanVideoGame_PrepareSt(
                t,
                texture != 0 ? texture->height : 1u,
                q
            );
            TitanVideo_GameSetFloat3(cursor++, s, t, q);
        }

        TitanVideo_GameSetPackedRgba(
            cursor++,
            red,
            green,
            blue,
            alpha
        );
        TitanVideo_GameSetInt4(
            cursor++,
            TitanVideo_GameXyzFixed(vertex->x),
            TitanVideo_GameXyzFixed(vertex->y),
            vertex->z,
            0u
        );
    }

    packet2_update(g_video.draw_packet, cursor);
    TitanPikaVr_RecordDraw(vertex_count / 3u);
    return 1;
}

/* P162: PATH1 has higher GIF arbitration priority than PATH3, so it must
 * not overtake a queued texture upload or conventional game primitive. A
 * FINISH is required only at an actual PATH3 -> PATH1 ownership transition;
 * consecutive VU1 material batches remain free of per-draw GS stalls. */
static int TitanVideo_GameNativeQCompatible(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    int needs_second_pass
)
{
    unsigned int index;

    if (vertices == 0 || vertex_count == 0u)
        return 0;

    /* Preserve the public material ABI exactly.  ResolveQ historically honors
     * an explicit positive Q supplied by a caller and derives 1/W only when Q
     * is absent.  TitanEye intentionally supplies zero Q so VU1 can own the
     * reciprocal; another Titan game may not.  Keep those explicit-Q batches
     * on the proven EE path rather than silently changing interpolation. */
    for (index = 0u; index < vertex_count; index++)
    {
        if (isfinite(vertices[index].q0) && vertices[index].q0 > 0.000001f)
            return 0;
        if (needs_second_pass && isfinite(vertices[index].q1) &&
            vertices[index].q1 > 0.000001f)
            return 0;
    }
    return 1;
}

static TitanResult TitanVideo_GamePrepareNativePath1(void)
{
    if (!g_video.game_mode || !g_video.frame_open)
        return TITAN_ERROR_FRAME_END;

    if (g_video.game_texture_upload_pending ||
        g_video.game_path3_pending ||
        (g_video.draw_packet != 0 &&
         packet2_get_qw_count(g_video.draw_packet) != 0u))
    {
        return TitanVideo_FlushForPath1();
    }
    return TITAN_RESULT_OK;
}

/* P10.57: end-to-end Titan Game material batching.
 *
 * The TitanEye backend already groups up to 32 triangles, but the game-runtime
 * bridge used to remap and submit each triangle independently.  That turned
 * one producer batch back into 32 kernel calls, 32 GIF tags and 32 packet
 * updates.  Map the whole batch here and emit one packed GS primitive stream
 * per material pass. */
int TitanVideo_DrawGameMaterialTrianglesBatch(
    const TitanGameMaterialVertex *vertices,
    uint32_t vertex_count,
    uint32_t texture0_handle,
    uint32_t texture1_handle
)
{
    const TitanGameRenderState *state = &g_video.game_render_state;
    TitanVideoGameTexture *texture0 = 0;
    TitanVideoGameTexture *texture1 = 0;
    uint32_t base;
    unsigned int batch_vertices = 0u;
    int drawn = 0;
    int textured;

    if (!g_video.game_mode || !g_video.frame_open || vertices == 0 ||
        vertex_count == 0u || vertex_count % 3u != 0u)
        return -1;

    if (texture0_handle != 0u)
    {
        texture0 = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool,
            texture0_handle
        );
        if (texture0 == 0)
            return -2;
    }
    if (texture1_handle != 0u)
    {
        texture1 = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool,
            texture1_handle
        );
        if (texture1 == 0)
            return -2;
    }

    textured =
        state->combine_mode != TITAN_GAME_COMBINE_SHADE &&
        texture0 != 0;

    /* P162 PS2-native path. Fast3D still owns N64 GBI semantics on EE, but
     * perspective divide, viewport/depth mapping, STQ and packed GS vertex
     * generation are VU1 work. Material GS state travels in the same PATH1
     * packet, avoiding a PATH3 FINISH for every producer batch. */
    /* P162: keep public two-pass LOD atomic.  A VIF1 failure after pass 0
     * cannot be safely replayed through PATH3 without risking a partial/
     * duplicated blend pass. TitanEye's production backend uses TEX0_MODULATE,
     * so this conservative exclusion costs it nothing while preserving every
     * other Titan game's two-pass semantics on the proven EE path. */
    if (state->combine_mode != TITAN_GAME_COMBINE_TEX0_TEX1_LOD &&
        g_video.game_vu1_active && TitanGameVu1_IsAvailable() &&
        TitanVideo_GameNativeQCompatible(vertices, vertex_count, 0))
    {
        int native_drawn;

        if (TitanVideo_GamePrepareNativePath1() != TITAN_RESULT_OK)
            return -2;

        native_drawn = TitanGameVu1_DrawMaterialPass(
            vertices,
            vertex_count,
            state,
            textured ? texture0 : 0,
            g_video.game_texture_arena_address,
            g_video.game_zbuffer_address,
            0);
        if (native_drawn >= 0)
        {
            /* Commit sampler tracking only after PATH1 accepted the batch.
             * NOT_ELIGIBLE issued no native state, so recording before this
             * point would turn the later EE fallback into a false cache hit. */
            if (textured && texture0 != 0)
            {
                (void)TitanPikaVr_RecordSamplerState(
                    texture0->handle, 0u, &state->sampler[0]);
            }
            if (native_drawn != 0)
            {
                TitanPikaVr_RecordDraw((unsigned int)native_drawn);
                TitanPikaVr_RecordMaterialPass();
                TitanVideo_AccountExternalQwords(
                    9u + (unsigned int)native_drawn * 9u);
            }


            /* A later PATH3 primitive must reinstall its material state because
             * PATH1 changed TEST/ZBUF/ALPHA/CLAMP/TEX registers directly. */
            g_video.game_material_state_dirty = 1;
            return native_drawn;
        }

        g_video.game_vu1_active = TitanGameVu1_IsAvailable();
        if (native_drawn == TITAN_GAME_VU1_RESULT_SUBMIT_FAILED)
        {
            /* VIF1 may have consumed a prefix before timing out.  The fault is
             * frame-fatal, not draw-local: any later PATH3 packet could race a
             * still-active PATH1/XGKICK.  Latch packet_error so EndFrame aborts
             * without submitting more GIF work; the VU1 fault itself remains
             * latched across BeginFrame until EndGameMode proves safe release. */
            g_video.game_material_state_dirty = 1;
            g_video.packet_error = 1;
            return -2;
        }
        /* NOT_ELIGIBLE guarantees no PATH1 work was issued, so replaying the
         * complete batch through the proven EE/GIF path is safe. */
    }

    for (base = 0u; base < vertex_count; base += 3u)
    {
        TitanVideoGameScreenVertex screen[3];
        unsigned int index;
        int visible = 1;

        for (index = 0u; index < 3u; index++)
        {
            const TitanGameMaterialVertex *source =
                &vertices[base + index];

            if (!TitanVideoGame_MapClipVertex(
                    state,
                    source->x,
                    source->y,
                    source->z,
                    source->w,
                    &screen[index]))
            {
                visible = 0;
                break;
            }
        }

        if (!visible)
            continue;

        /* The producer currently sends <=96 vertices, but keep this robust if
         * another Titan game uses a larger public API batch later. */
        if (batch_vertices + 3u >
            TITAN_VIDEO_GAME_MATERIAL_BATCH_VERTICES)
        {
            if (!TitanVideo_GameAppendMaterialBatchPass(
                    texture0,
                    g_video_game_material_batch,
                    batch_vertices,
                    textured,
                    0))
                return drawn != 0 ? drawn : -2;

            if (state->combine_mode ==
                    TITAN_GAME_COMBINE_TEX0_TEX1_LOD &&
                texture1 != 0)
            {
                if (!TitanVideo_GameAppendMaterialBatchPass(
                        texture1,
                        g_video_game_material_batch,
                        batch_vertices,
                        1,
                        1))
                    return drawn != 0 ? drawn : -2;
            }

            drawn += (int)(batch_vertices / 3u);
            batch_vertices = 0u;
        }

        for (index = 0u; index < 3u; index++)
        {
            const TitanGameMaterialVertex *source =
                &vertices[base + index];
            TitanVideoGameMaterialPhysicalVertex *destination =
                &g_video_game_material_batch[batch_vertices + index];
            uint32_t color = source->rgba;

            destination->x = screen[index].x;
            destination->y = screen[index].y;
            destination->z = screen[index].z;
            destination->s0 = source->s0;
            destination->t0 = source->t0;
            destination->q0 = TitanVideoGame_ResolveQ(
                source->q0,
                screen[index].reciprocal_w
            );
            destination->s1 = source->s1;
            destination->t1 = source->t1;
            destination->q1 = TitanVideoGame_ResolveQ(
                source->q1,
                screen[index].reciprocal_w
            );
            destination->red = (unsigned char)(color >> 24u);
            destination->green = (unsigned char)(color >> 16u);
            destination->blue = (unsigned char)(color >> 8u);
            destination->alpha = (unsigned char)color;
            destination->fog = source->fog;
            destination->lod = source->lod_fraction;
        }

        batch_vertices += 3u;
    }

    if (batch_vertices != 0u)
    {
        if (!TitanVideo_GameAppendMaterialBatchPass(
                texture0,
                g_video_game_material_batch,
                batch_vertices,
                textured,
                0))
            return drawn != 0 ? drawn : -2;

        if (state->combine_mode ==
                TITAN_GAME_COMBINE_TEX0_TEX1_LOD &&
            texture1 != 0)
        {
            if (!TitanVideo_GameAppendMaterialBatchPass(
                    texture1,
                    g_video_game_material_batch,
                    batch_vertices,
                    1,
                    1))
                return drawn != 0 ? drawn : -2;
        }

        drawn += (int)(batch_vertices / 3u);
    }

    return drawn;
}

static int TitanVideo_GameAppendMaterialPass(
    const TitanVideoGameTexture *texture,
    float x0, float y0, unsigned int z0, float s0, float t0, float q0,
    float x1, float y1, unsigned int z1, float s1, float t1, float q1,
    float x2, float y2, unsigned int z2, float s2, float t2, float q2,
    unsigned char r0, unsigned char g0, unsigned char b0, unsigned char a0,
    unsigned char r1, unsigned char g1, unsigned char b1, unsigned char a1,
    unsigned char r2, unsigned char g2, unsigned char b2, unsigned char a2,
    int textured,
    int second_pass
)
{
    TitanVideoGameMaterialPhysicalVertex local_vertices[3];

#define TITAN_GAME_COPY_MATERIAL_VERTEX(I,X,Y,Z,S,T,Q,R,G,B,A) do { \
    local_vertices[(I)].x = (X); \
    local_vertices[(I)].y = (Y); \
    local_vertices[(I)].z = (Z); \
    local_vertices[(I)].s0 = (S); \
    local_vertices[(I)].t0 = (T); \
    local_vertices[(I)].q0 = (Q); \
    local_vertices[(I)].s1 = (S); \
    local_vertices[(I)].t1 = (T); \
    local_vertices[(I)].q1 = (Q); \
    local_vertices[(I)].red = (R); \
    local_vertices[(I)].green = (G); \
    local_vertices[(I)].blue = (B); \
    local_vertices[(I)].alpha = (A); \
    local_vertices[(I)].fog = 0u; \
    local_vertices[(I)].lod = (A); \
} while (0)

    TITAN_GAME_COPY_MATERIAL_VERTEX(0u,x0,y0,z0,s0,t0,q0,r0,g0,b0,a0);
    TITAN_GAME_COPY_MATERIAL_VERTEX(1u,x1,y1,z1,s1,t1,q1,r1,g1,b1,a1);
    TITAN_GAME_COPY_MATERIAL_VERTEX(2u,x2,y2,z2,s2,t2,q2,r2,g2,b2,a2);
#undef TITAN_GAME_COPY_MATERIAL_VERTEX

    return TitanVideo_GameAppendMaterialBatchPass(
        texture,
        local_vertices,
        3u,
        textured,
        second_pass
    );
}

int TitanVideo_DrawGameMaterialTrianglePhysical(
    float x0, float y0, unsigned int z0,
    float s00, float t00, float q00, float s10, float t10, float q10,
    float x1, float y1, unsigned int z1,
    float s01, float t01, float q01, float s11, float t11, float q11,
    float x2, float y2, unsigned int z2,
    float s02, float t02, float q02, float s12, float t12, float q12,
    unsigned char r0, unsigned char g0, unsigned char b0, unsigned char a0,
    unsigned char r1, unsigned char g1, unsigned char b1, unsigned char a1,
    unsigned char r2, unsigned char g2, unsigned char b2, unsigned char a2,
    unsigned char fog0, unsigned char fog1, unsigned char fog2,
    unsigned char lod0, unsigned char lod1, unsigned char lod2,
    uint32_t texture0_handle, uint32_t texture1_handle
)
{
    const TitanGameRenderState *state = &g_video.game_render_state;
    TitanVideoGameTexture *texture0 = 0;
    TitanVideoGameTexture *texture1 = 0;
    int textured;
    if (!g_video.game_mode || !g_video.frame_open) return 0;
    if (texture0_handle != 0u)
    {
        texture0 = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool,
            texture0_handle
        );
        if (texture0 == 0) return 0;
    }
    if (texture1_handle != 0u)
    {
        texture1 = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool,
            texture1_handle
        );
        if (texture1 == 0) return 0;
    }
    TitanVideo_GameApplyFog(&r0, &g0, &b0, fog0);
    TitanVideo_GameApplyFog(&r1, &g1, &b1, fog1);
    TitanVideo_GameApplyFog(&r2, &g2, &b2, fog2);
    textured = state->combine_mode != TITAN_GAME_COMBINE_SHADE && texture0 != 0;
    if (!TitanVideo_GameAppendMaterialPass(
            texture0,
            x0,y0,z0,TitanVideoGame_PrepareSt(
                s00,texture0 ? texture0->width : 1u,q00),
                TitanVideoGame_PrepareSt(
                    t00,texture0 ? texture0->height : 1u,q00),q00,
            x1,y1,z1,TitanVideoGame_PrepareSt(
                s01,texture0 ? texture0->width : 1u,q01),
                TitanVideoGame_PrepareSt(
                    t01,texture0 ? texture0->height : 1u,q01),q01,
            x2,y2,z2,TitanVideoGame_PrepareSt(
                s02,texture0 ? texture0->width : 1u,q02),
                TitanVideoGame_PrepareSt(
                    t02,texture0 ? texture0->height : 1u,q02),q02,
            r0,g0,b0,a0,r1,g1,b1,a1,r2,g2,b2,a2,textured,0))
        return 0;
    if (state->combine_mode == TITAN_GAME_COMBINE_TEX0_TEX1_LOD && texture1 != 0)
    {
        if (!TitanVideo_GameAppendMaterialPass(
                texture1,
                x0,y0,z0,TitanVideoGame_PrepareSt(
                    s10,texture1->width,q10),
                    TitanVideoGame_PrepareSt(t10,texture1->height,q10),q10,
                x1,y1,z1,TitanVideoGame_PrepareSt(
                    s11,texture1->width,q11),
                    TitanVideoGame_PrepareSt(t11,texture1->height,q11),q11,
                x2,y2,z2,TitanVideoGame_PrepareSt(
                    s12,texture1->width,q12),
                    TitanVideoGame_PrepareSt(t12,texture1->height,q12),q12,
                255u,255u,255u,lod0,255u,255u,255u,lod1,255u,255u,255u,lod2,1,1))
            return 0;
    }
    return 1;
}

static TitanResult TitanVideo_GameAllocateTexture(
    unsigned int width,
    unsigned int height,
    unsigned char format,
    uint32_t *allocated_handle,
    TitanVideoGameTexture **game_texture
)
{
    if (!TitanVideoGameTexturePool_AllocateFormat(
            &g_video.game_texture_pool,
            width,
            height,
            format,
            allocated_handle,
            game_texture))
    {
        /* Released textures stay resident until all queued draws that reference
         * them have completed.  Under pressure, submit that prefix, reclaim the
         * retired ranges, then retry without changing Titan's frame ownership. */
        if (!g_video.frame_open ||
            !TitanVideoGameTexturePool_HasRetired(&g_video.game_texture_pool) ||
            TitanVideo_SubmitPacket(1) != TITAN_RESULT_OK)
            return TITAN_ERROR_VIDEO_INITIALIZATION;
        TitanPikaVr_RecordBackingReclaimed(
            TitanVideoGameTexturePool_ReclaimRetiredCount(
                &g_video.game_texture_pool
            )
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
        g_video.bound_game_texture_handle = 0u;
        if (!TitanVideoGameTexturePool_AllocateFormat(
                &g_video.game_texture_pool,
                width,
                height,
                format,
                allocated_handle,
                game_texture))
            return TITAN_ERROR_VIDEO_INITIALIZATION;
    }
    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_GameSubmitTextureUpload(
    const void *staging,
    const uint32_t *clut,
    unsigned int staging_index,
    const TitanVideoGameTexture *game_texture,
    uint32_t allocated_handle,
    uint32_t *texture_handle
)
{
    qword_t *cursor;
    unsigned int texture_address = g_video.game_texture_arena_address +
        game_texture->offset_bytes / 4u;
    unsigned int staging_bytes;
    int psm = TitanVideo_GameTexturePsm(game_texture);

    if (staging == 0 || game_texture == 0 || texture_handle == 0 ||
        staging_index > 1u)
        return TITAN_ERROR_INVALID_ARGUMENT;

    /* Conversion into the alternate EE staging buffer happened before this
     * call, overlapping the previous GIF DMA.  Only now wait before reusing
     * the shared packet2 chain and GIF channel. */
    if (TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
    {
        (void)TitanVideoGameTexturePool_Release(
            &g_video.game_texture_pool,
            allocated_handle,
            0
        );
        return TITAN_ERROR_FRAME_END;
    }

    if (game_texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32)
        staging_bytes =
            game_texture->buffer_width * game_texture->height * 4u;
    else if (game_texture->format ==
             TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16)
        staging_bytes =
            game_texture->buffer_width * game_texture->height * 2u;
    else
        staging_bytes =
            game_texture->buffer_width * game_texture->height;

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)staging,
        game_texture->buffer_width,
        game_texture->height,
        psm,
        (int)texture_address,
        game_texture->buffer_width
    );

    if (game_texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
    {
        unsigned int clut_address;

        if (clut == 0)
        {
            (void)TitanVideoGameTexturePool_Release(
                &g_video.game_texture_pool,
                allocated_handle,
                0
            );
            return TITAN_ERROR_INVALID_ARGUMENT;
        }

        clut_address = g_video.game_texture_arena_address +
            (game_texture->offset_bytes +
             game_texture->clut_offset_bytes) / 4u;
        cursor = draw_texture_transfer(
            cursor,
            (void *)clut,
            16u,
            16u,
            GS_PSM_32,
            (int)clut_address,
            64u
        );
    }

    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    /* P10.58 PS2 cache coherency: write back only the actual DMA inputs.
     * P153 measured hundreds of uploads per slow frame; FlushCache(0) here
     * multiplied into a catastrophic whole-D$ writeback storm. */
    SyncDCache(
        (void *)staging,
        (void *)((unsigned char *)staging + staging_bytes)
    );
    if (clut != 0)
    {
        SyncDCache(
            (void *)clut,
            (void *)((unsigned char *)clut + 256u * sizeof(uint32_t))
        );
    }
    SyncDCache(
        g_video.upload_packet->base,
        g_video.upload_packet->next
    );

    dma_channel_send_packet2(
        g_video.upload_packet,
        DMA_CHANNEL_GIF,
        1
    );

    /* Return immediately: the next texture is converted on the EE into the
     * other staging slot while this DMA runs.  The next upload or any draw
     * submission will fence it through TitanVideo_GameWaitTextureUpload(). */
    g_video.game_texture_upload_pending = 1;
    g_video.game_path3_pending = 1;
    g_video.game_texture_upload_handle = allocated_handle;
    g_video.game_texture_staging_index = staging_index ^ 1u;

    *texture_handle = allocated_handle;
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanPikaVr_UpdateTextureUsed(
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
        TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool)
    );
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_GameUploadTexturePsm16(
    const unsigned short *pixels, unsigned int width, unsigned int height,
    unsigned int stride, uint32_t *texture_handle
)
{
    TitanVideoGameTexture *game_texture;
    TitanPikaVrFingerprint fingerprint;
    TitanResult result;
    uint32_t allocated_handle;
    unsigned int row;
    unsigned int upload_pixels;
    unsigned int reference_bytes;
    unsigned int staging_index;

    if (!g_video.game_mode || pixels == 0 || texture_handle == 0 ||
        width == 0u || height == 0u || stride < width ||
        width > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH ||
        height > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT)
        return TITAN_ERROR_INVALID_ARGUMENT;

    TitanPikaVr_FingerprintPsm16(pixels,width,height,stride,&fingerprint);
    if (TitanPikaVr_TryReuseTexture(&fingerprint, texture_handle))
        return TITAN_RESULT_OK;

    staging_index = g_video.game_texture_staging_index;

    result = TitanVideo_GameAllocateTexture(
        width,
        height,
        TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16,
        &allocated_handle,
        &game_texture
    );
    if (result != TITAN_RESULT_OK) return result;

    upload_pixels = game_texture->buffer_width * height;
    memset(
        g_video_game_texture_staging[staging_index].psm16,
        0,
        upload_pixels * sizeof(g_video_game_texture_staging[staging_index].psm16[0])
    );
    for (row = 0u; row < height; row++)
    {
        memcpy(
            &g_video_game_texture_staging[staging_index].psm16[
                row * game_texture->buffer_width
            ],
            &pixels[row * stride],
            width * sizeof(pixels[0])
        );
    }
    result = TitanVideo_GameSubmitTextureUpload(
        g_video_game_texture_staging[staging_index].psm16,
        0,
        staging_index,
        game_texture,
        allocated_handle,
        texture_handle
    );
    if (result != TITAN_RESULT_OK) return result;
    reference_bytes = TitanVideoGameTexturePool_GetStorageBytesForFormat(
        width,height,TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16
    );
    (void)TitanPikaVr_RegisterTexture(
        &fingerprint,*texture_handle,reference_bytes,
        game_texture->storage_bytes,0,0u
    );
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_GameUploadTextureI8(
    const unsigned char *pixels, unsigned int width, unsigned int height,
    unsigned int stride, uint32_t *texture_handle
)
{
    TitanVideoGameTexture *game_texture;
    TitanResult result;
    uint32_t allocated_handle;
    unsigned int row;
    unsigned int value;
    unsigned int staging_index;
    unsigned int buffer_width;
    int large;
    unsigned char *staging;
    uint32_t *clut;

    if (!g_video.game_mode || pixels == 0 || texture_handle == 0 ||
        width == 0u || height == 0u || stride < width ||
        width > TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH ||
        height > TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT)
        return TITAN_ERROR_INVALID_ARGUMENT;

    large = width > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH ||
        height > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT;
    staging_index = g_video.game_texture_staging_index;
    staging = large ? g_video_game_texture_large_psm8
                    : g_video_game_texture_staging[staging_index].psm8;
    clut = large ? g_video_game_texture_large_clut
                 : g_video_game_texture_clut[staging_index];

    /* The large indexed lane has one scratch. Fence the previous GIF DMA
     * before overwriting it; <=128 textures keep the normal double buffer. */
    if (large && TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
        return TITAN_ERROR_FRAME_END;

    result = TitanVideo_GameAllocateTexture(
        width, height, TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT,
        &allocated_handle, &game_texture);
    if (result != TITAN_RESULT_OK)
        return result;

    buffer_width = game_texture->buffer_width;
    memset(staging, 0, buffer_width * height);
    for (row = 0u; row < height; row++)
        memcpy(&staging[row * buffer_width], &pixels[row * stride], width);

    for (value = 0u; value < 256u; value++)
        clut[value] = TitanVideoGame_PackPsm32(
            (uint8_t)value, (uint8_t)value, (uint8_t)value, 255u);

    result = TitanVideo_GameSubmitTextureUpload(
        staging, clut, staging_index, game_texture,
        allocated_handle, texture_handle);
    if (result != TITAN_RESULT_OK)
        return result;

    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanPikaVr_UpdateTextureUsed(
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
        TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool));
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_GameUploadTextureRgba32(
    const unsigned char *rgba,
    unsigned int width,
    unsigned int height,
    unsigned int stride_bytes,
    uint32_t *texture_handle
)
{
    /* P160.3 alpha contract restoration: TitanGePlatform now passes decoded RGBA directly into this kernel lane. Preserve RGBA alpha semantics here: exact PSM8+CLUT remains allowed, otherwise use PSM32. Do not silently collapse the RGBA API to A1/PSM16. */

    TitanVideoGameTexture *game_texture;
    TitanPikaVrFingerprint fingerprint;
    TitanResult result;
    uint32_t allocated_handle;
    unsigned int upload_pixels;
    unsigned int row;
    unsigned int column;
    unsigned int reference_bytes;
    unsigned int indexed_bytes;
    unsigned int staging_index;
    int use_palette = 0;
    int large_indexed_only = 0;
    unsigned char *palette_staging;
    uint32_t *palette_clut;

    if (!g_video.game_mode || rgba == 0 || texture_handle == 0 ||
        width == 0u || height == 0u ||
        stride_bytes < width * 4u ||
        width > TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH ||
        height > TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT)
        return TITAN_ERROR_INVALID_ARGUMENT;

    TitanPikaVr_FingerprintRgba32(rgba,width,height,stride_bytes,&fingerprint);
    if (TitanPikaVr_TryReuseTexture(&fingerprint, texture_handle))
        return TITAN_RESULT_OK;

    staging_index = g_video.game_texture_staging_index;
    large_indexed_only =
        width > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_WIDTH ||
        height > TITAN_VIDEO_GAME_TEXTURE_DIRECT_MAX_HEIGHT;
    palette_staging = large_indexed_only
        ? g_video_game_texture_large_psm8
        : g_video_game_texture_staging[staging_index].psm8;
    palette_clut = large_indexed_only
        ? g_video_game_texture_large_clut
        : g_video_game_texture_clut[staging_index];
    if (large_indexed_only &&
        TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
        return TITAN_ERROR_FRAME_END;

    reference_bytes = TitanVideoGameTexturePool_GetStorageBytesForFormat(
        width,height,TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32
    );
    indexed_bytes = TitanVideoGameTexturePool_GetStorageBytesForFormat(
        width,height,TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT
    );
    if ((large_indexed_only || TitanPikaVr_IsOptimizedPathActive()) &&
        indexed_bytes != 0u && indexed_bytes < reference_bytes &&
        TitanPikaVr_BuildExactPaletteRgba32(
            rgba,width,height,stride_bytes,
            palette_staging,
            TitanVideoGameTexturePool_GetBufferWidthForFormat(
                width,TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT),
            palette_clut,256u))
        use_palette = 1;

    /* Large textures deliberately have no generic PSM32 fallback: that would
     * require >600 KiB per staging slot and recreate the EE memory pressure
     * this pass is removing. The gunbarrel eye is exact grayscale/alpha and
     * therefore always fits the lossless PSM8+CLUT lane. */
    if (large_indexed_only && !use_palette)
        return TITAN_ERROR_INVALID_ARGUMENT;

    result = TitanVideo_GameAllocateTexture(
        width,
        height,
        use_palette ? TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT
                    : TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32,
        &allocated_handle,
        &game_texture
    );
    if (result != TITAN_RESULT_OK) return result;

    if (use_palette)
    {
        result = TitanVideo_GameSubmitTextureUpload(
            palette_staging,
            palette_clut,
            staging_index,
            game_texture,
            allocated_handle,
            texture_handle
        );
    }
    else
    {
        upload_pixels = game_texture->buffer_width * height;
        memset(
            g_video_game_texture_staging[staging_index].psm32,
            0,
            upload_pixels * sizeof(g_video_game_texture_staging[staging_index].psm32[0])
        );
        for (row = 0u; row < height; row++)
        {
            for (column = 0u; column < width; column++)
            {
                const unsigned char *source =
                    &rgba[row * stride_bytes + column * 4u];
                g_video_game_texture_staging[staging_index].psm32[
                    row * game_texture->buffer_width + column
                ] = TitanVideoGame_PackPsm32(
                    source[0], source[1], source[2], source[3]
                );
            }
        }
        result = TitanVideo_GameSubmitTextureUpload(
            g_video_game_texture_staging[staging_index].psm32,
            0,
            staging_index,
            game_texture,
            allocated_handle,
            texture_handle
        );
    }
    if (result != TITAN_RESULT_OK) return result;
    (void)TitanPikaVr_RegisterTexture(
        &fingerprint,*texture_handle,reference_bytes,
        game_texture->storage_bytes,use_palette,game_texture->clut_bytes
    );
    return TITAN_RESULT_OK;
}

int TitanVideo_GameReleaseTexture(uint32_t texture_handle)
{
    int release_backing;
    int defer_reuse;

    if (!g_video.game_mode) return 0;
    if (TitanVideoGameTexturePool_Lookup(&g_video.game_texture_pool, texture_handle) == 0)
        return 0;
    release_backing = TitanPikaVr_ReleaseTexture(texture_handle);
    if (!release_backing)
        return 1;
    defer_reuse = g_video.frame_open != 0;
    if (!TitanVideoGameTexturePool_Release(
            &g_video.game_texture_pool,
            texture_handle,
            defer_reuse))
        return 0;
    if (!defer_reuse)
        TitanPikaVr_RecordBackingReclaimed(1u);
    if (g_video.bound_texture == TITAN_VIDEO_TEXTURE_GAME_BASE &&
        g_video.bound_game_texture_handle == texture_handle)
    {
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
        g_video.bound_game_texture_handle = 0u;
    }
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanPikaVr_UpdateTextureUsed(
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
        TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool)
    );
    return 1;
}

void TitanVideo_GameResetTextures(void)
{
    unsigned int texture_index;

    if (TitanVideo_GameWaitTextureUpload() != TITAN_RESULT_OK)
        return;
    if (g_video.frame_open)
    {
        for (texture_index = 0u;
             texture_index < TITAN_VIDEO_GAME_TEXTURE_CAPACITY;
             texture_index++)
        {
            if (g_video.game_texture_pool.textures[texture_index].state ==
                TITAN_VIDEO_GAME_TEXTURE_LIVE)
                g_video.game_texture_pool.textures[texture_index].state =
                    TITAN_VIDEO_GAME_TEXTURE_RETIRED;
        }
    }
    else
    {
        TitanPikaVr_RecordBackingReclaimed(
            TitanVideoGameTexturePool_GetAllocatedTextureCount(
                &g_video.game_texture_pool
            )
        );
        TitanVideoGameTexturePool_Reset(&g_video.game_texture_pool);
    }
    TitanPikaVr_ResetTextures();
    TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
    TitanPikaVr_UpdateTextureUsed(
        TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
        TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool)
    );
    if (g_video.game_mode)
    {
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
        g_video.bound_game_texture_handle = 0u;

        /* P10.23 v2: BeginGameMode creates the visual reference textures, but
         * TitanGameRuntime_Begin immediately calls GameResetTextures().  The
         * original P123 therefore invalidated its own handles while leaving
         * the green status bits set, making all three textured squares appear
         * as their solid lane backgrounds.  Recreate the diagnostic textures
         * after a non-frame reset so the visual A/B actually samples live GS
         * backing.  GameResetTextures is only called by runtime bring-up in
         * the current target tree. */
        if (!g_video.frame_open)
            TitanVideo_GameDiagnosticTextureInit();
    }
}

TitanResult TitanVideo_Set720pPath(TitanVideo720pPath path)
{
    if (path != TITAN_VIDEO_720P_PATH_REFERENCE &&
        path != TITAN_VIDEO_720P_PATH_OPTIMIZED)
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }
    if (g_video.frame_open || g_video.game_mode)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    /* CP03 keeps path switching deterministic: choose the policy before
     * entering 720p. Live rebuild while scanout is active is intentionally
     * deferred rather than risking a partially reconfigured GS state. */
    if (TitanVideoMode_GetOutput() == TITAN_VIDEO_OUTPUT_HDTV_720P &&
        g_video.hd_path_preference != path)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_video.hd_path_preference = path;
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_Set720pValidationPath(TitanVideo720pPath path)
{
    TitanVideo720pPath previous;
    TitanVideoFramebufferConfig config;
    TitanVideo720pTransactionResolution resolution;
    TitanResult result;
    TitanResult rollback_result;

    if (path != TITAN_VIDEO_720P_PATH_REFERENCE &&
        path != TITAN_VIDEO_720P_PATH_OPTIMIZED)
        return TITAN_ERROR_INVALID_ARGUMENT;
    if (!g_video.initialized || g_video.frame_open || g_video.game_mode)
        return TITAN_ERROR_VIDEO_INITIALIZATION;

    previous = TitanVideo720pPath_Sanitize(g_video.hd_path_preference);
    path = TitanVideo720pPath_Sanitize(path);
    g_video.hd_path_preference = previous;
    if (previous == path)
        return TITAN_RESULT_OK;

    /* Outside active 720p this is only a preference change; the next 720p
     * transition consumes it. 480i resources are not rebuilt or contaminated. */
    if (TitanVideoMode_GetOutput() != TITAN_VIDEO_OUTPUT_HDTV_720P)
    {
        g_video.hd_path_preference = path;
        return TITAN_RESULT_OK;
    }

    config = TitanVideo_GetFramebufferConfig(TITAN_VIDEO_OUTPUT_HDTV_720P);
    result = TitanVideoMode_BeginReconfigure();
    if (result != TITAN_RESULT_OK)
        return result;

    g_video.hd_path_preference = path;
    result = TitanVideo_RestoreFramebufferConfig(&config);
    if (result != TITAN_RESULT_OK)
    {
        /* BuildFix25 transactional rollback: RestoreFramebufferConfig() now
         * reasserts initialized only after a complete GS/resource restore. */
        g_video.hd_path_preference = previous;
        rollback_result = TitanVideo_RestoreFramebufferConfig(&config);
        resolution = TitanVideo720pTransaction_Resolve(
            previous,
            path,
            result,
            1,
            rollback_result
        );
        if (rollback_result == TITAN_RESULT_OK)
        {
            g_video.hd_path_preference = resolution.active_path;
            g_video.initialized = 1;
            g_video.packet_error = 0;
            return result;
        }

        /* Primary and rollback both failed. Never report a renderable shell. */
        g_video.hd_path_preference = resolution.active_path;
        g_video.initialized = 0;
        g_video.packet_error = 1;
        return rollback_result;
    }

    resolution = TitanVideo720pTransaction_Resolve(
        previous,
        path,
        result,
        0,
        TITAN_RESULT_OK
    );
    g_video.hd_path_preference = resolution.active_path;
    g_video.initialized = 1;
    g_video.packet_error = 0;
    return TITAN_RESULT_OK;
}

TitanVideo720pPath TitanVideo_Get720pPath(void)
{
    g_video.hd_path_preference = TitanVideo720pPath_Sanitize(
        g_video.hd_path_preference
    );
    return g_video.hd_path_preference;
}

unsigned int TitanVideo_GetInternalWidth(void)
{
    return g_video.internal_width;
}

unsigned int TitanVideo_GetInternalHeight(void)
{
    return g_video.internal_height;
}

unsigned int TitanVideo_GetOutputWidth(void)
{
    return g_video.framebuffer_width;
}

unsigned int TitanVideo_GetOutputHeight(void)
{
    return g_video.framebuffer_height;
}

TitanResult TitanVideo_SetProgressiveOutput(int enabled)
{
    TitanVideoOutputMode output;

    if (!g_video.initialized || g_video.game_mode)
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    output = enabled != 0
        ? TITAN_VIDEO_OUTPUT_HDTV_720P
        : TITAN_VIDEO_OUTPUT_LEGACY;

    return TitanVideo_SwitchFramebufferConfig(output);
}

int TitanVideo_IsProgressiveOutput(void)
{
    return TitanVideoMode_IsProgressive();
}

TitanResult TitanVideo_FlushForPath1(void)
{
    if (
        !g_video.initialized ||
        !g_video.frame_open ||
        g_video.draw_packet == 0
    )
    {
        return TITAN_ERROR_FRAME_END;
    }

    /* PATH1 (VU1 XGKICK) must not race a still-executing PATH3 packet.
     * dma completion only proves that the GIF DMA has been fed; it does not
     * prove that the GS finished consuming the packet. Submit a FINISH marker
     * and wait for it before handing GIF ownership to PATH1. */
    if (TitanVideo_SubmitPacket(1) != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_RestoreAfterPath1(void)
{
    qword_t *cursor;

    if (
        !g_video.initialized ||
        !g_video.frame_open ||
        g_video.draw_packet == 0 ||
        !TitanVideo_ReserveQwords(32u)
    )
    {
        return TITAN_ERROR_FRAME_END;
    }

    cursor = g_video.draw_packet->next;
    cursor = draw_setup_environment(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        TitanVideo_GetActiveDrawFramebuffer(),
        &g_video.zbuffer
    );
    packet2_update(g_video.draw_packet, cursor);

    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return TITAN_RESULT_OK;
}

void TitanVideo_AccountExternalQwords(unsigned int qwords)
{
    if (g_video.frame_open)
    {
        g_video.current_frame_qwords += qwords;
        g_video.current_external_qwords += qwords;
        TitanPikaVr_RecordRibbonPath1Qwords(qwords);
    }
}

void TitanVideo_SetPikaVrOptimizedPath(int enabled)
{
    TitanPikaVr_SetOptimizedPath(enabled);
}

void TitanVideo_GetPikaVrTelemetry(TitanPikaVrTelemetry *telemetry)
{
    TitanPikaVr_GetTelemetry(telemetry);
}

unsigned int TitanVideo_GetVramBytesReserved(void)
{
    return TitanVideoMemory_GetReservedBytes();
}

unsigned int TitanVideo_GetVramBytesFree(void)
{
    return TitanVideoMemory_GetFreeBytes();
}

unsigned int TitanVideo_GetFramebufferWidth(void)
{
    return g_video.framebuffer_width;
}

unsigned int TitanVideo_GetFramebufferHeight(void)
{
    return g_video.framebuffer_height;
}

unsigned int TitanVideo_GetFramebufferCount(void)
{
    return g_video.framebuffer_count;
}

int TitanVideo_GetFramebufferPsm(void)
{
    return g_video.framebuffer_psm;
}

unsigned int TitanVideo_GetLastFrameGsLoadPercent(void)
{
    unsigned int usable_qwords;
    unsigned int percent;

    usable_qwords = TITAN_VIDEO_PACKET_QWORDS - TITAN_VIDEO_PACKET_SAFETY_QWORDS;
    percent = usable_qwords > 0u
        ? (g_video.last_frame_qwords * 100u) / usable_qwords
        : 0u;

    return percent > 100u ? 100u : percent;
}

TitanResult TitanVideo_BeginFrame(void)
{
    qword_t *cursor;
    u64 *tag;

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-begin-enter", 0);
    }

    if (
        !g_video.initialized ||
        g_video.draw_packet == 0
    )
    {
        return TITAN_ERROR_FRAME_BEGIN;
    }

    /* Never let the ordinary per-frame reset erase a VIF1 ownership fault.
     * Continuing with EE/PATH3 here could overlap an unfinished PATH1 stream. */
    if (g_video.game_mode && TitanGameVu1_HasFault())
        return TITAN_ERROR_FRAME_BEGIN;

    packet2_reset(
        g_video.draw_packet,
        0
    );

    g_video.packet_error = 0;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    g_video.bound_game_texture_handle = 0u;
    g_video.game_material_state_dirty = 1;
    g_video.game_material_texture_handle = 0xffffffffu;
    g_video.game_material_second_pass = 0xffffffffu;
    g_video.current_frame_qwords = 0u;
    g_video.current_external_qwords = 0u;
    TitanPikaVr_BeginRendererFrame();
    g_video.frame_open = 1;

    cursor = g_video.draw_packet->next;
    cursor = draw_setup_environment(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        TitanVideo_GetActiveDrawFramebuffer(),
        &g_video.zbuffer
    );
    if (g_video.game_mode)
    {
        /* RGBA5551 alpha is meaningful only with TA0=0 and TA1=0x80.  AEM is
         * disabled so RGB zero never silently turns an opaque texel clear. */
        tag = (u64 *)cursor;
        tag[0] = GIF_SET_TAG(1u, 0u, 0u, 0u, GIF_FLG_PACKED, 1u);
        tag[1] = GIF_REG_AD;
        cursor++;
        cursor = TitanVideo_GameSetAd(
            cursor,
            GS_SET_TEXA(0u, 0u, 0x80u),
            GS_REG_TEXA
        );
    }
    packet2_update(
        g_video.draw_packet,
        cursor
    );

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-begin-done", (int)packet2_get_qw_count(g_video.draw_packet));
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
    point_t point;
    qword_t *cursor;

    if (
        x < 0 ||
        y < 0 ||
        (unsigned int)x >= g_video.framebuffer_width ||
        y >= (int)g_video.framebuffer_height
    )
    {
        return;
    }

    if (!TitanVideo_ReserveQwords(3u))
    {
        return;
    }

    point.v0.x = (float)x;
    point.v0.y = (float)y;
    point.v0.z = TITAN_VIDEO_Z;
    point.color = TitanVideo_MakeColor(
        red,
        green,
        blue,
        255u
    );

    cursor = g_video.draw_packet->next;
    cursor = draw_point(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &point
    );
    packet2_update(
        g_video.draw_packet,
        cursor
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
    point_t point;
    qword_t *cursor;

    if (
        x < 0 ||
        y < 0 ||
        (unsigned int)x >= g_video.framebuffer_width ||
        y >= (int)g_video.framebuffer_height ||
        alpha == 0u
    )
    {
        return;
    }

    if (!TitanVideo_ReserveQwords(3u))
    {
        return;
    }

    point.v0.x = (float)x;
    point.v0.y = (float)y;
    point.v0.z = TITAN_VIDEO_Z;
    point.color = TitanVideo_MakeColor(
        red,
        green,
        blue,
        alpha
    );

    cursor = g_video.draw_packet->next;
    cursor = draw_point(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &point
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}

void TitanVideo_Clear(
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanVideo_AppendRectPhysical(
        0.0f,
        0.0f,
        (float)g_video.framebuffer_width,
        (float)g_video.framebuffer_height,
        red,
        green,
        blue,
        255u
    );
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
    g_video.background_top.red = top_red;
    g_video.background_top.green = top_green;
    g_video.background_top.blue = top_blue;

    g_video.background_bottom.red = bottom_red;
    g_video.background_bottom.green = bottom_green;
    g_video.background_bottom.blue = bottom_blue;

    g_video.background_ready = 1;
}

void TitanVideo_RestoreBackground(void)
{
    unsigned int band;
    unsigned int factor;
    unsigned int inverse_factor;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    float y0;
    float y1;

    if (!g_video.background_ready)
    {
        TitanVideo_Clear(
            82,
            82,
            88
        );

        return;
    }

    if (
        g_video.background_top.red ==
            g_video.background_bottom.red &&
        g_video.background_top.green ==
            g_video.background_bottom.green &&
        g_video.background_top.blue ==
            g_video.background_bottom.blue
    )
    {
        TitanVideo_Clear(
            g_video.background_top.red,
            g_video.background_top.green,
            g_video.background_top.blue
        );

        return;
    }

    for (
        band = 0u;
        band < TITAN_VIDEO_BACKGROUND_BANDS;
        band++
    )
    {
        factor =
            (band * 255u) /
            (TITAN_VIDEO_BACKGROUND_BANDS - 1u);

        inverse_factor = 255u - factor;

        red = (unsigned char)(
            (
                ((unsigned int)g_video.background_top.red * inverse_factor) +
                ((unsigned int)g_video.background_bottom.red * factor) +
                127u
            ) / 255u
        );

        green = (unsigned char)(
            (
                ((unsigned int)g_video.background_top.green * inverse_factor) +
                ((unsigned int)g_video.background_bottom.green * factor) +
                127u
            ) / 255u
        );

        blue = (unsigned char)(
            (
                ((unsigned int)g_video.background_top.blue * inverse_factor) +
                ((unsigned int)g_video.background_bottom.blue * factor) +
                127u
            ) / 255u
        );

        y0 =
            ((float)band * (float)g_video.framebuffer_height) /
            (float)TITAN_VIDEO_BACKGROUND_BANDS;

        y1 =
            ((float)(band + 1u) * (float)g_video.framebuffer_height) /
            (float)TITAN_VIDEO_BACKGROUND_BANDS;

        TitanVideo_AppendRectPhysical(
            0.0f,
            y0,
            (float)g_video.framebuffer_width,
            y1,
            red,
            green,
            blue,
            255u
        );
    }
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
        TitanVideo_MapLogicalX((float)x),
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
    if (
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0u
    )
    {
        return;
    }

    TitanVideo_AppendRectPhysical(
        (float)TitanVideo_MapLogicalX(x),
        (float)TitanVideo_MapLogicalY(y),
        (float)TitanVideo_MapLogicalX(x + width),
        (float)TitanVideo_MapLogicalY(y + height),
        red,
        green,
        blue,
        alpha
    );
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
        255u
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
    float physical_center_x;
    float physical_center_y;
    float radius_x;
    float radius_y;

    if (radius <= 0.0f || alpha == 0u)
    {
        return;
    }

    physical_center_x =
        (float)TitanVideo_MapLogicalX(center_x);

    physical_center_y =
        (float)TitanVideo_MapLogicalY(center_y);

    radius_x =
        (float)TitanVideo_MapLogicalRadiusX(radius);

    radius_y = (float)TitanVideo_MapLogicalRadiusY(radius);

    if (radius_y < 1.0f)
    {
        radius_y = 1.0f;
    }

    TitanVideo_AppendEllipseFanPhysical(
        physical_center_x,
        physical_center_y,
        radius_x,
        radius_y,
        0.0f,
        (float)(M_PI * 2.0),
        TITAN_VIDEO_ELLIPSE_SEGMENTS,
        red,
        green,
        blue,
        alpha
    );
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
    float x0;
    float y0;
    float x1;
    float y1;
    float radius_x;
    float radius_y;
    float maximum_radius_x;
    float maximum_radius_y;

    if (
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0u
    )
    {
        return;
    }

    x0 = TitanVideo_MapLogicalFloatX(x);
    y0 = TitanVideo_MapLogicalFloatY(y);
    x1 = TitanVideo_MapLogicalFloatX(x + width);
    y1 = TitanVideo_MapLogicalFloatY(y + height);

    if (x1 < x0)
    {
        float temporary;
        temporary = x0;
        x0 = x1;
        x1 = temporary;
    }

    if (y1 < y0)
    {
        float temporary;
        temporary = y0;
        y0 = y1;
        y1 = temporary;
    }

    radius_x = (float)TitanVideo_MapLogicalRadiusX(radius);
    radius_y = (float)TitanVideo_MapLogicalRadiusY(radius);

    maximum_radius_x = (x1 - x0) * 0.5f;
    maximum_radius_y = (y1 - y0) * 0.5f;

    if (radius_x > maximum_radius_x)
    {
        radius_x = maximum_radius_x;
    }

    if (radius_y > maximum_radius_y)
    {
        radius_y = maximum_radius_y;
    }

    if (radius_x <= 0.5f || radius_y <= 0.5f)
    {
        TitanVideo_AppendRectPhysical(
            x0,
            y0,
            x1,
            y1,
            red,
            green,
            blue,
            alpha
        );
        return;
    }

    TitanVideo_AppendRectPhysical(
        x0 + radius_x,
        y0,
        x1 - radius_x,
        y1,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendRectPhysical(
        x0,
        y0 + radius_y,
        x0 + radius_x,
        y1 - radius_y,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendRectPhysical(
        x1 - radius_x,
        y0 + radius_y,
        x1,
        y1 - radius_y,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendEllipseFanPhysical(
        x0 + radius_x,
        y0 + radius_y,
        radius_x,
        radius_y,
        (float)M_PI,
        (float)(M_PI * 1.5),
        TITAN_VIDEO_CORNER_SEGMENTS,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendEllipseFanPhysical(
        x1 - radius_x,
        y0 + radius_y,
        radius_x,
        radius_y,
        (float)(M_PI * 1.5),
        (float)(M_PI * 2.0),
        TITAN_VIDEO_CORNER_SEGMENTS,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendEllipseFanPhysical(
        x1 - radius_x,
        y1 - radius_y,
        radius_x,
        radius_y,
        0.0f,
        (float)(M_PI * 0.5),
        TITAN_VIDEO_CORNER_SEGMENTS,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendEllipseFanPhysical(
        x0 + radius_x,
        y1 - radius_y,
        radius_x,
        radius_y,
        (float)(M_PI * 0.5),
        (float)M_PI,
        TITAN_VIDEO_CORNER_SEGMENTS,
        red,
        green,
        blue,
        alpha
    );
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
    float physical_x0;
    float physical_y0;
    float physical_x1;
    float physical_y1;
    float delta_x;
    float delta_y;
    float length;
    float half_thickness;
    float perpendicular_x;
    float perpendicular_y;

    if (thickness <= 0.0f || alpha == 0u)
    {
        return;
    }

    physical_x0 = (float)TitanVideo_MapLogicalX(x0);
    physical_y0 = (float)TitanVideo_MapLogicalY(y0);
    physical_x1 = (float)TitanVideo_MapLogicalX(x1);
    physical_y1 = (float)TitanVideo_MapLogicalY(y1);

    delta_x = physical_x1 - physical_x0;
    delta_y = physical_y1 - physical_y0;
    length = sqrtf((delta_x * delta_x) + (delta_y * delta_y));

    if (length < 0.5f)
    {
        TitanVideo_AppendEllipseFanPhysical(
            physical_x0,
            physical_y0,
            (float)TitanVideo_MapLogicalRadiusX(thickness) * 0.5f,
            (float)TitanVideo_MapLogicalRadiusY(thickness) * 0.5f,
            0.0f,
            (float)(M_PI * 2.0),
            12u,
            red,
            green,
            blue,
            alpha
        );
        return;
    }

    /* The active logical mapping already accounts for the output mode.
     * Build line thickness in framebuffer space so native 720p is not
     * incorrectly treated as a horizontally magnified 640-pixel surface. */
    half_thickness =
        (float)TitanVideo_MapLogicalRadiusX(thickness) *
        0.5f;
    perpendicular_x =
        (-delta_y / length) *
        half_thickness;
    perpendicular_y =
        (delta_x / length) *
        half_thickness;

    TitanVideo_AppendTrianglePhysical(
        physical_x0 + perpendicular_x,
        physical_y0 + perpendicular_y,
        physical_x1 + perpendicular_x,
        physical_y1 + perpendicular_y,
        physical_x1 - perpendicular_x,
        physical_y1 - perpendicular_y,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_AppendTrianglePhysical(
        physical_x0 + perpendicular_x,
        physical_y0 + perpendicular_y,
        physical_x1 - perpendicular_x,
        physical_y1 - perpendicular_y,
        physical_x0 - perpendicular_x,
        physical_y0 - perpendicular_y,
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
        255u
    );
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
    float physical_x0;
    float physical_y0;
    float physical_x1;
    float physical_y1;
    float physical_x2;
    float physical_y2;
    float physical_x3;
    float physical_y3;

    physical_x0 = (float)TitanVideo_MapLogicalX(x0);
    physical_y0 = (float)TitanVideo_MapLogicalY(y0);
    physical_x1 = (float)TitanVideo_MapLogicalX(x1);
    physical_y1 = (float)TitanVideo_MapLogicalY(y1);
    physical_x2 = (float)TitanVideo_MapLogicalX(x2);
    physical_y2 = (float)TitanVideo_MapLogicalY(y2);
    physical_x3 = (float)TitanVideo_MapLogicalX(x3);
    physical_y3 = (float)TitanVideo_MapLogicalY(y3);

    TitanVideo_AppendTrianglePhysical(
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

    TitanVideo_AppendTrianglePhysical(
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
    qword_t *cursor;

    if (
        !g_video.initialized ||
        g_video.upload_packet == 0 ||
        pixels == 0 ||
        width != TITAN_VIDEO_CACHED_LAYER_WIDTH ||
        height != TITAN_VIDEO_CACHED_LAYER_HEIGHT ||
        stride != TITAN_VIDEO_CACHED_LAYER_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    g_video.cached_layer_source = pixels;
    g_video.cached_layer_source_stride = stride;

    if (g_video.compact_resources)
    {
        /* Native 720p uses all VRAM for two page-aligned scanout surfaces
         * and the losslessly indexed resident atlases. The text-input code
         * already has a live-draw fallback when this optional cache is not
         * available. */
        g_video.cached_layer_ready = 0;
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    packet2_reset(
        g_video.upload_packet,
        0
    );

    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)pixels,
        (int)width,
        (int)height,
        GS_PSM_16,
        (int)g_video.cached_layer.address,
        (int)g_video.cached_layer.width
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

    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.cached_layer_ready = 1;
    g_video.transient_atlas_uploaded_generation = 0u;
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
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;

    if (
        !g_video.cached_layer_ready ||
        !g_video.frame_open ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_width > TITAN_VIDEO_CACHED_LAYER_WIDTH ||
        source_height > TITAN_VIDEO_CACHED_LAYER_HEIGHT ||
        alpha == 0u ||
        !TitanVideo_ReserveQwords(16u)
    )
    {
        return;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = 0u;
    clut.psm = 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = CLUT_NO_LOAD;

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = 0.5f;
    rect.t0.v = 0.5f;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = (float)source_width - 0.5f;
    rect.t1.v = (float)source_height - 0.5f;
    rect.color = TitanVideo_MakeColor(
        128,
        128,
        128,
        alpha
    );

    cursor = g_video.draw_packet->next;

    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_CACHED_LAYER)
    {
        cursor = draw_texture_sampling(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &lod
        );
        cursor = draw_texturebuffer(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &g_video.cached_layer,
            &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_CACHED_LAYER;
    }

    cursor = draw_rect_textured(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &rect
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}


static TitanResult TitanVideo_UploadCachedLayerTransient16(
    const unsigned short *pixels
)
{
    qword_t *cursor;

    if (
        !g_video.initialized || g_video.upload_packet == 0 || pixels == 0 ||
        g_video.compact_resources
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)pixels,
        TITAN_VIDEO_CACHED_LAYER_WIDTH,
        TITAN_VIDEO_CACHED_LAYER_HEIGHT,
        GS_PSM_16,
        (int)g_video.cached_layer.address,
        (int)g_video.cached_layer.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }
    g_video.cached_layer_ready = 1;
    g_video.transient_atlas_uploaded_generation = 0u;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_DrawLegacyMediaTile16(
    const unsigned short *pixels,
    unsigned int source_stride,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int tile_width,
    unsigned int tile_height,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    qword_t *cursor;
    unsigned int row;

    if (
        pixels == 0 || tile_width == 0u || tile_height == 0u ||
        tile_width > TITAN_VIDEO_CACHED_LAYER_WIDTH ||
        tile_height > TITAN_VIDEO_CACHED_LAYER_HEIGHT ||
        g_video.compact_resources
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    /* The cached-layer allocation is also the primary-background streaming
     * scratch in 480i/480p. Complete queued sampling before overwriting it. */
    if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_FRAME_END;
    }

    for (row = 0u; row < tile_height; row++)
    {
        unsigned int column;
        const unsigned short *source_row =
            &pixels[(source_y + row) * source_stride + source_x];
        unsigned short *destination_row =
            &g_video_media_psm16[row * tile_width];

        for (column = 0u; column < tile_width; column++)
        {
            destination_row[column] = TitanVideo_Rgb565ToPsm16(source_row[column]);
        }
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video_media_psm16,
        (int)tile_width,
        (int)tile_height,
        GS_PSM_16,
        (int)g_video.cached_layer.address,
        (int)g_video.cached_layer.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.cached_layer_ready = 1;
    g_video.transient_atlas_uploaded_generation = 0u;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    TitanVideo_DrawCachedLayer16(
        x, y, width, height,
        tile_width, tile_height, alpha
    );
    return TITAN_RESULT_OK;
}

static TitanResult TitanVideo_DrawCompactMediaTile16(
    const unsigned short *pixels,
    unsigned int source_stride,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int tile_width,
    unsigned int tile_height,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    unsigned int row;

    if (
        pixels == 0 || tile_width == 0u || tile_height == 0u ||
        tile_width > TITAN_VIDEO_AVATAR_SCRATCH_WIDTH ||
        tile_height > TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    /* Any queued draw using the scratch must complete before its VRAM is
     * overwritten. This is the same ordering rule already used by streamed
     * avatars in native 720p. */
    if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
    {
        return TITAN_ERROR_FRAME_END;
    }

    for (row = 0u; row < tile_height; row++)
    {
        unsigned int column;
        const unsigned short *source_row =
            &pixels[(source_y + row) * source_stride + source_x];
        unsigned short *destination_row =
            &g_video_media_scratch[row * tile_width];

        for (column = 0u; column < tile_width; column++)
        {
            destination_row[column] = TitanVideo_Rgb565ToPsm16(source_row[column]);
        }
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video_media_scratch,
        (int)tile_width,
        (int)tile_height,
        GS_PSM_16,
        (int)g_video.avatar_atlas.address,
        (int)g_video.avatar_atlas.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.avatar_cached_chunk = TITAN_VIDEO_AVATAR_CHUNK_INVALID;
    g_video.transient_atlas_uploaded_generation = 0u;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;

    if (!TitanVideo_ReserveQwords(16u))
    {
        return TITAN_ERROR_FRAME_END;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = 0u;
    clut.psm = 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = CLUT_NO_LOAD;

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = 0.5f;
    rect.t0.v = 0.5f;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = (float)tile_width - 0.5f;
    rect.t1.v = (float)tile_height - 0.5f;
    rect.color = TitanVideo_MakeColor(128, 128, 128, alpha);

    cursor = g_video.draw_packet->next;
    cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
    cursor = draw_texturebuffer(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &g_video.avatar_atlas,
        &clut
    );
    cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
    packet2_update(g_video.draw_packet, cursor);
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_AVATAR_ATLAS;
    return TITAN_RESULT_OK;
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
    unsigned int tile_x;
    unsigned int tile_y;
    int primary_surface;
    int legacy_surface;

    primary_surface =
        width == TITAN_VIDEO_MEDIA_PRIMARY_WIDTH &&
        height == TITAN_VIDEO_MEDIA_PRIMARY_HEIGHT;
    legacy_surface =
        width == TITAN_VIDEO_MEDIA_LEGACY_WIDTH &&
        height == TITAN_VIDEO_MEDIA_LEGACY_HEIGHT;

    if (
        !g_video.initialized || !g_video.frame_open || pixels == 0 ||
        (!primary_surface && !legacy_surface) ||
        stride < width || draw_width <= 0.0f || draw_height <= 0.0f ||
        alpha == 0u
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    if (!g_video.compact_resources)
    {
        /* Keep the old single-transfer fast path for 512x256 fallback art. */
        if (legacy_surface)
        {
            TitanResult result;
            unsigned int row;
            unsigned int column;

            for (row = 0u; row < height; row++)
            {
                const unsigned short *source_row = &pixels[row * stride];
                unsigned short *destination_row = &g_video_media_psm16[row * width];
                for (column = 0u; column < width; column++)
                {
                    destination_row[column] =
                        TitanVideo_Rgb565ToPsm16(source_row[column]);
                }
            }

            result = TitanVideo_UploadCachedLayerTransient16(g_video_media_psm16);
            if (result != TITAN_RESULT_OK)
            {
                return result;
            }
            TitanVideo_DrawCachedLayer16(
                x, y, draw_width, draw_height, width, height, alpha
            );
            return TITAN_RESULT_OK;
        }

        /* 640x480 primary background: stream through the existing 512x256 GS
         * cached-layer allocation in four bounded tiles. This preserves every
         * one of the 480 source lines without allocating extra GS VRAM. */
        for (tile_y = 0u; tile_y < height; tile_y += TITAN_VIDEO_CACHED_LAYER_HEIGHT)
        {
            unsigned int tile_height = height - tile_y;
            if (tile_height > TITAN_VIDEO_CACHED_LAYER_HEIGHT)
                tile_height = TITAN_VIDEO_CACHED_LAYER_HEIGHT;

            for (tile_x = 0u; tile_x < width; tile_x += TITAN_VIDEO_CACHED_LAYER_WIDTH)
            {
                unsigned int tile_width = width - tile_x;
                float destination_x;
                float destination_y;
                float destination_width;
                float destination_height;
                TitanResult result;

                if (tile_width > TITAN_VIDEO_CACHED_LAYER_WIDTH)
                    tile_width = TITAN_VIDEO_CACHED_LAYER_WIDTH;

                destination_x = x + draw_width * ((float)tile_x / (float)width);
                destination_y = y + draw_height * ((float)tile_y / (float)height);
                destination_width = draw_width * ((float)tile_width / (float)width);
                destination_height = draw_height * ((float)tile_height / (float)height);

                result = TitanVideo_DrawLegacyMediaTile16(
                    pixels, stride, tile_x, tile_y, tile_width, tile_height,
                    destination_x, destination_y,
                    destination_width, destination_height, alpha
                );
                if (result != TITAN_RESULT_OK)
                    return result;
            }
        }
        return TITAN_RESULT_OK;
    }

    /* Native 720p deliberately reserves zero extra VRAM for media. Stream
     * either the 640x480 primary frame or 512x256 fallback through the existing
     * 256x64 avatar scratch. The source contract remains 480-line whenever the
     * package provides it; the compositor scales it to the active output. */
    for (tile_y = 0u; tile_y < height; tile_y += TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT)
    {
        unsigned int tile_height;
        tile_height = height - tile_y;
        if (tile_height > TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT)
            tile_height = TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT;

        for (tile_x = 0u; tile_x < width; tile_x += TITAN_VIDEO_AVATAR_SCRATCH_WIDTH)
        {
            unsigned int tile_width;
            float destination_x;
            float destination_y;
            float destination_width;
            float destination_height;
            TitanResult result;

            tile_width = width - tile_x;
            if (tile_width > TITAN_VIDEO_AVATAR_SCRATCH_WIDTH)
                tile_width = TITAN_VIDEO_AVATAR_SCRATCH_WIDTH;

            destination_x = x + draw_width * ((float)tile_x / (float)width);
            destination_y = y + draw_height * ((float)tile_y / (float)height);
            destination_width = draw_width * ((float)tile_width / (float)width);
            destination_height = draw_height * ((float)tile_height / (float)height);

            result = TitanVideo_DrawCompactMediaTile16(
                pixels, stride, tile_x, tile_y, tile_width, tile_height,
                destination_x, destination_y,
                destination_width, destination_height, alpha
            );
            if (result != TITAN_RESULT_OK)
                return result;
        }
    }
    return TITAN_RESULT_OK;
}

TitanResult TitanVideo_UploadAvatarAtlas16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    qword_t *cursor;

    if (
        !g_video.initialized ||
        g_video.upload_packet == 0 ||
        pixels == 0 ||
        width != TITAN_VIDEO_AVATAR_ATLAS_WIDTH ||
        height != TITAN_VIDEO_AVATAR_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_AVATAR_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    g_video.avatar_atlas_source = pixels;
    g_video.avatar_atlas_source_stride = stride;

    if (g_video.compact_resources)
    {
        /* Avatars remain byte-identical in main memory and are streamed to a
         * small PSM16 scratch texture in eight-cell chunks when drawn. */
        g_video.avatar_cached_chunk = TITAN_VIDEO_AVATAR_CHUNK_INVALID;
        g_video.avatar_atlas_ready = 1;
        return TITAN_RESULT_OK;
    }

    packet2_reset(
        g_video.upload_packet,
        0
    );

    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)pixels,
        (int)width,
        (int)height,
        GS_PSM_16,
        (int)g_video.avatar_atlas.address,
        (int)g_video.avatar_atlas.width
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

    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.avatar_atlas_ready = 1;
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
    qword_t *cursor;
    unsigned int row;
    unsigned int column;

    if (
        !g_video.initialized || !g_video.frame_open || pixels == 0 ||
        width != 256u || height != 64u || stride < width || generation == 0u
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }
    if (g_video.transient_atlas_uploaded_generation == generation)
        return TITAN_RESULT_OK;

    if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
        return TITAN_ERROR_FRAME_END;

    /* Asset TIMG pixels are already GS_PSM_16.  The gateway performs the
     * color conversion so navigation never burns EE cycles repacking artwork. */
    (void)row;
    (void)column;

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)pixels,
        (int)width,
        (int)height,
        GS_PSM_16,
        (int)g_video.transient_atlas.address,
        (int)g_video.transient_atlas.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);
    FlushCache(0);
    dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
        return TITAN_ERROR_FRAME_END;

    if (g_video.compact_resources)
        g_video.avatar_cached_chunk = TITAN_VIDEO_AVATAR_CHUNK_INVALID;
    else
        g_video.cached_layer_ready = 0;
    g_video.transient_atlas_uploaded_generation = generation;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return TITAN_RESULT_OK;
}

void TitanVideo_DrawTransientAtlasRegionPsm16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
)
{
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;

    if (
        !g_video.frame_open || g_video.transient_atlas_uploaded_generation == 0u ||
        width <= 0.0f || height <= 0.0f || source_width == 0u || source_height == 0u ||
        source_x + source_width > 256u || source_y + source_height > 64u || alpha == 0u ||
        !TitanVideo_ReserveQwords(16u)
    )
        return;

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;
    clut.address = 0u;
    clut.psm = 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = CLUT_NO_LOAD;

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = (float)source_x + 0.5f;
    rect.t0.v = (float)source_y + 0.5f;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = (float)(source_x + source_width) - 0.5f;
    rect.t1.v = (float)(source_y + source_height) - 0.5f;
    rect.color = TitanVideo_MakeColor(128, 128, 128, alpha);

    cursor = g_video.draw_packet->next;
    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_TRANSIENT_ATLAS)
    {
        cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
        cursor = draw_texturebuffer(
            cursor, TITAN_VIDEO_DRAW_CONTEXT, &g_video.transient_atlas, &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_TRANSIENT_ATLAS;
    }
    cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
    packet2_update(g_video.draw_packet, cursor);
}

TitanResult TitanVideo_DrawAdaptiveImagePsm16(
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
    if (
        !g_video.initialized || !g_video.frame_open || pixels == 0 ||
        width == 0u || height == 0u || width > 256u || height > 128u ||
        stride < width || draw_width <= 0.0f || draw_height <= 0.0f || alpha == 0u
    )
        return TITAN_ERROR_INVALID_ARGUMENT;

    g_video.transient_atlas_uploaded_generation = 0u;
    if (g_video.compact_resources)
    {
        unsigned int tile_y;
        for (tile_y = 0u; tile_y < height; tile_y += 64u)
        {
            unsigned int tile_height = height - tile_y;
            float dy;
            float dh;
            TitanResult result;
            if (tile_height > 64u) tile_height = 64u;
            dy = y + draw_height * ((float)tile_y / (float)height);
            dh = draw_height * ((float)tile_height / (float)height);
            /* Fixed Titan backgrounds are 256 pixels wide and contiguous.
             * Upload the gateway-produced PSM16 tile directly. */
            if (stride != width) return TITAN_ERROR_INVALID_ARGUMENT;
            if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
                return TITAN_ERROR_FRAME_END;
            packet2_reset(g_video.upload_packet, 0);
            {
                qword_t *cursor = g_video.upload_packet->base;
                cursor = draw_texture_transfer(
                    cursor, (void *)&pixels[tile_y * stride], (int)width, (int)tile_height,
                    GS_PSM_16, (int)g_video.avatar_atlas.address,
                    (int)g_video.avatar_atlas.width
                );
                cursor = draw_texture_flush(cursor);
                packet2_update(g_video.upload_packet, cursor);
            }
            FlushCache(0);
            dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
            if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
                return TITAN_ERROR_FRAME_END;
            g_video.avatar_cached_chunk = TITAN_VIDEO_AVATAR_CHUNK_INVALID;
            g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
            {
                clutbuffer_t clut;
                lod_t lod;
                texrect_t rect;
                texbuffer_t texture;
                qword_t *cursor;
                TitanVideo_SetTextureDescriptor(
                    &texture, g_video.avatar_atlas.address, g_video.avatar_atlas.width,
                    GS_PSM_16, width, tile_height
                );
                lod.calculation = LOD_USE_K;
                lod.max_level = 0u;
                lod.mag_filter = LOD_MAG_LINEAR;
                lod.min_filter = LOD_MIN_LINEAR;
                lod.mipmap_select = LOD_MIPMAP_REGISTER;
                lod.l = 0u;
                lod.k = 0.0f;
                clut.address = 0u;
                clut.psm = 0u;
                clut.storage_mode = CLUT_STORAGE_MODE1;
                clut.start = 0u;
                clut.load_method = CLUT_NO_LOAD;
                if (!TitanVideo_ReserveQwords(16u)) return TITAN_ERROR_FRAME_END;
                rect.v0.x = TitanVideo_MapLogicalFloatX(x);
                rect.v0.y = TitanVideo_MapLogicalFloatY(dy);
                rect.v0.z = TITAN_VIDEO_Z;
                rect.t0.u = 0.5f;
                rect.t0.v = 0.5f;
                rect.v1.x = TitanVideo_MapLogicalFloatX(x + draw_width);
                rect.v1.y = TitanVideo_MapLogicalFloatY(dy + dh);
                rect.v1.z = TITAN_VIDEO_Z;
                rect.t1.u = (float)width - 0.5f;
                rect.t1.v = (float)tile_height - 0.5f;
                rect.color = TitanVideo_MakeColor(128, 128, 128, alpha);
                cursor = g_video.draw_packet->next;
                cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
                cursor = draw_texturebuffer(cursor, TITAN_VIDEO_DRAW_CONTEXT, &texture, &clut);
                cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
                packet2_update(g_video.draw_packet, cursor);
            }
            result = TITAN_RESULT_OK;
            if (result != TITAN_RESULT_OK) return result;
        }
        return TITAN_RESULT_OK;
    }
    else
    {
        clutbuffer_t clut;
        lod_t lod;
        texrect_t rect;
        texbuffer_t texture;
        qword_t *cursor;
        unsigned int row;
        unsigned int column;

        if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
            return TITAN_ERROR_FRAME_END;
        (void)row;
        (void)column;
        if (stride != width) return TITAN_ERROR_INVALID_ARGUMENT;
        packet2_reset(g_video.upload_packet, 0);
        cursor = g_video.upload_packet->base;
        cursor = draw_texture_transfer(
            cursor, (void *)pixels, (int)width, (int)height, GS_PSM_16,
            (int)g_video.cached_layer.address, (int)g_video.cached_layer.width
        );
        cursor = draw_texture_flush(cursor);
        packet2_update(g_video.upload_packet, cursor);
        FlushCache(0);
        dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
        if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
            return TITAN_ERROR_FRAME_END;

        TitanVideo_SetTextureDescriptor(
            &texture, g_video.cached_layer.address, g_video.cached_layer.width,
            GS_PSM_16, width, height
        );
        lod.calculation = LOD_USE_K;
        lod.max_level = 0u;
        lod.mag_filter = LOD_MAG_LINEAR;
        lod.min_filter = LOD_MIN_LINEAR;
        lod.mipmap_select = LOD_MIPMAP_REGISTER;
        lod.l = 0u;
        lod.k = 0.0f;
        clut.address = 0u;
        clut.psm = 0u;
        clut.storage_mode = CLUT_STORAGE_MODE1;
        clut.start = 0u;
        clut.load_method = CLUT_NO_LOAD;
        if (!TitanVideo_ReserveQwords(16u)) return TITAN_ERROR_FRAME_END;
        rect.v0.x = TitanVideo_MapLogicalFloatX(x);
        rect.v0.y = TitanVideo_MapLogicalFloatY(y);
        rect.v0.z = TITAN_VIDEO_Z;
        rect.t0.u = 0.5f;
        rect.t0.v = 0.5f;
        rect.v1.x = TitanVideo_MapLogicalFloatX(x + draw_width);
        rect.v1.y = TitanVideo_MapLogicalFloatY(y + draw_height);
        rect.v1.z = TITAN_VIDEO_Z;
        rect.t1.u = (float)width - 0.5f;
        rect.t1.v = (float)height - 0.5f;
        rect.color = TitanVideo_MakeColor(128, 128, 128, alpha);
        cursor = g_video.draw_packet->next;
        cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
        cursor = draw_texturebuffer(cursor, TITAN_VIDEO_DRAW_CONTEXT, &texture, &clut);
        cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
        packet2_update(g_video.draw_packet, cursor);
        g_video.cached_layer_ready = 0;
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
        return TITAN_RESULT_OK;
    }
}

static int TitanVideo_LoadAvatarChunk(
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned int *local_x,
    unsigned int *local_y
)
{
    qword_t *cursor;
    unsigned int chunk_x;
    unsigned int chunk_y;
    unsigned int chunk_key;
    unsigned int row;

    if (!g_video.compact_resources)
    {
        *local_x = source_x;
        *local_y = source_y;
        return 1;
    }

    chunk_x =
        (source_x / TITAN_VIDEO_AVATAR_SCRATCH_WIDTH) *
        TITAN_VIDEO_AVATAR_SCRATCH_WIDTH;
    chunk_y =
        (source_y / TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT) *
        TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT;

    if (
        source_x + source_width >
            chunk_x + TITAN_VIDEO_AVATAR_SCRATCH_WIDTH ||
        source_y + source_height >
            chunk_y + TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT ||
        g_video.avatar_atlas_source == 0
    )
    {
        return 0;
    }

    *local_x = source_x - chunk_x;
    *local_y = source_y - chunk_y;
    chunk_key =
        (chunk_y / TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT) *
        (TITAN_VIDEO_AVATAR_ATLAS_WIDTH /
         TITAN_VIDEO_AVATAR_SCRATCH_WIDTH) +
        (chunk_x / TITAN_VIDEO_AVATAR_SCRATCH_WIDTH);

    if (g_video.avatar_cached_chunk == chunk_key)
    {
        return 1;
    }

    if (TitanVideo_SubmitPacket(0) != TITAN_RESULT_OK)
    {
        return 0;
    }

    for (row = 0u; row < TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT; row++)
    {
        memcpy(
            &g_video_avatar_scratch[
                row * TITAN_VIDEO_AVATAR_SCRATCH_WIDTH
            ],
            &g_video.avatar_atlas_source[
                ((chunk_y + row) * g_video.avatar_atlas_source_stride) +
                chunk_x
            ],
            TITAN_VIDEO_AVATAR_SCRATCH_WIDTH * sizeof(unsigned short)
        );
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video_avatar_scratch,
        TITAN_VIDEO_AVATAR_SCRATCH_WIDTH,
        TITAN_VIDEO_AVATAR_SCRATCH_DATA_HEIGHT,
        GS_PSM_16,
        (int)g_video.avatar_atlas.address,
        (int)g_video.avatar_atlas.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(
        g_video.upload_packet,
        DMA_CHANNEL_GIF,
        1
    );
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return 0;
    }

    g_video.avatar_cached_chunk = chunk_key;
    if (g_video.compact_resources)
        g_video.transient_atlas_uploaded_generation = 0u;
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE;
    return 1;
}

void TitanVideo_DrawAvatarAtlasRegion16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
)
{
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    unsigned int texture_x;
    unsigned int texture_y;

    if (
        !g_video.avatar_atlas_ready ||
        !g_video.frame_open ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_x + source_width > TITAN_VIDEO_AVATAR_ATLAS_WIDTH ||
        source_y + source_height > TITAN_VIDEO_AVATAR_ATLAS_HEIGHT ||
        alpha == 0u
    )
    {
        return;
    }

    if (
        !TitanVideo_LoadAvatarChunk(
            source_x,
            source_y,
            source_width,
            source_height,
            &texture_x,
            &texture_y
        ) ||
        !TitanVideo_ReserveQwords(16u)
    )
    {
        return;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = 0u;
    clut.psm = 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = CLUT_NO_LOAD;

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = (float)texture_x + 0.5f;
    rect.t0.v = (float)texture_y + 0.5f;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = (float)(texture_x + source_width) - 0.5f;
    rect.t1.v = (float)(texture_y + source_height) - 0.5f;
    rect.color = TitanVideo_MakeColor(
        128,
        128,
        128,
        alpha
    );

    cursor = g_video.draw_packet->next;

    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_AVATAR_ATLAS)
    {
        cursor = draw_texture_sampling(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &lod
        );
        cursor = draw_texturebuffer(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &g_video.avatar_atlas,
            &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_AVATAR_ATLAS;
    }

    cursor = draw_rect_textured(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &rect
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}

TitanResult TitanVideo_UploadFontAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    qword_t *cursor;
    unsigned int x;
    unsigned int y;

    if (
        !g_video.initialized ||
        g_video.upload_packet == 0 ||
        pixels == 0 ||
        width != TITAN_VIDEO_FONT_ATLAS_WIDTH ||
        height != TITAN_VIDEO_FONT_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_FONT_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    g_video.font_atlas_source = pixels;
    g_video.font_atlas_source_stride = stride;

    if (g_video.compact_resources)
    {
        for (y = 0u; y < TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT; y++)
        {
            for (x = 0u; x < TITAN_VIDEO_HD_FONT_STORAGE_WIDTH; x++)
            {
                g_video_font_indices[
                    (y * TITAN_VIDEO_HD_FONT_STORAGE_WIDTH) + x
                ] = (unsigned char)(
                    pixels[(y * stride) + x] >> 24u
                );
            }
        }
    }

    packet2_reset(
        g_video.upload_packet,
        0
    );

    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video.compact_resources
            ? (void *)g_video_font_indices
            : (void *)pixels,
        g_video.compact_resources
            ? (int)TITAN_VIDEO_HD_FONT_STORAGE_WIDTH
            : (int)width,
        g_video.compact_resources
            ? (int)TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT
            : (int)height,
        g_video.font_atlas.psm,
        (int)g_video.font_atlas.address,
        (int)g_video.font_atlas.width
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

    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.font_atlas_ready = 1;
    return TITAN_RESULT_OK;
}

void TitanVideo_DrawFontAtlasRegion32(
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
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    float u0;
    float v0;
    float u1;
    float v1;

    if (
        !g_video.font_atlas_ready ||
        !g_video.frame_open ||
        width <= 0.0f ||
        height <= 0.0f ||
        source_width == 0u ||
        source_height == 0u ||
        source_x + source_width > TITAN_VIDEO_FONT_ATLAS_WIDTH ||
        source_y + source_height > TITAN_VIDEO_FONT_ATLAS_HEIGHT ||
        (
            g_video.compact_resources &&
            (
                source_x + source_width >
                    TITAN_VIDEO_HD_FONT_STORAGE_WIDTH ||
                source_y + source_height >
                    TITAN_VIDEO_HD_FONT_STORAGE_HEIGHT
            )
        ) ||
        alpha == 0u ||
        !TitanVideo_ReserveQwords(16u)
    )
    {
        return;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = g_video.compact_resources
        ? g_video.alpha_clut_address
        : 0u;
    clut.psm = g_video.compact_resources
        ? GS_PSM_32
        : 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = g_video.compact_resources
        ? CLUT_LOAD
        : CLUT_NO_LOAD;

    /*
     * Sample inside texel centres so bilinear filtering cannot pull alpha
     * from a neighbouring glyph cell.
     */
    u0 = (float)source_x + 0.5f;
    v0 = (float)source_y + 0.5f;
    u1 = (float)(source_x + source_width) - 0.5f;
    v1 = (float)(source_y + source_height) - 0.5f;
    if (u1 < u0)
    {
        u1 = u0;
    }
    if (v1 < v0)
    {
        v1 = v0;
    }

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = u0;
    rect.t0.v = v0;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = u1;
    rect.t1.v = v1;
    rect.color = TitanVideo_MakeColor(
        red,
        green,
        blue,
        alpha
    );

    cursor = g_video.draw_packet->next;

    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_FONT_ATLAS)
    {
        cursor = draw_texture_sampling(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &lod
        );
        cursor = draw_texturebuffer(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &g_video.font_atlas,
            &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_FONT_ATLAS;
    }

    cursor = draw_rect_textured(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        &rect
    );
    packet2_update(
        g_video.draw_packet,
        cursor
    );
}

TitanResult TitanVideo_UploadUiAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
)
{
    qword_t *cursor;
    unsigned int index;

    if (
        !g_video.initialized ||
        g_video.upload_packet == 0 ||
        pixels == 0 ||
        width != TITAN_VIDEO_UI_ATLAS_WIDTH ||
        height != TITAN_VIDEO_UI_ATLAS_HEIGHT ||
        stride != TITAN_VIDEO_UI_ATLAS_WIDTH
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    g_video.ui_atlas_source = pixels;
    g_video.ui_atlas_source_stride = stride;

    if (g_video.compact_resources)
    {
        for (
            index = 0u;
            index <
                TITAN_VIDEO_UI_ATLAS_WIDTH *
                TITAN_VIDEO_UI_ATLAS_HEIGHT;
            index++
        )
        {
            g_video_ui_indices[index] =
                (unsigned char)(pixels[index] >> 24u);
        }
    }

    packet2_reset(g_video.upload_packet, 0);
    cursor = g_video.upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        g_video.compact_resources
            ? (void *)g_video_ui_indices
            : (void *)pixels,
        (int)width,
        (int)height,
        g_video.ui_atlas.psm,
        (int)g_video.ui_atlas.address,
        (int)g_video.ui_atlas.width
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_video.upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(g_video.upload_packet, DMA_CHANNEL_GIF, 1);
    if (dma_channel_wait(DMA_CHANNEL_GIF, 0) < 0)
    {
        return TITAN_ERROR_FRAME_END;
    }

    g_video.ui_atlas_ready = 1;
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
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    float u0;
    float v0;
    float u1;
    float v1;

    if (
        !g_video.ui_atlas_ready ||
        !g_video.frame_open ||
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0u ||
        source_width == 0u ||
        source_height == 0u ||
        source_x + source_width > TITAN_VIDEO_UI_ATLAS_WIDTH ||
        source_y + source_height > TITAN_VIDEO_UI_ATLAS_HEIGHT ||
        !TitanVideo_ReserveQwords(16u)
    )
    {
        return;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = g_video.compact_resources
        ? g_video.alpha_clut_address
        : 0u;
    clut.psm = g_video.compact_resources
        ? GS_PSM_32
        : 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = g_video.compact_resources
        ? CLUT_LOAD
        : CLUT_NO_LOAD;

    u0 = (float)source_x + 0.5f;
    v0 = (float)source_y + 0.5f;
    u1 = (float)(source_x + source_width) - 0.5f;
    v1 = (float)(source_y + source_height) - 0.5f;
    if (u1 < u0)
    {
        u1 = u0;
    }
    if (v1 < v0)
    {
        v1 = v0;
    }

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = u0;
    rect.t0.v = v0;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = u1;
    rect.t1.v = v1;
    rect.color = TitanVideo_MakeColor(red, green, blue, alpha);

    cursor = g_video.draw_packet->next;
    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_UI_ATLAS)
    {
        cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
        cursor = draw_texturebuffer(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &g_video.ui_atlas,
            &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_UI_ATLAS;
    }

    cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
    packet2_update(g_video.draw_packet, cursor);
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
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    float u0;
    float v0;
    float u1;
    float v1;

    if (
        !g_video.ui_atlas_ready ||
        !g_video.frame_open ||
        width <= 0.0f ||
        height <= 0.0f ||
        alpha == 0u ||
        source_width == 0u ||
        source_height == 0u ||
        source_x + source_width > TITAN_VIDEO_UI_ATLAS_WIDTH ||
        source_y + source_height > TITAN_VIDEO_UI_ATLAS_HEIGHT ||
        !TitanVideo_ReserveQwords(16u)
    )
    {
        return;
    }

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = 0u; /* point/nearest sampling */
    lod.min_filter = 0u; /* point/nearest sampling */
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;

    clut.address = g_video.compact_resources
        ? g_video.alpha_clut_address
        : 0u;
    clut.psm = g_video.compact_resources
        ? GS_PSM_32
        : 0u;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0u;
    clut.load_method = g_video.compact_resources
        ? CLUT_LOAD
        : CLUT_NO_LOAD;

    u0 = (float)source_x + 0.5f;
    v0 = (float)source_y + 0.5f;
    u1 = (float)(source_x + source_width) - 0.5f;
    v1 = (float)(source_y + source_height) - 0.5f;
    if (u1 < u0)
    {
        u1 = u0;
    }
    if (v1 < v0)
    {
        v1 = v0;
    }

    rect.v0.x = TitanVideo_MapLogicalFloatX(x);
    rect.v0.y = TitanVideo_MapLogicalFloatY(y);
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = u0;
    rect.t0.v = v0;
    rect.v1.x = TitanVideo_MapLogicalFloatX(x + width);
    rect.v1.y = TitanVideo_MapLogicalFloatY(y + height);
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = u1;
    rect.t1.v = v1;
    rect.color = TitanVideo_MakeColor(red, green, blue, alpha);

    cursor = g_video.draw_packet->next;
    if (g_video.bound_texture != TITAN_VIDEO_TEXTURE_UI_ATLAS)
    {
        cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
        cursor = draw_texturebuffer(
            cursor,
            TITAN_VIDEO_DRAW_CONTEXT,
            &g_video.ui_atlas,
            &clut
        );
        g_video.bound_texture = TITAN_VIDEO_TEXTURE_UI_ATLAS;
    }

    cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
    packet2_update(g_video.draw_packet, cursor);
}

static TitanResult TitanVideo_ScaleOptimized720pToOutput(void)
{
    clutbuffer_t clut;
    lod_t lod;
    texrect_t rect;
    qword_t *cursor;
    u64 *tag;
    TitanResult result;

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-scale-build-enter", 0);
    }

    if (!g_video.optimized_720p_active || g_video.framebuffer_count != 1u ||
        g_video.composition_framebuffer.width != 960u ||
        g_video.composition_framebuffer.height != 540u ||
        g_video.framebuffers[0].width != 1280u ||
        g_video.framebuffers[0].height != 720u ||
        !TitanVideo_ReserveQwords(32u))
    {
        return TITAN_ERROR_FRAME_END;
    }

    memset(&clut, 0, sizeof(clut));
    memset(&lod, 0, sizeof(lod));
    memset(&rect, 0, sizeof(rect));

    lod.calculation = LOD_USE_K;
    lod.max_level = 0u;
    lod.mag_filter = LOD_MAG_LINEAR;
    lod.min_filter = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0u;
    lod.k = 0.0f;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.load_method = CLUT_NO_LOAD;

    /* Half-texel endpoints sample texel centers and explicit CLAMP prevents
     * the 4:3 scale from bleeding past the 960x540 composition bounds. */
    rect.v0.x = 0.0f;
    rect.v0.y = 0.0f;
    rect.v0.z = TITAN_VIDEO_Z;
    rect.t0.u = 0.5f;
    rect.t0.v = 0.5f;
    rect.v1.x = 1280.0f;
    rect.v1.y = 720.0f;
    rect.v1.z = TITAN_VIDEO_Z;
    rect.t1.u = 959.5f;
    rect.t1.v = 539.5f;
    rect.color = TitanVideo_MakeColor(128u, 128u, 128u, 255u);

    cursor = g_video.draw_packet->next;
    cursor = draw_setup_environment(
        cursor, TITAN_VIDEO_DRAW_CONTEXT, &g_video.framebuffers[0], &g_video.zbuffer
    );
    /* The composition pass is FINISH-fenced by TitanVideo_EndFrame() before
     * this packet is built.  This is a NORMAL packet, so do not use
     * draw_texture_flush(): PS2SDK's helper prepends a DMATAG_END for DMA
     * chain packets.  Emit TEXFLUSH as ordinary GIF AD payload instead. */
    tag = (u64 *)cursor;
    tag[0] = GIF_SET_TAG(1u, 0u, 0u, 0u, GIF_FLG_PACKED, 1u);
    tag[1] = GIF_REG_AD;
    cursor++;
    cursor = TitanVideo_GameSetAd(cursor, 1u, GS_REG_TEXFLUSH);
    tag = (u64 *)cursor;
    tag[0] = GIF_SET_TAG(1u, 0u, 0u, 0u, GIF_FLG_PACKED, 1u);
    tag[1] = GIF_REG_AD;
    cursor++;
    cursor = TitanVideo_GameSetAd(
        cursor,
        GS_SET_CLAMP(1u, 1u, 0u, 959u, 0u, 539u),
        GS_REG_CLAMP_1
    );
    cursor = draw_texture_sampling(cursor, TITAN_VIDEO_DRAW_CONTEXT, &lod);
    cursor = draw_texturebuffer(
        cursor, TITAN_VIDEO_DRAW_CONTEXT, &g_video.composition_texture, &clut
    );
    cursor = draw_rect_textured(cursor, TITAN_VIDEO_DRAW_CONTEXT, &rect);
    packet2_update(g_video.draw_packet, cursor);
    g_video.bound_texture = TITAN_VIDEO_TEXTURE_COMPOSITION;

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-scale-build-done", (int)packet2_get_qw_count(g_video.draw_packet));
        TitanVideo_SwitchProbe("postframe-scale-submit-enter", 0);
    }
    result = TitanVideo_SubmitPacket(1);
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-scale-submit-done", (int)result);
    }
    return result;
}

/* P10.23 known-texture A/B diagnostics.
 *
 * The P10.22 hardware run proved framebuffer/PATH3/basic GS state and the
 * untextured material lane are sound.  P10.23 adds two tiny, deterministic
 * textures allocated before GoldenEye texture churn begins:
 *
 *   - one 16x16 PSM16 red/blue checker;
 *   - one 16x16 RGBA32 four-colour checker which the active pikaVR exact-
 *     palette path should store as PSM8 + RGBA32 CLUT.
 *
 * The PSM16 checker is rendered twice: once through the current ST/Q material
 * lane and once through the older UV/FST=1 lane.  The PSM8 checker is rendered
 * through ST/Q.  This single frame therefore distinguishes:
 *
 *   upload/bind common path  vs  ST/Q packing  vs  pikaVR/CLUT.
 *
 * No filesystem/log write occurs here; the result is intentionally visual so
 * it cannot create the USB activity observed shortly before the later freeze.
 */
static void TitanVideo_GameDiagnosticTextureReset(void)
{
    g_p123_diag_psm16_handle = 0u;
    g_p123_diag_psm8_handle = 0u;
    g_p123_diag_texture_status = 0u;
    g_p130_diag_heartbeat_frame = 0u;
}

static void TitanVideo_GameDiagnosticTextureInit(void)
{
    unsigned short psm16_pixels[16u * 16u];
    unsigned char rgba_pixels[16u * 16u * 4u];
    TitanVideoGameTexture *texture;
    TitanResult result;
    unsigned int x;
    unsigned int y;

    TitanVideo_GameDiagnosticTextureReset();
    if (!g_video.game_mode) return;

    for (y = 0u; y < 16u; y++)
    {
        for (x = 0u; x < 16u; x++)
        {
            unsigned int index = y * 16u + x;
            unsigned int checker = ((x >> 2u) ^ (y >> 2u)) & 1u;
            unsigned char *rgba = &rgba_pixels[index * 4u];

            /* A1R5G5B5 layout used by TitanGePlatform's PSM16 conversion. */
            psm16_pixels[index] = checker ? 0x801fu : 0xfc00u;

            /* Four unmistakable opaque colours.  16x16 + four exact colours
             * is intentionally profitable for pikaVR PSM8+CLUT storage. */
            switch (((x >> 2u) + (y >> 2u)) & 3u)
            {
                case 0u:
                    rgba[0] = 0u;   rgba[1] = 255u; rgba[2] = 0u;   break;
                case 1u:
                    rgba[0] = 255u; rgba[1] = 255u; rgba[2] = 0u;   break;
                case 2u:
                    rgba[0] = 255u; rgba[1] = 255u; rgba[2] = 255u; break;
                default:
                    rgba[0] = 0u;   rgba[1] = 0u;   rgba[2] = 0u;   break;
            }
            rgba[3] = 255u;
        }
    }

    result = TitanVideo_GameUploadTexturePsm16(
        psm16_pixels, 16u, 16u, 16u, &g_p123_diag_psm16_handle
    );
    if (result == TITAN_RESULT_OK && g_p123_diag_psm16_handle != 0u)
    {
        texture = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool, g_p123_diag_psm16_handle
        );
        if (texture != 0 &&
            texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16)
            g_p123_diag_texture_status |= TITAN_P123_DIAG_PSM16_READY;
    }

    result = TitanVideo_GameUploadTextureRgba32(
        rgba_pixels, 16u, 16u, 16u * 4u, &g_p123_diag_psm8_handle
    );
    if (result == TITAN_RESULT_OK && g_p123_diag_psm8_handle != 0u)
    {
        g_p123_diag_texture_status |= TITAN_P123_DIAG_PSM8_READY;
        texture = TitanVideoGameTexturePool_Lookup(
            &g_video.game_texture_pool, g_p123_diag_psm8_handle
        );
        if (texture != 0 &&
            texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
            g_p123_diag_texture_status |= TITAN_P123_DIAG_PSM8_EXACT;
    }
}

static int TitanVideo_GameDiagnosticDrawMaterialTexturedQuad(
    uint32_t texture_handle,
    float x,
    float y,
    float width,
    float height
)
{
    int first;
    int second;

    first = TitanVideo_DrawGameMaterialTrianglePhysical(
        x, y, 0u,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        x + width, y, 0u,
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        x, y + height, 0u,
        0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        0u,0u,0u, 0u,0u,0u,
        texture_handle, 0u
    );
    second = TitanVideo_DrawGameMaterialTrianglePhysical(
        x + width, y, 0u,
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        x + width, y + height, 0u,
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        x, y + height, 0u,
        0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        0u,0u,0u, 0u,0u,0u,
        texture_handle, 0u
    );
    return first && second;
}

static int TitanVideo_GameDiagnosticDrawLegacyUvQuad(
    uint32_t texture_handle,
    float x,
    float y,
    float width,
    float height
)
{
    int first;
    int second;

    first = TitanVideo_DrawGameTexturedTrianglePhysical(
        x, y, 0u, 0.0f, 0.0f,
        x + width, y, 0u, 1.0f, 0.0f,
        x, y + height, 0u, 0.0f, 1.0f,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        texture_handle
    );
    second = TitanVideo_DrawGameTexturedTrianglePhysical(
        x + width, y, 0u, 1.0f, 0.0f,
        x + width, y + height, 0u, 1.0f, 1.0f,
        x, y + height, 0u, 0.0f, 1.0f,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        255u,255u,255u,255u,
        texture_handle
    );
    return first && second;
}

/* P10.22 diagnostic A/B overlay.
 *
 * This is deliberately drawn after the TitanEYE frame and immediately before
 * PATH3 submission/present.  It separates three layers with one hardware run:
 *
 *   1. solid bars: draw_setup_environment + draw_clear only
 *      -> framebuffer / GS environment / PATH3 / present reference;
 *   2. yellow triangle: the untextured Titan Game material packet
 *      -> clip-independent material RGBAQ/XYZ2 reference;
 *   3. the underlying TitanEYE frame remains untouched
 *      -> compare its textured geometry against the two known references.
 *
 * No texture is allocated or sampled by this diagnostic and the original game
 * render-state struct is restored before EndFrame returns.
 */
static void TitanVideo_GameDiagnosticAbOverlay(void)
{
    TitanGameRenderState saved_state;
    TitanGameRenderState diagnostic_state;
    zbuffer_t diagnostic_z;
    qword_t *cursor;

    if (!g_video.game_mode || !g_video.frame_open ||
        g_video.draw_packet == 0 || g_video.packet_error)
        return;

    /* If the game packet is nearly full, ReserveQwords may submit that prefix
     * first.  This is intentional: the diagnostic then becomes the final PATH3
     * packet before present, so it still overlays the game deterministically. */
    if (!TitanVideo_ReserveQwords(128u))
        return;

    memset(&diagnostic_z, 0, sizeof(diagnostic_z));
    diagnostic_z.enable = 0u;
    diagnostic_z.method = 1u;
    diagnostic_z.address = 0u;
    diagnostic_z.zsm = GS_PSMZ_16;
    diagnostic_z.mask = 1u;

    /* Reassert a completely ordinary full-frame GS environment.  This removes
     * the last GoldenEye material's TEST/SCISSOR/Z state from the solid-bar
     * reference and targets the exact framebuffer that EndFrame will present. */
    cursor = g_video.draw_packet->next;
    cursor = draw_setup_environment(
        cursor,
        TITAN_VIDEO_DRAW_CONTEXT,
        TitanVideo_GetActiveDrawFramebuffer(),
        &diagnostic_z
    );

    /* Bottom-right reference panel: black background plus three unmistakable
     * solid bars.  draw_clear emits no texture coordinates and explicitly
     * controls primitive override, so PikaVR/STQ/CLUT cannot affect these. */
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        392.0f, 300.0f, 240.0f, 140.0f, 0, 0, 0);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        400.0f, 308.0f, 68.0f, 28.0f, 255, 0, 255);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        476.0f, 308.0f, 68.0f, 28.0f, 255, 255, 255);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        552.0f, 308.0f, 68.0f, 28.0f, 0, 255, 255);
    packet2_update(g_video.draw_packet, cursor);

    /* A second reference traverses the Titan Game material lane but disables
     * texturing, blending and depth.  If the bars are correct and this triangle
     * is correct too, the remaining corruption is downstream only when texture
     * state/data is introduced. */
    saved_state = g_video.game_render_state;
    memset(&diagnostic_state, 0, sizeof(diagnostic_state));
    diagnostic_state.struct_size = sizeof(diagnostic_state);
    diagnostic_state.viewport_width = g_video.framebuffer_width;
    diagnostic_state.viewport_height = g_video.framebuffer_height;
    diagnostic_state.scissor_width = g_video.framebuffer_width;
    diagnostic_state.scissor_height = g_video.framebuffer_height;
    diagnostic_state.depth_test = 0u;
    diagnostic_state.depth_write = 0u;
    diagnostic_state.depth_compare = TITAN_GAME_DEPTH_ALWAYS;
    diagnostic_state.blend_mode = TITAN_GAME_BLEND_OPAQUE;
    diagnostic_state.combine_mode = TITAN_GAME_COMBINE_SHADE;
    g_video.game_render_state = diagnostic_state;

    (void)TitanVideo_GameAppendMaterialPass(
        0,
        408.0f, 352.0f, 0u, 0.0f, 0.0f, 1.0f,
        520.0f, 352.0f, 0u, 0.0f, 0.0f, 1.0f,
        408.0f, 424.0f, 0u, 0.0f, 0.0f, 1.0f,
        255u, 255u, 0u, 255u,
        255u, 255u, 0u, 255u,
        255u, 255u, 0u, 255u,
        0,
        0
    );

    /* P10.23 second panel, bottom-left.  The coloured borders mirror the
     * P10.22 reference bars and identify each textured lane without fonts:
     *   magenta -> PSM16 through material ST/Q;
     *   white   -> the SAME PSM16 backing through legacy UV/FST=1;
     *   cyan    -> pikaVR RGBA32 -> PSM8+CLUT through material ST/Q. */
    cursor = g_video.draw_packet->next;
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        8.0f, 300.0f, 240.0f, 140.0f, 0, 0, 0);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        16.0f, 308.0f, 68.0f, 68.0f, 255, 0, 255);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        92.0f, 308.0f, 68.0f, 68.0f, 255, 255, 255);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        168.0f, 308.0f, 68.0f, 68.0f, 0, 255, 255);
    packet2_update(g_video.draw_packet, cursor);

    diagnostic_state.combine_mode = TITAN_GAME_COMBINE_TEX0_DECAL;
    diagnostic_state.sampler[0].wrap_s = TITAN_GAME_WRAP_CLAMP;
    diagnostic_state.sampler[0].wrap_t = TITAN_GAME_WRAP_CLAMP;
    diagnostic_state.sampler[0].linear_filter = 0u;
    g_video.game_render_state = diagnostic_state;

    if ((g_p123_diag_texture_status & TITAN_P123_DIAG_PSM16_READY) != 0u)
    {
        (void)TitanVideo_GameDiagnosticDrawMaterialTexturedQuad(
            g_p123_diag_psm16_handle, 20.0f, 312.0f, 60.0f, 60.0f
        );
        (void)TitanVideo_GameDiagnosticDrawLegacyUvQuad(
            g_p123_diag_psm16_handle, 96.0f, 312.0f, 60.0f, 60.0f
        );
    }
    if ((g_p123_diag_texture_status &
            (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT)) ==
        (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT))
    {
        (void)TitanVideo_GameDiagnosticDrawMaterialTexturedQuad(
            g_p123_diag_psm8_handle, 172.0f, 312.0f, 60.0f, 60.0f
        );
    }

    /* Tiny status bars at the base are solid-only.  Green means the expected
     * upload/format exists; red means the diagnostic itself could not create
     * that reference, so a black square must not be misread as render failure. */
    cursor = g_video.draw_packet->next;
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        16.0f, 384.0f, 68.0f, 10.0f,
        (g_p123_diag_texture_status & TITAN_P123_DIAG_PSM16_READY) ? 0 : 255,
        (g_p123_diag_texture_status & TITAN_P123_DIAG_PSM16_READY) ? 255 : 0,
        0);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        92.0f, 384.0f, 68.0f, 10.0f,
        (g_p123_diag_texture_status & TITAN_P123_DIAG_PSM16_READY) ? 0 : 255,
        (g_p123_diag_texture_status & TITAN_P123_DIAG_PSM16_READY) ? 255 : 0,
        0);
    cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
        168.0f, 384.0f, 68.0f, 10.0f,
        ((g_p123_diag_texture_status &
          (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT)) ==
         (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT)) ? 0 : 255,
        ((g_p123_diag_texture_status &
          (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT)) ==
         (TITAN_P123_DIAG_PSM8_READY | TITAN_P123_DIAG_PSM8_EXACT)) ? 255 : 0,
        0);
    packet2_update(g_video.draw_packet, cursor);

    /* P10.30 live GS/present heartbeat.
     *
     * P10.29 proved the EE/RSP/scheduler corridor continues through native
     * gfx task #32.  The remaining ambiguity is whether the visible "freeze"
     * is a true execution stop or only stale/corrupt presentation.  Draw one
     * small solid-only marker at a different X position every EndFrame, after
     * all TitanEYE content and immediately before the packet is submitted.
     *
     * This intentionally uses draw_clear only: no GoldenEye texture, UV/STQ,
     * CLUT, depth or combiner state can affect it.  The adjacent parity block
     * reports the current draw-buffer index (magenta=0, cyan=1). */
    if (TitanVideo_ReserveQwords(40u))
    {
        unsigned int heartbeat_slot = g_p130_diag_heartbeat_frame & 7u;
        float heartbeat_x = 220.0f + (float)(heartbeat_slot * 24u);

        cursor = g_video.draw_packet->next;
        cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
            212.0f, 8.0f, 216.0f, 24.0f, 0, 0, 0);
        cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
            heartbeat_x, 12.0f, 18.0f, 16.0f, 255, 255, 255);
        if ((g_video.drawing_buffer & 1u) == 0u)
        {
            cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
                404.0f, 12.0f, 16.0f, 16.0f, 255, 0, 255);
        }
        else
        {
            cursor = draw_clear(cursor, TITAN_VIDEO_DRAW_CONTEXT,
                404.0f, 12.0f, 16.0f, 16.0f, 0, 255, 255);
        }
        packet2_update(g_video.draw_packet, cursor);
        g_p130_diag_heartbeat_frame++;
    }

    g_video.game_render_state = saved_state;
}

static TitanResult TitanVideo_AbortOpenFrame(TitanResult result)
{
    if (g_video.frame_open)
    {
        /* Close the logical pikaVR/frame lifecycle even though presentation and
         * texture reclamation are intentionally skipped.  In particular, do
         * not reclaim retired game textures while PATH1 completion is unknown. */
        TitanPikaVr_EndRendererFrame(
            g_video.current_frame_qwords >= g_video.current_external_qwords
                ? g_video.current_frame_qwords - g_video.current_external_qwords
                : 0u
        );
        g_video.frame_open = 0;
    }
    return result;
}

TitanResult TitanVideo_EndFrame(void)
{
    TitanResult result;

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-end-enter", (int)packet2_get_qw_count(g_video.draw_packet));
    }

    if (
        !g_video.initialized ||
        g_video.draw_packet == 0 ||
        !g_video.frame_open ||
        g_video.packet_error
    )
    {
        return TitanVideo_AbortOpenFrame(TITAN_ERROR_FRAME_END);
    }

    /* P10.22: append the independent hardware A/B references after all game
     * draws and before the packet is FINISH-fenced/submitted. */
    TitanVideo_GameDiagnosticAbOverlay();

    /* The optimized 720p path renders the shell into a 960x540 GS
     * framebuffer and immediately samples that same local-memory surface as
     * a texture for the 1280x720 presentation pass.  DMA/GIF ordering and
     * TEXFLUSH alone are not a render-target completion barrier on the GS.
     * A FINISH is required here so every composition write is visible before
     * the texture unit starts the scale pass.  The final scale submission
     * retains its own FINISH before PRESENT/VSYNC. */
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-composition-submit-enter", (int)packet2_get_qw_count(g_video.draw_packet));
    }
    result = TitanVideo_SubmitPacket(1);
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-composition-submit-done", (int)result);
    }

    if (result != TITAN_RESULT_OK)
    {
        return TitanVideo_AbortOpenFrame(result);
    }

    if (g_video.optimized_720p_active)
    {
        if (g_video_post_switch_probe_pending)
        {
            TitanVideo_SwitchProbe("postframe-scale-enter", 0);
        }
        result = TitanVideo_ScaleOptimized720pToOutput();
        if (g_video_post_switch_probe_pending)
        {
            TitanVideo_SwitchProbe("postframe-scale-done", (int)result);
        }
        if (result != TITAN_RESULT_OK)
        {
            return TitanVideo_AbortOpenFrame(result);
        }
    }

    g_video.last_frame_qwords = g_video.current_frame_qwords;
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-pikavr-enter", (int)g_video.current_frame_qwords);
    }
    TitanPikaVr_EndRendererFrame(
        g_video.current_frame_qwords >= g_video.current_external_qwords
            ? g_video.current_frame_qwords - g_video.current_external_qwords
            : 0u
    );
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-pikavr-done", 0);
    }
    if (g_video.game_mode)
    {
        TitanPikaVr_RecordBackingReclaimed(
            TitanVideoGameTexturePool_ReclaimRetiredCount(
                &g_video.game_texture_pool
            )
        );
        TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes());
        TitanPikaVr_UpdateTextureUsed(
            TITAN_VIDEO_GAME_TEXTURE_ARENA_BYTES -
            TitanVideoGameTexturePool_GetFreeBytes(&g_video.game_texture_pool)
        );
    }

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-vsync-enter", 0);
    }
    graph_wait_vsync();
    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-vsync-done", 0);
        TitanVideo_SwitchProbe("postframe-present-enter", (int)g_video.framebuffers[0].address);
    }

    if (g_video.optimized_720p_active)
    {
        TitanVideoMode_Present(g_video.framebuffers[0].address);
        g_video.displayed_buffer = 0u;
        g_video.drawing_buffer = 0u;
    }
    else
    {
        TitanVideoMode_Present(
            g_video.framebuffers[g_video.drawing_buffer].address
        );
        g_video.displayed_buffer = g_video.drawing_buffer;
        g_video.drawing_buffer = g_video.displayed_buffer ^ 1u;
    }

    if (g_video_post_switch_probe_pending)
    {
        TitanVideo_SwitchProbe("postframe-present-done", 0);
        TitanVideo_SwitchProbe("postframe-end-done", 0);
        g_video_post_switch_probe_pending = 0;
    }

    g_video.frame_open = 0;
    return TITAN_RESULT_OK;
}

#endif
