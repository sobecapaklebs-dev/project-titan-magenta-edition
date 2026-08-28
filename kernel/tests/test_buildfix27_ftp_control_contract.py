#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
c=(root/'kernel/src/services/titan_ftp_service.c').read_text(errors='replace')
h=(root/'kernel/src/services/titan_ftp_service.h').read_text(errors='replace')
mk=(root/'kernel/Makefile').read_text(errors='replace')
def req(x,m):
 if not x: raise SystemExit('buildfix27-ftp-control-contract: FAIL: '+m)
req('TitanFtpService_Start' in h and 'TitanFtpService_Tick' in h and 'TitanFtpService_Stop' in h,'service lifecycle missing')
req('TitanNetworkStream_Listen' in c and 'TitanNetworkStream_Accept' in c and 'TitanNetworkStream_Read' in c and 'TitanNetworkStream_Write' in c,'FTP bypasses Network Stream')
req('socket(' not in c and 'accept(' not in c and 'recv(' not in c and 'send(' not in c and 'fileXio' not in c,'FTP service bypasses Titan ownership')
req('TitanStorageIo_PathInfo64' in c and 'TitanFtp_PathComponentSafe' in c,'VFS/path containment missing')
req('TitanFtp_TickFile' in c and 'TitanStorageIo_AtomicReplace' in c,'CP25 transactional transfer evolution missing')
req('TitanFtp_OpenPassive' in c and 'TitanFtp_TickList' in c,'CP24 passive LIST evolution missing')
req('src/services/titan_ftp_service.o' in mk,'FTP service not linked')
print('buildfix27-ftp-control-contract: OK Titan Network Stream + VFS control plane')
