#include "titan_game_vu1.h"

#ifdef TITAN_VIDEO_RENDERER_GS

#include "titan_video.h"
#include "titan_video_game_math.h"

#include <dma.h>
#include <draw.h>
#include <draw_sampling.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <math.h>
#include <stdint.h>
#include <packet2.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <string.h>
#include <tamtypes.h>
#include <vif_registers.h>

#include "generated/titan_game_vu1_micro.h"

extern void SyncDCache(void *start, void *end);

#define TITAN_GAME_VU1_MICRO_ADDRESS 256u
#define TITAN_GAME_VU1_MICRO_INSTRUCTION_CAPACITY 2048u
#define TITAN_GAME_VU1_DRAW_PACKET_QWORDS 128u
#define TITAN_GAME_VU1_UPLOAD_PACKET_QWORDS 96u
#define TITAN_GAME_VU1_DMA_TIMEOUT 8000000
#define TITAN_GAME_VU1_STATUS_TIMEOUT 8000000u

/* packet2 chain mode is created with TTE=1.  In current PS2SDK each REF+
 * STCYCL+UNPACK helper occupies exactly one local qword, while its payload is
 * referenced externally.  Draw uses five such UNPACKs, a two-qword
 * FLUSH/MSCAL/FLUSHE launch block, and one aligned END tag.  Micro upload uses
 * one local qword per <=256-instruction MPG REF plus one END tag.  Keep these
 * as compile-time capacity proofs because packet2_advance_next itself has no
 * bounds check. */
#define TITAN_GAME_VU1_DRAW_UNPACK_QWORDS 5u
#define TITAN_GAME_VU1_DRAW_LAUNCH_QWORDS 2u
#define TITAN_GAME_VU1_DRAW_END_QWORDS 1u
#define TITAN_GAME_VU1_DRAW_REQUIRED_QWORDS \
    (TITAN_GAME_VU1_DRAW_UNPACK_QWORDS + \
     TITAN_GAME_VU1_DRAW_LAUNCH_QWORDS + TITAN_GAME_VU1_DRAW_END_QWORDS)
#define TITAN_GAME_VU1_UPLOAD_MAX_INSTRUCTIONS \
    (TITAN_GAME_VU1_MICRO_INSTRUCTION_CAPACITY - TITAN_GAME_VU1_MICRO_ADDRESS)
#define TITAN_GAME_VU1_UPLOAD_MAX_MPG_BLOCKS \
    ((TITAN_GAME_VU1_UPLOAD_MAX_INSTRUCTIONS + 255u) / 256u)
#define TITAN_GAME_VU1_UPLOAD_REQUIRED_QWORDS \
    (TITAN_GAME_VU1_UPLOAD_MAX_MPG_BLOCKS + 1u)
#define TITAN_GAME_VU1_VIF_IDLE_MASK 0x1F00000Fu
#define TITAN_GAME_VU1_GIF_PATH_IDLE_MASK 0x00000F00u

/* EE hardware registers used only by the fault-release path.  Normal packet
 * submission continues to use PS2SDK's VIF1_STAT/dma_channel_* interfaces. */
#if defined(__EE__) || defined(_EE)
#define TITAN_GAME_VU1_GIF_STAT_REG \
    (*(volatile u32 *)(uintptr_t)0x10003020u)
#define TITAN_GAME_VU1_VIF1_FBRST_REG \
    (*(volatile u32 *)(uintptr_t)0x10003C10u)
#endif

#define TITAN_GAME_VU1_ADDR_VIEWPORT_SCALE 0u
#define TITAN_GAME_VU1_ADDR_VIEWPORT_OFFSET 1u
#define TITAN_GAME_VU1_ADDR_CONSTANTS 2u
#define TITAN_GAME_VU1_ADDR_VERTEX_COUNT 3u
#define TITAN_GAME_VU1_ADDR_CLIP 16u
#define TITAN_GAME_VU1_ADDR_ST 112u
#define TITAN_GAME_VU1_ADDR_RGBA 208u
#define TITAN_GAME_VU1_ADDR_PATH1 320u
#define TITAN_GAME_VU1_ADDR_OUTPUT 329u
#define TITAN_GAME_VU1_PATH1_QWORDS 9u
#define TITAN_GAME_VU1_OUTPUT_QWORDS_PER_VERTEX 3u

