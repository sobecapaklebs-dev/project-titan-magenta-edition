#include "titan_fmcb_core.h"

#include <errno.h>
#include <delaythread.h>
#include <fileXio_rpc.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <iopheap.h>
#include <kernel.h>
#include <libmc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "iop.h"
#include "libsecr.h"
#include "mctools_rpc.h"

/* system.h exposes maintenance helpers taking struct McData pointers; the
 * standalone main.h owns the full definition, which the integrated core does
 * not need.  Forward-declare it here to keep the frozen header ABI visible
 * without importing standalone UI/debug state. */
struct McData;
#include "system.h"
#include "UI.h"
#include "pad.h"

#define IMPORT_IRX(_IRX) extern unsigned char _IRX[]; extern unsigned int size_##_IRX
IMPORT_IRX(IOMANX_irx);
IMPORT_IRX(FILEXIO_irx);
IMPORT_IRX(SIO2MAN_irx);
IMPORT_IRX(MCMAN_irx);
IMPORT_IRX(MCSERV_irx);
IMPORT_IRX(USBD_irx);
IMPORT_IRX(USBHDFSD_irx);
IMPORT_IRX(SECRSIF_irx);
IMPORT_IRX(MCTOOLS_irx);
IMPORT_IRX(IOPRP_img);

/* Symbols normally owned by the standalone upstream main/UI/iop units.
 * The Titan integration owns them instead so the upstream install engine can
 * remain source-identical where MagicGate/KELF logic matters. */
int InstallLockSema = -1;
u8 dev9Loaded = 0;
unsigned short int SelectButton = 0;
unsigned short int CancelButton = 0;

static TitanFmcbProgressCallback g_progress;
static void *g_progress_opaque;

static void report(const char *stage, float progress, int detail)
{
    if (g_progress != NULL)
        g_progress(g_progress_opaque, stage, progress, detail);
}

static int exec_irx(void *data, unsigned int size)
{
    int result = -1;
    int id = SifExecModuleBuffer(data, size, 0, NULL, &result);
    return (id >= 0 && result >= 0) ? 0 : (id < 0 ? id : result);
}

static int load_mc_stack(void)
{
    if (exec_irx(SIO2MAN_irx, size_SIO2MAN_irx) < 0) return -EIO;
    if (exec_irx(MCMAN_irx, size_MCMAN_irx) < 0) return -EIO;
    if (exec_irx(MCSERV_irx, size_MCSERV_irx) < 0) return -EIO;
    return mcInit(MC_TYPE_XMC) >= 0 ? 0 : -EIO;
}

static int wait_for_root(const char *root)
{
    unsigned int i;
    char probe[320];
    FILE *fp;
    int written;
    if (root == NULL || root[0] == '\0') return -EINVAL;
    written = snprintf(probe, sizeof(probe), "%s/INSTALL/SYSTEM/FMCB.XLF", root);
    if (written < 0 || (unsigned int)written >= sizeof(probe)) return -ENAMETOOLONG;
    for (i = 0; i < 100u; ++i) {
        fp = fopen(probe, "rb");
        if (fp != NULL) {
            fclose(fp);
            return 0;
        }
        DelayThread(100000);
    }
    return -ENOENT;
}

#define TITAN_FMCB_IOP_RESET_RETRIES 2000u
#define TITAN_FMCB_IOP_SYNC_RETRIES  4000u
#define TITAN_FMCB_IOP_POLL_US       5000u

static int reset_and_sync_iop(unsigned int flags)
{
    unsigned int i;
    int accepted = 0;
    for (i = 0u; i < TITAN_FMCB_IOP_RESET_RETRIES; ++i) {
        int result = (flags & IOP_LIBSECR_IMG)
            ? SifIopRebootBuffer(IOPRP_img, size_IOPRP_img)
            : SifIopReset("", 0);
        if (result > 0) {
            accepted = 1;
            break;
        }
        DelayThread(TITAN_FMCB_IOP_POLL_US);
    }
    if (!accepted) return -ETIMEDOUT;
    for (i = 0u; i < TITAN_FMCB_IOP_SYNC_RETRIES; ++i) {
        if (SifIopSync()) return 0;
        DelayThread(TITAN_FMCB_IOP_POLL_US);
    }
    return -ETIMEDOUT;
}

/* Compatibility implementation of the upstream IOP bootstrap.  It keeps the
 * exact custom IOPRP/secrsif path required for MagicGate/KELF signing, but it
 * deliberately does not initialize FMCB's GS/UI/menu.  Titan remains the only
 * visible installer environment on EE. */
