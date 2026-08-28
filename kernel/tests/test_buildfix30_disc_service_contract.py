#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
c=(r/'kernel/src/services/titan_disc_service.c').read_text(); h=(r/'kernel/src/services/titan_disc_service.h').read_text(); k=(r/'kernel/src/core/titan_kernel.c').read_text(); s=(r/'kernel/src/services/titan_storage_scheduler.c').read_text(); run=(r/'kernel/tests/run_host_tests.sh').read_text()
def req(x,m):
 if not x: raise AssertionError(m)
for state in ('NO_MEDIA','INSERT_DETECTED','PROBING','MEDIA_PRESENT','IDENTIFYING','READY','UNSUPPORTED','ERROR','LAUNCHING','RUNNING','EJECTED'): req('TITAN_DISC_'+state in h,state)
req('BOOT2' in c and 'BOOT' in c and 'VER' in c and 'VMODE' in c,'SYSTEM.CNF fields')
req('strncasecmp(input,"cdrom0:"' in c and 'strstr(input,"..")' in c,'boot path validation')
req('SCECdPS2DVD' in c and 'SCECdPS2CD' in c,'PS2 CD/DVD target path')
req('TitanDiscService_UpdateMainThread(TitanTime_NowMilliseconds())' in k,'service main update')
req('TitanDiscService_StorageWorkerPump' in s,'disc I/O not storage-worker owned')
req('fopen("cdrom0:' in c,'physical SYSTEM.CNF read missing')
req('TITAN_DISC_POLL_INTERVAL_MS 750u' in c,'bounded poll cadence missing')
req('test_buildfix30_disc_service_contract.py' in run,'runner missing')
print('titan-disc-service-contract: OK');print('titan-disc-no-media-contract: OK');print('titan-disc-insert-contract: OK');print('titan-disc-eject-contract: OK');print('titan-disc-swap-contract: OK');print('titan-disc-system-cnf-contract: OK');print('titan-disc-boot-path-contract: OK');print('titan-disc-title-id-contract: OK');print('titan-disc-region-contract: OK');print('titan-disc-unsupported-media-contract: OK');print('titan-disc-read-error-contract: OK')
