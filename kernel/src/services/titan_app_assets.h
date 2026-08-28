#ifndef PROJECT_TITAN_APP_ASSETS_H
#define PROJECT_TITAN_APP_ASSETS_H

#include <limits.h>

#define TITAN_APP_ICON_WIDTH 64u
#define TITAN_APP_ICON_HEIGHT 64u
#define TITAN_APP_ICON_RUNTIME_SIZE 32u
#define TITAN_APP_ICON_CAPACITY 57u
#define TITAN_APP_TILE_WIDTH 256u
#define TITAN_APP_TILE_HEIGHT 256u
#define TITAN_APP_BACKGROUND_PRIMARY_WIDTH 640u
#define TITAN_APP_BACKGROUND_PRIMARY_HEIGHT 480u
#define TITAN_APP_BACKGROUND_LEGACY_WIDTH 512u
#define TITAN_APP_BACKGROUND_LEGACY_HEIGHT 256u
#define TITAN_APP_ASSET_NO_SELECTION UINT_MAX

void TitanAppAssets_Initialize(void);
void TitanAppAssets_RequestRebuild(void);
int TitanAppAssets_HasBuildWork(void);
int TitanAppAssets_StepBuild(void);
int TitanAppAssets_CommitPendingAtlas(void);
void TitanAppAssets_EnsureUploaded(void);
int TitanAppAssets_DrawIcon(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);

/* Selected-app live-area assets. Selection requests are issued by the main
 * shell thread, storage is performed only by the low-priority storage worker,
 * and pointer publication happens from CommitPendingAtlas() between frames. */
void TitanAppAssets_Select(unsigned int application_index);
int TitanAppAssets_IsSelectionLoading(void);
int TitanAppAssets_DrawSelectedTile(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);
int TitanAppAssets_DrawSelectedBackground(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);
void TitanAppAssets_ReleaseSelectedBackground(void);

#endif
