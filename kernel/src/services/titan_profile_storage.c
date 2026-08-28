#include "titan_profile_storage.h"

#include "titan_profile_snapshot.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_shell_busy.h"
#include "titan_user_profile.h"

#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <libmc.h>
#include <loadfile.h>
#include <sbv_patches.h>

#include "titan_iop_modules.h"
#endif

#define TITAN_STORAGE_MOUNT_ATTEMPTS 240
#define TITAN_STORAGE_MOUNT_DELAY_US 50000

#define TITAN_STORAGE_COMMAND_NONE 0
#define TITAN_STORAGE_COMMAND_USB_USBD 1
#define TITAN_STORAGE_COMMAND_USB_MASS 2
#define TITAN_STORAGE_COMMAND_IOMANX 3
#define TITAN_STORAGE_COMMAND_FILEXIO 4
#define TITAN_STORAGE_COMMAND_DIRECTORY 5
#define TITAN_STORAGE_COMMAND_OPEN 6
#define TITAN_STORAGE_COMMAND_WRITE 7
#define TITAN_STORAGE_COMMAND_READ 8
#define TITAN_STORAGE_COMMAND_DECODE 9
#define TITAN_STORAGE_COMMAND_MEMORY_CARD 10

static TitanProfileStorageStatus g_storage_status =
    TITAN_PROFILE_STORAGE_NOT_INITIALIZED;
static int g_storage_initialized;
static int g_storage_loaded_profiles;
#if defined(__EE__) || defined(_EE)
static int g_memory_card_service_ready;
#endif
static unsigned int g_storage_generation;
static unsigned int g_storage_saved_revision;
static TitanVmcResult g_storage_last_vmc_result =
    TITAN_VMC_RESULT_NO_STORAGE;
static TitanProfileStorageFailureStage g_storage_last_failure_stage;
static int g_storage_last_failure_command;
static int g_storage_last_failure_result;

static unsigned char g_storage_snapshot[
    TITAN_PROFILE_SNAPSHOT_SIZE
] __attribute__((aligned(64)));

static unsigned char g_storage_verification[
    TITAN_PROFILE_SNAPSHOT_SIZE
] __attribute__((aligned(64)));

static void TitanProfileStorage_ClearFailure(void)
{
    g_storage_last_failure_stage =
        TITAN_PROFILE_STORAGE_FAILURE_NONE;
    g_storage_last_failure_command =
        TITAN_STORAGE_COMMAND_NONE;
    g_storage_last_failure_result = 0;
}

static void TitanProfileStorage_RecordFailure(
    TitanProfileStorageFailureStage stage,
    int command,
    int result
)
{
    g_storage_last_failure_stage = stage;
    g_storage_last_failure_command = command;
    g_storage_last_failure_result = result;
}

static int TitanProfileStorage_EnsureDirectory(
    const char *path
)
{
    int is_directory;

    if (
        TitanStorageIo_PathInfo(
            path,
            &is_directory,
            0
        )
    )
    {
        return is_directory;
    }

    if (TitanStorageIo_CreateDirectory(path) >= 0)
    {
        return 1;
    }

    return
        TitanStorageIo_PathInfo(
            path,
            &is_directory,
            0
        ) &&
        is_directory;
}

static int TitanProfileStorage_EnsureRoot(void)
{
    int attempt;

    for (attempt = 0; attempt < TITAN_STORAGE_MOUNT_ATTEMPTS; attempt++)
    {
        if (
            TitanProfileStorage_EnsureDirectory(
                TITAN_STORAGE_DEVICE_ROOT
            ) &&
            TitanProfileStorage_EnsureDirectory(
                TITAN_PROFILE_USERS_PATH
            )
        )
        {
            return 1;
        }

#if defined(__EE__) || defined(_EE)
        TitanShellBusy_Pulse();
        DelayThread(TITAN_STORAGE_MOUNT_DELAY_US);
#else
        break;
#endif
    }

    TitanProfileStorage_RecordFailure(
        TITAN_PROFILE_STORAGE_FAILURE_STORAGE_MOUNT,
        TITAN_STORAGE_COMMAND_DIRECTORY,
        -1
    );

    return 0;
}

