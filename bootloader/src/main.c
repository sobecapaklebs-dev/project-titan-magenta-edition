#include "boot_path.h"
#include "boot_video.h"
#include "elf_validate.h"
#include "titan_boot_version.h"

#include "generated/titan_stage2_elf_data.h"

#include <debug.h>
#include <delaythread.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <sbv_patches.h>
#include <libmc.h>

#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE
#define TITAN_BOOT_DEFINED_NEWLIB_PORT_AWARE
#endif
#include <io_common.h>
#include <fileXio_rpc.h>
#ifdef TITAN_BOOT_DEFINED_NEWLIB_PORT_AWARE
#undef TITAN_BOOT_DEFINED_NEWLIB_PORT_AWARE
#undef NEWLIB_PORT_AWARE
#endif

#include "generated/titan_iomanx_irx.h"
#include "generated/titan_filexio_irx.h"
#include "generated/titan_mcman_irx.h"
#include "generated/titan_mcserv_irx.h"
#include "generated/titan_usbd_irx.h"
#include "generated/titan_bdm_irx.h"
#include "generated/titan_bdmfs_fatfs_irx.h"
#include "generated/titan_usbmass_bd_irx.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_BOOT_STAGE2_MINIMUM_ADDRESS 0x00094000u
#define TITAN_BOOT_STAGE2_MAXIMUM_ADDRESS 0x00100000u
#define TITAN_BOOT_STAGE2_PROGRAM_LOAD 1u
#define TITAN_BOOT_READY_HOLD_US 120000
#define TITAN_BOOT_IOP_RESET_ATTEMPTS 120
#define TITAN_BOOT_IOP_SYNC_ATTEMPTS 500
#define TITAN_BOOT_IOP_DELAY_US 10000
#define TITAN_BOOT_USB_PROBE_ATTEMPTS 120
#define TITAN_BOOT_USB_PROBE_DELAY_US 100000
#define TITAN_BOOT_LOG_CAPACITY 16384u
#define TITAN_BOOT_LOG_PATH "/TITAN_BOOTLOADER.LOG"
#define TITAN_BOOT_LOG_FLUSH_POLL_INTERVAL 10u

typedef struct TitanBootUsbDiag {
    int reset_result;
    int sync_result;
    int loadfile_result;
    int lmb_patch_result;
    int prefix_patch_result;
    int iomanx_id, iomanx_start;
    int filexio_id, filexio_start;
    int sio2man_id;
    int mcman_id, mcman_start;
    int mcserv_id, mcserv_start;
    int mcinit_result;
    int padman_id;
    int filexio_buffer_result;
    int usbd_id, usbd_start;
    int bdm_id, bdm_start;
    int bdmfs_id, bdmfs_start;
    int usbmass_id, usbmass_start;
    int filexio_init_result;
} TitanBootUsbDiag;

static TitanBootUsbDiag g_usb_diag;

static void TitanBoot_UpdateProgress(unsigned int percent);
static void TitanBoot_ExecEmbeddedModuleDiag(
    void *module, unsigned int module_size, int *module_id_out, int *module_start_out
);

static char g_boot_log[TITAN_BOOT_LOG_CAPACITY];
static size_t g_boot_log_length;
static unsigned int g_boot_log_sequence;
static int g_boot_log_mc_port;
static int g_boot_log_mc_ready;
static int g_boot_log_flushing;

static int TitanBoot_McWaitResult(int submit_result)
{
    int command = 0;
    int result = -1;

    if (submit_result < 0)
        return submit_result;
    if (mcSync(MC_WAIT, &command, &result) < 0)
        return -10000;
    return result;
}

static void TitanBoot_LogSelectMemoryCard(int argc, char *argv[])
{
    g_boot_log_mc_port = 0;
    if (argc > 0 && argv != 0 && argv[0] != 0 && strncmp(argv[0], "mc1:", 4u) == 0)
        g_boot_log_mc_port = 1;
}

