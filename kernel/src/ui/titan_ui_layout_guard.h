#ifndef PROJECT_TITAN_UI_LAYOUT_GUARD_H
#define PROJECT_TITAN_UI_LAYOUT_GUARD_H

#include <stdint.h>

typedef struct TitanUiRect { float x, y, width, height; } TitanUiRect;
typedef enum TitanUiAnchor { TITAN_UI_ANCHOR_TOP_LEFT=0, TITAN_UI_ANCHOR_TOP_RIGHT, TITAN_UI_ANCHOR_BOTTOM_LEFT, TITAN_UI_ANCHOR_BOTTOM_RIGHT } TitanUiAnchor;

typedef struct TitanUiTextBounds {
    float measured_width, measured_height, max_width;
    unsigned int max_lines;
    TitanUiRect clip_rect;
    float padding;
    int ellipsis;
} TitanUiTextBounds;

float TitanUiFocus_Step(float current, int target, float rate);
unsigned int TitanUiFocus_UniqueIndex(int target, unsigned int count);
int TitanUiRect_Overlaps(const TitanUiRect *a, const TitanUiRect *b);
TitanUiRect TitanUiLayout_PlaceAnchored(float canvas_w, float canvas_h, float width, float height, float margin, TitanUiAnchor anchor);
TitanUiAnchor TitanUiLayout_SelectSafeAnchor(float canvas_w, float canvas_h, float width, float height, float margin, const TitanUiRect *exclusions, unsigned int exclusion_count);
float TitanUiText_MeasureCached(float scale, const char *text, float (*measure)(float,const char *));
void TitanUiText_ResetMetricsCache(void);

#endif
