#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1])
files={p.name:p.read_text(errors='ignore') for p in [r/'modules/titan-ul-engine/include/titan_pademu_telemetry.h',r/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c',r/'modules/titan-ul-engine/modules/pademu/ds34usb.c',r/'modules/titan-ul-engine/modules/pademu/ds34bt.c',r/'kernel/src/services/titan_pademu_input_bridge.c']}
blob='\n'.join(files.values())
for x in ['TITAN_PADEMU_TELEMETRY_RPC_ID','TITAN_PADEMU_RPC_GET_SNAPSHOT','pademu_telemetry_rpc_start','ds34usb_get_telemetry','ds34bt_get_telemetry','TITAN_PADEMU_BATTERY_UNKNOWN','TITAN_NBTN_SYSTEM','TitanUnifiedInput_ConnectEx']:
 assert x in blob,x
assert 'system_button=report->PSButton' in blob or 'system_button = report->PSButton' in blob
assert 'TITAN_PADEMU_BATTERY_UNSUPPORTED' in blob
pademu=(r/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
assert 'pademu_telemetry_rpc_start()' in pademu
assert 'TITAN_PADEMU_TRACE_RPC_START' in pademu
imports=(r/'modules/titan-ul-engine/modules/pademu/imports.lst').read_text()
for x in ['I_sceSifSetRpcQueue','I_sceSifRegisterRpc','I_sceSifRpcLoop','I_CreateThread','I_StartThread']:
 assert x in imports,x
print('buildfix29-pademu-telemetry-contract: OK')