static void TitanBoot_LogFlush(void)
{
    int fd;
    int result;

    if (!g_boot_log_mc_ready || g_boot_log_flushing || g_boot_log_length == 0u)
        return;

    g_boot_log_flushing = 1;

    result = TitanBoot_McWaitResult(mcOpen(
        g_boot_log_mc_port,
        0,
        TITAN_BOOT_LOG_PATH,
        FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC
    ));
    if (result < 0)
    {
        g_boot_log_flushing = 0;
        return;
    }
    fd = result;

    result = TitanBoot_McWaitResult(mcWrite(fd, g_boot_log, (int)g_boot_log_length));
    (void)result;
    (void)TitanBoot_McWaitResult(mcClose(fd));
    g_boot_log_flushing = 0;
}

static void TitanBoot_Logf(const char *format, ...)
{
    char line[512];
    va_list ap;
    int body_length;
    int line_length;
    size_t remaining;

    if (format == 0)
        return;

    va_start(ap, format);
    body_length = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if (body_length < 0)
        return;

    remaining = TITAN_BOOT_LOG_CAPACITY - g_boot_log_length;
    if (remaining <= 2u)
        return;

    line_length = snprintf(
        g_boot_log + g_boot_log_length,
        remaining,
        "[%04u] %s\n",
        ++g_boot_log_sequence,
        line
    );
    if (line_length < 0)
        return;

    if ((size_t)line_length >= remaining)
        g_boot_log_length = TITAN_BOOT_LOG_CAPACITY - 1u;
    else
        g_boot_log_length += (size_t)line_length;

    /* Deliberately do not flush here. Persistent logging must never become
       part of the boot critical path. Flush only on fatal error or handoff. */
}

static void TitanBoot_LogInitializeMemoryCard(void)
{
    int sio2man_id;

    /* Match wLaunchELF's basic-module ownership more closely.  Memory-card
       logging is diagnostic only: every failure here must be non-fatal. */
    TitanBoot_UpdateProgress(20u);
    sio2man_id = SifLoadModule("rom0:SIO2MAN", 0, 0);
    g_usb_diag.sio2man_id = sio2man_id;

    TitanBoot_UpdateProgress(22u);
    TitanBoot_ExecEmbeddedModuleDiag(
        titan_mcman_irx,
        size_titan_mcman_irx,
        &g_usb_diag.mcman_id,
        &g_usb_diag.mcman_start
    );

    TitanBoot_UpdateProgress(24u);
    TitanBoot_ExecEmbeddedModuleDiag(
        titan_mcserv_irx,
        size_titan_mcserv_irx,
        &g_usb_diag.mcserv_id,
        &g_usb_diag.mcserv_start
    );

    /* wLaunchELF keeps PADMAN resident in its basic module set before USB. */
    TitanBoot_UpdateProgress(25u);
    g_usb_diag.padman_id = SifLoadModule("rom0:PADMAN", 0, 0);

    /* Match wLaunchELF Reset(): FileXio client is bound before USB modules. */
    TitanBoot_UpdateProgress(26u);
    g_usb_diag.filexio_init_result = fileXioInit();
    TitanBoot_Logf("fileXioInit(pre-USB)=%d", g_usb_diag.filexio_init_result);

    TitanBoot_UpdateProgress(27u);
    if (g_usb_diag.filexio_init_result >= 0)
        g_usb_diag.filexio_buffer_result = fileXioSetRWBufferSize(128 * 1024);
    else
        g_usb_diag.filexio_buffer_result = -1;
    TitanBoot_Logf("fileXioSetRWBufferSize=%d", g_usb_diag.filexio_buffer_result);

    TitanBoot_UpdateProgress(28u);
    g_usb_diag.mcinit_result = mcInit(MC_TYPE_MC);

    if (g_usb_diag.mcinit_result >= 0)
        g_boot_log_mc_ready = 1;

    /* Do not make the logger part of the boot critical path.  A failed MC
       stack simply leaves diagnostics buffered in EE RAM and USB bootstrap
       continues. */
    /* Keep the accumulated diagnostics in EE RAM. A blocking MC flush here
       could recreate the exact pre-USB deadlock this BuildFix removes. */
}

