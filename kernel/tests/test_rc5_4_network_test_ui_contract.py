#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text(encoding='utf-8')
loc=(root/'kernel/src/services/titan_localization.c').read_text(encoding='utf-8')
start=ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_TEST)')
end=ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_STATUS)',start)
body=ui[start:end]
checks={
 'five diagnostic fields':all(x in body for x in ('"Obtain IP Address"','"Internet Connection"','"Connection Type"','"Physical Link"','"IP Address"')),
 'real result API':'TitanNetwork_GetConnectionTestStatus()' in body and 'TitanNetwork_GetTestResultName' in body,
 'running state':'"Running connection test..."' in body,
 'no fixed selection':',1.0f' not in body,
 'no fake status literal':'Not completed' not in body,
 'canonical english labels':'"Obtain IP Address"' in body and '"IP Address"' in body,
 'pt translation': '"Obter endereço IP"' in loc and '"Endereço IP"' in loc,
 'es translation': '"Obtener dirección IP"' in loc and '"Dirección IP"' in loc,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(f'{k}: {"PASS" if v else "FAIL"}')
if failed: raise SystemExit('RC5.4 network test UI contract failed: '+', '.join(failed))
print('rc5.4-network-test-ui: PASS')
