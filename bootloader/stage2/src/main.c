#include "titan_boot_version.h"
#include "stage2_video.h"

/*
 * The low-memory second-stage strategy follows the PS2SDK elf-loader design.
 * PS2SDK is distributed under the Academic Free License 2.0; see
 * licenses/PS2SDK-AFL-2.0.txt and THIRD_PARTY_NOTICES.md.
 */

#include <debug.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <delaythread.h>
#include <sbv_patches.h>

#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE
#define TITAN_STAGE2_DEFINED_NEWLIB_PORT_AWARE
#endif
#include <fileXio_rpc.h>
#ifdef TITAN_STAGE2_DEFINED_NEWLIB_PORT_AWARE
#undef TITAN_STAGE2_DEFINED_NEWLIB_PORT_AWARE
#undef NEWLIB_PORT_AWARE
#endif

#include "generated/titan_stage2_iomanx_irx.h"
#include "generated/titan_stage2_filexio_irx.h"
#include "generated/titan_stage2_usbd_irx.h"
#include "generated/titan_stage2_bdm_irx.h"
#include "generated/titan_stage2_bdmfs_fatfs_irx.h"
#include "generated/titan_stage2_usbmass_bd_irx.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TITAN_STAGE2_LOG_PATH "mass:/TITAN/stage2_handoff.log"
#define TITAN_STAGE2_LOG_FALLBACK "mass:/stage2_handoff.log"

/* TITAN_RC4_COLD_IOP_HANDOFF_R1 */
#define TITAN_STAGE2_IOP_RESET_ATTEMPTS 120
#define TITAN_STAGE2_IOP_SYNC_ATTEMPTS 500
#define TITAN_STAGE2_IOP_DELAY_US 10000
#define TITAN_STAGE2_USB_PROBE_ATTEMPTS 120
#define TITAN_STAGE2_USB_PROBE_DELAY_US 100000

typedef struct TitanStage2IopDiag
{
    int reset_attempts;
    int sync_attempts;
    int loadfile_result;
    int lmb_patch_result;
    int prefix_patch_result;
    int iomanx_id, iomanx_result;
    int filexio_id, filexio_result;
    int filexio_init_result;
    int filexio_buffer_result;
    int usbd_id, usbd_result;
    int bdm_id, bdm_result;
    int bdmfs_id, bdmfs_result;
    int usbmass_id, usbmass_result;
    int probe_attempts;
} TitanStage2IopDiag;

static TitanStage2IopDiag g_iop_diag;

static void TitanStage2_WriteLog(
    int truncate_file,
    const char *stage,
    const char *target,
    int result,
    unsigned int entry_point,
    unsigned int global_pointer
)
{
    char message[384];
    int flags;
    int descriptor;
    int length;

    flags = O_WRONLY | O_CREAT | (truncate_file ? O_TRUNC : O_APPEND);
    descriptor = open(TITAN_STAGE2_LOG_PATH, flags, 0666);
    if (descriptor < 0)
    {
        descriptor = open(TITAN_STAGE2_LOG_FALLBACK, flags, 0666);
    }
    if (descriptor < 0)
    {
        return;
    }

    length = snprintf(
        message,
        sizeof(message),
        "stage=%s\ntarget=%s\nresult=%d\nepc=0x%08x\ngp=0x%08x\n---\n",
        stage != 0 ? stage : "unknown",
        target != 0 ? target : "(null)",
        result,
        entry_point,
        global_pointer
    );
    if (length > 0)
    {
        if ((unsigned int)length >= sizeof(message))
        {
            length = (int)sizeof(message) - 1;
        }
        {
            size_t written = 0u;
            while (written < (size_t)length)
            {
                ssize_t amount = write(
                    descriptor,
                    message + written,
                    (size_t)length - written
                );
                if (amount <= 0)
                {
                    break;
                }
                written += (size_t)amount;
            }
        }
    }
    close(descriptor);
}

static int TitanStage2_ModuleStayedResident(int result)
{
    return result == 0 || result == 2;
}

static int TitanStage2_ExecEmbeddedModule(
    const unsigned char *data,
    unsigned int size,
    int *id_out,
    int *result_out
)
{
    int id;
    int result = -9999;

    if (data == 0 || size == 0u)
        return 0;

    id = SifExecModuleBuffer(
        (void *)data,
        (int)size,
        0,
        0,
        &result
    );

    if (id_out != 0) *id_out = id;
    if (result_out != 0) *result_out = result;
    return id >= 0 && TitanStage2_ModuleStayedResident(result);
}

