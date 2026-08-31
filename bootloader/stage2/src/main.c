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
#include <syscallnr.h>
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
#include <stdint.h>
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


/* Internal handoff sideband.  This marker exists only between Titan's kernel
 * and its embedded stage2.  It is stripped before the Golden engine receives
 * argv, so the validated Neutrino/UL contracts remain byte-for-byte semantic
 * equivalents of the pre-feature launch path. */
#define TITAN_STAGE2_PATCH_PREFIX "--titan-internal-patch="
#define TITAN_STAGE2_PATCH_MAGIC 0x48435054u /* TPCH */
#define TITAN_STAGE2_PATCH_VERSION 1u
#define TITAN_STAGE2_PATCH_MAX_PAIRS 254u
#define TITAN_STAGE2_PS2RD_MAX_HOOKS 5u
#define TITAN_STAGE2_PS2RD_MAX_CODES 250u
#define TITAN_STAGE2_PS2RD_LOW_START 0x00080000u
#define TITAN_STAGE2_PS2RD_LOW_END 0x00084000u

typedef struct TitanStage2PatchHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t pair_count;
    uint32_t checksum;
} __attribute__((packed)) TitanStage2PatchHeader;

typedef struct TitanStage2PatchPair
{
    uint32_t address;
    uint32_t value;
} TitanStage2PatchPair;

extern unsigned char __titan_ps2rd_start[];
extern unsigned char __titan_ps2rd_end[];
extern u32 Old_SetupThread;
extern void *HookSetupThread(
    void *gp,
    void *stack,
    s32 stack_size,
    void *args,
    void *root_func
);
extern u32 numhooks;
extern u32 hooklist[];
extern u32 numcodes;
extern u32 codelist[];

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


static unsigned int TitanStage2_PatchFnvStep(
    unsigned int hash,
    unsigned char value
)
{
    hash ^= (unsigned int)value;
    hash *= 16777619u;
    return hash;
}

static unsigned int TitanStage2_PatchChecksum(
    const TitanStage2PatchPair *pairs,
    unsigned int count
)
{
    unsigned int hash = 2166136261u;
    unsigned int index;
    int shift;

    for (index = 0u; index < count; ++index)
    {
        for (shift = 0; shift < 32; shift += 8)
            hash = TitanStage2_PatchFnvStep(
                hash,
                (unsigned char)((pairs[index].address >> shift) & 0xffu)
            );
        for (shift = 0; shift < 32; shift += 8)
            hash = TitanStage2_PatchFnvStep(
                hash,
                (unsigned char)((pairs[index].value >> shift) & 0xffu)
            );
    }
    return hash;
}

static int TitanStage2_ReadExact(
    int descriptor,
    void *buffer,
    size_t size
)
{
    unsigned char *cursor = (unsigned char *)buffer;
    size_t used = 0u;

    while (used < size)
    {
        ssize_t amount = read(descriptor, cursor + used, size - used);
        if (amount <= 0) return 0;
        used += (size_t)amount;
    }
    return 1;
}

static int TitanStage2_LoadPatchPayload(
    const char *path,
    TitanStage2PatchPair pairs[TITAN_STAGE2_PATCH_MAX_PAIRS],
    unsigned int *pair_count
)
{
    TitanStage2PatchHeader header;
    int descriptor;

    if (path == 0 || path[0] == '\0' || pairs == 0 || pair_count == 0)
        return 0;
    *pair_count = 0u;

    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return 0;
    if (!TitanStage2_ReadExact(descriptor, &header, sizeof(header)) ||
        header.magic != TITAN_STAGE2_PATCH_MAGIC ||
        header.version != TITAN_STAGE2_PATCH_VERSION ||
        header.pair_count == 0u ||
        header.pair_count > TITAN_STAGE2_PATCH_MAX_PAIRS ||
        !TitanStage2_ReadExact(
            descriptor,
            pairs,
            (size_t)header.pair_count * sizeof(pairs[0])
        ))
    {
        close(descriptor);
        return 0;
    }
    close(descriptor);

    if (TitanStage2_PatchChecksum(pairs, header.pair_count) != header.checksum)
        return 0;
    *pair_count = header.pair_count;
    return 1;
}

