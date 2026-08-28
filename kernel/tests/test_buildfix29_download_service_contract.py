#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
svc=(root/'kernel/src/services/titan_download_service.c').read_text()
hdr=(root/'kernel/src/services/titan_download_service.h').read_text()
sched=(root/'kernel/src/services/titan_storage_scheduler.c').read_text()
http=(root/'kernel/src/services/titan_http_client.h').read_text()
net=(root/'kernel/src/services/titan_network_client.h').read_text()
pkg=(root/'kernel/src/services/titan_package_installer.h').read_text()
make=(root/'kernel/Makefile').read_text()
checks={
 'fixed-queue':'TITAN_DOWNLOAD_QUEUE_CAPACITY 6u' in hdr and 'calloc(TITAN_DOWNLOAD_QUEUE_CAPACITY, sizeof(TitanDownloadJob))' in svc,
 'streaming-http':'TitanHttpBodyWriteCallback' in http and 'body_write' in svc,
 'network-owner':'TITAN_NETWORK_CLIENT_OWNER_DOWNLOAD_SERVICE' in net and 'TITAN_NETWORK_CLIENT_OWNER_DOWNLOAD_SERVICE' in svc,
 'shared-storage-worker':'TitanDownloadService_StorageWorkerPump' in sched and 'TitanDownloadService_HasStorageWork' in sched,
 'no-network-thread-storage-write':'TitanDownload_BodyWrite' in svc and 'TitanStorageIo_WriteExact' not in svc[svc.index('static int TitanDownload_BodyWrite'):svc.index('static int TitanDownload_WaitForStorage')],
 'temp-file':'.part' in svc,
 'sha-before-package':svc.index('TitanSha256_HexEqual') < svc.index('TitanPackageInstaller_ValidatePackageFile'),
 'package-before-publish':svc.index('TitanPackageInstaller_ValidatePackageFile') < svc.index('TitanStorageIo_AtomicReplace'),
 'package-validator-public':'TitanPackageInstaller_ValidatePackageFile' in pkg,
 'atomic-publish':'TitanStorageIo_AtomicReplace' in svc,
 'honest-resume':'TITAN_DOWNLOAD_RANGE_RESUME_SUPPORTED 0' in svc,
 'no-frame-io':'void TitanDownloadService_Update(void)' in svc and 'no network/storage work here' in svc,
 'target-linked':'src/services/titan_download_service.o' in make and 'src/services/titan_sha256.o' in make,
 'lazy-idle-memory':'TitanDownloadJob *jobs;' in svc and 'TitanDownloadRingSlot *ring;' in svc and 'unsigned char *worker_stack;' in svc and 'No queue heap, ring, worker stack or EE thread exists until first enqueue' in svc,
}
bad=[k for k,v in checks.items() if not v]
if bad:
 print('buildfix29-download-service-contract: FAIL', ', '.join(bad)); raise SystemExit(1)
print('buildfix29-download-service-contract: OK')
