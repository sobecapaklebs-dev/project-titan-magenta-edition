#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
store=(root/'kernel/src/services/titan_store_service.c').read_text()
browser=(root/'kernel/src/services/titan_browser.c').read_text()
notify=(root/'kernel/src/ui/titan_notifications.c').read_text()
kernel=(root/'kernel/src/core/titan_kernel.c').read_text()
checks={
'repository-parser':'TitanRepository_Parse' in store,
'download-service-only':'TitanDownloadService_Enqueue' in store and 'TitanHttpClient_' not in store,
'package-root':'mass:/TITAN/packages/%s-%s.tpkg' in store,
'browser-repository':'titan://repository' in browser and 'TITAN_BROWSER_OPERATION_REPOSITORY' in browser,
'direct-repo-fetch':'TitanBrowserNet_BeginPage(normalized)' in browser,
'browser-package':'titan://package' in browser and 'TitanStoreService_QueuePackage' in browser,
'notification-progress':'TitanNotifications_UpdateDownloadActivity' in store,
'generic-header':'g_notifications.download_activity.title, processed, total' in notify,
'kernel-init':'TitanStoreService_Initialize();' in kernel,
'kernel-update':'TitanStoreService_Update();' in kernel,
'lazy-catalog':'TitanRepositoryCatalog *catalog;' in store and 'malloc(sizeof(TitanRepositoryCatalog))' in store and 'free(g_store.catalog)' in store,
'no-static-catalog':'TitanRepositoryCatalog catalog;' not in store,
}
bad=[k for k,v in checks.items() if not v]
if bad: print('buildfix29-store-foundation-contract: FAIL',','.join(bad));raise SystemExit(1)
print('buildfix29-store-foundation-contract: OK')
