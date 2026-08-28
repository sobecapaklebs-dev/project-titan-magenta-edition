#include "titan_wave_vu1.h"

#ifdef TITAN_VIDEO_RENDERER_GS

#include "titan_video.h"
#include "titan_video_memory.h"

#include <dma.h>
#include <draw.h>
#include <draw_sampling.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <packet2.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <tamtypes.h>
#include <vif_registers.h>

#include "generated/titan_wave_silk_texture.h"
#include "generated/titan_wave_vu1_micro.h"

#include <string.h>

#define TITAN_WAVE_TEXTURE_PSM GS_PSM_32
#define TITAN_WAVE_VU1_PI_OVER_64 0.04908738521234052f

#define TITAN_WAVE_VU1_PACKET_QWORDS 256u
#define TITAN_WAVE_VU1_UPLOAD_PACKET_QWORDS 96u
#define TITAN_WAVE_TEXTURE_UPLOAD_PACKET_QWORDS 32u
#define TITAN_WAVE_VU1_DMA_TIMEOUT 8000000
#define TITAN_WAVE_VU1_STATUS_TIMEOUT 8000000u
#define TITAN_WAVE_VU1_MAX_TRANSIENT_FAILURES 3u
#define TITAN_WAVE_VU1_PASSES_PER_LAYER 1u
#define TITAN_WAVE_VU1_PASS_COUNT \
    (TITAN_WAVE_VU1_LAYER_COUNT * TITAN_WAVE_VU1_PASSES_PER_LAYER)
#define TITAN_WAVE_VU1_VERTICES_PER_PASS \
    (TITAN_WAVE_VU1_POINT_COUNT * 2u)

/* VU1 data-memory map, in qwords. */
#define TITAN_WAVE_VU1_ADDR_PARAMETERS 0u
#define TITAN_WAVE_VU1_ADDR_GS_OFFSET 1u
#define TITAN_WAVE_VU1_ADDR_POINTS 16u
#define TITAN_WAVE_VU1_ADDR_COLORS 160u
#define TITAN_WAVE_VU1_ADDR_UVS 240u
#define TITAN_WAVE_VU1_ADDR_PATH1_STATE 384u
#define TITAN_WAVE_VU1_ADDR_VERTEX_GIFTAG 389u
#define TITAN_WAVE_VU1_ADDR_OUTPUT 390u
#define TITAN_WAVE_VU1_PATH1_STATE_QWORDS 6u

typedef char TitanWaveVu1PointsFit[
    TITAN_WAVE_VU1_ADDR_POINTS +
        TITAN_WAVE_VU1_POINT_COUNT * 2u <=
        TITAN_WAVE_VU1_ADDR_COLORS ? 1 : -1
];
typedef char TitanWaveVu1ColorsFit[
    TITAN_WAVE_VU1_ADDR_COLORS +
        TITAN_WAVE_VU1_POINT_COUNT <=
        TITAN_WAVE_VU1_ADDR_UVS ? 1 : -1
];
typedef char TitanWaveVu1UvsFit[
    TITAN_WAVE_VU1_ADDR_UVS +
        TITAN_WAVE_VU1_POINT_COUNT * 2u <=
        TITAN_WAVE_VU1_ADDR_PATH1_STATE ? 1 : -1
];
typedef char TitanWaveVu1OutputFits[
    TITAN_WAVE_VU1_ADDR_OUTPUT +
        TITAN_WAVE_VU1_VERTICES_PER_PASS * 3u <= 1024u ? 1 : -1
];

#define TITAN_WAVE_VU1_MICRO_ADDRESS 0u
#define TITAN_WAVE_VU1_PATH1_QWORDS_PER_PASS \
    (TITAN_WAVE_VU1_PATH1_STATE_QWORDS + \
     TITAN_WAVE_VU1_VERTICES_PER_PASS * 3u)