int IopInitStart(unsigned int flags)
{
    ee_sema_t sema;
    int sema_id;

    report("fmcb_iop", 0.02f, (int)flags);

    /* Discard EE-side clients before replacing the IOP image. */
    fileXioExit();
    SecrDeinit();
    DeinitMCTOOLS();

    if (reset_and_sync_iop(flags) < 0) {
        report("fmcb_iop_reset_timeout", 0.03f, -ETIMEDOUT);
        return -ETIMEDOUT;
    }

    SifInitRpc(0);
    SifInitIopHeap();
    SifLoadFileInit();
    sbv_patch_enable_lmb();

    if (exec_irx(IOMANX_irx, size_IOMANX_irx) < 0 ||
        exec_irx(FILEXIO_irx, size_FILEXIO_irx) < 0) {
        report("fmcb_fileio_stack_error", 0.04f, -1);
        return -EIO;
    }
    if (fileXioInit() < 0) {
        report("fmcb_filexio_init_error", 0.04f, -1);
        return -EIO;
    }

    if (load_mc_stack() < 0) {
        report("fmcb_mc_stack_error", 0.04f, -1);
        return -EIO;
    }

    if (exec_irx(USBD_irx, size_USBD_irx) < 0 ||
        exec_irx(USBHDFSD_irx, size_USBHDFSD_irx) < 0) {
        report("fmcb_usb_stack_error", 0.04f, -1);
        return -EIO;
    }

    if (flags & IOP_MOD_SECRSIF) {
        if (exec_irx(SECRSIF_irx, size_SECRSIF_irx) < 0 || !SecrInit()) {
            report("fmcb_secr_error", 0.04f, -1);
            return -EIO;
        }
    }
    if (flags & IOP_MOD_MCTOOLS) {
        if (exec_irx(MCTOOLS_irx, size_MCTOOLS_irx) < 0) {
            report("fmcb_mctools_error", 0.04f, -1);
            return -EIO;
        }
        InitMCTOOLS();
    }

    /* Keep the asynchronous contract expected by upstream call sites while
     * completing initialization synchronously inside the Titan process. */
    sema.init_count = 1;
    sema.max_count = 1;
    sema.attr = sema.option = 0;
    sema_id = CreateSema(&sema);
    report("fmcb_iop_ready", 0.06f, sema_id);
    return sema_id;
}

void IopDeinit(void)
{
    DeinitMCTOOLS();
    SecrDeinit();
    fileXioExit();
    SifLoadFileExit();
}

/* Standalone-FMCB UI entry points referenced by upstream system.c. They are
 * intentionally mapped to Titan progress callbacks instead of drawing FMCB. */
void InitProgressScreen(int label) { report("fmcb_copy", 0.08f, label); }
void DrawFileCopyProgressScreen(float p) { report("fmcb_copy", p, 0); }
void DrawMemoryCardDumpingProgressScreen(float p, unsigned int rate, unsigned int remain) { (void)rate; (void)remain; report("fmcb_mc", p, 0); }
void DrawMemoryCardRestoreProgressScreen(float p, unsigned int rate, unsigned int remain) { (void)rate; (void)remain; report("fmcb_mc", p, 0); }
void DisplayOutOfSpaceMessage(unsigned int available, unsigned int required) { report("fmcb_no_space", 0.0f, (int)(required > available ? required - available : 0)); }
void DisplayOutOfSpaceMessageHDD_APPS(unsigned int available, unsigned int required) { DisplayOutOfSpaceMessage(available, required); }
void DisplayOutOfSpaceMessageHDD(unsigned int available, unsigned int required) { DisplayOutOfSpaceMessage(available, required); }
void DisplayErrorMessage(unsigned int message) { report("fmcb_error", 0.0f, (int)message); }
void DisplayWarningMessage(unsigned int message) { report("fmcb_warning", 0.0f, (int)message); }
void DisplayInfoMessage(unsigned int message) { report("fmcb_info", 0.0f, (int)message); }
int DisplayPromptMessage(unsigned int message, unsigned char a, unsigned char b) { (void)a; (void)b; report("fmcb_prompt_blocked", 0.0f, (int)message); return 0; }
void DisplayFlashStatusUpdate(unsigned int message) { report("fmcb_status", 0.0f, (int)message); }
int ReinitializeUI(void) { return 0; }
int InitializeUI(int buffer_font) { (void)buffer_font; return 0; }
void DeinitializeUI(void) { }
void RedrawLoadingScreen(unsigned int frame) { (void)frame; report("fmcb_loading", 0.03f, 0); }
int ShowMessageBox(int a,int b,int c,int d,const char *m,int t) { (void)a;(void)b;(void)c;(void)d;(void)m;(void)t; return 0; }

