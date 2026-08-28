#ifndef PROJECT_TITAN_UI_TRANSITION_H
#define PROJECT_TITAN_UI_TRANSITION_H

typedef enum TitanUiTransitionPhase {
    TITAN_UI_TRANSITION_ENTER_BEGIN = 0,
    TITAN_UI_TRANSITION_ENTER_PROGRESS,
    TITAN_UI_TRANSITION_ENTER_END,
    TITAN_UI_TRANSITION_EXIT_BEGIN,
    TITAN_UI_TRANSITION_EXIT_PROGRESS,
    TITAN_UI_TRANSITION_EXIT_END
} TitanUiTransitionPhase;

typedef struct TitanUiTransitionSample {
    TitanUiTransitionPhase phase;
    float progress;
    float source_visibility;
    float destination_visibility;
    float source_scale;
    float source_offset_y;
    float destination_scale;
    float destination_offset_y;
    float wave_progress;
} TitanUiTransitionSample;

void TitanUiTransition_Sample(float progress, int entering, TitanUiTransitionSample *sample);

#endif