/* Compile-time proof that inputs and generated PATH1 stream fit VU1's 1024
 * qword data memory. */
typedef char TitanGameVu1ClipFits[
    TITAN_GAME_VU1_ADDR_CLIP + TITAN_GAME_VU1_MAX_VERTICES <=
        TITAN_GAME_VU1_ADDR_ST ? 1 : -1];
typedef char TitanGameVu1StFits[
    TITAN_GAME_VU1_ADDR_ST + TITAN_GAME_VU1_MAX_VERTICES <=
        TITAN_GAME_VU1_ADDR_RGBA ? 1 : -1];
typedef char TitanGameVu1RgbaFits[
    TITAN_GAME_VU1_ADDR_RGBA + TITAN_GAME_VU1_MAX_VERTICES <=
        TITAN_GAME_VU1_ADDR_PATH1 ? 1 : -1];
typedef char TitanGameVu1Path1Fits[
    TITAN_GAME_VU1_ADDR_PATH1 + TITAN_GAME_VU1_PATH1_QWORDS <=
        TITAN_GAME_VU1_ADDR_OUTPUT ? 1 : -1];
typedef char TitanGameVu1OutputFits[
    TITAN_GAME_VU1_ADDR_OUTPUT +
        TITAN_GAME_VU1_MAX_VERTICES * TITAN_GAME_VU1_OUTPUT_QWORDS_PER_VERTEX <=
        1024u ? 1 : -1];
typedef char TitanGameVu1DrawPacketFits[
    TITAN_GAME_VU1_DRAW_REQUIRED_QWORDS <= TITAN_GAME_VU1_DRAW_PACKET_QWORDS
        ? 1 : -1];
typedef char TitanGameVu1UploadPacketFits[
    TITAN_GAME_VU1_UPLOAD_REQUIRED_QWORDS <= TITAN_GAME_VU1_UPLOAD_PACKET_QWORDS
        ? 1 : -1];

typedef struct TitanGameVu1State
{
    int available;
    int faulted;
    unsigned int transient_failures;
    packet2_t *upload_packet;
    packet2_t *draw_packet;
} TitanGameVu1State;

static TitanGameVu1State g_game_vu1;
static qword_t g_game_vu1_params[4] __attribute__((aligned(64)));
static qword_t g_game_vu1_clip[TITAN_GAME_VU1_MAX_VERTICES]
    __attribute__((aligned(64)));
static qword_t g_game_vu1_st[TITAN_GAME_VU1_MAX_VERTICES]
    __attribute__((aligned(64)));
static qword_t g_game_vu1_rgba[TITAN_GAME_VU1_MAX_VERTICES]
    __attribute__((aligned(64)));
static qword_t g_game_vu1_path1[TITAN_GAME_VU1_PATH1_QWORDS]
    __attribute__((aligned(64)));

static void TitanGameVu1_SetFloat4(
    qword_t *destination,
    float x,
    float y,
    float z,
    float w
)
{
    float *values = (float *)destination;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
}

static void TitanGameVu1_SetInt4(
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

static void TitanGameVu1_SetAd(qword_t *destination, u64 value, u64 reg)
{
    u64 *words = (u64 *)destination;
    words[0] = value;
    words[1] = reg;
}

static int TitanGameVu1_WrapMode(unsigned char mode)
{
    return mode == TITAN_GAME_WRAP_CLAMP ? WRAP_CLAMP : WRAP_REPEAT;
}

static int TitanGameVu1_TexturePsm(const TitanVideoGameTexture *texture)
{
    if (texture == 0) return GS_PSM_16;
    if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32)
        return GS_PSM_32;
    if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        return GS_PSM_8;
    return GS_PSM_16;
}

