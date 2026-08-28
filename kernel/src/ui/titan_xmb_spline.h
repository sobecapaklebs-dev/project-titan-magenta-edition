#ifndef PROJECT_TITAN_XMB_SPLINE_H
#define PROJECT_TITAN_XMB_SPLINE_H

void TitanXmbSpline_Initialize(void);
void TitanXmbSpline_InvalidateTheme(void);
int TitanXmbSpline_IsVuActive(void);

void TitanXmbSpline_Draw(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition
);

/* Opacity-aware variant used only when the shell crossfades between full
 * scene-v2 artwork and the functional SYSTEM ribbon. The legacy Draw entry
 * point remains source/ABI compatible and is equivalent to opacity=1. */
void TitanXmbSpline_DrawWithOpacity(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    float opacity
);

#endif
