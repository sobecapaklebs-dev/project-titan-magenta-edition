#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
shell=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
sched=(r/'kernel/src/services/titan_storage_scheduler.c').read_text()
svc=(r/'kernel/src/services/titan_disc_service.c').read_text()
req=lambda c,m: (_ for _ in ()).throw(AssertionError(m)) if not c else None
req('TitanDiscEngine_Shutdown();' in shell and 'TitanDiscService_Shutdown();' in shell,'disc shutdown omitted')
req('TitanStorageScheduler_SetPackageExclusiveMode(1)' in shell,'power storage quiesce missing')
req('TitanDiscService_HasStorageWork()' in sched and 'TitanDiscService_StorageWorkerPump()' in sched,'disc does not share storage owner')
req('750u' in svc or '750' in svc,'bounded no-frame poll cadence missing')
print('titan-disc-shutdown-contract: OK')
print('titan-disc-no-frame-io-contract: OK')