/* Triangle strip + Gouraud + texture mapping + alpha blending + UV mode.
 *
 * IMPORTANT: AA1 stays disabled for the filled strip.  The wave texture already
 * carries an anti-aliased alpha profile; GS primitive-edge AA on a translucent
 * triangle strip exposes the internal tessellation as bright diagonal seams
 * (the hardware "fish-scale" artifact). */
#define TITAN_WAVE_VU1_PRIM_VALUE \
    ((u64)GS_SET_PRIM( \
        GS_PRIM_TRIANGLE_STRIP, \
        1u, \
        1u, \
        0u, \
        1u, \
        0u, \
        1u, \
        0u, \
        0u \
    ))

typedef struct TitanWaveVu1State
{
    int available;
    unsigned int transient_failures;
    int texture_owned;
    unsigned int texture_address;
    packet2_t *texture_upload_packet;
    packet2_t *upload_packet;
    packet2_t *draw_packet;
} TitanWaveVu1State;

static TitanWaveVu1State g_wave_vu1;

/*
 * REF-tagged data must stay immutable until VIF1 finishes the chain.  Every
 * layer/pass owns a distinct cache-aligned block.
 */
static qword_t g_wave_points[TITAN_WAVE_VU1_LAYER_COUNT]
    [TITAN_WAVE_VU1_POINT_COUNT * 2u] __attribute__((aligned(64)));
static qword_t g_wave_uvs[TITAN_WAVE_VU1_LAYER_COUNT]
    [TITAN_WAVE_VU1_POINT_COUNT * 2u] __attribute__((aligned(64)));
static qword_t g_wave_parameters[TITAN_WAVE_VU1_PASS_COUNT]
    [1] __attribute__((aligned(64)));
static qword_t g_wave_alpha_state[TITAN_WAVE_VU1_PASS_COUNT]
    [1] __attribute__((aligned(64)));
static qword_t g_wave_colors[TITAN_WAVE_VU1_PASS_COUNT]
    [TITAN_WAVE_VU1_POINT_COUNT] __attribute__((aligned(64)));
static qword_t g_wave_gs_offset[1] __attribute__((aligned(64)));
static qword_t g_wave_path1_state[TITAN_WAVE_VU1_PATH1_STATE_QWORDS]
    __attribute__((aligned(64)));

