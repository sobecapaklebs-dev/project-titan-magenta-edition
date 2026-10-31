#ifndef PROJECT_TITAN_ASSET_SERVICE_H
#define PROJECT_TITAN_ASSET_SERVICE_H

#include <stddef.h>

#define TITAN_ASSET_VISIBLE_GAME_CAPACITY 16u
#define TITAN_ASSET_ICON_SOURCE_WIDTH 128u
#define TITAN_ASSET_ICON_SOURCE_HEIGHT 128u
/* Resident icon slots keep the full 128x128 TIMG. The fast visible atlas is
 * still derived at 32x32 so native 720p does not consume additional GS VRAM. */
#define TITAN_ASSET_ICON_RUNTIME_SIZE 128u
#define TITAN_ASSET_ICON_THUMBNAIL_SIZE 32u
#define TITAN_ASSET_BACKGROUND_WIDTH 256u
#define TITAN_ASSET_BACKGROUND_HEIGHT 128u
#define TITAN_ASSET_TIMG_HEADER_SIZE 32u

typedef enum TitanAssetKind
{
    TITAN_ASSET_KIND_ICON = 1,
    TITAN_ASSET_KIND_BACKGROUND = 2,
    /* Internal decode target for Titan-owned native-app media surfaces. */
    TITAN_ASSET_KIND_APP_MEDIA = 3
} TitanAssetKind;

typedef enum TitanAssetManualPhase
{
    TITAN_ASSET_MANUAL_IDLE = 0,
    TITAN_ASSET_MANUAL_PROBE_ICONS,
    TITAN_ASSET_MANUAL_DOWNLOAD_ICONS,
    TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS,
    TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS,
    TITAN_ASSET_MANUAL_CLEAR_CACHE,
    TITAN_ASSET_MANUAL_RECOVERING,
    TITAN_ASSET_MANUAL_COMPLETE,
    TITAN_ASSET_MANUAL_CANCELLED,
    TITAN_ASSET_MANUAL_IMPORT_ARTWORK
} TitanAssetManualPhase;

typedef struct TitanAssetServiceStatus
{
    int initialized;
    int direct_internet;
    int network_busy;
    int storage_busy;
    int automatic_download_enabled;
    int manual_session_active;
    int manual_refresh_existing;
    int manual_clear_cache;
    int artwork_wizard_active;
    int manual_start_queued;
    int recovering_previous_state;
    unsigned int recovery_count;
    TitanAssetManualPhase manual_phase;
    unsigned int session_games;
    unsigned int session_total_assets;
    unsigned int session_completed_assets;
    unsigned int session_failed_assets;
    unsigned int session_unavailable_assets;
    unsigned int visible_game_count;
    unsigned int selected_game_index;
    unsigned int current_game_index;
    unsigned int current_game_ordinal;
    TitanAssetKind current_kind;
    int automatic_paused_for_activity;
    unsigned int icons_ready;
    unsigned int downloads_completed;
    unsigned int cache_hits;
    unsigned int cache_misses;
    unsigned int transfer_bytes_received;
    unsigned int transfer_bytes_total;
    unsigned int transfer_bytes_per_second;
    unsigned int transfer_progress_percent;
    char provider[96];
    char detail[96];
} TitanAssetServiceStatus;

void TitanAssetService_Initialize(void);
void TitanAssetService_UpdateMainThread(unsigned int frame_index);
void TitanAssetService_NotifyCatalogChanged(void);

/* Feed only games that are actually visible. The renderer never performs
 * network or filesystem I/O; it sees ready RAM slots or the normal placeholder. */
void TitanAssetService_SetVisibleGames(
    unsigned int selected_game_index,
    const unsigned int *game_indices,
    unsigned int count,
    unsigned int frame_index
);

int TitanAssetService_DrawIcon(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);


/* High-quality selected-tile path. It streams the resident 128x128 PSM16 icon
 * through the existing scratch texture, so no new permanent GS VRAM is used. */
int TitanAssetService_DrawIconHighQuality(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);

int TitanAssetService_DrawBackground(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
);

/* Called only by TitanStorageScheduler's serialized FileXio worker. Exactly
 * one bounded cache quantum is consumed per call. */
int TitanAssetService_HasStorageWork(void);
int TitanAssetService_StorageWorkerPump(void);

/* Network becoming ONLINE never starts Internet asset traffic by itself.
 * Downloads are manual by default and can be explicitly enabled as an
 * optional automatic mode from Settings > Network > Dados de jogos. */
int TitanAssetService_StartManualDownload(int refresh_existing);
int TitanAssetService_StartClearCache(void);
/* Converts local mass:/ART sources for one game or the whole catalog to the
 * existing persistent TIMG cache. kind is ICON, BACKGROUND, or 0 for both. */
int TitanAssetService_StartArtworkWizard(int all_games, unsigned int selected_game,
    TitanAssetKind kind);
void TitanAssetService_CancelManualOperation(void);
int TitanAssetService_IsManualOperationActive(void);
int TitanAssetService_IsAutomaticDownloadEnabled(void);
void TitanAssetService_SetAutomaticDownloadEnabled(int enabled);
/* Automatic mode is intentionally stricter than manual mode: it runs only
 * while the shell explicitly reports an idle Home surface. Any user input
 * pauses/cancels the current automatic fetch and restarts the idle window. */
void TitanAssetService_SetAutomaticIdleEligible(int eligible);
void TitanAssetService_NotifyUserActivity(void);
const char *TitanAssetService_GetManualPhaseName(TitanAssetManualPhase phase);

const TitanAssetServiceStatus *TitanAssetService_GetStatus(void);

#endif
