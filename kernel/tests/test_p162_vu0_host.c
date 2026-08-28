#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ports/goldeneye/mgb64/src/platform/fast3d/gfx_ps2_vu0.h"

typedef struct VertexI16 {
    int16_t xyz[3];
    uint16_t pad;
} VertexI16;

typedef struct NormalI8 {
    int8_t xyz[3];
    uint8_t pad;
} NormalI8;

static uint32_t s_rng = 0x1622026u;

static uint32_t rng_u32(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

static float rng_float(float lo, float hi)
{
    const float t = (float)(rng_u32() & 0x00ffffffu) / 16777215.0f;
    return lo + (hi - lo) * t;
}

static int same_float(float a, float b)
{
    uint32_t ua, ub;
    memcpy(&ua, &a, sizeof(ua));
    memcpy(&ub, &b, sizeof(ub));
    return ua == ub;
}

static void ref_transform(float out[4], const float in[4], const float m[4][4])
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

static int fail(const char *what, size_t batch, size_t sample, size_t lane,
                float got, float expected)
{
    fprintf(stderr,
            "P162_VU0_HOST=FAIL what=%s batch=%zu sample=%zu lane=%zu got=%.9g expected=%.9g\n",
            what, batch, sample, lane, (double)got, (double)expected);
    return 1;
}

int main(void)
{
    enum { BATCHES = 4000, LERPS_PER_BATCH = 25 };
    VertexI16 verts[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES];
    NormalI8 normals[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES];
    float transformed[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES][4];
    float dotted[GFX_PS2_VU0_MAX_TRANSFORM_VERTICES][4];
    float matrix[4][4];
    float coeff[4][3];
    size_t batch;
    size_t transform_samples = 0u;
    size_t lerp_samples = 0u;

    /* Alignment is a correctness requirement before casting byte-backed Fast3D
     * vertex storage to int16_t. Both a misaligned base and an odd inter-record
     * stride must fail before any load occurs. */
    {
        uint16_t storage[16] = {0};
        unsigned char *bytes = (unsigned char *)(void *)storage;
        float out[1][4];
        const float identity[4][4] = {
            {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
        };
        if (gfx_ps2_vu0_transform_i16_batch(out, bytes + 1u, 6u, 1u, identity) != 0 ||
            gfx_ps2_vu0_transform_i16_batch(out, bytes, 7u, 1u, identity) != 0) {
            fputs("P162_VU0_HOST=FAIL alignment_guard\n", stderr);
            return 1;
        }
    }

    /* Cancellation witness: the pre-divided candidate and historical Fast3D
     * operation order are observably different in IEEE-754 float. */
    {
        const int8_t n[3] = {-101, -24, -25};
        const float c[4][3] = {
            {-17397.513671875f, 53974.7421875f, 95191.2265625f},
            {0,0,0}, {0,0,0}, {0,0,0}
        };
        float got[1][4];
        float raw = (float)n[0] * c[0][0] + (float)n[1] * c[0][1] +
                    (float)n[2] * c[0][2];
        float expected = raw * (1.0f / 127.0f);
        float old = (float)n[0] * (c[0][0] * (1.0f / 127.0f)) +
                    (float)n[1] * (c[0][1] * (1.0f / 127.0f)) +
                    (float)n[2] * (c[0][2] * (1.0f / 127.0f));
        if (same_float(expected, old)) {
            fputs("P162_VU0_HOST=FAIL cancellation_fixture_not_discriminating\n", stderr);
            return 1;
        }
        if (!gfx_ps2_vu0_dot3_i8_batch(got, n, 3u, 1u, c) ||
            !same_float(got[0][0], expected) || same_float(got[0][0], old)) {
            fprintf(stderr,
                    "P162_VU0_HOST=FAIL dot_order got=%.9g expected=%.9g old=%.9g\n",
                    (double)got[0][0], (double)expected, (double)old);
            return 1;
        }
    }

    for (batch = 0u; batch < BATCHES; batch++) {
        const size_t count = 8u + (batch & 15u); /* exactly 62,000 samples */
        size_t i, lane, j;

        for (lane = 0u; lane < 4u; lane++) {
            for (j = 0u; j < 4u; j++)
                matrix[lane][j] = rng_float(-8.0f, 8.0f);
        }
        for (lane = 0u; lane < 4u; lane++) {
            for (j = 0u; j < 3u; j++)
                coeff[lane][j] = rng_float(-4096.0f, 4096.0f);
        }
        for (i = 0u; i < count; i++) {
            verts[i].xyz[0] = (int16_t)(rng_u32() & 0xffffu);
            verts[i].xyz[1] = (int16_t)(rng_u32() & 0xffffu);
            verts[i].xyz[2] = (int16_t)(rng_u32() & 0xffffu);
            verts[i].pad = 0u;
            normals[i].xyz[0] = (int8_t)(rng_u32() & 0xffu);
            normals[i].xyz[1] = (int8_t)(rng_u32() & 0xffu);
            normals[i].xyz[2] = (int8_t)(rng_u32() & 0xffu);
            normals[i].pad = 0u;
        }

        if (!gfx_ps2_vu0_transform_i16_batch(transformed, verts, sizeof(verts[0]), count, matrix)) {
            fputs("P162_VU0_HOST=FAIL transform_call\n", stderr);
            return 1;
        }
        if (!gfx_ps2_vu0_dot3_i8_batch(dotted, normals, sizeof(normals[0]), count, coeff)) {
            fputs("P162_VU0_HOST=FAIL dot_call\n", stderr);
            return 1;
        }

        for (i = 0u; i < count; i++) {
            float in[4] = {(float)verts[i].xyz[0], (float)verts[i].xyz[1],
                           (float)verts[i].xyz[2], 1.0f};
            float expected[4];
            ref_transform(expected, in, matrix);
            for (lane = 0u; lane < 4u; lane++) {
                if (!same_float(transformed[i][lane], expected[lane]))
                    return fail("transform", batch, i, lane,
                                transformed[i][lane], expected[lane]);
            }

            for (lane = 0u; lane < 4u; lane++) {
                float raw = (float)normals[i].xyz[0] * coeff[lane][0] +
                            (float)normals[i].xyz[1] * coeff[lane][1] +
                            (float)normals[i].xyz[2] * coeff[lane][2];
                float dot_expected = raw * (1.0f / 127.0f);
                if (!same_float(dotted[i][lane], dot_expected))
                    return fail("dot3", batch, i, lane,
                                dotted[i][lane], dot_expected);
            }
        }
        transform_samples += count;

        for (i = 0u; i < LERPS_PER_BATCH; i++) {
            float a[4], b[4], got[4], expected[4];
            float t = rng_float(-2.0f, 3.0f);
            for (lane = 0u; lane < 4u; lane++) {
                a[lane] = rng_float(-100000.0f, 100000.0f);
                b[lane] = rng_float(-100000.0f, 100000.0f);
                expected[lane] = a[lane] + (b[lane] - a[lane]) * t;
            }
            gfx_ps2_vu0_lerp4(got, a, b, t);
            for (lane = 0u; lane < 4u; lane++) {
                if (!same_float(got[lane], expected[lane]))
                    return fail("lerp", batch, i, lane, got[lane], expected[lane]);
            }
            lerp_samples++;
        }
    }

    if (transform_samples != 62000u || lerp_samples != 100000u) {
        fprintf(stderr,
                "P162_VU0_HOST=FAIL counters transform_samples=%zu lerp_samples=%zu\n",
                transform_samples, lerp_samples);
        return 1;
    }

    printf("P162_VU0_HOST=PASS\n");
    printf("batches=%u\n", BATCHES);
    printf("transform_samples=%zu\n", transform_samples);
    printf("lerp_samples=%zu\n", lerp_samples);
    return 0;
}
