#include "titan_storage_scheduler.h"
#include "titan_download_service.h"
#include "titan_disc_service.h"

#include "titan_library_scan.h"
#include "titan_localization.h"
#include "titan_app_assets.h"
#include "titan_asset_service.h"
#include "titan_diagnostics.h"
#include "titan_health_trace.h"
#include "titan_controller_assignment.h"
#include "titan_storage_io.h"
#include "titan_storage_inventory.h"
#include "titan_notifications.h"
#include "titan_shell_busy.h"
#include "titan_audio.h"
#include "titan_hid_input.h"
#include "titan_package_installer.h"
#include "titan_system_update.h"
#include "titan_vlombiscore.h"
#include "titan_shell_background.h"

#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Avoid pulling PADEMU shared ABI headers into storage-only host contracts. */
extern int TitanPademuInputBridge_HasStorageWork(void);
extern int TitanPademuInputBridge_StorageWorkerPump(void);

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
extern void *_gp;
#endif

#define TITAN_STORAGE_WORKER_STACK_SIZE (32u * 1024u)
#define TITAN_STORAGE_WORKER_PRIORITY 96
#define TITAN_STORAGE_WORKER_IDLE_DELAY_US 6000u
#define TITAN_STORAGE_WORKER_ACTIVE_DELAY_US 1000u
#define TITAN_STORAGE_SCAN_DELAY_US 250u
#define TITAN_STORAGE_PACKAGE_BUDGET (4u * 1024u)
#define TITAN_STORAGE_PACKAGE_MIN_BUDGET (1u * 1024u)
#define TITAN_STORAGE_PACKAGE_MAX_BUDGET (4u * 1024u)

typedef struct TitanStorageSchedulerState
{
    int initialized;
    int threaded;
#if defined(__EE__) || defined(_EE)
    int worker_thread_id;
    int wake_sema_id;
    int lock_sema_id;
    unsigned char worker_stack[TITAN_STORAGE_WORKER_STACK_SIZE]
        __attribute__((aligned(64)));
#endif

    int scan_requested;
    int scan_force_rebuild;
    int scan_running;
    int scan_finished;
    int asset_scan_turn;
    int inventory_bulk_turn;
    int package_exclusive_mode;
    int package_worker_parked;
    int app_exclusive_mode;
    int app_worker_parked;
    int game_exclusive_mode;
    int game_worker_parked;
    int vmc_refresh_requested;
    TitanUserProfile vmc_refresh_profile;
    unsigned int scan_last_progress;
    char scan_last_message[112];

    TitanStorageForegroundJobType foreground_pending_type;
    TitanStorageForegroundJobType foreground_running_type;
    TitanProfileStoragePreparedSave prepared_save;
    TitanUserProfile vmc_profile;
    char application_root[256];
    char application_title_id[16];
    int foreground_allow_format_prompt;
    int foreground_show_success;
    int foreground_show_failure;

    int completion_pending;
    TitanStorageCompletion completion;
} TitanStorageSchedulerState;

static TitanStorageSchedulerState g_storage_scheduler;

static void TitanStorageScheduler_Lock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_storage_scheduler.initialized &&
        g_storage_scheduler.lock_sema_id >= 0)
    {
        WaitSema(g_storage_scheduler.lock_sema_id);
    }
#endif
}

static void TitanStorageScheduler_Unlock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_storage_scheduler.initialized &&
        g_storage_scheduler.lock_sema_id >= 0)
    {
        SignalSema(g_storage_scheduler.lock_sema_id);
    }
#endif
}

static void TitanStorageScheduler_WakeWorker(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_storage_scheduler.threaded && g_storage_scheduler.wake_sema_id >= 0)
    {
        (void)SignalSema(g_storage_scheduler.wake_sema_id);
    }
#endif
}

static int TitanStorageScheduler_IsPackageExclusiveMode(void)
{
    int enabled;

    TitanStorageScheduler_Lock();
    enabled = g_storage_scheduler.package_exclusive_mode;
    TitanStorageScheduler_Unlock();
    return enabled;
}

static void TitanStorageScheduler_SetPackageWorkerParked(int parked)
{
    TitanStorageScheduler_Lock();
    g_storage_scheduler.package_worker_parked = parked != 0;
    TitanStorageScheduler_Unlock();
}

int TitanStorageScheduler_IsPackageWorkerParked(void)
{
    int parked;

    /* Host/fallback mode has no background worker to acknowledge the park.
     * In that mode the main thread already owns the FileXio lane. */
    if (!g_storage_scheduler.initialized || !g_storage_scheduler.threaded)
    {
        return 1;
    }

    TitanStorageScheduler_Lock();
    parked = g_storage_scheduler.package_worker_parked;
    TitanStorageScheduler_Unlock();
    return parked;
}

static int TitanStorageScheduler_IsTitanAppExclusiveMode(void)
{
    int enabled;

    TitanStorageScheduler_Lock();
    enabled = g_storage_scheduler.app_exclusive_mode;
    TitanStorageScheduler_Unlock();
    return enabled;
}

static void TitanStorageScheduler_SetTitanAppWorkerParked(int parked)
{
    TitanStorageScheduler_Lock();
    g_storage_scheduler.app_worker_parked = parked != 0;
    TitanStorageScheduler_Unlock();
}

int TitanStorageScheduler_IsTitanAppWorkerParked(void)
{
    int parked;

    if (!g_storage_scheduler.initialized || !g_storage_scheduler.threaded)
    {
        return 1;
    }
    TitanStorageScheduler_Lock();
    parked = g_storage_scheduler.app_worker_parked;
    TitanStorageScheduler_Unlock();
    return parked;
}

