#ifndef PROJECT_TITAN_AVATARS_H
#define PROJECT_TITAN_AVATARS_H

#include "titan_avatar_ids.h"
#include "titan_result.h"

typedef enum TitanAssetState
{
    TITAN_ASSET_UNLOADED = 0,
    TITAN_ASSET_LOADING,
    TITAN_ASSET_READY,
    TITAN_ASSET_FAILED
} TitanAssetState;

TitanResult TitanAvatars_Initialize(void);
TitanResult TitanAvatars_Retry(void);
int TitanAvatars_IsReady(void);
TitanAssetState TitanAvatars_GetState(void);
TitanResult TitanAvatars_GetLastResult(void);

/* Copies the immutable built-in avatar atlas into caller-owned EE RAM.
 * The generated bin2c symbols remain private to titan_avatars.c so there is
 * exactly one definition of the embedded atlas in the final kernel. */
int TitanAvatars_CopyBaseAtlas(
    unsigned short *destination,
    unsigned int pixel_capacity
);

void TitanAvatars_Draw(
    unsigned int avatar_id,
    float x,
    float y,
    float size,
    unsigned char alpha
);

#endif
