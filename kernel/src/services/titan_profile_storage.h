#ifndef PROJECT_TITAN_PROFILE_STORAGE_H
#define PROJECT_TITAN_PROFILE_STORAGE_H

#include "titan_vmc.h"
#include "titan_profile_snapshot.h"

typedef enum TitanProfileStorageStatus
{
    TITAN_PROFILE_STORAGE_NOT_INITIALIZED = 0,
    TITAN_PROFILE_STORAGE_READY_LOADED,
    TITAN_PROFILE_STORAGE_READY_EMPTY,
    TITAN_PROFILE_STORAGE_NO_STORAGE,
    TITAN_PROFILE_STORAGE_CORRUPT,
    TITAN_PROFILE_STORAGE_FUTURE_VERSION,
    TITAN_PROFILE_STORAGE_IO_ERROR
} TitanProfileStorageStatus;

typedef enum TitanProfileStorageSaveResult
{
    TITAN_PROFILE_STORAGE_SAVE_OK = 0,
    TITAN_PROFILE_STORAGE_SAVE_NOT_NEEDED,
    TITAN_PROFILE_STORAGE_SAVE_NO_STORAGE,
    TITAN_PROFILE_STORAGE_SAVE_PROTECTED,
    TITAN_PROFILE_STORAGE_SAVE_ERROR
} TitanProfileStorageSaveResult;

typedef enum TitanProfileStorageRecoveryResult
{
    TITAN_PROFILE_STORAGE_RECOVERY_OK = 0,
    TITAN_PROFILE_STORAGE_RECOVERY_NOT_NEEDED,
    TITAN_PROFILE_STORAGE_RECOVERY_NO_STORAGE,
    TITAN_PROFILE_STORAGE_RECOVERY_NO_VALID_COPY,
    TITAN_PROFILE_STORAGE_RECOVERY_FUTURE_VERSION,
    TITAN_PROFILE_STORAGE_RECOVERY_ERROR
} TitanProfileStorageRecoveryResult;

typedef enum TitanProfileStorageFormatResult
{
    TITAN_PROFILE_STORAGE_FORMAT_OK = 0,
    TITAN_PROFILE_STORAGE_FORMAT_NO_CARD,
    TITAN_PROFILE_STORAGE_FORMAT_UNSUPPORTED,
    TITAN_PROFILE_STORAGE_FORMAT_ERROR
} TitanProfileStorageFormatResult;

typedef struct TitanProfileStoragePreparedSave
{
    unsigned char snapshot[TITAN_PROFILE_SNAPSHOT_SIZE];
    unsigned int generation;
    unsigned int revision;
    int valid;
} TitanProfileStoragePreparedSave;

typedef enum TitanProfileStorageFailureStage
{
    TITAN_PROFILE_STORAGE_FAILURE_NONE = 0,
    TITAN_PROFILE_STORAGE_FAILURE_ARGUMENT,
    TITAN_PROFILE_STORAGE_FAILURE_USB_MODULE,
    TITAN_PROFILE_STORAGE_FAILURE_STORAGE_MOUNT,
    TITAN_PROFILE_STORAGE_FAILURE_DIRECTORY_CREATE,
    TITAN_PROFILE_STORAGE_FAILURE_WRITE_OPEN,
    TITAN_PROFILE_STORAGE_FAILURE_WRITE_DATA,
    TITAN_PROFILE_STORAGE_FAILURE_WRITE_CLOSE,
    TITAN_PROFILE_STORAGE_FAILURE_READ_OPEN,
    TITAN_PROFILE_STORAGE_FAILURE_READ_DATA,
    TITAN_PROFILE_STORAGE_FAILURE_READ_CLOSE,
    TITAN_PROFILE_STORAGE_FAILURE_VERIFY_CONTENT,
    TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_ENCODE,
    TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_VERIFY,
    TITAN_PROFILE_STORAGE_FAILURE_FORMAT,
    TITAN_PROFILE_STORAGE_FAILURE_FORMAT_VERIFY
} TitanProfileStorageFailureStage;

TitanProfileStorageStatus TitanProfileStorage_Initialize(
    int use_extended_modules
);

TitanProfileStorageStatus TitanProfileStorage_GetStatus(void);
TitanProfileStorageStatus TitanProfileStorage_Reload(void);

TitanProfileStorageSaveResult TitanProfileStorage_Save(void);
TitanProfileStorageSaveResult TitanProfileStorage_PrepareSave(
    TitanProfileStoragePreparedSave *prepared
);
TitanProfileStorageSaveResult TitanProfileStorage_CommitPreparedSave(
    const TitanProfileStoragePreparedSave *prepared
);
TitanProfileStorageSaveResult TitanProfileStorage_FormatAndSave(void);

TitanProfileStorageRecoveryResult TitanProfileStorage_RecoverPort(
    int port
);

TitanProfileStorageFormatResult TitanProfileStorage_FormatPort(
    int port
);

int TitanProfileStorage_GetPort(void);
int TitanProfileStorage_HasLoadedProfiles(void);
int TitanProfileStorage_IsDirty(void);

unsigned int TitanProfileStorage_GetGeneration(void);
TitanVmcResult TitanProfileStorage_GetLastVmcResult(void);

TitanProfileStorageFailureStage
TitanProfileStorage_GetLastFailureStage(void);

const char *TitanProfileStorage_GetLastFailureStageName(void);

int TitanProfileStorage_GetLastFailureCommand(void);
int TitanProfileStorage_GetLastFailureResult(void);

#endif