static int TitanStorageScheduler_IsTitanGameExclusiveMode(void)
{
    int enabled;
    TitanStorageScheduler_Lock();
    enabled = g_storage_scheduler.game_exclusive_mode;
    TitanStorageScheduler_Unlock();
    return enabled;
}

static void TitanStorageScheduler_SetTitanGameWorkerParked(int parked)
{
    TitanStorageScheduler_Lock();
    g_storage_scheduler.game_worker_parked = parked != 0;
    TitanStorageScheduler_Unlock();
}

int TitanStorageScheduler_IsTitanGameWorkerParked(void)
{
    int parked;
    if (!g_storage_scheduler.initialized || !g_storage_scheduler.threaded)
        return 1;
    TitanStorageScheduler_Lock();
    parked = g_storage_scheduler.game_worker_parked;
    TitanStorageScheduler_Unlock();
    return parked;
}

static int TitanStorageScheduler_IsAnyExclusiveMode(void)
{
    return TitanStorageScheduler_IsPackageExclusiveMode() ||
        TitanStorageScheduler_IsTitanAppExclusiveMode() ||
        TitanStorageScheduler_IsTitanGameExclusiveMode();
}

static void TitanStorageScheduler_CopyText(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;
    if (destination == 0 || capacity == 0u) return;
    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanStorageScheduler_PathJoin(
    char *destination,
    unsigned int capacity,
    const char *root,
    const char *name
)
{
    int length;
    length = snprintf(destination, capacity, "%s/%s", root, name);
    return length >= 0 && (unsigned int)length < capacity;
}

static int TitanStorageScheduler_RemoveTree(const char *path)
{
    DIR *directory;
    struct dirent *entry;

    directory = opendir(path);
    if (directory == 0)
        return errno == ENOENT;
    while ((entry = readdir(directory)) != 0)
    {
        char child[256];
        int is_directory;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!TitanStorageScheduler_PathJoin(child, sizeof(child), path, entry->d_name))
        {
            closedir(directory);
            return 0;
        }
        if (TitanStorageIo_PathInfo64(child, &is_directory, 0) && is_directory)
        {
            if (!TitanStorageScheduler_RemoveTree(child))
            {
                closedir(directory);
                return 0;
            }
        }
        else if (TitanStorageIo_Remove(child) < 0 && errno != ENOENT)
        {
            closedir(directory);
            return 0;
        }
#if defined(__EE__) || defined(_EE)
        DelayThread(4000u);
#endif
    }
    closedir(directory);
    return rmdir(path) == 0 || errno == ENOENT;
}

static int TitanStorageScheduler_TakeForegroundJob(
    TitanStorageForegroundJobType *type,
    TitanProfileStoragePreparedSave *prepared_save,
    TitanUserProfile *vmc_profile,
    char *application_root,
    char *application_title_id,
    int *allow_format_prompt,
    int *show_success,
    int *show_failure
)
{
    int taken;

    taken = 0;
    TitanStorageScheduler_Lock();
    if (g_storage_scheduler.foreground_pending_type !=
        TITAN_STORAGE_FOREGROUND_NONE)
    {
        *type = g_storage_scheduler.foreground_pending_type;
        g_storage_scheduler.foreground_pending_type =
            TITAN_STORAGE_FOREGROUND_NONE;
        g_storage_scheduler.foreground_running_type = *type;
        *prepared_save = g_storage_scheduler.prepared_save;
        *vmc_profile = g_storage_scheduler.vmc_profile;
        TitanStorageScheduler_CopyText(application_root, 256u, g_storage_scheduler.application_root);
        TitanStorageScheduler_CopyText(application_title_id, 16u, g_storage_scheduler.application_title_id);
        *allow_format_prompt =
            g_storage_scheduler.foreground_allow_format_prompt;
        *show_success = g_storage_scheduler.foreground_show_success;
        *show_failure = g_storage_scheduler.foreground_show_failure;
        taken = 1;
    }
    TitanStorageScheduler_Unlock();
    return taken;
}

static void TitanStorageScheduler_FinishForegroundJob(
    TitanStorageForegroundJobType type,
    int result,
    unsigned int profile_identifier,
    const char *application_title_id,
    int allow_format_prompt,
    int show_success,
    int show_failure
)
{
    TitanStorageScheduler_Lock();
    g_storage_scheduler.foreground_running_type =
        TITAN_STORAGE_FOREGROUND_NONE;
    g_storage_scheduler.completion.type = type;
    g_storage_scheduler.completion.result = result;
    g_storage_scheduler.completion.profile_identifier = profile_identifier;
    TitanStorageScheduler_CopyText(
        g_storage_scheduler.completion.application_title_id,
        sizeof(g_storage_scheduler.completion.application_title_id),
        application_title_id
    );
    g_storage_scheduler.completion.allow_format_prompt = allow_format_prompt;
    g_storage_scheduler.completion.show_success = show_success;
    g_storage_scheduler.completion.show_failure = show_failure;
    g_storage_scheduler.completion_pending = 1;
    TitanStorageScheduler_Unlock();
}