static void TitanBoot_ExecEmbeddedModuleDiag(
    void *module,
    unsigned int module_size,
    int *module_id_out,
    int *module_start_out
)
{
    int module_result = -9999;
    int module_id = SifExecModuleBuffer(module, module_size, 0, 0, &module_result);
    if (module_id_out) *module_id_out = module_id;
    if (module_start_out) *module_start_out = module_result;
}

static int TitanBoot_InitializeUsbStack(void)
{
    int attempt;
    int ready = 0;

    memset(&g_usb_diag, 0xff, sizeof(g_usb_diag));
    TitanBoot_Logf("USB bootstrap begin");

    /* Match wLaunchELF DEV1 bootstrap semantics exactly: initialize RPC,
       perform a normal IOP reset, sync, then rebind RPC/loadfile. */
    TitanBoot_Logf("wLaunchELF-style RPC/IOP bootstrap begin");
    SifInitRpc(0);

    for (attempt = 0; attempt < TITAN_BOOT_IOP_RESET_ATTEMPTS; ++attempt)
    {
        if (SifIopReset("", 0))
        {
            ready = 1;
            break;
        }
        DelayThread(TITAN_BOOT_IOP_DELAY_US);
    }
    g_usb_diag.reset_result = ready ? 1 : 0;
    TitanBoot_Logf("IOP reset result=%d attempts=%d", g_usb_diag.reset_result, attempt + 1);
    if (!ready) return -1;

    ready = 0;
    for (attempt = 0; attempt < TITAN_BOOT_IOP_SYNC_ATTEMPTS; ++attempt)
    {
        if (SifIopSync())
        {
            ready = 1;
            break;
        }
        DelayThread(TITAN_BOOT_IOP_DELAY_US);
    }
    g_usb_diag.sync_result = ready ? 1 : 0;
    TitanBoot_Logf("IOP sync result=%d attempts=%d", g_usb_diag.sync_result, attempt + 1);
    if (!ready) return -2;

    SifInitRpc(0);
    g_usb_diag.loadfile_result = SifLoadFileInit();
    TitanBoot_Logf("SifLoadFileInit=%d", g_usb_diag.loadfile_result);
    if (g_usb_diag.loadfile_result < 0) return -3;

    g_usb_diag.lmb_patch_result = sbv_patch_enable_lmb();
    g_usb_diag.prefix_patch_result = sbv_patch_disable_prefix_check();
    TitanBoot_Logf("SBV patches lmb=%d prefix=%d", g_usb_diag.lmb_patch_result, g_usb_diag.prefix_patch_result);

    TitanBoot_UpdateProgress(16u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_iomanx_irx, size_titan_iomanx_irx,
        &g_usb_diag.iomanx_id, &g_usb_diag.iomanx_start);
    TitanBoot_Logf("IOMANX id=%d start=%d", g_usb_diag.iomanx_id, g_usb_diag.iomanx_start);

    TitanBoot_UpdateProgress(18u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_filexio_irx, size_titan_filexio_irx,
        &g_usb_diag.filexio_id, &g_usb_diag.filexio_start);
    TitanBoot_Logf("FILEXIO id=%d start=%d", g_usb_diag.filexio_id, g_usb_diag.filexio_start);

    /* Logging is optional and cannot stop the boot. */
    TitanBoot_LogInitializeMemoryCard();

    TitanBoot_UpdateProgress(30u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_usbd_irx, size_titan_usbd_irx,
        &g_usb_diag.usbd_id, &g_usb_diag.usbd_start);
    TitanBoot_Logf("USBD id=%d start=%d", g_usb_diag.usbd_id, g_usb_diag.usbd_start);

    TitanBoot_UpdateProgress(35u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_bdm_irx, size_titan_bdm_irx,
        &g_usb_diag.bdm_id, &g_usb_diag.bdm_start);
    TitanBoot_Logf("BDM id=%d start=%d", g_usb_diag.bdm_id, g_usb_diag.bdm_start);

    TitanBoot_UpdateProgress(40u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_bdmfs_fatfs_irx, size_titan_bdmfs_fatfs_irx,
        &g_usb_diag.bdmfs_id, &g_usb_diag.bdmfs_start);
    TitanBoot_Logf("BDMFS_FATFS id=%d start=%d", g_usb_diag.bdmfs_id, g_usb_diag.bdmfs_start);

    TitanBoot_UpdateProgress(45u);
    TitanBoot_ExecEmbeddedModuleDiag(titan_usbmass_bd_irx, size_titan_usbmass_bd_irx,
        &g_usb_diag.usbmass_id, &g_usb_diag.usbmass_start);
    TitanBoot_Logf("USBMASS_BD id=%d start=%d", g_usb_diag.usbmass_id, g_usb_diag.usbmass_start);

    TitanBoot_UpdateProgress(48u);
    TitanBoot_Logf("USB mass module chain returned");
    SifLoadFileExit();
    TitanBoot_Logf("USB bootstrap module phase complete");
    return 0;
}