static int TitanStage2_IsMassTarget(const char *path)
{
    return path != 0 && (
        strncmp(path, "mass:", 5u) == 0 ||
        strncmp(path, "mass0:", 6u) == 0 ||
        strncmp(path, "usb:", 4u) == 0
    );
}

static int TitanStage2_ProbeTarget(const char *path)
{
    int attempt;

    for (attempt = 1; attempt <= TITAN_STAGE2_USB_PROBE_ATTEMPTS; ++attempt)
    {
        int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            close(fd);
            g_iop_diag.probe_attempts = attempt;
            return 1;
        }
        DelayThread(TITAN_STAGE2_USB_PROBE_DELAY_US);
    }

    g_iop_diag.probe_attempts = TITAN_STAGE2_USB_PROBE_ATTEMPTS;
    return 0;
}

static void TitanStage2_FlushIopDiag(const char *target)
{
    TitanStage2_WriteLog(0, "cold-iop-reset", target, 0,
        (unsigned int)g_iop_diag.reset_attempts, 0u);
    TitanStage2_WriteLog(0, "cold-iop-sync", target, 0,
        (unsigned int)g_iop_diag.sync_attempts, 0u);
    TitanStage2_WriteLog(0, "cold-iop-loadfile", target,
        g_iop_diag.loadfile_result,
        (unsigned int)g_iop_diag.lmb_patch_result,
        (unsigned int)g_iop_diag.prefix_patch_result);
    TitanStage2_WriteLog(0, "cold-iop-iomanx", target,
        g_iop_diag.iomanx_result,
        (unsigned int)g_iop_diag.iomanx_id, 0u);
    TitanStage2_WriteLog(0, "cold-iop-filexio", target,
        g_iop_diag.filexio_result,
        (unsigned int)g_iop_diag.filexio_id,
        (unsigned int)g_iop_diag.filexio_init_result);
    TitanStage2_WriteLog(0, "cold-iop-usbd", target,
        g_iop_diag.usbd_result,
        (unsigned int)g_iop_diag.usbd_id, 0u);
    TitanStage2_WriteLog(0, "cold-iop-bdm", target,
        g_iop_diag.bdm_result,
        (unsigned int)g_iop_diag.bdm_id, 0u);
    TitanStage2_WriteLog(0, "cold-iop-bdmfs", target,
        g_iop_diag.bdmfs_result,
        (unsigned int)g_iop_diag.bdmfs_id, 0u);
    TitanStage2_WriteLog(0, "cold-iop-usbmass", target,
        g_iop_diag.usbmass_result,
        (unsigned int)g_iop_diag.usbmass_id,
        (unsigned int)g_iop_diag.probe_attempts);
    TitanStage2_WriteLog(0, "cold-iop-ready", target, 0, 0u, 0u);
}

static int TitanStage2_RebuildMinimalIop(const char *target)
{
    int attempt;
    int ready;

    memset(&g_iop_diag, 0xff, sizeof(g_iop_diag));

    SifInitRpc(0);
    ready = 0;
    for (attempt = 1; attempt <= TITAN_STAGE2_IOP_RESET_ATTEMPTS; ++attempt)
    {
        if (SifIopReset("", 0))
        {
            ready = 1;
            break;
        }
        DelayThread(TITAN_STAGE2_IOP_DELAY_US);
    }
    g_iop_diag.reset_attempts = attempt;
    if (!ready) return -100;

    ready = 0;
    for (attempt = 1; attempt <= TITAN_STAGE2_IOP_SYNC_ATTEMPTS; ++attempt)
    {
        if (SifIopSync())
        {
            ready = 1;
            break;
        }
        DelayThread(TITAN_STAGE2_IOP_DELAY_US);
    }
    g_iop_diag.sync_attempts = attempt;
    if (!ready) return -101;

    SifInitRpc(0);
    g_iop_diag.loadfile_result = SifLoadFileInit();
    if (g_iop_diag.loadfile_result < 0) return -102;

    g_iop_diag.lmb_patch_result = sbv_patch_enable_lmb();
    g_iop_diag.prefix_patch_result = sbv_patch_disable_prefix_check();
    if (g_iop_diag.lmb_patch_result < 0) return -103;

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_iomanx_irx,
        size_titan_stage2_iomanx_irx,
        &g_iop_diag.iomanx_id,
        &g_iop_diag.iomanx_result
    )) return -104;

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_filexio_irx,
        size_titan_stage2_filexio_irx,
        &g_iop_diag.filexio_id,
        &g_iop_diag.filexio_result
    )) return -105;

    g_iop_diag.filexio_init_result = fileXioInit();
    if (g_iop_diag.filexio_init_result < 0) return -106;
    g_iop_diag.filexio_buffer_result = fileXioSetRWBufferSize(128 * 1024);

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_usbd_irx,
        size_titan_stage2_usbd_irx,
        &g_iop_diag.usbd_id,
        &g_iop_diag.usbd_result
    )) return -107;

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_bdm_irx,
        size_titan_stage2_bdm_irx,
        &g_iop_diag.bdm_id,
        &g_iop_diag.bdm_result
    )) return -108;

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_bdmfs_fatfs_irx,
        size_titan_stage2_bdmfs_fatfs_irx,
        &g_iop_diag.bdmfs_id,
        &g_iop_diag.bdmfs_result
    )) return -109;

    if (!TitanStage2_ExecEmbeddedModule(
        titan_stage2_usbmass_bd_irx,
        size_titan_stage2_usbmass_bd_irx,
        &g_iop_diag.usbmass_id,
        &g_iop_diag.usbmass_result
    )) return -110;

    SifLoadFileExit();

    if (!TitanStage2_ProbeTarget(target))
        return -111;

    TitanStage2_FlushIopDiag(target);
    return 0;
}