static int TitanProfileStorage_ReadExact(
    const char *path,
    unsigned char *buffer,
    unsigned int size,
    int missing_is_empty
)
{
    int file_size;
    unsigned int offset;
    int path_exists;
    int descriptor;

    path_exists = TitanStorageIo_PathInfo(path, 0, 0);
    descriptor = TitanStorageIo_Open(
        path,
        TITAN_STORAGE_IO_READ
    );

    if (descriptor < 0)
    {
        if (missing_is_empty && !path_exists)
        {
            return 0;
        }

        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_READ_OPEN,
            TITAN_STORAGE_COMMAND_OPEN,
            descriptor
        );
        return -1;
    }

    file_size = TitanStorageIo_Seek(
        descriptor,
        0,
        TITAN_STORAGE_IO_SEEK_END
    );

    if (
        file_size != (int)size ||
        TitanStorageIo_Seek(
            descriptor,
            0,
            TITAN_STORAGE_IO_SEEK_SET
        ) != 0
    )
    {
        TitanStorageIo_Close(descriptor);
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_READ_DATA,
            TITAN_STORAGE_COMMAND_READ,
            (int)file_size
        );
        return -1;
    }

    offset = 0u;

    while (offset < size)
    {
        int result;

        result = TitanStorageIo_Read(
            descriptor,
            &buffer[offset],
            size - offset
        );

        if (result <= 0)
        {
            TitanStorageIo_Close(descriptor);
            TitanProfileStorage_RecordFailure(
                TITAN_PROFILE_STORAGE_FAILURE_READ_DATA,
                TITAN_STORAGE_COMMAND_READ,
                (int)result
            );
            return -1;
        }

        offset += (unsigned int)result;
    }

    if (TitanStorageIo_Close(descriptor) < 0)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_READ_CLOSE,
            TITAN_STORAGE_COMMAND_READ,
            -1
        );
        return -1;
    }

    return 1;
}

static int TitanProfileStorage_WriteExact(
    const char *path,
    const unsigned char *buffer,
    unsigned int size
)
{
    unsigned int offset;
    int descriptor;

    descriptor = TitanStorageIo_Open(
        path,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );

    if (descriptor < 0)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_WRITE_OPEN,
            TITAN_STORAGE_COMMAND_OPEN,
            descriptor
        );
        return 0;
    }

    offset = 0u;

    while (offset < size)
    {
        int result;

        result = TitanStorageIo_Write(
            descriptor,
            &buffer[offset],
            size - offset
        );

        if (result <= 0)
        {
            TitanStorageIo_Close(descriptor);
            TitanProfileStorage_RecordFailure(
                TITAN_PROFILE_STORAGE_FAILURE_WRITE_DATA,
                TITAN_STORAGE_COMMAND_WRITE,
                (int)result
            );
            return 0;
        }

        offset += (unsigned int)result;
    }

    if (TitanStorageIo_Close(descriptor) < 0)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_WRITE_CLOSE,
            TITAN_STORAGE_COMMAND_WRITE,
            -1
        );
        return 0;
    }

    return 1;
}

static TitanProfileStorageStatus TitanProfileStorage_DecodePath(
    const char *path,
    unsigned char *buffer,
    TitanUserProfileState *state,
    unsigned int *generation,
    int missing_is_empty
)
{
    TitanProfileSnapshotResult result;
    int read_result;

    read_result = TitanProfileStorage_ReadExact(
        path,
        buffer,
        TITAN_PROFILE_SNAPSHOT_SIZE,
        missing_is_empty
    );
    if (read_result == 0)
    {
        return TITAN_PROFILE_STORAGE_READY_EMPTY;
    }
    if (read_result < 0)
    {
        return TITAN_PROFILE_STORAGE_CORRUPT;
    }

    result = TitanProfileSnapshot_Decode(
        buffer,
        TITAN_PROFILE_SNAPSHOT_SIZE,
        state,
        generation
    );
    if (result == TITAN_PROFILE_SNAPSHOT_FUTURE_VERSION)
    {
        return TITAN_PROFILE_STORAGE_FUTURE_VERSION;
    }
    if (result != TITAN_PROFILE_SNAPSHOT_OK ||
        !TitanUserProfiles_ValidateState(state))
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_VERIFY,
            TITAN_STORAGE_COMMAND_DECODE,
            (int)result
        );
        return TITAN_PROFILE_STORAGE_CORRUPT;
    }

    return TITAN_PROFILE_STORAGE_READY_LOADED;
}