static const char *TitanBoot_GetOverridePath(
    int argc,
    char *argv[]
)
{
    static const char prefix[] = "--kernel=";
    int index;

    if (argv == 0)
    {
        return 0;
    }

    for (index = 1; index < argc; index++)
    {
        if (
            argv[index] != 0 &&
            strncmp(
                argv[index],
                prefix,
                sizeof(prefix) - 1u
            ) == 0 &&
            argv[index][sizeof(prefix) - 1u] != '\0'
        )
        {
            return argv[index] + sizeof(prefix) - 1u;
        }
    }

    return 0;
}

static void TitanBoot_Halt(void)
{
    for (;;)
    {
        SleepThread();
    }
}

static void TitanBoot_ShowError(
    const char *stage,
    int detail,
    const char *path,
    char candidates[][TITAN_BOOT_MAX_PATH],
    size_t candidate_count
)
{
    size_t index;

    TitanBoot_Logf("FATAL stage=%s detail=%d path=%s", stage != 0 ? stage : "unknown", detail, path != 0 ? path : "(none)");
    TitanBoot_LogFlush();
    init_scr();

    scr_printf(
        "\nPROJECT TITAN BOOTLOADER %s\n"
        "Kernel alvo: %s / %s\n"
        "----------------------------------------\n\n"
        "FALHA DE INICIALIZACAO\n"
        "Etapa: %s\n"
        "Codigo: %d\n",
        TITAN_BOOTLOADER_VERSION_STRING,
        TITAN_BOOTLOADER_KERNEL_TARGET,
        TITAN_BOOTLOADER_UI_TARGET,
        stage != 0 ? stage : "unknown",
        detail
    );

    if (path != 0 && path[0] != '\0')
    {
        scr_printf("Caminho: %s\n", path);
    }

    if (candidates != 0 && candidate_count > 0u)
    {
        scr_printf("\nCaminhos tentados:\n");

        for (
            index = 0u;
            index < candidate_count && index < 8u;
            index++
        )
        {
            scr_printf(" - %s\n", candidates[index]);
        }
    }

    scr_printf(
        "\nDiagnostico USB/IOP:\n"
        " reset=%d sync=%d loadfile=%d lmb=%d prefix=%d\n"
        " iomanX=%d/%d fileXio=%d/%d\n"
        " sio2man=%d mcman=%d/%d mcserv=%d/%d padman=%d\n"
        " fileXioInit=%d rwbuf=%d mcInit=%d logReady=%d\n"
        " usbd=%d/%d bdm=%d/%d bdmfs=%d/%d\n"
        " usbmass_bd=%d/%d fileXioInit=%d\n"
        "\nKernel esperado na raiz do USB: massN:/kernel.elf\n"
        "O sistema foi interrompido com seguranca.\n",
        g_usb_diag.reset_result,
        g_usb_diag.sync_result,
        g_usb_diag.loadfile_result,
        g_usb_diag.lmb_patch_result,
        g_usb_diag.prefix_patch_result,
        g_usb_diag.iomanx_id, g_usb_diag.iomanx_start,
        g_usb_diag.filexio_id, g_usb_diag.filexio_start,
        g_usb_diag.sio2man_id,
        g_usb_diag.mcman_id, g_usb_diag.mcman_start,
        g_usb_diag.mcserv_id, g_usb_diag.mcserv_start,
        g_usb_diag.padman_id,
        g_usb_diag.filexio_init_result,
        g_usb_diag.filexio_buffer_result,
        g_usb_diag.mcinit_result,
        g_boot_log_mc_ready,
        g_usb_diag.usbd_id, g_usb_diag.usbd_start,
        g_usb_diag.bdm_id, g_usb_diag.bdm_start,
        g_usb_diag.bdmfs_id, g_usb_diag.bdmfs_start,
        g_usb_diag.usbmass_id, g_usb_diag.usbmass_start,
        g_usb_diag.filexio_init_result
    );

    TitanBoot_Halt();
}

