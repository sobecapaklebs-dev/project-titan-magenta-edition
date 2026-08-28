#include "titan_installer.h"
#include "titan_installer_model.h"
#include "titan_install_container.h"
#include "titan_installer_diag.h"
#include "titan_fmcb_backend.h"

#include "titan_font.h"
#include "titan_input.h"
#include "titan_platform.h"
#include "titan_video.h"
#include "titan_shell_background.h"
#include "titan_theme.h"
#include "titan_video_output.h"
#include "titan_time.h"

#include "generated/titan_installer_fat_data.h"
#include "generated/titan_installer_slim70_data.h"
#include "generated/titan_installer_slim90_data.h"
#include "titan_iop_modules.h"

#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

/* v2.0.14 uses direct FileXio with the IOP/FIO flag domain documented by
 * PS2SDK. BuildFix3 applies the same domain conversion to direct libmc mcOpen():
 * libmc forwards the mode unchanged to MCSERV/MCMAN, and real hardware showed
 * that newlib O_RDONLY=0 opened the staging file without read permission, making
 * the first mcRead() fail with sceMcResDeniedPermit (-5). Data RPCs remain
 * asynchronous and are polled between rendered frames. */
#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE
#define TITAN_INSTALLER_DEFINED_NEWLIB_PORT_AWARE
#endif
/* io_common.h itself enforces NEWLIB_PORT_AWARE on newlib builds, so it must
 * be included inside the same explicit legacy-RPC opt-in window as fileXio. */
#include <io_common.h>
#include <fileXio_rpc.h>
#ifdef TITAN_INSTALLER_DEFINED_NEWLIB_PORT_AWARE
#undef TITAN_INSTALLER_DEFINED_NEWLIB_PORT_AWARE
#undef NEWLIB_PORT_AWARE
#endif

#include <delaythread.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libmc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TITAN_INSTALLER_VERSION "0.2.15-bf30-fmcb-integrated"
#define TITAN_INSTALLER_DIR_REL "TITAN_INSTALL"
#define TITAN_INSTALLER_CONTAINER_REL "TITAN_INSTALL/TITAN_SYSTEM.TINS"
#define TITAN_INSTALLER_CONSUMED_REL "TITAN_INSTALL/INSTALLER.CONSUMED"
#define TITAN_INSTALLER_COMPLETE_REL "TITAN/INSTALL.COMPLETE"
#define TITAN_INSTALLER_FMCB_ROOT_REL "TITAN_INSTALL/FMCB"
#define TITAN_INSTALLER_FMCB_SYSTEM_REL "TITAN_INSTALL/FMCB/INSTALL/SYSTEM/FMCB.XLF"
#define TITAN_INSTALLER_FMCB_CNF_REL "TITAN_INSTALL/FMCB/INSTALL/SYS-CONF/FREEMCB.CNF"
#define TITAN_INSTALLER_FMCB_JOURNAL_REL "TITAN_INSTALL/FMCB/TITAN_TXN.JOURNAL"
#define TITAN_INSTALLER_FMCB_SPACE_REL "TITAN_INSTALL/FMCB/TITAN_SPACE.REQ"
#define TITAN_INSTALLER_USB_PROBE_ATTEMPTS 120u
#define TITAN_INSTALLER_USB_PROBE_DELAY_US 100000u
#define TITAN_INSTALLER_MAX_ENTRIES 192u
#define TITAN_INSTALLER_PATH_CAP 256u
#define TITAN_INSTALLER_COPY_BUFFER (32u * 1024u)
#define TITAN_INSTALLER_IO_SLICE TITAN_INSTALLER_COPY_BUFFER
#define TITAN_INSTALLER_IO_FRAME_BUDGET (64u * 1024u)
#define TITAN_INSTALLER_IO_FRAME_BUDGET_MS 4u
#define TITAN_INSTALLER_META_STEPS_PER_FRAME 6u
/* POSIX adaptive slices remain only as a safe output-open fallback. Normal
 * mass: payload I/O is FileXio NOWAIT; normal Memory Card payload I/O is libmc
 * async. The tighter metadata budget prevents a burst of synchronous stat/mkdir
 * probes from consuming a complete 60 Hz render interval. */
#define TITAN_INSTALLER_IO_TARGET_MS 8u
#define TITAN_INSTALLER_USB_READ_MIN 1024u
#define TITAN_INSTALLER_USB_READ_MAX 8192u
#define TITAN_INSTALLER_USB_READ_START 4096u
#define TITAN_INSTALLER_USB_WRITE_MIN 512u
#define TITAN_INSTALLER_USB_WRITE_MAX 8192u
#define TITAN_INSTALLER_USB_WRITE_START 4096u
#define TITAN_INSTALLER_MC_READ_MIN 512u
#define TITAN_INSTALLER_MC_READ_MAX 4096u
#define TITAN_INSTALLER_MC_READ_START 2048u
#define TITAN_INSTALLER_MC_WRITE_MIN 256u
#define TITAN_INSTALLER_MC_WRITE_MAX 2048u
#define TITAN_INSTALLER_MC_WRITE_START 512u
#define TITAN_INSTALLER_BACKUP_PROBE_ATTEMPTS 8u
#define TITAN_INSTALLER_FULL_VERIFY_LIMIT (4u * 1024u * 1024u)
#define TITAN_INSTALLER_BACKUP_SLOTS 64u
#define TITAN_INSTALLER_MC_DIR_ENTRIES 128u
#define TITAN_INSTALLER_VIDEO_TEST_FRAMES 360u
#define TITAN_INSTALLER_PAGE_TRANSITION_FRAMES 10u
#define TITAN_INSTALLER_PAGE_TRANSITION_OFFSET_X 42.0f
#define TITAN_INSTALLER_HERO_CYCLE_FRAMES 150u
#define TITAN_INSTALLER_HERO_FADE_FRAMES 18u
#define TITAN_INSTALLER_HERO_SOURCE_SIZE 256u
#define TITAN_INSTALLER_HERO_ATLAS_WIDTH TITAN_VIDEO_UI_ATLAS_WIDTH
#define TITAN_INSTALLER_HERO_ATLAS_HEIGHT TITAN_VIDEO_UI_ATLAS_HEIGHT

/* Canonical user-approved VlombisCore notice. Keep wording and final slogan. */
static const char *g_vlombis_terms[] = {
    "⚠ AVISO SOBRE A TECNOLOGIA VLOMBISCORE™",
    "O Project Titan: Magenta Edition utiliza VlombisCore™ Technology, uma tecnologia avançada projetada para explorar capacidades anteriormente desconhecidas do PlayStation®2.",
    "Durante a utilização, a Emotion Engine™ poderá apresentar níveis de emoção superiores aos especificados originalmente pelo fabricante.",
    "Possíveis sintomas incluem:",
    "• aumento espontâneo de performance;",
    "• processamento excessivamente sentimental;",
    "• apego emocional ao I/O Processor;",
    "• execução de instruções motivada por saudade;",
    "• crises existenciais durante operações de DMA;",
    "• recusa ocasional em executar código por motivos pessoais.",
    "Caso a Emotion Engine demonstre emoções intensas, não desligue o console imediatamente. Aguarde até que o processador consiga organizar seus sentimentos.",
    "A equipe Project Titan não se responsabiliza por discussões entre a Emotion Engine e o Graphics Synthesizer.",
    "VlombisCore™ Technology",
    "Feel the processing."
};

typedef enum TitanInstallerPage
{
    TITAN_INSTALLER_PAGE_VIDEO_OUTPUT = 0,
    TITAN_INSTALLER_PAGE_CONTROLLER,
    TITAN_INSTALLER_PAGE_LANGUAGE,
    TITAN_INSTALLER_PAGE_WELCOME,
    TITAN_INSTALLER_PAGE_PREFLIGHT,
    TITAN_INSTALLER_PAGE_METHOD,
    TITAN_INSTALLER_PAGE_MEMORY_CARD,
    TITAN_INSTALLER_PAGE_BOOT_ANALYSIS,
    TITAN_INSTALLER_PAGE_DESTINATION,
    TITAN_INSTALLER_PAGE_TERMS,
    TITAN_INSTALLER_PAGE_SUMMARY,
    TITAN_INSTALLER_PAGE_INSTALLING,
    TITAN_INSTALLER_PAGE_COMPLETE,
    TITAN_INSTALLER_PAGE_ERROR
} TitanInstallerPage;

typedef enum TitanInstallerMethod
{
    TITAN_INSTALLER_METHOD_SEMI_AUTONOMOUS = 0,
    TITAN_INSTALLER_METHOD_AUTONOMOUS = 1
} TitanInstallerMethod;

typedef enum TitanInstallerFmcbState
{
    TITAN_FMCB_NO_FMCB = 0,
    TITAN_FMCB_COMPATIBLE_FMCB,
    TITAN_FMCB_UNKNOWN_FMCB,
    TITAN_FMCB_CORRUPT_FMCB,
    TITAN_FMCB_TITAN_MANAGED_FMCB
} TitanInstallerFmcbState;

typedef struct TitanInstallerManifestEntry
{
    char kind[12];
    char path[192];
    unsigned int payload_offset;
    unsigned int size;
    unsigned int crc32;
    int had_existing;
    int published;
    int unchanged;
    int backup_transport_ready; /* BF5: complete USB copy of prior MC destination */
    int backup_slot; /* -2 = canonical .TITAN.BAK, -1 = none, >=0 = .TITAN.BKxx */
} TitanInstallerManifestEntry;

typedef enum TitanInstallerInstallPhase
{
    TITAN_INSTALL_JOB_IDLE = 0,
    TITAN_INSTALL_JOB_RECOVER,
    TITAN_INSTALL_JOB_OPEN_CONTAINER,
    TITAN_INSTALL_JOB_SKIP_PREFIX,
    TITAN_INSTALL_JOB_PREPARE_ENTRY,
    TITAN_INSTALL_JOB_SKIP_GAP,
    TITAN_INSTALL_JOB_SKIP_ENTRY,
    TITAN_INSTALL_JOB_PREPARE_STAGE,
    TITAN_INSTALL_JOB_CHECK_EXISTING_READ,
    TITAN_INSTALL_JOB_CHECK_EXISTING_EOF,
    TITAN_INSTALL_JOB_READ_SOURCE,
    TITAN_INSTALL_JOB_WRITE_STAGE,
    TITAN_INSTALL_JOB_VERIFY_OPEN,
    TITAN_INSTALL_JOB_VERIFY_READ,
    TITAN_INSTALL_JOB_VERIFY_EOF,
    TITAN_INSTALL_JOB_PUBLISH_ENTRY,
    TITAN_INSTALL_JOB_BACKUP_COPY_OPEN,
    TITAN_INSTALL_JOB_BACKUP_COPY_READ,
    TITAN_INSTALL_JOB_BACKUP_COPY_WRITE,
    TITAN_INSTALL_JOB_BACKUP_COPY_FINISH,
    TITAN_INSTALL_JOB_PUBLISH_STAGING,
    TITAN_INSTALL_JOB_PUBLISH_COPY_OPEN,
    TITAN_INSTALL_JOB_PUBLISH_COPY_READ,
    TITAN_INSTALL_JOB_PUBLISH_COPY_WRITE,
    TITAN_INSTALL_JOB_PUBLISH_COPY_FINISH,
    TITAN_INSTALL_JOB_ROLLBACK_RESTORE_OPEN,
    TITAN_INSTALL_JOB_ROLLBACK_RESTORE_READ,
    TITAN_INSTALL_JOB_ROLLBACK_RESTORE_WRITE,
    TITAN_INSTALL_JOB_ROLLBACK_RESTORE_FINISH,
    TITAN_INSTALL_JOB_FMCB_INTEGRATED,
    TITAN_INSTALL_JOB_WRITE_COMPLETE,
    TITAN_INSTALL_JOB_CLEAN_BACKUPS,
    TITAN_INSTALL_JOB_ROLLBACK,
    TITAN_INSTALL_JOB_SUCCESS,
    TITAN_INSTALL_JOB_FAILED
} TitanInstallerInstallPhase;