static void TitanStorageScheduler_RunForegroundJob(void)
{
    TitanStorageForegroundJobType type;
    TitanProfileStoragePreparedSave prepared_save;
    TitanUserProfile vmc_profile;
    char application_root[256];
    char application_title_id[16];
    int allow_format_prompt;
    int show_success;
    int show_failure;
    int result;
    unsigned int profile_identifier;

    memset(&prepared_save, 0, sizeof(prepared_save));
    memset(&vmc_profile, 0, sizeof(vmc_profile));
    memset(application_root, 0, sizeof(application_root));
    memset(application_title_id, 0, sizeof(application_title_id));
    type = TITAN_STORAGE_FOREGROUND_NONE;
    allow_format_prompt = 0;
    show_success = 0;
    show_failure = 0;

    if (!TitanStorageScheduler_TakeForegroundJob(
            &type,
            &prepared_save,
            &vmc_profile,
            application_root,
            application_title_id,
            &allow_format_prompt,
            &show_success,
            &show_failure))
    {
        return;
    }

    profile_identifier = vmc_profile.identifier;
    result = 0;

    if (type == TITAN_STORAGE_FOREGROUND_PROFILE_SAVE)
    {
        result = (int)TitanProfileStorage_CommitPreparedSave(&prepared_save);
        profile_identifier = 0u;
    }
    else if (type == TITAN_STORAGE_FOREGROUND_VMC_VERIFY)
    {
        TitanVmcResult vmc_result;

        vmc_result = TitanVmc_Ensure(&vmc_profile);
        if (vmc_result == TITAN_VMC_RESULT_OK &&
            TitanVmc_Verify(&vmc_profile) != TITAN_VMC_STATUS_READY)
        {
            vmc_result = TITAN_VMC_RESULT_INVALID;
        }
        result = (int)vmc_result;
    }
    else if (type == TITAN_STORAGE_FOREGROUND_VMC_FORMAT)
    {
        result = (int)TitanVmc_Format(&vmc_profile);
    }
    else if (type == TITAN_STORAGE_FOREGROUND_VMC_RECREATE)
    {
        result = (int)TitanVmc_Recreate(&vmc_profile);
    }
    else if (type == TITAN_STORAGE_FOREGROUND_APP_UNINSTALL)
    {
        result = TitanStorageScheduler_RemoveTree(application_root) ? 1 : 0;
        profile_identifier = 0u;
    }

    TitanStorageScheduler_FinishForegroundJob(
        type,
        result,
        profile_identifier,
        application_title_id,
        allow_format_prompt,
        show_success,
        show_failure
    );
}

static int TitanStorageScheduler_HasForegroundWork(void)
{
    int has_work;

    TitanStorageScheduler_Lock();
    has_work =
        g_storage_scheduler.foreground_pending_type !=
            TITAN_STORAGE_FOREGROUND_NONE;
    TitanStorageScheduler_Unlock();
    return has_work;
}

static int TitanStorageScheduler_RunVmcRefresh(void)
{
    TitanUserProfile profile;
    int requested;

    requested = 0;
    memset(&profile, 0, sizeof(profile));

    TitanStorageScheduler_Lock();
    if (g_storage_scheduler.vmc_refresh_requested)
    {
        profile = g_storage_scheduler.vmc_refresh_profile;
        g_storage_scheduler.vmc_refresh_requested = 0;
        requested = 1;
    }
    TitanStorageScheduler_Unlock();

    if (requested)
    {
        (void)TitanVmc_RefreshStatus(&profile);
    }
    return requested;
}

static int TitanStorageScheduler_StartOrStepScan(void)
{
    int should_start;
    int force_rebuild;

    should_start = 0;
    force_rebuild = 0;

    TitanStorageScheduler_Lock();
    if (!g_storage_scheduler.scan_running &&
        g_storage_scheduler.scan_requested)
    {
        should_start = 1;
        force_rebuild = g_storage_scheduler.scan_force_rebuild;
        g_storage_scheduler.scan_requested = 0;
        g_storage_scheduler.scan_force_rebuild = 0;
        g_storage_scheduler.scan_running = 1;
    }
    TitanStorageScheduler_Unlock();

    if (should_start)
    {
        TitanDiagnostics_Event(
            TITAN_DIAG_INFO,TITAN_DIAG_LIBRARY,0x612,0,
            (uint32_t)force_rebuild,0u,0u,0u
        );
        TitanLibraryScan_SetDeferredUi(1);
        TitanLibraryScan_Begin(force_rebuild);
    }

    TitanStorageScheduler_Lock();
    should_start = g_storage_scheduler.scan_running;
    TitanStorageScheduler_Unlock();
    if (!should_start)
    {
        return 0;
    }

    if (TitanLibraryScan_IsActive())
    {
        TitanVlombisCore_Record(
            TITAN_VLOMBIS_CAP_STORAGE_QOS | TITAN_VLOMBIS_CAP_BACKGROUND_QOS |
                TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN,
            TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION);
        TitanLibraryScan_Update(1u);
    }

    if (!TitanLibraryScan_IsActive())
    {
        const TitanLibraryScanStatus *scan_status = TitanLibraryScan_GetStatus();
        TitanDiagnostics_Event(
            scan_status != 0 && scan_status->phase == TITAN_LIBRARY_SCAN_COMPLETE
                ? TITAN_DIAG_INFO:TITAN_DIAG_WARN,
            TITAN_DIAG_LIBRARY,0x613,0,
            scan_status != 0 ? scan_status->games_found : 0u,
            scan_status != 0 ? scan_status->applications_found : 0u,
            scan_status != 0 ? (uint32_t)scan_status->phase : 0u,0u
        );
        TitanStorageScheduler_Lock();
        g_storage_scheduler.scan_running = 0;
        g_storage_scheduler.scan_finished = 1;
        TitanStorageScheduler_Unlock();
        return 0;
    }

    return 1;
}

static int TitanStorageScheduler_PumpAudioRealtime(void)
{
    int did_work = TitanAudio_StorageWorkerPumpRealtime();
    if (did_work)
        TitanVlombisCore_RecordWork(
            TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                TITAN_VLOMBIS_CAP_BACKGROUND_QOS | TITAN_VLOMBIS_CAP_IO_COORDINATION,
            TITAN_VLOMBIS_EVENT_AUDIO_BUFFER_REFILL, 1u, 0u);
    return did_work;
}

