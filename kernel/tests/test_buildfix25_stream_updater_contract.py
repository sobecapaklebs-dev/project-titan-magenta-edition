#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
c=(root/'src/services/titan_system_update.c').read_text()
h=(root/'src/services/titan_system_update.h').read_text()
make=(root/'Makefile').read_text()
def req(x,m):
    if not x: raise SystemExit(m)
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in h,'system update generation missing')
req('TitanSystemUpdate_OpenPayloadStream' in c and 'TitanSystemUpdate_DiscardExact' in c,'forward-only stream reader missing')
req('TitanStorageIo_Seek64(' not in c,'updater payload path still uses TitanStorageIo_Seek64')
req('entries[i].offset != expected_offset' in c,'contiguous TPUP payload contract missing')
req('TITAN_SYSTEM_UPDATE_VERIFYING_STAGED' in h and 'TitanSystemUpdate_VerifyStagedStep' in c,'staging reread phase missing')
req('transfer_size' in c and 'transfer_offset' in c,'read/write alternating transfer state missing')
req('--build ' in make and 'PUP_OUTPUT ?=' in make,'PUP target missing')
print('buildfix25-stream-updater-contract: OK')