static const char *install_phase_name(TitanInstallerInstallPhase phase)
{
    switch (phase) {
        case TITAN_INSTALL_JOB_IDLE: return "IDLE";
        case TITAN_INSTALL_JOB_RECOVER: return "RECOVER";
        case TITAN_INSTALL_JOB_OPEN_CONTAINER: return "OPEN_CONTAINER";
        case TITAN_INSTALL_JOB_SKIP_PREFIX: return "SKIP_PREFIX";
        case TITAN_INSTALL_JOB_PREPARE_ENTRY: return "PREPARE_ENTRY";
        case TITAN_INSTALL_JOB_SKIP_GAP: return "SKIP_GAP";
        case TITAN_INSTALL_JOB_SKIP_ENTRY: return "SKIP_ENTRY";
        case TITAN_INSTALL_JOB_PREPARE_STAGE: return "PREPARE_STAGE";
        case TITAN_INSTALL_JOB_CHECK_EXISTING_READ: return "CHECK_EXISTING_READ";
        case TITAN_INSTALL_JOB_CHECK_EXISTING_EOF: return "CHECK_EXISTING_EOF";
        case TITAN_INSTALL_JOB_READ_SOURCE: return "READ_SOURCE";
        case TITAN_INSTALL_JOB_WRITE_STAGE: return "WRITE_STAGE";
        case TITAN_INSTALL_JOB_VERIFY_OPEN: return "VERIFY_OPEN";
        case TITAN_INSTALL_JOB_VERIFY_READ: return "VERIFY_READ";
        case TITAN_INSTALL_JOB_VERIFY_EOF: return "VERIFY_EOF";
        case TITAN_INSTALL_JOB_PUBLISH_ENTRY: return "PUBLISH_ENTRY";
        case TITAN_INSTALL_JOB_BACKUP_COPY_OPEN: return "BACKUP_COPY_OPEN";
        case TITAN_INSTALL_JOB_BACKUP_COPY_READ: return "BACKUP_COPY_READ";
        case TITAN_INSTALL_JOB_BACKUP_COPY_WRITE: return "BACKUP_COPY_WRITE";
        case TITAN_INSTALL_JOB_BACKUP_COPY_FINISH: return "BACKUP_COPY_FINISH";
        case TITAN_INSTALL_JOB_PUBLISH_STAGING: return "PUBLISH_STAGING";
        case TITAN_INSTALL_JOB_PUBLISH_COPY_OPEN: return "PUBLISH_COPY_OPEN";
        case TITAN_INSTALL_JOB_PUBLISH_COPY_READ: return "PUBLISH_COPY_READ";
        case TITAN_INSTALL_JOB_PUBLISH_COPY_WRITE: return "PUBLISH_COPY_WRITE";
        case TITAN_INSTALL_JOB_PUBLISH_COPY_FINISH: return "PUBLISH_COPY_FINISH";
        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_OPEN: return "ROLLBACK_RESTORE_OPEN";
        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_READ: return "ROLLBACK_RESTORE_READ";
        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_WRITE: return "ROLLBACK_RESTORE_WRITE";
        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_FINISH: return "ROLLBACK_RESTORE_FINISH";
        case TITAN_INSTALL_JOB_FMCB_INTEGRATED: return "FMCB_INTEGRATED";
        case TITAN_INSTALL_JOB_WRITE_COMPLETE: return "WRITE_COMPLETE";
        case TITAN_INSTALL_JOB_CLEAN_BACKUPS: return "CLEAN_BACKUPS";
        case TITAN_INSTALL_JOB_ROLLBACK: return "ROLLBACK";
        case TITAN_INSTALL_JOB_SUCCESS: return "SUCCESS";
        case TITAN_INSTALL_JOB_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

typedef enum TitanInstallerAsyncIoKind
{
    TITAN_INSTALLER_ASYNC_NONE = 0,
    TITAN_INSTALLER_ASYNC_OPEN,
    TITAN_INSTALLER_ASYNC_READ,
    TITAN_INSTALLER_ASYNC_WRITE,
    TITAN_INSTALLER_ASYNC_CLOSE,
    TITAN_INSTALLER_ASYNC_REMOVE,
    TITAN_INSTALLER_ASYNC_RENAME
} TitanInstallerAsyncIoKind;

typedef enum TitanInstallerAsyncBackend
{
    TITAN_INSTALLER_ASYNC_BACKEND_NONE = 0,
    TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
    TITAN_INSTALLER_ASYNC_BACKEND_LIBMC
} TitanInstallerAsyncBackend;

typedef enum TitanInstallerFdBackend
{
    TITAN_INSTALLER_FD_BACKEND_NONE = 0,
    TITAN_INSTALLER_FD_BACKEND_POSIX,
    TITAN_INSTALLER_FD_BACKEND_FILEXIO,
    TITAN_INSTALLER_FD_BACKEND_LIBMC
} TitanInstallerFdBackend;

typedef struct TitanInstallerAsyncIo
{
    TitanInstallerAsyncIoKind kind;
    TitanInstallerAsyncBackend backend;
    uint64_t started_ms;
    unsigned int requested;
    char operation[40];
    char path[TITAN_INSTALLER_PATH_CAP];
    char path2[TITAN_INSTALLER_PATH_CAP];
} TitanInstallerAsyncIo;

typedef struct TitanInstallerInstallJob
{
    TitanInstallerInstallPhase phase;
    int container_fd;
    int stage_fd;
    TitanInstallerFdBackend stage_backend;
    int verify_fd;
    unsigned int entry_index;
    unsigned int recovery_index;
    unsigned int publish_index;
    unsigned int cleanup_index;
    unsigned int rollback_index;
    unsigned int cursor;
    unsigned int remaining;
    unsigned int pending_size;
    unsigned int pending_written;
    unsigned int source_crc;
    unsigned int verify_crc;
    unsigned int verify_size;
    unsigned int existing_crc;
    unsigned int existing_size;
    int last_posix_errno;
    int last_filexio_result;
    unsigned long long work_total;
    unsigned long long work_done;
    int cancel_requested;
    char destination[TITAN_INSTALLER_PATH_CAP];
    char temporary[TITAN_INSTALLER_PATH_CAP];
    char backup[TITAN_INSTALLER_PATH_CAP];
    char backup_transport[TITAN_INSTALLER_PATH_CAP];
    char failure[192];
    TitanInstallerAsyncIo async_io;
    int mc_dir_pending;
    int mc_dir_result;
    unsigned int prepare_stage_step;
    unsigned int recovery_step;
    unsigned int cleanup_step;
    unsigned int rollback_step;
    unsigned int close_step;
    unsigned int open_step;
    unsigned int usb_read_slice;
    unsigned int usb_write_slice;
    unsigned int mc_read_slice;
    unsigned int mc_write_slice;
    unsigned int backup_slot_seed;
    unsigned int backup_transport_phase;
    int current_entry_touched;
    int eof_result;
} TitanInstallerInstallJob;

typedef struct TitanInstallerState
{
    TitanInstallerPage page;
    TitanInstallerConsoleInfo console;
    TitanInstallerMethod method;
    int language;
    int memory_card;
    int autonomous_available;
    TitanInstallerFmcbState fmcb_state;
    int fmcb_integrated_started;
    int fmcb_integrated_result;
    int fmcb_iop_restored;
    int storage_ready;
    int tins_preflight_attempted;
    int tins_preflight_ok;
    int usb_container_probe;
    int usb_probe_errno;
    int usb_filexio_client_result;
    int usb_inherited_iop;
    int usb_iomanx_id;
    int usb_iomanx_result;
    int usb_filexio_id;
    int usb_filexio_result;
    int usb_usbd_id;
    int usb_usbd_result;
    int usb_usbhdfsd_id;
    int usb_usbhdfsd_result;
    int mc0_ready;
    int mc1_ready;
    int existing_boot;
    int terms_scroll;
    int selection;
    int video_test_active;
    unsigned int video_test_frames_remaining;
    int video_test_previous_progressive;
    char video_status[128];
    unsigned int progress;
    unsigned int manifest_count;
    unsigned int container_payload_base;
    unsigned int container_size;
    unsigned int container_flags;
    TitanInstallerManifestEntry manifest[TITAN_INSTALLER_MAX_ENTRIES];
    char status[160];
    char error[192];
    char executable_path[TITAN_INSTALLER_PATH_CAP];
    char usb_prefix[16];
    char installer_root[TITAN_INSTALLER_PATH_CAP];
    char container_path[TITAN_INSTALLER_PATH_CAP];
    char consumed_path[TITAN_INSTALLER_PATH_CAP];
    char complete_path[TITAN_INSTALLER_PATH_CAP];
    char fmcb_root_path[TITAN_INSTALLER_PATH_CAP];
    char fmcb_system_path[TITAN_INSTALLER_PATH_CAP];
    char fmcb_cnf_path[TITAN_INSTALLER_PATH_CAP];
    char fmcb_journal_path[TITAN_INSTALLER_PATH_CAP];
    char fmcb_space_path[TITAN_INSTALLER_PATH_CAP];
    unsigned int frame;
    int last_rendered_page;
    unsigned int page_transition_frame;
    TitanInstallerInstallJob install_job;
    int hero_atlas_ready;
    int hero_atlas_family_a;
    int hero_atlas_family_b;
} TitanInstallerState;

static unsigned char g_copy_buffer[TITAN_INSTALLER_COPY_BUFFER] __attribute__((aligned(64)));
static sceMcTblGetDir g_mc_boot_dir[TITAN_INSTALLER_MC_DIR_ENTRIES] __attribute__((aligned(64)));
static unsigned int g_installer_hero_atlas[TITAN_INSTALLER_HERO_ATLAS_WIDTH * TITAN_INSTALLER_HERO_ATLAS_HEIGHT] __attribute__((aligned(64)));
static int g_installer_hero_atlas_built;

static void render(TitanInstallerState *s);

static int path_exists(const char *path)
{
    struct stat st;
    uint64_t start_ms;
    int result;
    int saved_errno;
    if (path == 0) return 0;
    errno = 0;
    start_ms = TitanTime_NowMilliseconds();
    result = stat(path, &st);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        "STAT",
        path,
        0u,
        result,
        saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result == 0;
}

static int installer_diag_open(const char *path, int flags, int mode, const char *operation)
{
    uint64_t start_ms = TitanTime_NowMilliseconds();
    int result;
    int saved_errno;
    errno = 0;
    result = open(path, flags, mode);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        operation ? operation : "OPEN",
        path,
        0u,
        result,
        saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result;
}

static int installer_diag_read(int fd, void *buffer, unsigned int size, const char *path, const char *operation)
{
    uint64_t start_ms = TitanTime_NowMilliseconds();
    int result;
    int saved_errno;
    errno = 0;
    result = read(fd, buffer, size);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        operation ? operation : "READ",
        path,
        size,
        result,
        saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result;
}

static int installer_diag_write(int fd, const void *buffer, unsigned int size, const char *path, const char *operation)
{
    uint64_t start_ms = TitanTime_NowMilliseconds();
    int result;
    int saved_errno;
    errno = 0;
    result = write(fd, buffer, size);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        operation ? operation : "WRITE",
        path,
        size,
        result,
        saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result;
}

static int installer_diag_mkdir(const char *path, int mode)
{
    uint64_t start_ms = TitanTime_NowMilliseconds();
    int result;
    int saved_errno;
    errno = 0;
    result = mkdir(path, mode);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        "MKDIR_OPTIONAL", path, 0u, result, saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result;
}

static void copy_string(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0u;
    if (dst == 0 || cap == 0u) return;
    if (src == 0) src = "";
    while (i + 1u < cap && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int has_text(const char *value, const char *needle)
{
    return value != 0 && needle != 0 && strstr(value, needle) != 0;
}

int TitanInstaller_ShouldRun(int argc, char *argv[])
{
    int i;
    if (argc > 0 && argv != 0 && argv[0] != 0 &&
        (has_text(argv[0], "titan-installer") || has_text(argv[0], "TITAN-INSTALLER")))
        return 1;
    for (i = 1; i < argc; i++)
        if (argv[i] != 0 && strcmp(argv[i], "--installer") == 0) return 1;
    return 0;
}

static int exec_module_checked(
    void *module,
    unsigned int size,
    int *id_out,
    int *result_out
)
{
    int result = -1;
    int id = SifExecModuleBuffer(module, size, 0, 0, &result);

    if (id_out != 0) *id_out = id;
    if (result_out != 0) *result_out = result;

    /* SifExecModuleBuffer has two independent results: LOADFILE must have
     * accepted the module and the module's _start must also have succeeded.
     * Keep this helper strict for diagnostics. The storage bootstrap itself
     * intentionally does not short-circuit on an individual module result,
     * because a resident/duplicate service can report an error while mass:/
     * is nevertheless usable after the complete chain is attempted. */
    return id >= 0 && result >= 0;
}

static int extract_mass_prefix(const char *path, char *prefix, unsigned int cap)
{
    const char *colon;
    const char *p;
    unsigned int length;
    if (path == 0 || prefix == 0 || cap < 6u || strncmp(path, "mass", 4u) != 0) return 0;
    colon = strchr(path, ':');
    if (colon == 0) return 0;
    for (p = path + 4; p < colon; p++)
        if (*p < '0' || *p > '9') return 0;
    length = (unsigned int)(colon - path + 1);
    if (length + 1u > cap) return 0;
    memcpy(prefix, path, length);
    prefix[length] = '\0';
    return 1;
}

static void build_usb_path(const char *prefix, const char *relative, char *out, unsigned int cap)
{
    if (out == 0 || cap == 0u) return;
    if (prefix == 0) prefix = "mass:";
    if (relative == 0) relative = "";
    while (*relative == '/') relative++;
    snprintf(out, cap, "%s/%s", prefix, relative);
}

static void set_usb_prefix(TitanInstallerState *s, const char *prefix)
{
    if (s == 0 || prefix == 0) return;
    copy_string(s->usb_prefix, sizeof(s->usb_prefix), prefix);
    build_usb_path(prefix, TITAN_INSTALLER_DIR_REL, s->installer_root, sizeof(s->installer_root));
    build_usb_path(prefix, TITAN_INSTALLER_CONTAINER_REL, s->container_path, sizeof(s->container_path));
    build_usb_path(prefix, TITAN_INSTALLER_CONSUMED_REL, s->consumed_path, sizeof(s->consumed_path));
    build_usb_path(prefix, TITAN_INSTALLER_COMPLETE_REL, s->complete_path, sizeof(s->complete_path));
    build_usb_path(prefix, TITAN_INSTALLER_FMCB_ROOT_REL, s->fmcb_root_path, sizeof(s->fmcb_root_path));
    build_usb_path(prefix, TITAN_INSTALLER_FMCB_SYSTEM_REL, s->fmcb_system_path, sizeof(s->fmcb_system_path));
    build_usb_path(prefix, TITAN_INSTALLER_FMCB_CNF_REL, s->fmcb_cnf_path, sizeof(s->fmcb_cnf_path));
    build_usb_path(prefix, TITAN_INSTALLER_FMCB_JOURNAL_REL, s->fmcb_journal_path, sizeof(s->fmcb_journal_path));
    build_usb_path(prefix, TITAN_INSTALLER_FMCB_SPACE_REL, s->fmcb_space_path, sizeof(s->fmcb_space_path));
}

static int probe_usb_prefix(TitanInstallerState *s, const char *prefix)
{
    char container_path[TITAN_INSTALLER_PATH_CAP];
    char root_path[24];
    unsigned char magic[4];
    int fd;
    int got;
    int saved_errno;

    build_usb_path(prefix, TITAN_INSTALLER_CONTAINER_REL, container_path, sizeof(container_path));

    /* The authoritative probe is the file the installer actually needs.
     * This avoids depending on directory-stat behaviour that differs between
     * USB mass implementations. If it opens, the USB storage is usable even
     * when the TINS header later proves invalid. */
    fd = open(container_path, O_RDONLY);
    if (fd >= 0) {
        got = read(fd, magic, sizeof(magic));
        saved_errno = got < 0 ? errno : 0;
        close(fd);
        set_usb_prefix(s, prefix);
        s->usb_probe_errno = saved_errno;
        s->usb_container_probe =
            got == (int)sizeof(magic) && memcmp(magic, TITAN_TINS_MAGIC, sizeof(magic)) == 0
                ? 1
                : -1;
        return 2;
    }

    saved_errno = errno;

    /* Secondary probe keeps "USB storage" separate from "TINS present".
     * A mounted device with a missing/corrupt container must report USB OK
     * and a TINS error, not collapse both conditions into one message. */
    snprintf(root_path, sizeof(root_path), "%s/", prefix);
    if (path_exists(root_path)) {
        if (s->usb_prefix[0] == '\0') set_usb_prefix(s, prefix);
        s->usb_probe_errno = saved_errno;
        return 1;
    }

    s->usb_probe_errno = saved_errno;
    return 0;
}

static int probe_inherited_usb_once(TitanInstallerState *s)
{
    char preferred[16];
    unsigned int index;
    int probe_result;
    static const char *const fallback[] = {
        "mass:", "mass0:", "mass1:", "mass2:", "mass3:",
        "mass4:", "mass5:", "mass6:", "mass7:", "mass8:", "mass9:"
    };

    if (s == 0) return 0;

    preferred[0] = '\0';
    s->usb_prefix[0] = '\0';
    s->installer_root[0] = '\0';
    s->container_path[0] = '\0';
    s->consumed_path[0] = '\0';
    s->complete_path[0] = '\0';
    s->usb_container_probe = 0;
    s->usb_probe_errno = 0;
    (void)extract_mass_prefix(s->executable_path, preferred, sizeof(preferred));

    if (preferred[0] != '\0') {
        probe_result = probe_usb_prefix(s, preferred);
        if (probe_result != 0) return 1;
    }

    for (index = 0u; index < (unsigned int)(sizeof(fallback) / sizeof(fallback[0])); index++) {
        if (preferred[0] != '\0' && strcmp(preferred, fallback[index]) == 0) continue;
        probe_result = probe_usb_prefix(s, fallback[index]);
        if (probe_result != 0) return 1;
    }

    return 0;
}

static int adopt_launcher_storage(TitanInstallerState *s)
{
    if (s == 0) return 0;

    /* An ExecPS2-launched ELF has fresh EE-side RPC clients while the IOP image
     * can remain alive when the launcher did not request an IOP reboot. Rebind to
     * launcher's FILEXIO service first and test the actual installer source.
     * If this works, preserving that IOP image is strictly safer than tearing
     * down a USB stack that has already demonstrated hardware compatibility. */
    SifInitRpc(0);
    (void)SifLoadFileInit();
    s->usb_filexio_client_result = fileXioInit();

    if (!probe_inherited_usb_once(s)) {
        (void)fileXioExit();
        (void)SifLoadFileExit();
        return 0;
    }

    s->usb_inherited_iop = 1;
    printf(
        "[TitanInstaller] preserving launcher IOP; USB source is %s (fileXioInit=%d)\n",
        s->usb_prefix,
        s->usb_filexio_client_result
    );
    return 1;
}

static int probe_usb_mount(TitanInstallerState *s)
{
    char preferred[16];
    unsigned int attempt;
    unsigned int index;
    int mounted_seen = 0;
    static const char *const fallback[] = {
        "mass:", "mass0:", "mass1:", "mass2:", "mass3:",
        "mass4:", "mass5:", "mass6:", "mass7:", "mass8:", "mass9:"
    };

    if (s == 0) return 0;
    preferred[0] = '\0';
    s->usb_prefix[0] = '\0';
    s->installer_root[0] = '\0';
    s->container_path[0] = '\0';
    s->consumed_path[0] = '\0';
    s->complete_path[0] = '\0';
    s->usb_container_probe = 0;
    s->usb_probe_errno = 0;
    (void)extract_mass_prefix(s->executable_path, preferred, sizeof(preferred));

    for (attempt = 0u; attempt < TITAN_INSTALLER_USB_PROBE_ATTEMPTS; attempt++) {
        int probe_result;
        if (preferred[0] != '\0') {
            probe_result = probe_usb_prefix(s, preferred);
            if (probe_result == 2) return 1;
            if (probe_result == 1) mounted_seen = 1;
        }
        for (index = 0u; index < (unsigned int)(sizeof(fallback) / sizeof(fallback[0])); index++) {
            if (preferred[0] != '\0' && strcmp(preferred, fallback[index]) == 0) continue;
            probe_result = probe_usb_prefix(s, fallback[index]);
            if (probe_result == 2) return 1;
            if (probe_result == 1) mounted_seen = 1;
        }
        if (attempt + 1u < TITAN_INSTALLER_USB_PROBE_ATTEMPTS)
            DelayThread(TITAN_INSTALLER_USB_PROBE_DELAY_US);
    }

    return mounted_seen;
}

static void initialize_memory_card_services(int extended_mc)
{
    const char *mcman = extended_mc ? "rom0:XMCMAN" : "rom0:MCMAN";
    const char *mcserv = extended_mc ? "rom0:XMCSERV" : "rom0:MCSERV";
    int mc_type = extended_mc ? MC_TYPE_XMC : MC_TYPE_MC;

    /* Duplicate/resident ROM modules may return an error here. mcInit() is
     * authoritative and will bind to the already-resident service when the
     * launcher supplied it. */
    (void)SifLoadModule(mcman, 0, 0);
    (void)SifLoadModule(mcserv, 0, 0);
    (void)mcInit(mc_type);
}

static int initialize_storage(TitanInstallerState *s, int extended_mc)
{
    if (s == 0) return 0;

    s->usb_iomanx_id = -1;
    s->usb_iomanx_result = -1;
    s->usb_filexio_id = -1;
    s->usb_filexio_result = -1;
    s->usb_usbd_id = -1;
    s->usb_usbd_result = -1;
    s->usb_usbhdfsd_id = -1;
    s->usb_usbhdfsd_result = -1;
    s->usb_filexio_client_result = -1;

    sbv_patch_enable_lmb();

    /* Match the hardened Titan storage bootstrap: never short-circuit this
     * chain. A module can report duplicate/resident/startup failure while the
     * remaining services still need to be attempted. Actual mass:/ access is
     * the authority, not one isolated module return code. */
    (void)exec_module_checked(
        titan_iomanx_irx,
        size_titan_iomanx_irx,
        &s->usb_iomanx_id,
        &s->usb_iomanx_result
    );
    (void)exec_module_checked(
        titan_filexio_irx,
        size_titan_filexio_irx,
        &s->usb_filexio_id,
        &s->usb_filexio_result
    );
    (void)exec_module_checked(
        titan_usbd_irx,
        size_titan_usbd_irx,
        &s->usb_usbd_id,
        &s->usb_usbd_result
    );
    (void)exec_module_checked(
        titan_usbhdfsd_irx,
        size_titan_usbhdfsd_irx,
        &s->usb_usbhdfsd_id,
        &s->usb_usbhdfsd_result
    );

    /* fileXioInit() can legitimately report an already-bound client. Keep
     * the return code for diagnostics but continue to the real POSIX probe. */
    s->usb_filexio_client_result = fileXioInit();

    initialize_memory_card_services(extended_mc);

    printf(
        "[TitanInstaller] USB bootstrap: IOMANX=%d/%d FILEXIO=%d/%d "
        "USBD=%d/%d USBHDFSD=%d/%d fileXioInit=%d\n",
        s->usb_iomanx_id,
        s->usb_iomanx_result,
        s->usb_filexio_id,
        s->usb_filexio_result,
        s->usb_usbd_id,
        s->usb_usbd_result,
        s->usb_usbhdfsd_id,
        s->usb_usbhdfsd_result,
        s->usb_filexio_client_result
    );

    /* TitanInput intentionally reset the IOP. Re-enumeration is asynchronous,
     * so poll all plausible mass aliases. A direct TINS open is preferred;
     * root availability is only the fallback used to distinguish a missing
     * container from a missing USB device. */
    return probe_usb_mount(s);
}

static unsigned int crc32_update(unsigned int crc, const unsigned char *data, unsigned int length)
{
    unsigned int i, bit;
    crc = ~crc;
    for (i = 0u; i < length; i++) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static unsigned int installer_diag_crc32_update(
    unsigned int crc,
    const unsigned char *data,
    unsigned int length,
    const char *operation
)
{
    uint64_t start_ms = TitanTime_NowMilliseconds();
    unsigned int result = crc32_update(crc, data, length);
    TitanInstallerDiag_RecordCpuWork(
        operation ? operation : "CRC32",
        length,
        TitanTime_NowMilliseconds() - start_ms
    );
    return result;
}

static void ensure_parent_dirs(const char *path)
{
    char temp[TITAN_INSTALLER_PATH_CAP];
    const char *colon;
    unsigned int prefix_end = 0u;
    unsigned int i;
    copy_string(temp, sizeof(temp), path);
    colon = strchr(temp, ':');
    if (colon != 0) prefix_end = (unsigned int)(colon - temp + 1);
    for (i = 0u; temp[i] != '\0'; i++) {
        if (temp[i] != '/') continue;
        if (i <= prefix_end) continue; /* keep mass:/, massN:/ and mcN:/ prefixes intact */
        temp[i] = '\0';
        (void)installer_diag_mkdir(temp, 0777);
        temp[i] = '/';
    }
}

static int read_exact_stream(int fd, void *buffer, unsigned int length)
{
    unsigned char *out = (unsigned char *)buffer;
    unsigned int done = 0u;
    if (fd < 0 || (buffer == 0 && length != 0u)) return 0;
    while (done < length) {
        int got = read(fd, out + done, length - done);
        if (got <= 0) return 0;
        done += (unsigned int)got;
    }
    return 1;
}

static void transaction_destination(
    const TitanInstallerState *s,
    const TitanInstallerManifestEntry *e,
    char *destination,
    unsigned int destination_cap
)
{
    if (strcmp(e->kind, "SYSTEM") == 0) {
        snprintf(destination, destination_cap, "%s/%s", s->usb_prefix, e->path);
    } else if (strcmp(e->kind, "BOOT") == 0) {
        snprintf(destination, destination_cap, "mc%d:/BOOT/BOOT.ELF", s->memory_card);
    } else {
        snprintf(destination, destination_cap, "mc%d:/%s", s->memory_card, e->path);
    }
}

static int safe_relative_path(const char *path)
{
    const char *p;
    if (path == 0 || path[0] == '\0' || path[0] == '/' || strchr(path, '|') != 0 || strchr(path, ':') != 0)
        return 0;
    p = path;
    while ((p = strstr(p, "..")) != 0) {
        int left_ok = (p == path || p[-1] == '/');
        int right_ok = (p[2] == '\0' || p[2] == '/');
        if (left_ok && right_ok) return 0;
        p += 2;
    }
    return 1;
}

static const char *container_kind_name(unsigned int kind)
{
    if (kind == TITAN_TINS_KIND_SYSTEM) return "SYSTEM";
    if (kind == TITAN_TINS_KIND_BOOT) return "BOOT";
    if (kind == TITAN_TINS_KIND_EXPLOIT) return "EXPLOIT";
    if (kind == TITAN_TINS_KIND_BOOTSTRAP) return "BOOTSTRAP";
    return 0;
}

static int load_manifest(TitanInstallerState *s)
{
    int fd;
    TitanInstallContainerHeader header;
    unsigned int i;
    unsigned int previous_end = 0u;
    unsigned int table_crc = 0u;
    int has_bootstrap = 0;

    s->manifest_count = 0u;
    s->container_payload_base = 0u;
    s->container_size = 0u;
    s->container_flags = 0u;

    /* TINS v1 is intentionally streamable. Do not use lseek()/SEEK_END here:
     * a fresh open starts at byte zero and header/table are consumed once. */
    fd = open(s->container_path, O_RDONLY);
    if (fd < 0) return 0;
    if (!TitanInstallContainer_ReadHeader(fd, &header) ||
        header.file_count > TITAN_INSTALLER_MAX_ENTRIES) {
        close(fd);
        return 0;
    }

    for (i = 0u; i < header.file_count; i++) {
        unsigned char raw[TITAN_TINS_ENTRY_SIZE];
        TitanInstallContainerEntry source_entry;
        TitanInstallerManifestEntry *e;
        const char *kind_name;
        unsigned int end_offset;

        if (!read_exact_stream(fd, raw, sizeof(raw))) {
            close(fd);
            return 0;
        }
        table_crc = crc32_update(table_crc, raw, sizeof(raw));
        if (!TitanInstallContainer_ParseEntryRaw(raw, &source_entry) ||
            !TitanInstallContainer_ValidateBounds(&header, &source_entry)) {
            close(fd);
            return 0;
        }
        kind_name = container_kind_name(source_entry.kind);
        if (kind_name == 0 || !safe_relative_path(source_entry.path)) {
            close(fd);
            return 0;
        }
        end_offset = source_entry.payload_offset + source_entry.size;
        if (end_offset < source_entry.payload_offset ||
            source_entry.payload_offset < previous_end) {
            close(fd);
            return 0;
        }
        previous_end = end_offset;

        e = &s->manifest[s->manifest_count++];
        memset(e, 0, sizeof(*e));
        e->backup_slot = -1;
        copy_string(e->kind, sizeof(e->kind), kind_name);
        copy_string(e->path, sizeof(e->path), source_entry.path);
        e->payload_offset = source_entry.payload_offset;
        e->size = source_entry.size;
        e->crc32 = source_entry.crc32;
        if (source_entry.kind == TITAN_TINS_KIND_BOOTSTRAP) has_bootstrap = 1;
        if (source_entry.kind == TITAN_TINS_KIND_EXPLOIT) { close(fd); s->manifest_count = 0u; return 0; } /* release path: no OpenTuna/Fortuna/generic exploit payload */
    }

    if (table_crc != header.table_crc32) {
        close(fd);
        s->manifest_count = 0u;
        return 0;
    }

    s->container_payload_base = header.payload_offset;
    s->container_size = header.total_size;
    s->container_flags = header.flags;
    s->autonomous_available = has_bootstrap && ((header.flags & TITAN_TINS_FLAG_AUTONOMOUS) != 0u) &&
        TitanInstallerModel_IsAutonomousCompatible(&s->console) &&
        TitanFmcbBackend_IsIntegrated() &&
        path_exists(s->fmcb_system_path) && path_exists(s->fmcb_cnf_path) && path_exists(s->fmcb_space_path);
    close(fd);
    return s->manifest_count > 0u;
}

static int preflight_payload(TitanInstallerState *s)
{
    /* Fast preflight: validate only header/table/table CRC/path bounds. Full
     * payload CRC is calculated incrementally while staging each file. */
    s->tins_preflight_attempted = 0;
    s->tins_preflight_ok = 0;
    if (!s->storage_ready) return 0;

    s->tins_preflight_attempted = 1;
    if (!load_manifest(s)) {
        TitanInstallerDiag_Logf(
            "ERROR", "TINS_PREFLIGHT",
            "result=failed path=%s usb_errno=%d container_probe=%d",
            s->container_path, s->usb_probe_errno, s->usb_container_probe
        );
        return 0;
    }

    s->tins_preflight_ok = 1;
    TitanInstallerDiag_Logf(
        "INFO", "TINS_PREFLIGHT",
        "result=ok path=%s entries=%u container_size=%u payload_base=%u flags=0x%08X autonomous=%d",
        s->container_path, s->manifest_count, s->container_size,
        s->container_payload_base, s->container_flags, s->autonomous_available
    );
    return 1;
}

static int install_entry_enabled(const TitanInstallerState *s, const TitanInstallerManifestEntry *e)
{
    if (s == 0 || e == 0) return 0;
    if (strcmp(e->kind, "EXPLOIT") == 0) return 0;
    if (strcmp(e->kind, "BOOTSTRAP") == 0)
        return s->method == TITAN_INSTALLER_METHOD_AUTONOMOUS;
    return 1;
}

static int install_close_sync_compat(int *fd, const char *path, const char *operation);


static void install_async_io_reset(TitanInstallerInstallJob *job)
{
    if (job == 0) return;
    memset(&job->async_io, 0, sizeof(job->async_io));
    job->async_io.kind = TITAN_INSTALLER_ASYNC_NONE;
    job->async_io.backend = TITAN_INSTALLER_ASYNC_BACKEND_NONE;
}

static void install_async_io_format_path(const TitanInstallerAsyncIo *io, char *out, unsigned int cap)
{
    if (out == 0 || cap == 0u) return;
    out[0] = '\0';
    if (io == 0) return;
    if (io->path2[0] != '\0')
        snprintf(out, cap, "%s -> %s", io->path, io->path2);
    else
        copy_string(out, cap, io->path);
}

/* Metadata RPC completion is polled between rendered frames. USB/FAT uses
 * FileXio NOWAIT; Memory Card metadata uses libmc's native asynchronous RPC
 * because fileXioRemove/fileXioRename returned -2 for mc0: paths on the real
 * console while POSIX/libmc access to the same card remained valid. */
static int install_async_io_poll(TitanInstallerInstallJob *job, TitanInstallerAsyncIoKind expected, int *result_out)
{
    int state;
    int result = 0;
    char operation_path[TITAN_INSTALLER_PATH_CAP];
    uint64_t elapsed;
    if (job == 0 || result_out == 0 || job->async_io.kind != expected) return -1;

    if (job->async_io.backend == TITAN_INSTALLER_ASYNC_BACKEND_LIBMC) {
        int cmd = 0;
        state = mcSync(MC_NOWAIT, &cmd, &result);
        if (state == 0) return 0;
        if (state != 1) result = state;
    } else if (job->async_io.backend == TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO) {
        state = fileXioWaitAsync(FXIO_NOWAIT, &result);
        if (state == FXIO_INCOMPLETE) return 0;
        if (state != FXIO_COMPLETE) result = state;
    } else {
        return -1;
    }

    elapsed = TitanTime_NowMilliseconds() - job->async_io.started_ms;
    install_async_io_format_path(&job->async_io, operation_path, sizeof(operation_path));
    TitanInstallerDiag_RecordIo(job->async_io.operation, operation_path, job->async_io.requested,
                                result, 0, elapsed);
    job->last_filexio_result = result;
    if (job->async_io.backend == TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO)
        fileXioSetBlockMode(FXIO_WAIT);
    install_async_io_reset(job);
    *result_out = result;
    return 1;
}

static int install_async_begin_backend(TitanInstallerInstallJob *job, TitanInstallerAsyncIoKind kind,
                                       TitanInstallerAsyncBackend backend, const char *operation,
                                       const char *path, const char *path2,
                                       unsigned int requested, int immediate_result)
{
    if (job == 0 || job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE) return -1;
    memset(&job->async_io, 0, sizeof(job->async_io));
    job->async_io.kind = kind;
    job->async_io.backend = backend;
    job->async_io.started_ms = TitanTime_NowMilliseconds();
    job->async_io.requested = requested;
    copy_string(job->async_io.operation, sizeof(job->async_io.operation), operation ? operation : "ASYNC_IO");
    copy_string(job->async_io.path, sizeof(job->async_io.path), path ? path : "");
    copy_string(job->async_io.path2, sizeof(job->async_io.path2), path2 ? path2 : "");
    if (immediate_result < 0) {
        char operation_path[TITAN_INSTALLER_PATH_CAP];
        install_async_io_format_path(&job->async_io, operation_path, sizeof(operation_path));
        job->last_filexio_result = immediate_result;
        TitanInstallerDiag_RecordIo(job->async_io.operation, operation_path, requested,
                                    immediate_result, 0, 0u);
        if (backend == TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO)
            fileXioSetBlockMode(FXIO_WAIT);
        install_async_io_reset(job);
        return -1;
    }
    return 0;
}

static int install_async_begin(TitanInstallerInstallJob *job, TitanInstallerAsyncIoKind kind,
                               const char *operation, const char *path, const char *path2,
                               unsigned int requested, int immediate_result)
{
    return install_async_begin_backend(job, kind, TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                       operation, path, path2, requested, immediate_result);
}


static int install_path_is_mc(const char *path)
{
    return path != 0 && path[0] == 'm' && path[1] == 'c' &&
           path[2] >= '0' && path[2] <= '9' && path[3] == ':';
}

static int install_mc_path_parts(const char *path, int *port_out, const char **name_out)
{
    if (!install_path_is_mc(path)) return 0;
    if (port_out != 0) *port_out = path[2] - '0';
    if (name_out != 0) {
        const char *name = path + 4;
        if (*name == '\0') name = "/";
        *name_out = name;
    }
    return 1;
}

/* BuildFix5: never use a Memory Card file as the staging/transport source for
 * another Memory Card write.  MCSERV/libmc serializes all commands through one
 * global RPC command slot, and the BF4 MC->MC publisher froze real hardware.
 * MC-bound payloads therefore stage on the installer USB and are later copied
 * USB->MC with only one libmc file descriptor active. */
static void install_build_temporary_path(const TitanInstallerState *s, unsigned int entry_index,
                                         const char *destination, char *out, unsigned int cap)
{
    if (out == 0 || cap == 0u) return;
    if (install_path_is_mc(destination) && s != 0 && s->installer_root[0] != '\0') {
        snprintf(out, cap, "%s/.MC_STAGE_%03u.TITAN.NEW", s->installer_root, entry_index);
        return;
    }
    snprintf(out, cap, "%s.TITAN.NEW", destination ? destination : "");
}

static void install_build_backup_transport_path(const TitanInstallerState *s, unsigned int entry_index,
                                                char *out, unsigned int cap)
{
    if (out == 0 || cap == 0u) return;
    if (s != 0 && s->installer_root[0] != '\0')
        snprintf(out, cap, "%s/.MC_BACKUP_%03u.TMP", s->installer_root, entry_index);
    else
        out[0] = '\0';
}

static const char *install_backup_copy_source(const TitanInstallerInstallJob *job)
{
    if (job != 0 && job->backup_transport_phase == 2u) return job->backup_transport;
    return job != 0 ? job->destination : "";
}

static const char *install_backup_copy_destination(const TitanInstallerInstallJob *job)
{
    if (job != 0 && job->backup_transport_phase == 1u) return job->backup_transport;
    return job != 0 ? job->backup : "";
}

/* newlib/POSIX O_* flags and the IOP FIO_O_* flags are different namespaces.
 * Direct fileXioOpen() and libmc mcOpen() ultimately feed IOP-side file services.
 * On real hardware, passing newlib O_RDONLY (0) through libmc opened the MC
 * staging file but the first mcRead() completed with sceMcResDeniedPermit (-5).
 * Use the explicit FIO_O_* domain for both direct IOP-facing open paths.
 *
 * BuildFix5 additionally isolates MC publication transport: a BF4 hardware run
 * froze while copying staging MC->MC. MC-bound staging now lives on USB and
 * backup/rollback use USB as the transport hop, so no state holds a libmc read
 * fd and a libmc write fd for the same copy operation. */
static int install_iop_open_flags_from_posix(int flags)
{
    int out = 0;
    switch (flags & O_ACCMODE) {
        case O_WRONLY: out |= FIO_O_WRONLY; break;
        case O_RDWR:   out |= FIO_O_RDWR; break;
        case O_RDONLY:
        default:       out |= FIO_O_RDONLY; break;
    }
#ifdef O_APPEND
    if ((flags & O_APPEND) != 0) out |= FIO_O_APPEND;
#endif
#ifdef O_CREAT
    if ((flags & O_CREAT) != 0) out |= FIO_O_CREAT;
#endif
#ifdef O_TRUNC
    if ((flags & O_TRUNC) != 0) out |= FIO_O_TRUNC;
#endif
#ifdef O_EXCL
    if ((flags & O_EXCL) != 0) out |= FIO_O_EXCL;
#endif
    return out;
}

static void install_log_open_flag_translation_once(const char *backend, const char *path,
                                                   int posix_flags, int iop_flags)
{
    static int logged_filexio = 0;
    static int logged_libmc = 0;
    int *gate = (backend != 0 && strncmp(backend, "libmc", 5u) == 0) ? &logged_libmc : &logged_filexio;
    if (*gate) return;
    *gate = 1;
    TitanInstallerDiag_Logf("INFO", "OPEN_FLAG_TRANSLATION",
                            "backend=%s posix=0x%X iop=0x%X path=%s",
                            backend ? backend : "iop", posix_flags, iop_flags, path ? path : "");
}

static int install_close_sync_compat(int *fd, const char *path, const char *operation)
{
    int result, saved_errno = 0;
    uint64_t start_ms;
    if (fd == 0 || *fd < 0) return 1;
    start_ms = TitanTime_NowMilliseconds();
    if (install_path_is_mc(path)) {
        int cmd = 0;
        result = mcClose(*fd);
        if (result >= 0) {
            int sync_result = 0;
            int sync_state = mcSync(MC_WAIT, &cmd, &sync_result);
            result = sync_state < 0 ? sync_state : sync_result;
        }
        TitanInstallerDiag_RecordIo(operation ? operation : "CLOSE_MC_SYNC", path ? path : "", 0u, result, 0,
                                    TitanTime_NowMilliseconds() - start_ms);
    } else {
        fileXioSetBlockMode(FXIO_WAIT);
        result = fileXioClose(*fd);
        saved_errno = 0;
        TitanInstallerDiag_RecordIo(operation ? operation : "CLOSE_FILEXIO_SYNC", path ? path : "", 0u, result, saved_errno,
                                    TitanTime_NowMilliseconds() - start_ms);
    }
    *fd = -1;
    return result >= 0;
}

static unsigned int install_adaptive_get_slice(TitanInstallerInstallJob *job, const char *path, int write_op)
{
    if (job == 0) return 512u;
    if (install_path_is_mc(path))
        return write_op ? job->mc_write_slice : job->mc_read_slice;
    return write_op ? job->usb_write_slice : job->usb_read_slice;
}

static void install_adaptive_set_slice(TitanInstallerInstallJob *job, const char *path, int write_op, unsigned int value)
{
    if (job == 0) return;
    if (install_path_is_mc(path)) {
        if (write_op) job->mc_write_slice = value;
        else job->mc_read_slice = value;
    } else {
        if (write_op) job->usb_write_slice = value;
        else job->usb_read_slice = value;
    }
}

static void install_adaptive_limits(const char *path, int write_op, unsigned int *min_out, unsigned int *max_out)
{
    unsigned int lo, hi;
    if (install_path_is_mc(path)) {
        lo = write_op ? TITAN_INSTALLER_MC_WRITE_MIN : TITAN_INSTALLER_MC_READ_MIN;
        hi = write_op ? TITAN_INSTALLER_MC_WRITE_MAX : TITAN_INSTALLER_MC_READ_MAX;
    } else {
        lo = write_op ? TITAN_INSTALLER_USB_WRITE_MIN : TITAN_INSTALLER_USB_READ_MIN;
        hi = write_op ? TITAN_INSTALLER_USB_WRITE_MAX : TITAN_INSTALLER_USB_READ_MAX;
    }
    if (min_out != 0) *min_out = lo;
    if (max_out != 0) *max_out = hi;
}

static void install_adaptive_update(TitanInstallerInstallJob *job, const char *path, int write_op,
                                    unsigned int requested, int result, uint64_t elapsed_ms)
{
    unsigned int slice, lo, hi, next;
    if (job == 0 || result <= 0) return;
    slice = install_adaptive_get_slice(job, path, write_op);
    install_adaptive_limits(path, write_op, &lo, &hi);
    next = slice;

    if (elapsed_ms > (uint64_t)(TITAN_INSTALLER_IO_TARGET_MS * 2u) && slice > lo) {
        next = slice / 2u;
        if (next < lo) next = lo;
    } else if (elapsed_ms <= (uint64_t)(TITAN_INSTALLER_IO_TARGET_MS / 2u) &&
               (unsigned int)result >= requested && slice < hi) {
        next = slice * 2u;
        if (next > hi) next = hi;
    }

    if (next != slice) {
        install_adaptive_set_slice(job, path, write_op, next);
        TitanInstallerDiag_Logf(
            "INFO", "IO_SLICE_TUNE",
            "device=%s direction=%s old=%u new=%u elapsed_ms=%llu result=%d",
            install_path_is_mc(path) ? "mc" : "usb", write_op ? "write" : "read",
            slice, next, (unsigned long long)elapsed_ms, result
        );
    }
}

static int install_posix_adaptive_write(TitanInstallerInstallJob *job, int fd, const void *buffer,
                                        unsigned int requested, const char *path, const char *operation)
{
    unsigned int cap;
    int result, saved_errno;
    uint64_t start_ms, elapsed;
    if (job == 0 || fd < 0 || buffer == 0 || requested == 0u) return -1;
    cap = install_adaptive_get_slice(job, path, 1);
    if (cap == 0u || cap > requested) cap = requested;
    errno = 0;
    start_ms = TitanTime_NowMilliseconds();
    result = write(fd, buffer, cap);
    elapsed = TitanTime_NowMilliseconds() - start_ms;
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(operation ? operation : "WRITE", path ? path : "",
                                cap, result, saved_errno, elapsed);
    install_adaptive_update(job, path, 1, cap, result, elapsed);
    if (result < 0) job->last_posix_errno = saved_errno;
    return result;
}

static void install_sync_operation_name(const char *operation, char *out, unsigned int cap)
{
    if (out == 0 || cap == 0u) return;
    if (operation != 0 && strncmp(operation, "ASYNC_", 6u) == 0)
        copy_string(out, cap, operation + 6u);
    else
        copy_string(out, cap, operation ? operation : "IO");
}

static int install_async_open_step(TitanInstallerInstallJob *job, const char *path, int flags, int mode,
                                   const char *operation, int *result_out)
{
    int r;
    int iop_flags;
    if (job == 0 || result_out == 0) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
        return install_async_io_poll(job, TITAN_INSTALLER_ASYNC_OPEN, result_out);

    iop_flags = install_iop_open_flags_from_posix(flags);
    if (install_path_is_mc(path)) {
        int port = 0;
        const char *name = 0;
        (void)mode;
        if (!install_mc_path_parts(path, &port, &name)) return -1;
        install_log_open_flag_translation_once("libmc-iop", path, flags, iop_flags);
        r = mcOpen(port, 0, name, iop_flags);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_OPEN,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, 0u, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }

    install_log_open_flag_translation_once("filexio", path, flags, iop_flags);
    fileXioSetBlockMode(FXIO_NOWAIT);
    r = fileXioOpen(path, iop_flags, mode);
    if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_OPEN,
                                    TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                    operation, path, 0, 0u, r) < 0) {
        *result_out = r;
        return 1;
    }
    return 0;
}

static int install_async_close_step(TitanInstallerInstallJob *job, int *fd, const char *path,
                                    const char *operation, int *result_out)
{
    int r;
    if (job == 0 || fd == 0 || result_out == 0) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE) {
        int state = install_async_io_poll(job, TITAN_INSTALLER_ASYNC_CLOSE, result_out);
        if (state > 0) *fd = -1;
        return state;
    }
    if (*fd < 0) { *result_out = 0; return 1; }

    if (install_path_is_mc(path)) {
        r = mcClose(*fd);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_CLOSE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, 0u, r) < 0) {
            *result_out = r;
            *fd = -1;
            return 1;
        }
        return 0;
    }

    fileXioSetBlockMode(FXIO_NOWAIT);
    r = fileXioClose(*fd);
    if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_CLOSE,
                                    TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                    operation, path, 0, 0u, r) < 0) {
        *result_out = r;
        *fd = -1;
        return 1;
    }
    return 0;
}

