#ifndef GFX_TITAN_COMBINER_FAST_H
#define GFX_TITAN_COMBINER_FAST_H

#include <stdint.h>
#include "gfx_cc.h"

enum TitanCombinerFastKind {
    TITAN_COMBINER_FAST_GENERIC = 0,
    TITAN_COMBINER_FAST_ZERO,
    TITAN_COMBINER_FAST_ONE,
    TITAN_COMBINER_FAST_INPUT
};

struct TitanCombinerFastLane {
    uint8_t kind;
    uint8_t input_index;
};

static inline struct TitanCombinerFastLane titan_combiner_fast_source(uint8_t source)
{
    struct TitanCombinerFastLane lane;
    lane.kind = TITAN_COMBINER_FAST_GENERIC;
    lane.input_index = 0u;

    if (source == SHADER_0) lane.kind = TITAN_COMBINER_FAST_ZERO;
    else if (source == SHADER_1 || source == SHADER_TEXEL0 ||
             source == SHADER_TEXEL1 || source == SHADER_TEXEL0A ||
             source == SHADER_TEXEL1A)
        lane.kind = TITAN_COMBINER_FAST_ONE;
    else if (source >= SHADER_INPUT_1 && source <= SHADER_INPUT_7)
    {
        lane.kind = TITAN_COMBINER_FAST_INPUT;
        lane.input_index = (uint8_t)(source - SHADER_INPUT_1);
    }
    return lane;
}

static inline struct TitanCombinerFastLane titan_combiner_compile_lane(
    const struct CCFeatures *features, int channel)
{
    const struct TitanCombinerFastLane generic = {
        TITAN_COMBINER_FAST_GENERIC, 0u
    };
    struct TitanCombinerFastLane a, b, m, d;
    const uint8_t *c;

    if (features == 0 || channel < 0 || channel > 1 || features->opt_2cyc)
        return generic;
    c = features->c[0][channel];
    a = titan_combiner_fast_source(c[0]);
    b = titan_combiner_fast_source(c[1]);
    m = titan_combiner_fast_source(c[2]);
    d = titan_combiner_fast_source(c[3]);

    /* (A-B)*0 + D == D. */
    if (m.kind == TITAN_COMBINER_FAST_ZERO) return d;
    /* Identical A/B cancels exactly for finite colour inputs. */
    if (c[0] == c[1]) return d;

    /* (A-0)*M + 0. Texture sources are 1.0 in the Titan CPU-side
     * partial combiner because GS performs the actual texture modulation. */
    if (b.kind == TITAN_COMBINER_FAST_ZERO &&
        d.kind == TITAN_COMBINER_FAST_ZERO)
    {
        if (a.kind == TITAN_COMBINER_FAST_ZERO ||
            m.kind == TITAN_COMBINER_FAST_ZERO)
            return (struct TitanCombinerFastLane){
                TITAN_COMBINER_FAST_ZERO, 0u
            };
        if (a.kind == TITAN_COMBINER_FAST_ONE) return m;
        if (m.kind == TITAN_COMBINER_FAST_ONE) return a;
    }

    /* mix(B,A,C): C=0 -> B, C=1 -> A. */
    if (c[1] == c[3])
    {
        if (m.kind == TITAN_COMBINER_FAST_ZERO) return b;
        if (m.kind == TITAN_COMBINER_FAST_ONE) return a;
    }

    return generic;
}

#endif
