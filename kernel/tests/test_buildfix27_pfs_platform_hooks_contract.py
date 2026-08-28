#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve(); c=(r/'kernel/src/platform/ps2/titan_platform_pfs.c').read_text(errors='replace'); h=(r/'kernel/src/services/titan_storage_mount.h').read_text(errors='replace'); mk=(r/'kernel/Makefile').read_text(errors='replace')
def req(v,m):
 if not v: raise SystemExit('buildfix27-pfs-platform-hooks-contract: FAIL: '+m)
for x in ('fileXioGetDeviceList','fileXioMount','fileXioUmount','FILEXIO_MOUNTFLAG_NORMAL','FILEXIO_MOUNTFLAG_READONLY'):
 req(x in c,'missing official PS2SDK PFS/FileXio integration token '+x)
req('strcmp(devices[i].name, wanted)' in c and '"hdd"' in c and '"pfs"' in c,'driver presence gate missing')
req('uint32_t capabilities' in h,'provider mount interface lost capability propagation')
req('TITAN_STORAGE_MOUNT_CAP_WRITE' in c,'RW/RO mount policy not capability driven')
req('src/platform/ps2/titan_platform_pfs.o' in mk,'platform PFS hooks not built')
req('LoadModule' not in c and 'SifLoadModule' not in c,'CP39 must not load unproven PFS IRX')
print('buildfix27-pfs-platform-hooks-contract: OK official FileXio signatures + driver-presence fail-closed gate')
