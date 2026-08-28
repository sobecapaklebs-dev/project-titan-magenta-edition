#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "titan_video.h"
#include "titan_wave_vu1.h"
#include "titan_xmb_spline.h"
#include "titan_theme.h"


static TitanThemeInfo g_test_theme;

const TitanThemeInfo *TitanTheme_GetActive(void)
{
    return &g_test_theme;
}

const TitanThemeFlowStyle *TitanTheme_GetFlowStyle(void)
{
    return &g_test_theme.flow;
}

unsigned int TitanTheme_GetGeneration(void)
{
    return 1u;
}

int TitanSystemClock_GetHour24(void)
{
    return 12;
}

static unsigned int g_draw_calls;
static float g_minimum_y = 1000000.0f;
static float g_maximum_y = -1000000.0f;
static unsigned int g_reference_draw_calls;
static float g_reference_min_half_width = 1000000.0f;
static float g_reference_max_half_width = 0.0f;
static float g_reference_min_x = 1000000.0f;
static float g_reference_max_x = -1000000.0f;

void TitanWaveVu1_Initialize(void)
{
}

int TitanWaveVu1_IsAvailable(void)
{
    return 1;
}

int TitanWaveVu1_Draw(
    const TitanWaveVu1Layer *layers,
    unsigned int layer_count
)
{
    unsigned int layer;

    assert(layer_count == TITAN_WAVE_VU1_LAYER_COUNT);
    for (layer = 0u; layer < layer_count; layer++)
    {
        unsigned int point_index;

        assert(layers[layer].points != 0);
        assert(layers[layer].half_width == 1.0f);
        assert(layers[layer].fresnel_bias >= 0.0f);
        assert(layers[layer].fresnel_scale >= 0.0f);
        if (g_test_theme.flow.primary_waves[0].reference_ribbon_enabled != 0u)
        {
            assert(layers[layer].procedural_frequency == 0.0f);
            assert(layers[layer].procedural_amplitude == 0.0f);
        }

        for (point_index = 0u;
             point_index < TITAN_WAVE_VU1_POINT_COUNT;
             point_index++)
        {
            const TitanWaveVu1Point *point;
            float first_y;
            float second_y;

            point = &layers[layer].points[point_index];
            assert(isfinite(point->x));
            assert(isfinite(point->y));
            assert(isfinite(point->normal_x));
            assert(isfinite(point->normal_y));

            if (g_test_theme.flow.primary_waves[0].reference_ribbon_enabled != 0u)
            {
                float half_width = fabsf(point->normal_x) + fabsf(point->normal_y);
                if (half_width < g_reference_min_half_width)
                    g_reference_min_half_width = half_width;
                if (half_width > g_reference_max_half_width)
                    g_reference_max_half_width = half_width;
                if (point->x < g_reference_min_x) g_reference_min_x = point->x;
                if (point->x > g_reference_max_x) g_reference_max_x = point->x;
            }

            first_y = point->y - point->normal_y;
            second_y = point->y + point->normal_y;
            if (first_y < g_minimum_y)
            {
                g_minimum_y = first_y;
            }
            if (second_y < g_minimum_y)
            {
                g_minimum_y = second_y;
            }
            if (first_y > g_maximum_y)
            {
                g_maximum_y = first_y;
            }
            if (second_y > g_maximum_y)
            {
                g_maximum_y = second_y;
            }
        }
    }

    if (g_test_theme.flow.primary_waves[0].reference_ribbon_enabled != 0u)
        g_reference_draw_calls++;
    g_draw_calls++;
    return 1;
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
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}

