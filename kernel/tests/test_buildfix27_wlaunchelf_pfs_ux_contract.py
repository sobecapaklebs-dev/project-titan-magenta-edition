#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
a=(r/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text(errors='replace')
t=(r/'apps/wlaunchelf/tests/host_lifecycle_test.c').read_text(errors='replace')
def req(v,m):
    if not v: raise SystemExit('buildfix27-wlaunchelf-pfs-ux-contract: FAIL: '+m)
for x in ('wl_pfs_api_available','wl_pfs_action','TITAN_APP_BUTTON_R1','pfs_mount','pfs_unmount','TITAN_APP_PFS_CAP_READ | TITAN_APP_PFS_CAP_WRITE'):
    req(x in a,'missing '+x)
req('offsetof(TitanAppApiV1, pfs_status)' in a,'optional append-only PFS ABI size gate missing')
req('fileXio' not in a and 'fileXioMount' not in a,'wLaunchELF bypasses Titan PFS ownership')
req('pfs_mount_calls!=1' in t and 'pfs_unmount_calls!=1' in t and 'wle_test_entry_count()!=6u' in t,'host lifecycle lacks mount/unmount/root-volume proof')
print('buildfix27-wlaunchelf-pfs-ux-contract: OK R1 provider-gated mount/unmount through Titan ABI')
