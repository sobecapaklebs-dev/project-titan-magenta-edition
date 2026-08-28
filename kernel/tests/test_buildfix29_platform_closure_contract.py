#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
dl=(r/'kernel/src/services/titan_download_service.c').read_text()
st=(r/'kernel/src/services/titan_store_service.c').read_text()
inp=(r/'kernel/src/services/titan_unified_input.c').read_text()
ux=(r/'kernel/src/services/titan_controller_ux.c').read_text()
welcome=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
over=(r/'kernel/src/services/titan_ingame_overlay.c').read_text()
checks={
 'download-lazy': all(x in dl for x in ['TitanDownload_EnsureResources','TitanDownloadJob *jobs;','TitanDownloadRingSlot *ring;','unsigned char *worker_stack;']),
 'download-idle-no-io': 'void TitanDownloadService_Update(void)' in dl and 'no network/storage work here' in dl,
 'store-lazy': 'TitanRepositoryCatalog *catalog;' in st and 'malloc(sizeof(TitanRepositoryCatalog))' in st,
 'system-edge': 'TITAN_INPUT_EVENT_SYSTEM_BUTTON' in inp and 'TITAN_NBTN_SYSTEM' in inp,
 'battery-honest': 'TITAN_INPUT_BATTERY_UNSUPPORTED' in inp and 'battery_valid = 0u' in inp,
 'deferred-notify': 'if(home_ready)emit' in ux and 'g_metrics.deferred' in ux,
 'welcome-device-aware': 'TITAN_INPUT_DEVICE_DUALSHOCK2' in welcome and 'TITAN_NBTN_SYSTEM' in welcome,
 'overlay-system': 'TITAN_NBTN_SYSTEM' in over,
}
bad=[k for k,v in checks.items() if not v]
if bad:
 print('buildfix29-platform-closure-contract: FAIL',','.join(bad)); raise SystemExit(1)
print('buildfix29-platform-closure-contract: OK')
