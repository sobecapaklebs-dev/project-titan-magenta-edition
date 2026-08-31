#include "titan_storage_mount.h"
#include "titan_storage_pfs_iop.h"

#if defined(__EE__) || defined(_EE)
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <loadfile.h>
#include <string.h>
#include "titan_iop_modules.h"

static int TitanPlatformPfs_HasDevice(const char *wanted)
{
    struct fileXioDevice devices[FILEXIO_MAX_DEVICES];
    int count;
    int i;
    memset(devices, 0, sizeof(devices));
    count = fileXioGetDeviceList(devices, FILEXIO_MAX_DEVICES);
    if (count <= 0) return 0;
    for (i = 0; i < count; ++i)
        if (strcmp(devices[i].name, wanted) == 0) return 1;
    return 0;
}

static int TitanPlatformPfs_DevicesReady(void *context)
{
    (void)context;
    return TitanPlatformPfs_HasDevice("hdd") && TitanPlatformPfs_HasDevice("pfs");
}

#if defined(TITAN_HAVE_PFS_EMBEDDED)
static int TitanPlatformPfs_ModuleResident(void *context, const char *name)
{
    (void)context;
    return SifSearchModuleByName(name) >= 0;
}

/* TITAN_RC4_IOP_START_RESULT_FIX_R1:
 * result==1 (MODULE_NO_RESIDENT_END) means the module did not remain loaded.
 * result==2 is a valid resident/removable module status. */
static int TitanPlatformPfs_ExecModule(void *context, const unsigned char *data, uint32_t size)
{
    int result = -1;
    int id;
    (void)context;
    if (data == 0 || size == 0u) return 0;
    id = SifExecModuleBuffer((void *)data, (int)size, 0, 0, &result);
    return id >= 0 && (result == 0 || result == 2);
}

static int TitanPlatformPfs_EnsureDrivers(void)
{
    static const TitanStoragePfsIopBackend backend = {
        TitanPlatformPfs_ModuleResident,
        TitanPlatformPfs_ExecModule,
        TitanPlatformPfs_DevicesReady
    };
    const TitanStoragePfsIopImage images[4] = {
        { "dev9", titan_ps2dev9_irx, size_titan_ps2dev9_irx },
        { "atad", titan_ps2atad_irx, size_titan_ps2atad_irx },
        { "hdd_driver", titan_ps2hdd_irx, size_titan_ps2hdd_irx },
        { "pfs_driver", titan_ps2fs_irx, size_titan_ps2fs_irx }
    };
    return TitanStoragePfsIop_Ensure(&backend, 0, images);
}
#else
static int TitanPlatformPfs_EnsureDrivers(void)
{
    return TitanPlatformPfs_DevicesReady(0);
}
#endif

int TitanPlatformPfs_Available(void)
{
    return TitanPlatformPfs_EnsureDrivers();
}

int TitanPlatformPfs_Mount(const char *source, const char *root, uint32_t capabilities)
{
    char mountpoint[TITAN_STORAGE_MOUNT_ROOT_CAPACITY];
    size_t length;
    int flag;
    if (!source || !root || !TitanPlatformPfs_Available()) return 0;
    length = strlen(root);
    if (length < 2u || length >= sizeof(mountpoint)) return 0;
    memcpy(mountpoint, root, length + 1u);
    if (mountpoint[length - 1u] == '/') mountpoint[length - 1u] = '\0';
    flag = (capabilities & TITAN_STORAGE_MOUNT_CAP_WRITE) != 0u ? FILEXIO_MOUNTFLAG_NORMAL : FILEXIO_MOUNTFLAG_READONLY;
    return fileXioMount(mountpoint, source, flag) == 0;
}

int TitanPlatformPfs_Unmount(const char *root)
{
    char mountpoint[TITAN_STORAGE_MOUNT_ROOT_CAPACITY];
    size_t length;
    if (!root) return 0;
    length = strlen(root);
    if (length < 2u || length >= sizeof(mountpoint)) return 0;
    memcpy(mountpoint, root, length + 1u);
    if (mountpoint[length - 1u] == '/') mountpoint[length - 1u] = '\0';
    return fileXioUmount(mountpoint) == 0;
}
#endif
