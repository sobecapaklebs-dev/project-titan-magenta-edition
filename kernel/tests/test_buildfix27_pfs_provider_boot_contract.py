#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve(); k=(r/'kernel/src/core/titan_kernel.c').read_text(errors='replace'); c=(r/'kernel/src/services/titan_storage_pfs_provider_ps2.c').read_text(errors='replace'); mk=(r/'kernel/Makefile').read_text(errors='replace')
def req(v,m):
    if not v: raise SystemExit('buildfix27-pfs-provider-boot-contract: FAIL: '+m)
req('TitanStorageMount_Initialize();' in k,'mount lifecycle not initialized by kernel boot')
req('TitanStoragePfsProviderPs2_RegisterIfAvailable();' in k,'provider adapter not registered by kernel boot')
req(k.index('TitanStorageMount_Initialize();') < k.index('TitanStoragePfsProviderPs2_RegisterIfAvailable();'),'provider registration before mount initialization')
for x in ('TitanPlatformPfs_Available','TitanPlatformPfs_Mount','TitanPlatformPfs_Unmount','TITAN_WEAK'):
    req(x in c,'missing fail-closed platform hook '+x)
req('fileXioMount' not in c and 'fileXioUmount' not in c,'adapter guesses unavailable FileXio/PFS ABI')
req('src/services/titan_storage_pfs_provider_ps2.o' in mk,'adapter not in kernel build')
print('buildfix27-pfs-provider-boot-contract: OK boot initialization + weak-hook fail-closed adapter')
