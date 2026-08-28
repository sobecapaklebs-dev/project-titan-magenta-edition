#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
sched=(root/'kernel/src/services/titan_storage_scheduler.c').read_text()

def need(v,msg): assert v,msg
need('#define TITAN_PADEMU_RPC_RX_BYTES 384u' in rpc,'BondSync-capable SIF RX missing')
need('static unsigned char g_rx[TITAN_PADEMU_RPC_RX_BYTES]' in rpc,'registered SIF RX does not use P10.5.2 capacity')
need('TITAN_PADEMU_RPC_RX_BYTES >= sizeof(TitanPademuBondSync)' in rpc,'compile-time BondSync/RX proof missing')
need('static unsigned char g_rx[32]' not in rpc,'historical 32-byte SIF RX regression remains')
need('if (fno == TITAN_PADEMU_RPC_BOND_SYNC)' in rpc,'P10.5 BondSync RPC missing')
need(sched.count('if (TitanPademuInputBridge_HasStorageWork())')==1,'duplicate PADEMU storage lane remains')
need(sched.count('if (TitanDiagnostics_HasStorageWork())')==1,'duplicate diagnostics storage lane remains')
print('b30-p1052-bt-rpc-rx: PASS BondSync fits SIF RX; storage worker liveness preserved')