static float TitanWaveVu1_ClampFloat(
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

static unsigned int TitanWaveVu1_ToGsAlpha(unsigned char alpha)
{
    unsigned int converted;

    converted = (((unsigned int)alpha * 128u) + 127u) / 255u;
    return converted > 128u ? 128u : converted;
}

static float TitanWaveVu1_EdgeFade(unsigned int index)
{
    float amount;
    float edge;

    amount = (float)index / (float)(TITAN_WAVE_VU1_POINT_COUNT - 1u);
    edge = amount < 0.5f ? amount : 1.0f - amount;
    edge = TitanWaveVu1_ClampFloat(edge * 9.0f, 0.0f, 1.0f);
    return edge * edge * (3.0f - 2.0f * edge);
}

static float TitanWaveVu1_Shimmer(
    unsigned int index,
    unsigned char phase
)
{
    unsigned int position;
    unsigned int distance;
    float triangle;

    position = (index * 5u + (unsigned int)phase) & 63u;
    distance = position > 32u ? position - 32u : 32u - position;
    triangle = 1.0f - ((float)distance / 32.0f);
    return 0.95f + triangle * 0.05f;
}

static unsigned char TitanWaveVu1_ModulateAlpha(
    unsigned char alpha,
    float amount
)
{
    float value;

    value = (float)alpha * TitanWaveVu1_ClampFloat(amount, 0.0f, 1.0f);
    if (value <= 0.0f)
    {
        return 0u;
    }
    if (value >= 255.0f)
    {
        return 255u;
    }
    return (unsigned char)value;
}

static void TitanWaveVu1_SetFloat4(
    qword_t *destination,
    float x,
    float y,
    float z,
    float w
)
{
    float *values;

    values = (float *)destination;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
}

static void TitanWaveVu1_SetInt4(
    qword_t *destination,
    unsigned int x,
    unsigned int y,
    unsigned int z,
    unsigned int w
)
{
    u32 *values;

    values = (u32 *)destination;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
}

static void TitanWaveVu1_SetAd(
    qword_t *destination,
    u64 value,
    u64 register_address
)
{
    u64 *words;

    words = (u64 *)destination;
    words[0] = value;
    words[1] = register_address;
}

static void TitanWaveVu1_PrepareImmutableState(void)
{
    u64 *tag;

    /* GS raster coordinates use a 2048-pixel origin and 12.4 fixed point. */
    TitanWaveVu1_SetFloat4(
        &g_wave_gs_offset[0],
        2048.0f,
        2048.0f,
        0.0f,
        0.0f
    );

    tag = (u64 *)&g_wave_path1_state[0];
    tag[0] = GIF_SET_TAG(
        4u,
        0u,
        0u,
        0u,
        GIF_FLG_PACKED,
        1u
    );
    tag[1] = (u64)GIF_REG_AD;

    TitanWaveVu1_SetAd(
        &g_wave_path1_state[1],
        GS_SET_TEX0(
            g_wave_vu1.texture_address >> 6u,
            TITAN_WAVE_TEXTURE_WIDTH >> 6u,
            TITAN_WAVE_TEXTURE_PSM,
            7u,
            5u,
            1u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u
        ),
        GS_REG_TEX0_1
    );
    TitanWaveVu1_SetAd(
        &g_wave_path1_state[2],
        GS_SET_TEX1(
            LOD_FORMULAIC,
            0u,
            LOD_MAG_LINEAR,
            LOD_MIN_LINEAR,
            LOD_MIPMAP_REGISTER,
            0u,
            0u
        ),
        GS_REG_TEX1_1
    );
    TitanWaveVu1_SetAd(
        &g_wave_path1_state[3],
        GS_SET_CLAMP(
            WRAP_REPEAT,
            WRAP_CLAMP,
            0u,
            0u,
            0u,
            TITAN_WAVE_TEXTURE_HEIGHT - 1u
        ),
        GS_REG_CLAMP_1
    );
    TitanWaveVu1_SetAd(
        &g_wave_path1_state[4],
        GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u),
        GS_REG_ALPHA_1
    );

    tag = (u64 *)&g_wave_path1_state[5];
    tag[0] = GIF_SET_TAG(
        TITAN_WAVE_VU1_VERTICES_PER_PASS,
        1u,
        1u,
        TITAN_WAVE_VU1_PRIM_VALUE,
        GIF_FLG_PACKED,
        3u
    );
    tag[1] =
        (u64)GIF_REG_UV |
        ((u64)GIF_REG_RGBAQ << 4u) |
        ((u64)GIF_REG_XYZ2 << 8u);
}

