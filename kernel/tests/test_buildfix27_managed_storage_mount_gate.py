#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
runtime=(root/'kernel/src/services/titan_app_runtime.c').read_text(errors='replace')
vmc=(root/'kernel/src/services/titan_vmc.c').read_text(errors='replace')
mount_h=(root/'kernel/src/services/titan_storage_mount.h').read_text(errors='replace')
mount_c=(root/'kernel/src/services/titan_storage_mount.c').read_text(errors='replace')
all_kernel='\n'.join(p.read_text(errors='replace') for p in (root/'kernel/src').rglob('*.c'))
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-managed-storage-mount-gate: FAIL: '+msg)
req('strncmp(path, "hdd0:", 5u) == 0' in runtime, 'hdd0 read policy missing')
req('Keep ROM/host/pfs/vmc namespaces under the kernel' in runtime, 'pfs/vmc ownership denial missing')
req('TitanStorageMount_RegisterPfsProvider' in mount_h and 'TitanStorageMount_MountPfs' in mount_h, 'kernel-owned PFS lifecycle layer missing')
req('fileXioMount' not in mount_c and 'fileXioUmount' not in mount_c, 'unavailable concrete target provider was hardcoded')
req('TitanVmc_VerifyPathRaw' in vmc and 'TITAN_VMC_SIZE_BYTES' in vmc, 'Titan managed VMC image backend missing')
req('TitanVmc_BuildNeutrinoArgument' in vmc, 'VMC lifecycle is not tied to Titan game backend')
print('buildfix27-managed-storage-mount-gate: OK hdd0 read-only, PFS lifecycle provider-gated, VMC kernel-managed')