static int TitanProfileStorage_PublishRecoveredPath(
    const char *recovery_path
)
{
    if (recovery_path == 0)
    {
        return 0;
    }

    TitanStorageIo_Remove(TITAN_PROFILE_DATA_PATH);
    if (TitanStorageIo_Rename(
        recovery_path,
        TITAN_PROFILE_DATA_PATH
    ) != 0)
    {
        return 0;
    }

    TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
    return 1;
}

static TitanProfileStorageStatus TitanProfileStorage_LoadData(void)
{
    TitanProfileStorageStatus status;
    TitanProfileStorageStatus recovery_status;
    TitanUserProfileState state;
    unsigned int generation;
    const char *recovery_paths[2];
    unsigned int recovery_index;
    int primary_missing;

    primary_missing = !TitanStorageIo_PathInfo(
        TITAN_PROFILE_DATA_PATH,
        0,
        0
    );

    status = TitanProfileStorage_DecodePath(
        TITAN_PROFILE_DATA_PATH,
        g_storage_snapshot,
        &state,
        &generation,
        1
    );
    if (status == TITAN_PROFILE_STORAGE_READY_LOADED)
    {
        if (!TitanUserProfiles_ImportState(&state))
        {
            return TITAN_PROFILE_STORAGE_CORRUPT;
        }

        g_storage_loaded_profiles = 1;
        g_storage_generation = generation;
        g_storage_saved_revision = TitanUserProfiles_GetRevision();
        TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
        TitanProfileStorage_ClearFailure();
        return TITAN_PROFILE_STORAGE_READY_LOADED;
    }

    /* A snapshot de versão futura nunca deve ser substituída por uma cópia
     * antiga. O usuário precisa de um binário compatível para abri-la. */
    if (status == TITAN_PROFILE_STORAGE_FUTURE_VERSION)
    {
        return status;
    }

    recovery_paths[0] = TITAN_PROFILE_DATA_BACKUP_PATH;
    recovery_paths[1] = TITAN_PROFILE_DATA_TEMP_PATH;

    for (recovery_index = 0u; recovery_index < 2u; recovery_index++)
    {
        recovery_status = TitanProfileStorage_DecodePath(
            recovery_paths[recovery_index],
            g_storage_verification,
            &state,
            &generation,
            1
        );
        if (recovery_status == TITAN_PROFILE_STORAGE_FUTURE_VERSION)
        {
            return recovery_status;
        }
        if (recovery_status != TITAN_PROFILE_STORAGE_READY_LOADED)
        {
            continue;
        }

        if (!TitanUserProfiles_ImportState(&state))
        {
            continue;
        }

        /* A publicação é best-effort: mesmo se a mídia negar o rename, a
         * sessão usa o snapshot validado e não descarta o perfil recuperado. */
        TitanProfileStorage_PublishRecoveredPath(
            recovery_paths[recovery_index]
        );
        g_storage_loaded_profiles = 1;
        g_storage_generation = generation;
        g_storage_saved_revision = TitanUserProfiles_GetRevision();
        TitanProfileStorage_ClearFailure();
        return TITAN_PROFILE_STORAGE_READY_LOADED;
    }

    g_storage_loaded_profiles = 0;
    g_storage_generation = 0u;
    g_storage_saved_revision = 0u;

    if (primary_missing &&
        !TitanStorageIo_PathInfo(TITAN_PROFILE_DATA_BACKUP_PATH, 0, 0) &&
        !TitanStorageIo_PathInfo(TITAN_PROFILE_DATA_TEMP_PATH, 0, 0))
    {
        TitanProfileStorage_ClearFailure();
        return TITAN_PROFILE_STORAGE_READY_EMPTY;
    }

    return TITAN_PROFILE_STORAGE_CORRUPT;
}

