#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve();c=(r/'kernel/src/services/titan_ftp_service.c').read_text(errors='replace');h=(r/'kernel/src/services/titan_network_stream.h').read_text(errors='replace')
def req(x,m):
 if not x: raise SystemExit('buildfix27-ftp-file-contract: FAIL: '+m)
req('TitanNetworkStream_ReadStatus' in h and 'TITAN_NETWORK_STREAM_IO_CLOSED' in h,'EOF-aware network read contract missing')
req('TitanStorageIo_ReadSome' in c and 'Opening file download' in c,'cooperative RETR missing')
req('.titantmp' in c and 'TitanStorageIo_WriteExact' in c and 'TitanStorageIo_AtomicReplace' in c,'transactional STOR missing')
req('File upload committed' in c and 'TitanFtp_CloseFileTransfer(1)' in c,'upload commit/failure cleanup missing')
req('socket(' not in c and 'fileXio' not in c,'FTP file transfer bypasses Titan ownership')
print('buildfix27-ftp-file-contract: OK cooperative RETR + transactional STOR')
