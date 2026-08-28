#include "titan_ui_transition.h"
#include "titan_animation.h"

#define TITAN_UI_HOME_EXIT_END 0.32f
#define TITAN_UI_WAVE_START 0.20f
#define TITAN_UI_WAVE_END 0.74f
#define TITAN_UI_DEST_START 0.60f

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void TitanUiTransition_Sample(float progress, int entering, TitanUiTransitionSample *sample)
{
    float p, source_exit, dest_enter, wave;
    if (sample == 0) return;
    p = Clamp01(progress);
    source_exit = TitanAnimation_SmoothStep(Clamp01(p / TITAN_UI_HOME_EXIT_END));
    wave = TitanAnimation_SmoothStep(Clamp01((p - TITAN_UI_WAVE_START) / (TITAN_UI_WAVE_END - TITAN_UI_WAVE_START)));
    dest_enter = TitanAnimation_SmoothStep(Clamp01((p - TITAN_UI_DEST_START) / (1.0f - TITAN_UI_DEST_START)));
    sample->progress = p;
    sample->source_visibility = 1.0f - source_exit;
    sample->destination_visibility = dest_enter;
    sample->source_scale = 1.0f - source_exit * 0.14f;
    sample->source_offset_y = -10.0f * source_exit;
    sample->destination_scale = 0.76f + dest_enter * 0.24f;
    sample->destination_offset_y = (1.0f - dest_enter) * 20.0f;
    sample->wave_progress = wave;
    if (entering) sample->phase = p <= 0.0f ? TITAN_UI_TRANSITION_ENTER_BEGIN : (p >= 1.0f ? TITAN_UI_TRANSITION_ENTER_END : TITAN_UI_TRANSITION_ENTER_PROGRESS);
    else sample->phase = p >= 1.0f ? TITAN_UI_TRANSITION_EXIT_BEGIN : (p <= 0.0f ? TITAN_UI_TRANSITION_EXIT_END : TITAN_UI_TRANSITION_EXIT_PROGRESS);
}
