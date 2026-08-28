#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def t(p): return (r/p).read_text(errors='replace')
def req(x,m):
    if not x: raise SystemExit('buildfix27-vmc-traversal: FAIL: '+m)
vmc=t('kernel/src/services/titan_vmc.c'); h=t('kernel/src/services/titan_vmc.h')
sdk=t('kernel/include/titan_app_sdk.h'); rt=t('kernel/src/services/titan_app_runtime.c')
w=t('apps/wlaunchelf/src/wlaunchelf_titan.c')
req('TitanVmc_ListDirectory' in h and 'TitanVmc_ResolveDirectory' in vmc,'bounded directory traversal missing')
req('depth > 8u' in vmc and 'strcmp(component, "..")' in vmc and "relative_path[0] == '/'" in vmc,'path containment rules missing')
req('vmc_list_active_path' in sdk and 'TitanAppApi_VmcListActivePath' in rt,'append-only VMC path ABI missing')
req('TitanAppRuntime_StringOwned' in rt,'VMC path string ownership is not validated')
req('wl_vmc_parent' in w and 'wl_vmc_append' in w and 'vmc_path' in w,'wLaunchELF nested VMC navigation missing')
req('fileXio' not in w and 'gsKit' not in w and 'ExecPS2' not in w,'wLaunchELF ownership regression')
print('buildfix27-vmc-traversal-contract: OK bounded nested read-only VMC navigation')
