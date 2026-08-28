#include "titan_storage_pfs_provider_ps2.h"
#include "titan_storage_mount.h"

#if defined(__GNUC__)
#define TITAN_WEAK __attribute__((weak))
#else
#define TITAN_WEAK
#endif

/* Deliberately platform hooks, not public app APIs. A target integration that
 * owns/loads the real PFS driver implements these functions. Weak linkage
 * keeps builds without that backend fail-closed and linkable. */
extern int TitanPlatformPfs_Available(void) TITAN_WEAK;
extern int TitanPlatformPfs_Mount(const char *source, const char *root, uint32_t capabilities) TITAN_WEAK;
extern int TitanPlatformPfs_Unmount(const char *root) TITAN_WEAK;

static int TitanStoragePfsProviderPs2_Available(void *context)
{
    (void)context;
    return TitanPlatformPfs_Available != 0 &&
        TitanPlatformPfs_Mount != 0 && TitanPlatformPfs_Unmount != 0 &&
        TitanPlatformPfs_Available() != 0;
}

static int TitanStoragePfsProviderPs2_Mount(void *context, const char *source, const char *root, uint32_t capabilities)
{
    (void)context;
    if (!TitanStoragePfsProviderPs2_Available(0)) return 0;
    return TitanPlatformPfs_Mount(source, root, capabilities) != 0;
}

static int TitanStoragePfsProviderPs2_Unmount(void *context, const char *root)
{
    (void)context;
    if (TitanPlatformPfs_Unmount == 0) return 0;
    return TitanPlatformPfs_Unmount(root) != 0;
}

int TitanStoragePfsProviderPs2_RegisterIfAvailable(void)
{
    static const TitanStoragePfsProvider provider = {
        TitanStoragePfsProviderPs2_Available,
        TitanStoragePfsProviderPs2_Mount,
        TitanStoragePfsProviderPs2_Unmount
    };
    /* Register the kernel-owned provider when the platform hook surface is
     * linked. Availability itself is dynamic/lazy: the platform may bring
     * official HDD/PFS IOP drivers online on first capability query. */
    if (TitanPlatformPfs_Available == 0 || TitanPlatformPfs_Mount == 0 ||
        TitanPlatformPfs_Unmount == 0) return 0;
    return TitanStorageMount_RegisterPfsProvider(&provider, 0);
}
