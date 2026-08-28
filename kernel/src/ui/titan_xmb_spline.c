#include "titan_xmb_spline.h"

#include "titan_video.h"
#include "titan_wave_vu1.h"
#include "titan_theme.h"
#include "titan_system_clock.h"

#define TITAN_XMB_SPLINE_ROW_COUNT (TITAN_WAVE_VU1_LAYER_COUNT + 1u)
#define TITAN_XMB_SPLINE_CONTROL_COUNT 16u
#define TITAN_XMB_SPLINE_TABLE_COUNT 361u
#define TITAN_XMB_SPLINE_KERNEL_ITERATIONS 8u
#define TITAN_XMB_SPLINE_KERNEL_STORES 8u
#define TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT \
    (TITAN_XMB_SPLINE_KERNEL_ITERATIONS * TITAN_XMB_SPLINE_KERNEL_STORES)
#define TITAN_XMB_SPLINE_SEGMENT_COUNT (TITAN_WAVE_VU1_POINT_COUNT - 1u)

/*
 * PS2-native adaptation of the MIT-licensed PlayStation-3-XMB spline study.
 * The browser implementation builds a 361-entry table, an eight-iteration
 * kernel and a 256x64 float displacement texture.  Here the table is static,
 * the kernel is sampled once per frame, and cubic B-splines directly produce
 * eight rows of 65 points.  Seven VU1 strips join those rows into a sheet.
 */

typedef struct TitanXmbSplineVec4
{
    float x;
    float y;
    float z;
    float w;
} TitanXmbSplineVec4;

typedef struct TitanXmbSplineRowPoint
{
    float x;
    float y;
} TitanXmbSplineRowPoint;

typedef struct TitanXmbSplineBasis
{
    unsigned char segment;
    float b0;
    float b1;
    float b2;
    float b3;
} TitanXmbSplineBasis;

typedef struct TitanXmbSplineStripStyle
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    unsigned char blend_mode;
    float texture_u_scale;
    float fresnel_bias;
    float fresnel_scale;
} TitanXmbSplineStripStyle;

static int g_spline_initialized;
static int g_temporal_valid;
static unsigned int g_cached_frame;
static float g_cached_transition;
static float g_cached_idle;
static float g_cached_horizontal_motion;
static float g_cached_vertical_transition;
static unsigned int g_cached_theme_generation;
static int g_cache_valid;

static TitanXmbSplineVec4 g_spline_table[TITAN_XMB_SPLINE_TABLE_COUNT];
static TitanXmbSplineVec4 g_kernel[TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT];
static TitanXmbSplineVec4 g_kernel_previous[TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT];
static float g_control_previous[TITAN_XMB_SPLINE_ROW_COUNT]
    [TITAN_XMB_SPLINE_CONTROL_COUNT];
static TitanXmbSplineBasis g_basis[TITAN_WAVE_VU1_POINT_COUNT];
static TitanXmbSplineRowPoint g_rows[TITAN_XMB_SPLINE_ROW_COUNT]
    [TITAN_WAVE_VU1_POINT_COUNT];
static TitanWaveVu1Point g_strips[TITAN_WAVE_VU1_LAYER_COUNT]
    [TITAN_WAVE_VU1_POINT_COUNT];
static float g_primary_wave_dynamic_visibility[TITAN_THEME_MAX_PRIMARY_WAVES] =
{
    1.0f, 1.0f, 1.0f
};

typedef struct TitanXmbSnakePoint
{
    float x;
    float y;
    float tangent_x;
    float tangent_y;
    float width;
    float visibility;
} TitanXmbSnakePoint;

static TitanXmbSnakePoint g_snake_spine[TITAN_THEME_MAX_PRIMARY_WAVES]
    [TITAN_WAVE_VU1_POINT_COUNT];
static unsigned char g_snake_active[TITAN_THEME_MAX_PRIMARY_WAVES];

static const signed char g_wave_lut[128] =
{
       0,   6,  12,  19,  25,  31,  37,  43,
      49,  55,  60,  66,  71,  76,  81,  86,
      90,  94,  98, 102, 106, 109, 112, 115,
     117, 120, 122, 123, 125, 126, 127, 127,
     127, 127, 127, 126, 125, 123, 122, 120,
     117, 115, 112, 109, 106, 102,  98,  94,
      90,  86,  81,  76,  71,  66,  60,  55,
      49,  43,  37,  31,  25,  19,  12,   6,
       0,  -6, -12, -19, -25, -31, -37, -43,
     -49, -55, -60, -66, -71, -76, -81, -86,
     -90, -94, -98,-102,-106,-109,-112,-115,
    -117,-120,-122,-123,-125,-126,-127,-127,
    -127,-127,-127,-126,-125,-123,-122,-120,
    -117,-115,-112,-109,-106,-102, -98, -94,
     -90, -86, -81, -76, -71, -66, -60, -55,
     -49, -43, -37, -31, -25, -19, -12,  -6
};

/* Draw outer depth bands first and the bright center band last. */
static const unsigned char g_draw_order[TITAN_WAVE_VU1_LAYER_COUNT] =
{
    0u, 6u, 1u, 5u, 2u, 4u, 3u
};

static const TitanXmbSplineStripStyle
    g_strip_styles[TITAN_WAVE_VU1_LAYER_COUNT] =
{
    {210u, 105u, 224u, 16u, TITAN_WAVE_BLEND_ALPHA,    0.82f, 0.34f, 0.045f},
    {224u, 118u, 235u, 22u, TITAN_WAVE_BLEND_ALPHA,    0.88f, 0.36f, 0.048f},
    {238u, 142u, 244u, 32u, TITAN_WAVE_BLEND_ALPHA,    0.94f, 0.38f, 0.052f},
    {255u, 202u, 252u, 58u, TITAN_WAVE_BLEND_ADDITIVE, 1.00f, 0.42f, 0.058f},
    {248u, 166u, 249u, 42u, TITAN_WAVE_BLEND_ALPHA,    0.96f, 0.40f, 0.054f},
    {232u, 132u, 240u, 28u, TITAN_WAVE_BLEND_ALPHA,    0.90f, 0.37f, 0.050f},
    {214u, 108u, 228u, 18u, TITAN_WAVE_BLEND_ALPHA,    0.84f, 0.34f, 0.046f}
};

static float TitanXmbSpline_Clamp01(float value);

static unsigned char TitanXmbSpline_LerpByte(
    unsigned char from,
    unsigned char to,
    float amount
)
{
    float value;

    amount = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    value = (float)from + ((float)to - (float)from) * amount;
    if (value < 0.0f) value = 0.0f;
    if (value > 255.0f) value = 255.0f;
    return (unsigned char)value;
}

static TitanXmbSplineStripStyle TitanXmbSpline_GetStripStyle(
    unsigned int source_strip,
    unsigned int primary_wave_index
)
{
    TitanXmbSplineStripStyle style;
    const TitanThemeInfo *theme;
    const TitanThemeFlowStyle *flow;
    static const unsigned int center_distance[TITAN_WAVE_VU1_LAYER_COUNT] =
    {
        3u, 2u, 1u, 0u, 1u, 2u, 3u
    };
    static const float alpha_weight[TITAN_WAVE_VU1_LAYER_COUNT] =
    {
        0.34f, 0.48f, 0.68f, 1.00f, 0.74f, 0.52f, 0.38f
    };
    float center;
    float highlight;
    float alpha;

    if (source_strip >= TITAN_WAVE_VU1_LAYER_COUNT)
        source_strip = 0u;
    theme = TitanTheme_GetActive();
    if (theme == 0 || theme->background_engine != TITAN_THEME_BACKGROUND_FLOW3D)
        return g_strip_styles[source_strip];

    flow = &theme->flow;
    if (primary_wave_index >= flow->primary_wave_count ||
        primary_wave_index >= TITAN_THEME_MAX_PRIMARY_WAVES)
        primary_wave_index = 0u;
    center = 1.0f - ((float)center_distance[source_strip] / 3.0f);
    if (flow->primary_waves[primary_wave_index].reference_ribbon_enabled != 0u)
    {
        float edge;
        static const float ribbon_alpha_weight[TITAN_WAVE_VU1_LAYER_COUNT] =
        {
            0.92f, 0.70f, 0.54f, 0.46f, 0.54f, 0.70f, 0.92f
        };

        edge = (float)center_distance[source_strip] / 3.0f;
        highlight = edge * edge * 0.82f + (1.0f - edge) * 0.10f;
        style.red = TitanXmbSpline_LerpByte(flow->wave_base.red, flow->wave_highlight.red, highlight);
        style.green = TitanXmbSpline_LerpByte(flow->wave_base.green, flow->wave_highlight.green, highlight);
        style.blue = TitanXmbSpline_LerpByte(flow->wave_base.blue, flow->wave_highlight.blue, highlight);
        alpha = (float)flow->wave_base.alpha * ribbon_alpha_weight[source_strip];
        if (source_strip == 0u || source_strip == TITAN_WAVE_VU1_LAYER_COUNT - 1u)
            alpha = (float)flow->wave_highlight.alpha * 0.82f;
        alpha *= flow->primary_waves[primary_wave_index].alpha_scale;
        alpha *= TitanXmbSpline_Clamp01(g_primary_wave_dynamic_visibility[primary_wave_index]);
        if (alpha > 255.0f) alpha = 255.0f;
        style.alpha = (unsigned char)alpha;
        style.blend_mode = TITAN_WAVE_BLEND_ALPHA;
        style.texture_u_scale = 0.94f + edge * 0.10f;
        style.fresnel_bias = 0.30f + edge * 0.18f;
        style.fresnel_scale = (0.052f + edge * 0.030f) * flow->fresnel;
        return style;
    }
    highlight = center * center * 0.78f;
    style.red = TitanXmbSpline_LerpByte(
        flow->wave_base.red, flow->wave_highlight.red, highlight
    );
    style.green = TitanXmbSpline_LerpByte(
        flow->wave_base.green, flow->wave_highlight.green, highlight
    );
    style.blue = TitanXmbSpline_LerpByte(
        flow->wave_base.blue, flow->wave_highlight.blue, highlight
    );
    alpha = (float)flow->wave_base.alpha * alpha_weight[source_strip];
    if (source_strip == 3u)
        alpha = (float)flow->wave_highlight.alpha;
    alpha *= flow->primary_waves[primary_wave_index].alpha_scale;
    alpha *= TitanXmbSpline_Clamp01(
        g_primary_wave_dynamic_visibility[primary_wave_index]
    );
    if (alpha > 255.0f) alpha = 255.0f;
    style.alpha = (unsigned char)alpha;
    style.blend_mode = source_strip == 3u
        ? TITAN_WAVE_BLEND_ADDITIVE : TITAN_WAVE_BLEND_ALPHA;
    style.texture_u_scale = 0.82f + center * 0.22f;
    style.fresnel_bias = 0.28f + center * 0.10f;
    style.fresnel_scale = (0.046f + center * 0.012f) * flow->fresnel;
    return style;
}

