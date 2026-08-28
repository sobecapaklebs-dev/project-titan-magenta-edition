#ifndef PROJECT_TITAN_DISC_SERVICE_H
#define PROJECT_TITAN_DISC_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#define TITAN_DISC_SERVICE_API_VERSION 1u

#define TITAN_DISC_TITLE_ID_CAPACITY 24u
#define TITAN_DISC_TITLE_CAPACITY 96u
#define TITAN_DISC_BOOT_PATH_CAPACITY 128u
#define TITAN_DISC_VERSION_CAPACITY 24u
#define TITAN_DISC_VMODE_CAPACITY 16u
#define TITAN_DISC_SYSTEM_CNF_CAPACITY (16u * 1024u)
#define TITAN_DISC_ASSET_PATH_CAPACITY 192u
#define TITAN_DISC_METADATA_CAPACITY 1024u

typedef enum TitanDiscState {
    TITAN_DISC_NO_MEDIA = 0,
    TITAN_DISC_INSERT_DETECTED,
    TITAN_DISC_PROBING,
    TITAN_DISC_MEDIA_PRESENT,
    TITAN_DISC_IDENTIFYING,
    TITAN_DISC_READY,
    TITAN_DISC_UNSUPPORTED,
    TITAN_DISC_ERROR,
    TITAN_DISC_LAUNCHING,
    TITAN_DISC_RUNNING,
    TITAN_DISC_EJECTED
} TitanDiscState;

typedef enum TitanDiscMediaType {
    TITAN_DISC_MEDIA_NONE = 0,
    TITAN_DISC_MEDIA_PS2_CD,
    TITAN_DISC_MEDIA_PS2_DVD,
    TITAN_DISC_MEDIA_UNSUPPORTED
} TitanDiscMediaType;

typedef enum TitanDiscRegion {
    TITAN_DISC_REGION_UNKNOWN = 0,
    TITAN_DISC_REGION_NTSC_U,
    TITAN_DISC_REGION_PAL,
    TITAN_DISC_REGION_NTSC_J
} TitanDiscRegion;

typedef enum TitanDiscError {
    TITAN_DISC_ERROR_NONE = 0,
    TITAN_DISC_ERROR_NO_MEDIA,
    TITAN_DISC_ERROR_UNSUPPORTED_MEDIA,
    TITAN_DISC_ERROR_SYSTEM_CNF_NOT_FOUND,
    TITAN_DISC_ERROR_SYSTEM_CNF_INVALID,
    TITAN_DISC_ERROR_BOOT_PATH_INVALID,
    TITAN_DISC_ERROR_DISC_READ,
    TITAN_DISC_ERROR_LAUNCH_FAILED,
    TITAN_DISC_ERROR_SESSION_FAILED,
    TITAN_DISC_ERROR_IGR_UNAVAILABLE
} TitanDiscError;

typedef enum TitanDiscMetadataState {
    TITAN_DISC_METADATA_NONE = 0,
    TITAN_DISC_METADATA_FALLBACK,
    TITAN_DISC_METADATA_LOCAL_CACHE
} TitanDiscMetadataState;

typedef struct TitanDiscInfo {
    TitanDiscState state;
    TitanDiscMediaType media_type;
    TitanDiscRegion region;
    TitanDiscError last_error;
    TitanDiscMetadataState metadata_state;
    int media_present;
    int system_cnf_valid;
    char title_id[TITAN_DISC_TITLE_ID_CAPACITY];
    char title[TITAN_DISC_TITLE_CAPACITY];
    char boot_path[TITAN_DISC_BOOT_PATH_CAPACITY];
    char version[TITAN_DISC_VERSION_CAPACITY];
    char vmode[TITAN_DISC_VMODE_CAPACITY];
    char icon_path[TITAN_DISC_ASSET_PATH_CAPACITY];
    char cover_path[TITAN_DISC_ASSET_PATH_CAPACITY];
    char background_path[TITAN_DISC_ASSET_PATH_CAPACITY];
    uint32_t probe_count;
    uint32_t media_generation;
    uint32_t read_time_ms;
    uint32_t compatibility_profile_hint;
} TitanDiscInfo;

typedef struct TitanDiscPlatformOps {
    /* return 1=present/typed, 0=no media, 2=detecting/not-ready, -1=read error */
    int (*probe_media)(void *context, TitanDiscMediaType *media_type);
    /* return bytes read, 0=not found, -1=read error */
    int (*read_system_cnf)(void *context, char *buffer, size_t capacity);
    /* Optional local metadata cache lookup. Runs only on the serialized storage worker. */
    int (*read_metadata)(void *context, const char *title_id, char *buffer, size_t capacity);
} TitanDiscPlatformOps;

void TitanDiscService_Initialize(void);
void TitanDiscService_Shutdown(void);
void TitanDiscService_UpdateMainThread(uint64_t now_ms);
int TitanDiscService_HasStorageWork(void);
int TitanDiscService_StorageWorkerPump(void);
const TitanDiscInfo *TitanDiscService_GetInfo(void);
int TitanDiscService_IsReady(void);
void TitanDiscService_MarkLaunching(void);
void TitanDiscService_MarkRunning(void);
void TitanDiscService_MarkLaunchFailed(void);
void TitanDiscService_SetPlatformOps(const TitanDiscPlatformOps *ops, void *context);

int TitanDisc_ParseSystemCnf(const char *data, size_t length, TitanDiscInfo *info);
int TitanDisc_NormalizeBootPath(const char *input, char *output, size_t capacity);
int TitanDisc_ExtractTitleId(const char *boot_path, char *output, size_t capacity);
TitanDiscRegion TitanDisc_InferRegion(const char *title_id);
int TitanDisc_ParseMetadata(const char *data, size_t length, TitanDiscInfo *info);
const char *TitanDiscState_Name(TitanDiscState state);
const char *TitanDiscError_Name(TitanDiscError error);

#endif
