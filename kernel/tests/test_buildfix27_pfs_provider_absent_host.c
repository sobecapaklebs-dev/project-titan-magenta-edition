#include <assert.h>
#include <stdio.h>
#include "titan_storage_mount.h"
#include "titan_storage_pfs_provider_ps2.h"
int main(void){ TitanStorageMount_Initialize(); assert(!TitanStoragePfsProviderPs2_RegisterIfAvailable()); assert(!TitanStorageMount_PfsAvailable()); puts("buildfix27-pfs-provider-absent-host: OK fail-closed without platform hooks"); return 0; }
