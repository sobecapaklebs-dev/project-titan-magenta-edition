#ifndef PROJECT_TITAN_ASSET_IMAGE_H
#define PROJECT_TITAN_ASSET_IMAGE_H

#include "titan_asset_service.h"

typedef struct TitanAssetImageOptions
{
    int (*should_cancel)(void *user_data);
    void (*yield)(void *user_data);
    void (*progress)(void *user_data, unsigned int completed, unsigned int total);
    void *user_data;
    unsigned int yield_every_rows;
    /* Remote artwork smaller than these bounds is rejected before resize.
     * This prevents tiny thumbnails from becoming blurred full-tile art. */
    unsigned int minimum_source_width;
    unsigned int minimum_source_height;
} TitanAssetImageOptions;

/* Converts compressed PNG/JPEG bytes to the tiny TIMG v2 cache format. The
 * implementation decodes one source row at a time and writes final GS PSM16
 * pixels directly into destination. */
int TitanAssetImage_ConvertToTimgEx(
    const unsigned char *source,
    unsigned int source_size,
    TitanAssetKind kind,
    unsigned char *destination,
    unsigned int destination_capacity,
    unsigned int *out_size,
    const TitanAssetImageOptions *options
);

/* Compatibility wrapper for host tools/tests and non-cooperative callers. */
/* Decodes PNG/JPEG to a caller-owned RGB565 media canvas using FIT semantics:
 * the full source image is preserved and letterboxed as needed. */
int TitanAssetImage_DecodeMediaRgb565Fit(
    const unsigned char *source,
    unsigned int source_size,
    unsigned short *destination,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    const TitanAssetImageOptions *options
);

int TitanAssetImage_ConvertToTimg(
    const unsigned char *source,
    unsigned int source_size,
    TitanAssetKind kind,
    unsigned char *destination,
    unsigned int destination_capacity,
    unsigned int *out_size
);

#endif
