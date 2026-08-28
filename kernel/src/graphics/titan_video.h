#ifndef PROJECT_TITAN_VIDEO_H
#define PROJECT_TITAN_VIDEO_H

#include <stdint.h>

#include "titan_result.h"
#include "titan_game_sdk.h"
#include "titan_video_game_texture.h"
#include "titan_pikavr.h"
#include "titan_video_720p_plan.h"

#define TITAN_VIDEO_LEGACY_WIDTH 640u
#define TITAN_VIDEO_LEGACY_HEIGHT 448u
#define TITAN_VIDEO_HD_WIDTH 1280u
#define TITAN_VIDEO_HD_HEIGHT 720u
#define TITAN_VIDEO_HD_STORAGE_HEIGHT 768u

/* Software-backend compatibility aliases. The GS backend uses the active
 * dimensions stored in TitanVideoState and never treats these as 720p. */
#define TITAN_VIDEO_WIDTH TITAN_VIDEO_LEGACY_WIDTH
#define TITAN_VIDEO_HEIGHT TITAN_VIDEO_LEGACY_HEIGHT
#define TITAN_VIDEO_LEGACY_LOGICAL_HEIGHT TITAN_VIDEO_LEGACY_HEIGHT

#ifdef PROJECT_TITAN_WIDESCREEN
#define TITAN_UI_WIDTH 854
#else
#define TITAN_UI_WIDTH TITAN_VIDEO_WIDTH
#endif

#define TITAN_UI_HEIGHT TITAN_VIDEO_LEGACY_LOGICAL_HEIGHT

#define TITAN_VIDEO_CACHED_LAYER_WIDTH 512u
#define TITAN_VIDEO_CACHED_LAYER_HEIGHT 256u
#define TITAN_VIDEO_MEDIA_PRIMARY_WIDTH 640u
#define TITAN_VIDEO_MEDIA_PRIMARY_HEIGHT 480u
#define TITAN_VIDEO_MEDIA_LEGACY_WIDTH 512u
#define TITAN_VIDEO_MEDIA_LEGACY_HEIGHT 256u
/* The EE staging surface intentionally stays at the legacy cached-layer size.
 * 640x480 media is streamed through it in tiles, so primary backgrounds do not
 * reserve another full-frame buffer or any additional GS VRAM. */
#define TITAN_VIDEO_MEDIA_SURFACE_WIDTH TITAN_VIDEO_MEDIA_LEGACY_WIDTH
#define TITAN_VIDEO_MEDIA_SURFACE_HEIGHT TITAN_VIDEO_MEDIA_LEGACY_HEIGHT
#define TITAN_VIDEO_AVATAR_ATLAS_WIDTH 512u
#define TITAN_VIDEO_AVATAR_ATLAS_HEIGHT 512u
#define TITAN_VIDEO_FONT_ATLAS_WIDTH 512u
#define TITAN_VIDEO_FONT_ATLAS_HEIGHT 256u
#define TITAN_VIDEO_UI_ATLAS_WIDTH 512u
#define TITAN_VIDEO_UI_ATLAS_HEIGHT 256u

TitanResult TitanVideo_Initialize(void);
TitanResult TitanVideo_SetProgressiveOutput(int enabled);
int TitanVideo_IsProgressiveOutput(void);

/* 720p composition policy. OPTIMIZED 960x540 is the approved/default shell
 * path and scales once to the physical 1280x720p output. REFERENCE remains
 * available only for DEX A/B diagnostics/fallback. */
TitanResult TitanVideo_Set720pPath(TitanVideo720pPath path);
/* DEX/target-validation only: safely rebuilds active 720p surfaces for A/B. */
TitanResult TitanVideo_Set720pValidationPath(TitanVideo720pPath path);
TitanVideo720pPath TitanVideo_Get720pPath(void);
unsigned int TitanVideo_GetInternalWidth(void);
unsigned int TitanVideo_GetInternalHeight(void);
unsigned int TitanVideo_GetOutputWidth(void);
unsigned int TitanVideo_GetOutputHeight(void);