static int TitanWaveVu1_UploadTexture(void)
{
    qword_t *cursor;
    int texture_address;

    if (titan_wave_silk_texture_size != TITAN_WAVE_TEXTURE_BYTES)
    {
        return 0;
    }

    texture_address = TitanVideoMemory_FindAddress(
        TITAN_WAVE_TEXTURE_ALLOCATION_NAME
    );
    g_wave_vu1.texture_owned = 0;

    /* Compatibility fallback for isolated tests. In the full renderer this
     * slot is reserved before the dynamic framebuffers, so changing video
     * mode never rewinds the stack allocator over the VU1 texture. */
    if (texture_address < 0)
    {
        texture_address = TitanVideoMemory_Allocate(
            TITAN_WAVE_TEXTURE_ALLOCATION_NAME,
            TITAN_WAVE_TEXTURE_WIDTH,
            TITAN_WAVE_TEXTURE_HEIGHT,
            TITAN_WAVE_TEXTURE_PSM,
            TITAN_VIDEO_MEMORY_ALIGN_BLOCK
        );
        g_wave_vu1.texture_owned = texture_address >= 0;
    }

    if (texture_address < 0)
    {
        return 0;
    }

    g_wave_vu1.texture_address = (unsigned int)texture_address;
    if (g_wave_vu1.texture_upload_packet == 0)
    {
        g_wave_vu1.texture_upload_packet = packet2_create(
            TITAN_WAVE_TEXTURE_UPLOAD_PACKET_QWORDS,
            P2_TYPE_NORMAL,
            P2_MODE_CHAIN,
            0
        );
    }
    if (g_wave_vu1.texture_upload_packet == 0)
    {
        if (g_wave_vu1.texture_owned)
        {
            TitanVideoMemory_Free((unsigned int)texture_address);
        }
        g_wave_vu1.texture_address = 0u;
        g_wave_vu1.texture_owned = 0;
        return 0;
    }

    packet2_reset(g_wave_vu1.texture_upload_packet, 0);
    cursor = g_wave_vu1.texture_upload_packet->base;
    cursor = draw_texture_transfer(
        cursor,
        (void *)titan_wave_silk_texture,
        TITAN_WAVE_TEXTURE_WIDTH,
        TITAN_WAVE_TEXTURE_HEIGHT,
        TITAN_WAVE_TEXTURE_PSM,
        texture_address,
        TITAN_WAVE_TEXTURE_WIDTH
    );
    cursor = draw_texture_flush(cursor);
    packet2_update(g_wave_vu1.texture_upload_packet, cursor);

    FlushCache(0);
    dma_channel_send_packet2(
        g_wave_vu1.texture_upload_packet,
        DMA_CHANNEL_GIF,
        1
    );
    if (dma_channel_wait(DMA_CHANNEL_GIF, TITAN_WAVE_VU1_DMA_TIMEOUT) < 0)
    {
        if (g_wave_vu1.texture_owned)
        {
            TitanVideoMemory_Free((unsigned int)texture_address);
        }
        g_wave_vu1.texture_address = 0u;
        g_wave_vu1.texture_owned = 0;
        return 0;
    }

    return 1;
}

