#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
tele=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
wire=(root/'kernel/src/services/titan_wireless_accessories.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v,m): assert v,m

need('TITAN_PADEMU_WIRELESS_BOOTSTRAP=5' in tele,'BOOTSTRAP command missing')
need('command>TITAN_PADEMU_WIRELESS_BOOTSTRAP' in bridge,'EE bridge rejects BOOTSTRAP')
need('command!=TITAN_PADEMU_WIRELESS_QUERY && !TitanBluetoothRuntime_Request()' in bridge,
     'non-QUERY no longer owns async BT runtime activation')

start=wire.index('void TitanWirelessAccessories_BeginStartupReconnect(void)')
end=wire.index('void TitanWirelessAccessories_EndStartupReconnect(void)',start)
boot=wire[start:end]
need('TITAN_PADEMU_WIRELESS_BOOTSTRAP' in boot,'Health startup does not request BOOTSTRAP')
need('TITAN_PADEMU_WIRELESS_BEGIN_SCAN' not in boot,'Health startup performs discovery')
need('TITAN_PADEMU_WIRELESS_QUERY' not in boot,'startup kick regressed to QUERY-only deadlock')

upd=wire[wire.index('void TitanWirelessAccessories_Update(void)'):]
need('TITAN_PADEMU_WIRELESS_QUERY' in upd,'periodic state QUERY removed')

need('request.command != TITAN_PADEMU_WIRELESS_QUERY' in rpc,'non-QUERY init gate missing')
need('pademu_transport_request_init()' in rpc,'IOP async transport-init request missing')
need(re.search(r'if\s*\(\s*request\.command\s*!=\s*TITAN_PADEMU_WIRELESS_QUERY\s*\)[\s\S]{0,700}?pademu_transport_request_init\(\)',rpc),
     'transport init no longer causally gated by non-QUERY request')

need('#define TITAN_PADEMU_RPC_RX_BYTES 384u' in rpc,'P10.5.2 SIF RX regression')
need('P10.5.7' in bt or 'TITAN_WIRELESS_BA(22)' in bt,'P10.5.7 ACL reassembly missing')
need('iomanX_open(' not in bt and 'iomanX_write(' not in bt,
     'P10.5 storage ownership regressed into ds34bt IOP')

print('b30-p1061-startup-bt-bootstrap: PASS Health BOOTSTRAP -> async IOP transport init; QUERY remains passive; no discovery/re-pair')