int main(void)
{
    unsigned int frame;

    g_test_theme.background_engine = TITAN_THEME_BACKGROUND_DEFAULT;
    g_test_theme.flow.speed = 1.0f;
    g_test_theme.flow.frequency = 1.0f;
    g_test_theme.flow.amplitude = 1.0f;
    g_test_theme.flow.noise = 1.0f;
    g_test_theme.flow.fresnel = 1.0f;
    g_test_theme.flow.horizontal_boost = 1.7f;
    g_test_theme.flow.vertical_shift = 18.0f;
    g_test_theme.flow.day_frequency = 1.0f;
    g_test_theme.flow.night_frequency = 1.0f;
    g_test_theme.flow.day_speed = 1.0f;
    g_test_theme.flow.night_speed = 1.0f;
    TitanXmbSpline_Initialize();
    for (frame = 0u; frame < 3600u; frame++)
    {
        float transition;
        float idle;

        transition = (float)(frame % 240u) / 239.0f;
        idle = (float)(frame % 180u) / 179.0f;
        TitanXmbSpline_Draw(frame, transition, idle, 0.0f, 0.0f);
    }

    assert(g_draw_calls == 3600u);
    assert(g_minimum_y > 100.0f);
    assert(g_maximum_y < 410.0f);

    /* Flow3D themes may define more than one independently positioned primary
     * ribbon. One frame with two primary waves must submit the VU sheet twice. */
    g_test_theme.background_engine = TITAN_THEME_BACKGROUND_FLOW3D;
    g_test_theme.flow.primary_wave_count = 2u;
    g_test_theme.flow.primary_waves[0].start_y = 332.0f;
    g_test_theme.flow.primary_waves[0].control1_y = 408.0f;
    g_test_theme.flow.primary_waves[0].control2_y = 72.0f;
    g_test_theme.flow.primary_waves[0].end_y = 118.0f;
    g_test_theme.flow.primary_waves[0].drift_y = 13.0f;
    g_test_theme.flow.primary_waves[0].thickness_scale = 1.12f;
    g_test_theme.flow.primary_waves[0].amplitude_scale = 0.61f;
    g_test_theme.flow.primary_waves[0].alpha_scale = 1.0f;
    g_test_theme.flow.primary_waves[0].phase_offset = 0u;
    g_test_theme.flow.primary_waves[1].start_y = 198.0f;
    g_test_theme.flow.primary_waves[1].control1_y = 108.0f;
    g_test_theme.flow.primary_waves[1].control2_y = 356.0f;
    g_test_theme.flow.primary_waves[1].end_y = 264.0f;
    g_test_theme.flow.primary_waves[1].drift_y = 9.0f;
    g_test_theme.flow.primary_waves[1].thickness_scale = 0.76f;
    g_test_theme.flow.primary_waves[1].amplitude_scale = 0.47f;
    g_test_theme.flow.primary_waves[1].alpha_scale = 0.65f;
    g_test_theme.flow.primary_waves[1].phase_offset = 53u;
    TitanXmbSpline_InvalidateTheme();
    TitanXmbSpline_Draw(3601u, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(g_draw_calls == 3602u);

    /* Reference-video model: one transported sheet must overscan the viewport,
     * occupy a broad face-on silhouette, collapse to a thin attack line, and
     * keep the per-strip VU deformation disabled for the complete slow cycle. */
    g_test_theme.flow.primary_wave_count = 1u;
    g_test_theme.flow.primary_waves[0].thickness_scale = 1.0f;
    g_test_theme.flow.primary_waves[0].amplitude_scale = 1.0f;
    g_test_theme.flow.primary_waves[0].alpha_scale = 1.0f;
    g_test_theme.flow.primary_waves[0].sweep_enabled = 0u;
    g_test_theme.flow.primary_waves[0].reference_ribbon_enabled = 1u;
    g_test_theme.flow.primary_waves[0].ribbon_speed = 0.050f;
    g_test_theme.flow.primary_waves[0].ribbon_center_y = 224.0f;
    g_test_theme.flow.primary_waves[0].ribbon_thickness = 352.0f;
    g_test_theme.flow.primary_waves[0].ribbon_fold_amplitude = 88.0f;
    g_test_theme.flow.primary_waves[0].ribbon_fold_frequency = 96.0f;
    g_test_theme.flow.primary_waves[0].ribbon_twist_amplitude = 235.0f;
    g_test_theme.flow.primary_waves[0].ribbon_span_scale = 1.64f;
    g_test_theme.flow.primary_waves[0].ribbon_drift_x = 120.0f;
    g_test_theme.flow.primary_waves[0].ribbon_drift_y = 86.0f;
    g_test_theme.flow.primary_waves[0].ribbon_seed = 52391u;
    TitanXmbSpline_InvalidateTheme();
    for (frame = 0u; frame < 2560u; frame += 8u)
        TitanXmbSpline_Draw(frame, 0.0f, 0.0f, 0.0f, 0.0f);
    assert(g_reference_draw_calls == 320u);
    assert(g_reference_min_half_width < 0.8f);
    assert(g_reference_max_half_width > 20.0f);
    assert(g_reference_min_x < 0.0f);
    assert(g_reference_max_x > 640.0f);
    assert(g_draw_calls == 3922u);
    printf(
        "xmb-spline-runtime: OK draws=%u y=[%.2f, %.2f] ribbon-half=[%.2f, %.2f] x=[%.2f, %.2f]\n",
        g_draw_calls,
        g_minimum_y,
        g_maximum_y,
        g_reference_min_half_width,
        g_reference_max_half_width,
        g_reference_min_x,
        g_reference_max_x
    );
    return 0;
}