static void TitanWaveVu1_AddStartProgram(packet2_t *packet)
{
    packet2_chain_open_cnt(packet, 0, 0, 0);

    /*
     * FLUSH serializes the previous invocation.  FLUSHE keeps VIF1 blocked
     * until the microprogram reaches its E bit.  TTE makes pad128 mandatory.
     */
    packet2_vif_flush(packet, 0);
    packet2_vif_mscal(packet, TITAN_WAVE_VU1_MICRO_ADDRESS, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_pad128(packet);

    packet2_chain_close_tag(packet);
}

static int TitanWaveVu1_SendPacket(packet2_t *packet)
{
    unsigned int timeout;

    FlushCache(0);
    dma_channel_send_packet2(packet, DMA_CHANNEL_VIF1, 1);

    if (dma_channel_wait(DMA_CHANNEL_VIF1, TITAN_WAVE_VU1_DMA_TIMEOUT) < 0)
    {
        return 0;
    }

    /* VPS = bits 0-1, VEW = bit 2, FQC = bits 24-28. */
    timeout = TITAN_WAVE_VU1_STATUS_TIMEOUT;
    while ((VIF1_STAT & 0x1F000007u) != 0u)
    {
        timeout--;
        if (timeout == 0u)
        {
            return 0;
        }
    }

    return 1;
}

static void TitanWaveVu1_Disable(void)
{
    g_wave_vu1.available = 0;
}

static void TitanWaveVu1_MarkTransientFailure(void)
{
    if (g_wave_vu1.transient_failures < TITAN_WAVE_VU1_MAX_TRANSIENT_FAILURES)
    {
        g_wave_vu1.transient_failures++;
    }
    if (g_wave_vu1.transient_failures >= TITAN_WAVE_VU1_MAX_TRANSIENT_FAILURES)
    {
        TitanWaveVu1_Disable();
    }
}

void TitanWaveVu1_Initialize(void)
{
    memset(&g_wave_vu1, 0, sizeof(g_wave_vu1));

    if (dma_channel_initialize(DMA_CHANNEL_VIF1, 0, 0) < 0)
    {
        return;
    }

    if (!TitanWaveVu1_UploadTexture())
    {
        return;
    }

    TitanWaveVu1_PrepareImmutableState();

    /* VIF opcodes live in the upper half of each DMA tag, so TTE must be on. */
    g_wave_vu1.upload_packet = packet2_create(
        TITAN_WAVE_VU1_UPLOAD_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1
    );
    g_wave_vu1.draw_packet = packet2_create(
        TITAN_WAVE_VU1_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1
    );

    if (g_wave_vu1.upload_packet == 0 || g_wave_vu1.draw_packet == 0)
    {
        TitanWaveVu1_Disable();
        return;
    }

    packet2_reset(g_wave_vu1.upload_packet, 0);
    packet2_vif_add_micro_program(
        g_wave_vu1.upload_packet,
        TITAN_WAVE_VU1_MICRO_ADDRESS,
        (u32 *)(void *)titan_wave_vu1_micro,
        (u32 *)(void *)(titan_wave_vu1_micro + titan_wave_vu1_micro_size)
    );
    packet2_utils_vu_add_unpack_data(
        g_wave_vu1.upload_packet,
        TITAN_WAVE_VU1_ADDR_GS_OFFSET,
        g_wave_gs_offset,
        1u,
        0u
    );
    packet2_utils_vu_add_unpack_data(
        g_wave_vu1.upload_packet,
        TITAN_WAVE_VU1_ADDR_PATH1_STATE,
        g_wave_path1_state,
        TITAN_WAVE_VU1_PATH1_STATE_QWORDS,
        0u
    );
    packet2_utils_vu_add_end_tag(g_wave_vu1.upload_packet);

    if (!TitanWaveVu1_SendPacket(g_wave_vu1.upload_packet))
    {
        TitanWaveVu1_Disable();
        return;
    }

    g_wave_vu1.transient_failures = 0u;
    g_wave_vu1.available = 1;
}

static float TitanWaveVu1_WrapPhaseUnits(float phase)
{
    /* Theme phases are deliberately small, so bounded subtraction avoids
     * pulling libm/fmodf into the EE binary. One complete carrier is 128
     * phase units, matching Titan's historical sine LUT convention. */
    while (phase >= 128.0f)
    {
        phase -= 128.0f;
    }
    while (phase < 0.0f)
    {
        phase += 128.0f;
    }
    return phase;
}

static void TitanWaveVu1_FoldSinPhase(
    float phase_units,
    float *efu_angle,
    float *sign
)
{
    float phase;
    float folded;
    float polarity;

    phase = TitanWaveVu1_WrapPhaseUnits(phase_units);
    polarity = 1.0f;
    if (phase < 32.0f)
    {
        folded = phase;
    }
    else if (phase < 64.0f)
    {
        folded = 64.0f - phase;
    }
    else if (phase < 96.0f)
    {
        folded = phase - 64.0f;
        polarity = -1.0f;
    }
    else
    {
        folded = 128.0f - phase;
        polarity = -1.0f;
    }

    /* ESIN is documented for -pi/2..+pi/2. Folding the full carrier into
     * 0..pi/2 and moving the quadrant sign into the amplitude keeps every
     * EFU input inside that guaranteed range. */
    *efu_angle = folded * TITAN_WAVE_VU1_PI_OVER_64;
    *sign = polarity;
}

static void TitanWaveVu1_PrepareLayerPoints(
    unsigned int layer_index,
    const TitanWaveVu1Layer *layer
)
{
    unsigned int index;
    float scale_x;
    float scale_y;
    float offset_y;
    float amplitude;
    const TitanWaveVu1Point *points;

    points = layer->points;
    scale_x = TitanVideo_GetLogicalScaleX();
    scale_y = TitanVideo_GetLogicalScaleY();
    offset_y = TitanVideo_GetLogicalOffsetY();
    amplitude = TitanWaveVu1_ClampFloat(
        layer->procedural_amplitude,
        0.0f,
        36.0f
    );

    for (index = 0u; index < TITAN_WAVE_VU1_POINT_COUNT; index++)
    {
        float angle_y;
        float angle_x;
        float sign_y;
        float sign_x;
        float phase_y;
        float phase_x;

        phase_y = layer->procedural_phase +
            (float)index * layer->procedural_frequency;
        /* Quadrature carrier: cos(theta) = sin(theta + pi/2). The +32
         * phase-unit offset is one quarter of Titan's 128-unit full cycle. */
        phase_x = phase_y + 32.0f;
        TitanWaveVu1_FoldSinPhase(phase_y, &angle_y, &sign_y);
        TitanWaveVu1_FoldSinPhase(phase_x, &angle_x, &sign_x);

        /* z/w are scratch inputs for the VU1 EFU and never reach XYZ2. */
        TitanWaveVu1_SetFloat4(
            &g_wave_points[layer_index][index * 2u],
            points[index].x * scale_x,
            offset_y + (points[index].y * scale_y),
            angle_y,
            angle_x
        );
        /* normal.z/normal.w carry signed procedural amplitudes. VU1 adds
         * them to center X/Y after ESIN and before rail extrusion. */
        TitanWaveVu1_SetFloat4(
            &g_wave_points[layer_index][index * 2u + 1u],
            points[index].normal_x * scale_x,
            points[index].normal_y * scale_y,
            sign_x * amplitude * 0.42f * scale_x,
            sign_y * amplitude * scale_y
        );
    }
}

static void TitanWaveVu1_PrepareLayerUvs(
    unsigned int layer_index,
    unsigned char phase,
    float texture_u_scale
)
{
    unsigned int index;
    float scroll;

    scroll = ((float)phase / 255.0f) * (float)TITAN_WAVE_TEXTURE_WIDTH;
    texture_u_scale = TitanWaveVu1_ClampFloat(texture_u_scale, 0.25f, 4.0f);

    for (index = 0u; index < TITAN_WAVE_VU1_POINT_COUNT; index++)
    {
        float amount;
        float u;
        unsigned int u_fixed;
        unsigned int top_v_fixed;
        unsigned int bottom_v_fixed;

        amount = (float)index / (float)(TITAN_WAVE_VU1_POINT_COUNT - 1u);
        u = scroll + amount *
            ((float)TITAN_WAVE_TEXTURE_WIDTH * texture_u_scale);
        u_fixed = (unsigned int)(u * 16.0f);
        top_v_fixed = 8u;
        bottom_v_fixed = (TITAN_WAVE_TEXTURE_HEIGHT * 16u) - 8u;

        TitanWaveVu1_SetInt4(
            &g_wave_uvs[layer_index][index * 2u],
            u_fixed,
            top_v_fixed,
            0u,
            0u
        );
        TitanWaveVu1_SetInt4(
            &g_wave_uvs[layer_index][index * 2u + 1u],
            u_fixed,
            bottom_v_fixed,
            0u,
            0u
        );
    }
}

static void TitanWaveVu1_PreparePass(
    unsigned int pass_index,
    const TitanWaveVu1Layer *layer,
    unsigned int local_pass
)
{
    unsigned int index;
    float half_width;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;

    (void)local_pass;

    /*
     * V4 uses one pass per curve.  The 2-D texture already contains a broad
     * halo, a translucent body and a narrow offset ridge, so a second pass
     * would only recreate the doubled laser-like center visible in V3.
     */
    half_width = layer->half_width;
    red = layer->red;
    green = layer->green;
    blue = layer->blue;
    alpha = layer->alpha;

    TitanWaveVu1_SetAd(
        &g_wave_alpha_state[pass_index][0],
        layer->blend_mode == TITAN_WAVE_BLEND_ADDITIVE
            ? GS_SET_ALPHA(0u, 2u, 0u, 1u, 0u)
            : GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u),
        GS_REG_ALPHA_1
    );

    TitanWaveVu1_SetFloat4(
        &g_wave_parameters[pass_index][0],
        -half_width,
        half_width,
        0.0f,
        0.0f
    );

    for (index = 0u; index < TITAN_WAVE_VU1_POINT_COUNT; index++)
    {
        float amount;
        float surface_rise;
        float fresnel;
        unsigned char final_alpha;

        surface_rise = layer->points[index].normal_y;
        if (surface_rise < 0.0f)
        {
            surface_rise = -surface_rise;
        }
        surface_rise += layer->points[index].normal_x < 0.0f
            ? -layer->points[index].normal_x * 0.5f
            : layer->points[index].normal_x * 0.5f;
        fresnel = TitanWaveVu1_ClampFloat(
            layer->fresnel_bias + surface_rise * layer->fresnel_scale,
            0.0f,
            1.0f
        );
        amount = TitanWaveVu1_EdgeFade(index) *
            TitanWaveVu1_Shimmer(
                index,
                (unsigned char)(layer->glow_phase + local_pass * 17u)
            ) * fresnel;
        final_alpha = TitanWaveVu1_ModulateAlpha(alpha, amount);

        TitanWaveVu1_SetInt4(
            &g_wave_colors[pass_index][index],
            red,
            green,
            blue,
            TitanWaveVu1_ToGsAlpha(final_alpha)
        );
    }
}

