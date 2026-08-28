#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve(); c=(r/'kernel/src/services/titan_ftp_service.c').read_text(errors='replace')
def req(x,m):
 if not x: raise SystemExit('buildfix27-ftp-data-contract: FAIL: '+m)
req('TitanFtp_OpenPassive' in c and 'Entering Extended Passive Mode' in c and 'Entering Passive Mode' in c,'passive listeners missing')
req('TitanStorageIo_ListDirectory' in c and 'g_list_index++' in c,'paged VFS LIST missing')
req('TitanFtp_TickList' in c and 'TITAN_FTP_TICK_DEADLINE_MS' in c,'cooperative list state machine missing')
req('TitanFtp_TickFile' in c and 'TITAN_STORAGE_IO_TRUNCATE' in c,'CP25 file transfer state missing')
req('socket(' not in c and 'fileXio' not in c,'FTP data plane bypasses Titan ownership')
print('buildfix27-ftp-data-contract: OK passive LIST/NLST data plane')