static int install_async_read_step(TitanInstallerInstallJob *job, int fd, void *buffer, unsigned int size,
                                   const char *path, const char *operation, int *result_out)
{
    unsigned int cap;
    int r;
    if (job == 0 || result_out == 0 || fd < 0 || buffer == 0 || size == 0u) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
        return install_async_io_poll(job, TITAN_INSTALLER_ASYNC_READ, result_out);

    cap = size > TITAN_INSTALLER_COPY_BUFFER ? TITAN_INSTALLER_COPY_BUFFER : size;
    if (install_path_is_mc(path)) {
        r = mcRead(fd, buffer, (int)cap);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_READ,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, cap, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }

    fileXioSetBlockMode(FXIO_NOWAIT);
    r = fileXioRead(fd, buffer, (int)cap);
    if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_READ,
                                    TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                    operation, path, 0, cap, r) < 0) {
        *result_out = r;
        return 1;
    }
    return 0;
}

/* mass: output uses FileXio NOWAIT with explicit FIO flag translation; MC
 * output uses asynchronous libmc. A POSIX write backend remains only as the
 * safe fallback if a direct FileXio OPEN itself fails. */
static int install_output_open_step(TitanInstallerInstallJob *job, const char *path,
                                    int flags, int mode, const char *operation,
                                    int *fd_out, TitanInstallerFdBackend *backend_out)
{
    int r;
    if (job == 0 || fd_out == 0 || backend_out == 0) return -1;

    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE) {
        int state = install_async_io_poll(job, TITAN_INSTALLER_ASYNC_OPEN, &r);
        if (state <= 0) return state;
        if (r >= 0) {
            *fd_out = r;
            return 1;
        }
        /* A FileXio OPEN failure is safe to fall back from because no payload
         * bytes have been committed through that descriptor yet. */
        if (*backend_out == TITAN_INSTALLER_FD_BACKEND_FILEXIO) {
            r = installer_diag_open(path, flags, mode, "OPEN_OUTPUT_POSIX_FALLBACK");
            if (r >= 0) {
                *backend_out = TITAN_INSTALLER_FD_BACKEND_POSIX;
                *fd_out = r;
                TitanInstallerDiag_Logf("WARN", "OUTPUT_BACKEND_FALLBACK",
                                        "path=%s from=filexio to=posix reason=open result=%d",
                                        path ? path : "", r);
                return 1;
            }
        }
        *fd_out = r;
        return 1;
    }

    if (install_path_is_mc(path)) {
        int port = 0;
        const char *name = 0;
        (void)mode;
        if (!install_mc_path_parts(path, &port, &name)) return -1;
        {
            *backend_out = TITAN_INSTALLER_FD_BACKEND_LIBMC;
            int iop_flags = install_iop_open_flags_from_posix(flags);
            install_log_open_flag_translation_once("libmc-iop", path, flags, iop_flags);
            r = mcOpen(port, 0, name, iop_flags);
        }
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_OPEN,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, 0u, r) < 0) {
            *fd_out = r;
            return 1;
        }
        return 0;
    }

    *backend_out = TITAN_INSTALLER_FD_BACKEND_FILEXIO;
    fileXioSetBlockMode(FXIO_NOWAIT);
    {
        int iop_flags = install_iop_open_flags_from_posix(flags);
        install_log_open_flag_translation_once("filexio", path, flags, iop_flags);
        r = fileXioOpen(path, iop_flags, mode);
    }
    if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_OPEN,
                                    TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                    operation, path, 0, 0u, r) < 0) {
        /* Immediate RPC submission failure: POSIX fallback is still safe. */
        r = installer_diag_open(path, flags, mode, "OPEN_OUTPUT_POSIX_FALLBACK");
        if (r >= 0) {
            *backend_out = TITAN_INSTALLER_FD_BACKEND_POSIX;
            *fd_out = r;
            TitanInstallerDiag_Logf("WARN", "OUTPUT_BACKEND_FALLBACK",
                                    "path=%s from=filexio to=posix reason=submit result=%d",
                                    path ? path : "", r);
            return 1;
        }
        *fd_out = r;
        return 1;
    }
    return 0;
}

static int install_output_write_step(TitanInstallerInstallJob *job, int fd,
                                     TitanInstallerFdBackend backend,
                                     const void *buffer, unsigned int size,
                                     const char *path, const char *operation,
                                     int *result_out)
{
    unsigned int cap;
    int r;
    if (job == 0 || result_out == 0 || fd < 0 || buffer == 0 || size == 0u) return -1;

    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
        return install_async_io_poll(job, TITAN_INSTALLER_ASYNC_WRITE, result_out);

    cap = size > TITAN_INSTALLER_COPY_BUFFER ? TITAN_INSTALLER_COPY_BUFFER : size;
    if (backend == TITAN_INSTALLER_FD_BACKEND_LIBMC) {
        r = mcWrite(fd, buffer, (int)cap);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_WRITE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, cap, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }
    if (backend == TITAN_INSTALLER_FD_BACKEND_FILEXIO) {
        fileXioSetBlockMode(FXIO_NOWAIT);
        r = fileXioWrite(fd, buffer, (int)cap);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_WRITE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                        operation, path, 0, cap, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }
    if (backend == TITAN_INSTALLER_FD_BACKEND_POSIX) {
        char op[40];
        install_sync_operation_name(operation, op, sizeof(op));
        *result_out = install_posix_adaptive_write(job, fd, buffer, cap, path, op);
        return 1;
    }
    return -1;
}

static int install_output_close_step(TitanInstallerInstallJob *job, int *fd,
                                     TitanInstallerFdBackend *backend,
                                     const char *path, const char *operation,
                                     int *result_out)
{
    int r;
    if (job == 0 || fd == 0 || backend == 0 || result_out == 0) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE) {
        int state = install_async_io_poll(job, TITAN_INSTALLER_ASYNC_CLOSE, result_out);
        if (state > 0) {
            *fd = -1;
            *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
        }
        return state;
    }
    if (*fd < 0) {
        *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
        *result_out = 0;
        return 1;
    }
    if (*backend == TITAN_INSTALLER_FD_BACKEND_LIBMC) {
        r = mcClose(*fd);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_CLOSE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, 0u, r) < 0) {
            *result_out = r;
            *fd = -1;
            *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
            return 1;
        }
        return 0;
    }
    if (*backend == TITAN_INSTALLER_FD_BACKEND_FILEXIO) {
        fileXioSetBlockMode(FXIO_NOWAIT);
        r = fileXioClose(*fd);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_CLOSE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_FILEXIO,
                                        operation, path, 0, 0u, r) < 0) {
            *result_out = r;
            *fd = -1;
            *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
            return 1;
        }
        return 0;
    }
    if (*backend == TITAN_INSTALLER_FD_BACKEND_POSIX) {
        int saved_errno = 0;
        uint64_t started = TitanTime_NowMilliseconds();
        errno = 0;
        r = close(*fd);
        if (r < 0) saved_errno = errno;
        TitanInstallerDiag_RecordIo(operation ? operation : "CLOSE_OUTPUT_POSIX", path ? path : "",
                                    0u, r, saved_errno, TitanTime_NowMilliseconds() - started);
        *fd = -1;
        *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
        *result_out = r;
        return 1;
    }
    *fd = -1;
    *backend = TITAN_INSTALLER_FD_BACKEND_NONE;
    *result_out = -1;
    return 1;
}

static int install_output_close_sync(TitanInstallerInstallJob *job)
{
    int result = 0;
    uint64_t started;
    if (job == 0 || job->stage_fd < 0) return 1;
    started = TitanTime_NowMilliseconds();
    if (job->stage_backend == TITAN_INSTALLER_FD_BACKEND_LIBMC) {
        int cmd = 0;
        result = mcClose(job->stage_fd);
        if (result >= 0) {
            int sync_result = 0;
            int state = mcSync(MC_WAIT, &cmd, &sync_result);
            result = state < 0 ? state : sync_result;
        }
    } else if (job->stage_backend == TITAN_INSTALLER_FD_BACKEND_FILEXIO) {
        fileXioSetBlockMode(FXIO_WAIT);
        result = fileXioClose(job->stage_fd);
    } else {
        result = close(job->stage_fd);
    }
    TitanInstallerDiag_RecordIo("CLOSE_OUTPUT_SYNC", job->temporary, 0u, result, 0,
                                TitanTime_NowMilliseconds() - started);
    job->stage_fd = -1;
    job->stage_backend = TITAN_INSTALLER_FD_BACKEND_NONE;
    return result >= 0;
}

static int install_async_remove_step(TitanInstallerInstallJob *job, const char *path,
                                     const char *operation, int *result_out)
{
    int r;
    if (job == 0 || result_out == 0) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
        return install_async_io_poll(job, TITAN_INSTALLER_ASYNC_REMOVE, result_out);

    if (install_path_is_mc(path)) {
        int port = 0;
        const char *name = 0;
        if (!install_mc_path_parts(path, &port, &name)) return -1;
        r = mcDelete(port, 0, name);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_REMOVE,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, path, 0, 0u, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }

    fileXioSetBlockMode(FXIO_NOWAIT);
    r = fileXioRemove(path);
    if (install_async_begin(job, TITAN_INSTALLER_ASYNC_REMOVE, operation, path, 0, 0u, r) < 0) {
        *result_out = r;
        return 1;
    }
    return 0;
}

static int install_async_rename_step(TitanInstallerInstallJob *job, const char *source, const char *destination,
                                     const char *operation, int *result_out)
{
    int r;
    if (job == 0 || result_out == 0) return -1;
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
        return install_async_io_poll(job, TITAN_INSTALLER_ASYNC_RENAME, result_out);

    if (install_path_is_mc(source) || install_path_is_mc(destination)) {
        int source_port = 0, destination_port = 0;
        const char *source_name = 0, *destination_name = 0;
        if (!install_mc_path_parts(source, &source_port, &source_name) ||
            !install_mc_path_parts(destination, &destination_port, &destination_name) ||
            source_port != destination_port) {
            *result_out = -1;
            return 1;
        }
        r = mcRename(source_port, 0, source_name, destination_name);
        if (install_async_begin_backend(job, TITAN_INSTALLER_ASYNC_RENAME,
                                        TITAN_INSTALLER_ASYNC_BACKEND_LIBMC,
                                        operation, source, destination, 0u, r) < 0) {
            *result_out = r;
            return 1;
        }
        return 0;
    }

    fileXioSetBlockMode(FXIO_NOWAIT);
    r = fileXioRename(source, destination);
    if (install_async_begin(job, TITAN_INSTALLER_ASYNC_RENAME, operation, source, destination, 0u, r) < 0) {
        *result_out = r;
        return 1;
    }
    return 0;
}


static void install_job_close_fds(TitanInstallerInstallJob *job)
{
    int ignored = 0;
    if (job == 0) return;
    /* Failure/cancel should normally be entered only after the outstanding RPC
     * completes. This blocking wait is an emergency cleanup guard, not normal
     * installation flow. */
    if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE) {
        if (job->async_io.backend == TITAN_INSTALLER_ASYNC_BACKEND_LIBMC) {
            (void)mcSync(MC_WAIT, 0, &ignored);
        } else {
            (void)fileXioWaitAsync(FXIO_WAIT, &ignored);
            fileXioSetBlockMode(FXIO_WAIT);
        }
        install_async_io_reset(job);
    }
    (void)install_close_sync_compat(&job->container_fd, "TITAN_SYSTEM.TINS", "CLOSE_TINS");
    (void)install_output_close_sync(job);
    (void)install_close_sync_compat(&job->verify_fd, job->temporary, "CLOSE_VERIFY");
}

static void install_job_update_progress(TitanInstallerState *s)
{
    TitanInstallerInstallJob *job;
    unsigned long long scaled;
    if (s == 0) return;
    job = &s->install_job;
    if (job->work_total == 0u) return;
    scaled = (job->work_done * 87ull) / job->work_total;
    if (scaled > 87ull) scaled = 87ull;
    {
        unsigned int next = 3u + (unsigned int)scaled;
        if (next > s->progress) s->progress = next;
    }
}

static void install_job_begin_rollback(TitanInstallerState *s, const char *failure)
{
    TitanInstallerInstallJob *job;
    if (s == 0) return;
    job = &s->install_job;
    TitanInstallerDiag_Logf(
        "ERROR",
        "ROLLBACK_BEGIN",
        "entry=%u/%u phase=%s path=%s failure=%s posix_errno=%d filexio_result=%d progress=%u",
        job->entry_index < s->manifest_count ? job->entry_index + 1u : s->manifest_count,
        s->manifest_count,
        install_phase_name(job->phase),
        job->entry_index < s->manifest_count ? s->manifest[job->entry_index].path : "",
        failure ? failure : "Falha na transação de instalação.",
        job->last_posix_errno,
        job->last_filexio_result,
        s->progress
    );
    install_job_close_fds(job);
    copy_string(job->failure, sizeof(job->failure), failure ? failure : "Falha na transação de instalação.");
    /* Roll back only entries that this run could have touched. v2.0.12 walked
     * all 84 manifest entries after an entry-1 CRC failure, creating hundreds
     * of pointless STATs and even touching the BOOT staging path. */
    job->rollback_index = job->entry_index + (job->current_entry_touched ? 1u : 0u);
    if (job->rollback_index > s->manifest_count) job->rollback_index = s->manifest_count;
    job->rollback_step = 0u;
    job->phase = TITAN_INSTALL_JOB_ROLLBACK;
    copy_string(s->status, sizeof(s->status), "Restaurando o estado anterior com segurança...");
}

static int install_entry_requires_full_verify(const TitanInstallerManifestEntry *e)
{
    const char *dot;
    if (e == 0) return 1;
    if (strcmp(e->kind, "BOOT") == 0 || strcmp(e->kind, "EXPLOIT") == 0) return 1;
    if (e->size <= TITAN_INSTALLER_FULL_VERIFY_LIMIT) return 1;
    dot = strrchr(e->path, '.');
    if (dot != 0 && (
        strcmp(dot, ".elf") == 0 || strcmp(dot, ".ELF") == 0 ||
        strcmp(dot, ".irx") == 0 || strcmp(dot, ".IRX") == 0 ||
        strcmp(dot, ".tmod") == 0 || strcmp(dot, ".TMOD") == 0 ||
        strcmp(dot, ".pkg") == 0 || strcmp(dot, ".PKG") == 0))
        return 1;
    return 0;
}

static int install_stat_size(const char *path, unsigned int *size_out)
{
    struct stat st;
    uint64_t start_ms;
    int result;
    int saved_errno;
    if (path == 0) return 0;
    errno = 0;
    start_ms = TitanTime_NowMilliseconds();
    result = stat(path, &st);
    saved_errno = result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        "STAT_SIZE", path, 0u, result, saved_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    if (result != 0 || st.st_size < 0) return 0;
    if ((unsigned long long)st.st_size > 0xffffffffull) return 0;
    if (size_out != 0) *size_out = (unsigned int)st.st_size;
    return 1;
}