static void TitanBoot_UpdateProgress(
    unsigned int percent
)
{
    const int result = TitanBootVideo_SetProgress(percent);

    if (result < 0)
    {
        TitanBoot_ShowError(
            "boot progress",
            result,
            0,
            0,
            0u
        );
    }
}

static int TitanBoot_CopyStage2(
    uint32_t *entry_point
)
{
    TitanElfValidationPolicy policy;
    TitanElfImageInfo info;
    TitanElfValidationResult validation;
    unsigned int index;

    if (entry_point == 0)
    {
        return -1;
    }

    policy.minimum_address = TITAN_BOOT_STAGE2_MINIMUM_ADDRESS;
    policy.maximum_address = TITAN_BOOT_STAGE2_MAXIMUM_ADDRESS;
    policy.maximum_program_headers = 32u;
    policy.require_executable_entry = 1;

    validation = TitanElf_ValidateMemory(
        titan_stage2_elf,
        (size_t)size_titan_stage2_elf,
        &policy,
        &info
    );
    if (validation != TITAN_ELF_VALID)
    {
        return -2;
    }

    for (index = 0u; index < info.program_header_count; index++)
    {
        TitanElf32ProgramHeader program;

        if (!TitanElf_ReadMemoryProgramHeader(
            titan_stage2_elf,
            (size_t)size_titan_stage2_elf,
            index,
            &program
        ))
        {
            return -3;
        }

        if (program.type != TITAN_BOOT_STAGE2_PROGRAM_LOAD)
        {
            continue;
        }

        memcpy(
            (void *)(uintptr_t)program.virtual_address,
            titan_stage2_elf + program.offset,
            program.file_size
        );

        if (program.memory_size > program.file_size)
        {
            memset(
                (void *)(uintptr_t)(
                    program.virtual_address + program.file_size
                ),
                0,
                program.memory_size - program.file_size
            );
        }
    }

    *entry_point = info.entry_point;
    return 0;
}

