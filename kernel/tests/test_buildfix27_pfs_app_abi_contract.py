#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def text(p): return (root/p).read_text(errors='replace')
def req(v,m):
    if not v: raise SystemExit('buildfix27-pfs-app-abi-contract: FAIL: '+m)
sdk=text('kernel/include/titan_app_sdk.h')
rt=text('kernel/src/services/titan_app_runtime.c')
mount=text('kernel/src/services/titan_storage_mount.c')
for token in ('pfs_available','pfs_mount','pfs_unmount','pfs_status','TitanAppPfsMountStatus'):
    req(token in sdk, 'missing append-only PFS ABI token '+token)
req(sdk.index('vmc_read_active_file') < sdk.index('pfs_available'), 'PFS ABI not append-only after CP33 tail')
for token in ('pfs_owned_slots','TitanAppRuntime_ReleasePfsMounts','TitanStorageMount_MountPfs','TitanStorageMount_Unmount'):
    req(token in rt, 'runtime session ownership/reclaim missing '+token)
req('TitanAppRuntime_ReleasePfsMounts();' in rt[rt.index('static void TitanAppRuntime_ResetSessionMemory'):], 'PFS reclaim not wired into reset')
req('mount.state == TITAN_STORAGE_MOUNT_ONLINE' in rt and 'TITAN_STORAGE_MOUNT_CAP_WRITE' in rt, 'dynamic PFS path capability gate missing')
req('TitanStorageMount_Get(i, &mount)' in rt and 'volumes[produced].root' in rt, 'online PFS volumes not enumerated')
req('fileXioMount' not in rt and 'fileXioMount' not in sdk, 'app/runtime exposes direct FileXio mount ownership')
req('g_pfs_provider.mount' in mount, 'provider lifecycle lost')
# public SDK copies must stay byte-identical
for rel in ('apps/wlaunchelf/include/titan_app_sdk.h','apps/project-nes/include/titan_app_sdk.h','sdk/titan-app/include/titan_app_sdk.h'):
    p=root/rel
    if p.exists(): req(p.read_bytes()==(root/'kernel/include/titan_app_sdk.h').read_bytes(), rel+' diverged')
print('buildfix27-pfs-app-abi-contract: OK append-only provider-gated PFS session ownership')