/* Titan Game Runtime display ownership. v1 uses the proven 640x448 PSM32
 * legacy scanout with a dedicated Z16 surface, intentionally omitting all
 * shell atlases from GS VRAM. This leaves a bounded texture arena while
 * preserving the existing video timing path. */
TitanResult TitanVideo_BeginGameMode(unsigned int width, unsigned int height);
TitanResult TitanVideo_EndGameMode(void);
int TitanVideo_IsGameMode(void);
unsigned int TitanVideo_GetGameZBufferAddress(void);
unsigned int TitanVideo_GetGameTextureBytesFree(void);

/* Titan Game Runtime 3D backend. Game textures share a 1 MiB block arena with
 * generation-checked handles and end-of-frame retirement. Small textures no
 * longer consume a full 128x128 slot, while queued GIF packets can never see
 * storage recycled underneath them. UV coordinates are normalized. */
void TitanVideo_DrawGameTrianglePhysical(
    float x0, float y0, unsigned int z0,
    float x1, float y1, unsigned int z1,
    float x2, float y2, unsigned int z2,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
);
int TitanVideo_DrawGameTexturedTrianglePhysical(
    float x0, float y0, unsigned int z0, float u0, float v0,
    float x1, float y1, unsigned int z1, float u1, float v1,
    float x2, float y2, unsigned int z2, float u2, float v2,
    unsigned char r0, unsigned char g0, unsigned char b0, unsigned char a0,
    unsigned char r1, unsigned char g1, unsigned char b1, unsigned char a1,
    unsigned char r2, unsigned char g2, unsigned char b2, unsigned char a2,
    uint32_t texture_handle
);
int TitanVideo_GameSetRenderState(const TitanGameRenderState *state);
int TitanVideo_DrawGameMaterialTrianglePhysical(
    float x0, float y0, unsigned int z0,
    float s00, float t00, float q00, float s10, float t10, float q10,
    float x1, float y1, unsigned int z1,
    float s01, float t01, float q01, float s11, float t11, float q11,
    float x2, float y2, unsigned int z2,
    float s02, float t02, float q02, float s12, float t12, float q12,
    unsigned char r0, unsigned char g0, unsigned char b0, unsigned char a0,
    unsigned char r1, unsigned char g1, unsigned char b1, unsigned char a1,
    unsigned char r2, unsigned char g2, unsigned char b2, unsigned char a2,
    unsigned char fog0, unsigned char fog1, unsigned char fog2,
    unsigned char lod0, unsigned char lod1, unsigned char lod2,
    uint32_t texture0_handle, uint32_t texture1_handle
);
TitanResult TitanVideo_GameUploadTexturePsm16(
    const unsigned short *pixels, unsigned int width, unsigned int height,
    unsigned int stride, uint32_t *texture_handle
);
TitanResult TitanVideo_GameUploadTextureRgba32(
    const unsigned char *rgba, unsigned int width, unsigned int height,
    unsigned int stride_bytes, uint32_t *texture_handle
);
TitanResult TitanVideo_GameUploadTextureI8(
    const unsigned char *pixels, unsigned int width, unsigned int height,
    unsigned int stride, uint32_t *texture_handle
);
int TitanVideo_GameReleaseTexture(uint32_t texture_handle);
void TitanVideo_GameResetTextures(void);

/* pikaVR VLX2 remains optional and reversible. The reference path keeps the
 * exact legacy upload/render behavior when optimization is disabled. */
void TitanVideo_SetPikaVrOptimizedPath(int enabled);
void TitanVideo_GetPikaVrTelemetry(TitanPikaVrTelemetry *telemetry);

unsigned int TitanVideo_GetLastFrameGsLoadPercent(void);
unsigned int TitanVideo_GetVramBytesReserved(void);
unsigned int TitanVideo_GetVramBytesFree(void);
unsigned int TitanVideo_GetFramebufferWidth(void);
unsigned int TitanVideo_GetFramebufferHeight(void);
unsigned int TitanVideo_GetFramebufferCount(void);
int TitanVideo_GetFramebufferPsm(void);
TitanResult TitanVideo_BeginFrame(void);

/* Flushes pending PATH3 commands so a PATH1 producer can run in-order. */
TitanResult TitanVideo_FlushForPath1(void);

