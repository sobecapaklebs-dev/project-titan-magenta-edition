#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def t(p): return (root/p).read_text(encoding='utf-8',errors='replace')
def req(x,m):
    if not x: raise SystemExit('buildfix27-vmc-adapter: FAIL: '+m)
vmc=t('kernel/src/services/titan_vmc.c')
h=t('kernel/src/services/titan_vmc.h')
sdk=t('kernel/include/titan_app_sdk.h')
runtime=t('kernel/src/services/titan_app_runtime.c')
wle=t('apps/wlaunchelf/src/wlaunchelf_titan.c')
req('TitanVmc_ListRoot' in h and 'TitanVmc_ListRoot' in vmc,'bounded VMC root parser missing')
req('vmc_list_active_root' in sdk and 'TitanAppVmcEntry' in sdk,'append-only VMC app view missing')
req('TitanUserProfiles_GetActive' in runtime and 'TitanVmc_ListRoot' in runtime,'runtime does not bind active profile')
req('vmc:' not in runtime and 'fileXioMount' not in runtime,'runtime must not expose/mount raw VMC filesystem')
req('vmc_list_active_root' in wle and 'vmc_view_active' in wle,'wLaunchELF VMC view missing')
req('TITAN_APP_BUTTON_R2' in wle,'VMC view has no explicit user entry/exit control')
req('TITAN_APP_STORAGE_ENTRY_READ_ONLY' in wle,'VMC view must remain read-only')
req('fileXio' not in wle and 'gsKit' not in wle and 'ExecPS2' not in wle,'native ownership regression')
# Public copies must remain byte-identical.
canon=(root/'kernel/include/titan_app_sdk.h').read_bytes()
for rel in ['apps/wlaunchelf/include/titan_app_sdk.h','apps/project-nes/include/titan_app_sdk.h']:
    req((root/rel).read_bytes()==canon, rel+' diverged from canonical SDK')
print('buildfix27-vmc-adapter-contract: OK read-only active-profile VMC inspection')