static int TitanStage2_InstallPatchEngine(
    const TitanStage2PatchPair *pairs,
    unsigned int pair_count
)
{
    unsigned int index;
    unsigned int hooks = 0u;
    unsigned int codes = 0u;
    int next_code_can_be_hook = 1;
    void *old_setup_thread;

    if (pairs == 0 || pair_count == 0u ||
        (uintptr_t)__titan_ps2rd_start < TITAN_STAGE2_PS2RD_LOW_START ||
        (uintptr_t)__titan_ps2rd_end > TITAN_STAGE2_PS2RD_LOW_END)
        return 0;

    numhooks = 0u;
    numcodes = 0u;
    memset(
        hooklist,
        0,
        TITAN_STAGE2_PS2RD_MAX_HOOKS * 2u * sizeof(u32)
    );
    memset(
        codelist,
        0,
        TITAN_STAGE2_PS2RD_MAX_CODES * 2u * sizeof(u32)
    );

    for (index = 0u; index < pair_count; ++index)
    {
        const u32 address = pairs[index].address;
        const u32 value = pairs[index].value;

        if ((address & 0xfe000000u) == 0x90000000u && next_code_can_be_hook)
        {
            if (hooks >= TITAN_STAGE2_PS2RD_MAX_HOOKS) return 0;
            hooklist[hooks * 2u] = address & 0x01fffffcu;
            hooklist[hooks * 2u + 1u] = value;
            ++hooks;
        }
        else
        {
            if (codes >= TITAN_STAGE2_PS2RD_MAX_CODES) return 0;
            codelist[codes * 2u] = address;
            codelist[codes * 2u + 1u] = value;
            ++codes;
        }

        /* The second line of a multi-line type-3/type-4 code may start with 9
         * and must never be misclassified as a hook/master code. */
        next_code_can_be_hook =
            ((address & 0xf0000000u) == 0x40000000u ||
             (address & 0xf0000000u) == 0x30000000u) ? 0 : 1;
    }

    numhooks = hooks;
    numcodes = codes;
    old_setup_thread = GetSyscallHandler(__NR_SetupThread);
    if (old_setup_thread == 0) return 0;
    Old_SetupThread = (u32)(uintptr_t)old_setup_thread;
    SetSyscall(__NR_SetupThread, (void *)HookSetupThread);
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);
    return 1;
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
    TitanStage2PatchPair patch_pairs[TITAN_STAGE2_PATCH_MAX_PAIRS];
    unsigned int patch_pair_count = 0u;
    const char *patch_path = 0;
    const char *target;
    char **forward_argv;
    int forward_argc;
    int target_index = 0;
    int result;
    int video_result;
    const size_t patch_prefix_length = strlen(TITAN_STAGE2_PATCH_PREFIX);

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

    /* Patched launches use one private argv element for stage2 only.  The
     * target executable and every original argument remain contiguous after
     * it and are forwarded unchanged to the Golden engine. */
    if (
        argc >= 2 &&
        strncmp(argv[0], TITAN_STAGE2_PATCH_PREFIX, patch_prefix_length) == 0
    )
    {
        patch_path = argv[0] + patch_prefix_length;
        if (patch_path[0] == '\0')
            TitanStage2_Halt("(empty Titan patch payload)", -200, 0u);
        target_index = 1;
    }

    if (
        target_index >= argc ||
        argv[target_index] == 0 ||
        argv[target_index][0] == '\0'
    )
    {
        TitanStage2_Halt("(missing target after Titan sideband)", -201, 0u);
    }

    target = argv[target_index];
    forward_argc = argc - target_index;
    forward_argv = &argv[target_index];
    memset(&executable, 0, sizeof(executable));

    /* Draw the handoff logo from stage2 itself before the long RAM wipe and
       ELF load. The bootloader framebuffer is not treated as persistent. */
    video_result = TitanStage2Video_ShowLogo();

    SifInitRpc(0);
    TitanStage2_WriteLog(
        1,
        "stage2-enter",
        target,
        video_result,
        0u,
        0u
    );

    if (TitanStage2_IsMassTarget(target))
    {
        result = TitanStage2_RebuildMinimalIop(target);
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
                target,
                result
            );
            for (;;) SleepThread();
        }
    }

    if (patch_path != 0)
    {
        if (!TitanStage2_LoadPatchPayload(
                patch_path,
                patch_pairs,
                &patch_pair_count))
        {
            TitanStage2_WriteLog(0, "patch-load", target, -202, 0u, 0u);
            TitanStage2_Halt(target, -202, 0u);
        }
        if (!TitanStage2_InstallPatchEngine(patch_pairs, patch_pair_count))
        {
            TitanStage2_WriteLog(
                0,
                "patch-install",
                target,
                -203,
                patch_pair_count,
                0u
            );
            TitanStage2_Halt(target, -203, 0u);
        }
        TitanStage2_WriteLog(
            0,
            "patch-ready",
            target,
            0,
            patch_pair_count,
            0u
        );
    }

    /* The resident PS2RD block is below 0x00084000 and therefore survives the
     * normal user-RAM wipe. Neutrino's ee_core begins exactly at 0x00084000. */
    TitanStage2_WipeUserMemory();
    FlushCache(WRITEBACK_DCACHE);

    result = SifLoadFileInit();
    TitanStage2_WriteLog(
        0,
        "loadfile-init",
        target,
        result,
        0u,
        0u
    );

    if (result < 0)
    {
        TitanStage2_Halt(target, result, 0u);
    }

    result = SifLoadElf(target, &executable);
    TitanStage2_WriteLog(
        0,
        "load-elf",
        target,
        result,
        executable.epc,
        executable.gp
    );
    SifLoadFileExit();

    if (result != 0 || executable.epc == 0u)
    {
        TitanStage2_Halt(target, result, executable.epc);
    }

    SifExitRpc();
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);

    return ExecPS2(
        (void *)executable.epc,
        (void *)executable.gp,
        forward_argc,
        forward_argv
    );
}
