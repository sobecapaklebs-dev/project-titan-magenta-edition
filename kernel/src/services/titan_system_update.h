#ifndef PROJECT_TITAN_SYSTEM_UPDATE_H
#define PROJECT_TITAN_SYSTEM_UPDATE_H

#define TITAN_SYSTEM_UPDATE_CURRENT_BUILD 36u
#define TITAN_SYSTEM_UPDATE_CAPACITY 8u

typedef enum TitanSystemUpdateResult
{
    TITAN_SYSTEM_UPDATE_OK = 0,
    TITAN_SYSTEM_UPDATE_NO_PACKAGE,
    TITAN_SYSTEM_UPDATE_INVALID_PACKAGE,
    TITAN_SYSTEM_UPDATE_OLD_VERSION,
    TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
    TITAN_SYSTEM_UPDATE_CHECKSUM_ERROR,
    TITAN_SYSTEM_UPDATE_PUBLISH_ERROR,
    TITAN_SYSTEM_UPDATE_CANCELLED
} TitanSystemUpdateResult;

typedef enum TitanSystemUpdatePhase
{
    TITAN_SYSTEM_UPDATE_IDLE = 0,
    TITAN_SYSTEM_UPDATE_PREPARING,
    TITAN_SYSTEM_UPDATE_VERIFYING,
    TITAN_SYSTEM_UPDATE_EXTRACTING,
    TITAN_SYSTEM_UPDATE_VERIFYING_STAGED,
    TITAN_SYSTEM_UPDATE_PUBLISHING,
    TITAN_SYSTEM_UPDATE_COMPLETE,
    TITAN_SYSTEM_UPDATE_ERROR
} TitanSystemUpdatePhase;

typedef struct TitanSystemUpdateInfo
{
    char path[256];
    char filename[128];
    char version[32];
    unsigned int build_number;
    unsigned int file_count;
    unsigned int package_size;
} TitanSystemUpdateInfo;

typedef struct TitanSystemUpdateStatus
{
    TitanSystemUpdatePhase phase;
    TitanSystemUpdateResult result;
    unsigned int active;
    unsigned int progress_percent;
    unsigned int file_index;
    unsigned int file_count;
    char message[112];
} TitanSystemUpdateStatus;

void TitanSystemUpdate_Initialize(void);
unsigned int TitanSystemUpdate_Refresh(void);
unsigned int TitanSystemUpdate_GetCount(void);
const TitanSystemUpdateInfo *TitanSystemUpdate_Get(unsigned int index);
TitanSystemUpdateResult TitanSystemUpdate_Begin(unsigned int index);
void TitanSystemUpdate_Update(unsigned int byte_budget);
void TitanSystemUpdate_Cancel(void);
int TitanSystemUpdate_IsActive(void);
const TitanSystemUpdateStatus *TitanSystemUpdate_GetStatus(void);
const char *TitanSystemUpdate_GetResultName(TitanSystemUpdateResult result);

#endif