static int install_remove_compat(const char *path)
{
    int posix_errno;
    int posix_result;
    int fxio_result;
    uint64_t start_ms;
    if (path == 0 || !path_exists(path)) return 1;
    errno = 0;
    start_ms = TitanTime_NowMilliseconds();
    posix_result = remove(path);
    posix_errno = posix_result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        "REMOVE_POSIX", path, 0u, posix_result, posix_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    if (posix_result == 0 || !path_exists(path)) return 1;
    start_ms = TitanTime_NowMilliseconds();
    fxio_result = fileXioRemove(path);
    TitanInstallerDiag_RecordIo(
        "REMOVE_FILEXIO", path, 0u, fxio_result, posix_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return fxio_result >= 0 || !path_exists(path);
}

static void install_build_backup_path(
    const TitanInstallerManifestEntry *e,
    const char *destination,
    char *out,
    unsigned int cap
)
{
    if (out == 0 || cap == 0u) return;
    out[0] = '\0';
    if (e == 0 || destination == 0) return;
    if (e->backup_slot == -2)
        snprintf(out, cap, "%s.TITAN.BAK", destination);
    else if (e->backup_slot >= 0)
        snprintf(out, cap, "%s.TITAN.BK%02d", destination, e->backup_slot);
}

/*
 * Transaction backups must never depend on deleting a stale FAT directory
 * entry first. Failed/interrupted older installer builds can leave a
 * destination.TITAN.BAK that is visible through stat() but cannot be removed
 * by the active USBHDFSD/newlib combination. Requiring its deletion made a
 * harmless stale artifact fatal.
 *
 * BOOT keeps the canonical BOOT.TITAN.BAK contract when that name is free.
 * If it already exists we preserve it and allocate a numbered transaction
 * backup instead. All other entries always use a numbered, never-overwritten
 * backup slot. Numbered backups are transaction-owned and can be cleaned
 * best-effort after success.
 */
static int install_mc_dir_contains(const char *leaf, int count)
{
    int i;
    if (leaf == 0 || count <= 0) return 0;
    for (i = 0; i < count && i < (int)TITAN_INSTALLER_MC_DIR_ENTRIES; i++) {
        if (strncmp((const char *)g_mc_boot_dir[i].EntryName, leaf, sizeof(g_mc_boot_dir[i].EntryName)) == 0)
            return 1;
    }
    return 0;
}

/* Return 1=selected, 0=pending, -1=failure. The Memory Card path is
 * enumerated once with libmc instead of issuing 65+ stat() probes. This is
 * authoritative for /BOOT and directly addresses the hardware log where all
 * numbered slots were falsely treated as occupied. */
static int install_allocate_backup_path_step(TitanInstallerState *s, TitanInstallerManifestEntry *e,
                                             const char *destination, char *out, unsigned int cap)
{
    TitanInstallerInstallJob *job;
    unsigned int slot;
    char candidate[TITAN_INSTALLER_PATH_CAP];
    if (s == 0 || e == 0 || destination == 0 || out == 0 || cap == 0u) return -1;
    job = &s->install_job;

    if (strcmp(e->kind, "BOOT") == 0) {
        int sync_state, dir_result = 0;
        if (!job->mc_dir_pending) {
            memset(g_mc_boot_dir, 0, sizeof(g_mc_boot_dir));
            if (mcGetDir(s->memory_card, 0, "/BOOT/*", 0,
                         (int)TITAN_INSTALLER_MC_DIR_ENTRIES, g_mc_boot_dir) < 0) {
                TitanInstallerDiag_Logf("ERROR", "MC_BACKUP_ENUM_BEGIN_FAILED", "mc=%d path=/BOOT/*", s->memory_card);
                return -1;
            }
            job->mc_dir_pending = 1;
            TitanInstallerDiag_Logf("INFO", "MC_BACKUP_ENUM_BEGIN", "mc=%d path=/BOOT/* max=%u",
                                    s->memory_card, TITAN_INSTALLER_MC_DIR_ENTRIES);
            return 0;
        }
        sync_state = mcSync(1, 0, &dir_result);
        if (sync_state == 0) return 0;
        job->mc_dir_pending = 0;
        job->mc_dir_result = dir_result;
        if (sync_state < 0 || dir_result < 0 || dir_result >= (int)TITAN_INSTALLER_MC_DIR_ENTRIES) {
            TitanInstallerDiag_Logf("ERROR", "MC_BACKUP_ENUM_FAILED", "mc=%d sync=%d result=%d",
                                    s->memory_card, sync_state, dir_result);
            return -1;
        }
        if (!install_mc_dir_contains("BOOT.ELF.TITAN.BAK", dir_result)) {
            snprintf(candidate, sizeof(candidate), "%s.TITAN.BAK", destination);
            e->backup_slot = -2;
            copy_string(out, cap, candidate);
            TitanInstallerDiag_Logf("INFO", "MC_BACKUP_SLOT_SELECTED", "slot=canonical backup=%s entries=%d", out, dir_result);
            return 1;
        }
        for (slot = 0u; slot < TITAN_INSTALLER_BACKUP_SLOTS; slot++) {
            char leaf[40];
            snprintf(leaf, sizeof(leaf), "BOOT.ELF.TITAN.BK%02u", slot);
            if (!install_mc_dir_contains(leaf, dir_result)) {
                snprintf(candidate, sizeof(candidate), "%s.TITAN.BK%02u", destination, slot);
                e->backup_slot = (int)slot;
                copy_string(out, cap, candidate);
                TitanInstallerDiag_Logf("INFO", "MC_BACKUP_SLOT_SELECTED", "slot=%u backup=%s entries=%d", slot, out, dir_result);
                return 1;
            }
        }
        TitanInstallerDiag_Logf("ERROR", "MC_BACKUP_NO_SLOT", "entries=%d", dir_result);
        return -1;
    }

    /*
     * FAT backup allocation must not scan all 64 names every time. Start from
     * a per-session/per-entry seed and probe only a small bounded window.
     * Existing old BKxx files are preserved; a collision simply advances to
     * another slot. Eight probes cap foreground metadata work while keeping
     * collision probability tiny for normal installer use.
     */
    for (slot = 0u; slot < TITAN_INSTALLER_BACKUP_PROBE_ATTEMPTS; slot++) {
        unsigned int selected = (job->backup_slot_seed + job->entry_index + slot) % TITAN_INSTALLER_BACKUP_SLOTS;
        snprintf(candidate, sizeof(candidate), "%s.TITAN.BK%02u", destination, selected);
        if (!path_exists(candidate)) {
            e->backup_slot = (int)selected;
            copy_string(out, cap, candidate);
            TitanInstallerDiag_Logf("INFO", "USB_BACKUP_SLOT_SELECTED",
                                    "slot=%u backup=%s probes=%u", selected, out, slot + 1u);
            return 1;
        }
    }
    e->backup_slot = -1;
    out[0] = '\0';
    TitanInstallerDiag_Logf("ERROR", "USB_BACKUP_NO_SLOT_BOUNDED",
                            "destination=%s seed=%u probes=%u", destination,
                            job->backup_slot_seed, TITAN_INSTALLER_BACKUP_PROBE_ATTEMPTS);
    return -1;
}

/* Recovery uses the newest numbered slot first. Slots are allocated from low
 * to high, so a later interrupted transaction naturally has the highest live
 * slot. Legacy .TITAN.BAK is kept as the final compatibility fallback. */
static int install_find_recovery_backup(
    const char *destination,
    char *out,
    unsigned int cap
)
{
    char candidate[TITAN_INSTALLER_PATH_CAP];
    if (destination == 0 || out == 0 || cap == 0u) return 0;

    /*
     * Legacy numbered backups (.TITAN.BKxx) are deliberately NOT probed here.
     * The v2.0.11 hardware log showed recovery issuing 65+ stat() calls for
     * every missing destination, creating thousands of false ENOENT events and
     * 67-100 ms frame stalls before installation even opened the TINS.
     *
     * Numbered backups are transaction-owned and ambiguous after an older
     * unjournaled installer run. Preserve them untouched. The canonical
     * .TITAN.BAK is the only legacy recovery artifact with stable semantics
     * (notably BOOT.TITAN.BAK). The current live transaction never needs this
     * finder for its own numbered backup because backup_slot is retained in
     * the manifest entry and rollback builds the exact path directly.
     */
    snprintf(candidate, sizeof(candidate), "%s.TITAN.BAK", destination);
    if (path_exists(candidate)) {
        copy_string(out, cap, candidate);
        TitanInstallerDiag_Logf("INFO", "LEGACY_CANONICAL_BACKUP_FOUND",
                                "destination=%s backup=%s", destination, out);
        return 1;
    }

    out[0] = '\0';
    return 0;
}

static int install_rename_compat(
    TitanInstallerInstallJob *job,
    const char *source,
    const char *destination
)
{
    int fxio_result;
    int posix_result;
    uint64_t start_ms;
    char operation_path[TITAN_INSTALLER_PATH_CAP];
    if (job == 0 || source == 0 || destination == 0) return 0;
    snprintf(operation_path, sizeof(operation_path), "%s -> %s", source, destination);
    errno = 0;
    start_ms = TitanTime_NowMilliseconds();
    posix_result = rename(source, destination);
    job->last_posix_errno = posix_result < 0 ? errno : 0;
    TitanInstallerDiag_RecordIo(
        "RENAME_POSIX", operation_path, 0u, posix_result, job->last_posix_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    if (posix_result == 0) {
        job->last_filexio_result = 0;
        return 1;
    }
    start_ms = TitanTime_NowMilliseconds();
    fxio_result = fileXioRename(source, destination);
    job->last_filexio_result = fxio_result;
    TitanInstallerDiag_RecordIo(
        "RENAME_FILEXIO", operation_path, 0u, fxio_result, job->last_posix_errno,
        TitanTime_NowMilliseconds() - start_ms
    );
    return fxio_result >= 0;
}

/*
 * Publication stays rename-only on the active filesystem. POSIX rename is
 * attempted first and fileXioRename is the hardware fallback. A whole-file
 * copy is deliberately not hidden inside this helper: doing so on the UI/FileXio
 * owner thread would freeze waves for large assets. If both rename paths fail,
 * the transaction fails immediately with the recorded errno/FileXio result and
 * rollback can start without wasting minutes on an invisible bulk copy.
 */
static int install_preserve_existing(
    TitanInstallerInstallJob *job,
    const char *source,
    const char *backup
)
{
    if (job == 0 || source == 0 || backup == 0) return 0;
    if (!install_remove_compat(backup)) return 0;
    return install_rename_compat(job, source, backup);
}

static int install_publish_staging(
    TitanInstallerInstallJob *job,
    const char *temporary,
    const char *destination
)
{
    if (job == 0 || temporary == 0 || destination == 0) return 0;
    if (path_exists(destination) && !install_remove_compat(destination)) return 0;
    return install_rename_compat(job, temporary, destination);
}

static int install_restore_backup(
    TitanInstallerInstallJob *job,
    const char *backup,
    const char *destination
)
{
    if (job == 0 || backup == 0 || destination == 0 || !path_exists(backup)) return 0;
    if (path_exists(destination) && !install_remove_compat(destination)) return 0;
    return install_rename_compat(job, backup, destination);
}

static int install_job_begin(TitanInstallerState *s)
{
    TitanInstallerInstallJob *job;
    unsigned int i;
    unsigned long long active_bytes = 0ull;
    unsigned long long verify_bytes = 0ull;
    unsigned long long payload_bytes;

    if (s == 0 || !s->storage_ready || !s->tins_preflight_ok || s->manifest_count == 0u)
        return 0;
    if (s->method == TITAN_INSTALLER_METHOD_AUTONOMOUS && !s->autonomous_available)
        return 0;

    job = &s->install_job;
    memset(job, 0, sizeof(*job));
    job->container_fd = -1;
    job->stage_fd = -1;
    job->stage_backend = TITAN_INSTALLER_FD_BACKEND_NONE;
    job->verify_fd = -1;
    /* Stale staging is handled on-demand for the entry being installed. A
     * full-manifest recovery sweep caused 30-100 ms metadata frames before any
     * payload work on physical hardware. */
    job->phase = TITAN_INSTALL_JOB_OPEN_CONTAINER;
    job->usb_read_slice = TITAN_INSTALLER_USB_READ_START;
    job->usb_write_slice = TITAN_INSTALLER_USB_WRITE_START;
    job->mc_read_slice = TITAN_INSTALLER_MC_READ_START;
    job->mc_write_slice = TITAN_INSTALLER_MC_WRITE_START;
    job->backup_slot_seed = (unsigned int)(TitanTime_NowMilliseconds() & (TITAN_INSTALLER_BACKUP_SLOTS - 1u));

    for (i = 0u; i < s->manifest_count; i++) {
        s->manifest[i].had_existing = 0;
        s->manifest[i].published = 0;
        s->manifest[i].unchanged = 0;
        s->manifest[i].backup_transport_ready = 0;
        s->manifest[i].backup_slot = -1;
        if (install_entry_enabled(s, &s->manifest[i])) {
            active_bytes += (unsigned long long)s->manifest[i].size;
            if (install_entry_requires_full_verify(&s->manifest[i]))
                verify_bytes += (unsigned long long)s->manifest[i].size;
        }
    }

    payload_bytes = s->container_size >= s->container_payload_base
        ? (unsigned long long)(s->container_size - s->container_payload_base)
        : 0ull;
    /* Real work = source stream once + active staging writes once + a second
     * read only for critical/small files. Large media/assets use source CRC +
     * exact write/size verification to avoid a needless third USB pass. */
    job->work_total = payload_bytes + active_bytes + verify_bytes;
    if (job->work_total == 0ull) job->work_total = 1ull;

    s->progress = 1u;
    copy_string(s->status, sizeof(s->status), "Preparando instalação transacional...");
    TitanInstallerDiag_Logf(
        "INFO",
        "INSTALL_BEGIN",
        "entries=%u payload_bytes=%llu active_bytes=%llu verify_bytes=%llu work_total=%llu method=%d mc=%d",
        s->manifest_count,
        payload_bytes,
        active_bytes,
        verify_bytes,
        job->work_total,
        (int)s->method,
        s->memory_card
    );

    TitanInstallerDiag_Logf("INFO", "RECOVERY_POLICY",
                            "mode=per-entry-on-demand full_manifest_sweep=0 rollback_scope=touched-prefix");
    (void)TitanInstallerDiag_FlushCheckpoint("install-start");
    return 1;
}


static TitanInstallerFmcbState probe_fmcb_state(int mc)
{
    char cnf[64], uninst[64], uninst_old[64], managed[64], sysa[64], syse[64], sysi[64], sysc[64];
    int has_cnf, has_uninst, has_managed, has_system;
    snprintf(cnf,sizeof(cnf),"mc%d:/SYS-CONF/FREEMCB.CNF",mc);
    snprintf(uninst,sizeof(uninst),"mc%d:/SYS-CONF/FMCBUINST.dat",mc);
    snprintf(uninst_old,sizeof(uninst_old),"mc%d:/SYS-CONF/uninstall.dat",mc);
    snprintf(managed,sizeof(managed),"mc%d:/SYS-CONF/TITAN_FMCB.MANAGED",mc);
    snprintf(sysa,sizeof(sysa),"mc%d:/BAEXEC-SYSTEM",mc);
    snprintf(syse,sizeof(syse),"mc%d:/BEEXEC-SYSTEM",mc);
    snprintf(sysi,sizeof(sysi),"mc%d:/BIEXEC-SYSTEM",mc);
    snprintf(sysc,sizeof(sysc),"mc%d:/BCEXEC-SYSTEM",mc);
    has_cnf=path_exists(cnf); has_uninst=path_exists(uninst)||path_exists(uninst_old); has_managed=path_exists(managed);
    has_system=path_exists(sysa)||path_exists(syse)||path_exists(sysi)||path_exists(sysc);
    if (has_managed && has_cnf && has_system) return TITAN_FMCB_TITAN_MANAGED_FMCB;
    /* Any pre-existing EXEC-SYSTEM without recognized FMCB identity is foreign
     * or partial state. Do not let the FMCB cleanup path mutate it blindly. */
    if (!has_cnf && !has_uninst && !has_managed && !has_system) return TITAN_FMCB_NO_FMCB;
    if (has_cnf && has_system) return TITAN_FMCB_COMPATIBLE_FMCB;
    if (has_cnf || has_managed) return TITAN_FMCB_CORRUPT_FMCB;
    return TITAN_FMCB_UNKNOWN_FMCB;
}

static const char *fmcb_state_name(TitanInstallerFmcbState state);

static int fmcb_autonomous_mc_space_check(TitanInstallerState *s, unsigned int *required_out, unsigned int *free_out)
{
    FILE *fp;
    char line[128];
    unsigned int fresh = 0u, adopt = 0u, required;
    int type = 0, free_kb = 0, formatted = 0, cmd = 0, result = 0;
    if (s == 0 || s->fmcb_space_path[0] == '\0') return 0;
    fp = fopen(s->fmcb_space_path, "rb");
    if (fp == 0) return 0;
    while (fgets(line, sizeof(line), fp) != 0) {
        if (sscanf(line, "fresh_required_bytes=%u", &fresh) == 1) continue;
        if (sscanf(line, "adopt_required_bytes=%u", &adopt) == 1) continue;
    }
    fclose(fp);
    required = (s->fmcb_state == TITAN_FMCB_NO_FMCB) ? fresh : adopt;
    if (required == 0u) return 0;
    if (mcGetInfo(s->memory_card, 0, &type, &free_kb, &formatted) < 0) return 0;
    if (mcSync(MC_WAIT, &cmd, &result) < 0 || result < -1 || type != MC_TYPE_PS2 || !formatted || free_kb < 0) return 0;
    if (required_out) *required_out = required;
    if (free_out) *free_out = (unsigned int)free_kb * 1024u;
    TitanInstallerDiag_Logf("INFO", "FMCB_MC_SPACE", "mc=%d state=%s required=%u free=%u", s->memory_card, fmcb_state_name(s->fmcb_state), required, (unsigned int)free_kb * 1024u);
    return ((unsigned int)free_kb * 1024u) >= required;
}

static const char *fmcb_state_name(TitanInstallerFmcbState state)
{
    switch(state) {
        case TITAN_FMCB_NO_FMCB: return "NO_FMCB";
        case TITAN_FMCB_COMPATIBLE_FMCB: return "COMPATIBLE_FMCB";
        case TITAN_FMCB_UNKNOWN_FMCB: return "UNKNOWN_FMCB";
        case TITAN_FMCB_CORRUPT_FMCB: return "CORRUPT_FMCB";
        case TITAN_FMCB_TITAN_MANAGED_FMCB: return "TITAN_MANAGED_FMCB";
        default: return "UNKNOWN_FMCB";
    }
}

static int fmcb_write_journal(TitanInstallerState *s)
{
    FILE *fp; unsigned int i;
    ensure_parent_dirs(s->fmcb_journal_path);
    fp=fopen(s->fmcb_journal_path,"wb"); if(!fp) return 0;
    fprintf(fp,"TITAN_FMCB_TXN=1\nmc=%d\nentries=%u\n",s->memory_card,s->manifest_count);
    for(i=0;i<s->manifest_count;i++) {
        TitanInstallerManifestEntry *e=&s->manifest[i];
        fprintf(fp,"%u|%d|%d|%d|%d|%d\n",i,e->had_existing,e->published,e->unchanged,e->backup_slot,e->backup_transport_ready);
    }
    fclose(fp); return 1;
}

static int fmcb_load_journal(TitanInstallerState *s, int *mc_out)
{
    FILE *fp;
    char line[160];
    unsigned int expected_entries = 0u;
    unsigned int seen_entries = 0u;
    int journal_mc = -1;
    int version_ok = 0;

    if (s == 0 || s->fmcb_journal_path[0] == '\0') return 0;
    fp = fopen(s->fmcb_journal_path, "rb");
    if (fp == 0) return 0;
    while (fgets(line, sizeof(line), fp) != 0) {
        unsigned int index;
        int had_existing, published, unchanged, backup_slot, backup_transport_ready;
        if (sscanf(line, "TITAN_FMCB_TXN=%d", &version_ok) == 1) continue;
        if (sscanf(line, "mc=%d", &journal_mc) == 1) continue;
        if (sscanf(line, "entries=%u", &expected_entries) == 1) continue;
        if (sscanf(line, "%u|%d|%d|%d|%d|%d", &index, &had_existing, &published,
                   &unchanged, &backup_slot, &backup_transport_ready) == 6) {
            TitanInstallerManifestEntry *e;
            if (index >= s->manifest_count) { fclose(fp); return 0; }
            e = &s->manifest[index];
            e->had_existing = had_existing ? 1 : 0;
            e->published = published ? 1 : 0;
            e->unchanged = unchanged ? 1 : 0;
            e->backup_slot = backup_slot;
            e->backup_transport_ready = backup_transport_ready ? 1 : 0;
            seen_entries++;
        }
    }
    fclose(fp);
    if (version_ok != 1 || journal_mc < 0 || journal_mc > 1 ||
        expected_entries != s->manifest_count || seen_entries != s->manifest_count)
        return 0;
    if (mc_out) *mc_out = journal_mc;
    return 1;
}

static void fmcb_integrated_progress(void *opaque, const char *stage, float progress, int detail)
{
    TitanInstallerState *s = (TitanInstallerState *)opaque;
    unsigned int mapped;
    (void)detail;
    if (s == 0) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    mapped = 96u + (unsigned int)(progress * 1.0f);
    if (mapped > 97u) mapped = 97u;
    s->progress = mapped;
    if (stage != 0 && (strstr(stage, "failed") != 0 || strstr(stage, "error") != 0))
        copy_string(s->status, sizeof(s->status), "Falha ao configurar a inicialização autônoma; preparando reversão...");
    else if (stage != 0 && strstr(stage, "done") != 0)
        copy_string(s->status, sizeof(s->status), "Inicialização autônoma configurada; verificando instalação...");
    else
        copy_string(s->status, sizeof(s->status), "Configurando inicialização autônoma...");
    s->frame++;
    render(s);
}

static int fmcb_restore_titan_iop(TitanInstallerState *s)
{
    TitanResult input_result;
    if (s == 0) return 0;
    if (!s->fmcb_integrated_started || s->fmcb_iop_restored) return 1;
    copy_string(s->status, sizeof(s->status), "Restaurando serviços do Project Titan...");
    render(s);
    input_result = TitanInput_Initialize(0, 0);
    if (input_result != TITAN_RESULT_OK && !TitanInput_IsIopReady()) {
        TitanInstallerDiag_Logf("ERROR", "FMCB_TITAN_IOP_RESTORE", "input_result=%d iop_ready=0", (int)input_result);
        return 0;
    }
    s->storage_ready = initialize_storage(s, TitanInput_UsesExtendedModules());
    s->fmcb_iop_restored = s->storage_ready ? 1 : 0;
    TitanInstallerDiag_Logf("INFO", "FMCB_TITAN_IOP_RESTORE", "input_result=%d storage_ready=%d usb=%s",
                            (int)input_result, s->storage_ready, s->usb_prefix);
    return s->fmcb_iop_restored;
}

static int fmcb_run_integrated(TitanInstallerState *s)
{
    int result;
    if (s == 0 || s->memory_card < 0 || s->memory_card > 1) return -EINVAL;
    if (!fmcb_write_journal(s)) return -EIO;
    s->fmcb_integrated_started = 1;
    s->fmcb_integrated_result = -1;
    s->fmcb_iop_restored = 0;
    TitanInstallerDiag_Logf("INFO", "FMCB_INTEGRATED_BEGIN",
                            "state=%s mc=%d rom=%u root=%s backend=builtin",
                            fmcb_state_name(s->fmcb_state), s->memory_card,
                            TitanInstallerModel_GetRomVersionCode(&s->console), s->fmcb_root_path);
    result = TitanFmcbBackend_Install(s->fmcb_root_path, s->memory_card,
                                      fmcb_integrated_progress, s);
    s->fmcb_integrated_result = result;
    TitanInstallerDiag_Logf(result >= 0 ? "INFO" : "ERROR", "FMCB_INTEGRATED_END",
                            "mc=%d result=%d backend=builtin", s->memory_card, result);
    return result;
}

static int fmcb_prepare_interrupted_recovery(TitanInstallerState *s)
{
    TitanInstallerInstallJob *job;
    int journal_mc = -1;
    int cleanup_result;

    if (s == 0 || s->fmcb_journal_path[0] == '\0' || !path_exists(s->fmcb_journal_path))
        return 0;

    /* The journal is written only after every Titan payload has been published
     * and immediately before the integrated FMCB engine starts. A surviving
     * journal therefore means the autonomous transaction did not reach commit.
     * Rehydrate the TINS table, remove only the FMCB files this clean-card
     * transaction was allowed to create, then roll Titan entries back. */
    if (!preflight_payload(s) || !fmcb_load_journal(s, &journal_mc)) {
        copy_string(s->error, sizeof(s->error),
                    "Foi detectada uma instalação autônoma interrompida, mas o journal de recuperação não pôde ser validado. Nenhuma nova instalação será iniciada.");
        TitanInstallerDiag_Logf("ERROR", "FMCB_RECOVERY_JOURNAL_INVALID", "path=%s", s->fmcb_journal_path);
        s->page = TITAN_INSTALLER_PAGE_ERROR;
        return -1;
    }

    s->memory_card = journal_mc;
    s->method = TITAN_INSTALLER_METHOD_AUTONOMOUS;
    s->fmcb_state = probe_fmcb_state(s->memory_card);
    s->page = TITAN_INSTALLER_PAGE_INSTALLING;
    s->progress = 1u;
    copy_string(s->status, sizeof(s->status), "Recuperando uma instalação autônoma interrompida...");
    TitanInstallerDiag_SetMemoryCard(s->memory_card);
    TitanInstallerDiag_Logf("WARN", "FMCB_INTERRUPTED_TXN_RECOVERY",
                            "journal=%s mc=%d entries=%u", s->fmcb_journal_path,
                            s->memory_card, s->manifest_count);

    s->fmcb_integrated_started = 1;
    s->fmcb_iop_restored = 0;
    cleanup_result = TitanFmcbBackend_Cleanup(s->fmcb_root_path, s->memory_card,
                                               fmcb_integrated_progress, s);
    TitanInstallerDiag_Logf(cleanup_result >= 0 ? "INFO" : "WARN", "FMCB_INTERRUPTED_CLEANUP",
                            "mc=%d result=%d", s->memory_card, cleanup_result);
    if (!fmcb_restore_titan_iop(s)) {
        copy_string(s->error, sizeof(s->error),
                    "A recuperação encontrou uma transação interrompida, mas não conseguiu restaurar os serviços do instalador após a limpeza autônoma.");
        s->page = TITAN_INSTALLER_PAGE_ERROR;
        return -1;
    }

    job = &s->install_job;
    memset(job, 0, sizeof(*job));
    job->container_fd = -1;
    job->stage_fd = -1;
    job->verify_fd = -1;
    job->rollback_index = s->manifest_count;
    job->phase = TITAN_INSTALL_JOB_ROLLBACK;
    copy_string(job->failure, sizeof(job->failure),
                "Uma instalação autônoma interrompida foi detectada e revertida com segurança.");
    copy_string(s->status, sizeof(s->status), "Restaurando o estado anterior com segurança...");
    /* IOP has already been restored for the asynchronous Titan rollback. */
    s->fmcb_integrated_started = 0;
    s->fmcb_iop_restored = 1;
    return 1;
}

static int install_job_step(TitanInstallerState *s)
{
    TitanInstallerInstallJob *job;
    TitanInstallerManifestEntry *e;
    unsigned int want;
    int io_result;

    if (s == 0) return -1;
    job = &s->install_job;
    TitanInstallerDiag_SetContext(
        job->entry_index < s->manifest_count ? job->entry_index + 1u : s->manifest_count,
        s->manifest_count,
        install_phase_name(job->phase),
        job->entry_index < s->manifest_count ? s->manifest[job->entry_index].path : "",
        s->progress
    );

    if (job->cancel_requested &&
        job->async_io.kind == TITAN_INSTALLER_ASYNC_NONE &&
        job->phase != TITAN_INSTALL_JOB_ROLLBACK &&
        job->phase != TITAN_INSTALL_JOB_SUCCESS &&
        job->phase != TITAN_INSTALL_JOB_FAILED) {
        install_job_begin_rollback(s, "A instalação foi cancelada e o estado anterior foi restaurado.");
        return 0;
    }

    switch (job->phase) {
        case TITAN_INSTALL_JOB_RECOVER:
            if (job->recovery_index >= s->manifest_count) {
                job->recovery_step = 0u;
                job->phase = TITAN_INSTALL_JOB_OPEN_CONTAINER;
                s->progress = 2u;
                copy_string(s->status, sizeof(s->status), "Abrindo TITAN_SYSTEM.TINS...");
                return 0;
            }
            e = &s->manifest[job->recovery_index];
            if (!install_entry_enabled(s, e)) {
                job->recovery_index++;
                job->recovery_step = 0u;
                return 0;
            }
            if (job->recovery_step == 0u) {
                transaction_destination(s, e, job->destination, sizeof(job->destination));
                install_build_temporary_path(s, job->recovery_index, job->destination, job->temporary, sizeof(job->temporary));
                job->backup[0] = '\0';
                job->recovery_step = 1u;
            }
            if (job->recovery_step == 1u) {
                if (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->temporary)) {
                    int rr = 0;
                    int a = install_async_remove_step(job, job->temporary, "ASYNC_RECOVERY_REMOVE_STAGE", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0 && path_exists(job->temporary))
                        TitanInstallerDiag_Logf("WARN", "RECOVERY_STALE_STAGE_PRESERVED", "path=%s result=%d", job->temporary, rr);
                }
                job->recovery_step = 2u;
            }
            if (job->recovery_step == 2u) {
                if (job->async_io.kind == TITAN_INSTALLER_ASYNC_RENAME) {
                    int rr = 0;
                    int a = install_async_rename_step(job, job->backup, job->destination,
                                                      "ASYNC_RECOVERY_RENAME_BACKUP", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0)
                        TitanInstallerDiag_Logf("WARN", "RECOVERY_BACKUP_RENAME_FAILED",
                                                "backup=%s destination=%s result=%d", job->backup, job->destination, rr);
                    job->recovery_step = 3u;
                } else {
                    if (!path_exists(job->destination) && job->backup[0] == '\0')
                        (void)install_find_recovery_backup(job->destination, job->backup, sizeof(job->backup));
                    if (job->backup[0] != '\0' && !path_exists(job->destination)) {
                        int rr = 0;
                        int a = install_async_rename_step(job, job->backup, job->destination,
                                                          "ASYNC_RECOVERY_RENAME_BACKUP", &rr);
                        if (a <= 0) return 0;
                        if (rr < 0)
                            TitanInstallerDiag_Logf("WARN", "RECOVERY_BACKUP_RENAME_FAILED",
                                                    "backup=%s destination=%s result=%d", job->backup, job->destination, rr);
                    }
                    job->recovery_step = 3u;
                }
            }
            job->recovery_index++;
            job->recovery_step = 0u;
            return 0;

        case TITAN_INSTALL_JOB_OPEN_CONTAINER:
            { int a = install_async_open_step(job, s->container_path, O_RDONLY, 0, "ASYNC_OPEN_TINS", &io_result); if (a <= 0) return 0; job->container_fd = io_result; }
            if (job->container_fd < 0) {
                install_job_begin_rollback(s, "Não foi possível abrir TITAN_SYSTEM.TINS durante a instalação.");
                return 0;
            }
            job->remaining = s->container_payload_base;
            job->cursor = 0u;
            job->phase = TITAN_INSTALL_JOB_SKIP_PREFIX;
            return 0;

        case TITAN_INSTALL_JOB_SKIP_PREFIX:
            if (job->remaining == 0u) {
                job->entry_index = 0u;
                job->phase = TITAN_INSTALL_JOB_PREPARE_ENTRY;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->container_fd, g_copy_buffer, want, s->container_path, "ASYNC_READ_TINS", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha ao posicionar o stream do TINS.");
                return 0;
            }
            job->remaining -= (unsigned int)io_result;
            return 0;

        case TITAN_INSTALL_JOB_PREPARE_ENTRY:
            if (job->entry_index >= s->manifest_count) {
                if (job->container_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                    int cr = 0;
                    int a = install_async_close_step(job, &job->container_fd, s->container_path, "ASYNC_CLOSE_TINS", &cr);
                    if (a <= 0) return 0;
                }
                if (s->method == TITAN_INSTALLER_METHOD_AUTONOMOUS && s->fmcb_state == TITAN_FMCB_NO_FMCB) {
                    job->phase = TITAN_INSTALL_JOB_FMCB_INTEGRATED;
                    s->progress = 96u;
                    copy_string(s->status, sizeof(s->status), "Configurando inicialização autônoma...");
                } else {
                    job->phase = TITAN_INSTALL_JOB_WRITE_COMPLETE;
                    s->progress = 97u;
                    copy_string(s->status, sizeof(s->status), "Gravando marcador de instalação concluída...");
                }
                return 0;
            }
            e = &s->manifest[job->entry_index];
            if (e->payload_offset < job->cursor) {
                install_job_begin_rollback(s, "A ordem dos payloads no TINS é inválida.");
                return 0;
            }
            if (e->payload_offset > job->cursor) {
                job->remaining = e->payload_offset - job->cursor;
                job->phase = TITAN_INSTALL_JOB_SKIP_GAP;
                return 0;
            }
            if (!install_entry_enabled(s, e)) {
                job->remaining = e->size;
                snprintf(s->status, sizeof(s->status), "Ignorando payload não selecionado: %.96s", e->path);
                job->phase = TITAN_INSTALL_JOB_SKIP_ENTRY;
                return 0;
            }
            job->phase = TITAN_INSTALL_JOB_PREPARE_STAGE;
            return 0;

        case TITAN_INSTALL_JOB_SKIP_GAP:
        case TITAN_INSTALL_JOB_SKIP_ENTRY:
            if (job->remaining == 0u) {
                if (job->phase == TITAN_INSTALL_JOB_SKIP_ENTRY) {
                    e = &s->manifest[job->entry_index];
                    if (e->unchanged && job->source_crc != e->crc32) {
                        install_job_begin_rollback(s, "CRC do payload no TINS falhou para um arquivo já instalado.");
                        return 0;
                    }
                    job->entry_index++;
                    job->current_entry_touched = 0;
                }
                job->phase = TITAN_INSTALL_JOB_PREPARE_ENTRY;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->container_fd, g_copy_buffer, want, s->container_path, "ASYNC_READ_TINS", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha durante a leitura sequencial do TINS.");
                return 0;
            }
            if (job->phase == TITAN_INSTALL_JOB_SKIP_ENTRY) {
                e = &s->manifest[job->entry_index];
                if (e->unchanged)
                    job->source_crc = installer_diag_crc32_update(job->source_crc, g_copy_buffer, (unsigned int)io_result, "CRC_TINS_UNCHANGED");
            }
            job->remaining -= (unsigned int)io_result;
            job->cursor += (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_PREPARE_STAGE:
            e = &s->manifest[job->entry_index];
            if (job->prepare_stage_step == 0u) {
                transaction_destination(s, e, job->destination, sizeof(job->destination));
                install_build_temporary_path(s, job->entry_index, job->destination, job->temporary, sizeof(job->temporary));
                e->had_existing = 0;
                e->published = 0;
                e->unchanged = 0;
                e->backup_transport_ready = 0;
                e->backup_slot = -1;
                job->source_crc = 0u;
                job->verify_crc = 0u;
                job->verify_size = 0u;
                job->existing_crc = 0u;
                job->existing_size = 0u;
                job->pending_size = 0u;
                job->pending_written = 0u;
                ensure_parent_dirs(job->destination);
                job->current_entry_touched = 1;
                job->prepare_stage_step = 1u;
            }
            if (job->prepare_stage_step == 1u) {
                int rr = 0;
                int a = install_async_remove_step(job, job->temporary, "ASYNC_REMOVE_STALE_STAGE", &rr);
                if (a <= 0) return 0;
                /* Missing stale staging is normal. Even if a legacy driver
                 * refuses deletion, O_TRUNC below is the authoritative test. */
                if (rr < 0)
                    TitanInstallerDiag_Logf("INFO", "STALE_STAGE_REMOVE_NONFATAL",
                                            "path=%s result=%d", job->temporary, rr);
                job->prepare_stage_step = 2u;
            }
            if (job->prepare_stage_step == 2u) {
                { int a = install_async_open_step(job, job->destination, O_RDONLY, 0, "ASYNC_OPEN_EXISTING", &io_result); if (a <= 0) return 0;
                  job->verify_fd = io_result; }
                if (job->verify_fd >= 0) {
                    e->had_existing = 1;
                    job->remaining = e->size;
                    snprintf(s->status, sizeof(s->status), "Verificando arquivo já instalado: %.86s", e->path);
                    job->phase = TITAN_INSTALL_JOB_CHECK_EXISTING_READ;
                    job->prepare_stage_step = 0u;
                    return 0;
                }
                e->had_existing = 0;
                job->prepare_stage_step = 3u;
            }
            { int a = install_output_open_step(job, job->temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666, "ASYNC_OPEN_STAGE_WRITE", &job->stage_fd, &job->stage_backend); if (a <= 0) return 0; }
            if (job->stage_fd < 0) {
                install_job_begin_rollback(s, "Não foi possível criar staging transacional.");
                return 0;
            }
            job->remaining = e->size;
            snprintf(s->status, sizeof(s->status), "Instalando: %.104s", e->path);
            job->phase = TITAN_INSTALL_JOB_READ_SOURCE;
            job->prepare_stage_step = 0u;
            return 0;

        case TITAN_INSTALL_JOB_CHECK_EXISTING_READ:
            e = &s->manifest[job->entry_index];
            if (job->remaining == 0u) {
                job->phase = TITAN_INSTALL_JOB_CHECK_EXISTING_EOF;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, want, job->destination, "ASYNC_READ_EXISTING", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                if (job->verify_fd >= 0) { (void)install_close_sync_compat(&job->verify_fd, job->destination, "CLOSE_EXISTING"); }
                job->prepare_stage_step = 3u;
                job->phase = TITAN_INSTALL_JOB_PREPARE_STAGE;
                return 0;
            }
            job->existing_crc = installer_diag_crc32_update(job->existing_crc, g_copy_buffer, (unsigned int)io_result, "CRC_EXISTING");
            job->existing_size += (unsigned int)io_result;
            job->remaining -= (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_CHECK_EXISTING_EOF: {
            e = &s->manifest[job->entry_index];
            if (job->close_step == 0u) {
                { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, 1u, job->destination, "ASYNC_READ_EXISTING_EOF", &io_result); if (a <= 0) return 0; }
                job->eof_result = io_result;
                job->close_step = 1u;
                return 0;
            }
            if (job->close_step == 1u) {
                int cr = 0;
                int a = install_async_close_step(job, &job->verify_fd, job->destination, "ASYNC_CLOSE_EXISTING", &cr);
                if (a <= 0) return 0;
                job->close_step = 0u;
            }
            io_result = job->eof_result;
            if (io_result == 0 && job->existing_size == e->size && job->existing_crc == e->crc32) {
                e->unchanged = 1;
                TitanInstallerDiag_Logf(
                    "INFO", "ENTRY_UNCHANGED",
                    "entry=%u/%u path=%s size=%u crc=0x%08X",
                    job->entry_index + 1u, s->manifest_count, e->path, e->size, e->crc32
                );
                /* The original estimate included one staging write for this
                 * entry. Identical destinations skip that write entirely. */
                if (job->work_total > (unsigned long long)e->size)
                    job->work_total -= (unsigned long long)e->size;
                job->remaining = e->size;
                job->source_crc = 0u;
                snprintf(s->status, sizeof(s->status), "Já está atualizado: %.108s", e->path);
                job->phase = TITAN_INSTALL_JOB_SKIP_ENTRY;
                return 0;
            }
            job->prepare_stage_step = 3u;
            job->phase = TITAN_INSTALL_JOB_PREPARE_STAGE;
            return 0;
        }

        case TITAN_INSTALL_JOB_READ_SOURCE:
            e = &s->manifest[job->entry_index];
            if (job->remaining == 0u) {
                if (job->stage_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                    int cr = 0;
                    int a = install_output_close_step(job, &job->stage_fd, &job->stage_backend, job->temporary, "ASYNC_CLOSE_STAGE", &cr);
                    if (a <= 0) return 0;
                }
                if (job->source_crc != e->crc32) {
                    TitanInstallerDiag_Logf(
                        "ERROR", "SOURCE_CRC_MISMATCH",
                        "entry=%u/%u path=%s size=%u expected_crc=0x%08X actual_crc=0x%08X",
                        job->entry_index + 1u, s->manifest_count, e->path, e->size, e->crc32, job->source_crc
                    );
                    install_job_begin_rollback(s, "CRC do arquivo de origem no TINS não corresponde ao manifesto.");
                    return 0;
                }
                if (!install_entry_requires_full_verify(e)) {
                    unsigned int staged_size = 0u;
                    if (!install_stat_size(job->temporary, &staged_size) || staged_size != e->size) {
                        install_job_begin_rollback(s, "O staging não corresponde ao tamanho esperado após a gravação.");
                        return 0;
                    }
                    job->phase = TITAN_INSTALL_JOB_PUBLISH_ENTRY;
                    return 0;
                }
                job->phase = TITAN_INSTALL_JOB_VERIFY_OPEN;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->container_fd, g_copy_buffer, want, s->container_path, "ASYNC_READ_TINS", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha lendo um payload do TITAN_SYSTEM.TINS.");
                return 0;
            }
            job->source_crc = installer_diag_crc32_update(job->source_crc, g_copy_buffer, (unsigned int)io_result, "CRC_TINS_SOURCE");
            job->remaining -= (unsigned int)io_result;
            job->cursor += (unsigned int)io_result;
            job->pending_size = (unsigned int)io_result;
            job->pending_written = 0u;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            job->phase = TITAN_INSTALL_JOB_WRITE_STAGE;
            return 0;

        case TITAN_INSTALL_JOB_WRITE_STAGE:
            if (job->pending_written >= job->pending_size) {
                job->phase = TITAN_INSTALL_JOB_READ_SOURCE;
                return 0;
            }
            { int a = install_output_write_step(job, job->stage_fd, job->stage_backend,
                                                g_copy_buffer + job->pending_written,
                                                job->pending_size - job->pending_written,
                                                job->temporary, "ASYNC_WRITE_STAGE", &io_result);
              if (a <= 0) return 0; }
            if (io_result <= 0) {
                install_job_begin_rollback(s, "Falha gravando um arquivo de staging .TITAN.NEW.");
                return 0;
            }
            job->pending_written += (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_VERIFY_OPEN:
            e = &s->manifest[job->entry_index];
            { int a = install_async_open_step(job, job->temporary, O_RDONLY, 0, "ASYNC_OPEN_STAGE_VERIFY", &io_result); if (a <= 0) return 0;
                  job->verify_fd = io_result; }
            if (job->verify_fd < 0) {
                install_job_begin_rollback(s, "Não foi possível reler o staging para validação.");
                return 0;
            }
            job->remaining = e->size;
            job->verify_crc = 0u;
            job->verify_size = 0u;
            snprintf(s->status, sizeof(s->status), "Validando %.116s", e->path);
            job->phase = TITAN_INSTALL_JOB_VERIFY_READ;
            return 0;

        case TITAN_INSTALL_JOB_VERIFY_READ:
            if (job->remaining == 0u) {
                job->phase = TITAN_INSTALL_JOB_VERIFY_EOF;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, want, job->temporary, "ASYNC_READ_STAGE_VERIFY", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha relendo o staging para CRC.");
                return 0;
            }
            job->verify_crc = installer_diag_crc32_update(job->verify_crc, g_copy_buffer, (unsigned int)io_result, "CRC_STAGING");
            job->verify_size += (unsigned int)io_result;
            job->remaining -= (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_VERIFY_EOF: {
            e = &s->manifest[job->entry_index];
            if (job->close_step == 0u) {
                { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, 1u, job->temporary, "ASYNC_READ_STAGE_EOF", &io_result); if (a <= 0) return 0; }
                job->eof_result = io_result;
                job->close_step = 1u;
                return 0;
            }
            if (job->close_step == 1u) {
                int cr = 0;
                int a = install_async_close_step(job, &job->verify_fd, job->temporary, "ASYNC_CLOSE_VERIFY", &cr);
                if (a <= 0) return 0;
                job->close_step = 0u;
            }
            io_result = job->eof_result;
            if (io_result != 0 || job->verify_size != e->size || job->verify_crc != e->crc32) {
                TitanInstallerDiag_Logf(
                    "ERROR", "STAGING_VERIFY_MISMATCH",
                    "entry=%u/%u path=%s expected_size=%u actual_size=%u expected_crc=0x%08X actual_crc=0x%08X eof_result=%d",
                    job->entry_index + 1u, s->manifest_count, e->path, e->size, job->verify_size,
                    e->crc32, job->verify_crc, io_result
                );
                install_job_begin_rollback(s, "O staging não passou na validação de tamanho/CRC.");
                return 0;
            }
            job->phase = TITAN_INSTALL_JOB_PUBLISH_ENTRY;
            return 0;
        }

        case TITAN_INSTALL_JOB_PUBLISH_ENTRY:
            e = &s->manifest[job->entry_index];
            if (e->had_existing) {
                if (job->backup[0] == '\0') {
                    int ar = install_allocate_backup_path_step(s, e, job->destination, job->backup, sizeof(job->backup));
                    if (ar == 0) return 0;
                    if (ar < 0) {
                        install_job_begin_rollback(s, "Não há um slot livre para backup transacional; backups antigos foram preservados.");
                        return 0;
                    }
                    TitanInstallerDiag_Logf("INFO", "BACKUP_ALLOCATED", "entry=%u/%u path=%s backup=%s",
                                            job->entry_index + 1u, s->manifest_count, e->path, job->backup);
                }
                if (install_path_is_mc(job->destination)) {
                    install_build_backup_transport_path(s, job->entry_index,
                                                        job->backup_transport, sizeof(job->backup_transport));
                    job->backup_transport_phase = 1u;
                    TitanInstallerDiag_Logf("INFO", "MC_BACKUP_TRANSPORT",
                                            "source=%s transport=%s backup=%s",
                                            job->destination, job->backup_transport, job->backup);
                    job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_OPEN;
                    return 0;
                } else {
                    int rr = 0;
                    int a = install_async_rename_step(job, job->destination, job->backup, "ASYNC_RENAME_BACKUP", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0) {
                        job->backup_transport_phase = 0u;
                        job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_OPEN;
                        return 0;
                    }
                }
            }
            job->phase = TITAN_INSTALL_JOB_PUBLISH_STAGING;
            return 0;

        case TITAN_INSTALL_JOB_BACKUP_COPY_OPEN: {
            const char *copy_source = install_backup_copy_source(job);
            const char *copy_destination = install_backup_copy_destination(job);
            if (job->open_step == 0u) {
                unsigned int copy_size = 0u;
                /* The existing-file scan already counted the MC source exactly.
                 * Reusing that value avoids a synchronous newlib stat(mc0:) in
                 * the critical transport path. */
                if (job->backup_transport_phase == 1u && install_path_is_mc(copy_source))
                    copy_size = job->existing_size;
                else if (!install_stat_size(copy_source, &copy_size)) {
                    install_job_begin_rollback(s, "Falha obtendo o tamanho do arquivo que seria preservado.");
                    return 0;
                }
                job->verify_size = copy_size;
                job->open_step = 1u;
            }
            if (job->open_step == 1u) {
                { int a = install_async_open_step(job, copy_source, O_RDONLY, 0,
                                                  "ASYNC_OPEN_BACKUP_SOURCE", &io_result);
                  if (a <= 0) return 0;
                  job->verify_fd = io_result; }
                if (job->verify_fd < 0) {
                    job->open_step = 0u;
                    install_job_begin_rollback(s, "Falha abrindo o arquivo anterior para backup.");
                    return 0;
                }
                job->open_step = 2u;
            }
            if (job->open_step == 2u) {
                { int a = install_output_open_step(job, copy_destination, O_WRONLY | O_CREAT | O_TRUNC, 0666,
                                                  "ASYNC_OPEN_BACKUP_WRITE", &job->stage_fd, &job->stage_backend);
                  if (a <= 0) return 0; }
                if (job->stage_fd < 0) {
                    (void)install_close_sync_compat(&job->verify_fd, copy_source, "CLOSE_BACKUP_SOURCE_FAILED");
                    job->open_step = 0u;
                    install_job_begin_rollback(s, "Falha criando backup transacional por cópia.");
                    return 0;
                }
                job->open_step = 3u;
            }
            job->remaining = job->verify_size;
            job->existing_size = 0u;
            job->pending_size = 0u;
            job->pending_written = 0u;
            job->work_total += (unsigned long long)job->verify_size * 2ull;
            job->open_step = 0u;
            job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_READ;
            return 0;
        }

        case TITAN_INSTALL_JOB_BACKUP_COPY_READ:
            if (job->remaining == 0u) {
                if (job->close_step == 0u) {
                    if (job->verify_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                        int cr = 0; int a = install_async_close_step(job, &job->verify_fd, install_backup_copy_source(job), "ASYNC_CLOSE_BACKUP_SOURCE", &cr);
                        if (a <= 0) return 0;
                    }
                    job->close_step = 1u;
                }
                if (job->close_step == 1u) {
                    if (job->stage_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                        int cr = 0; int a = install_output_close_step(job, &job->stage_fd, &job->stage_backend, install_backup_copy_destination(job), "ASYNC_CLOSE_BACKUP_WRITE", &cr);
                        if (a <= 0) return 0;
                    }
                    job->close_step = 0u;
                }
                job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_FINISH;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, want, install_backup_copy_source(job), "ASYNC_READ_BACKUP_SOURCE", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha lendo o arquivo anterior durante backup transacional.");
                return 0;
            }
            job->remaining -= (unsigned int)io_result;
            job->pending_size = (unsigned int)io_result;
            job->pending_written = 0u;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_WRITE;
            return 0;

        case TITAN_INSTALL_JOB_BACKUP_COPY_WRITE:
            if (job->pending_written >= job->pending_size) {
                job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_READ;
                return 0;
            }
            { int a = install_output_write_step(job, job->stage_fd, job->stage_backend, g_copy_buffer + job->pending_written, job->pending_size - job->pending_written, install_backup_copy_destination(job), "ASYNC_WRITE_BACKUP", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0) {
                install_job_begin_rollback(s, "Falha gravando backup transacional.");
                return 0;
            }
            job->pending_written += (unsigned int)io_result;
            job->existing_size += (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_BACKUP_COPY_FINISH: {
            const char *copy_destination = install_backup_copy_destination(job);
            unsigned int backup_size = 0u;
            int size_ok = job->existing_size == job->verify_size;
            if (!install_path_is_mc(copy_destination)) {
                size_ok = size_ok && install_stat_size(copy_destination, &backup_size) && backup_size == job->verify_size;
            }
            if (!size_ok) {
                install_job_begin_rollback(s, "Backup transacional por cópia ficou incompleto.");
                return 0;
            }
            if (job->backup_transport_phase == 1u) {
                /* First hop: existing MC file -> USB transport. Only now is the
                 * transport authoritative for rollback; a partial first-hop
                 * file must never be restored over the still-intact MC file. */
                e = &s->manifest[job->entry_index];
                e->backup_transport_ready = 1;
                job->backup_transport_phase = 2u;
                job->open_step = 0u;
                job->phase = TITAN_INSTALL_JOB_BACKUP_COPY_OPEN;
                return 0;
            }
            if (job->backup_transport_phase == 2u) {
                job->backup_transport_phase = 0u;
                job->phase = TITAN_INSTALL_JOB_PUBLISH_STAGING;
                return 0;
            }
            if (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->destination)) {
                int rr = 0;
                int a = install_async_remove_step(job, job->destination, "ASYNC_REMOVE_BACKUP_SOURCE", &rr);
                if (a <= 0) return 0;
                if (rr < 0 && path_exists(job->destination)) {
                    install_job_begin_rollback(s, "Backup foi criado, mas o arquivo anterior não pôde ser removido.");
                    return 0;
                }
            }
            job->phase = TITAN_INSTALL_JOB_PUBLISH_STAGING;
            return 0;
        }

        case TITAN_INSTALL_JOB_PUBLISH_STAGING:
            /* rom0:MCSERV does not implement mcRename(). BuildFix5 also avoids
             * BF4's MC->MC stream copy: MC-bound staging lives on USB, so final
             * publication is USB->MC with only one libmc descriptor active. */
            if (install_path_is_mc(job->destination)) {
                TitanInstallerDiag_Logf("INFO", "MC_PUBLISH_STREAM",
                                        "staging=%s destination=%s reason=mcserv-no-rename",
                                        job->temporary, job->destination);
                job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_OPEN;
                return 0;
            }
            {
                int rr = 0;
                int a = install_async_rename_step(job, job->temporary, job->destination, "ASYNC_RENAME_PUBLISH", &rr);
                if (a <= 0) return 0;
                if (rr >= 0) {
                    e = &s->manifest[job->entry_index];
                    e->published = 1;
                    TitanInstallerDiag_Logf("INFO", "ENTRY_PUBLISHED",
                                            "entry=%u/%u path=%s destination=%s method=fileXio-async-rename size=%u crc=0x%08X",
                                            job->entry_index + 1u, s->manifest_count, e->path, job->destination, e->size, e->crc32);
                    job->entry_index++;
                    job->current_entry_touched = 0;
                    job->backup[0] = '\0';
                    job->phase = TITAN_INSTALL_JOB_PREPARE_ENTRY;
                    return 0;
                }
            }
            job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_OPEN;
            return 0;

        case TITAN_INSTALL_JOB_PUBLISH_COPY_OPEN: {
            if (job->open_step == 0u) {
                /* For Memory Card publication, deletion is unnecessary and can
                 * be actively misleading: newlib stat(mc0:) reported BOOT.ELF
                 * present on hardware while libmc mcDelete returned -4.  The
                 * authoritative operation is the following libmc open with
                 * O_WRONLY|O_CREAT|O_TRUNC, which safely creates or replaces the
                 * destination after any required transactional backup. */
                if (!install_path_is_mc(job->destination) &&
                    (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->destination))) {
                    int rr = 0;
                    int a = install_async_remove_step(job, job->destination, "ASYNC_REMOVE_PUBLISH_DEST", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0 && path_exists(job->destination)) {
                        install_job_begin_rollback(s, "Não foi possível limpar destino antes da publicação por streaming.");
                        return 0;
                    }
                }
                job->open_step = 1u;
            }
            if (job->open_step == 1u) {
                unsigned int copy_size = 0u;
                if (!install_stat_size(job->temporary, &copy_size)) {
                    install_job_begin_rollback(s, "Não foi possível medir staging para publicação por streaming.");
                    return 0;
                }
                job->verify_size = copy_size;
                job->open_step = 2u;
            }
            if (job->open_step == 2u) {
                { int a = install_async_open_step(job, job->temporary, O_RDONLY, 0,
                                                  "ASYNC_OPEN_PUBLISH_SOURCE", &io_result);
                  if (a <= 0) return 0;
                  job->verify_fd = io_result; }
                if (job->verify_fd < 0) {
                    job->open_step = 0u;
                    install_job_begin_rollback(s, "Falha abrindo staging para publicação por streaming.");
                    return 0;
                }
                job->open_step = 3u;
            }
            if (job->open_step == 3u) {
                { int a = install_output_open_step(job, job->destination, O_WRONLY | O_CREAT | O_TRUNC, 0666,
                                                  "ASYNC_OPEN_PUBLISH_DEST", &job->stage_fd, &job->stage_backend);
                  if (a <= 0) return 0; }
                if (job->stage_fd < 0) {
                    (void)install_close_sync_compat(&job->verify_fd, job->temporary, "CLOSE_PUBLISH_SOURCE_FAILED");
                    job->open_step = 0u;
                    install_job_begin_rollback(s, "Falha criando destino durante publicação por streaming.");
                    return 0;
                }
                job->open_step = 4u;
            }
            job->remaining = job->verify_size;
            job->existing_size = 0u;
            job->pending_size = 0u;
            job->pending_written = 0u;
            job->work_total += (unsigned long long)job->verify_size * 2ull;
            job->open_step = 0u;
            snprintf(s->status, sizeof(s->status), "Publicando por streaming: %.92s", s->manifest[job->entry_index].path);
            job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_READ;
            return 0;
        }

        case TITAN_INSTALL_JOB_PUBLISH_COPY_READ:
            if (job->remaining == 0u) {
                if (job->close_step == 0u) {
                    if (job->verify_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                        int cr = 0; int a = install_async_close_step(job, &job->verify_fd, job->temporary, "ASYNC_CLOSE_PUBLISH_SOURCE", &cr);
                        if (a <= 0) return 0;
                    }
                    job->close_step = 1u;
                }
                if (job->close_step == 1u) {
                    if (job->stage_fd >= 0 || job->async_io.kind == TITAN_INSTALLER_ASYNC_CLOSE) {
                        int cr = 0; int a = install_output_close_step(job, &job->stage_fd, &job->stage_backend, job->destination, "ASYNC_CLOSE_PUBLISH_DEST", &cr);
                        if (a <= 0) return 0;
                    }
                    job->close_step = 0u;
                }
                job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_FINISH;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, want, job->temporary, "ASYNC_READ_PUBLISH_SOURCE", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                install_job_begin_rollback(s, "Falha lendo staging durante publicação por streaming.");
                return 0;
            }
            job->remaining -= (unsigned int)io_result;
            job->pending_size = (unsigned int)io_result;
            job->pending_written = 0u;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_WRITE;
            return 0;

        case TITAN_INSTALL_JOB_PUBLISH_COPY_WRITE:
            if (job->pending_written >= job->pending_size) {
                job->phase = TITAN_INSTALL_JOB_PUBLISH_COPY_READ;
                return 0;
            }
            { int a = install_output_write_step(job, job->stage_fd, job->stage_backend, g_copy_buffer + job->pending_written, job->pending_size - job->pending_written, job->destination, "ASYNC_WRITE_PUBLISH", &io_result); if (a <= 0) return 0; }
            if (io_result <= 0) {
                install_job_begin_rollback(s, "Falha gravando destino durante publicação por streaming.");
                return 0;
            }
            job->pending_written += (unsigned int)io_result;
            job->existing_size += (unsigned int)io_result;
            job->work_done += (unsigned int)io_result;
            install_job_update_progress(s);
            return 0;

        case TITAN_INSTALL_JOB_PUBLISH_COPY_FINISH: {
            unsigned int final_size = 0u;
            int size_ok;
            e = &s->manifest[job->entry_index];
            size_ok = job->existing_size == job->verify_size;
            if (!install_path_is_mc(job->destination))
                size_ok = size_ok && install_stat_size(job->destination, &final_size) && final_size == job->verify_size;
            if (!size_ok) {
                install_job_begin_rollback(s, "Publicação por streaming ficou incompleta.");
                return 0;
            }
            if (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->temporary)) {
                int rr = 0;
                int a = install_async_remove_step(job, job->temporary, "ASYNC_REMOVE_PUBLISHED_STAGE", &rr);
                if (a <= 0) return 0;
                if (rr < 0 && path_exists(job->temporary))
                    TitanInstallerDiag_Logf("WARN", "PUBLISHED_STAGE_PRESERVED", "temporary=%s result=%d", job->temporary, rr);
            }
            e->published = 1;
            TitanInstallerDiag_Logf("INFO", "ENTRY_PUBLISHED",
                                    "entry=%u/%u path=%s destination=%s method=stream-copy size=%u crc=0x%08X",
                                    job->entry_index + 1u, s->manifest_count, e->path, job->destination, e->size, e->crc32);
            job->entry_index++;
            job->current_entry_touched = 0;
            job->backup[0] = '\0';
            job->phase = TITAN_INSTALL_JOB_PREPARE_ENTRY;
            return 0;
        }

        case TITAN_INSTALL_JOB_FMCB_INTEGRATED: {
            int fmcb_result;
            if (!TitanFmcbBackend_IsIntegrated() || !path_exists(s->fmcb_system_path) ||
                !path_exists(s->fmcb_cnf_path) || !path_exists(s->fmcb_space_path)) {
                install_job_begin_rollback(s, "Backend ou material da inicialização autônoma desapareceu antes da ativação.");
                return 0;
            }
            fmcb_result = fmcb_run_integrated(s);
            if (fmcb_result < 0) {
                /* The integrated FMCB backend replaces the IOP image.  Restore
                 * Titan's storage/input stack before touching staged/backed-up
                 * Titan files; otherwise rollback would run against stale RPC
                 * clients from the pre-FMCB IOP generation. */
                if (!fmcb_restore_titan_iop(s))
                    TitanInstallerDiag_Logf("ERROR", "FMCB_ROLLBACK_IOP_RESTORE",
                                            "result=%d rollback_will_fail_closed=1", fmcb_result);
                install_job_begin_rollback(s, "A configuração da inicialização autônoma falhou; as alterações Titan serão revertidas.");
                return 0;
            }
            job->phase = TITAN_INSTALL_JOB_WRITE_COMPLETE;
            s->progress = 97u;
            copy_string(s->status, sizeof(s->status), "Inicialização autônoma configurada; concluindo commit...");
            return 0;
        }

        case TITAN_INSTALL_JOB_WRITE_COMPLETE: {
            /* BuildFix6: do not leave the final INSTALL.COMPLETE commit marker
             * behind an unbounded FileXio NOWAIT request. Real hardware reached
             * this phase with all 84 payloads published and then remained at
             * 97% forever while fileXioWaitAsync(FXIO_NOWAIT) never completed.
             * The marker is tiny and is written only after the TINS stream and
             * every payload descriptor are closed, so use one short POSIX
             * open/write/close transaction here. This deliberately trades a
             * possible brief final-frame pause for guaranteed forward progress
             * and does not alter payload staging/publication. */
            static const char marker[] = "Project Titan Magenta Edition\ninstaller=" TITAN_INSTALLER_VERSION "\nstatus=complete\n";
            int fd;
            int wr;
            int cr;
            int saved_errno;
            uint64_t close_started;

            ensure_parent_dirs(s->complete_path);
            fd = installer_diag_open(s->complete_path, O_WRONLY | O_CREAT | O_TRUNC, 0666,
                                     "OPEN_COMPLETE_MARKER_POSIX");
            if (fd < 0) {
                install_job_begin_rollback(s, "Falha ao criar INSTALL.COMPLETE.");
                return 0;
            }

            wr = installer_diag_write(fd, marker, sizeof(marker)-1u, s->complete_path,
                                      "WRITE_COMPLETE_MARKER_POSIX");
            if (wr != (int)(sizeof(marker)-1u)) {
                (void)close(fd);
                install_job_begin_rollback(s, "Falha ao gravar INSTALL.COMPLETE.");
                return 0;
            }

            errno = 0;
            close_started = TitanTime_NowMilliseconds();
            cr = close(fd);
            saved_errno = cr < 0 ? errno : 0;
            TitanInstallerDiag_RecordIo("CLOSE_COMPLETE_MARKER_POSIX", s->complete_path,
                                        0u, cr, saved_errno,
                                        TitanTime_NowMilliseconds() - close_started);
            if (cr < 0) {
                install_job_begin_rollback(s, "Falha ao fechar INSTALL.COMPLETE.");
                return 0;
            }

            TitanInstallerDiag_Logf("INFO", "COMPLETE_MARKER_COMMITTED",
                                    "path=%s bytes=%u backend=posix",
                                    s->complete_path, (unsigned int)(sizeof(marker)-1u));
            job->cleanup_step = 0u;
            job->cleanup_index = 0u;
            job->phase = TITAN_INSTALL_JOB_CLEAN_BACKUPS;
            s->progress = 98u;
            copy_string(s->status, sizeof(s->status), "Finalizando transação...");
            return 0;
        }

        case TITAN_INSTALL_JOB_CLEAN_BACKUPS:
            if (job->cleanup_index >= s->manifest_count) {
                if (s->fmcb_integrated_started && !s->fmcb_iop_restored) {
                    if (!fmcb_restore_titan_iop(s))
                        TitanInstallerDiag_Logf("WARN", "FMCB_TITAN_IOP_RESTORE_DEFERRED", "installation_committed=1 reboot_required=1");
                }
                s->progress = 100u;
                job->phase = TITAN_INSTALL_JOB_SUCCESS;
                copy_string(s->status, sizeof(s->status), "Instalação validada com sucesso.");
                if (s->fmcb_journal_path[0] != '\0') (void)remove(s->fmcb_journal_path);
                TitanInstallerDiag_Logf("INFO", "INSTALL_COMPLETE", "entries=%u work_done=%llu work_total=%llu FMCB_INSTALLED=%d TITAN_BOOT_ENTRY_INSTALLED=%d VERIFY_PASS=1 TRANSACTION_COMMITTED=1",
                                        s->manifest_count, job->work_done, job->work_total,
                                        s->method == TITAN_INSTALLER_METHOD_AUTONOMOUS ? 1 : 0,
                                        s->method == TITAN_INSTALLER_METHOD_AUTONOMOUS ? 1 : 0);
                return 1;
            }
            e = &s->manifest[job->cleanup_index];
            if (!install_entry_enabled(s, e)) {
                job->cleanup_index++;
                job->cleanup_step = 0u;
                return 0;
            }
            if (job->cleanup_step == 0u) {
                transaction_destination(s, e, job->destination, sizeof(job->destination));
                install_build_backup_path(e, job->destination, job->backup, sizeof(job->backup));
                install_build_backup_transport_path(s, job->cleanup_index,
                                                    job->backup_transport, sizeof(job->backup_transport));
                job->cleanup_step = 1u;
            }
            if (job->cleanup_step == 1u && install_path_is_mc(job->destination) &&
                job->backup_transport[0] != '\0' &&
                (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->backup_transport))) {
                int rr = 0;
                int a = install_async_remove_step(job, job->backup_transport, "ASYNC_CLEAN_MC_BACKUP_TRANSPORT", &rr);
                if (a <= 0) return 0;
                if (rr < 0 && path_exists(job->backup_transport))
                    TitanInstallerDiag_Logf("WARN", "MC_BACKUP_TRANSPORT_PRESERVED",
                                            "transport=%s result=%d", job->backup_transport, rr);
                job->cleanup_step = 2u;
            } else if (job->cleanup_step == 1u) {
                job->cleanup_step = 2u;
            }
            if (job->cleanup_step == 2u && e->backup_slot >= 0 && job->backup[0] != '\0' &&
                (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE || path_exists(job->backup))) {
                int rr = 0;
                int a = install_async_remove_step(job, job->backup, "ASYNC_CLEAN_BACKUP", &rr);
                if (a <= 0) return 0;
                if (rr < 0 && path_exists(job->backup))
                    TitanInstallerDiag_Logf("WARN", "CLEAN_BACKUP_PRESERVED", "backup=%s result=%d", job->backup, rr);
            }
            job->cleanup_index++;
            job->cleanup_step = 0u;
            s->progress = 98u + (job->cleanup_index * 2u) / (s->manifest_count ? s->manifest_count : 1u);
            return 0;

        case TITAN_INSTALL_JOB_ROLLBACK:
            if (job->rollback_index == 0u) {
                if (s->fmcb_integrated_started && !s->fmcb_iop_restored)
                    (void)fmcb_restore_titan_iop(s);
                job->phase = TITAN_INSTALL_JOB_FAILED;
                copy_string(s->error, sizeof(s->error), job->failure);
                return -1;
            }
            e = &s->manifest[job->rollback_index - 1u];
            if (!install_entry_enabled(s, e) || e->unchanged) {
                job->rollback_index--;
                job->rollback_step = 0u;
                return 0;
            }
            if (job->rollback_step == 0u) {
                transaction_destination(s, e, job->destination, sizeof(job->destination));
                install_build_temporary_path(s, job->rollback_index - 1u, job->destination, job->temporary, sizeof(job->temporary));
                install_build_backup_path(e, job->destination, job->backup, sizeof(job->backup));
                install_build_backup_transport_path(s, job->rollback_index - 1u,
                                                    job->backup_transport, sizeof(job->backup_transport));
                job->rollback_step = 1u;
            }
            if (job->rollback_step == 1u) {
                if (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE) {
                    int rr = 0;
                    int a = install_async_remove_step(job, job->temporary, "ASYNC_ROLLBACK_REMOVE_STAGE", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0 && path_exists(job->temporary))
                        TitanInstallerDiag_Logf("WARN", "ROLLBACK_STAGE_PRESERVED", "path=%s result=%d", job->temporary, rr);
                    job->rollback_step = 2u;
                } else if (path_exists(job->temporary)) {
                    int rr = 0;
                    int a = install_async_remove_step(job, job->temporary, "ASYNC_ROLLBACK_REMOVE_STAGE", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0 && path_exists(job->temporary))
                        TitanInstallerDiag_Logf("WARN", "ROLLBACK_STAGE_PRESERVED", "path=%s result=%d", job->temporary, rr);
                    job->rollback_step = 2u;
                } else {
                    job->rollback_step = 2u;
                }
            }
            if (job->rollback_step == 2u) {
                /* If an MC destination had an older file, the USB transport is
                 * authoritative for rollback. Do not delete/stat the MC file
                 * first: restoration opens it with O_TRUNC, so a pre-delete is
                 * unnecessary and was unreliable on the real MCSERV path. */
                if (install_path_is_mc(job->destination) && e->had_existing && e->backup_transport_ready &&
                    job->backup_transport[0] != '\0' && path_exists(job->backup_transport)) {
                    job->rollback_step = 3u;
                } else if (job->async_io.kind == TITAN_INSTALLER_ASYNC_REMOVE) {
                    int rr = 0;
                    int a = install_async_remove_step(job, job->destination, "ASYNC_ROLLBACK_REMOVE_DEST", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0 && !install_path_is_mc(job->destination) && path_exists(job->destination))
                        TitanInstallerDiag_Logf("WARN", "ROLLBACK_DEST_PRESERVED", "path=%s result=%d", job->destination, rr);
                    job->rollback_step = 3u;
                } else {
                    int should_remove = 0;
                    if (!e->unchanged) {
                        if (e->published) should_remove = install_path_is_mc(job->destination) ? 1 : path_exists(job->destination);
                        else if (e->had_existing && job->backup[0] != '\0' && !install_path_is_mc(job->destination) && path_exists(job->backup))
                            should_remove = path_exists(job->destination);
                        else if (!e->had_existing) should_remove = install_path_is_mc(job->destination) ? 1 : path_exists(job->destination);
                    }
                    if (should_remove) {
                        int rr = 0;
                        int a = install_async_remove_step(job, job->destination, "ASYNC_ROLLBACK_REMOVE_DEST", &rr);
                        if (a <= 0) return 0;
                        if (rr < 0 && !install_path_is_mc(job->destination) && path_exists(job->destination))
                            TitanInstallerDiag_Logf("WARN", "ROLLBACK_DEST_PRESERVED", "path=%s result=%d", job->destination, rr);
                    }
                    job->rollback_step = 3u;
                }
            }
            if (job->rollback_step == 3u) {
                if (!e->unchanged && e->had_existing && install_path_is_mc(job->destination)) {
                    if (e->backup_transport_ready && job->backup_transport[0] != '\0' &&
                        path_exists(job->backup_transport)) {
                        job->phase = TITAN_INSTALL_JOB_ROLLBACK_RESTORE_OPEN;
                        return 0;
                    }
                    /* No complete USB backup means publication never had a safe
                     * rollback image. Preserve the original MC destination as-is
                     * rather than trying unsupported mcRename on a partial backup. */
                    job->rollback_step = 4u;
                } else if (job->async_io.kind == TITAN_INSTALLER_ASYNC_RENAME) {
                    int rr = 0;
                    int a = install_async_rename_step(job, job->backup, job->destination,
                                                      "ASYNC_ROLLBACK_RESTORE_RENAME", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0)
                        TitanInstallerDiag_Logf("ERROR", "ROLLBACK_RESTORE_RENAME_FAILED",
                                                "backup=%s destination=%s result=%d backup_preserved=1",
                                                job->backup, job->destination, rr);
                    job->rollback_step = 4u;
                } else if (!e->unchanged && e->had_existing && job->backup[0] != '\0' && path_exists(job->backup)) {
                    int rr = 0;
                    int a = install_async_rename_step(job, job->backup, job->destination,
                                                      "ASYNC_ROLLBACK_RESTORE_RENAME", &rr);
                    if (a <= 0) return 0;
                    if (rr < 0)
                        TitanInstallerDiag_Logf("ERROR", "ROLLBACK_RESTORE_RENAME_FAILED",
                                                "backup=%s destination=%s result=%d backup_preserved=1",
                                                job->backup, job->destination, rr);
                    job->rollback_step = 4u;
                } else {
                    job->rollback_step = 4u;
                }
            }
            e->published = 0;
            job->rollback_index--;
            job->rollback_step = 0u;
            return 0;

        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_OPEN: {
            if (job->open_step == 0u) {
                unsigned int copy_size = 0u;
                if (!install_stat_size(job->backup_transport, &copy_size)) {
                    TitanInstallerDiag_Logf("ERROR", "ROLLBACK_USB_BACKUP_MISSING",
                                            "transport=%s destination=%s",
                                            job->backup_transport, job->destination);
                    job->phase = TITAN_INSTALL_JOB_ROLLBACK;
                    job->rollback_step = 4u;
                    return 0;
                }
                job->verify_size = copy_size;
                job->open_step = 1u;
            }
            if (job->open_step == 1u) {
                int a = install_async_open_step(job, job->backup_transport, O_RDONLY, 0,
                                                "ASYNC_OPEN_ROLLBACK_USB_BACKUP", &io_result);
                if (a <= 0) return 0;
                job->verify_fd = io_result;
                if (job->verify_fd < 0) {
                    job->open_step = 0u;
                    install_job_close_fds(job);
                    job->phase = TITAN_INSTALL_JOB_ROLLBACK;
                    job->rollback_step = 4u;
                    return 0;
                }
                job->open_step = 2u;
            }
            if (job->open_step == 2u) {
                int a = install_output_open_step(job, job->destination, O_WRONLY | O_CREAT | O_TRUNC, 0666,
                                                 "ASYNC_OPEN_ROLLBACK_MC_DEST",
                                                 &job->stage_fd, &job->stage_backend);
                if (a <= 0) return 0;
                if (job->stage_fd < 0) {
                    (void)install_close_sync_compat(&job->verify_fd, job->backup_transport,
                                                    "CLOSE_ROLLBACK_USB_BACKUP_FAILED");
                    job->open_step = 0u;
                    install_job_close_fds(job);
                    job->phase = TITAN_INSTALL_JOB_ROLLBACK;
                    job->rollback_step = 4u;
                    return 0;
                }
                job->open_step = 3u;
            }
            job->remaining = job->verify_size;
            job->existing_size = 0u;
            job->pending_size = 0u;
            job->pending_written = 0u;
            job->open_step = 0u;
            job->phase = TITAN_INSTALL_JOB_ROLLBACK_RESTORE_READ;
            copy_string(s->status, sizeof(s->status), "Restaurando BOOT anterior via USB...");
            return 0;
        }

        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_READ:
            if (job->remaining == 0u) {
                if (job->close_step == 0u) {
                    int cr = 0; int a = install_async_close_step(job, &job->verify_fd, job->backup_transport,
                                                                 "ASYNC_CLOSE_ROLLBACK_USB_BACKUP", &cr);
                    if (a <= 0) return 0;
                    job->close_step = 1u;
                }
                if (job->close_step == 1u) {
                    int cr = 0; int a = install_output_close_step(job, &job->stage_fd, &job->stage_backend,
                                                                  job->destination, "ASYNC_CLOSE_ROLLBACK_MC_DEST", &cr);
                    if (a <= 0) return 0;
                    job->close_step = 0u;
                }
                job->phase = TITAN_INSTALL_JOB_ROLLBACK_RESTORE_FINISH;
                return 0;
            }
            want = job->remaining > TITAN_INSTALLER_IO_SLICE ? TITAN_INSTALLER_IO_SLICE : job->remaining;
            { int a = install_async_read_step(job, job->verify_fd, g_copy_buffer, want,
                                              job->backup_transport, "ASYNC_READ_ROLLBACK_USB_BACKUP", &io_result);
              if (a <= 0) return 0; }
            if (io_result <= 0 || (unsigned int)io_result > want) {
                TitanInstallerDiag_Logf("ERROR", "ROLLBACK_USB_RESTORE_READ_FAILED",
                                        "transport=%s destination=%s result=%d",
                                        job->backup_transport, job->destination, io_result);
                install_job_close_fds(job);
                job->phase = TITAN_INSTALL_JOB_ROLLBACK;
                job->rollback_step = 4u;
                return 0;
            }
            job->remaining -= (unsigned int)io_result;
            job->pending_size = (unsigned int)io_result;
            job->pending_written = 0u;
            job->phase = TITAN_INSTALL_JOB_ROLLBACK_RESTORE_WRITE;
            return 0;

        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_WRITE:
            if (job->pending_written >= job->pending_size) {
                job->phase = TITAN_INSTALL_JOB_ROLLBACK_RESTORE_READ;
                return 0;
            }
            { int a = install_output_write_step(job, job->stage_fd, job->stage_backend,
                                                g_copy_buffer + job->pending_written,
                                                job->pending_size - job->pending_written,
                                                job->destination, "ASYNC_WRITE_ROLLBACK_MC_DEST", &io_result);
              if (a <= 0) return 0; }
            if (io_result <= 0) {
                TitanInstallerDiag_Logf("ERROR", "ROLLBACK_USB_RESTORE_WRITE_FAILED",
                                        "transport=%s destination=%s result=%d",
                                        job->backup_transport, job->destination, io_result);
                install_job_close_fds(job);
                job->phase = TITAN_INSTALL_JOB_ROLLBACK;
                job->rollback_step = 4u;
                return 0;
            }
            job->pending_written += (unsigned int)io_result;
            job->existing_size += (unsigned int)io_result;
            return 0;

        case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_FINISH:
            if (job->existing_size != job->verify_size)
                TitanInstallerDiag_Logf("ERROR", "ROLLBACK_USB_RESTORE_INCOMPLETE",
                                        "transport=%s destination=%s expected=%u actual=%u",
                                        job->backup_transport, job->destination,
                                        job->verify_size, job->existing_size);
            else
                TitanInstallerDiag_Logf("INFO", "ROLLBACK_USB_RESTORE_COMPLETE",
                                        "transport=%s destination=%s size=%u",
                                        job->backup_transport, job->destination, job->verify_size);
            e = &s->manifest[job->rollback_index - 1u];
            e->published = 0;
            job->rollback_index--;
            job->rollback_step = 0u;
            job->phase = TITAN_INSTALL_JOB_ROLLBACK;
            return 0;

        case TITAN_INSTALL_JOB_SUCCESS:
            return 1;
        case TITAN_INSTALL_JOB_FAILED:
            return -1;
        case TITAN_INSTALL_JOB_IDLE:
        default:
            return -1;
    }
}


/*
 * Real-hardware rule: keep FileXio/newlib filesystem RPCs on the same EE
 * thread that owns the UI loop. The normal Titan storage scheduler already
 * follows this rule for package/update transactions because moving these
 * filesystem calls to a second EE thread can stall at 0% on hardware.
 *
 * v2.0.13 keeps the single-owner rule and splits direction as well as device:
 * sequential TINS/input reads use proven POSIX/newlib; mass: output open/write/
 * close and FAT remove/rename use FileXio NOWAIT; Memory Card uses libmc NOWAIT; MC-bound staging/backup transport uses USB to avoid MC->MC RPC chains.
 * This preserves the seekless TINS stream while keeping the real-hardware FAT
 * write latency measured in v2.0.12 off the EE rendering frame.
 * render frame whenever FileXio can execute it asynchronously.
 */
static int install_job_pump_main_thread(TitanInstallerState *s)
{
    TitanInstallerInstallJob *job;
    unsigned long long start_work;
    uint64_t start_ms;
    unsigned int steps = 0u;
    int result = 0;

    if (s == 0) return -1;
    job = &s->install_job;
    start_work = job->work_done;
    start_ms = TitanTime_NowMilliseconds();

    while (result == 0) {
        result = install_job_step(s);
        if (result != 0) return result;
        steps++;
        /* A metadata RPC (FileXio for mass:, libmc for mc:) is outstanding.
         * Do not spin here: return to the renderer and poll next frame. */
        if (job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE)
            break;
        if (steps >= TITAN_INSTALLER_META_STEPS_PER_FRAME)
            break;
        if (job->work_done >= start_work && job->work_done - start_work >= TITAN_INSTALLER_IO_FRAME_BUDGET)
            break;
        if (TitanTime_NowMilliseconds() - start_ms >= TITAN_INSTALLER_IO_FRAME_BUDGET_MS)
            break;
    }
    return 0;
}

static void draw_progress(unsigned int page)
{
    float x = 634.0f;
    float y = 44.0f;
    float width = 160.0f;
    unsigned int completed = page > TITAN_INSTALLER_PAGE_SUMMARY
        ? (unsigned int)TITAN_INSTALLER_PAGE_SUMMARY
        : page;

    TitanVideo_DrawRoundedRectAlpha(x, y, width, 4.0f, 2.0f, 255, 255, 255, 52);
    TitanVideo_DrawRoundedRectAlpha(
        x,
        y,
        width * ((float)completed / (float)TITAN_INSTALLER_PAGE_SUMMARY),
        4.0f,
        2.0f,
        255,
        255,
        255,
        228
    );
}

static void installer_hero_atlas_copy_a8(
    const unsigned char *source,
    unsigned int source_size,
    unsigned int destination_x,
    unsigned int destination_y
)
{
    unsigned int x;
    unsigned int y;

    if (source == 0 || source_size != TITAN_INSTALLER_HERO_SOURCE_SIZE * TITAN_INSTALLER_HERO_SOURCE_SIZE)
        return;

    for (y = 0u; y < TITAN_INSTALLER_HERO_SOURCE_SIZE; y++) {
        for (x = 0u; x < TITAN_INSTALLER_HERO_SOURCE_SIZE; x++) {
            unsigned int coverage = source[y * TITAN_INSTALLER_HERO_SOURCE_SIZE + x];
            unsigned int gs_alpha = ((coverage * 128u) + 127u) / 255u;
            g_installer_hero_atlas[
                (destination_y + y) * TITAN_INSTALLER_HERO_ATLAS_WIDTH +
                destination_x + x
            ] = 0x00808080u | (gs_alpha << 24u);
        }
    }
}

static const unsigned char *installer_hero_data_for_family(
    TitanInstallerConsoleFamily family,
    unsigned int *size_out
)
{
    if (family == TITAN_INSTALLER_CONSOLE_FAT) {
        if (size_out) *size_out = size_titan_installer_fat;
        return titan_installer_fat;
    }
    if (family == TITAN_INSTALLER_CONSOLE_SLIM_90) {
        if (size_out) *size_out = size_titan_installer_slim90;
        return titan_installer_slim90;
    }
    if (size_out) *size_out = size_titan_installer_slim70;
    return titan_installer_slim70;
}

static int installer_hero_atlas_ensure_pair(
    TitanInstallerState *s,
    TitanInstallerConsoleFamily family_a,
    TitanInstallerConsoleFamily family_b
)
{
    TitanResult result;
    unsigned int size_a;
    unsigned int size_b;
    const unsigned char *data_a;
    const unsigned char *data_b;

    if (
        s->hero_atlas_ready &&
        s->hero_atlas_family_a == (int)family_a &&
        s->hero_atlas_family_b == (int)family_b
    )
        return 1;

    data_a = installer_hero_data_for_family(family_a, &size_a);
    data_b = installer_hero_data_for_family(family_b, &size_b);

    memset(g_installer_hero_atlas, 0, sizeof(g_installer_hero_atlas));
    installer_hero_atlas_copy_a8(data_a, size_a, 0u, 0u);
    installer_hero_atlas_copy_a8(data_b, size_b, 256u, 0u);

    result = TitanVideo_UploadUiAtlas32(
        g_installer_hero_atlas,
        TITAN_INSTALLER_HERO_ATLAS_WIDTH,
        TITAN_INSTALLER_HERO_ATLAS_HEIGHT,
        TITAN_INSTALLER_HERO_ATLAS_WIDTH
    );
    if (result != TITAN_RESULT_OK)
        return 0;

    s->hero_atlas_ready = 1;
    s->hero_atlas_family_a = (int)family_a;
    s->hero_atlas_family_b = (int)family_b;
    return 1;
}

static void draw_background(TitanInstallerState *s, int chrome)
{
    TitanVideo_Clear(0, 0, 0);
    TitanShellBackground_Draw(s->frame, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f);
    /* The controller-pairing page mirrors the Orbis first-run composition: no
     * header/footer chrome, just the live Magenta shell background beneath
     * the illustration and multilingual instruction block. */
    TitanVideo_DrawRectAlpha(0.0f, -32.0f, 854.0f, 512.0f, 5, 8, 18, chrome ? 92 : 34);
    if (chrome) {
        TitanVideo_DrawRectAlpha(0.0f, 0.0f, 854.0f, 78.0f, 6, 10, 20, 118);
        TitanVideo_DrawLineAlpha(48.0f, 76.0f, 806.0f, 76.0f, 1.0f, 255, 255, 255, 54);
        TitanVideo_DrawLineAlpha(48.0f, 392.0f, 806.0f, 392.0f, 1.0f, 255, 255, 255, 42);
    }
}

static int g_installer_ui_language = 0; /* 0=pt-BR, 1=en, 2=es */

typedef struct TitanInstallerTranslation {
    const char *pt;
    const char *en;
    const char *es;
} TitanInstallerTranslation;

static const TitanInstallerTranslation g_installer_translations[] = {
    {"Configuração inicial", "Initial setup", "Configuración inicial"},
    {"Selecione o idioma", "Select language", "Selecciona el idioma"},
    {"Selecionar", "Select", "Seleccionar"},
    {"Continuar", "Continue", "Continuar"},
    {"O  Voltar", "O  Back", "O  Volver"},
    {"X  Selecionar", "X  Select", "X  Seleccionar"},
    {"X  Continuar", "X  Continue", "X  Continuar"},
    {"Bem-vindo ao Project Titan", "Welcome to Project Titan", "Bienvenido a Project Titan"},
    {"Verificação do sistema", "System verification", "Verificación del sistema"},
    {"Armazenamento USB", "USB storage", "Almacenamiento USB"},
    {"Memory Card na porta 1", "Memory Card in port 1", "Memory Card en el puerto 1"},
    {"Memory Card na porta 2", "Memory Card in port 2", "Memory Card en el puerto 2"},
    {"Container TINS válido", "Valid TINS container", "Contenedor TINS válido"},
    {"TITAN_SYSTEM.TINS inválido ou ausente", "TITAN_SYSTEM.TINS invalid or missing", "TITAN_SYSTEM.TINS inválido o ausente"},
    {"Perfil autônomo disponível", "Autonomous profile available", "Perfil autónomo disponible"},
    {"Modo autônomo bloqueado por compatibilidade", "Autonomous mode blocked by compatibility", "Modo autónomo bloqueado por compatibilidad"},
    {"Escolha o método de inicialização", "Choose startup method", "Elige el método de inicio"},
    {"Semi-autônoma — bootloader no Memory Card / Matrix-DEV1", "Semi-autonomous — bootloader on Memory Card / Matrix-DEV1", "Semiautónoma — bootloader en Memory Card / Matrix-DEV1"},
    {"Autônoma — exploit/cadeia de boot no Memory Card", "Autonomous — exploit/boot chain on Memory Card", "Autónoma — exploit/cadena de arranque en Memory Card"},
    {"Selecione o Memory Card", "Select Memory Card", "Selecciona la Memory Card"},
    {"Porta 1  (mc0:)", "Port 1  (mc0:)", "Puerto 1  (mc0:)"},
    {"Porta 2  (mc1:)", "Port 2  (mc1:)", "Puerto 2  (mc1:)"},
    {"Corrija os itens acima", "Fix the items above", "Corrige los elementos anteriores"},
    {"Nenhum arquivo persistente será alterado antes da confirmação final.", "No persistent file will be changed before final confirmation.", "Ningún archivo persistente se modificará antes de la confirmación final."},
    {"Este assistente prepara o console e confirma o ambiente antes da instalação.", "This wizard prepares the console and verifies the environment before installation.", "Este asistente prepara la consola y verifica el entorno antes de la instalación."},
    {"O instalador valida o pacote e os destinos antes de oferecer os modos de boot.", "The installer validates the package and destinations before offering boot modes.", "El instalador valida el paquete y los destinos antes de ofrecer modos de arranque."},
    {"Em ambos os modos, o Titan Bootloader continua residindo no Memory Card.", "In both modes, Titan Bootloader remains on the Memory Card.", "En ambos modos, Titan Bootloader permanece en la Memory Card."},
    {"O bootloader será publicado em mcX:/BOOT/BOOT.ELF conforme a porta escolhida.", "The bootloader will be published to mcX:/BOOT/BOOT.ELF on the selected port.", "El bootloader se publicará en mcX:/BOOT/BOOT.ELF según el puerto seleccionado."},
};

static const char *installer_tr(const char *text)
{
    unsigned int i;
    if (!text || g_installer_ui_language == 0) return text;
    for (i=0u;i<(unsigned int)(sizeof(g_installer_translations)/sizeof(g_installer_translations[0]));++i) {
        if (strcmp(text,g_installer_translations[i].pt)==0)
            return g_installer_ui_language==1 ? g_installer_translations[i].en : g_installer_translations[i].es;
    }
    return text; /* deterministic pt-BR fallback for legacy strings */
}

static void draw_header(const char *title, unsigned int page)
{
    title = installer_tr(title);
    TitanFont_DrawTextShadow(56.0f, 40.0f, 0.88f, title, 255, 255, 255, 255);
    draw_progress(page);
}

static void draw_footer(const char *left, const char *right)
{
    left = installer_tr(left); right = installer_tr(right);
    if (left)
        TitanFont_DrawText(56.0f, 405.0f, 0.54f, left, 235, 240, 255);
    if (right) {
        float width = TitanFont_MeasureText(0.54f, right);
        TitanFont_DrawText(798.0f - width, 405.0f, 0.54f, right, 235, 240, 255);
    }
}

static void draw_body_title(const char *title, const char *subtitle)
{
    title = installer_tr(title); subtitle = installer_tr(subtitle);
    float title_width = TitanFont_MeasureText(0.86f, title);
    TitanFont_DrawText((854.0f - title_width) * 0.5f, 102.0f, 0.86f, title, 255, 255, 255);
    if (subtitle != 0 && subtitle[0] != '\0') {
        float subtitle_width = TitanFont_MeasureText(0.56f, subtitle);
        TitanFont_DrawText((854.0f - subtitle_width) * 0.5f, 136.0f, 0.56f, subtitle, 222, 232, 255);
    }
}

static void draw_panel(float x, float y, float width, float height)
{
    TitanVideo_DrawRoundedRectAlpha(x, y, width, height, 12.0f, 0, 0, 0, 110);
    TitanVideo_DrawRoundedRectAlpha(x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, 11.0f, 255, 255, 255, 18);
}

static float clamp01f(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float ease_out_cubic(float value)
{
    float inv = 1.0f - clamp01f(value);
    return 1.0f - (inv * inv * inv);
}

static void draw_focus_outline(float x, float y, float width, float height, unsigned char alpha)
{
    const unsigned char r = 244u;
    const unsigned char g = 55u;
    const unsigned char b = 101u;

    TitanVideo_DrawLineAlpha(x - 3.0f, y - 3.0f, x + width + 3.0f, y - 3.0f, 2.0f, r, g, b, (unsigned char)(alpha * 0.32f));
    TitanVideo_DrawLineAlpha(x - 3.0f, y + height + 3.0f, x + width + 3.0f, y + height + 3.0f, 2.0f, r, g, b, (unsigned char)(alpha * 0.32f));
    TitanVideo_DrawLineAlpha(x - 3.0f, y - 3.0f, x - 3.0f, y + height + 3.0f, 2.0f, r, g, b, (unsigned char)(alpha * 0.32f));
    TitanVideo_DrawLineAlpha(x + width + 3.0f, y - 3.0f, x + width + 3.0f, y + height + 3.0f, 2.0f, r, g, b, (unsigned char)(alpha * 0.32f));

    TitanVideo_DrawLineAlpha(x, y, x + width, y, 2.0f, r, g, b, alpha);
    TitanVideo_DrawLineAlpha(x, y + height, x + width, y + height, 2.0f, r, g, b, alpha);
    TitanVideo_DrawLineAlpha(x, y, x, y + height, 2.0f, r, g, b, alpha);
    TitanVideo_DrawLineAlpha(x + width, y, x + width, y + height, 2.0f, r, g, b, alpha);
}

static void draw_list_item(float x, float y, float width, const char *text, int selected, int disabled)
{
    text = installer_tr(text);
    unsigned char text_r = disabled ? 142u : 244u;
    unsigned char text_g = disabled ? 154u : 247u;
    unsigned char text_b = disabled ? 176u : 252u;
    float box_y = y - 9.0f;
    float box_h = 40.0f;

    if (!selected)
        TitanVideo_DrawRoundedRectAlpha(x, box_y, width, box_h, 0.0f, 255, 255, 255, disabled ? 10 : 16);
    else
        draw_focus_outline(x, box_y, width, box_h, disabled ? 74u : 224u);

    TitanFont_DrawText(x + 18.0f, y, 0.68f, text, text_r, text_g, text_b);
}

static void draw_status_item(float x, float y, float width, const char *prefix, const char *text, int ok, int warning)
{
    unsigned char r = ok ? 210u : (warning ? 242u : 255u);
    unsigned char g = ok ? 245u : (warning ? 214u : 168u);
    unsigned char b = ok ? 228u : (warning ? 160u : 168u);
    char line[220];
    snprintf(line, sizeof(line), "%s %s", prefix, text);
    TitanVideo_DrawRoundedRectAlpha(x, y - 8.0f, width, 34.0f, 7.0f, 255, 255, 255, 20);
    TitanFont_DrawText(x + 16.0f, y, 0.58f, line, r, g, b);
}

static void draw_center_button(const char *label, int enabled)
{
    label = installer_tr(label);
    float width = TitanFont_MeasureText(0.58f, label) + 62.0f;
    float x = (854.0f - width) * 0.5f;

    draw_focus_outline(x, 352.0f, width, 34.0f, enabled ? 224u : 74u);
    TitanFont_DrawText(x + 30.0f, 361.0f, 0.58f, label,
        enabled ? 244 : 164,
        enabled ? 247 : 178,
        enabled ? 252 : 198);
}

static void draw_console_hero_slot_alpha(unsigned int slot, unsigned char alpha)
{
    unsigned int source_x = slot == 0u ? 0u : 256u;
    if (alpha == 0u)
        return;

    TitanVideo_DrawUiAtlasRegion32Point(
        70.0f,
        78.0f,
        280.0f,
        280.0f,
        source_x,
        0u,
        TITAN_INSTALLER_HERO_SOURCE_SIZE,
        TITAN_INSTALLER_HERO_SOURCE_SIZE,
        255u,
        255u,
        255u,
        alpha
    );
}

static TitanInstallerConsoleFamily installer_unknown_family(unsigned int index)
{
    if ((index % 3u) == 0u) return TITAN_INSTALLER_CONSOLE_FAT;
    if ((index % 3u) == 1u) return TITAN_INSTALLER_CONSOLE_SLIM_70;
    return TITAN_INSTALLER_CONSOLE_SLIM_90;
}

static void prepare_controller_hero_atlas(TitanInstallerState *s)
{
    if (s->console.family == TITAN_INSTALLER_CONSOLE_UNKNOWN) {
        unsigned int cycle = (s->frame / TITAN_INSTALLER_HERO_CYCLE_FRAMES) % 3u;
        TitanInstallerConsoleFamily current = installer_unknown_family(cycle);
        TitanInstallerConsoleFamily next = installer_unknown_family(cycle + 1u);
        (void)installer_hero_atlas_ensure_pair(s, current, next);
        return;
    }

    (void)installer_hero_atlas_ensure_pair(s, s->console.family, s->console.family);
}

static void draw_selected_console_hero(const TitanInstallerState *s)
{
    if (!s->hero_atlas_ready)
        return;

    if (s->console.family == TITAN_INSTALLER_CONSOLE_UNKNOWN) {
        unsigned int local_frame = s->frame % TITAN_INSTALLER_HERO_CYCLE_FRAMES;

        if (local_frame >= TITAN_INSTALLER_HERO_CYCLE_FRAMES - TITAN_INSTALLER_HERO_FADE_FRAMES) {
            float progress = (float)(local_frame - (TITAN_INSTALLER_HERO_CYCLE_FRAMES - TITAN_INSTALLER_HERO_FADE_FRAMES)) /
                (float)TITAN_INSTALLER_HERO_FADE_FRAMES;
            unsigned char next_alpha = (unsigned char)(242.0f * clamp01f(progress));
            unsigned char current_alpha = (unsigned char)(242u - next_alpha);
            draw_console_hero_slot_alpha(0u, current_alpha);
            draw_console_hero_slot_alpha(1u, next_alpha);
        } else {
            draw_console_hero_slot_alpha(0u, 242u);
        }
        return;
    }

    draw_console_hero_slot_alpha(0u, 242u);
}

static void draw_controller_instruction_block(void)
{
    const unsigned char r = 218u, g = 226u, b = 241u;
    const float x = 392.0f;
    const float s = 0.47f;

    TitanFont_DrawText(x, 58.0f, s, "Connect the DUALSHOCK 2 analog controller to controller", r, g, b);
    TitanFont_DrawText(x, 78.0f, s, "port 1, and then press the X button.", r, g, b);

    TitanFont_DrawText(x, 116.0f, s, "Conecte o controle analógico DUALSHOCK 2 à porta de", r, g, b);
    TitanFont_DrawText(x, 136.0f, s, "controle 1 e pressione o botão X.", r, g, b);

    TitanFont_DrawText(x, 174.0f, s, "Schließe den DUALSHOCK 2 Analog-Controller an", r, g, b);
    TitanFont_DrawText(x, 194.0f, s, "Controller-Port 1 an und drücke die X-Taste.", r, g, b);

    TitanFont_DrawText(x, 232.0f, s, "Conecta el mando analógico DUALSHOCK 2 al puerto", r, g, b);
    TitanFont_DrawText(x, 252.0f, s, "de control 1 y pulsa el botón X.", r, g, b);

    TitanFont_DrawText(x, 290.0f, s, "Collega il controller analogico DUALSHOCK 2 alla porta", r, g, b);
    TitanFont_DrawText(x, 310.0f, s, "controller 1, quindi premi il tasto X.", r, g, b);

    TitanFont_DrawText(x, 348.0f, s, "Connectez la manette analogique DUALSHOCK 2 au port", r, g, b);
    TitanFont_DrawText(x, 368.0f, s, "de manette 1, puis appuyez sur la touche X.", r, g, b);
}

static float draw_wrapped_block(
    float x,
    float y,
    float scale,
    const char *text,
    float max_width,
    float bottom_y
)
{
    char line[180], word[80];
    const char *p = text;
    unsigned int li = 0u, wi;
    const float line_height = 22.0f;
    const float item_spacing = 6.0f;
    const float space_width = TitanFont_MeasureText(scale, " ");

    line[0] = '\0';
    while (*p) {
        float candidate_width;
        while (*p == ' ') p++;
        wi = 0u;
        while (*p && *p != ' ' && wi + 1u < sizeof(word)) word[wi++] = *p++;
        word[wi] = '\0';
        if (wi == 0u) break;

        candidate_width = TitanFont_MeasureText(scale, line);
        if (li != 0u) candidate_width += space_width;
        candidate_width += TitanFont_MeasureText(scale, word);

        if (li != 0u && candidate_width > max_width) {
            if (y + line_height > bottom_y) return y;
            TitanFont_DrawText(x, y, scale, line, 245, 248, 255);
            y += line_height;
            li = 0u;
            line[0] = '\0';
        }

        if (li && li + 1u < sizeof(line)) line[li++] = ' ';
        for (wi = 0u; word[wi] && li + 1u < sizeof(line); wi++) line[li++] = word[wi];
        line[li] = '\0';
    }

    if (li) {
        if (y + line_height > bottom_y) return y;
        TitanFont_DrawText(x, y, scale, line, 245, 248, 255);
        y += line_height;
    }
    return y + item_spacing;
}

static void draw_wrapped(float x, float y, float scale, const char *text, float max_width)
{
    (void)draw_wrapped_block(x, y, scale, text, max_width, 10000.0f);
}

static void render_boot_splash(void)
{
    TitanVideo_BeginFrame();
    TitanVideo_Clear(0, 0, 0);
    TitanFont_DrawText(238.0f, 186.0f, 0.96f, "Project Titan: Magenta Edition", 255, 255, 255);
    TitanFont_DrawText(308.0f, 230.0f, 0.56f, "Preparando instalador...", 188, 198, 218);
    TitanVideo_EndFrame();
}

static void render(TitanInstallerState *s)
{
    char text[224];
    unsigned int i;
    unsigned int terms_limit;

    float transition_amount;
    float transition_offset_x;
    float transition_fade;

    if (s->last_rendered_page != (int)s->page) {
        s->last_rendered_page = (int)s->page;
        s->page_transition_frame = 0u;
    }
    transition_amount = s->page_transition_frame >= TITAN_INSTALLER_PAGE_TRANSITION_FRAMES
        ? 1.0f
        : (float)s->page_transition_frame / (float)TITAN_INSTALLER_PAGE_TRANSITION_FRAMES;
    transition_amount = ease_out_cubic(transition_amount);
    transition_offset_x = (1.0f - transition_amount) * TITAN_INSTALLER_PAGE_TRANSITION_OFFSET_X;
    transition_fade = 1.0f - transition_amount;

    if (s->page == TITAN_INSTALLER_PAGE_CONTROLLER)
        prepare_controller_hero_atlas(s);

    TitanVideo_BeginFrame();
    draw_background(s, s->page != TITAN_INSTALLER_PAGE_CONTROLLER);
    TitanVideo_SetTransform(1.0f, 0.0f, 0.0f, transition_offset_x, 0.0f);

    switch (s->page) {
        case TITAN_INSTALLER_PAGE_VIDEO_OUTPUT: {
            const char *region = TitanVideoOutput_GetRegionName();
            const char *output = TitanVideoOutput_GetConfiguredOutputName();
            int can_720p = TitanVideoOutput_CanTestProgressive();
            const char *legacy_mode = strcmp(region, "PAL") == 0 ? "576i (PAL)" : "480i (NTSC)";

            draw_header("Configuração inicial", s->page);
            draw_body_title("Saída de vídeo", "O Project Titan identificou a configuração de vídeo do PlayStation 2.");
            draw_panel(142.0f, 160.0f, 570.0f, 164.0f);

            snprintf(text, sizeof(text), "Saída configurada no PS2: %s", output);
            TitanFont_DrawText(174.0f, 184.0f, 0.58f, text, 255, 255, 255);
            snprintf(text, sizeof(text), "Região de vídeo: %s", region);
            TitanFont_DrawText(174.0f, 211.0f, 0.54f, text, 224, 234, 255);

            if (s->video_test_active) {
                unsigned int seconds = (s->video_test_frames_remaining + 59u) / 60u;
                TitanFont_DrawText(174.0f, 244.0f, 0.62f, "720p Progressive ativado temporariamente.", 255, 255, 255);
                snprintf(text, sizeof(text), "A imagem está visível? X mantém 720p. O restaura %s. (%us)", legacy_mode, seconds);
                TitanFont_DrawText(174.0f, 274.0f, 0.50f, text, 220, 232, 255);
                draw_footer("O  Restaurar", "X  Usar 720p");
            } else {
                char legacy_label[96];
                snprintf(legacy_label, sizeof(legacy_label), "Executar instalador em %s", legacy_mode);
                draw_list_item(174.0f, 242.0f, 506.0f, legacy_label, s->selection == 0, 0);
                draw_list_item(174.0f, 286.0f, 506.0f, "Executar instalador em 720p Progressive", s->selection == 1, !can_720p);
                if (!can_720p)
                    TitanFont_DrawText(174.0f, 324.0f, 0.48f, "720p requer saída Component (YPbPr) configurada no OSD do PS2.", 224, 214, 188);
                else if (s->video_status[0] != '\0')
                    TitanFont_DrawText(174.0f, 324.0f, 0.48f, s->video_status, 224, 232, 255);
                draw_footer(0, "X  Selecionar");
            }
            break;
        }

        case TITAN_INSTALLER_PAGE_CONTROLLER:
            /* Orbis first interactive screen: model-specific console art on
             * the left, multilingual instructions on the right, and no extra
             * setup chrome before the user has selected a language. */
            draw_selected_console_hero(s);
            draw_controller_instruction_block();
            break;

        case TITAN_INSTALLER_PAGE_LANGUAGE:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Selecione o idioma", "O restante do assistente seguirá o layout do setup inicial do Orbis OS, com tema Magenta.");
            draw_panel(176.0f, 160.0f, 502.0f, 156.0f);
            draw_list_item(196.0f, 184.0f, 462.0f, "Português (Brasil)", s->selection == 0, 0);
            draw_list_item(196.0f, 228.0f, 462.0f, "English", s->selection == 1, 0);
            draw_list_item(196.0f, 272.0f, 462.0f, "Español", s->selection == 2, 0);
            draw_center_button("Selecionar", 1);
            draw_footer("O  Voltar", "X  Selecionar");
            break;

        case TITAN_INSTALLER_PAGE_WELCOME:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Bem-vindo ao Project Titan", "Este assistente prepara o console e confirma o ambiente antes da instalação.");
            draw_panel(132.0f, 166.0f, 590.0f, 138.0f);
            snprintf(text, sizeof(text), "Modelo detectado: %s", s->console.model_name);
            TitanFont_DrawText(162.0f, 194.0f, 0.60f, text, 255, 255, 255);
            snprintf(text, sizeof(text), "Model ID: %s%04X", s->console.model_id_valid ? "0x" : "não disponível ", s->console.model_id_valid ? s->console.model_id : 0u);
            TitanFont_DrawText(162.0f, 225.0f, 0.60f, text, 228, 236, 255);
            snprintf(text, sizeof(text), "ROM: %s", s->console.romver[0] ? s->console.romver : "não identificada");
            TitanFont_DrawText(162.0f, 256.0f, 0.60f, text, 228, 236, 255);
            TitanFont_DrawText(162.0f, 287.0f, 0.54f, "Nenhum arquivo persistente será alterado antes da confirmação final.", 218, 230, 255);
            draw_center_button("Continuar", 1);
            draw_footer("O  Voltar", "X  Continuar");
            break;

        case TITAN_INSTALLER_PAGE_PREFLIGHT:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Verificação do sistema", "O instalador valida o pacote e os destinos antes de oferecer os modos de boot.");
            draw_panel(126.0f, 160.0f, 602.0f, 164.0f);
            draw_status_item(148.0f, 186.0f, 558.0f, s->storage_ready ? "OK" : "ERRO", "Armazenamento USB", s->storage_ready, 0);
            draw_status_item(148.0f, 216.0f, 558.0f, s->mc0_ready ? "OK" : "INFO", "Memory Card na porta 1", s->mc0_ready, !s->mc0_ready);
            draw_status_item(148.0f, 246.0f, 558.0f, s->mc1_ready ? "OK" : "INFO", "Memory Card na porta 2", s->mc1_ready, !s->mc1_ready);
            if (!s->storage_ready)
                draw_status_item(148.0f, 276.0f, 558.0f, "INFO", "TITAN_SYSTEM.TINS ainda não verificado", 0, 1);
            else
                draw_status_item(148.0f, 276.0f, 558.0f, s->tins_preflight_ok ? "OK" : "ERRO", s->tins_preflight_ok ? "Container TINS válido" : "TITAN_SYSTEM.TINS inválido ou ausente", s->tins_preflight_ok, 0);
            draw_status_item(148.0f, 306.0f, 558.0f, s->autonomous_available ? "OK" : "INFO", s->autonomous_available ? "Inicialização autônoma disponível" : "Somente instalação semi-autônoma disponível", s->autonomous_available, !s->autonomous_available);
            draw_center_button("Continuar", s->storage_ready && s->tins_preflight_ok && (s->mc0_ready || s->mc1_ready));
            draw_footer("O  Voltar", (s->storage_ready && s->tins_preflight_ok && (s->mc0_ready || s->mc1_ready)) ? "X  Continuar" : "Corrija os itens acima");
            break;

        case TITAN_INSTALLER_PAGE_METHOD:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Escolha o método de inicialização", "Em ambos os modos, o Titan Bootloader continua residindo no Memory Card.");
            if (s->autonomous_available) {
                draw_panel(138.0f, 176.0f, 578.0f, 124.0f);
                draw_list_item(160.0f, 206.0f, 534.0f, "Semi-autônoma — bootloader no Memory Card / Matrix-DEV1", s->selection == 0, 0);
                draw_list_item(160.0f, 254.0f, 534.0f, "Autônoma — inicialização própria pelo Memory Card", s->selection == 1, 0);
            } else {
                draw_panel(138.0f, 176.0f, 578.0f, 142.0f);
                draw_list_item(160.0f, 206.0f, 534.0f, "Semi-autônoma — bootloader no Memory Card / Matrix-DEV1", 1, 0);
                TitanFont_DrawText(174.0f, 266.0f, 0.50f, "Este console não é compatível com a inicialização autônoma.", 218, 230, 255);
                TitanFont_DrawText(174.0f, 288.0f, 0.50f, "A instalação semi-autônoma é a única opção disponível.", 218, 230, 255);
            }
            draw_center_button("Selecionar", 1);
            draw_footer("O  Voltar", "X  Selecionar");
            break;

        case TITAN_INSTALLER_PAGE_MEMORY_CARD:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Selecione o Memory Card", "O bootloader será publicado em mcX:/BOOT/BOOT.ELF conforme a porta escolhida.");
            draw_panel(196.0f, 188.0f, 462.0f, 96.0f);
            draw_list_item(216.0f, 214.0f, 422.0f, "Porta 1  (mc0:)", s->selection == 0, !s->mc0_ready);
            draw_list_item(216.0f, 258.0f, 422.0f, "Porta 2  (mc1:)", s->selection == 1, !s->mc1_ready);
            draw_center_button("Selecionar", !((s->selection == 0 && !s->mc0_ready) || (s->selection == 1 && !s->mc1_ready)));
            draw_footer("O  Voltar", "X  Selecionar");
            break;

        case TITAN_INSTALLER_PAGE_BOOT_ANALYSIS:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Análise do boot existente", "A instalação respeita o DEV1 atual e mantém um backup restaurável do BOOT.ELF.");
            draw_panel(122.0f, 178.0f, 610.0f, 114.0f);
            snprintf(text, sizeof(text), "Caminho DEV1: mc%d:/BOOT/BOOT.ELF", s->memory_card);
            TitanFont_DrawText(154.0f, 206.0f, 0.60f, text, 255, 255, 255);
            TitanFont_DrawText(154.0f, 236.0f, 0.56f,
                s->existing_boot ?
                    "Um BOOT.ELF já existe. Ele será preservado como BOOT.TITAN.BAK." :
                    "Nenhum BOOT.ELF existente foi encontrado.",
                225, 235, 255);
            TitanFont_DrawText(154.0f, 266.0f, 0.52f, "O instalador nunca sobrescreve o backup existente.", 216, 228, 255);
            draw_center_button("Continuar", 1);
            draw_footer("O  Voltar", "X  Continuar");
            break;

        case TITAN_INSTALLER_PAGE_DESTINATION:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Destino da instalação", "Os arquivos são publicados por staging, CRC32 e rename para reduzir riscos de corrupção.");
            draw_panel(124.0f, 172.0f, 606.0f, 124.0f);
            snprintf(text, sizeof(text), "Memory Card: mc%d:/BOOT/BOOT.ELF  →  Titan Bootloader", s->memory_card);
            TitanFont_DrawText(154.0f, 200.0f, 0.58f, text, 255, 255, 255);
            TitanFont_DrawText(154.0f, 232.0f, 0.58f, "Armazenamento principal: USB (kernel.elf + TITAN/)", 255, 255, 255);
            TitanFont_DrawText(154.0f, 264.0f, 0.52f, "Se uma falha ocorrer, a transação tenta restaurar o estado anterior automaticamente.", 216, 228, 255);
            draw_center_button("Continuar", 1);
            draw_footer("O  Voltar", "X  Continuar");
            break;

        case TITAN_INSTALLER_PAGE_TERMS:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Termos de Uso", "Leia e aceite o aviso canônico da tecnologia VlombisCore™ para continuar.");
            draw_panel(86.0f, 156.0f, 682.0f, 188.0f);
            terms_limit = (unsigned int)(sizeof(g_vlombis_terms) / sizeof(g_vlombis_terms[0]));
            {
                float terms_y = 182.0f;
                const float terms_bottom = 330.0f;
                for (i = (unsigned int)s->terms_scroll; i < terms_limit && terms_y < terms_bottom; i++) {
                    float next_y = draw_wrapped_block(112.0f, terms_y, 0.46f, g_vlombis_terms[i], 630.0f, terms_bottom);
                    if (next_y <= terms_y) break;
                    terms_y = next_y;
                }
            }
            draw_center_button("Aceitar", 1);
            draw_footer("↑↓  Ler   O  Voltar", "X  Aceitar");
            break;

        case TITAN_INSTALLER_PAGE_SUMMARY:
            draw_header("Configuração inicial", s->page);
            draw_body_title("Pronto para instalar", "Revise os principais pontos antes de iniciar a publicação definitiva.");
            draw_panel(160.0f, 170.0f, 534.0f, 124.0f);
            snprintf(text, sizeof(text), "Console: %s", s->console.model_name);
            TitanFont_DrawText(188.0f, 198.0f, 0.58f, text, 255, 255, 255);
            snprintf(text, sizeof(text), "Método: %s", s->method == TITAN_INSTALLER_METHOD_SEMI_AUTONOMOUS ? "Semi-autônoma (Matrix/DEV1)" : "Autônoma");
            TitanFont_DrawText(188.0f, 228.0f, 0.58f, text, 228, 236, 255);
            snprintf(text, sizeof(text), "Boot: mc%d:/BOOT/BOOT.ELF", s->memory_card);
            TitanFont_DrawText(188.0f, 258.0f, 0.58f, text, 228, 236, 255);
            draw_center_button("Instalar", 1);
            draw_footer("O  Voltar", "X  INSTALAR");
            break;

        case TITAN_INSTALLER_PAGE_INSTALLING:
            draw_header("Instalação em andamento", s->page);
            draw_body_title("Instalando Project Titan", "Aguarde enquanto o instalador verifica, publica e valida os arquivos.");
            draw_panel(142.0f, 188.0f, 570.0f, 112.0f);
            TitanFont_DrawText(176.0f, 214.0f, 0.58f, s->status, 255, 255, 255);
            TitanVideo_DrawRoundedRectAlpha(176.0f, 246.0f, 502.0f, 14.0f, 7.0f, 255, 255, 255, 64);
            TitanVideo_DrawRoundedRectAlpha(176.0f, 246.0f, 502.0f * ((float)s->progress / 100.0f), 14.0f, 7.0f, 255, 255, 255, 230);
            snprintf(text, sizeof(text), "%u%%", s->progress);
            TitanFont_DrawText(404.0f, 274.0f, 0.60f, text, 255, 255, 255);
            TitanFont_DrawText(176.0f, 302.0f, 0.50f, "Não desligue o console. A Emotion Engine está organizando seus sentimentos.", 216, 228, 255);
            break;

        case TITAN_INSTALLER_PAGE_COMPLETE:
            draw_header("Instalação concluída", s->page);
            draw_body_title("Project Titan está pronto", "O instalador foi invalidado e a próxima inicialização seguirá o novo fluxo do Titan.");
            draw_panel(166.0f, 176.0f, 522.0f, 120.0f);
            TitanFont_DrawText(220.0f, 204.0f, 0.80f, "Project Titan: Magenta Edition", 255, 255, 255);
            TitanFont_DrawText(310.0f, 238.0f, 0.62f, "VlombisCore™ Technology", 255, 255, 255);
            TitanFont_DrawText(345.0f, 266.0f, 0.54f, "Feel the processing.", 225, 235, 255);
            TitanFont_DrawText(248.0f, 294.0f, 0.50f, "Esta tela não aparecerá novamente neste pendrive de instalação.", 216, 228, 255);
            draw_center_button("Iniciar Titan", 1);
            draw_footer(0, "X  Iniciar Titan");
            break;

        case TITAN_INSTALLER_PAGE_ERROR:
        default:
            draw_header("Falha na instalação", s->page);
            draw_body_title("Não foi possível concluir", "O instalador tentou restaurar o estado anterior sempre que possível.");
            draw_panel(116.0f, 170.0f, 622.0f, 144.0f);
            TitanFont_DrawText(148.0f, 198.0f, 0.62f, "Emotion Engine Status: Sad", 255, 255, 255);
            draw_wrapped(148.0f, 232.0f, 0.54f, s->error, 560.0f);
            TitanFont_DrawText(148.0f, 300.0f, 0.50f, "O instalador NÃO se autodestruiu e o BOOT anterior foi restaurado quando possível.", 225, 235, 255);
            draw_center_button("Voltar", 1);
            draw_footer("O  Voltar", 0);
            break;
    }

    TitanVideo_ResetTransform();
    if (s->page_transition_frame < TITAN_INSTALLER_PAGE_TRANSITION_FRAMES) {
        unsigned char fade_alpha = (unsigned char)(64.0f * transition_fade);
        if (fade_alpha > 0u)
            TitanVideo_DrawRectAlpha(0.0f, -32.0f, 854.0f, 512.0f, 0, 0, 0, fade_alpha);
        s->page_transition_frame++;
    }
    TitanVideo_EndFrame();
}

static void probe_mc(TitanInstallerState *s)
{
    s->mc0_ready = path_exists("mc0:/") || path_exists("mc0:/BOOT") || path_exists("mc0:/SYS-CONF");
    s->mc1_ready = path_exists("mc1:/") || path_exists("mc1:/BOOT") || path_exists("mc1:/SYS-CONF");
}

static void update_preflight(TitanInstallerState *s)
{
    probe_mc(s);
    if (!preflight_payload(s)) {
        s->manifest_count = 0u;
        s->autonomous_available = 0;
        s->tins_preflight_ok = 0;
    }
}

static int invalidate_self(TitanInstallerState *s)
{
    int fd;
    const char marker[] = "consumed=1\n";
    fd = installer_diag_open(s->consumed_path, O_WRONLY | O_CREAT | O_TRUNC, 0666, "OPEN_CONSUMED_MARKER");
    if (fd < 0) return 0;
    if (installer_diag_write(fd, marker, sizeof(marker)-1u, s->consumed_path, "WRITE_CONSUMED_MARKER") != (int)(sizeof(marker)-1u)) {
        close(fd);
        remove(s->consumed_path);
        return 0;
    }
    close(fd);
    /* Best effort only: the consumed marker is authoritative. Deleting the
     * running ELF is constrained to the installer directory. */
    if (s->installer_root[0] != '\0') {
        unsigned int root_len = (unsigned int)strlen(s->installer_root);
        if (strncmp(s->executable_path, s->installer_root, root_len) == 0 && s->executable_path[root_len] == '/')
            (void)remove(s->executable_path);
    }
    return 1;
}

int TitanInstaller_Run(int argc, char *argv[])
{
    TitanInstallerState s;
    TitanResult result;
    int install_result;
    int diag_flush_pending = 0;
    const char *diag_flush_result = 0;
    const char *diag_region = 0;
    uint64_t frame_start_ms;
    uint64_t input_start_ms;
    uint64_t input_elapsed_ms;
    uint64_t install_start_ms;
    uint64_t install_elapsed_ms;
    uint64_t render_start_ms;
    uint64_t render_elapsed_ms;
    uint64_t frame_end_ms;
    unsigned int target_frame_ms;

    memset(&s,0,sizeof(s));
    s.page = TITAN_INSTALLER_PAGE_VIDEO_OUTPUT;
    s.method = TITAN_INSTALLER_METHOD_SEMI_AUTONOMOUS;
    s.memory_card = 0;
    s.last_rendered_page = -1;
    s.hero_atlas_family_a = -1;
    s.hero_atlas_family_b = -1;
    if (argc > 0 && argv && argv[0]) copy_string(s.executable_path,sizeof(s.executable_path),argv[0]);

    result = TitanPlatform_Initialize();
    if (result != TITAN_RESULT_OK) return 1;
    TitanInstallerDiag_Start(TITAN_INSTALLER_VERSION, s.executable_path);
    TitanInstallerDiag_Logf("INFO", "PLATFORM_READY", "result=%d", (int)result);
    TitanInstallerDiag_Logf(
        "INFO", "DIAG_CONFIG",
        "copy_buffer=%u io_frame_budget=%u io_budget_ms=%u meta_steps=%u wave_clock=frame_counter logger=ram_ring_128k",
        TITAN_INSTALLER_COPY_BUFFER, TITAN_INSTALLER_IO_FRAME_BUDGET,
        TITAN_INSTALLER_IO_FRAME_BUDGET_MS, TITAN_INSTALLER_META_STEPS_PER_FRAME
    );
    TitanTheme_Initialize();
    TitanShellBackground_Initialize();
    TitanVideoOutput_Initialize();
    /* Boot preparation is intentionally non-interactive. Video-output state
     * is read from the PS2 OSD before the first interactive page; model
     * detection is completed before the following controller illustration. */
    render_boot_splash();

    /* First choice on real hardware: inherit the launcher's already-working
     * USB stack. wLaunchELF has just read this ELF/TINS from the device, so a
     * successful rebind is stronger evidence than any module-return heuristic.
     * Crucially, this path does NOT reset the IOP. */
    s.storage_ready = adopt_launcher_storage(&s);
    if (s.storage_ready) {
        result = TitanInput_InitializePreserveIop(0,0);
        if (result == TITAN_RESULT_OK) {
            initialize_memory_card_services(0);
        } else {
            /* The inherited USB worked but the inherited PAD service did not.
             * Release EE-side clients before the clean-reset fallback. */
            (void)fileXioExit();
            (void)SifLoadFileExit();
            s.storage_ready = 0;
            s.usb_inherited_iop = 0;
        }
    }

    if (!s.storage_ready) {
        result = TitanInput_Initialize(0,0);
        if (result != TITAN_RESULT_OK && !TitanInput_IsIopReady()) {
            copy_string(s.error,sizeof(s.error),"Não foi possível inicializar o controle/IOP."); s.page=TITAN_INSTALLER_PAGE_ERROR; render(&s); SleepThread(); return 1;
        }
        s.storage_ready = initialize_storage(&s, TitanInput_UsesExtendedModules());
    }
    if (s.storage_ready && path_exists(s.consumed_path)) {
        char kernel_path[TITAN_INSTALLER_PATH_CAP];
        char *boot_argv[3];
        build_usb_path(s.usb_prefix, "kernel.elf", kernel_path, sizeof(kernel_path));
        boot_argv[0] = kernel_path;
        boot_argv[1] = "--installer-consumed";
        boot_argv[2] = 0;
        LoadExecPS2(kernel_path, 2, boot_argv);
        return 0;
    }
    TitanInstallerModel_Detect(&s.console);
    /* Normal startup keeps TINS parsing interactive. The sole exception is a
     * validated transaction journal from an interrupted autonomous install:
     * recovery must run before presenting another installation choice. */
    {
        int recovery = fmcb_prepare_interrupted_recovery(&s);
        if (recovery < 0) {
            TitanInstallerDiag_BindStorage(s.usb_prefix, s.memory_card);
            render(&s);
        }
    }
    probe_mc(&s);
    TitanInstallerDiag_BindStorage(s.usb_prefix, s.memory_card);
    TitanInstallerDiag_Logf(
        "INFO", "HARDWARE",
        "storage_ready=%d inherited_iop=%d usb=%s probe=%d probe_errno=%d mc0=%d mc1=%d model=%s model_id_valid=%d model_id=0x%04X romver=%s family=%d",
        s.storage_ready, s.usb_inherited_iop, s.usb_prefix, s.usb_container_probe,
        s.usb_probe_errno, s.mc0_ready, s.mc1_ready, s.console.model_name,
        s.console.model_id_valid, s.console.model_id, s.console.romver, (int)s.console.family
    );
    render(&s);

    for (;;) {
        frame_start_ms = TitanTime_NowMilliseconds();
        input_elapsed_ms = 0u;
        install_elapsed_ms = 0u;
        render_elapsed_ms = 0u;
        s.frame++;
        TitanInstallerDiag_BeginFrame(s.frame);
        input_start_ms = TitanTime_NowMilliseconds();
        TitanInput_Update();
        input_elapsed_ms = TitanTime_NowMilliseconds() - input_start_ms;
        if (s.page == TITAN_INSTALLER_PAGE_INSTALLING) {
            /* v2.0.8: filesystem RPC ownership stays on this main EE thread.
             * Pump only a bounded I/O/time quantum, then return to input,
             * waves and rendering. This preserves the hardware-safe single-
             * owner model without throttling bulk files to one tiny step per
             * frame. */
            if (TitanInput_WasPressed(TITAN_INPUT_BUTTON_CIRCLE))
                s.install_job.cancel_requested = 1;
            install_start_ms = TitanTime_NowMilliseconds();
            install_result = install_job_pump_main_thread(&s);
            install_elapsed_ms = TitanTime_NowMilliseconds() - install_start_ms;
            if (install_result > 0) {
                copy_string(s.status,sizeof(s.status),"Validando instalação concluída...");
                TitanInstallerDiag_Logf(
                    "INFO", "INSTALL_TRANSACTION_SUCCESS",
                    "progress=%u work_done=%llu work_total=%llu",
                    s.progress, s.install_job.work_done, s.install_job.work_total
                );
                if (invalidate_self(&s)) {
                    s.page = TITAN_INSTALLER_PAGE_COMPLETE;
                    diag_flush_pending = 1;
                    diag_flush_result = "SUCCESS";
                } else {
                    copy_string(s.error,sizeof(s.error),"A instalação foi validada, mas o instalador não conseguiu gravar seu marcador de autoinvalidação. Nenhum arquivo do instalador foi apagado.");
                    TitanInstallerDiag_Logf("ERROR", "SELF_INVALIDATION_FAILED", "path=%s", s.consumed_path);
                    s.page = TITAN_INSTALLER_PAGE_ERROR;
                    diag_flush_pending = 1;
                    diag_flush_result = "SELF_INVALIDATION_FAILED";
                }
            } else if (install_result < 0) {
                if (s.error[0] == '\0')
                    copy_string(s.error,sizeof(s.error),"A transação de instalação falhou e o estado anterior foi restaurado quando possível.");
                TitanInstallerDiag_Logf(
                    "ERROR", "INSTALL_TRANSACTION_FAILED",
                    "error=%s phase=%s entry=%u/%u progress=%u posix_errno=%d filexio_result=%d",
                    s.error, install_phase_name(s.install_job.phase),
                    s.install_job.entry_index < s.manifest_count ? s.install_job.entry_index + 1u : s.manifest_count,
                    s.manifest_count, s.progress, s.install_job.last_posix_errno, s.install_job.last_filexio_result
                );
                s.page = TITAN_INSTALLER_PAGE_ERROR;
                diag_flush_pending = 1;
                diag_flush_result = "INSTALL_FAILED";
            }
        }

        if (s.page == TITAN_INSTALLER_PAGE_VIDEO_OUTPUT && s.video_test_active) {
            if (s.video_test_frames_remaining > 0u)
                s.video_test_frames_remaining--;
            if (s.video_test_frames_remaining == 0u) {
                (void)TitanVideoOutput_SetShellProgressive(s.video_test_previous_progressive);
                s.hero_atlas_ready = 0;
                s.video_test_active = 0;
                copy_string(s.video_status, sizeof(s.video_status), "Tempo esgotado. O modo de vídeo anterior foi restaurado.");
                s.selection = 0;
            }
        }

        if (TitanInput_WasPressed(TITAN_INPUT_BUTTON_UP)) {
            if (s.page==TITAN_INSTALLER_PAGE_VIDEO_OUTPUT && !s.video_test_active) s.selection--;
            else if (s.page==TITAN_INSTALLER_PAGE_LANGUAGE || s.page==TITAN_INSTALLER_PAGE_METHOD || s.page==TITAN_INSTALLER_PAGE_MEMORY_CARD) s.selection--;
            else if (s.page==TITAN_INSTALLER_PAGE_TERMS && s.terms_scroll>0) s.terms_scroll--;
        }
        if (TitanInput_WasPressed(TITAN_INPUT_BUTTON_DOWN)) {
            if (s.page==TITAN_INSTALLER_PAGE_VIDEO_OUTPUT && !s.video_test_active) s.selection++;
            else if (s.page==TITAN_INSTALLER_PAGE_LANGUAGE || s.page==TITAN_INSTALLER_PAGE_METHOD || s.page==TITAN_INSTALLER_PAGE_MEMORY_CARD) s.selection++;
            else if (s.page==TITAN_INSTALLER_PAGE_TERMS && s.terms_scroll<(int)(sizeof(g_vlombis_terms)/sizeof(g_vlombis_terms[0]))-1) s.terms_scroll++;
        }
        if (s.selection < 0) s.selection = 0;
        if (s.page==TITAN_INSTALLER_PAGE_LANGUAGE && s.selection>2) s.selection=2;
        else if ((s.page==TITAN_INSTALLER_PAGE_VIDEO_OUTPUT || s.page==TITAN_INSTALLER_PAGE_MEMORY_CARD) && s.selection>1) s.selection=1;
        else if (s.page==TITAN_INSTALLER_PAGE_METHOD) {
            if (!s.autonomous_available) s.selection=0;
            else if (s.selection>1) s.selection=1;
        }

        if (TitanInput_WasPressed(TITAN_INPUT_BUTTON_CIRCLE)) {
            if (s.page == TITAN_INSTALLER_PAGE_VIDEO_OUTPUT && s.video_test_active) {
                (void)TitanVideoOutput_SetShellProgressive(s.video_test_previous_progressive);
                s.hero_atlas_ready = 0;
                s.video_test_active = 0;
                s.video_test_frames_remaining = 0u;
                copy_string(s.video_status, sizeof(s.video_status), "Modo 720p cancelado. Saída anterior restaurada.");
                s.selection = 0;
            } else if (s.page>TITAN_INSTALLER_PAGE_VIDEO_OUTPUT && s.page<TITAN_INSTALLER_PAGE_INSTALLING) {
                s.page=(TitanInstallerPage)(s.page-1);
                if (s.page == TITAN_INSTALLER_PAGE_VIDEO_OUTPUT)
                    s.selection = TitanVideoOutput_IsShellProgressive() ? 1 : 0;
                else
                    s.selection=0;
            } else if (s.page==TITAN_INSTALLER_PAGE_ERROR) { update_preflight(&s); s.page=TITAN_INSTALLER_PAGE_PREFLIGHT; }
        }
        if (TitanInput_WasPressed(TITAN_INPUT_BUTTON_CROSS)) {
            switch(s.page) {
                case TITAN_INSTALLER_PAGE_VIDEO_OUTPUT:
                    if (s.video_test_active) {
                        s.video_test_active = 0;
                        s.video_test_frames_remaining = 0u;
                        copy_string(s.video_status, sizeof(s.video_status), "720p confirmado para esta sessão do instalador.");
                        s.page = TITAN_INSTALLER_PAGE_CONTROLLER;
                        s.selection = 0;
                    } else if (s.selection == 0) {
                        if (TitanVideoOutput_SetShellProgressive(0) == TITAN_RESULT_OK) {
                            s.hero_atlas_ready = 0;
                            copy_string(s.video_status, sizeof(s.video_status), "Modo interlaçado mantido para esta sessão.");
                            s.page = TITAN_INSTALLER_PAGE_CONTROLLER;
                            s.selection = 0;
                        } else {
                            copy_string(s.video_status, sizeof(s.video_status), "Não foi possível restaurar o modo de vídeo padrão.");
                        }
                    } else if (TitanVideoOutput_CanTestProgressive()) {
                        TitanResult video_result;
                        s.video_test_previous_progressive = TitanVideoOutput_IsShellProgressive();
                        video_result = TitanVideoOutput_SetShellProgressive(1);
                        if (video_result == TITAN_RESULT_OK) {
                            s.hero_atlas_ready = 0;
                            s.video_test_active = 1;
                            s.video_test_frames_remaining = TITAN_INSTALLER_VIDEO_TEST_FRAMES;
                            copy_string(s.video_status, sizeof(s.video_status), "720p temporário ativado; confirme se a imagem estiver visível.");
                        } else {
                            copy_string(s.video_status, sizeof(s.video_status), "Falha ao ativar 720p. O modo anterior foi preservado.");
                        }
                    }
                    break;
                case TITAN_INSTALLER_PAGE_CONTROLLER: s.page=TITAN_INSTALLER_PAGE_LANGUAGE; s.selection=0; break;
                case TITAN_INSTALLER_PAGE_LANGUAGE:
                    if (s.selection < 0 || s.selection > 2) break;
                    s.language=s.selection; g_installer_ui_language=s.language;
                    s.page=TITAN_INSTALLER_PAGE_WELCOME; s.selection=0; break;
                case TITAN_INSTALLER_PAGE_WELCOME: update_preflight(&s); s.page=TITAN_INSTALLER_PAGE_PREFLIGHT; break;
                case TITAN_INSTALLER_PAGE_PREFLIGHT:
                    if (s.storage_ready && s.manifest_count && (s.mc0_ready||s.mc1_ready)) { s.page=TITAN_INSTALLER_PAGE_METHOD; s.selection=0; }
                    break;
                case TITAN_INSTALLER_PAGE_METHOD:
                    if (!s.autonomous_available) s.selection=0;
                    if (s.selection==1 && !s.autonomous_available) break;
                    s.method=(TitanInstallerMethod)s.selection; s.page=TITAN_INSTALLER_PAGE_MEMORY_CARD; s.selection=s.mc0_ready?0:1; break;
                case TITAN_INSTALLER_PAGE_MEMORY_CARD:
                    if ((s.selection==0&&!s.mc0_ready)||(s.selection==1&&!s.mc1_ready)) break;
                    s.memory_card=s.selection;
                    TitanInstallerDiag_SetMemoryCard(s.memory_card);
                    TitanInstallerDiag_Logf("INFO", "MEMORY_CARD_SELECTED", "mc=%d", s.memory_card);
                    { char p[64]; snprintf(p,sizeof(p),"mc%d:/BOOT/BOOT.ELF",s.memory_card); s.existing_boot=path_exists(p); }
                    s.fmcb_state=probe_fmcb_state(s.memory_card);
                    TitanInstallerDiag_Logf("INFO","FMCB_EXISTING_STATE","mc=%d state=%s",s.memory_card,fmcb_state_name(s.fmcb_state));
                    if (s.method==TITAN_INSTALLER_METHOD_AUTONOMOUS &&
                        (s.fmcb_state==TITAN_FMCB_UNKNOWN_FMCB || s.fmcb_state==TITAN_FMCB_CORRUPT_FMCB)) {
                        copy_string(s.error,sizeof(s.error),"A inicialização autônoma existente não pôde ser validada com segurança. Use a instalação semi-autônoma ou a recuperação.");
                        s.page=TITAN_INSTALLER_PAGE_ERROR; break;
                    }
                    if (s.method==TITAN_INSTALLER_METHOD_AUTONOMOUS) {
                        unsigned int required_bytes=0u, free_bytes=0u;
                        if (!fmcb_autonomous_mc_space_check(&s,&required_bytes,&free_bytes)) {
                            TitanInstallerDiag_Logf("ERROR","FMCB_MC_SPACE","mc=%d required=%u free=%u result=insufficient_or_unknown",s.memory_card,required_bytes,free_bytes);
                            copy_string(s.error,sizeof(s.error),"Espaço livre insuficiente ou não verificável no Memory Card para a instalação autônoma.");
                            s.page=TITAN_INSTALLER_PAGE_ERROR; break;
                        }
                    }
                    s.page=TITAN_INSTALLER_PAGE_BOOT_ANALYSIS; break;
                case TITAN_INSTALLER_PAGE_BOOT_ANALYSIS: s.page=TITAN_INSTALLER_PAGE_DESTINATION; break;
                case TITAN_INSTALLER_PAGE_DESTINATION: s.page=TITAN_INSTALLER_PAGE_TERMS; break;
                case TITAN_INSTALLER_PAGE_TERMS: s.page=TITAN_INSTALLER_PAGE_SUMMARY; break;
                case TITAN_INSTALLER_PAGE_SUMMARY:
                    s.progress=0u;
                    if (install_job_begin(&s)) {
                        /* FileXio remains single-owner on the main EE thread.
                         * Long RPCs run in NOWAIT mode and are polled between
                         * rendered frames by install_job_pump_main_thread(). */
                        s.page=TITAN_INSTALLER_PAGE_INSTALLING;
                    } else {
                        copy_string(s.error,sizeof(s.error),"Não foi possível preparar a transação de instalação.");
                        s.page=TITAN_INSTALLER_PAGE_ERROR;
                    }
                    break;
                case TITAN_INSTALLER_PAGE_COMPLETE: {
                    char boot[96]; char *a[2];
                    if (s.method==TITAN_INSTALLER_METHOD_AUTONOMOUS) snprintf(boot,sizeof(boot),"mc%d:/TITAN/BOOT/TITAN_FMCB_ENTRY.ELF",s.memory_card);
                    else snprintf(boot,sizeof(boot),"mc%d:/BOOT/BOOT.ELF",s.memory_card);
                    a[0]=boot; a[1]=0; LoadExecPS2(boot,1,a); break;
                }
                default: break;
            }
        }
        render_start_ms = TitanTime_NowMilliseconds();
        render(&s);
        render_elapsed_ms = TitanTime_NowMilliseconds() - render_start_ms;
        frame_end_ms = TitanTime_NowMilliseconds();
        diag_region = TitanVideoOutput_GetRegionName();
        target_frame_ms = TitanVideoOutput_IsShellProgressive()
            ? 17u
            : (diag_region != 0 && strstr(diag_region, "PAL") != 0 ? 20u : 17u);
        TitanInstallerDiag_EndFrame(
            frame_end_ms - frame_start_ms,
            input_elapsed_ms,
            install_elapsed_ms,
            render_elapsed_ms,
            target_frame_ms,
            s.progress,
            install_phase_name(s.install_job.phase),
            s.install_job.entry_index < s.manifest_count ? s.manifest[s.install_job.entry_index].path : ""
        );
        if (diag_flush_pending) {
            (void)TitanInstallerDiag_FlushFinal(diag_flush_result ? diag_flush_result : "FINAL");
            diag_flush_pending = 0;
            diag_flush_result = 0;
        }
    }
}