#if defined(__EE__) || defined(_EE)
static int TitanProfileStorage_LoadEmbeddedModule(
    void *module,
    unsigned int module_size,
    int command
)
{
    int module_result;
    int module_id;

    module_result = -1;
    module_id = SifExecModuleBuffer(
        module,
        module_size,
        0,
        0,
        &module_result
    );

    if (module_id < 0 || module_result < 0)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_USB_MODULE,
            command,
            module_id < 0 ? module_id : module_result
        );
        return 0;
    }

    return 1;
}

static void TitanProfileStorage_InitializeUsb(void)
{
    sbv_patch_enable_lmb();

    /* Never short-circuit this chain. A bootloader may leave one service
     * resident, making that module report an error even though later modules
     * still have to be loaded. Actual mass:/ availability is authoritative. */
    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_iomanx_irx,
        size_titan_iomanx_irx,
        TITAN_STORAGE_COMMAND_IOMANX
    );
    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_filexio_irx,
        size_titan_filexio_irx,
        TITAN_STORAGE_COMMAND_FILEXIO
    );

    /* Match the wLaunchELF bootstrap ordering: bind the EE FileXio client
     * before USBD/BDM are started. The later real mass:/ probe remains the
     * authority, so an already-bound/negative diagnostic is non-fatal. */
    (void)TitanStorageIo_InitializeClient();

    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_usbd_irx,
        size_titan_usbd_irx,
        TITAN_STORAGE_COMMAND_USB_USBD
    );
    /* Match wLaunchELF's proven modern USB storage chain. USBHDFSD remains
     * embedded for installer compatibility, but the runtime mounts through BDM. */
    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_bdm_irx,
        size_titan_bdm_irx,
        TITAN_STORAGE_COMMAND_USB_MASS
    );
    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_bdmfs_fatfs_irx,
        size_titan_bdmfs_fatfs_irx,
        TITAN_STORAGE_COMMAND_USB_MASS
    );
    (void)TitanProfileStorage_LoadEmbeddedModule(
        titan_usbmass_bd_irx,
        size_titan_usbmass_bd_irx,
        TITAN_STORAGE_COMMAND_USB_MASS
    );
}

static void TitanProfileStorage_InitializeMemoryCard(
    int use_extended_modules
)
{
    const char *manager_path;
    const char *server_path;
    int server_type;

    manager_path =
        use_extended_modules ? "rom0:XMCMAN" : "rom0:MCMAN";
    server_path =
        use_extended_modules ? "rom0:XMCSERV" : "rom0:MCSERV";
    server_type =
        use_extended_modules ? MC_TYPE_XMC : MC_TYPE_MC;

    g_memory_card_service_ready =
        SifLoadModule(manager_path, 0, 0) >= 0 &&
        SifLoadModule(server_path, 0, 0) >= 0 &&
        mcInit(server_type) >= 0;
}

static int TitanProfileStorage_MemoryCardSync(
    int request_result,
    int *operation_result
)
{
    int command;
    int result;
    int sync_result;

    if (request_result < 0)
    {
        return 0;
    }

    command = 0;
    result = 0;

    for (;;)
    {
        sync_result = mcSync(1, &command, &result);
        if (sync_result < 0)
        {
            return 0;
        }
        if (sync_result > 0)
        {
            break;
        }

        TitanShellBusy_Pulse();
        DelayThread(1000);
    }

    g_storage_last_failure_command = command;
    g_storage_last_failure_result = result;

    if (operation_result != 0)
    {
        *operation_result = result;
    }

    return 1;
}
#endif

