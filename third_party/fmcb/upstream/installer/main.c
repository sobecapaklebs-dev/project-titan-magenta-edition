#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libmc.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <loadfile.h>
#include <malloc.h>
#include <sbv_patches.h>
#include <sifcmd.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <wchar.h>

#include <libgs.h>

#include "main.h"
#include "iop.h"
#include "pad.h"
#include "graphics.h"
#include "font.h"

#include "libsecr.h"
#include "mctools_rpc.h"
#include "system.h"
#include "ReqSpaceCalc.h"
#include "UI.h"
#include "menu.h"

#define GS_BGCOLOUR(x) *((volatile unsigned long int *)0x120000E0) = x

int IsHDDUnitConnected = 0;

int VBlankStartSema;
int InstallLockSema;

static int VBlankStartHandler(int cause)
{
    ee_sema_t sema;
    iReferSemaStatus(VBlankStartSema, &sema);
    if (sema.count < sema.max_count)
        iSignalSema(VBlankStartSema);
    ExitHandler();
    return 0;
}

static void DeinitServices(void)
{
    DisableIntc(kINTC_VBLANK_START);
    RemoveIntcHandler(kINTC_VBLANK_START, 0);
    DeleteSema(VBlankStartSema);
    DeleteSema(InstallLockSema);

    IopDeinit();
}

static int WriteTitanResult(const char *root, const char *status, int result)
{
    char path[256];
    FILE *fp;
    if (root == NULL || status == NULL) return -1;
    snprintf(path, sizeof(path), "%s/TITAN_FMCB.RESULT", root);
    fp = fopen(path, "wb");
    if (!fp) return -1;
    fprintf(fp, "status=%s\nresult=%d\n", status, result);
    fclose(fp);
    return 0;
}

static void ReturnToTitan(const char *elf, int mc, const char *status)
{
    char mcarg[16], resultarg[48];
    char *args[4];
    snprintf(mcarg, sizeof(mcarg), "--mc=%d", mc);
    snprintf(resultarg, sizeof(resultarg), "--fmcb-resume=%s", status);
    args[0] = (char *)elf; args[1] = resultarg; args[2] = mcarg; args[3] = NULL;
    LoadExecPS2(elf, 3, args);
}

int main(int argc, char *argv[])
{
    int SystemType, InitSemaID, BootDevice, result;
    int TitanAuto = 0, TitanMc = 0;
    const char *TitanRoot = NULL;
    const char *TitanReturn = NULL;
    unsigned int FrameNum;
    ee_sema_t sema;
    sio_init(38400, 0, 0, 0, 0);

    for (result = 0; result < argc; result++) {
        sio_printf("\targv[%d] = %s\n", result, argv[result]);
        if (!strcmp(argv[result], "--titan-auto")) TitanAuto = 1;
        else if (!strncmp(argv[result], "--mc=", 5)) TitanMc = atoi(argv[result] + 5);
        else if (!strncmp(argv[result], "--root=", 7)) TitanRoot = argv[result] + 7;
        else if (!strncmp(argv[result], "--return=", 9)) TitanReturn = argv[result] + 9;
    }
    if (TitanMc < 0 || TitanMc > 1) return -1;
    if (TitanAuto && (TitanRoot == NULL || TitanReturn == NULL)) return -1;
    if (TitanAuto && chdir(TitanRoot) != 0) return -1;

    // chdir("mass:/FMCBInstaller/");
    if ((BootDevice = GetBootDeviceID()) == BOOT_DEVICE_UNKNOWN)
    {
        GS_BGCOLOUR(0x0000FF);
        sio_printf("BOOT DEVICE IS NOT USB. EXITING INSTALLER\n");
        Exit(-1);
    }

    InitSemaID = IopInitStart(IOP_MOD_SET_MAIN);

    sema.init_count = 0;
    sema.max_count = 1;
    sema.attr = sema.option = 0;
    VBlankStartSema = CreateSema(&sema);

    sema.init_count = 1;
    sema.max_count = 1;
    sema.attr = sema.option = 0;
    InstallLockSema = CreateSema(&sema);

    AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
    EnableIntc(kINTC_VBLANK_START);

    if (InitializeUI(0) != 0) {
        GS_BGCOLOUR(0xFF00FF);
        sio_printf("InitializeUI(0) failed \n");
        SifExitRpc();
        Exit(-1);
    }

    FrameNum = 0;
    /* Draw something nice here while waiting... */
    do {
        RedrawLoadingScreen(FrameNum);
        FrameNum++;
    } while (PollSema(InitSemaID) != InitSemaID);
    DeleteSema(InitSemaID);

    DEBUG_PRINTF("Modules loaded.\n");

    StartWorkerThread();

    UpdateRegionalPaths();

    if (TitanAuto) {
        /* Titan only invokes fresh-install mode after proving NO_FMCB. Refuse
         * a race or unexpected existing installation rather than overwriting it. */
        if (HasOldFMCBConfigFile(TitanMc, 0) || HasOldMultiInstall(TitanMc, 0)) {
            result = -EEXIST;
        } else {
            result = PerformInstallation((unsigned char)TitanMc, 0, INSTALL_MODE_FLAG_TITAN_MINIMAL);
            if (result < 0)
                (void)CleanupTarget(TitanMc, 0);
        }
        (void)WriteTitanResult(TitanRoot, result == 0 ? "success" : "failure", result);
        StopWorkerThread();
        DeinitializeUI();
        DeinitServices();
        ReturnToTitan(TitanReturn, TitanMc, result == 0 ? "success" : "failure");
        return result;
    }

    SystemType = GetPs2Type();
    if (SystemType == PS2_SYSTEM_TYPE_PS2 || SystemType == PS2_SYSTEM_TYPE_DEX) {
        result = HDDCheckStatus();
        if (result >= 0 && result <= 1) { // Accept only if a usable disk is connected (formatted or not).
            IsHDDUnitConnected = 1;
        }
    }

    MainMenu();

ShutdownRPCsAndExit:
    StopWorkerThread();

    if (!IsHDDBootingEnabled())
        fileXioDevctl("hdd0:", HDDCTL_DEV9_SHUTDOWN, NULL, 0, NULL, 0);

    DeinitializeUI();
    DeinitServices();

    return 0;
}