/* Pad input is never used by the Titan autonomous path; keep upstream helper
 * references non-interactive if the linker retains maintenance code. */
void PadInitPads(void) { }
void PadDeinitPads(void) { }
int ReadPadStatus_raw(int port, int slot) { (void)port; (void)slot; return 0; }
int ReadCombinedPadStatus_raw(void) { return 0; }
int ReadPadStatus(int port, int slot) { (void)port; (void)slot; return 0; }
int ReadCombinedPadStatus(void) { return 0; }

static int core_prepare(const char *root)
{
    int init_sema;
    int result;

    init_sema = IopInitStart(IOP_MOD_SET_MAIN);
    if (init_sema < 0) return init_sema;
    WaitSema(init_sema);
    DeleteSema(init_sema);

    result = wait_for_root(root);
    if (result < 0) return result;
    if (chdir(root) != 0) return -errno;

    /* UpdateRegionalPaths() is upstream code and assumes ROMVER exists.  The
     * Titan front-end already fail-closes unknown ROMs; repeat the physical
     * source check here so a missing/truncated ROMVER can never become a NULL
     * FILE dereference inside the integrated backend. */
    {
        char romver_probe[8];
        FILE *rom = fopen("rom0:ROMVER", "rb");
        size_t got = 0u;
        if (rom != NULL) {
            got = fread(romver_probe, 1u, 6u, rom);
            fclose(rom);
        }
        if (got < 6u) return -EIO;
    }
    UpdateRegionalPaths();
    return 0;
}

int TitanFmcbCore_Install(const char *fmcb_root, int mc_port,
                         TitanFmcbProgressCallback progress, void *opaque)
{
    ee_sema_t sema;
    char previous[256];
    int result;

    if (mc_port < 0 || mc_port > 1 || fmcb_root == NULL) return -EINVAL;
    g_progress = progress;
    g_progress_opaque = opaque;
    if (getcwd(previous, sizeof(previous)) == NULL) previous[0] = '\0';

    sema.init_count = 1;
    sema.max_count = 1;
    sema.attr = sema.option = 0;
    InstallLockSema = CreateSema(&sema);
    if (InstallLockSema < 0) {
        result = InstallLockSema;
        g_progress = NULL;
        g_progress_opaque = NULL;
        return result;
    }

    report("fmcb_prepare", 0.01f, mc_port);
    result = core_prepare(fmcb_root);
    if (result >= 0) {
        /* A fresh autonomous install may never adopt/overwrite an unknown FMCB.
         * The Titan front-end performs the same check before staging, and this
         * second check closes the race immediately before MagicGate writes. */
        if (HasOldFMCBConfigFile(mc_port, 0) || HasOldMultiInstall(mc_port, 0)) {
            result = -EEXIST;
        } else {
            report("fmcb_install", 0.10f, mc_port);
            result = PerformInstallation((unsigned char)mc_port, 0,
                                         INSTALL_MODE_FLAG_TITAN_MINIMAL);
            if (result < 0)
                (void)CleanupTarget(mc_port, 0);
        }
    }

    if (previous[0] != '\0') (void)chdir(previous);
    if (InstallLockSema >= 0) {
        DeleteSema(InstallLockSema);
        InstallLockSema = -1;
    }
    report(result >= 0 ? "fmcb_done" : "fmcb_failed", result >= 0 ? 1.0f : 0.0f, result);
    g_progress = NULL;
    g_progress_opaque = NULL;
    return result;
}

int TitanFmcbCore_Cleanup(const char *fmcb_root, int mc_port,
                         TitanFmcbProgressCallback progress, void *opaque)
{
    ee_sema_t sema;
    char previous[256];
    int result;
    if (mc_port < 0 || mc_port > 1 || fmcb_root == NULL) return -EINVAL;
    g_progress = progress; g_progress_opaque = opaque;
    if (getcwd(previous, sizeof(previous)) == NULL) previous[0] = '\0';
    sema.init_count=1; sema.max_count=1; sema.attr=sema.option=0;
    InstallLockSema=CreateSema(&sema);
    if (InstallLockSema < 0) {
        result=InstallLockSema;
        g_progress=NULL; g_progress_opaque=NULL;
        return result;
    }
    result=core_prepare(fmcb_root);
    if (result >= 0) result=CleanupTarget(mc_port,0);
    if (previous[0] != '\0') (void)chdir(previous);
    DeleteSema(InstallLockSema); InstallLockSema=-1;
    g_progress=NULL; g_progress_opaque=NULL;
    return result;
}