static float TitanXmbSpline_Absolute(float value)
{
    return value < 0.0f ? -value : value;
}

static float TitanXmbSpline_Clamp(
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

static float TitanXmbSpline_Clamp01(float value)
{
    return TitanXmbSpline_Clamp(value, 0.0f, 1.0f);
}

static float TitanXmbSpline_Lerp(float from, float to, float amount)
{
    return from + ((to - from) * amount);
}

static float TitanXmbSpline_CubicBezierY(
    float start_y,
    float control1_y,
    float control2_y,
    float end_y,
    float amount
)
{
    float one_minus;
    float a;
    float b;
    float c;
    float d;

    amount = TitanXmbSpline_Clamp01(amount);
    one_minus = 1.0f - amount;
    a = one_minus * one_minus * one_minus;
    b = 3.0f * one_minus * one_minus * amount;
    c = 3.0f * one_minus * amount * amount;
    d = amount * amount * amount;
    return start_y * a + control1_y * b + control2_y * c + end_y * d;
}

static float TitanXmbSpline_Wave(unsigned int phase)
{
    return (float)g_wave_lut[phase & 127u] / 127.0f;
}

/* Fractional LUT sampling keeps the low-cost 128-entry wave table while
 * removing visible phase quantization.  Slow theme motion such as the PS4
 * Flow sweep may advance by far less than one LUT entry per rendered frame;
 * truncating that phase to an integer made the ribbon hold for many frames and
 * then jump, even while the shell itself stayed at full frame rate. */
static float TitanXmbSpline_WaveFloat(float phase)
{
    unsigned int word;
    unsigned int base;
    unsigned int next;
    float fraction;
    float first;
    float second;

    if (phase < 0.0f)
    {
        /* Current Flow phases are positive. Keep negative wrapping defined for
         * future themes without adding libm calls to the EE hot path. */
        while (phase < 0.0f) phase += 128.0f;
    }

    /* Do not repeatedly subtract 128 as uptime grows. The integer conversion
     * gives us both the LUT word and the fractional sub-step in O(1). */
    word = (unsigned int)phase;
    fraction = phase - (float)word;
    base = word & 127u;
    next = (base + 1u) & 127u;
    first = (float)g_wave_lut[base] / 127.0f;
    second = (float)g_wave_lut[next] / 127.0f;
    return TitanXmbSpline_Lerp(first, second, fraction);
}

static float TitanXmbSpline_SoftClip(float value)
{
    float magnitude;

    magnitude = TitanXmbSpline_Absolute(value);
    return value / (1.0f + magnitude);
}

static unsigned int TitanXmbSpline_Hash(unsigned int value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

static float TitanXmbSpline_HashSigned(unsigned int value)
{
    unsigned int sample;

    sample = TitanXmbSpline_Hash(value) & 65535u;
    return ((float)sample / 32767.5f) - 1.0f;
}

static float TitanXmbSpline_Hash01(unsigned int value)
{
    return (float)(TitanXmbSpline_Hash(value) & 65535u) / 65535.0f;
}

static float TitanXmbSpline_SmootherStep(float amount)
{
    amount = TitanXmbSpline_Clamp01(amount);
    return amount * amount * amount *
        (amount * (amount * 6.0f - 15.0f) + 10.0f);
}

static float TitanXmbSpline_CatmullRom(
    float p0, float p1, float p2, float p3, float t
)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

static void TitanXmbSpline_SnakeWaypoint(
    const TitanThemePrimaryWave *wave,
    unsigned int index,
    float *x,
    float *y,
    float *visibility,
    float *width
)
{
    unsigned int seed = wave->snake_seed + index * 0x9e3779b9u;
    *x = (float)TITAN_UI_WIDTH * 0.5f +
        TitanXmbSpline_HashSigned(seed + 0x13u) * wave->snake_extent_x;
    *y = (float)TITAN_UI_HEIGHT * 0.5f +
        TitanXmbSpline_HashSigned(seed + 0x2fu) * wave->snake_extent_y;
    *visibility = wave->snake_visibility_min +
        (1.0f - wave->snake_visibility_min) *
        TitanXmbSpline_Hash01(seed + 0x51u);
    *width = 0.72f + TitanXmbSpline_Hash01(seed + 0x79u) * 0.58f;
}

static void TitanXmbSpline_EvaluateSnakeHead(
    const TitanThemePrimaryWave *wave,
    float frame_time,
    TitanXmbSnakePoint *out
)
{
    float position;
    float t;
    unsigned int i;
    float x0, y0, v0, w0;
    float x1, y1, v1, w1;
    float x2, y2, v2, w2;
    float x3, y3, v3, w3;

    if (frame_time < 0.0f) frame_time = 0.0f;
    position = (frame_time + 2048.0f) * wave->snake_speed;
    i = (unsigned int)position;
    t = TitanXmbSpline_SmootherStep(position - (float)i);
    if (i == 0u) i = 1u;

    TitanXmbSpline_SnakeWaypoint(wave, i - 1u, &x0, &y0, &v0, &w0);
    TitanXmbSpline_SnakeWaypoint(wave, i,      &x1, &y1, &v1, &w1);
    TitanXmbSpline_SnakeWaypoint(wave, i + 1u, &x2, &y2, &v2, &w2);
    TitanXmbSpline_SnakeWaypoint(wave, i + 2u, &x3, &y3, &v3, &w3);

    out->x = TitanXmbSpline_CatmullRom(x0, x1, x2, x3, t);
    out->y = TitanXmbSpline_CatmullRom(y0, y1, y2, y3, t);
    out->visibility = TitanXmbSpline_Lerp(v1, v2, t);
    out->width = TitanXmbSpline_Lerp(w1, w2, t);
    out->tangent_x = 1.0f;
    out->tangent_y = 0.0f;
}

static void TitanXmbSpline_PrepareSnakeSpine(
    unsigned int frame,
    unsigned int primary_wave_index,
    const TitanThemePrimaryWave *wave
)
{
    unsigned int point_index;
    float visibility_sum = 0.0f;

    g_snake_active[primary_wave_index] = 0u;
    if (wave->snake_enabled == 0u || wave->snake_speed <= 0.0f) return;

    for (point_index = 0u; point_index < TITAN_WAVE_VU1_POINT_COUNT; point_index++)
    {
        float age = (float)(TITAN_XMB_SPLINE_SEGMENT_COUNT - point_index) *
            wave->snake_trail_frames;
        TitanXmbSpline_EvaluateSnakeHead(
            wave, (float)frame - age,
            &g_snake_spine[primary_wave_index][point_index]
        );
        visibility_sum += g_snake_spine[primary_wave_index][point_index].visibility;
    }

    for (point_index = 0u; point_index < TITAN_WAVE_VU1_POINT_COUNT; point_index++)
    {
        unsigned int before = point_index > 0u ? point_index - 1u : point_index;
        unsigned int after = point_index + 1u < TITAN_WAVE_VU1_POINT_COUNT
            ? point_index + 1u : point_index;
        float dx = g_snake_spine[primary_wave_index][after].x -
            g_snake_spine[primary_wave_index][before].x;
        float dy = g_snake_spine[primary_wave_index][after].y -
            g_snake_spine[primary_wave_index][before].y;
        float scale = dx * dx + dy * dy;
        if (scale > 0.0001f)
        {
            /* Fast normalization without libm sqrt: reciprocal length gets a
             * stable two-step Newton approximation around a conservative seed. */
            float inv = 1.0f / (TitanXmbSpline_Absolute(dx) +
                TitanXmbSpline_Absolute(dy) + 0.0001f);
            float nx = dx * inv;
            float ny = dy * inv;
            float correction = 1.5f - 0.5f * (nx * nx + ny * ny);
            nx *= correction; ny *= correction;
            correction = 1.5f - 0.5f * (nx * nx + ny * ny);
            g_snake_spine[primary_wave_index][point_index].tangent_x = nx * correction;
            g_snake_spine[primary_wave_index][point_index].tangent_y = ny * correction;
        }
    }

    g_primary_wave_dynamic_visibility[primary_wave_index] =
        TitanXmbSpline_Clamp01(visibility_sum / (float)TITAN_WAVE_VU1_POINT_COUNT);
    g_snake_active[primary_wave_index] = 1u;
}

static float TitanXmbSpline_PhaseLerp(float from, float to, float amount)
{
    float delta;

    delta = to - from;
    if (delta > 64.0f) delta -= 128.0f;
    if (delta < -64.0f) delta += 128.0f;
    return from + delta * amount;
}

static unsigned int TitanXmbSpline_TableIndexFromWord(unsigned int word)
{
    unsigned int high;
    unsigned int low;

    high = (word >> 4u) & 255u;
    low = word & 15u;
    return (19u * high + low) % TITAN_XMB_SPLINE_TABLE_COUNT;
}

static TitanXmbSplineVec4 TitanXmbSpline_MixVec4(
    const TitanXmbSplineVec4 *first,
    const TitanXmbSplineVec4 *second,
    float amount
)
{
    TitanXmbSplineVec4 result;

    result.x = TitanXmbSpline_Lerp(first->x, second->x, amount);
    result.y = TitanXmbSpline_Lerp(first->y, second->y, amount);
    result.z = TitanXmbSpline_Lerp(first->z, second->z, amount);
    result.w = TitanXmbSpline_Lerp(first->w, second->w, amount);
    return result;
}

static TitanXmbSplineVec4 TitanXmbSpline_SubVec4(
    const TitanXmbSplineVec4 *first,
    const TitanXmbSplineVec4 *second
)
{
    TitanXmbSplineVec4 result;

    result.x = first->x - second->x;
    result.y = first->y - second->y;
    result.z = first->z - second->z;
    result.w = first->w - second->w;
    return result;
}

static TitanXmbSplineVec4 TitanXmbSpline_SampleTable(float index)
{
    int integer_index;
    unsigned int first_index;
    unsigned int second_index;
    float amount;

    while (index < 0.0f)
    {
        index += (float)TITAN_XMB_SPLINE_TABLE_COUNT;
    }
    while (index >= (float)TITAN_XMB_SPLINE_TABLE_COUNT)
    {
        index -= (float)TITAN_XMB_SPLINE_TABLE_COUNT;
    }

    integer_index = (int)index;
    first_index = (unsigned int)integer_index;
    second_index = (first_index + 1u) % TITAN_XMB_SPLINE_TABLE_COUNT;
    amount = index - (float)integer_index;
    return TitanXmbSpline_MixVec4(
        &g_spline_table[first_index],
        &g_spline_table[second_index],
        amount
    );
}

static TitanXmbSplineVec4 TitanXmbSpline_SampleKernel(float index)
{
    int integer_index;
    unsigned int first_index;
    unsigned int second_index;
    float amount;

    while (index < 0.0f)
    {
        index += (float)TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT;
    }
    while (index >= (float)TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT)
    {
        index -= (float)TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT;
    }

    integer_index = (int)index;
    first_index = (unsigned int)integer_index;
    second_index = (first_index + 1u) %
        TITAN_XMB_SPLINE_KERNEL_VECTOR_COUNT;
    amount = index - (float)integer_index;
    return TitanXmbSpline_MixVec4(
        &g_kernel[first_index],
        &g_kernel[second_index],
        amount
    );
}

static void TitanXmbSpline_BuildDescriptorTable(void)
{
    static const float normalization_a[4] =
    {
        0.39584f, -0.005239f, -0.586645f, 0.189007f
    };
    static const float normalization_b[4] =
    {
        -0.003751f, -0.575361f, 0.161975f, 0.417137f
    };
    unsigned int entry;

    for (entry = 0u; entry < TITAN_XMB_SPLINE_TABLE_COUNT; entry++)
    {
        float *lanes;
        unsigned int lane;

        lanes = &g_spline_table[entry].x;
        for (lane = 0u; lane < 4u; lane++)
        {
            float descriptor;
            float harmonic_a;
            float harmonic_b;
            float raw;
            float divisor;
            float normalized;

            descriptor = TitanXmbSpline_HashSigned(
                entry * 0x9e3779b9u + lane * 0x85ebca6bu + 1337u
            );
            harmonic_a = TitanXmbSpline_Wave(entry * 7u + lane * 29u);
            harmonic_b = TitanXmbSpline_Wave(entry * 13u + lane * 17u + 41u);
            raw = descriptor * 0.34f + harmonic_a * 0.44f +
                harmonic_b * 0.22f;

            divisor = normalization_b[lane];
            if (TitanXmbSpline_Absolute(divisor) < 0.05f)
            {
                divisor = divisor < 0.0f ? -0.05f : 0.05f;
            }
            normalized = ((raw - normalization_a[lane]) / divisor) * 0.08f;
            lanes[lane] = TitanXmbSpline_SoftClip(normalized);
        }
    }
}

static void TitanXmbSpline_BuildBasisTable(void)
{
    unsigned int index;
    const float span_count =
        (float)(TITAN_XMB_SPLINE_CONTROL_COUNT - 3u);

    for (index = 0u; index < TITAN_WAVE_VU1_POINT_COUNT; index++)
    {
        float amount;
        float position;
        unsigned int segment;
        float t;
        float t2;
        float t3;

        amount = (float)index / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT;
        position = amount * span_count;
        if (position >= span_count)
        {
            position = span_count - 0.0001f;
        }
        segment = (unsigned int)position;
        t = position - (float)segment;
        t2 = t * t;
        t3 = t2 * t;

        g_basis[index].segment = (unsigned char)segment;
        g_basis[index].b0 = (1.0f - 3.0f * t + 3.0f * t2 - t3) /
            6.0f;
        g_basis[index].b1 = (4.0f - 6.0f * t2 + 3.0f * t3) /
            6.0f;
        g_basis[index].b2 = (1.0f + 3.0f * t + 3.0f * t2 - 3.0f * t3) /
            6.0f;
        g_basis[index].b3 = t3 / 6.0f;
    }
}

static void TitanXmbSpline_UpdateKernel(unsigned int frame)
{
    static const unsigned int base_words[4] =
    {
        0x00u, 0x11u, 0x22u, 0x33u
    };
    unsigned int iteration;

    for (iteration = 0u;
         iteration < TITAN_XMB_SPLINE_KERNEL_ITERATIONS;
         iteration++)
    {
        TitanXmbSplineVec4 samples[4];
        TitanXmbSplineVec4 stores[TITAN_XMB_SPLINE_KERNEL_STORES];
        float mix_a;
        float mix_b;
        unsigned int lane;
        unsigned int store;

        for (lane = 0u; lane < 4u; lane++)
        {
            unsigned int word;
            unsigned int base_index;
            float jitter;

            word = (base_words[lane] + iteration * 0x13u) & 255u;
            base_index = TitanXmbSpline_TableIndexFromWord(word);
            jitter = TitanXmbSpline_Wave(
                (frame >> 3u) + iteration * 11u + lane * 31u
            ) * 2.20f;
            samples[lane] = TitanXmbSpline_SampleTable(
                (float)base_index + jitter
            );
        }

        mix_a = 0.5f + 0.5f * TitanXmbSpline_Wave(
            (frame >> 4u) + iteration * 9u
        );
        mix_b = 0.5f + 0.5f * TitanXmbSpline_Wave(
            (frame >> 5u) + iteration * 13u + 29u
        );

        stores[0] = TitanXmbSpline_MixVec4(&samples[0], &samples[1], mix_a);
        stores[1] = TitanXmbSpline_MixVec4(&samples[1], &samples[2], mix_b);
        stores[2] = TitanXmbSpline_MixVec4(&samples[2], &samples[3], mix_a);
        stores[3] = TitanXmbSpline_MixVec4(&samples[3], &samples[0], mix_b);
        stores[4] = TitanXmbSpline_SubVec4(&samples[1], &samples[0]);
        stores[5] = TitanXmbSpline_SubVec4(&samples[2], &samples[1]);
        stores[6] = TitanXmbSpline_SubVec4(&samples[3], &samples[2]);
        stores[7] = TitanXmbSpline_SubVec4(&samples[0], &samples[3]);

        for (store = 0u; store < TITAN_XMB_SPLINE_KERNEL_STORES; store++)
        {
            unsigned int index;
            float *destination;
            float *previous;
            const float *source;
            unsigned int component;

            index = iteration * TITAN_XMB_SPLINE_KERNEL_STORES + store;
            destination = &g_kernel[index].x;
            previous = &g_kernel_previous[index].x;
            source = &stores[store].x;

            for (component = 0u; component < 4u; component++)
            {
                float value;

                value = source[component];
                if (g_temporal_valid)
                {
                    value = previous[component] * 0.84f + value * 0.16f;
                }
                destination[component] = value;
                previous[component] = value;
            }
        }
    }

    g_temporal_valid = 1;
}

static float TitanXmbSpline_EvaluateControlSpline(
    const float *controls,
    unsigned int point_index
)
{
    const TitanXmbSplineBasis *basis;
    unsigned int segment;

    basis = &g_basis[point_index];
    segment = basis->segment;
    return controls[segment] * basis->b0 +
        controls[segment + 1u] * basis->b1 +
        controls[segment + 2u] * basis->b2 +
        controls[segment + 3u] * basis->b3;
}

static void TitanXmbSpline_BuildRow(
    unsigned int frame,
    float transition,
    float idle_intensity,
    unsigned int row_index
)
{
    float controls[TITAN_XMB_SPLINE_CONTROL_COUNT];
    float row_amount;
    float row_depth;
    float center_y;
    float state_amplitude;
    unsigned int control_index;
    unsigned int point_index;

    row_amount = (float)row_index /
        (float)(TITAN_XMB_SPLINE_ROW_COUNT - 1u);
    row_depth = row_amount * 2.0f - 1.0f;
    center_y = TitanXmbSpline_Lerp(252.0f, 256.0f, transition) -
        idle_intensity * 4.0f;
    state_amplitude = TitanXmbSpline_Lerp(1.0f, 0.91f, transition) *
        (1.0f + idle_intensity * 0.05f);

    for (control_index = 0u;
         control_index < TITAN_XMB_SPLINE_CONTROL_COUNT;
         control_index++)
    {
        TitanXmbSplineVec4 kernel;
        float kernel_position;
        float kernel_core;
        float common_band;
        float secondary_band;
        float travelling_a;
        float travelling_b;
        float depth_detail;
        float value;

        kernel_position = (float)row_index * 0.93f +
            (float)control_index * 0.61f +
            (float)(frame & 1023u) * 0.021f;
        kernel = TitanXmbSpline_SampleKernel(kernel_position);
        kernel_core = (kernel.x * 0.45f + kernel.y * 0.25f +
            kernel.z * 0.20f + kernel.w * 0.10f) * 15.0f;

        common_band = TitanXmbSpline_Wave(
            (frame >> 2u) + control_index * 10u + 7u
        ) * 28.0f;
        secondary_band = TitanXmbSpline_Wave(
            (frame >> 4u) + control_index * 18u + row_index * 7u + 41u
        ) * 10.0f;
        travelling_a = TitanXmbSpline_Wave(
            (frame >> 3u) + control_index * 5u + row_index * 9u
        ) * 7.0f;
        travelling_b = TitanXmbSpline_Wave(
            (frame >> 5u) + control_index * 12u - row_index * 7u + 83u
        ) * 4.0f;
        depth_detail = row_depth * TitanXmbSpline_Wave(
            (frame >> 4u) + control_index * 11u + row_index * 17u
        ) * 14.0f;

        value = common_band + secondary_band * 0.75f +
            travelling_a + travelling_b + depth_detail + kernel_core;
        value = TitanXmbSpline_SoftClip(value * 0.030f) * 55.0f;
        value *= state_amplitude;

        if (g_cache_valid && frame >= g_cached_frame)
        {
            value = g_control_previous[row_index][control_index] * 0.72f +
                value * 0.28f;
        }
        controls[control_index] = value;
        g_control_previous[row_index][control_index] = value;
    }

    for (point_index = 0u;
         point_index < TITAN_WAVE_VU1_POINT_COUNT;
         point_index++)
    {
        float amount;
        float edge_curve;
        float displacement;
        float x_shear;
        float row_spread;

        amount = (float)point_index / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT;
        edge_curve = (amount - 0.5f) * (amount - 0.5f);
        displacement = TitanXmbSpline_EvaluateControlSpline(
            controls,
            point_index
        );
        displacement *= 1.0f - TitanXmbSpline_Absolute(row_depth) * 0.12f;

        row_spread = row_depth * (26.0f + edge_curve * 6.0f);
        x_shear = row_depth * 5.0f + TitanXmbSpline_Wave(
            (frame >> 5u) + point_index * 2u + row_index * 19u
        ) * 0.9f;

        g_rows[row_index][point_index].x = -76.0f +
            amount * ((float)TITAN_UI_WIDTH + 152.0f) + x_shear;
        g_rows[row_index][point_index].y = center_y + row_spread +
            displacement + row_depth * TitanXmbSpline_Wave(
                (frame >> 3u) + point_index * 3u + row_index * 23u
            ) * 5.5f;
    }
}

static void TitanXmbSpline_BuildReferenceRibbonRow(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    unsigned int row_index,
    unsigned int primary_wave_index,
    const TitanThemeFlowStyle *flow,
    const TitanThemePrimaryWave *wave
)
{
    unsigned int point_index;
    float row_amount;
    float row_depth;
    float time_phase;
    float span;
    float drift_x;
    float drift_y;
    float transition_flatten;
    float idle_breathe;
    float motion_scale;

    row_amount = (float)row_index / (float)(TITAN_XMB_SPLINE_ROW_COUNT - 1u);
    row_depth = row_amount * 2.0f - 1.0f;
    time_phase = (float)frame * wave->ribbon_speed + (float)(wave->ribbon_seed & 127u);
    span = wave->ribbon_span_scale > 0.0f ? wave->ribbon_span_scale : 1.30f;
    drift_x = TitanXmbSpline_WaveFloat(time_phase * 0.23f + 17.0f) * wave->ribbon_drift_x;
    drift_y = TitanXmbSpline_WaveFloat(time_phase * 0.19f + 73.0f) * wave->ribbon_drift_y;
    transition_flatten = TitanXmbSpline_Lerp(1.0f, 0.94f, page_transition);
    idle_breathe = 1.0f + idle_intensity * 0.018f;
    motion_scale = 1.0f + horizontal_motion * 0.018f;
    g_primary_wave_dynamic_visibility[primary_wave_index] = 1.0f;

    for (point_index = 0u; point_index < TITAN_WAVE_VU1_POINT_COUNT; point_index++)
    {
        float amount;
        float sample_before;
        float sample_after;
        float x_base;
        float center_y;
        float before_x;
        float before_y;
        float after_x;
        float after_y;
        float tangent_x;
        float tangent_y;
        float normal_x;
        float normal_y;
        float inverse_length;
        float correction;
        float broad_a;
        float broad_b;
        float broad_c;
        float edge_amount;
        float edge_curtain;
        float twist_phase;
        float twist_sin;
        float twist_cos;
        float thickness_breathe;
        float half_thickness;
        float projected_normal;
        float depth_projection;
        float cross_x;
        float cross_y;

        amount = (float)point_index / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT;
        sample_before = TitanXmbSpline_Clamp01(
            amount - 1.0f / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT
        );
        sample_after = TitanXmbSpline_Clamp01(
            amount + 1.0f / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT
        );
        x_base = (float)TITAN_UI_WIDTH * 0.5f +
            (amount - 0.5f) * (float)TITAN_UI_WIDTH * span * motion_scale;

        /* The reference is one transported sheet, not several independent
         * sine bands.  Its center line is evaluated from the same travelling
         * low-frequency field for every depth row. */
        broad_a = TitanXmbSpline_WaveFloat(
            time_phase * 0.41f + amount * wave->ribbon_fold_frequency + 9.0f
        );
        broad_b = TitanXmbSpline_WaveFloat(
            time_phase * 0.23f - amount * wave->ribbon_fold_frequency * 0.47f + 57.0f
        );
        broad_c = TitanXmbSpline_WaveFloat(
            time_phase * 0.13f + amount * wave->ribbon_fold_frequency * 0.19f + 101.0f
        );
        edge_amount = TitanXmbSpline_Absolute(amount - 0.5f) * 2.0f;
        edge_curtain = edge_amount * edge_amount;
        edge_curtain *= edge_curtain;
        center_y = wave->ribbon_center_y + drift_y +
            (broad_a * 0.62f + broad_b * 0.25f + broad_c * 0.13f) *
                wave->ribbon_fold_amplitude * transition_flatten;
        /* The sheet rises into the tall edge curtains visible during the
         * reference turn instead of fading at the viewport boundaries. */
        center_y += edge_curtain * wave->ribbon_fold_amplitude * 0.78f *
            TitanXmbSpline_WaveFloat(
                time_phase * 0.31f + (amount < 0.5f ? 33.0f : 91.0f)
            );
        center_y += vertical_transition * flow->vertical_shift - idle_intensity * 1.5f;

        /* Re-evaluate the center line on each side and transport the cross
         * section through its tangent.  All seven internal contours now share
         * this frame, so they cannot slide through or detach from the sheet. */
        before_x = (float)TITAN_UI_WIDTH * 0.5f +
            (sample_before - 0.5f) * (float)TITAN_UI_WIDTH * span * motion_scale;
        after_x = (float)TITAN_UI_WIDTH * 0.5f +
            (sample_after - 0.5f) * (float)TITAN_UI_WIDTH * span * motion_scale;
        before_y = wave->ribbon_center_y + drift_y +
            (TitanXmbSpline_WaveFloat(time_phase * 0.41f + sample_before * wave->ribbon_fold_frequency + 9.0f) * 0.62f +
             TitanXmbSpline_WaveFloat(time_phase * 0.23f - sample_before * wave->ribbon_fold_frequency * 0.47f + 57.0f) * 0.25f +
             TitanXmbSpline_WaveFloat(time_phase * 0.13f + sample_before * wave->ribbon_fold_frequency * 0.19f + 101.0f) * 0.13f) *
                wave->ribbon_fold_amplitude * transition_flatten;
        after_y = wave->ribbon_center_y + drift_y +
            (TitanXmbSpline_WaveFloat(time_phase * 0.41f + sample_after * wave->ribbon_fold_frequency + 9.0f) * 0.62f +
             TitanXmbSpline_WaveFloat(time_phase * 0.23f - sample_after * wave->ribbon_fold_frequency * 0.47f + 57.0f) * 0.25f +
             TitanXmbSpline_WaveFloat(time_phase * 0.13f + sample_after * wave->ribbon_fold_frequency * 0.19f + 101.0f) * 0.13f) *
                wave->ribbon_fold_amplitude * transition_flatten;
        tangent_x = after_x - before_x;
        tangent_y = after_y - before_y;
        inverse_length = 1.0f / (
            TitanXmbSpline_Absolute(tangent_x) +
            TitanXmbSpline_Absolute(tangent_y) + 0.0001f
        );
        tangent_x *= inverse_length;
        tangent_y *= inverse_length;
        correction = 1.5f - 0.5f *
            (tangent_x * tangent_x + tangent_y * tangent_y);
        tangent_x *= correction;
        tangent_y *= correction;
        correction = 1.5f - 0.5f *
            (tangent_x * tangent_x + tangent_y * tangent_y);
        tangent_x *= correction;
        tangent_y *= correction;
        normal_x = -tangent_y;
        normal_y = tangent_x;

        /* A broad attack line travels along the ribbon.  Keeping the full
         * signed cosine is important: it lets the rails reverse naturally
         * after the sheet passes through an almost edge-on silhouette. */
        twist_phase = time_phase * 0.79f +
            amount * wave->ribbon_fold_frequency +
            TitanXmbSpline_WaveFloat(time_phase * 0.17f + amount * 19.0f + 11.0f) * 9.0f +
            31.0f;
        twist_sin = TitanXmbSpline_WaveFloat(twist_phase);
        twist_cos = TitanXmbSpline_WaveFloat(twist_phase + 32.0f);
        thickness_breathe = 0.90f + 0.10f * TitanXmbSpline_WaveFloat(
            time_phase * 0.19f + amount * 23.0f + 11.0f
        );
        half_thickness = wave->ribbon_thickness * 0.5f *
            thickness_breathe * wave->thickness_scale * idle_breathe;
        /* Leave only a sub-pixel floor at ninety degrees.  The former 20%
         * floor was why the POC never produced the reference's thin attack
         * line.  A small depth projection preserves the sense of 3D without
         * inflating that line again. */
        projected_normal = half_thickness * twist_cos;
        depth_projection = wave->ribbon_twist_amplitude * twist_sin * 0.012f;
        cross_x = normal_x * projected_normal + tangent_x * depth_projection;
        cross_y = normal_y * projected_normal + tangent_y * depth_projection;

        g_rows[row_index][point_index].x = x_base + drift_x +
            row_depth * cross_x;
        g_rows[row_index][point_index].y = center_y +
            row_depth * cross_y;
    }
}

static void TitanXmbSpline_BuildFlow3dRow(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    unsigned int row_index,
    unsigned int primary_wave_index
)
{
    const TitanThemeFlowStyle *flow;
    float row_amount;
    float row_depth;
    const TitanThemePrimaryWave *primary_wave;
    float frequency;
    float speed;
    float state_amplitude;
    float motion_boost;
    float sweep_sin;
    float sweep_cos;
    float sweep_rot_sin;
    float sweep_rot_cos;
    float sweep_center_x;
    float sweep_center_y;
    float sweep_breathe;
    float sweep_visibility;
    float curve_morph_a;
    float curve_morph_b;
    float sweep_phase;
    float motion_span_scale;
    float motion_thickness_scale;
    float wander_tilt;
    unsigned int point_index;
    int hour;
    int night;

    flow = TitanTheme_GetFlowStyle();
    if (primary_wave_index >= flow->primary_wave_count ||
        primary_wave_index >= TITAN_THEME_MAX_PRIMARY_WAVES)
        primary_wave_index = 0u;
    primary_wave = &flow->primary_waves[primary_wave_index];
    row_amount = (float)row_index /
        (float)(TITAN_XMB_SPLINE_ROW_COUNT - 1u);
    row_depth = row_amount * 2.0f - 1.0f;
    if (primary_wave->reference_ribbon_enabled != 0u)
    {
        TitanXmbSpline_BuildReferenceRibbonRow(
            frame, page_transition, idle_intensity, horizontal_motion,
            vertical_transition, row_index, primary_wave_index, flow, primary_wave
        );
        return;
    }
    hour = TitanSystemClock_GetHour24();
    night = hour >= 0 && (hour < 6 || hour >= 18);
    frequency = flow->frequency *
        (night ? flow->night_frequency : flow->day_frequency);
    speed = flow->speed * (night ? flow->night_speed : flow->day_speed);
    motion_boost = 1.0f + horizontal_motion *
        (flow->horizontal_boost - 1.0f);
    speed *= motion_boost;
    state_amplitude = flow->amplitude * primary_wave->amplitude_scale *
        TitanXmbSpline_Lerp(1.0f, 0.92f, page_transition) *
        (1.0f + idle_intensity * 0.04f);

    sweep_phase = (float)primary_wave->sweep_phase;
    sweep_sin = 0.0f;
    sweep_cos = 1.0f;
    sweep_rot_sin = 0.0f;
    sweep_rot_cos = 1.0f;
    sweep_center_x = (float)TITAN_UI_WIDTH * 0.5f;
    sweep_center_y = (float)TITAN_UI_HEIGHT * 0.5f;
    sweep_breathe = 1.0f;
    sweep_visibility = 1.0f;
    curve_morph_a = 0.0f;
    curve_morph_b = 0.0f;
    motion_span_scale = 1.0f;
    motion_thickness_scale = 1.0f;
    wander_tilt = 0.0f;
    if (primary_wave->wander_enabled != 0u && primary_wave->wander_speed > 0.0f)
    {
        float wander_position;
        float wander_fraction;
        float wander_amount;
        float phase_from;
        float phase_to;
        float center_x_from;
        float center_x_to;
        float center_y_from;
        float center_y_to;
        float curve_a_from;
        float curve_a_to;
        float curve_b_from;
        float curve_b_to;
        float span_from;
        float span_to;
        float thickness_from;
        float thickness_to;
        float visibility_from;
        float visibility_to;
        float tilt_from;
        float tilt_to;
        unsigned int state_index;
        unsigned int seed_from;
        unsigned int seed_to;

        /* PS4 Flow does not follow a closed orbit. It continuously morphs
         * between pseudo-random spatial states: the ribbon can emerge from a
         * different edge, change direction/curvature, fade and later return.
         * Deterministic hashing keeps the motion reproducible while avoiding
         * a visible loop or any per-frame RNG/state allocation. */
        wander_position = (float)frame * primary_wave->wander_speed;
        state_index = (unsigned int)wander_position;
        wander_fraction = wander_position - (float)state_index;
        wander_amount = TitanXmbSpline_SmootherStep(wander_fraction);
        seed_from = primary_wave->wander_seed + state_index * 0x9e3779b9u;
        seed_to = primary_wave->wander_seed + (state_index + 1u) * 0x9e3779b9u;

        center_x_from = (float)TITAN_UI_WIDTH * 0.5f +
            TitanXmbSpline_HashSigned(seed_from + 0x11u) * primary_wave->wander_extent_x;
        center_x_to = (float)TITAN_UI_WIDTH * 0.5f +
            TitanXmbSpline_HashSigned(seed_to + 0x11u) * primary_wave->wander_extent_x;
        center_y_from = (float)TITAN_UI_HEIGHT * 0.5f +
            TitanXmbSpline_HashSigned(seed_from + 0x29u) * primary_wave->wander_extent_y;
        center_y_to = (float)TITAN_UI_HEIGHT * 0.5f +
            TitanXmbSpline_HashSigned(seed_to + 0x29u) * primary_wave->wander_extent_y;
        sweep_center_x = TitanXmbSpline_Lerp(center_x_from, center_x_to, wander_amount);
        sweep_center_y = TitanXmbSpline_Lerp(center_y_from, center_y_to, wander_amount);

        phase_from = TitanXmbSpline_Hash01(seed_from + 0x47u) * 128.0f;
        phase_to = TitanXmbSpline_Hash01(seed_to + 0x47u) * 128.0f;
        sweep_phase = TitanXmbSpline_PhaseLerp(phase_from, phase_to, wander_amount);
        sweep_rot_cos = TitanXmbSpline_WaveFloat(sweep_phase + 32.0f);
        sweep_rot_sin = TitanXmbSpline_WaveFloat(sweep_phase);

        curve_a_from = TitanXmbSpline_HashSigned(seed_from + 0x61u) * primary_wave->wander_curve;
        curve_a_to = TitanXmbSpline_HashSigned(seed_to + 0x61u) * primary_wave->wander_curve;
        curve_b_from = TitanXmbSpline_HashSigned(seed_from + 0x83u) * primary_wave->wander_curve;
        curve_b_to = TitanXmbSpline_HashSigned(seed_to + 0x83u) * primary_wave->wander_curve;
        curve_morph_a = TitanXmbSpline_Lerp(curve_a_from, curve_a_to, wander_amount);
        curve_morph_b = TitanXmbSpline_Lerp(curve_b_from, curve_b_to, wander_amount);

        span_from = 0.72f + TitanXmbSpline_Hash01(seed_from + 0xa7u) * 0.64f;
        span_to = 0.72f + TitanXmbSpline_Hash01(seed_to + 0xa7u) * 0.64f;
        motion_span_scale = TitanXmbSpline_Lerp(span_from, span_to, wander_amount);
        thickness_from = 0.72f + TitanXmbSpline_Hash01(seed_from + 0xc1u) * 0.56f;
        thickness_to = 0.72f + TitanXmbSpline_Hash01(seed_to + 0xc1u) * 0.56f;
        motion_thickness_scale = TitanXmbSpline_Lerp(
            thickness_from, thickness_to, wander_amount
        );

        visibility_from = primary_wave->wander_visibility_min +
            (1.0f - primary_wave->wander_visibility_min) *
            TitanXmbSpline_Hash01(seed_from + 0xe3u);
        visibility_to = primary_wave->wander_visibility_min +
            (1.0f - primary_wave->wander_visibility_min) *
            TitanXmbSpline_Hash01(seed_to + 0xe3u);
        sweep_visibility = TitanXmbSpline_Lerp(
            visibility_from, visibility_to, wander_amount
        );
        tilt_from = TitanXmbSpline_HashSigned(seed_from + 0x10du) * 92.0f;
        tilt_to = TitanXmbSpline_HashSigned(seed_to + 0x10du) * 92.0f;
        wander_tilt = TitanXmbSpline_Lerp(tilt_from, tilt_to, wander_amount);

        /* Tiny non-periodic-looking breathing rides on top of the keyframe
         * morph without defining the trajectory itself. */
        sweep_breathe = 0.94f + 0.08f * TitanXmbSpline_WaveFloat(
            (float)frame * 0.037f + (float)(primary_wave->wander_seed & 127u)
        );
        motion_span_scale *= sweep_breathe;
    }
    else if (primary_wave->sweep_enabled != 0u && primary_wave->sweep_speed > 0.0f)
    {
        /* The PS4 Flow reference does not look like a rigid strip being
         * rotated in place.  It behaves more like a living ribbon: a long
         * viewport sweep, slow visibility breathing, subtle orbital drift and
         * continuous local flexing.  We therefore keep the orbital tangent but
         * also modulate center, span and deformation over time. */
        float breathe_phase;
        sweep_phase += (float)frame * primary_wave->sweep_speed;
        sweep_sin = TitanXmbSpline_WaveFloat(sweep_phase);
        sweep_cos = TitanXmbSpline_WaveFloat(sweep_phase + 32.0f);
        sweep_rot_cos = -sweep_sin;
        sweep_rot_sin = sweep_cos;
        breathe_phase = sweep_phase * 0.25f + 19.0f;
        sweep_breathe = 0.90f + 0.16f * TitanXmbSpline_WaveFloat(breathe_phase);
        sweep_visibility = 0.72f + 0.28f *
            TitanXmbSpline_WaveFloat(sweep_phase * 0.125f + 43.0f);
        sweep_center_x += sweep_cos * primary_wave->sweep_orbit_x * sweep_breathe +
            TitanXmbSpline_WaveFloat(sweep_phase * 0.5f + 11.0f) * 18.0f;
        sweep_center_y += sweep_sin * primary_wave->sweep_orbit_y * sweep_breathe +
            TitanXmbSpline_WaveFloat(sweep_phase * 0.5f + 59.0f) * 12.0f;
        curve_morph_a = TitanXmbSpline_WaveFloat(sweep_phase * 0.31f + 7.0f) *
            58.0f * state_amplitude;
        curve_morph_b = TitanXmbSpline_WaveFloat(sweep_phase * 0.27f + 81.0f) *
            66.0f * state_amplitude;
    }
    g_primary_wave_dynamic_visibility[primary_wave_index] =
        TitanXmbSpline_Clamp01(sweep_visibility);

    for (point_index = 0u;
         point_index < TITAN_WAVE_VU1_POINT_COUNT;
         point_index++)
    {
        float amount;
        float edge;
        float x_base;
        float center_line;
        float wave_a;
        float wave_b;
        float wave_c;
        float cosine_a;
        float cosine_b;
        float noise_a;
        float noise_b;
        float noise;
        float gerstner_x;
        float gerstner_y;
        float depth_z;
        float perspective;
        float lower_crest;
        float ribbon_shape;
        float width_envelope;
        float visibility_envelope;
        float flex_bend;
        float span_pulse;
        float point_rot_sin;
        float point_rot_cos;
        float point_rot_delta;
        float time_phase;
        float phase_a;
        float phase_b;
        float phase_c;
        float spatial_a;
        float spatial_b;

        amount = (float)point_index /
            (float)TITAN_XMB_SPLINE_SEGMENT_COUNT;
        edge = TitanXmbSpline_Clamp01(
            (amount < 0.5f ? amount : 1.0f - amount) * 7.0f
        );
        edge = edge * edge * (3.0f - 2.0f * edge);
        ribbon_shape = TitanXmbSpline_Clamp01(amount * (1.0f - amount) * 4.0f);
        width_envelope = TitanXmbSpline_Lerp(0.54f, 1.12f, ribbon_shape);
        visibility_envelope = TitanXmbSpline_Lerp(0.66f, 1.08f, ribbon_shape) *
            sweep_visibility;
        if (g_snake_active[primary_wave_index] != 0u)
        {
            width_envelope *= g_snake_spine[primary_wave_index][point_index].width;
            visibility_envelope *=
                g_snake_spine[primary_wave_index][point_index].visibility;
        }
        x_base = -78.0f +
            amount * ((float)TITAN_UI_WIDTH + 156.0f);
        /* PS4-Flow-style trajectory: the theme owns a cubic path rather than
         * only a straight start/end baseline. Two independently curved primary
         * sheets can therefore cross, separate and rejoin without collapsing
         * into the old horizontal center band. */
        center_line = TitanXmbSpline_CubicBezierY(
            primary_wave->start_y,
            primary_wave->control1_y + curve_morph_a,
            primary_wave->control2_y + curve_morph_b,
            primary_wave->end_y,
            amount
        );
        center_line += (amount - 0.5f) * wander_tilt;
        center_line += primary_wave->drift_y * TitanXmbSpline_WaveFloat(
            (float)frame * 0.125f + (float)primary_wave->phase_offset
        );
        center_line += vertical_transition * flow->vertical_shift -
            idle_intensity * 2.5f;
        /* Fine per-point breathing moved to VU1 in BuildFix31. Keeping the
         * EE spine free of this carrier reduces per-row trig/LUT work and
         * avoids applying the same deformation twice on hardware. */

        time_phase = (float)frame * speed * 0.34f +
            (float)primary_wave->phase_offset;
        spatial_a = (float)point_index * 5.2f * frequency;
        spatial_b = (float)point_index * 8.7f * frequency;
        phase_a = time_phase + spatial_a + (float)row_index * 11.0f;
        phase_b = time_phase * 0.5f + spatial_b +
            (float)row_index * 23.0f + 37.0f;
        phase_c = time_phase * 0.25f +
            (float)point_index * 3.1f * frequency +
            (float)row_index * 29.0f + 83.0f;

        wave_a = TitanXmbSpline_WaveFloat(phase_a);
        cosine_a = TitanXmbSpline_WaveFloat(phase_a + 32.0f);
        wave_b = TitanXmbSpline_WaveFloat(phase_b);
        cosine_b = TitanXmbSpline_WaveFloat(phase_b + 32.0f);
        wave_c = TitanXmbSpline_WaveFloat(phase_c);

        /* Two low-frequency decorrelated samples approximate a tiny temporal
         * 3-D gradient-noise field without spending EE cycles on full Perlin.
         * It modulates the Gerstner carriers rather than becoming a visible
         * texture, so the surface remains organic and non-repeating. */
        noise_a = TitanXmbSpline_WaveFloat(
            time_phase * 0.125f + (float)point_index * 2.0f +
            (float)row_index * 31.0f + 17.0f
        );
        noise_b = TitanXmbSpline_WaveFloat(
            time_phase * 0.0625f + (float)point_index * 7.0f -
            (float)row_index * 13.0f + 101.0f
        );
        noise = (noise_a * 0.58f + noise_b * 0.42f) * flow->noise;

        gerstner_x = (
            cosine_a * 12.0f + cosine_b * 5.8f + noise * 3.1f
        ) * state_amplitude * edge * width_envelope;
        gerstner_y = (
            wave_a * 31.0f + wave_b * 15.5f + wave_c * 8.0f +
            noise * 10.8f
        ) * state_amplitude * edge * visibility_envelope;

        /* Z exists in the theme mesh even though the current VU1 ribbon
         * microprogram receives the projected XY rails. Projecting here gives
         * the PS2 real parallax/depth behavior without introducing a second
         * GS perspective pipeline or consuming more VU1 data memory. */
        depth_z = row_depth * 38.0f * width_envelope +
            cosine_b * 17.0f * state_amplitude * visibility_envelope +
            noise * 11.0f;
        perspective = TitanXmbSpline_Clamp(
            1.0f + depth_z * 0.00175f,
            0.90f,
            1.12f
        );

        lower_crest = vertical_transition * (0.25f + row_amount * 0.75f) *
            TitanXmbSpline_WaveFloat(
                time_phase * 0.5f + (float)point_index * 4.0f +
                (float)row_index * 19.0f + 54.0f
            ) * 17.0f * edge * visibility_envelope;
        /* The high-frequency flex carrier is now produced by VU1 EFU/ESIN.
         * EE keeps only the broad Bézier/Gerstner spine and depth envelope. */
        flex_bend = 0.0f;
        span_pulse = 0.92f + 0.18f * TitanXmbSpline_WaveFloat(
            time_phase * 0.125f + (float)point_index + 73.0f
        );

        if (g_snake_active[primary_wave_index] != 0u)
        {
            const TitanXmbSnakePoint *snake =
                &g_snake_spine[primary_wave_index][point_index];
            float tangent_x = snake->tangent_x;
            float tangent_y = snake->tangent_y;
            float normal_x = -tangent_y;
            float normal_y = tangent_x;
            float across =
                row_depth * 31.0f * primary_wave->thickness_scale *
                    width_envelope +
                gerstner_y * 0.42f + lower_crest * 0.35f;
            float along = gerstner_x * 0.28f +
                row_depth * horizontal_motion * 2.0f;

            /* Head-driven trailing model: every point in the spine samples an
             * older head position. Tangent therefore changes progressively
             * from head to tail; rotation is a consequence of the travelled
             * path, never a whole-ribbon transform. */
            g_rows[row_index][point_index].x = snake->x +
                tangent_x * along + normal_x * across;
            g_rows[row_index][point_index].y = snake->y +
                tangent_y * along + normal_y * across;
        }
        else if (primary_wave->wander_enabled != 0u || primary_wave->sweep_enabled != 0u)
        {
            float local_x;
            float local_y;
            float span_scale = primary_wave->sweep_span_scale > 0.0f
                ? primary_wave->sweep_span_scale : 1.0f;

            span_scale *= motion_span_scale *
                TitanXmbSpline_Lerp(0.78f, 1.12f, ribbon_shape) * span_pulse;
            local_x = (x_base + gerstner_x -
                (float)TITAN_UI_WIDTH * 0.5f) * perspective * span_scale +
                row_depth * horizontal_motion * 5.5f;
            local_y = (center_line - (float)TITAN_UI_HEIGHT * 0.5f +
                row_depth * 29.0f * primary_wave->thickness_scale *
                    motion_thickness_scale * width_envelope +
                gerstner_y +
                depth_z * 0.075f * primary_wave->thickness_scale + lower_crest +
                flex_bend) * width_envelope;
            point_rot_delta = TitanXmbSpline_WaveFloat(
                time_phase * 0.0625f + (float)point_index * 2.0f +
                (float)row_index * 7.0f + 41.0f
            ) * 0.12f * state_amplitude;
            point_rot_cos = sweep_rot_cos - point_rot_delta * sweep_rot_sin;
            point_rot_sin = sweep_rot_sin + point_rot_delta * sweep_rot_cos;

            g_rows[row_index][point_index].x = sweep_center_x +
                local_x * point_rot_cos - local_y * point_rot_sin;
            g_rows[row_index][point_index].y = sweep_center_y +
                local_x * point_rot_sin + local_y * point_rot_cos;
        }
        else
        {
            g_rows[row_index][point_index].x =
                ((float)TITAN_UI_WIDTH * 0.5f) +
                (x_base + gerstner_x - (float)TITAN_UI_WIDTH * 0.5f) *
                    perspective +
                row_depth * horizontal_motion * 5.5f;
            g_rows[row_index][point_index].y = center_line +
                row_depth * 29.0f * primary_wave->thickness_scale + gerstner_y +
                depth_z * 0.075f * primary_wave->thickness_scale + lower_crest;
        }
    }
}

static void TitanXmbSpline_BuildStrips(void)
{
    unsigned int strip_index;

    for (strip_index = 0u;
         strip_index < TITAN_WAVE_VU1_LAYER_COUNT;
         strip_index++)
    {
        unsigned int point_index;

        for (point_index = 0u;
             point_index < TITAN_WAVE_VU1_POINT_COUNT;
             point_index++)
        {
            const TitanXmbSplineRowPoint *first;
            const TitanXmbSplineRowPoint *second;
            TitanWaveVu1Point *point;

            first = &g_rows[strip_index][point_index];
            second = &g_rows[strip_index + 1u][point_index];
            point = &g_strips[strip_index][point_index];

            point->x = (first->x + second->x) * 0.5f;
            point->y = (first->y + second->y) * 0.5f;
            point->normal_x = (second->x - first->x) * 0.5f;
            point->normal_y = (second->y - first->y) * 0.5f;
        }
    }
}

static void TitanXmbSpline_Rebuild(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    unsigned int primary_wave_index
)
{
    unsigned int row_index;
    const TitanThemeInfo *theme;

    theme = TitanTheme_GetActive();
    if (theme != 0 && theme->background_engine == TITAN_THEME_BACKGROUND_FLOW3D)
    {
        const TitanThemePrimaryWave *wave = &theme->flow.primary_waves[
            primary_wave_index < TITAN_THEME_MAX_PRIMARY_WAVES ? primary_wave_index : 0u
        ];
        if (wave->reference_ribbon_enabled != 0u)
            g_snake_active[primary_wave_index] = 0u;
        else
            TitanXmbSpline_PrepareSnakeSpine(frame, primary_wave_index, wave);
        for (row_index = 0u;
             row_index < TITAN_XMB_SPLINE_ROW_COUNT;
             row_index++)
        {
            TitanXmbSpline_BuildFlow3dRow(
                frame,
                page_transition,
                idle_intensity,
                horizontal_motion,
                vertical_transition,
                row_index,
                primary_wave_index
            );
        }
    }
    else
    {
        TitanXmbSpline_UpdateKernel(frame);
        for (row_index = 0u;
             row_index < TITAN_XMB_SPLINE_ROW_COUNT;
             row_index++)
        {
            TitanXmbSpline_BuildRow(
                frame,
                page_transition,
                idle_intensity,
                row_index
            );
        }
    }
    TitanXmbSpline_BuildStrips();

    g_cached_frame = frame;
    g_cached_transition = page_transition;
    g_cached_idle = idle_intensity;
    g_cached_horizontal_motion = horizontal_motion;
    g_cached_vertical_transition = vertical_transition;
    g_cached_theme_generation = TitanTheme_GetGeneration();
    g_cache_valid = 1;
}

static float TitanXmbSpline_EdgeFade(unsigned int index)
{
    float amount;
    float edge;

    amount = (float)index / (float)TITAN_XMB_SPLINE_SEGMENT_COUNT;
    edge = amount < 0.5f ? amount : 1.0f - amount;
    edge = TitanXmbSpline_Clamp01(edge * 8.0f);
    return edge * edge * (3.0f - 2.0f * edge);
}

static unsigned char TitanXmbSpline_ModulateAlpha(
    unsigned char alpha,
    float amount
)
{
    float value;

    value = (float)alpha * TitanXmbSpline_Clamp01(amount);
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

static void TitanXmbSpline_DrawFallbackStrip(unsigned int source_strip, unsigned int primary_wave_index, float opacity)
{
    TitanXmbSplineStripStyle style;
    const TitanWaveVu1Point *points;
    unsigned int index;

    style = TitanXmbSpline_GetStripStyle(source_strip, primary_wave_index);
    points = g_strips[source_strip];

    for (index = 0u; index < TITAN_XMB_SPLINE_SEGMENT_COUNT; index++)
    {
        float fade;
        unsigned char alpha;

        fade = (TitanXmbSpline_EdgeFade(index) +
            TitanXmbSpline_EdgeFade(index + 1u)) * 0.5f;
        alpha = TitanXmbSpline_ModulateAlpha(style.alpha, fade * 0.78f * opacity);

        TitanVideo_DrawSolidQuadAlpha(
            points[index].x - points[index].normal_x,
            points[index].y - points[index].normal_y,
            points[index].x + points[index].normal_x,
            points[index].y + points[index].normal_y,
            points[index + 1u].x + points[index + 1u].normal_x,
            points[index + 1u].y + points[index + 1u].normal_y,
            points[index + 1u].x - points[index + 1u].normal_x,
            points[index + 1u].y - points[index + 1u].normal_y,
            style.red,
            style.green,
            style.blue,
            alpha
        );
    }
}

static void TitanXmbSpline_DrawFallback(unsigned int primary_wave_index, float opacity)
{
    unsigned int draw_index;

    for (draw_index = 0u;
         draw_index < TITAN_WAVE_VU1_LAYER_COUNT;
         draw_index++)
    {
        TitanXmbSpline_DrawFallbackStrip(g_draw_order[draw_index], primary_wave_index, opacity);
    }
}

static void TitanXmbSpline_BuildVuLayers(
    TitanWaveVu1Layer *layers,
    unsigned int frame,
    unsigned int primary_wave_index,
    float opacity
)
{
    unsigned int draw_index;
    const TitanThemeInfo *theme;
    const TitanThemeFlowStyle *flow;
    const TitanThemePrimaryWave *primary_wave;
    float procedural_phase;
    float procedural_frequency;
    float procedural_amplitude;

    theme = TitanTheme_GetActive();
    flow = theme != 0 ? &theme->flow : 0;
    primary_wave = 0;
    procedural_phase = 0.0f;
    procedural_frequency = 0.0f;
    procedural_amplitude = 0.0f;
    if (theme != 0 &&
        theme->background_engine == TITAN_THEME_BACKGROUND_FLOW3D &&
        flow != 0 &&
        primary_wave_index < flow->primary_wave_count &&
        primary_wave_index < TITAN_THEME_MAX_PRIMARY_WAVES)
    {
        primary_wave = &flow->primary_waves[primary_wave_index];
        procedural_phase = (float)(frame & 8191u) * flow->vu1_deform_speed +
            (float)primary_wave->phase_offset;
        procedural_phase -= (float)((unsigned int)(procedural_phase / 128.0f)) *
            128.0f;
        procedural_frequency = flow->vu1_deform_frequency;
        procedural_amplitude = flow->vu1_deform_amplitude *
            primary_wave->amplitude_scale;
        if (primary_wave->reference_ribbon_enabled != 0u)
        {
            /* The calibrated center line already carries the complete motion.
             * Per-strip VU deformation would phase-shift the internal rails
             * and tear the visual surface apart, so Ribbon v2 deliberately
             * submits the coherent transported frame unchanged. */
            procedural_frequency = 0.0f;
            procedural_amplitude = 0.0f;
        }
    }

    for (draw_index = 0u;
         draw_index < TITAN_WAVE_VU1_LAYER_COUNT;
         draw_index++)
    {
        unsigned int source_strip;
        TitanXmbSplineStripStyle style;

        source_strip = g_draw_order[draw_index];
        style = TitanXmbSpline_GetStripStyle(source_strip, primary_wave_index);
        layers[draw_index].points = g_strips[source_strip];
        layers[draw_index].half_width = 1.0f;
        layers[draw_index].texture_u_scale = style.texture_u_scale;
        layers[draw_index].fresnel_bias = style.fresnel_bias;
        layers[draw_index].fresnel_scale = style.fresnel_scale;
        layers[draw_index].red = style.red;
        layers[draw_index].green = style.green;
        layers[draw_index].blue = style.blue;
        layers[draw_index].alpha = TitanXmbSpline_ModulateAlpha(style.alpha, opacity);
        layers[draw_index].glow_phase = (unsigned char)(
            (frame >> 2u) + source_strip * 21u
        );
        layers[draw_index].blend_mode = style.blend_mode;
        layers[draw_index].procedural_phase = procedural_phase +
            (float)source_strip * 7.0f;
        layers[draw_index].procedural_frequency = procedural_frequency *
            (1.0f + (float)source_strip * 0.017f);
        layers[draw_index].procedural_amplitude = procedural_amplitude *
            (0.72f + (float)source_strip * 0.055f);
    }
}

void TitanXmbSpline_Initialize(void)
{
    if (g_spline_initialized)
    {
        return;
    }

    g_spline_initialized = 1;
    g_temporal_valid = 0;
    g_cache_valid = 0;
    g_cached_frame = 0u;
    g_cached_transition = 0.0f;
    g_cached_idle = 0.0f;
    g_cached_horizontal_motion = 0.0f;
    g_cached_vertical_transition = 0.0f;
    g_cached_theme_generation = 0u;

    TitanXmbSpline_BuildDescriptorTable();
    TitanXmbSpline_BuildBasisTable();
    TitanWaveVu1_Initialize();
}

void TitanXmbSpline_InvalidateTheme(void)
{
    g_cache_valid = 0;
    g_temporal_valid = 0;
    g_cached_theme_generation = 0u;
}

int TitanXmbSpline_IsVuActive(void)
{
    return TitanWaveVu1_IsAvailable();
}

void TitanXmbSpline_DrawWithOpacity(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    float opacity
)
{
    TitanWaveVu1Layer layers[TITAN_WAVE_VU1_LAYER_COUNT];
    const TitanThemeInfo *theme;
    const TitanThemeFlowStyle *flow;
    float transition;
    float idle;
    float horizontal;
    float vertical;
    unsigned int primary_wave_index;
    unsigned int primary_wave_count;

    if (!g_spline_initialized)
        TitanXmbSpline_Initialize();

    transition = TitanXmbSpline_Clamp01(page_transition);
    idle = TitanXmbSpline_Clamp01(idle_intensity);
    horizontal = TitanXmbSpline_Clamp01(horizontal_motion);
    vertical = TitanXmbSpline_Clamp01(vertical_transition);
    opacity = TitanXmbSpline_Clamp01(opacity);
    theme = TitanTheme_GetActive();

    /* Flow3D themes own their ribbon layout. A theme may define up to three
     * primary sheets. Legacy themes may use orbital sweep or stochastic
     * whole-sheet wander. Scarlet Flow uses the head-driven snake path: its
     * head follows a smooth pseudo-random spline and every later body sample
     * follows an older head position. Rotation/turning therefore propagates
     * through the ribbon rather than transforming the complete sheet. */
    if (theme != 0 && theme->background_engine == TITAN_THEME_BACKGROUND_FLOW3D)
    {
        flow = &theme->flow;
        primary_wave_count = flow->primary_wave_count;
        if (primary_wave_count < 1u) primary_wave_count = 1u;
        if (primary_wave_count > TITAN_THEME_MAX_PRIMARY_WAVES)
            primary_wave_count = TITAN_THEME_MAX_PRIMARY_WAVES;

        for (primary_wave_index = 0u;
             primary_wave_index < primary_wave_count;
             primary_wave_index++)
        {
            TitanXmbSpline_Rebuild(
                frame,
                transition,
                idle,
                horizontal,
                vertical,
                primary_wave_index
            );
            TitanXmbSpline_BuildVuLayers(layers, frame, primary_wave_index, opacity);
            if (!TitanWaveVu1_Draw(layers, TITAN_WAVE_VU1_LAYER_COUNT))
                TitanXmbSpline_DrawFallback(primary_wave_index, opacity);
        }
        return;
    }

    if (
        !g_cache_valid ||
        frame != g_cached_frame ||
        g_cached_theme_generation != TitanTheme_GetGeneration() ||
        TitanXmbSpline_Absolute(transition - g_cached_transition) > 0.01f ||
        TitanXmbSpline_Absolute(idle - g_cached_idle) > 0.01f ||
        TitanXmbSpline_Absolute(horizontal - g_cached_horizontal_motion) > 0.01f ||
        TitanXmbSpline_Absolute(vertical - g_cached_vertical_transition) > 0.01f ||
        frame < g_cached_frame
    )
    {
        TitanXmbSpline_Rebuild(
            frame,
            transition,
            idle,
            horizontal,
            vertical,
            0u
        );
    }

    TitanXmbSpline_BuildVuLayers(layers, frame, 0u, opacity);
    if (!TitanWaveVu1_Draw(layers, TITAN_WAVE_VU1_LAYER_COUNT))
        TitanXmbSpline_DrawFallback(0u, opacity);
}

void TitanXmbSpline_Draw(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition
)
{
    TitanXmbSpline_DrawWithOpacity(
        frame, page_transition, idle_intensity, horizontal_motion,
        vertical_transition, 1.0f
    );
}