/* Re-emits the canonical PATH3 draw state after a PATH1 producer changed GS
 * registers such as ALPHA, CLAMP, TEX0 or TEX1. */
TitanResult TitanVideo_RestoreAfterPath1(void);

/* Includes PATH1 packets in the DEX GS packet-budget estimate. */
void TitanVideo_AccountExternalQwords(unsigned int qwords);

int TitanVideo_MapLogicalX(
    float logical_x
);

int TitanVideo_MapLogicalY(
    float logical_y
);

float TitanVideo_GetTransformScale(void);

/* Active logical-to-framebuffer mapping. Legacy mode renders 640x448 PSM32
 * with page flipping. HDTV mode rebuilds VRAM as two native 1280x720 PSM16
 * scanout surfaces plus losslessly indexed resident alpha atlases. */
float TitanVideo_GetLogicalScaleX(void);
float TitanVideo_GetLogicalScaleY(void);
float TitanVideo_GetLogicalOffsetY(void);

void TitanVideo_SetTransform(
    float scale,
    float origin_x,
    float origin_y,
    float offset_x,
    float offset_y
);

void TitanVideo_ResetTransform(void);

void TitanVideo_DrawPixelPhysical(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

void TitanVideo_DrawPixelPhysicalAlpha(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_Clear(
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

void TitanVideo_BuildBackgroundGradient(
    unsigned char top_red,
    unsigned char top_green,
    unsigned char top_blue,
    unsigned char bottom_red,
    unsigned char bottom_green,
    unsigned char bottom_blue
);

void TitanVideo_RestoreBackground(void);

void TitanVideo_DrawPixelAlpha(
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawRect(
    float x,
    float y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

void TitanVideo_DrawRectAlpha(
    float x,
    float y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawCircleAlpha(
    float center_x,
    float center_y,
    float radius,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawRoundedRectAlpha(
    float x,
    float y,
    float width,
    float height,
    float radius,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawLineAlpha(
    float x0,
    float y0,
    float x1,
    float y1,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawLine(
    float x0,
    float y0,
    float x1,
    float y1,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue
);

void TitanVideo_DrawSolidQuad(
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
    unsigned char blue
);

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
);

TitanResult TitanVideo_UploadCachedLayer16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
);

void TitanVideo_DrawCachedLayer16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
);

/* Draws a kernel-owned RGB565 media frame without reserving additional GS
 * VRAM. 640x480 is the primary live-area contract; 512x256 remains a legacy
 * fallback. Legacy output streams primary media through the existing 512x256
 * cached-layer allocation, while native 720p uses the existing avatar scratch
 * texture in ordered 256x64 tiles. The proven 4 MiB VRAM contract is unchanged. */
TitanResult TitanVideo_DrawMediaImage16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    float x,
    float y,
    float draw_width,
    float draw_height,
    unsigned char alpha
);

/* Asset-service scratch path. No resident 720p VRAM is added: the transient
 * atlas aliases the existing avatar scratch in 720p and the cached-layer
 * allocation in legacy output. generation avoids repeat DMA while consecutive
 * game icons share the same visible-window strip. */
TitanResult TitanVideo_UploadTransientAtlasPsm16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    unsigned int generation
);

void TitanVideo_DrawTransientAtlasRegionPsm16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
);

/* Draws a small RGB565 image (up to 256x128) without allocating new VRAM.
 * Native 720p streams 256x64 bands; legacy output aliases cached-layer VRAM. */
TitanResult TitanVideo_DrawAdaptiveImagePsm16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    float x,
    float y,
    float draw_width,
    float draw_height,
    unsigned char alpha
);

TitanResult TitanVideo_UploadAvatarAtlas16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
);

void TitanVideo_DrawAvatarAtlasRegion16(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char alpha
);

TitanResult TitanVideo_UploadFontAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
);

void TitanVideo_DrawFontAtlasRegion32(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

TitanResult TitanVideo_UploadUiAtlas32(
    const unsigned int *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride
);

void TitanVideo_DrawUiAtlasRegion32(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

void TitanVideo_DrawUiAtlasRegion32Point(
    float x,
    float y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

TitanResult TitanVideo_EndFrame(void);

#endif