static void TitanWaveVu1_PrepareData(
    const TitanWaveVu1Layer *layers,
    unsigned int layer_count
)
{
    unsigned int layer_index;

    for (layer_index = 0u; layer_index < layer_count; layer_index++)
    {
        unsigned int local_pass;
        unsigned int pass_base;

        TitanWaveVu1_PrepareLayerPoints(
            layer_index,
            &layers[layer_index]
        );
        TitanWaveVu1_PrepareLayerUvs(
            layer_index,
            layers[layer_index].glow_phase,
            layers[layer_index].texture_u_scale
        );

        pass_base = layer_index * TITAN_WAVE_VU1_PASSES_PER_LAYER;
        for (
            local_pass = 0u;
            local_pass < TITAN_WAVE_VU1_PASSES_PER_LAYER;
            local_pass++
        )
        {
            TitanWaveVu1_PreparePass(
                pass_base + local_pass,
                &layers[layer_index],
                local_pass
            );
        }
    }
}

static void TitanWaveVu1_BuildDrawPacket(unsigned int layer_count)
{
    unsigned int layer_index;

    packet2_reset(g_wave_vu1.draw_packet, 0);
    for (layer_index = 0u; layer_index < layer_count; layer_index++)
    {
        unsigned int local_pass;
        unsigned int pass_base;

        pass_base = layer_index * TITAN_WAVE_VU1_PASSES_PER_LAYER;
        packet2_utils_vu_add_unpack_data(
            g_wave_vu1.draw_packet,
            TITAN_WAVE_VU1_ADDR_POINTS,
            g_wave_points[layer_index],
            TITAN_WAVE_VU1_POINT_COUNT * 2u,
            0u
        );
        packet2_utils_vu_add_unpack_data(
            g_wave_vu1.draw_packet,
            TITAN_WAVE_VU1_ADDR_UVS,
            g_wave_uvs[layer_index],
            TITAN_WAVE_VU1_POINT_COUNT * 2u,
            0u
        );

        for (
            local_pass = 0u;
            local_pass < TITAN_WAVE_VU1_PASSES_PER_LAYER;
            local_pass++
        )
        {
            unsigned int pass_index;

            pass_index = pass_base + local_pass;
            packet2_utils_vu_add_unpack_data(
                g_wave_vu1.draw_packet,
                TITAN_WAVE_VU1_ADDR_PARAMETERS,
                g_wave_parameters[pass_index],
                1u,
                0u
            );
            packet2_utils_vu_add_unpack_data(
                g_wave_vu1.draw_packet,
                TITAN_WAVE_VU1_ADDR_COLORS,
                g_wave_colors[pass_index],
                TITAN_WAVE_VU1_POINT_COUNT,
                0u
            );
            packet2_utils_vu_add_unpack_data(
                g_wave_vu1.draw_packet,
                TITAN_WAVE_VU1_ADDR_PATH1_STATE + 4u,
                g_wave_alpha_state[pass_index],
                1u,
                0u
            );
            TitanWaveVu1_AddStartProgram(g_wave_vu1.draw_packet);
        }
    }
    packet2_utils_vu_add_end_tag(g_wave_vu1.draw_packet);
}