TitanProfileStorageStatus TitanProfileStorage_Initialize(
    int use_extended_modules
)
{
    TitanProfileStorageSaveResult initial_save;

    if (g_storage_initialized)
    {
        return g_storage_status;
    }

    TitanUserProfiles_Initialize();
    TitanProfileStorage_ClearFailure();
    g_storage_initialized = 1;
    g_storage_loaded_profiles = 0;
    g_storage_generation = 0u;
    g_storage_saved_revision = 0u;

#if defined(__EE__) || defined(_EE)
    TitanProfileStorage_InitializeUsb();
    TitanProfileStorage_InitializeMemoryCard(use_extended_modules);
#else
    (void)use_extended_modules;
#endif

    if (!TitanStorageIo_InitializeClient())
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_STORAGE_MOUNT,
            TITAN_STORAGE_COMMAND_OPEN,
            -1
        );
        /* Continue to the real device probe. fileXioInit() may report that a
         * client is already bound while the POSIX mass device is usable. */
    }

#if defined(__EE__) || defined(_EE)
    {
        unsigned int usb_attempt;
        int root_ready = 0;
        /* Physical USB enumeration is asynchronous. The installer already
         * proved on hardware that a single immediate probe is insufficient. */
        for (usb_attempt = 0u; usb_attempt < 120u; ++usb_attempt)
        {
            if (TitanProfileStorage_EnsureRoot())
            {
                root_ready = 1;
                break;
            }
            DelayThread(100000);
        }
        if (!root_ready)
        {
            g_storage_status = TITAN_PROFILE_STORAGE_NO_STORAGE;
            return g_storage_status;
        }
    }
#else
    if (!TitanProfileStorage_EnsureRoot())
    {
        g_storage_status = TITAN_PROFILE_STORAGE_NO_STORAGE;
        return g_storage_status;
    }
#endif

    /* A successful root probe supersedes benign duplicate-module/client
     * diagnostics and proves that profile, DEX and catalog persistence can
     * use the device. */
    TitanProfileStorage_ClearFailure();

    g_storage_status = TitanProfileStorage_LoadData();

    if (g_storage_status == TITAN_PROFILE_STORAGE_READY_EMPTY)
    {
        initial_save = TitanProfileStorage_Save();

        if (
            initial_save != TITAN_PROFILE_STORAGE_SAVE_OK &&
            initial_save != TITAN_PROFILE_STORAGE_SAVE_NOT_NEEDED
        )
        {
            g_storage_status = TITAN_PROFILE_STORAGE_IO_ERROR;
        }
    }
    else if (g_storage_status == TITAN_PROFILE_STORAGE_READY_LOADED)
    {
        /* Profile metadata must never trigger VMC filesystem I/O.  The kernel
         * seeds VMC readiness later, after the health/safety animation. */
        TitanVmc_InvalidateCachedStatus(0);
        g_storage_last_vmc_result = TITAN_VMC_RESULT_OK;
    }

    return g_storage_status;
}

TitanProfileStorageStatus TitanProfileStorage_GetStatus(void)
{
    return g_storage_status;
}

TitanProfileStorageStatus TitanProfileStorage_Reload(void)
{
    if (!g_storage_initialized || !TitanProfileStorage_EnsureRoot())
    {
        g_storage_status = TITAN_PROFILE_STORAGE_NO_STORAGE;
        return g_storage_status;
    }

    TitanProfileStorage_ClearFailure();
    g_storage_status = TitanProfileStorage_LoadData();

    if (g_storage_status == TITAN_PROFILE_STORAGE_READY_LOADED)
    {
        TitanVmc_InvalidateCachedStatus(0);
        g_storage_last_vmc_result = TITAN_VMC_RESULT_OK;
    }

    return g_storage_status;
}

static int TitanProfileStorage_VerifySnapshotPath(
    const char *path,
    const unsigned char *expected,
    unsigned int expected_generation
)
{
    TitanProfileSnapshotResult result;
    TitanUserProfileState state;
    unsigned int generation;

    if (TitanProfileStorage_ReadExact(
        path,
        g_storage_verification,
        sizeof(g_storage_verification),
        0
    ) != 1)
    {
        return 0;
    }

    result = TitanProfileSnapshot_Decode(
        g_storage_verification,
        sizeof(g_storage_verification),
        &state,
        &generation
    );
    if (result != TITAN_PROFILE_SNAPSHOT_OK ||
        generation != expected_generation ||
        memcmp(
            expected,
            g_storage_verification,
            sizeof(g_storage_verification)
        ) != 0 ||
        !TitanUserProfiles_ValidateState(&state))
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_VERIFY_CONTENT,
            TITAN_STORAGE_COMMAND_DECODE,
            (int)result
        );
        return 0;
    }

    return 1;
}

