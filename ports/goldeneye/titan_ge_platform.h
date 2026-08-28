#ifndef PROJECT_TITAN_GOLDENEYE_PLATFORM_H
#define PROJECT_TITAN_GOLDENEYE_PLATFORM_H

#include "../../kernel/include/titan_game_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TitanGePlatform
{
    const TitanGameApiV1 *api;
    TitanGamePadState pads[TITAN_GAME_MAX_PADS];
} TitanGePlatform;

int TitanGePlatform_Attach(TitanGePlatform *platform, const TitanGameApiV1 *api);
void TitanGePlatform_Poll(TitanGePlatform *platform);
const TitanGamePadState *TitanGePlatform_GetPad(const TitanGePlatform *platform, unsigned int index);

int TitanGePlatform_BeginFrame(TitanGePlatform *platform, unsigned char r, unsigned char g, unsigned char b);
void TitanGePlatform_SetMvp(TitanGePlatform *platform, const float matrix16[16]);
int TitanGePlatform_DrawTriangles(
    TitanGePlatform *platform,
    const TitanGameVertex3D *vertices, unsigned int vertex_count,
    const unsigned short *indices, unsigned int index_count
);
int TitanGePlatform_DrawTexturedTriangles(
    TitanGePlatform *platform,
    const TitanGameVertex3D *vertices, unsigned int vertex_count,
    const unsigned short *indices, unsigned int index_count,
    unsigned int texture_handle
);
int TitanGePlatform_UploadTextureRgba32(
    TitanGePlatform *platform,
    const unsigned char *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int *texture_handle
);
int TitanGePlatform_UploadTextureI8(
    TitanGePlatform *platform,
    const unsigned char *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    unsigned int *texture_handle
);
int TitanGePlatform_ReleaseTexture(
    TitanGePlatform *platform,
    unsigned int texture_handle
);
int TitanGePlatform_SetRenderState(
    TitanGePlatform *platform,
    const TitanGameRenderState *state
);
int TitanGePlatform_DrawMaterialTriangles(
    TitanGePlatform *platform,
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    unsigned int texture0_handle,
    unsigned int texture1_handle
);
int TitanGePlatform_EndFrame(TitanGePlatform *platform);

int TitanGePlatform_BeginAudio(TitanGePlatform *platform);
unsigned int TitanGePlatform_SubmitPcm(
    TitanGePlatform *platform, const short *samples, unsigned int frames
);
void TitanGePlatform_EndAudio(TitanGePlatform *platform);

#ifdef __cplusplus
}
#endif
#endif