static int TitanStorageScheduler_PumpAudioDeferred(void)
{
    int did_work = TitanAudio_StorageWorkerPumpDeferred();
    if (did_work)
        TitanVlombisCore_RecordWork(
            TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                TITAN_VLOMBIS_CAP_BACKGROUND_QOS,
            TITAN_VLOMBIS_EVENT_AUDIO_BUFFER_REFILL, 1u, 0u);
    return did_work;
}

#if defined(__EE__) || defined(_EE)
static void TitanStorageScheduler_WorkerBusyPump(void *user_data)
{
    (void)user_data;
    /* VMC verification/creation and metadata parsing already expose busy
     * pulse boundaries. Use those boundaries to refill the music ring from
     * this same storage lane before yielding; never render from the worker. */
    (void)TitanHidInput_StorageWorkerPump();
    (void)TitanStorageScheduler_PumpAudioRealtime();
}

static void TitanStorageScheduler_Worker(void *argument)
{
    (void)argument;

    for (;;)
    {
        TitanDiagnostics_StorageHeartbeat();
        int urgent_audio;

        /* Package installation is intentionally NOT executed by this EE worker.
         * The Package Installer catalog is enumerated successfully on the main
         * thread; real hardware regressed to 0%% when the same FileXio/newlib
         * path was moved to a second EE thread. During an install this worker is
         * therefore parked completely and the main thread becomes the sole
         * owner of filesystem RPCs, one bounded package step per frame. */
        if (TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive() ||
            TitanStorageScheduler_IsAnyExclusiveMode())
        {
            int package_mode = TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive() ||
                TitanStorageScheduler_IsPackageExclusiveMode();
            int app_mode = TitanStorageScheduler_IsTitanAppExclusiveMode();
            int game_mode = TitanStorageScheduler_IsTitanGameExclusiveMode();

            /* Acknowledge that this worker is no longer inside, or about to
             * enter, any filesystem client.  Package Installer and Titan App
             * runtime each wait for their own acknowledgement before the main
             * thread becomes the sole FileXio owner. */
            TitanStorageScheduler_SetPackageWorkerParked(package_mode);
            TitanStorageScheduler_SetTitanAppWorkerParked(app_mode);
            TitanStorageScheduler_SetTitanGameWorkerParked(game_mode);
            DelayThread(TITAN_STORAGE_WORKER_IDLE_DELAY_US);
            continue;
        }
        TitanStorageScheduler_SetPackageWorkerParked(0);
        TitanStorageScheduler_SetTitanAppWorkerParked(0);
        TitanStorageScheduler_SetTitanGameWorkerParked(0);

        /* Keyboard's PS2SDK driver is exposed through FileXio. fileXioRead()
         * waits on the library-wide completion semaphore even when the device
         * itself is configured non-blocking, so it must never run on the 60 Hz
         * frame thread. Poll it here on the single-owner FileXio lane and let
         * TitanHidInput_Update() consume only the RAM ring. */
        (void)TitanHidInput_StorageWorkerPump();

        /* Fair single-lane scheduling. Audio is allowed to preempt bulk I/O
         * only when the RAM ring falls below the low-water mark. One refill
         * chunk is then followed by one bounded storage step, so an install or
         * library scan can never be starved while music continuously drains. */
        urgent_audio = TitanAudio_StorageWorkerNeedsRealtime();
        if (urgent_audio)
        {
            TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_BACKGROUND_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS, TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION);
            (void)TitanStorageScheduler_PumpAudioRealtime();
        }

        if (TitanStorageScheduler_HasForegroundWork())
        {
            TitanStorageScheduler_RunForegroundJob();
            if (TitanAudio_StorageWorkerNeedsRealtime())
            {
                (void)TitanStorageScheduler_PumpAudioRealtime();
            }
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanStorageScheduler_RunVmcRefresh())
        {
            if (TitanAudio_StorageWorkerNeedsRealtime())
            {
                (void)TitanStorageScheduler_PumpAudioRealtime();
            }
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* RC5.6: an accepted Library request gets a guaranteed bounded quantum
         * immediately after urgent audio / foreground / one-shot VMC work.
         * RC5.5 still placed deferred audio and several always-active background
         * lanes above discovery, which hardware proved could leave 0x610 without
         * ever reaching 0x612. Deferred music refills must not starve the scan. */
        if (TitanStorageScheduler_StartOrStepScan())
        {
            g_storage_scheduler.asset_scan_turn = 0;
            g_storage_scheduler.inventory_bulk_turn = 0;
            if (TitanAudio_StorageWorkerNeedsRealtime())
            {
                (void)TitanStorageScheduler_PumpAudioRealtime();
            }
            DelayThread(TITAN_STORAGE_SCAN_DELAY_US);
            continue;
        }

        if (TitanStorageScheduler_PumpAudioDeferred())
        {
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanLocalization_HasStorageWork())
        {
            (void)TitanLocalization_StorageWorkerPump();
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanDiscService_HasStorageWork())
        {
            (void)TitanDiscService_StorageWorkerPump();
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanShellBackground_HasStorageWork())
        {
            (void)TitanShellBackground_StorageWorkerPump();
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanAppAssets_HasBuildWork())
        {
            (void)TitanAppAssets_StepBuild();
            if (TitanAudio_StorageWorkerNeedsRealtime())
            {
                (void)TitanStorageScheduler_PumpAudioRealtime();
            }
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* Downloads are user-facing background work. Network reception occurs on
         * its own EE worker, but all FileXio writes remain serialized here. One
         * 4 KiB quantum is serviced before yielding back to audio/other lanes. */
        if (TitanDownloadService_HasStorageWork())
        {
            if (TitanDownloadService_StorageWorkerPump())
                TitanVlombisCore_Record(
                    TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                        TITAN_VLOMBIS_CAP_BACKGROUND_QOS | TITAN_VLOMBIS_CAP_IO_COORDINATION,
                    TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION);
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* Bluetooth bond persistence is tiny but must never happen from the IOP
         * USBD/HCI path. The bridge only exposes work after CONNECT_IN_PROGRESS
         * is clear, so this lane cannot contend with the active pairing handshake. */
        if (TitanPademuInputBridge_HasStorageWork())
        {
            (void)TitanPademuInputBridge_StorageWorkerPump();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* P10.6 controller assignment is tiny persistent settings work. It lives
         * on the serialized storage lane, never in Settings/frame callbacks. */
        if (TitanControllerAssignment_HasStorageWork())
        {
            (void)TitanControllerAssignment_StorageWorkerPump();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanHealthTrace_HasStorageWork())
        {
            (void)TitanHealthTrace_StorageWorkerPump();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* Diagnostics is a tiny sequential snapshot and sits below all user-facing
         * foreground/audio/app work, but above bulk artwork. It can never reserve
         * the lane; one bounded flush is attempted and control returns immediately. */
        if (TitanDiagnostics_HasStorageWork())
        {
            (void)TitanDiagnostics_StorageWorkerPump();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* Storage Settings inventory is a bulk/background reader. Give it one
         * directory-entry quantum, then force at least one existing bulk lane
         * (artwork/library scan) to get a chance before inventory runs again.
         * Foreground, disc, audio, localization and downloads always stay above it. */
        if (TitanStorageInventory_HasStorageWork() && !g_storage_scheduler.inventory_bulk_turn)
        {
            (void)TitanStorageInventory_StorageWorkerPump();
            g_storage_scheduler.inventory_bulk_turn = 1;
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* Game artwork is deliberately below foreground/VMC/audio/app assets,
         * and shares the bulk lane fairly with Library scan. Exactly one 8 KiB
         * asset quantum may run before scan gets the next turn. This prevents a
         * large artwork queue from starving catalog discovery and, conversely,
         * prevents a long USB scan from keeping every visible game on placeholders. */
        if (TitanAssetService_HasStorageWork() && !g_storage_scheduler.asset_scan_turn)
        {
            (void)TitanAssetService_StorageWorkerPump();
            g_storage_scheduler.asset_scan_turn = 1;
            g_storage_scheduler.inventory_bulk_turn = 0;
            if (TitanAudio_StorageWorkerNeedsRealtime())
            {
                (void)TitanStorageScheduler_PumpAudioRealtime();
            }
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanAssetService_HasStorageWork())
        {
            (void)TitanAssetService_StorageWorkerPump();
            g_storage_scheduler.asset_scan_turn = 1;
            g_storage_scheduler.inventory_bulk_turn = 0;
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        if (TitanStorageInventory_HasStorageWork())
        {
            (void)TitanStorageInventory_StorageWorkerPump();
            g_storage_scheduler.inventory_bulk_turn = 1;
            if (TitanAudio_StorageWorkerNeedsRealtime())
                (void)TitanStorageScheduler_PumpAudioRealtime();
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        /* When no user/background storage job is waiting, opportunistically
         * fill music all the way to its high-water mark. This preserves the
         * multi-second PCM cushion without delaying foreground progress. */
        if (TitanStorageScheduler_PumpAudioRealtime())
        {
            DelayThread(TITAN_STORAGE_WORKER_ACTIVE_DELAY_US);
            continue;
        }

        DelayThread(TITAN_STORAGE_WORKER_IDLE_DELAY_US);
    }
}
#endif

int TitanStorageScheduler_Initialize(void)
{
    memset(&g_storage_scheduler, 0, sizeof(g_storage_scheduler));
#if defined(__EE__) || defined(_EE)
    g_storage_scheduler.worker_thread_id = -1;
    g_storage_scheduler.wake_sema_id = -1;
    g_storage_scheduler.lock_sema_id = -1;

    TitanShellBusy_SetWorkerPump(
        TitanStorageScheduler_WorkerBusyPump,
        0
    );

    {
        ee_sema_t sema;
        ee_thread_t thread;

        memset(&sema, 0, sizeof(sema));
        sema.init_count = 1;
        sema.max_count = 1;
        sema.option = 0;
        g_storage_scheduler.lock_sema_id = CreateSema(&sema);
        if (g_storage_scheduler.lock_sema_id < 0)
        {
            g_storage_scheduler.initialized = 1;
            return 0;
        }

        memset(&sema, 0, sizeof(sema));
        sema.init_count = 0;
        sema.max_count = 1;
        sema.option = 0;
        g_storage_scheduler.wake_sema_id = CreateSema(&sema);
        if (g_storage_scheduler.wake_sema_id < 0)
        {
            DeleteSema(g_storage_scheduler.lock_sema_id);
            g_storage_scheduler.lock_sema_id = -1;
            g_storage_scheduler.initialized = 1;
            return 0;
        }

        memset(&thread, 0, sizeof(thread));
        thread.attr = 0;
        thread.option = 0;
        thread.func = TitanStorageScheduler_Worker;
        thread.stack = g_storage_scheduler.worker_stack;
        thread.stack_size = sizeof(g_storage_scheduler.worker_stack);
        thread.gp_reg = &_gp;
        thread.initial_priority = TITAN_STORAGE_WORKER_PRIORITY;

        g_storage_scheduler.worker_thread_id = CreateThread(&thread);
        if (g_storage_scheduler.worker_thread_id < 0 ||
            StartThread(g_storage_scheduler.worker_thread_id, 0) < 0)
        {
            if (g_storage_scheduler.worker_thread_id >= 0)
            {
                DeleteThread(g_storage_scheduler.worker_thread_id);
            }
            DeleteSema(g_storage_scheduler.wake_sema_id);
            DeleteSema(g_storage_scheduler.lock_sema_id);
            g_storage_scheduler.worker_thread_id = -1;
            g_storage_scheduler.wake_sema_id = -1;
            g_storage_scheduler.lock_sema_id = -1;
            g_storage_scheduler.initialized = 1;
            return 0;
        }

        g_storage_scheduler.threaded = 1;
    }
#endif

    g_storage_scheduler.initialized = 1;
    return 1;
}

static void TitanStorageScheduler_RunFallbackWork(void)
{
    if (g_storage_scheduler.threaded)
    {
        return;
    }

    /* Degraded host/thread-creation fallback only. Package I/O is still driven
     * from UpdateMainThread so there is exactly one EE owner in every mode. */
    if (TitanStorageScheduler_IsAnyExclusiveMode())
    {
        return;
    }

    (void)TitanStorageScheduler_PumpAudioRealtime();

    if (TitanHealthTrace_HasStorageWork())
    {
        (void)TitanHealthTrace_StorageWorkerPump();
        return;
    }

    if (TitanStorageScheduler_HasForegroundWork())
    {
        TitanStorageScheduler_RunForegroundJob();
        return;
    }

    if (TitanStorageScheduler_RunVmcRefresh())
    {
        return;
    }

    if (TitanStorageScheduler_StartOrStepScan())
    {
        g_storage_scheduler.asset_scan_turn = 0;
        return;
    }

    if (TitanStorageScheduler_PumpAudioDeferred())
    {
        return;
    }

    if (TitanLocalization_HasStorageWork())
    {
        (void)TitanLocalization_StorageWorkerPump();
        return;
    }

    if (TitanDiscService_HasStorageWork())
    {
        (void)TitanDiscService_StorageWorkerPump();
        return;
    }

    if (TitanShellBackground_HasStorageWork())
    {
        (void)TitanShellBackground_StorageWorkerPump();
        return;
    }

    if (TitanAppAssets_HasBuildWork())
    {
        (void)TitanAppAssets_StepBuild();
        return;
    }

    if (TitanControllerAssignment_HasStorageWork())
    {
        (void)TitanControllerAssignment_StorageWorkerPump();
        return;
    }

    if (TitanStorageInventory_HasStorageWork())
    {
        (void)TitanStorageInventory_StorageWorkerPump();
        return;
    }

    if (TitanAssetService_HasStorageWork() && !g_storage_scheduler.asset_scan_turn)
    {
        (void)TitanAssetService_StorageWorkerPump();
        g_storage_scheduler.asset_scan_turn = 1;
        return;
    }
    if (TitanAssetService_HasStorageWork())
    {
        (void)TitanAssetService_StorageWorkerPump();
        g_storage_scheduler.asset_scan_turn = 1;
    }
}

void TitanStorageScheduler_UpdateMainThread(void)
{
    int scan_finished;
    uint32_t package_budget;
    const TitanLibraryScanStatus *status;

    if (!g_storage_scheduler.initialized)
    {
        return;
    }

    /* A package install already owns the storage lane exclusively. Shrinking
     * its syscall quantum after one slow FAT/FileXio frame creates a harmful
     * feedback loop: 4 KiB -> 1 KiB means roughly four times as many
     * synchronous RPC boundaries, which prolongs both the install and the
     * pressure condition. Keep the hardware-proven 4 KiB quantum while the
     * package lane is exclusive; adaptive QoS remains in effect for updates. */
    if (TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive())
        package_budget = TITAN_STORAGE_PACKAGE_BUDGET;
    else
        package_budget = TitanVlombisCore_GetRecommendedIoQuantum(
            TITAN_STORAGE_PACKAGE_BUDGET,
            TITAN_STORAGE_PACKAGE_MIN_BUDGET,
            TITAN_STORAGE_PACKAGE_MAX_BUDGET);

    /* Package mode is a deliberate single-owner critical section: background
     * storage/audio-prefetch work is parked and the UI thread advances exactly
     * one package I/O quantum per frame. This trades occasional frame-time
     * spikes for deterministic FileXio ownership and hardware reliability. */
    if (TitanPackageInstaller_IsActive() &&
        (!g_storage_scheduler.threaded ||
         TitanStorageScheduler_IsPackageWorkerParked()))
    {
        TitanVlombisCore_Record(
            TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
            TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION);
        TitanPackageInstaller_Update(package_budget);
    }

    if (TitanSystemUpdate_IsActive() &&
        (!g_storage_scheduler.threaded ||
         TitanStorageScheduler_IsPackageWorkerParked()))
    {
        TitanVlombisCore_Record(
            TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
            TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION);
        TitanSystemUpdate_Update(package_budget);
    }

    TitanStorageScheduler_RunFallbackWork();
    if (!TitanStorageScheduler_IsAnyExclusiveMode())
    {
        (void)TitanAppAssets_CommitPendingAtlas();
    }

    if (TitanLibraryScan_IsActive())
    {
        status = TitanLibraryScan_GetStatus();
        if (status != 0 &&
            (status->progress_percent != g_storage_scheduler.scan_last_progress ||
             strcmp(status->message, g_storage_scheduler.scan_last_message) != 0))
        {
            g_storage_scheduler.scan_last_progress = status->progress_percent;
            strncpy(
                g_storage_scheduler.scan_last_message,
                status->message,
                sizeof(g_storage_scheduler.scan_last_message) - 1u
            );
            g_storage_scheduler.scan_last_message[
                sizeof(g_storage_scheduler.scan_last_message) - 1u
            ] = '\0';
            TitanNotifications_UpdateBackgroundActivity(
                status->message,
                status->progress_percent,
                status->processed,
                status->total,
                status->games_found,
                status->applications_found
            );
        }
    }

    TitanStorageScheduler_Lock();
    scan_finished = g_storage_scheduler.scan_finished;
    if (scan_finished)
    {
        g_storage_scheduler.scan_finished = 0;
    }
    TitanStorageScheduler_Unlock();

    if (scan_finished)
    {
        status = TitanLibraryScan_GetStatus();
        (void)TitanLibraryScan_CommitPendingCatalog();
        TitanAssetService_NotifyCatalogChanged();
        TitanAppAssets_RequestRebuild();
        TitanStorageScheduler_WakeWorker();

        if (status != 0 && status->phase == TITAN_LIBRARY_SCAN_COMPLETE)
        {
            TitanNotifications_CompleteBackgroundActivity(
                status->message,
                status->games_found,
                status->applications_found
            );
        }
        else
        {
            TitanNotifications_FailBackgroundActivity(
                status != 0 && status->message[0] != '\0'
                    ? status->message
                    : TitanLocalization_Compat("Failed to update the library")
            );
        }
    }
}

void TitanStorageScheduler_NotifyBackgroundWork(void)
{
    TitanStorageScheduler_WakeWorker();
}

int TitanStorageScheduler_RequestLibraryScan(int force_rebuild)
{
    int first_request;

    if (!g_storage_scheduler.initialized)
    {
        return 0;
    }

    first_request = 0;
    TitanStorageScheduler_Lock();
    if (!g_storage_scheduler.scan_requested &&
        !g_storage_scheduler.scan_running)
    {
        first_request = 1;
    }
    g_storage_scheduler.scan_requested = 1;
    if (force_rebuild)
    {
        g_storage_scheduler.scan_force_rebuild = 1;
    }
    TitanStorageScheduler_Unlock();

    if (first_request)
    {
        g_storage_scheduler.scan_last_progress = 0u;
        g_storage_scheduler.scan_last_message[0] = '\0';
        TitanNotifications_BeginBackgroundActivity(
            TitanLocalization_Compat("Updating library"),
            TitanLocalization_Compat("Searching for games and applications in storage")
        );
    }

    TitanStorageScheduler_WakeWorker();
    return 1;
}

void TitanStorageScheduler_SetPackageExclusiveMode(int enabled)
{
    if (!g_storage_scheduler.initialized)
    {
        return;
    }

    TitanStorageScheduler_Lock();
    g_storage_scheduler.package_exclusive_mode = enabled != 0;
    if (enabled) TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF);
    /* A fresh exclusive session must receive a fresh worker acknowledgement;
     * clearing this on both transitions prevents a stale parked bit from a
     * previous install from authorizing concurrent FileXio access. */
    g_storage_scheduler.package_worker_parked = 0;
    TitanStorageScheduler_Unlock();
    TitanStorageScheduler_WakeWorker();
}

void TitanStorageScheduler_SetTitanAppExclusiveMode(int enabled)
{
    if (!g_storage_scheduler.initialized)
    {
        return;
    }

    TitanStorageScheduler_Lock();
    g_storage_scheduler.app_exclusive_mode = enabled != 0;
    if (enabled) TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF);
    /* Never reuse a parked acknowledgement from a previous app session. */
    g_storage_scheduler.app_worker_parked = 0;
    TitanStorageScheduler_Unlock();
    TitanStorageScheduler_WakeWorker();
}

void TitanStorageScheduler_SetTitanGameExclusiveMode(int enabled)
{
    if (!g_storage_scheduler.initialized)
        return;
    TitanStorageScheduler_Lock();
    g_storage_scheduler.game_exclusive_mode = enabled != 0;
    if (enabled) TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF);
    g_storage_scheduler.game_worker_parked = 0;
    TitanStorageScheduler_Unlock();
    TitanStorageScheduler_WakeWorker();
}

int TitanStorageScheduler_RequestVmcRefresh(
    const TitanUserProfile *profile
)
{
    if (!g_storage_scheduler.initialized || profile == 0)
    {
        return 0;
    }

    TitanStorageScheduler_Lock();
    g_storage_scheduler.vmc_refresh_profile = *profile;
    g_storage_scheduler.vmc_refresh_requested = 1;
    TitanStorageScheduler_Unlock();
    TitanStorageScheduler_WakeWorker();
    return 1;
}

int TitanStorageScheduler_IsLibraryScanBusy(void)
{
    int busy;

    if (!g_storage_scheduler.initialized)
    {
        return TitanLibraryScan_IsActive();
    }

    TitanStorageScheduler_Lock();
    busy =
        g_storage_scheduler.scan_requested ||
        g_storage_scheduler.scan_running;
    TitanStorageScheduler_Unlock();
    return busy || TitanLibraryScan_IsActive();
}

static int TitanStorageScheduler_QueueForeground(
    TitanStorageForegroundJobType type,
    const TitanProfileStoragePreparedSave *prepared_save,
    const TitanUserProfile *profile,
    const char *application_root,
    const char *application_title_id,
    int allow_format_prompt,
    int show_success,
    int show_failure
)
{
    int accepted;

    if (!g_storage_scheduler.initialized)
    {
        return 0;
    }

    accepted = 0;
    TitanStorageScheduler_Lock();
    if (g_storage_scheduler.foreground_pending_type ==
            TITAN_STORAGE_FOREGROUND_NONE &&
        g_storage_scheduler.foreground_running_type ==
            TITAN_STORAGE_FOREGROUND_NONE &&
        !g_storage_scheduler.completion_pending)
    {
        g_storage_scheduler.foreground_pending_type = type;
        if (prepared_save != 0)
        {
            g_storage_scheduler.prepared_save = *prepared_save;
        }
        else
        {
            memset(
                &g_storage_scheduler.prepared_save,
                0,
                sizeof(g_storage_scheduler.prepared_save)
            );
        }
        if (profile != 0)
        {
            g_storage_scheduler.vmc_profile = *profile;
        }
        else
        {
            memset(
                &g_storage_scheduler.vmc_profile,
                0,
                sizeof(g_storage_scheduler.vmc_profile)
            );
        }
        TitanStorageScheduler_CopyText(
            g_storage_scheduler.application_root,
            sizeof(g_storage_scheduler.application_root),
            application_root
        );
        TitanStorageScheduler_CopyText(
            g_storage_scheduler.application_title_id,
            sizeof(g_storage_scheduler.application_title_id),
            application_title_id
        );
        g_storage_scheduler.foreground_allow_format_prompt =
            allow_format_prompt;
        g_storage_scheduler.foreground_show_success = show_success;
        g_storage_scheduler.foreground_show_failure = show_failure;
        accepted = 1;
    }
    TitanStorageScheduler_Unlock();

    if (accepted)
    {
        TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_FOREGROUND_JOB);
        TitanStorageScheduler_WakeWorker();
    }
    return accepted;
}

int TitanStorageScheduler_RequestProfileSave(
    int allow_format_prompt,
    int show_success,
    int show_failure
)
{
    TitanProfileStoragePreparedSave prepared;
    TitanProfileStorageSaveResult result;

    if (!g_storage_scheduler.initialized)
    {
        return 0;
    }

    result = TitanProfileStorage_PrepareSave(&prepared);
    if (result != TITAN_PROFILE_STORAGE_SAVE_OK)
    {
        TitanStorageScheduler_Lock();
        if (!g_storage_scheduler.completion_pending &&
            g_storage_scheduler.foreground_running_type ==
                TITAN_STORAGE_FOREGROUND_NONE &&
            g_storage_scheduler.foreground_pending_type ==
                TITAN_STORAGE_FOREGROUND_NONE)
        {
            memset(&g_storage_scheduler.completion, 0,
                sizeof(g_storage_scheduler.completion));
            g_storage_scheduler.completion.type =
                TITAN_STORAGE_FOREGROUND_PROFILE_SAVE;
            g_storage_scheduler.completion.result = (int)result;
            g_storage_scheduler.completion.allow_format_prompt =
                allow_format_prompt;
            g_storage_scheduler.completion.show_success = show_success;
            g_storage_scheduler.completion.show_failure = show_failure;
            g_storage_scheduler.completion_pending = 1;
            TitanStorageScheduler_Unlock();
            return 1;
        }
        TitanStorageScheduler_Unlock();
        return 0;
    }

    return TitanStorageScheduler_QueueForeground(
        TITAN_STORAGE_FOREGROUND_PROFILE_SAVE,
        &prepared,
        0,
        0,
        0,
        allow_format_prompt,
        show_success,
        show_failure
    );
}

static int TitanStorageScheduler_RequestVmcJob(
    TitanStorageForegroundJobType type,
    const TitanUserProfile *profile
)
{
    if (profile == 0)
    {
        return 0;
    }

    return TitanStorageScheduler_QueueForeground(
        type,
        0,
        profile,
        0,
        0,
        0,
        0,
        1
    );
}

int TitanStorageScheduler_RequestVmcVerify(const TitanUserProfile *profile)
{
    return TitanStorageScheduler_RequestVmcJob(
        TITAN_STORAGE_FOREGROUND_VMC_VERIFY,
        profile
    );
}

int TitanStorageScheduler_RequestVmcFormat(const TitanUserProfile *profile)
{
    return TitanStorageScheduler_RequestVmcJob(
        TITAN_STORAGE_FOREGROUND_VMC_FORMAT,
        profile
    );
}

int TitanStorageScheduler_RequestVmcRecreate(const TitanUserProfile *profile)
{
    return TitanStorageScheduler_RequestVmcJob(
        TITAN_STORAGE_FOREGROUND_VMC_RECREATE,
        profile
    );
}

int TitanStorageScheduler_RequestApplicationUninstall(
    const char *root_path,
    const char *title_id
)
{
    if (root_path == 0 || title_id == 0 || root_path[0] == '\0' || title_id[0] == '\0')
        return 0;
    return TitanStorageScheduler_QueueForeground(
        TITAN_STORAGE_FOREGROUND_APP_UNINSTALL,
        0,
        0,
        root_path,
        title_id,
        0,
        0,
        1
    );
}

int TitanStorageScheduler_RequestAppAssetsRebuild(void)
{
    if (!g_storage_scheduler.initialized)
        return 0;
    TitanAppAssets_RequestRebuild();
    TitanStorageScheduler_WakeWorker();
    return 1;
}

int TitanStorageScheduler_IsForegroundBusy(void)
{
    int busy;

    if (!g_storage_scheduler.initialized)
    {
        return 0;
    }

    TitanStorageScheduler_Lock();
    busy =
        g_storage_scheduler.foreground_pending_type !=
            TITAN_STORAGE_FOREGROUND_NONE ||
        g_storage_scheduler.foreground_running_type !=
            TITAN_STORAGE_FOREGROUND_NONE;
    TitanStorageScheduler_Unlock();
    return busy;
}

int TitanStorageScheduler_PollCompletion(TitanStorageCompletion *completion)
{
    int available;

    if (completion == 0 || !g_storage_scheduler.initialized)
    {
        return 0;
    }

    available = 0;
    TitanStorageScheduler_Lock();
    if (g_storage_scheduler.completion_pending)
    {
        *completion = g_storage_scheduler.completion;
        memset(
            &g_storage_scheduler.completion,
            0,
            sizeof(g_storage_scheduler.completion)
        );
        g_storage_scheduler.completion_pending = 0;
        available = 1;
    }
    TitanStorageScheduler_Unlock();
    return available;
}