static int TitanProfileStorage_CommitSnapshot(
    const unsigned char *snapshot,
    unsigned int generation
)
{
    int had_primary;

    TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
    if (!TitanProfileStorage_WriteExact(
        TITAN_PROFILE_DATA_TEMP_PATH,
        snapshot,
        TITAN_PROFILE_SNAPSHOT_SIZE
    ) || !TitanProfileStorage_VerifySnapshotPath(
        TITAN_PROFILE_DATA_TEMP_PATH,
        snapshot,
        generation
    ))
    {
        TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
        return 0;
    }

    had_primary = TitanStorageIo_PathInfo(
        TITAN_PROFILE_DATA_PATH,
        0,
        0
    );
    TitanStorageIo_Remove(TITAN_PROFILE_DATA_BACKUP_PATH);

    if (had_primary && TitanStorageIo_Rename(
        TITAN_PROFILE_DATA_PATH,
        TITAN_PROFILE_DATA_BACKUP_PATH
    ) != 0)
    {
        TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
        return 0;
    }

    if (TitanStorageIo_Rename(
        TITAN_PROFILE_DATA_TEMP_PATH,
        TITAN_PROFILE_DATA_PATH
    ) != 0)
    {
        if (had_primary)
        {
            TitanStorageIo_Rename(
                TITAN_PROFILE_DATA_BACKUP_PATH,
                TITAN_PROFILE_DATA_PATH
            );
        }
        TitanStorageIo_Remove(TITAN_PROFILE_DATA_TEMP_PATH);
        return 0;
    }

    if (!TitanProfileStorage_VerifySnapshotPath(
        TITAN_PROFILE_DATA_PATH,
        snapshot,
        generation
    ))
    {
        TitanStorageIo_Remove(TITAN_PROFILE_DATA_PATH);
        if (had_primary)
        {
            TitanStorageIo_Rename(
                TITAN_PROFILE_DATA_BACKUP_PATH,
                TITAN_PROFILE_DATA_PATH
            );
        }
        return 0;
    }

    TitanStorageIo_Remove(TITAN_PROFILE_DATA_BACKUP_PATH);
    return 1;
}

TitanProfileStorageSaveResult TitanProfileStorage_PrepareSave(
    TitanProfileStoragePreparedSave *prepared
)
{
    TitanProfileSnapshotResult encode_result;
    TitanUserProfileState state;
    unsigned int next_generation;
    unsigned int revision;

    if (prepared == 0)
    {
        return TITAN_PROFILE_STORAGE_SAVE_ERROR;
    }
    memset(prepared, 0, sizeof(*prepared));

    if (!g_storage_initialized)
    {
        return TITAN_PROFILE_STORAGE_SAVE_NO_STORAGE;
    }

    if (
        g_storage_status == TITAN_PROFILE_STORAGE_CORRUPT ||
        g_storage_status == TITAN_PROFILE_STORAGE_FUTURE_VERSION
    )
    {
        return TITAN_PROFILE_STORAGE_SAVE_PROTECTED;
    }

    TitanProfileStorage_ClearFailure();
    revision = TitanUserProfiles_GetRevision();

    if (
        g_storage_loaded_profiles &&
        revision == g_storage_saved_revision
    )
    {
        g_storage_last_vmc_result = TITAN_VMC_RESULT_OK;
        return TITAN_PROFILE_STORAGE_SAVE_NOT_NEEDED;
    }

    if (!TitanUserProfiles_ExportState(&state))
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_ENCODE,
            TITAN_STORAGE_COMMAND_DECODE,
            -1
        );
        return TITAN_PROFILE_STORAGE_SAVE_ERROR;
    }

    next_generation = g_storage_generation + 1u;
    if (next_generation == 0u)
    {
        next_generation = 1u;
    }

    encode_result = TitanProfileSnapshot_Encode(
        &state,
        next_generation,
        prepared->snapshot,
        sizeof(prepared->snapshot)
    );

    if (encode_result != TITAN_PROFILE_SNAPSHOT_OK)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_ENCODE,
            TITAN_STORAGE_COMMAND_DECODE,
            (int)encode_result
        );
        return TITAN_PROFILE_STORAGE_SAVE_ERROR;
    }

    prepared->generation = next_generation;
    prepared->revision = revision;
    prepared->valid = 1;
    return TITAN_PROFILE_STORAGE_SAVE_OK;
}

