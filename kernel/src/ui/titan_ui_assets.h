#ifndef PROJECT_TITAN_UI_ASSETS_H
#define PROJECT_TITAN_UI_ASSETS_H

#include "titan_result.h"

TitanResult TitanUiAssets_Initialize(void);

void TitanUiAssets_DrawGamePlaceholder(
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanUiAssets_DrawAlert(
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanUiAssets_DrawBootLogo(
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanUiAssets_DrawAppHome(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
);

void TitanUiAssets_DrawFtpHome(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
);

void TitanUiAssets_DrawSystemIcon(
    unsigned int icon_index,
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

#endif
