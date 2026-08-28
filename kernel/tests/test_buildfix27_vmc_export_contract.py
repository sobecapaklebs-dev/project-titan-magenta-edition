#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def t(p): return (r/p).read_text(errors='replace')
def req(x,m):
    if not x: raise SystemExit('buildfix27-vmc-export: FAIL: '+m)
vmc=t('kernel/src/services/titan_vmc.c'); h=t('kernel/src/services/titan_vmc.h')
sdk=t('kernel/include/titan_app_sdk.h'); rt=t('kernel/src/services/titan_app_runtime.c')
w=t('apps/wlaunchelf/src/wlaunchelf_titan.c')
req('TitanVmc_ReadFile' in h and 'TitanVmc_ResolveFile' in vmc,'bounded VMC file reader missing')
req('TITAN_APP_VMC_READ_MAX_BYTES' in sdk and 'vmc_read_active_file' in sdk,'bounded app VMC read ABI missing')
req('capacity > TITAN_APP_VMC_READ_MAX_BYTES' in rt and 'TitanAppRuntime_WritableSpan(buffer, capacity)' in rt,'runtime VMC read bounds missing')
req('wl_begin_vmc_export' in w and 'wl_vmc_export_step' in w,'wLaunchELF VMC export job missing')
req('vmc_export_temporary' in w and 'storage_commit_replace' in w,'transactional VMC export missing')
req('storage_remove(g_wle.vmc_export_temporary)' in w,'failed temp cleanup missing')
req('fileXio' not in w and 'gsKit' not in w and 'ExecPS2' not in w,'ownership regression')
print('buildfix27-vmc-export-contract: OK bounded read + transactional storage export')