static int TitanGameVu1_TextureEligible(
    const TitanVideoGameTexture *texture
)
{
    unsigned int expected_buffer_width;

    if (texture == 0) return 1;
    if (texture->state != TITAN_VIDEO_GAME_TEXTURE_LIVE ||
        texture->width == 0u || texture->height == 0u ||
        texture->width > TITAN_VIDEO_GAME_TEXTURE_MAX_WIDTH ||
        texture->height > TITAN_VIDEO_GAME_TEXTURE_MAX_HEIGHT)
        return 0;
    if (texture->format != TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM16 &&
        texture->format != TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32 &&
        texture->format != TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        return 0;

    expected_buffer_width = TitanVideoGameTexturePool_GetBufferWidthForFormat(
        texture->width, texture->format);
    if (expected_buffer_width == 0u ||
        texture->buffer_width != expected_buffer_width ||
        texture->storage_bytes == 0u || texture->texture_bytes == 0u)
        return 0;
    if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
    {
        if (texture->clut_bytes != TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES ||
            texture->clut_offset_bytes != texture->texture_bytes)
            return 0;
    }
    else if (texture->clut_bytes != 0u)
    {
        return 0;
    }
    return 1;
}

static int TitanGameVu1_VertexFiniteGuard(
    const TitanGameMaterialVertex *vertex
)
{
    float guard;

    if (vertex == 0 || !isfinite(vertex->x) || !isfinite(vertex->y) ||
        !isfinite(vertex->z) || !isfinite(vertex->w) ||
        vertex->w < 0.0001f)
        return 0;

    guard = vertex->w * 2.0f;
    /* X/Y must fit the established GS guard-band. Z is depth-clamped and is
     * therefore not a visibility guard; PrepareVertices clamps it to +/-W
     * before VU1 performs the reciprocal. */
    return vertex->x >= -guard && vertex->x <= guard &&
        vertex->y >= -guard && vertex->y <= guard;
}

static unsigned char TitanGameVu1_FogChannel(
    unsigned char source,
    unsigned char target,
    unsigned char fog
)
{
    unsigned int inverse = 255u - (unsigned int)fog;
    return (unsigned char)(((unsigned int)source * inverse +
        (unsigned int)target * (unsigned int)fog + 127u) / 255u);
}

static int TitanGameVu1_StateEligible(const TitanGameRenderState *state)
{
    float min_x, max_x, min_y, max_y;

    if (state == 0 || state->viewport_width == 0u ||
        state->viewport_height == 0u || state->scissor_width == 0u ||
        state->scissor_height == 0u)
        return 0;

    /* The EE fallback saturates XY to the GS 12-bit screen domain. VU1 emits
     * FTOI4 directly, so only use the fast path when the complete established
     * +/-2 NDC guard band maps without saturation/wrap. */
    min_x = (float)state->viewport_x - 0.5f * (float)state->viewport_width;
    max_x = (float)state->viewport_x + 1.5f * (float)state->viewport_width;
    min_y = (float)state->viewport_y - 0.5f * (float)state->viewport_height;
    max_y = (float)state->viewport_y + 1.5f * (float)state->viewport_height;
    if (min_x < -2048.0f || min_y < -2048.0f ||
        max_x > 2047.9375f || max_y > 2047.9375f)
        return 0;

    if ((unsigned int)state->scissor_x + state->scissor_width > 2048u ||
        (unsigned int)state->scissor_y + state->scissor_height > 2048u)
        return 0;
    return 1;
}

static int TitanGameVu1_BatchEligible(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count
)
{
    unsigned int index;

    if (vertices == 0 || vertex_count == 0u || vertex_count % 3u != 0u)
        return 0;

    /* Native submission is intentionally all-or-nothing per producer batch.
     * Silently filtering one triangle here would make the caller believe the
     * whole material batch was consumed and would turn a guard-band miss into
     * a geometry hole.  If even one vertex is not representable by the VU1/GS
     * fast path, the complete batch falls back to the proven EE clipper in the
     * original order. */
    for (index = 0u; index < vertex_count; index++)
    {
        if (!TitanGameVu1_VertexFiniteGuard(&vertices[index]))
            return 0;
    }
    return 1;
}

static unsigned int TitanGameVu1_PrepareVertices(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    const TitanGameRenderState *state,
    const TitanVideoGameTexture *texture,
    int second_pass
)
{
    unsigned int source_base;
    unsigned int output = 0u;
    float texture_scale_s = 1.0f;
    float texture_scale_t = 1.0f;

    if (texture != 0)
    {
        texture_scale_s = (float)texture->width /
            (float)TitanVideoGame_NextPowerOfTwo(texture->width);
        texture_scale_t = (float)texture->height /
            (float)TitanVideoGame_NextPowerOfTwo(texture->height);
    }

    for (source_base = 0u; source_base < vertex_count; source_base += 3u)
    {
        unsigned int local;

        for (local = 0u; local < 3u; local++)
        {
            const TitanGameMaterialVertex *source =
                &vertices[source_base + local];
            unsigned int color = source->rgba;
            unsigned char red = (unsigned char)(color >> 24u);
            unsigned char green = (unsigned char)(color >> 16u);
            unsigned char blue = (unsigned char)(color >> 8u);
            unsigned char alpha = (unsigned char)color;
            float s = second_pass ? source->s1 : source->s0;
            float t = second_pass ? source->t1 : source->t0;

            if (second_pass)
            {
                red = 255u;
                green = 255u;
                blue = 255u;
                alpha = source->lod_fraction;
            }
            else if (state->fog_enabled && source->fog != 0u)
            {
                red = TitanGameVu1_FogChannel(
                    red, (unsigned char)(state->fog_rgba >> 24u), source->fog);
                green = TitanGameVu1_FogChannel(
                    green, (unsigned char)(state->fog_rgba >> 16u), source->fog);
                blue = TitanGameVu1_FogChannel(
                    blue, (unsigned char)(state->fog_rgba >> 8u), source->fog);
            }

            {
                float clip_z = source->z;
                if (clip_z < -source->w) clip_z = -source->w;
                if (clip_z > source->w) clip_z = source->w;
                TitanGameVu1_SetFloat4(
                    &g_game_vu1_clip[output],
                    source->x, source->y, clip_z, source->w);
            }
            TitanGameVu1_SetFloat4(
                &g_game_vu1_st[output],
                s * texture_scale_s,
                t * texture_scale_t,
                1.0f,
                0.0f);
            TitanGameVu1_SetFloat4(
                &g_game_vu1_rgba[output],
                (float)red,
                (float)green,
                (float)blue,
                (float)TitanVideoGame_ToGsAlpha(alpha));
            output++;
        }
    }

    return output;
}

static void TitanGameVu1_PrepareParams(
    const TitanGameRenderState *state,
    unsigned int vertex_count
)
{
    float half_width = (float)state->viewport_width * 0.5f;
    float half_height = (float)state->viewport_height * 0.5f;

    TitanGameVu1_SetFloat4(
        &g_game_vu1_params[0],
        half_width,
        -half_height,
        -32767.5f,
        0.0f);
    TitanGameVu1_SetFloat4(
        &g_game_vu1_params[1],
        (float)state->viewport_x + half_width + 2048.0f,
        (float)state->viewport_y + half_height + 2048.0f,
        32767.5f,
        0.0f);
    TitanGameVu1_SetFloat4(
        &g_game_vu1_params[2],
        0.03125f,
        0.5f,
        1.0f,
        -1.0f);
    TitanGameVu1_SetInt4(&g_game_vu1_params[3], vertex_count, 0u, 0u, 0u);
}

static void TitanGameVu1_PreparePath1State(
    const TitanGameRenderState *state,
    const TitanVideoGameTexture *texture,
    unsigned int texture_arena_address,
    unsigned int zbuffer_address,
    unsigned int vertex_count,
    int second_pass
)
{
    u64 *tag;
    unsigned int z_test;
    unsigned int alpha_test;
    u64 blend_mode;
    unsigned int sampler_index = second_pass ? 1u : 0u;
    unsigned int x0 = (unsigned int)state->scissor_x;
    unsigned int y0 = (unsigned int)state->scissor_y;
    unsigned int x1 = x0 + state->scissor_width - 1u;
    unsigned int y1 = y0 + state->scissor_height - 1u;
    u64 tex0 = 0u;
    u64 tex1;
    u64 clamp;

    z_test = state->depth_test
        ? (state->depth_compare == TITAN_GAME_DEPTH_GREATER ? 3u :
           state->depth_compare == TITAN_GAME_DEPTH_GEQUAL ? 2u : 1u)
        : 1u;
    alpha_test = state->alpha_compare ? 5u : 1u;

    if (second_pass || state->blend_mode == TITAN_GAME_BLEND_ALPHA)
        blend_mode = GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u);
    else if (state->blend_mode == TITAN_GAME_BLEND_ADDITIVE)
        blend_mode = GS_SET_ALPHA(0u, 2u, 0u, 1u, 0u);
    else if (state->blend_mode == TITAN_GAME_BLEND_MODULATE)
        blend_mode = GS_SET_ALPHA(0u, 2u, 1u, 2u, 0u);
    else
        blend_mode = GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u);

    clamp = GS_SET_CLAMP(
        TitanGameVu1_WrapMode(state->sampler[sampler_index].wrap_s),
        TitanGameVu1_WrapMode(state->sampler[sampler_index].wrap_t),
        0u, texture != 0 && texture->width != 0u ? texture->width - 1u : 0u,
        0u, texture != 0 && texture->height != 0u ? texture->height - 1u : 0u);

    tex1 = GS_SET_TEX1(
        LOD_FORMULAIC,
        0u,
        state->sampler[sampler_index].linear_filter ? LOD_MAG_LINEAR : LOD_MAG_NEAREST,
        state->sampler[sampler_index].linear_filter ? LOD_MIN_LINEAR : LOD_MIN_NEAREST,
        LOD_MIPMAP_REGISTER,
        0u,
        0u);

    /* Host GS macro stubs intentionally collapse register expressions. Keep
     * target-only operands visible under -Werror without weakening the gate. */
    (void)z_test;
    (void)alpha_test;
    (void)x1;
    (void)y1;
    (void)sampler_index;
    (void)zbuffer_address;

    if (texture != 0)
    {
        unsigned int texture_address = texture_arena_address +
            texture->offset_bytes / 4u;
        unsigned int clut_address = 0u;
        unsigned int clut_psm = 0u;
        unsigned int clut_storage = CLUT_STORAGE_MODE1;
        unsigned int clut_load = CLUT_NO_LOAD;

        if (texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT)
        {
            clut_address = texture_arena_address +
                texture->offset_bytes / 4u +
                texture->clut_offset_bytes / 4u;
            clut_psm = GS_PSM_32;
            clut_load = CLUT_LOAD;
        }

        (void)texture_address;
        (void)clut_address;
        (void)clut_psm;
        (void)clut_storage;
        (void)clut_load;

        tex0 = GS_SET_TEX0(
            texture_address >> 6u,
            texture->buffer_width >> 6u,
            TitanGameVu1_TexturePsm(texture),
            draw_log2(texture->width),
            draw_log2(texture->height),
            TEXTURE_COMPONENTS_RGBA,
            state->combine_mode == TITAN_GAME_COMBINE_TEX0_DECAL
                ? TEXTURE_FUNCTION_DECAL : TEXTURE_FUNCTION_MODULATE,
            clut_address >> 6u,
            clut_psm,
            clut_storage,
            0u,
            clut_load);
    }

    tag = (u64 *)&g_game_vu1_path1[0];
    tag[0] = GIF_SET_TAG(7u, 0u, 0u, 0u, GIF_FLG_PACKED, 1u);
    tag[1] = GIF_REG_AD;

    TitanGameVu1_SetAd(
        &g_game_vu1_path1[1],
        GS_SET_SCISSOR(x0, x1, y0, y1),
        GS_REG_SCISSOR_1);
    TitanGameVu1_SetAd(
        &g_game_vu1_path1[2],
        GS_SET_TEST(
            state->alpha_compare ? 1u : 0u,
            alpha_test,
            TitanVideoGame_ToGsAlpha(state->alpha_threshold),
            0u,
            0u,
            0u,
            state->depth_test ? 1u : 0u,
            z_test),
        GS_REG_TEST_1);
    TitanGameVu1_SetAd(
        &g_game_vu1_path1[3],
        GS_SET_ZBUF(
            zbuffer_address >> 11u,
            GS_PSMZ_16,
            (!state->depth_write || second_pass) ? 1u : 0u),
        GS_REG_ZBUF_1);
    TitanGameVu1_SetAd(&g_game_vu1_path1[4], blend_mode, GS_REG_ALPHA_1);
    TitanGameVu1_SetAd(&g_game_vu1_path1[5], clamp, GS_REG_CLAMP_1);
    TitanGameVu1_SetAd(&g_game_vu1_path1[6], tex1, GS_REG_TEX1_1);
    TitanGameVu1_SetAd(&g_game_vu1_path1[7], tex0, GS_REG_TEX0_1);

    tag = (u64 *)&g_game_vu1_path1[8];
    tag[0] = GIF_SET_TAG(
        vertex_count,
        1u,
        1u,
        GS_SET_PRIM(
            3u,
            1u,
            texture != 0 ? 1u : 0u,
            0u,
            second_pass || state->blend_mode != TITAN_GAME_BLEND_OPAQUE ? 1u : 0u,
            0u,
            0u,
            0u,
            0u),
        GIF_FLG_PACKED,
        3u);
    tag[1] = (u64)GIF_REG_ST |
        ((u64)GIF_REG_RGBAQ << 4u) |
        ((u64)GIF_REG_XYZ2 << 8u);
}

