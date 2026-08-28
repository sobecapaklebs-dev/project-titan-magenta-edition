#include "gfx_ps2_vu0.h"

#include <stdint.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#define GFX_PS2_EE 1
#else
#define GFX_PS2_EE 0
#endif

typedef struct GfxPs2AlignedVec4 {
    float v[4];
} __attribute__((aligned(16))) GfxPs2AlignedVec4;

static GfxPs2AlignedVec4 s_vu0_in[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES]
    __attribute__((aligned(64)));
static GfxPs2AlignedVec4 s_vu0_out[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES]
    __attribute__((aligned(64)));
static GfxPs2AlignedVec4 s_vu0_matrix[4]
    __attribute__((aligned(64)));

#if GFX_PS2_EE
static const GfxPs2AlignedVec4 s_vu0_inv127 __attribute__((aligned(16))) = {
    {1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f}
};
#endif

static void gfx_ps2_scalar_transform(
    float out[4], const float in[4], const float m[4][4])
{
    out[0] = in[0] * m[0][0] + in[1] * m[1][0] +
             in[2] * m[2][0] + m[3][0];
    out[1] = in[0] * m[0][1] + in[1] * m[1][1] +
             in[2] * m[2][1] + m[3][1];
    out[2] = in[0] * m[0][2] + in[1] * m[1][2] +
             in[2] * m[2][2] + m[3][2];
    out[3] = in[0] * m[0][3] + in[1] * m[1][3] +
             in[2] * m[2][3] + m[3][3];
}

int gfx_ps2_vu0_transform_i16_batch(
    float out[][4],
    const void *xyz_base,
    size_t xyz_stride,
    size_t count,
    const float matrix[4][4])
{
    const unsigned char *src = (const unsigned char *)xyz_base;
    size_t i;

    if (out == 0 || src == 0 || matrix == 0 || xyz_stride < 6u ||
        count == 0u || count > GFX_PS2_VU0_MAX_TRANSFORM_VERTICES ||
        (((uintptr_t)src & (sizeof(int16_t) - 1u)) != 0u) ||
        ((xyz_stride & (sizeof(int16_t) - 1u)) != 0u))
        return 0;

    /* lqc2 requires 16-byte aligned rows. rsp.MP_matrix lives inside a large
     * Fast3D state struct and its field alignment is not an ABI guarantee, so
     * materialize one aligned 64-byte matrix copy per G_VTX batch. */
    memcpy(s_vu0_matrix, matrix, sizeof(s_vu0_matrix));

    for (i = 0u; i < count; i++) {
        const int16_t *xyz = (const int16_t *)(const void *)(src + i * xyz_stride);
        s_vu0_in[i].v[0] = (float)xyz[0];
        s_vu0_in[i].v[1] = (float)xyz[1];
        s_vu0_in[i].v[2] = (float)xyz[2];
        s_vu0_in[i].v[3] = 1.0f;
    }

#if GFX_PS2_EE
    /* VU0 macro mode is intentionally self-contained in one asm block: load the
     * four matrix rows once, then run every vertex through the FMAC accumulator.
     * The instruction sequence follows the public PS2SDK/Tyra R5900 COP2 form:
     *   row3 + row0*x + row1*y + row2*z
     * which is exactly gfx_pc.c's historical row-vector transform. */
    {
        const GfxPs2AlignedVec4 *in_ptr = s_vu0_in;
        GfxPs2AlignedVec4 *out_ptr = s_vu0_out;
        unsigned int remaining = (unsigned int)count;
        __asm__ volatile(
            "lqc2      $vf1, 0x00(%3)        \n\t"
            "lqc2      $vf2, 0x10(%3)        \n\t"
            "lqc2      $vf3, 0x20(%3)        \n\t"
            "lqc2      $vf4, 0x30(%3)        \n\t"
            "1:                                \n\t"
            "lqc2      $vf5, 0x00(%0)        \n\t"
            "vmulaw    $ACC, $vf4, $vf0      \n\t"
            "vmaddax   $ACC, $vf1, $vf5      \n\t"
            "vmadday   $ACC, $vf2, $vf5      \n\t"
            "vmaddz    $vf6, $vf3, $vf5      \n\t"
            "sqc2      $vf6, 0x00(%1)        \n\t"
            "daddiu    %0, %0, 16            \n\t"
            "daddiu    %1, %1, 16            \n\t"
            "addiu     %2, %2, -1            \n\t"
            "bnez      %2, 1b                \n\t"
            "nop                               \n\t"
            : "+r"(in_ptr), "+r"(out_ptr), "+r"(remaining)
            : "r"(s_vu0_matrix)
            : "memory"
        );
    }
#else
    for (i = 0u; i < count; i++)
        gfx_ps2_scalar_transform(s_vu0_out[i].v, s_vu0_in[i].v, matrix);
#endif

    for (i = 0u; i < count; i++)
        memcpy(out[i], s_vu0_out[i].v, sizeof(s_vu0_out[i].v));
    return 1;
}

