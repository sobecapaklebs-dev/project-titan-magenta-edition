#ifndef PROJECT_TITAN_VIDEO_GAME_MATH_H
#define PROJECT_TITAN_VIDEO_GAME_MATH_H

#include "titan_game_sdk.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The GS packed GIF format is lane based.  ST carries S, T and Q in its
 * first three 32-bit lanes; packed RGBAQ carries one colour component per
 * lane and consumes the Q most recently written by ST. */
typedef struct TitanVideoGamePackedQword
{
    uint32_t lane[4];
} TitanVideoGamePackedQword;

typedef struct TitanVideoGameScreenVertex
{
    float x;
    float y;
    uint32_t z;
    float reciprocal_w;
} TitanVideoGameScreenVertex;

static inline int TitanVideoGame_NormalizeRenderState(
    const TitanGameRenderState *input,
    uint32_t framebuffer_width,
    uint32_t framebuffer_height,
    TitanGameRenderState *output
)
{
    size_t copy_bytes;

    if (input == 0 || output == 0 || framebuffer_width == 0u ||
        framebuffer_height == 0u ||
        input->struct_size < offsetof(TitanGameRenderState, reserved) ||
        input->viewport_width == 0u || input->viewport_height == 0u ||
        input->scissor_width == 0u || input->scissor_height == 0u ||
        input->depth_compare > TITAN_GAME_DEPTH_GREATER ||
        input->blend_mode > TITAN_GAME_BLEND_ADDITIVE ||
        input->combine_mode > TITAN_GAME_COMBINE_TEX0_TEX1_LOD ||
        input->sampler[0].wrap_s > TITAN_GAME_WRAP_MIRROR ||
        input->sampler[0].wrap_t > TITAN_GAME_WRAP_MIRROR ||
        input->sampler[1].wrap_s > TITAN_GAME_WRAP_MIRROR ||
        input->sampler[1].wrap_t > TITAN_GAME_WRAP_MIRROR)
        return 0;

    memset(output, 0, sizeof(*output));
    copy_bytes = input->struct_size < sizeof(*output)
        ? input->struct_size
        : sizeof(*output);
    memcpy(output, input, copy_bytes);
    output->struct_size = sizeof(*output);
    if (output->viewport_x < 0) output->viewport_x = 0;
    if (output->viewport_y < 0) output->viewport_y = 0;
    if ((uint32_t)output->viewport_x >= framebuffer_width ||
        (uint32_t)output->viewport_y >= framebuffer_height)
        return 0;
    if (output->viewport_width >
        framebuffer_width - (uint32_t)output->viewport_x)
        output->viewport_width =
            framebuffer_width - (uint32_t)output->viewport_x;
    if (output->viewport_height >
        framebuffer_height - (uint32_t)output->viewport_y)
        output->viewport_height =
            framebuffer_height - (uint32_t)output->viewport_y;

    if (output->scissor_x < 0) output->scissor_x = 0;
    if (output->scissor_y < 0) output->scissor_y = 0;
    if ((uint32_t)output->scissor_x >= framebuffer_width ||
        (uint32_t)output->scissor_y >= framebuffer_height)
        return 0;
    if (output->scissor_width >
        framebuffer_width - (uint32_t)output->scissor_x)
        output->scissor_width =
            framebuffer_width - (uint32_t)output->scissor_x;
    if (output->scissor_height >
        framebuffer_height - (uint32_t)output->scissor_y)
        output->scissor_height =
            framebuffer_height - (uint32_t)output->scissor_y;
    return 1;
}

static inline uint32_t TitanVideoGame_FloatBits(float value)
{
    union
    {
        float floating;
        uint32_t bits;
    } representation;

    representation.floating = value;
    return representation.bits;
}

static inline uint8_t TitanVideoGame_ToGsAlpha(uint8_t alpha)
{
    uint32_t converted = ((uint32_t)alpha * 128u + 127u) / 255u;
    return (uint8_t)(converted > 128u ? 128u : converted);
}

static inline uint32_t TitanVideoGame_PackPsm32(
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    return (uint32_t)red |
        ((uint32_t)green << 8u) |
        ((uint32_t)blue << 16u) |
        ((uint32_t)TitanVideoGame_ToGsAlpha(alpha) << 24u);
}

