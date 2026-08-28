#ifndef PROJECT_TITAN_IOP_SERVICE_MANAGER_H
#define PROJECT_TITAN_IOP_SERVICE_MANAGER_H

#include "titan_result.h"

typedef enum TitanIopBootstrapStage
{
    TITAN_IOP_BOOTSTRAP_IDLE = 0,
    TITAN_IOP_BOOTSTRAP_RESETTING,
    TITAN_IOP_BOOTSTRAP_SYNCING,
    TITAN_IOP_BOOTSTRAP_INITIALIZING_LOADFILE,
    TITAN_IOP_BOOTSTRAP_READY,
    TITAN_IOP_BOOTSTRAP_FAILED
} TitanIopBootstrapStage;

typedef void (*TitanIopProgressCallback)(
    TitanIopBootstrapStage stage,
    int detail,
    void *user_data
);

typedef struct TitanIopServiceState
{
    int initialized;
    int rpc_initialized;
    int loadfile_initialized;
    TitanResult result;
    TitanIopBootstrapStage stage;
    int detail;
    unsigned int generation;
} TitanIopServiceState;

TitanResult TitanIopServiceManager_Initialize(
    TitanIopProgressCallback callback,
    void *user_data
);

void TitanIopServiceManager_Shutdown(void);

const TitanIopServiceState *TitanIopServiceManager_GetState(void);

int TitanIopServiceManager_IsReady(void);

const char *TitanIopServiceManager_GetStageName(
    TitanIopBootstrapStage stage
);

#endif