int TitanWaveVu1_RebindVideoResources(void)
{
    if (g_wave_vu1.upload_packet == 0 || g_wave_vu1.draw_packet == 0)
    {
        return 0;
    }
    if (!TitanWaveVu1_UploadTexture())
    {
        TitanWaveVu1_Disable();
        return 0;
    }

    TitanWaveVu1_PrepareImmutableState();
    packet2_reset(g_wave_vu1.upload_packet, 0);
    packet2_vif_add_micro_program(
        g_wave_vu1.upload_packet,
        TITAN_WAVE_VU1_MICRO_ADDRESS,
        (u32 *)(void *)titan_wave_vu1_micro,
        (u32 *)(void *)(titan_wave_vu1_micro + titan_wave_vu1_micro_size)
    );
    packet2_utils_vu_add_unpack_data(
        g_wave_vu1.upload_packet, TITAN_WAVE_VU1_ADDR_GS_OFFSET,
        g_wave_gs_offset, 1u, 0u
    );
    packet2_utils_vu_add_unpack_data(
        g_wave_vu1.upload_packet, TITAN_WAVE_VU1_ADDR_PATH1_STATE,
        g_wave_path1_state, TITAN_WAVE_VU1_PATH1_STATE_QWORDS, 0u
    );
    if (!TitanWaveVu1_SendPacket(g_wave_vu1.upload_packet))
    {
        TitanWaveVu1_Disable();
        return 0;
    }
    g_wave_vu1.available = 1;
    g_wave_vu1.transient_failures = 0u;
    return 1;
}