int gfx_ps2_vu0_dot3_i8_batch(
    float out[][4],
    const void *normal_base,
    size_t normal_stride,
    size_t count,
    const float coeff[4][3])
{
    const unsigned char *src = (const unsigned char *)normal_base;
    size_t i;
    size_t lane;

    if (out == 0 || src == 0 || coeff == 0 || normal_stride < 3u ||
        count == 0u || count > GFX_PS2_VU0_MAX_TRANSFORM_VERTICES)
        return 0;

    /* Put four raw dot products in the four output lanes. Fast3D's historical
     * arithmetic is (normal . light) / 127, not normal . (light / 127).
     * Preserve that operation order exactly: build the unscaled coefficient
     * rows, evaluate dot3 once, then multiply the completed vector by 1/127.
     * This matters in float when component cancellation is present. */
    for (lane = 0u; lane < 4u; lane++) {
        s_vu0_matrix[0].v[lane] = coeff[lane][0];
        s_vu0_matrix[1].v[lane] = coeff[lane][1];
        s_vu0_matrix[2].v[lane] = coeff[lane][2];
        s_vu0_matrix[3].v[lane] = 0.0f;
    }

    for (i = 0u; i < count; i++) {
        const int8_t *normal = (const int8_t *)(const void *)(src + i * normal_stride);
        s_vu0_in[i].v[0] = (float)normal[0];
        s_vu0_in[i].v[1] = (float)normal[1];
        s_vu0_in[i].v[2] = (float)normal[2];
        s_vu0_in[i].v[3] = 1.0f;
    }

#if GFX_PS2_EE
    {
        const GfxPs2AlignedVec4 *in_ptr = s_vu0_in;
        GfxPs2AlignedVec4 *out_ptr = s_vu0_out;
        unsigned int remaining = (unsigned int)count;
        __asm__ volatile(
            "lqc2      $vf1, 0x00(%3)        \n\t"
            "lqc2      $vf2, 0x10(%3)        \n\t"
            "lqc2      $vf3, 0x20(%3)        \n\t"
            "lqc2      $vf4, 0x30(%3)        \n\t"
            "lqc2      $vf7, 0x00(%4)        \n\t"
            "1:                                \n\t"
            "lqc2      $vf5, 0x00(%0)        \n\t"
            "vmulaw    $ACC, $vf4, $vf0      \n\t"
            "vmaddax   $ACC, $vf1, $vf5      \n\t"
            "vmadday   $ACC, $vf2, $vf5      \n\t"
            "vmaddz    $vf6, $vf3, $vf5      \n\t"
            "vmul.xyzw $vf6, $vf6, $vf7      \n\t"
            "sqc2      $vf6, 0x00(%1)        \n\t"
            "daddiu    %0, %0, 16            \n\t"
            "daddiu    %1, %1, 16            \n\t"
            "addiu     %2, %2, -1            \n\t"
            "bnez      %2, 1b                \n\t"
            "nop                               \n\t"
            : "+r"(in_ptr), "+r"(out_ptr), "+r"(remaining)
            : "r"(s_vu0_matrix), "r"(&s_vu0_inv127)
            : "memory"
        );
    }
#else
    for (i = 0u; i < count; i++) {
        gfx_ps2_scalar_transform(s_vu0_out[i].v, s_vu0_in[i].v,
                                 (const float (*)[4])s_vu0_matrix);
        for (lane = 0u; lane < 4u; lane++)
            s_vu0_out[i].v[lane] *= (1.0f / 127.0f);
    }
#endif

    for (i = 0u; i < count; i++)
        memcpy(out[i], s_vu0_out[i].v, sizeof(s_vu0_out[i].v));
    return 1;
}

void gfx_ps2_vu0_lerp4(
    float out[4], const float a[4], const float b[4], float t)
{
#if GFX_PS2_EE
    GfxPs2AlignedVec4 ta __attribute__((aligned(16)));
    GfxPs2AlignedVec4 tb __attribute__((aligned(16)));
    GfxPs2AlignedVec4 to __attribute__((aligned(16)));
    GfxPs2AlignedVec4 tt __attribute__((aligned(16)));
    memcpy(ta.v, a, 16u);
    memcpy(tb.v, b, 16u);
    tt.v[0] = tt.v[1] = tt.v[2] = tt.v[3] = t;
    __asm__ volatile(
        "lqc2      $vf1, 0x00(%1)        \n\t"
        "lqc2      $vf2, 0x00(%2)        \n\t"
        "lqc2      $vf3, 0x00(%3)        \n\t"
        "vsub.xyzw $vf4, $vf2, $vf1     \n\t"
        "vmul.xyzw $vf4, $vf4, $vf3     \n\t"
        "vadd.xyzw $vf4, $vf1, $vf4     \n\t"
        "sqc2      $vf4, 0x00(%0)        \n\t"
        :
        : "r"(to.v), "r"(ta.v), "r"(tb.v), "r"(tt.v)
        : "memory"
    );
    memcpy(out, to.v, 16u);
#else
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
    out[3] = a[3] + (b[3] - a[3]) * t;
#endif
}
