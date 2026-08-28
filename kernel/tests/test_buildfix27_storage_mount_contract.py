#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
h=(r/'kernel/src/services/titan_storage_mount.h').read_text(errors='replace')
c=(r/'kernel/src/services/titan_storage_mount.c').read_text(errors='replace')
runtime=(r/'kernel/src/services/titan_app_runtime.c').read_text(errors='replace')
mk=(r/'kernel/Makefile').read_text(errors='replace')
def req(v,m):
 if not v: raise SystemExit('buildfix27-storage-mount-contract: FAIL: '+m)
for t in ('TitanStorageMount_RegisterPfsProvider','TitanStorageMount_PfsAvailable','TitanStorageMount_MountPfs','TitanStorageMount_Unmount','generation','TITAN_STORAGE_MOUNT_CAP_WRITE'):
 req(t in h,t+' missing')
req('strncmp(source, "hdd0:", 5u)' in c,'PFS source not constrained to hdd0')
req('pfs%u:/' in c,'bounded pfs slot namespace missing')
req('TITAN_STORAGE_MOUNT_ERROR' in c and 'TITAN_STORAGE_MOUNT_UNMOUNTING' in c,'lifecycle/error states missing')
req('src/services/titan_storage_mount.o' in mk,'mount service absent from kernel build')
req('strncmp(path, "hdd0:", 5u) == 0' in runtime,'hdd0 read policy regressed')
req('TitanStorageMount_Get(slot, &mount)' in runtime and 'mount.state == TITAN_STORAGE_MOUNT_ONLINE' in runtime, 'PFS app path is not dynamically provider/capability gated')
req('fileXioMount' not in runtime, 'runtime must not expose direct FileXio mount ownership')
req('fileXioMount' not in c and 'fileXioUmount' not in c,'provider layer must not hardcode unavailable target mount backend')
print('buildfix27-storage-mount-contract: OK provider-gated kernel-owned PFS lifecycle with dynamic app capability gate')
