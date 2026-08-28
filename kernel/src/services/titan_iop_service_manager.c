#include "titan_iop_service_manager.h"

#include <delaythread.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sifrpc.h>

#include <string.h>

#define TITAN_IOP_RESET_MAX_ATTEMPTS 120
#define TITAN_IOP_SYNC_MAX_ATTEMPTS 500
#define TITAN_IOP_RETRY_DELAY_US 10000

static TitanIopServiceState g_iop;

static void TitanIopServiceManager_Notify(
    TitanIopProgressCallback callback,
    void *user_data,
    TitanIopBootstrapStage stage,
    int detail
)
{
    g_iop.stage = stage;
    g_iop.detail = detail;

    if (callback != 0)
    {
        callback(stage, detail, user_data);
    }
}

static TitanResult TitanIopServiceManager_Fail(
    TitanIopProgressCallback callback,
    void *user_data,
    TitanResult result
)
{
    g_iop.initialized = 0;
    g_iop.result = result;
    TitanIopServiceManager_Notify(
        callback,
        user_data,
        TITAN_IOP_BOOTSTRAP_FAILED,
        (int)result
    );
    return result;
}

TitanResult TitanIopServiceManager_Initialize(
    TitanIopProgressCallback callback,
    void *user_data
)
{
    int attempt;
    int ready;
    unsigned int next_generation;

    if (g_iop.initialized)
    {
        TitanIopServiceManager_Notify(
            callback,
            user_data,
            TITAN_IOP_BOOTSTRAP_READY,
            0
        );
        return TITAN_RESULT_OK;
    }

    next_generation = g_iop.generation + 1u;
    memset(&g_iop, 0, sizeof(g_iop));
    g_iop.generation = next_generation;
    g_iop.result = TITAN_RESULT_OK;

    TitanIopServiceManager_Notify(
        callback,
        user_data,
        TITAN_IOP_BOOTSTRAP_RESETTING,
        0
    );

    SifInitRpc(0);
    g_iop.rpc_initialized = 1;
    ready = 0;

    for (attempt = 1; attempt <= TITAN_IOP_RESET_MAX_ATTEMPTS; ++attempt)
    {
        if (SifIopReset("", 0))
        {
            ready = 1;
            break;
        }

        if (attempt == 1 || (attempt % 20) == 0)
        {
            TitanIopServiceManager_Notify(
                callback,
                user_data,
                TITAN_IOP_BOOTSTRAP_RESETTING,
                attempt
            );
        }

        DelayThread(TITAN_IOP_RETRY_DELAY_US);
    }

    if (!ready)
    {
        return TitanIopServiceManager_Fail(
            callback,
            user_data,
            TITAN_ERROR_IOP_RESET
        );
    }

    TitanIopServiceManager_Notify(
        callback,
        user_data,
        TITAN_IOP_BOOTSTRAP_SYNCING,
        0
    );
    ready = 0;

    for (attempt = 1; attempt <= TITAN_IOP_SYNC_MAX_ATTEMPTS; ++attempt)
    {
        if (SifIopSync())
        {
            ready = 1;
            break;
        }

        if (attempt == 1 || (attempt % 50) == 0)
        {
            TitanIopServiceManager_Notify(
                callback,
                user_data,
                TITAN_IOP_BOOTSTRAP_SYNCING,
                attempt
            );
        }

        DelayThread(TITAN_IOP_RETRY_DELAY_US);
    }

    if (!ready)
    {
        return TitanIopServiceManager_Fail(
            callback,
            user_data,
            TITAN_ERROR_IOP_SYNC
        );
    }

    SifInitRpc(0);
    g_iop.rpc_initialized = 1;

    TitanIopServiceManager_Notify(
        callback,
        user_data,
        TITAN_IOP_BOOTSTRAP_INITIALIZING_LOADFILE,
        0
    );

    if (SifLoadFileInit() < 0)
    {
        return TitanIopServiceManager_Fail(
            callback,
            user_data,
            TITAN_ERROR_LOADFILE_INITIALIZATION
        );
    }

    g_iop.loadfile_initialized = 1;
    g_iop.initialized = 1;
    g_iop.result = TITAN_RESULT_OK;
    TitanIopServiceManager_Notify(
        callback,
        user_data,
        TITAN_IOP_BOOTSTRAP_READY,
        0
    );

    return TITAN_RESULT_OK;
}

void TitanIopServiceManager_Shutdown(void)
{
    /*
     * Resetting the IOP is the only reliable way to unload arbitrary modules.
     * The manager therefore only releases EE-side clients here. A later retry
     * performs a fresh bounded reset/sync sequence and increments generation.
     */
    if (g_iop.loadfile_initialized)
    {
        SifLoadFileExit();
    }

    if (g_iop.rpc_initialized)
    {
        SifExitRpc();
    }

    g_iop.initialized = 0;
    g_iop.rpc_initialized = 0;
    g_iop.loadfile_initialized = 0;
    g_iop.stage = TITAN_IOP_BOOTSTRAP_IDLE;
    g_iop.detail = 0;
}

const TitanIopServiceState *TitanIopServiceManager_GetState(void)
{
    return &g_iop;
}

int TitanIopServiceManager_IsReady(void)
{
    return g_iop.initialized;
}

const char *TitanIopServiceManager_GetStageName(
    TitanIopBootstrapStage stage
)
{
    switch (stage)
    {
        case TITAN_IOP_BOOTSTRAP_RESETTING:
            return "RESET IOP";
        case TITAN_IOP_BOOTSTRAP_SYNCING:
            return "SYNC IOP";
        case TITAN_IOP_BOOTSTRAP_INITIALIZING_LOADFILE:
            return "LOADFILE";
        case TITAN_IOP_BOOTSTRAP_READY:
            return "READY";
        case TITAN_IOP_BOOTSTRAP_FAILED:
            return "FAILED";
        case TITAN_IOP_BOOTSTRAP_IDLE:
        default:
            return "IDLE";
    }
}
