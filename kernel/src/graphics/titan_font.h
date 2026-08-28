#ifndef PROJECT_TITAN_FONT_H
#define PROJECT_TITAN_FONT_H

#include "titan_result.h"

TitanResult TitanFont_Initialize(void);
TitanResult TitanFont_UploadAtlas(void);

void TitanFont_DrawText(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

void TitanFont_DrawTextAlpha(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanFont_DrawTextShadow(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

float TitanFont_MeasureText(
    float scale,
    const char *text
);

float TitanFont_MeasureTextRaster(
    float horizontal_scale,
    const char *text
);

void TitanFont_RasterizeText16(
    unsigned short *pixels,
    unsigned int surface_width,
    unsigned int surface_height,
    unsigned int stride,
    int x,
    int y,
    float horizontal_scale,
    float vertical_scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

#endif