TitanProfileStorageSaveResult TitanProfileStorage_CommitPreparedSave(
    const TitanProfileStoragePreparedSave *prepared
)
{
    if (prepared == 0 || !prepared->valid)
    {
        return TITAN_PROFILE_STORAGE_SAVE_ERROR;
    }

    if (!g_storage_initialized)
    {
        return TITAN_PROFILE_STORAGE_SAVE_NO_STORAGE;
    }

    if (
        g_storage_status == TITAN_PROFILE_STORAGE_CORRUPT ||
        g_storage_status == TITAN_PROFILE_STORAGE_FUTURE_VERSION
    )
    {
        return TITAN_PROFILE_STORAGE_SAVE_PROTECTED;
    }

    TitanProfileStorage_ClearFailure();

    if (!TitanProfileStorage_EnsureRoot())
    {
        g_storage_status = TITAN_PROFILE_STORAGE_NO_STORAGE;
        return TITAN_PROFILE_STORAGE_SAVE_NO_STORAGE;
    }

    if (!TitanProfileStorage_CommitSnapshot(
            prepared->snapshot,
            prepared->generation))
    {
        g_storage_status = TITAN_PROFILE_STORAGE_IO_ERROR;
        return TITAN_PROFILE_STORAGE_SAVE_ERROR;
    }

    g_storage_generation = prepared->generation;
    g_storage_saved_revision = prepared->revision;
    g_storage_loaded_profiles = 1;
    g_storage_status = TITAN_PROFILE_STORAGE_READY_LOADED;
    TitanVmc_InvalidateCachedStatus(0);
    g_storage_last_vmc_result = TITAN_VMC_RESULT_OK;
    TitanProfileStorage_ClearFailure();

    return TITAN_PROFILE_STORAGE_SAVE_OK;
}

TitanProfileStorageSaveResult TitanProfileStorage_Save(void)
{
    TitanProfileStoragePreparedSave prepared;
    TitanProfileStorageSaveResult result;

    result = TitanProfileStorage_PrepareSave(&prepared);
    if (result != TITAN_PROFILE_STORAGE_SAVE_OK)
    {
        return result;
    }

    return TitanProfileStorage_CommitPreparedSave(&prepared);
}

TitanProfileStorageSaveResult TitanProfileStorage_FormatAndSave(void)
{
    return TitanProfileStorage_Save();
}

TitanProfileStorageRecoveryResult TitanProfileStorage_RecoverPort(
    int port
)
{
    (void)port;

    return TITAN_PROFILE_STORAGE_RECOVERY_NOT_NEEDED;
}