static void TitanStage2_WipeUserMemory(void)
{
    int address;

    for (
        address = 0x00100000;
        address < GetMemorySize();
        address += 64
    )
    {
        __asm__ __volatile__(
            "sq $0, 0(%0)\n"
            "sq $0, 16(%0)\n"
            "sq $0, 32(%0)\n"
            "sq $0, 48(%0)\n"
            :
            : "r"(address)
        );
    }
}

static void TitanStage2_Halt(
    const char *path,
    int load_result,
    unsigned int entry_point
)
{
    init_scr();

    scr_printf(
        "\nPROJECT TITAN BOOTLOADER %s\n"
        "----------------------------------------\n\n"
        "FALHA NO CARREGAMENTO DO EXECUTAVEL\n"
        "Caminho: %s\n"
        "SifLoadElf: %d\n"
        "Entry point: 0x%08x\n\n"
        "O sistema foi interrompido com seguranca.\n",
        TITAN_BOOTLOADER_VERSION_STRING,
        path != 0 ? path : "(null)",
        load_result,
        entry_point
    );

    for (;;)
    {
        SleepThread();
    }
}

int main(
    int argc,
    char *argv[]
)
{
    t_ExecData executable;
    int result;
    int video_result;

    if (
        argc < 1 ||
        argv == 0 ||
        argv[0] == 0 ||
        argv[0][0] == '\0'
    )
    {
        TitanStage2_Halt(
            "(missing executable path)",
            -EINVAL,
            0u
        );
    }

    memset(&executable, 0, sizeof(executable));

    /* Draw the handoff logo from stage2 itself before the long RAM wipe and
       ELF load. The bootloader framebuffer is not treated as persistent. */
    video_result = TitanStage2Video_ShowLogo();

    SifInitRpc(0);
    TitanStage2_WriteLog(
        1,
        "stage2-enter",
        argv[0],
        video_result,
        0u,
        0u
    );

    if (TitanStage2_IsMassTarget(argv[0]))
    {
        result = TitanStage2_RebuildMinimalIop(argv[0]);
        if (result < 0)
        {
            init_scr();
            scr_printf(
                "\nPROJECT TITAN HANDOFF\n"
                "----------------------------------------\n\n"
                "FATAL ERROR:\n"
                "Failed to rebuild clean IOP handoff\n\n"
                "Target: %s\n"
                "Stage code: %d\n\n"
                "The game engine was not started.\n",
                argv[0],
                result
            );
            for (;;) SleepThread();
        }
    }

    TitanStage2_WipeUserMemory();
    FlushCache(WRITEBACK_DCACHE);

    result = SifLoadFileInit();
    TitanStage2_WriteLog(
        0,
        "loadfile-init",
        argv[0],
        result,
        0u,
        0u
    );

    if (result < 0)
    {
        TitanStage2_Halt(
            argv[0],
            result,
            0u
        );
    }

    result = SifLoadElf(argv[0], &executable);
    TitanStage2_WriteLog(
        0,
        "load-elf",
        argv[0],
        result,
        executable.epc,
        executable.gp
    );
    SifLoadFileExit();

    if (result != 0 || executable.epc == 0u)
    {
        TitanStage2_Halt(
            argv[0],
            result,
            executable.epc
        );
    }

    SifExitRpc();
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);

    return ExecPS2(
        (void *)executable.epc,
        (void *)executable.gp,
        argc,
        argv
    );
}
