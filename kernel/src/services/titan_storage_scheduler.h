#ifndef PROJECT_TITAN_STORAGE_SCHEDULER_H
#define PROJECT_TITAN_STORAGE_SCHEDULER_H

#include "titan_profile_storage.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

typedef enum TitanStorageForegroundJobType
{
    TITAN_STORAGE_FOREGROUND_NONE = 0,
    TITAN_STORAGE_FOREGROUND_PROFILE_SAVE,
    TITAN_STORAGE_FOREGROUND_VMC_VERIFY,
    TITAN_STORAGE_FOREGROUND_VMC_FORMAT,
    TITAN_STORAGE_FOREGROUND_VMC_RECREATE,
    TITAN_STORAGE_FOREGROUND_APP_UNINSTALL
} TitanStorageForegroundJobType;

typedef struct TitanStorageCompletion
{
    TitanStorageForegroundJobType type;
    int result;
    unsigned int profile_identifier;
    int allow_format_prompt;
    int show_success;
    int show_failure;
    char application_title_id[16];
} TitanStorageCompletion;

int TitanStorageScheduler_Initialize(void);
void TitanStorageScheduler_UpdateMainThread(void);

int TitanStorageScheduler_RequestLibraryScan(int force_rebuild);
int TitanStorageScheduler_IsLibraryScanBusy(void);

/* Package Installer can reserve the storage lane across its complete UI
 * session. While enabled, an active package job is the only filesystem work
 * serviced; after completion/cancel the lane remains parked until the caller
 * leaves the installer and releases exclusive mode. */
void TitanStorageScheduler_SetPackageExclusiveMode(int enabled);
int TitanStorageScheduler_IsPackageWorkerParked(void);

/* Titan-native applications run inside the shell process.  During their
 * session the main EE thread owns FileXio and the background worker is
 * parked, preventing scanner/audio-prefetch/VMC jobs from racing app I/O. */
void TitanStorageScheduler_SetTitanAppExclusiveMode(int enabled);
int TitanStorageScheduler_IsTitanAppWorkerParked(void);

/* Native 3D games run synchronously on the main EE thread. Game-exclusive
 * mode is separate from Titan App so a game session cannot accidentally reuse
 * a stale app parked acknowledgement. */
void TitanStorageScheduler_SetTitanGameExclusiveMode(int enabled);
int TitanStorageScheduler_IsTitanGameWorkerParked(void);
int TitanStorageScheduler_RequestVmcRefresh(
    const TitanUserProfile *profile
);

int TitanStorageScheduler_RequestProfileSave(
    int allow_format_prompt,
    int show_success,
    int show_failure
);

int TitanStorageScheduler_RequestVmcVerify(
    const TitanUserProfile *profile
);
int TitanStorageScheduler_RequestVmcFormat(
    const TitanUserProfile *profile
);
int TitanStorageScheduler_RequestVmcRecreate(
    const TitanUserProfile *profile
);

int TitanStorageScheduler_RequestApplicationUninstall(
    const char *root_path,
    const char *title_id
);
int TitanStorageScheduler_RequestAppAssetsRebuild(void);

/* Wake the shared storage worker for newly queued bounded background work. */
void TitanStorageScheduler_NotifyBackgroundWork(void);

int TitanStorageScheduler_IsForegroundBusy(void);
int TitanStorageScheduler_PollCompletion(
    TitanStorageCompletion *completion
);

#endif