static void TitanGameVu1_AddStartProgram(packet2_t *packet)
{
    packet2_chain_open_cnt(packet, 0, 0, 0);
    /* Input UNPACKs never overlap the previous PATH1 output area.  FLUSH here
     * waits until that XGKICK has released output qwords 329..616 before the
     * new MSCAL can overwrite them, without stalling the safe input DMA. */
    packet2_vif_flush(packet, 0);
    packet2_vif_mscal(packet, TITAN_GAME_VU1_MICRO_ADDRESS, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_pad128(packet);
    packet2_chain_close_tag(packet);
}

static int TitanGameVu1_SendPacket(packet2_t *packet)
{
    unsigned int timeout;

    SyncDCache(packet->base, packet->next);
    dma_channel_send_packet2(packet, DMA_CHANNEL_VIF1, 1);
    if (dma_channel_wait(DMA_CHANNEL_VIF1, TITAN_GAME_VU1_DMA_TIMEOUT) < 0)
        return 0;

    timeout = TITAN_GAME_VU1_STATUS_TIMEOUT;
    while ((VIF1_STAT & TITAN_GAME_VU1_VIF_IDLE_MASK) != 0u)
    {
        if (--timeout == 0u)
            return 0;
    }
    return 1;
}

static void TitanGameVu1_MarkFailure(void)
{
    /* A VIF1 timeout is not safely retryable. The failed DMA may have consumed
     * only part of the UNPACK/MSCAL stream and PATH1 may still own VU memory.
     * Never issue a second geometry submission into an unknown VIF/VU state;
     * disable the optional backend immediately and latch ownership fault until
     * the explicit end-game recovery proves the hardware completely idle. */
    if (g_game_vu1.transient_failures != 0xFFFFFFFFu)
        g_game_vu1.transient_failures++;
    g_game_vu1.available = 0;
    g_game_vu1.faulted = 1;
}

static int TitanGameVu1_FaultHardwareIdle(void)
{
    /* First prove the DMAC no longer owns VIF1.  Never reset a live channel. */
    if (dma_channel_wait(DMA_CHANNEL_VIF1, TITAN_GAME_VU1_DMA_TIMEOUT) < 0)
        return 0;

    /* VPS/VEW/VGW and FQC must all be idle.  Stall/error latch bits may be
     * present after a fault; they are exactly what FBRST is meant to clear. */
    if ((VIF1_STAT & TITAN_GAME_VU1_VIF_IDLE_MASK) != 0u)
        return 0;

#if defined(__EE__) || defined(_EE)
    /* GIF_STAT: P1Q bit 8, OPH bit 9 and APATH bits 10..11.  Requiring all
     * four bits zero proves no queued PATH1 work and no active GIF path. */
    if ((TITAN_GAME_VU1_GIF_STAT_REG &
         TITAN_GAME_VU1_GIF_PATH_IDLE_MASK) != 0u)
        return 0;
#endif
    return 1;
}

static int TitanGameVu1_RecoverFault(void)
{
    if (!g_game_vu1.faulted)
        return 1;
    if (!TitanGameVu1_FaultHardwareIdle())
        return 0;

#if defined(__EE__) || defined(_EE)
    /* Recovery is deliberately last: only after every owner/consumer above is
     * proven idle may FBRST discard the faulted VIF1 state. */
    TITAN_GAME_VU1_VIF1_FBRST_REG = 1u;
    if ((VIF1_STAT & TITAN_GAME_VU1_VIF_IDLE_MASK) != 0u)
        return 0;
    if ((TITAN_GAME_VU1_GIF_STAT_REG &
         TITAN_GAME_VU1_GIF_PATH_IDLE_MASK) != 0u)
        return 0;
#endif

    if (dma_channel_initialize(DMA_CHANNEL_VIF1, 0, 0) < 0)
        return 0;
    g_game_vu1.faulted = 0;
    return 1;
}

int TitanGameVu1_Initialize(void)
{
    unsigned int micro_instructions;

    /* A prior partial VIF stream remains an ownership fault until the explicit
     * end-game release path proves hardware idle and resets VIF1. */
    if (g_game_vu1.faulted)
    {
        g_game_vu1.available = 0;
        return 0;
    }

    /* VU1 owns 16 KiB of micro memory = 2048 64-bit instructions. Reject
     * malformed/generated payloads before packet2 can emit an MPG upload. */
    if (titan_game_vu1_micro_size == 0u ||
        (titan_game_vu1_micro_size & 7u) != 0u)
    {
        g_game_vu1.available = 0;
        return 0;
    }
    micro_instructions = titan_game_vu1_micro_size / 8u;
    if (micro_instructions == 0u ||
        TITAN_GAME_VU1_MICRO_ADDRESS + micro_instructions >
            TITAN_GAME_VU1_MICRO_INSTRUCTION_CAPACITY)
    {
        g_game_vu1.available = 0;
        return 0;
    }

    if (g_game_vu1.upload_packet == 0)
    {
        g_game_vu1.upload_packet = packet2_create(
            TITAN_GAME_VU1_UPLOAD_PACKET_QWORDS,
            P2_TYPE_NORMAL,
            P2_MODE_CHAIN,
            1);
    }
    if (g_game_vu1.draw_packet == 0)
    {
        g_game_vu1.draw_packet = packet2_create(
            TITAN_GAME_VU1_DRAW_PACKET_QWORDS,
            P2_TYPE_NORMAL,
            P2_MODE_CHAIN,
            1);
    }
    if (g_game_vu1.upload_packet == 0 || g_game_vu1.draw_packet == 0)
    {
        g_game_vu1.available = 0;
        return 0;
    }
    if (dma_channel_initialize(DMA_CHANNEL_VIF1, 0, 0) < 0)
    {
        g_game_vu1.available = 0;
        return 0;
    }

    packet2_reset(g_game_vu1.upload_packet, 0);
    packet2_vif_add_micro_program(
        g_game_vu1.upload_packet,
        TITAN_GAME_VU1_MICRO_ADDRESS,
        (u32 *)(void *)titan_game_vu1_micro,
        (u32 *)(void *)(titan_game_vu1_micro + titan_game_vu1_micro_size));
    packet2_utils_vu_add_end_tag(g_game_vu1.upload_packet);

    if (!TitanGameVu1_SendPacket(g_game_vu1.upload_packet))
    {
        TitanGameVu1_MarkFailure();
        return 0;
    }

    g_game_vu1.transient_failures = 0u;
    g_game_vu1.available = 1;
    return 1;
}

int TitanGameVu1_Reset(void)
{
    g_game_vu1.available = 0;
    if (!TitanGameVu1_RecoverFault())
        return 0;
    g_game_vu1.transient_failures = 0u;
    return 1;
}

int TitanGameVu1_IsAvailable(void)
{
    return g_game_vu1.available;
}

int TitanGameVu1_HasFault(void)
{
    return g_game_vu1.faulted;
}

int TitanGameVu1_DrawMaterialPass(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    const TitanGameRenderState *state,
    const TitanVideoGameTexture *texture,
    unsigned int texture_arena_address,
    unsigned int zbuffer_address,
    int second_pass
)
{
    unsigned int submitted_vertices;

    if (!g_game_vu1.available || vertices == 0 || state == 0 ||
        vertex_count == 0u || vertex_count > TITAN_GAME_VU1_MAX_VERTICES ||
        vertex_count % 3u != 0u)
        return TITAN_GAME_VU1_RESULT_NOT_ELIGIBLE;

    /* Two-pass LOD remains atomic on the proven EE path until both PATH1
     * passes can be committed as one submission. */
    if (state->combine_mode == TITAN_GAME_COMBINE_TEX0_TEX1_LOD ||
        !TitanGameVu1_StateEligible(state) ||
        !TitanGameVu1_TextureEligible(texture) ||
        !TitanGameVu1_BatchEligible(vertices, vertex_count))
        return TITAN_GAME_VU1_RESULT_NOT_ELIGIBLE;

    submitted_vertices = TitanGameVu1_PrepareVertices(
        vertices, vertex_count, state, texture, second_pass);
    if (submitted_vertices == 0u)
        return 0;

    TitanGameVu1_PrepareParams(state, submitted_vertices);
    TitanGameVu1_PreparePath1State(
        state,
        texture,
        texture_arena_address,
        zbuffer_address,
        submitted_vertices,
        second_pass);

    SyncDCache(g_game_vu1_params, g_game_vu1_params + 4u);
    SyncDCache(g_game_vu1_clip, g_game_vu1_clip + submitted_vertices);
    SyncDCache(g_game_vu1_st, g_game_vu1_st + submitted_vertices);
    SyncDCache(g_game_vu1_rgba, g_game_vu1_rgba + submitted_vertices);
    SyncDCache(g_game_vu1_path1, g_game_vu1_path1 + TITAN_GAME_VU1_PATH1_QWORDS);

    packet2_reset(g_game_vu1.draw_packet, 0);
    packet2_utils_vu_add_unpack_data(
        g_game_vu1.draw_packet,
        TITAN_GAME_VU1_ADDR_VIEWPORT_SCALE,
        g_game_vu1_params,
        4u,
        0u);
    packet2_utils_vu_add_unpack_data(
        g_game_vu1.draw_packet,
        TITAN_GAME_VU1_ADDR_CLIP,
        g_game_vu1_clip,
        submitted_vertices,
        0u);
    packet2_utils_vu_add_unpack_data(
        g_game_vu1.draw_packet,
        TITAN_GAME_VU1_ADDR_ST,
        g_game_vu1_st,
        submitted_vertices,
        0u);
    packet2_utils_vu_add_unpack_data(
        g_game_vu1.draw_packet,
        TITAN_GAME_VU1_ADDR_RGBA,
        g_game_vu1_rgba,
        submitted_vertices,
        0u);
    packet2_utils_vu_add_unpack_data(
        g_game_vu1.draw_packet,
        TITAN_GAME_VU1_ADDR_PATH1,
        g_game_vu1_path1,
        TITAN_GAME_VU1_PATH1_QWORDS,
        0u);
    TitanGameVu1_AddStartProgram(g_game_vu1.draw_packet);
    packet2_utils_vu_add_end_tag(g_game_vu1.draw_packet);

    if (!TitanGameVu1_SendPacket(g_game_vu1.draw_packet))
    {
        TitanGameVu1_MarkFailure();
        return TITAN_GAME_VU1_RESULT_SUBMIT_FAILED;
    }

    g_game_vu1.transient_failures = 0u;
    return (int)(submitted_vertices / 3u);
}

#else

int TitanGameVu1_Initialize(void)
{
    return 0;
}

int TitanGameVu1_Reset(void)
{
    return 1;
}

int TitanGameVu1_IsAvailable(void)
{
    return 0;
}

int TitanGameVu1_HasFault(void)
{
    return 0;
}

int TitanGameVu1_DrawMaterialPass(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    const TitanGameRenderState *state,
    const TitanVideoGameTexture *texture,
    unsigned int texture_arena_address,
    unsigned int zbuffer_address,
    int second_pass
)
{
    (void)vertices;
    (void)vertex_count;
    (void)state;
    (void)texture;
    (void)texture_arena_address;
    (void)zbuffer_address;
    (void)second_pass;
    return TITAN_GAME_VU1_RESULT_NOT_ELIGIBLE;
}

#endif
