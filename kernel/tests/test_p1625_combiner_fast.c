#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx_titan_combiner_fast.h"

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float source_value(uint8_t source, const float inputs[7][4], int component)
{
    if (source == SHADER_0) return 0.0f;
    if (source == SHADER_1) return 1.0f;
    if (source == SHADER_COMBINED) return 0.0f; /* cycle 0 combined seed */
    if (source == SHADER_TEXEL0 || source == SHADER_TEXEL1 ||
        source == SHADER_TEXEL0A || source == SHADER_TEXEL1A)
        return 1.0f; /* Titan's CPU partial combiner semantics */
    if (source >= SHADER_INPUT_1 && source <= SHADER_INPUT_7)
        return inputs[source - SHADER_INPUT_1][component];
    return source == SHADER_NOISE ? 0.5f : 0.0f;
}

static float generic_lane(const uint8_t c[4], const float inputs[7][4], int component)
{
    float a = source_value(c[0], inputs, component);
    float b = source_value(c[1], inputs, component);
    float m = source_value(c[2], inputs, component);
    float d = source_value(c[3], inputs, component);
    return clamp01((a - b) * m + d);
}

static float fast_lane(struct TitanCombinerFastLane lane,
                       const float inputs[7][4], int component)
{
    switch (lane.kind)
    {
        case TITAN_COMBINER_FAST_ZERO: return 0.0f;
        case TITAN_COMBINER_FAST_ONE: return 1.0f;
        case TITAN_COMBINER_FAST_INPUT:
            assert(lane.input_index < 7u);
            return clamp01(inputs[lane.input_index][component]);
        default: assert(!"generic lane cannot be evaluated as fast"); return 0.0f;
    }
}

static uint32_t rng_state = 0x16250001u;
static float sample_value(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    /* Include values outside [0,1] to prove final-clamp equivalence too. */
    return ((float)((rng_state >> 8) & 0xffffu) / 65535.0f) * 2.0f - 0.5f;
}

int main(void)
{
    struct CCFeatures f;
    uint64_t fast_plans = 0u;
    uint64_t samples = 0u;

    memset(&f, 0, sizeof(f));
    f.opt_2cyc = false;

    for (unsigned a = 0; a < 16u; a++)
    for (unsigned b = 0; b < 16u; b++)
    for (unsigned m = 0; m < 16u; m++)
    for (unsigned d = 0; d < 16u; d++)
    {
        struct TitanCombinerFastLane plan;
        f.c[0][0][0] = (uint8_t)a;
        f.c[0][0][1] = (uint8_t)b;
        f.c[0][0][2] = (uint8_t)m;
        f.c[0][0][3] = (uint8_t)d;
        plan = titan_combiner_compile_lane(&f, 0);
        if (plan.kind == TITAN_COMBINER_FAST_GENERIC) continue;
        fast_plans++;

        for (unsigned trial = 0; trial < 8u; trial++)
        {
            float inputs[7][4];
            for (unsigned i = 0; i < 7u; i++)
                for (unsigned c = 0; c < 4u; c++)
                    inputs[i][c] = sample_value();
            for (int component = 0; component < 4; component++)
            {
                float reference = generic_lane(f.c[0][0], inputs, component);
                float fast = fast_lane(plan, inputs, component);
                assert(fabsf(reference - fast) <= 1.0e-6f);
                samples++;
            }
        }
    }

    /* Two-cycle equations must always stay on the exact generic path. */
    f.opt_2cyc = true;
    assert(titan_combiner_compile_lane(&f, 0).kind == TITAN_COMBINER_FAST_GENERIC);
    assert(titan_combiner_compile_lane(&f, 1).kind == TITAN_COMBINER_FAST_GENERIC);

    printf("P1625_COMBINER_FAST=PASS plans=%llu samples=%llu\n",
           (unsigned long long)fast_plans,
           (unsigned long long)samples);
    return 0;
}