int main(
    int argc,
    char *argv[]
)
{
    char candidates[
        TITAN_BOOT_MAX_CANDIDATES
    ][TITAN_BOOT_MAX_PATH];
    char selected_path[TITAN_BOOT_MAX_PATH];
    char version_argument[64];
    char *stage2_arguments[2];
    const char *override_path;
    TitanElfValidationResult validation;
    uint32_t stage2_entry;
    size_t candidate_count;
    size_t index;
    int result;

    TitanBoot_LogSelectMemoryCard(argc, argv);
    TitanBoot_Logf("Project Titan bootloader %s start argc=%d argv0=%s", TITAN_BOOTLOADER_VERSION_STRING, argc, (argc > 0 && argv != 0 && argv[0] != 0) ? argv[0] : "(none)");

    result = TitanBootVideo_Show();

    if (result < 0)
    {
        TitanBoot_ShowError(
            "boot video",
            result,
            0,
            0,
            0u
        );
    }

    TitanBoot_UpdateProgress(8u);
    SifInitRpc(0);
    TitanBoot_UpdateProgress(12u);

    TitanBoot_Logf("boot video initialized result=%d", result);
    result = TitanBoot_InitializeUsbStack();
    if (result < 0)
    {
        TitanBoot_ShowError(
            "USB bootstrap",
            result,
            0,
            0,
            0u
        );
    }
    TitanBoot_UpdateProgress(50u);

    override_path = TitanBoot_GetOverridePath(
        argc,
        argv
    );

    TitanBoot_Logf("kernel override=%s", override_path != 0 ? override_path : "(none)");
    candidate_count = TitanBootPath_BuildCandidates(
        argc > 0 && argv != 0 ? argv[0] : 0,
        override_path,
        candidates,
        TITAN_BOOT_MAX_CANDIDATES
    );
    TitanBoot_UpdateProgress(52u);
    TitanBoot_Logf("kernel candidate count=%u", (unsigned int)candidate_count);
    for (index = 0u; index < candidate_count; ++index)
        TitanBoot_Logf("candidate[%u]=%s", (unsigned int)index, candidates[index]);

    selected_path[0] = '\0';
    validation = TITAN_ELF_ERROR_OPEN;

    {
        unsigned int probe_attempt;
        for (probe_attempt = 0u;
             probe_attempt < TITAN_BOOT_USB_PROBE_ATTEMPTS && selected_path[0] == '\0';
             ++probe_attempt)
        {
            if (probe_attempt == 0u || (probe_attempt % TITAN_BOOT_LOG_FLUSH_POLL_INTERVAL) == 0u)
                TitanBoot_Logf("USB probe attempt=%u/%u", probe_attempt + 1u, TITAN_BOOT_USB_PROBE_ATTEMPTS);

            for (index = 0u; index < candidate_count; index++)
            {
                const unsigned int search_progress =
                    52u + (unsigned int)(
                        ((probe_attempt + 1u) * 18u) /
                        TITAN_BOOT_USB_PROBE_ATTEMPTS
                    );

                TitanBoot_UpdateProgress(search_progress);
                validation = TitanElf_ValidateFile(candidates[index]);

                if (probe_attempt == 0u || validation != TITAN_ELF_ERROR_OPEN)
                    TitanBoot_Logf("probe path=%s validation=%d", candidates[index], (int)validation);

                if (validation == TITAN_ELF_VALID)
                {
                    TitanBoot_Logf("kernel found path=%s attempt=%u", candidates[index], probe_attempt + 1u);
                    strcpy(selected_path, candidates[index]);
                    break;
                }

                if (validation != TITAN_ELF_ERROR_OPEN)
                {
                    TitanBoot_ShowError(
                        TitanElf_GetValidationName(validation),
                        (int)validation,
                        candidates[index],
                        candidates,
                        candidate_count
                    );
                }
            }

            if (selected_path[0] == '\0')
            {
                DelayThread(TITAN_BOOT_USB_PROBE_DELAY_US);
            }
        }
    }

    if (selected_path[0] == '\0')
    {
        TitanBoot_ShowError(
            "kernel.elf not found",
            (int)validation,
            0,
            candidates,
            candidate_count
        );
    }

    TitanBoot_UpdateProgress(78u);
    TitanBoot_Logf("kernel selected=%s", selected_path);

    result = TitanBoot_CopyStage2(&stage2_entry);

    if (result < 0)
    {
        TitanBoot_ShowError(
            "stage2 relocation",
            result,
            selected_path,
            0,
            0u
        );
    }

    TitanBoot_Logf("stage2 relocation result=%d entry=0x%08X", result, (unsigned int)stage2_entry);
    TitanBoot_UpdateProgress(90u);

    snprintf(
        version_argument,
        sizeof(version_argument),
        "--bootloader-version=%s",
        TITAN_BOOTLOADER_VERSION_STRING
    );

    stage2_arguments[0] = selected_path;
    stage2_arguments[1] = version_argument;

    TitanBoot_UpdateProgress(100u);
    TitanBoot_Logf("handoff ready kernel=%s stage2=0x%08X", selected_path, (unsigned int)stage2_entry);
    TitanBoot_LogFlush();
    DelayThread(TITAN_BOOT_READY_HOLD_US);

    /* Keep the logo framebuffer active during stage2. The kernel draws the
       same logo on its first frame and performs the final fade to Health. */
    TitanBoot_Logf("SifExitRpc and ExecPS2 handoff");
    TitanBoot_LogFlush();
    SifExitRpc();
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);

    result = ExecPS2(
        (void *)(uintptr_t)stage2_entry,
        0,
        2,
        stage2_arguments
    );

    TitanBoot_ShowError(
        "stage2 execution",
        result,
        selected_path,
        0,
        0u
    );

    return 0;
}