static inline TitanVideoGamePackedQword TitanVideoGame_PackSt(
    float s,
    float t,
    float q
)
{
    TitanVideoGamePackedQword packed;

    packed.lane[0] = TitanVideoGame_FloatBits(s);
    packed.lane[1] = TitanVideoGame_FloatBits(t);
    packed.lane[2] = TitanVideoGame_FloatBits(q);
    packed.lane[3] = 0u;
    return packed;
}

static inline TitanVideoGamePackedQword TitanVideoGame_PackRgba(
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
)
{
    TitanVideoGamePackedQword packed;

    packed.lane[0] = red;
    packed.lane[1] = green;
    packed.lane[2] = blue;
    packed.lane[3] = TitanVideoGame_ToGsAlpha(alpha);
    return packed;
}

static inline uint32_t TitanVideoGame_XyFixed(float coordinate)
{
    float fixed;

    if (!isfinite(coordinate)) coordinate = 0.0f;
    fixed = (coordinate + 2048.0f) * 16.0f;
    if (fixed <= 0.0f) return 0u;
    if (fixed >= 65535.0f) return 65535u;
    return (uint32_t)(fixed + 0.5f);
}

static inline TitanVideoGamePackedQword TitanVideoGame_PackXyz2(
    float x,
    float y,
    uint32_t z
)
{
    TitanVideoGamePackedQword packed;

    packed.lane[0] = TitanVideoGame_XyFixed(x);
    packed.lane[1] = TitanVideoGame_XyFixed(y);
    packed.lane[2] = z;
    packed.lane[3] = 0u;
    return packed;
}

static inline float TitanVideoGame_ResolveQ(
    float requested_q,
    float reciprocal_w
)
{
    if (isfinite(requested_q) && requested_q > 0.000001f)
        return requested_q;
    return reciprocal_w;
}

static inline uint32_t TitanVideoGame_NextPowerOfTwo(uint32_t value)
{
    uint32_t power = 1u;

    if (value == 0u) return 1u;
    while (power < value && power < 0x80000000u) power <<= 1u;
    return power;
}

static inline float TitanVideoGame_PrepareSt(
    float normalized,
    uint32_t logical_size,
    float q
)
{
    uint32_t declared_size = TitanVideoGame_NextPowerOfTwo(logical_size);
    return normalized * ((float)logical_size / (float)declared_size) * q;
}

static inline int TitanVideoGame_MapClipVertex(
    const TitanGameRenderState *state,
    float x,
    float y,
    float z,
    float w,
    TitanVideoGameScreenVertex *output
)
{
    float nx;
    float ny;
    float nz;
    float depth;
    float reciprocal_w;
    float clamped_z;

    if (state == 0 || output == 0 || !isfinite(x) || !isfinite(y) ||
        !isfinite(z) || !isfinite(w) || w < 0.0001f ||
        state->viewport_width == 0u || state->viewport_height == 0u)
        return 0;

    reciprocal_w = 1.0f / w;
    nx = x * reciprocal_w;
    ny = y * reciprocal_w;
    /* Fast3D clips exactly onto the +/-2 homogeneous guard-band. R5900/VU
     * interpolation can differ by a few ULPs at that plane; a strict compare
     * dropped the entire triangle and produced screen-position-locked cracks. */
    if (!isfinite(nx) || !isfinite(ny) ||
        nx < -2.0005f || nx > 2.0005f ||
        ny < -2.0005f || ny > 2.0005f)
        return 0;
    if (nx < -2.0f) nx = -2.0f;
    if (nx > 2.0f) nx = 2.0f;
    if (ny < -2.0f) ny = -2.0f;
    if (ny > 2.0f) ny = 2.0f;

    /* P162 depth clamp is not a visibility test.  Clamp in homogeneous
     * clip-space before multiplying by 1/W so arbitrarily deep but finite
     * vertices cannot overflow NDC and cannot be silently discarded by the
     * old +/-2 Z safety guard.  For positive W this is exactly equivalent to
     * clamp(Z/W,-1,+1). */
    clamped_z = z;
    if (clamped_z < -w) clamped_z = -w;
    if (clamped_z > w) clamped_z = w;
    nz = clamped_z * reciprocal_w;
    if (!isfinite(nz)) return 0;
    output->x = (float)state->viewport_x +
        (nx * 0.5f + 0.5f) * (float)state->viewport_width;
    output->y = (float)state->viewport_y +
        (0.5f - ny * 0.5f) * (float)state->viewport_height;
    depth = (1.0f - nz) * 0.5f;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    output->z = (uint32_t)(depth * 65535.0f + 0.5f);
    output->reciprocal_w = reciprocal_w;
    return 1;
}

#endif