int TitanWaveVu1_Draw(
    const TitanWaveVu1Layer *layers,
    unsigned int layer_count
)
{
    unsigned int submitted_passes;

    if (
        !g_wave_vu1.available ||
        layers == 0 ||
        layer_count == 0u ||
        layer_count > TITAN_WAVE_VU1_LAYER_COUNT
    )
    {
        return 0;
    }

    TitanWaveVu1_PrepareData(layers, layer_count);
    TitanWaveVu1_BuildDrawPacket(layer_count);

    /* PATH3 background restore must finish before the PATH1 ribbons. */
    if (TitanVideo_FlushForPath1() != TITAN_RESULT_OK)
    {
        TitanWaveVu1_MarkTransientFailure();
        return 0;
    }

    if (!TitanWaveVu1_SendPacket(g_wave_vu1.draw_packet))
    {
        TitanWaveVu1_MarkTransientFailure();
        return 0;
    }

    /* A PATH1 pass owns ALPHA/TEX0/TEX1/CLAMP while drawing the ribbons.
     * Restore the canonical PATH3 environment before any shell primitive or
     * texture is appended, otherwise the wave blend leaks into the UI. */
    if (TitanVideo_RestoreAfterPath1() != TITAN_RESULT_OK)
    {
        TitanWaveVu1_MarkTransientFailure();
        return 0;
    }

    g_wave_vu1.transient_failures = 0u;
    submitted_passes = layer_count * TITAN_WAVE_VU1_PASSES_PER_LAYER;
    TitanVideo_AccountExternalQwords(
        submitted_passes * TITAN_WAVE_VU1_PATH1_QWORDS_PER_PASS
    );
    return 1;
}

int TitanWaveVu1_IsAvailable(void)
{
    return g_wave_vu1.available;
}

#else

void TitanWaveVu1_Initialize(void)
{
}

int TitanWaveVu1_RebindVideoResources(void)
{
    return 0;
}

int TitanWaveVu1_Draw(
    const TitanWaveVu1Layer *layers,
    unsigned int layer_count
)
{
    (void)layers;
    (void)layer_count;
    return 0;
}

int TitanWaveVu1_IsAvailable(void)
{
    return 0;
}

#endif