TitanProfileStorageFormatResult TitanProfileStorage_FormatPort(
    int port
)
{
#if defined(__EE__) || defined(_EE)
    int card_type;
    int free_space;
    int formatted;
    int operation_result;

    if (!g_memory_card_service_ready || port < 0 || port > 1)
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_ARGUMENT,
            TITAN_STORAGE_COMMAND_MEMORY_CARD,
            -1
        );
        return TITAN_PROFILE_STORAGE_FORMAT_ERROR;
    }

    card_type = 0;
    free_space = 0;
    formatted = 0;
    operation_result = 0;

    if (
        !TitanProfileStorage_MemoryCardSync(
            mcGetInfo(
                port,
                0,
                &card_type,
                &free_space,
                &formatted
            ),
            &operation_result
        ) ||
        operation_result == -1
    )
    {
        return TITAN_PROFILE_STORAGE_FORMAT_NO_CARD;
    }

    if (card_type != MC_TYPE_PS2)
    {
        return TITAN_PROFILE_STORAGE_FORMAT_UNSUPPORTED;
    }

    if (
        !TitanProfileStorage_MemoryCardSync(
            mcFormat(port, 0),
            &operation_result
        ) ||
        operation_result < 0
    )
    {
        TitanProfileStorage_RecordFailure(
            TITAN_PROFILE_STORAGE_FAILURE_FORMAT,
            TITAN_STORAGE_COMMAND_MEMORY_CARD,
            operation_result
        );
        return TITAN_PROFILE_STORAGE_FORMAT_ERROR;
    }

    TitanProfileStorage_ClearFailure();
    return TITAN_PROFILE_STORAGE_FORMAT_OK;
#else
    (void)port;
    TitanProfileStorage_RecordFailure(
        TITAN_PROFILE_STORAGE_FAILURE_FORMAT,
        TITAN_STORAGE_COMMAND_MEMORY_CARD,
        -1
    );
    return TITAN_PROFILE_STORAGE_FORMAT_ERROR;
#endif
}

int TitanProfileStorage_GetPort(void)
{
    return -1;
}

int TitanProfileStorage_HasLoadedProfiles(void)
{
    return g_storage_loaded_profiles;
}

int TitanProfileStorage_IsDirty(void)
{
    return
        !g_storage_loaded_profiles ||
        TitanUserProfiles_GetRevision() != g_storage_saved_revision;
}

unsigned int TitanProfileStorage_GetGeneration(void)
{
    return g_storage_generation;
}

TitanVmcResult TitanProfileStorage_GetLastVmcResult(void)
{
    return g_storage_last_vmc_result;
}

TitanProfileStorageFailureStage
TitanProfileStorage_GetLastFailureStage(void)
{
    return g_storage_last_failure_stage;
}

const char *TitanProfileStorage_GetLastFailureStageName(void)
{
    switch (g_storage_last_failure_stage)
    {
        case TITAN_PROFILE_STORAGE_FAILURE_ARGUMENT:
            return "argument";

        case TITAN_PROFILE_STORAGE_FAILURE_USB_MODULE:
            return "usb module";

        case TITAN_PROFILE_STORAGE_FAILURE_STORAGE_MOUNT:
            return "storage mount";

        case TITAN_PROFILE_STORAGE_FAILURE_DIRECTORY_CREATE:
            return "directory";

        case TITAN_PROFILE_STORAGE_FAILURE_WRITE_OPEN:
            return "write open";

        case TITAN_PROFILE_STORAGE_FAILURE_WRITE_DATA:
            return "write data";

        case TITAN_PROFILE_STORAGE_FAILURE_WRITE_CLOSE:
            return "write close";

        case TITAN_PROFILE_STORAGE_FAILURE_READ_OPEN:
            return "read open";

        case TITAN_PROFILE_STORAGE_FAILURE_READ_DATA:
            return "read data";

        case TITAN_PROFILE_STORAGE_FAILURE_READ_CLOSE:
            return "read close";

        case TITAN_PROFILE_STORAGE_FAILURE_VERIFY_CONTENT:
            return "verify data";

        case TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_ENCODE:
            return "encode";

        case TITAN_PROFILE_STORAGE_FAILURE_SNAPSHOT_VERIFY:
            return "decode";

        case TITAN_PROFILE_STORAGE_FAILURE_FORMAT:
            return "format";

        case TITAN_PROFILE_STORAGE_FAILURE_FORMAT_VERIFY:
            return "format verify";

        case TITAN_PROFILE_STORAGE_FAILURE_NONE:
        default:
            return "none";
    }
}

int TitanProfileStorage_GetLastFailureCommand(void)
{
    return g_storage_last_failure_command;
}

int TitanProfileStorage_GetLastFailureResult(void)
{
    return g_storage_last_failure_result;
}
