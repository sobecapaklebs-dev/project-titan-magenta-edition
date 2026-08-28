#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
mk=(r/'kernel/Makefile').read_text()
plat=(r/'kernel/src/platform/ps2/titan_platform_pfs.c').read_text()
prov=(r/'kernel/src/services/titan_storage_pfs_provider_ps2.c').read_text()
mods=(r/'kernel/src/services/titan_iop_modules.c').read_text()
runner=(r/'kernel/tests/run_host_tests.sh').read_text()
network=(r/'kernel/src/services/titan_network.c').read_text()
validator=(r/'scripts/validate_pfs_target_prereqs.py').read_text()
def req(v,m):
 if not v: raise SystemExit('buildfix27-pfs-iop-loader-contract: FAIL: '+m)
for x in ('ps2atad.irx','ps2hdd.irx','ps2fs.irx'): req(x in mk and x in validator,x+' material gate missing')
req('TITAN_HAVE_PFS_EMBEDDED' in mk and 'TITAN_PFS_IRX_READY' in mk,'optional embed gate missing')
for n in ('"dev9"','"atad"','"hdd_driver"','"pfs_driver"'): req(n in plat,n+' dependency identity missing')
req('SifSearchModuleByName' in plat,'resident module detection missing')
req('TitanStoragePfsIop_Ensure' in plat,'Titan-owned loader sequence not used')
req('TITAN_HAVE_PFS_EMBEDDED' in mods,'conditional IOP storage ownership missing')
req('TitanStorageMount_RegisterPfsProvider' in prov and 'TitanPlatformPfs_Available == 0' in prov,'dynamic provider registration missing')
req('SifSearchModuleByName("dev9")' in network,'network does not reuse resident DEV9')
req('test_buildfix27_pfs_iop_loader_contract.py' in runner,'official runner gate missing')
print('buildfix27-pfs-iop-loader-contract: OK optional embedded official driver chain')
