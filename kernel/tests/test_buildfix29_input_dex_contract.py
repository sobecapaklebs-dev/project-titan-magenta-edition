#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); s=(r/'kernel/src/ui/titan_shell_scene.c').read_text(); u=(r/'kernel/src/services/titan_unified_input.c').read_text()
for token in ['Controller Diagnostics','TitanUnifiedInput_BackendName','TitanUnifiedInput_BatteryStateName','vendor_id','product_id','reconnect_count','last_latency_us','TitanEarlyInput_GetStatus','System %s','Rumble %s']:
 assert token in s,token
for token in ['TitanUnifiedInput_BackendName','TitanUnifiedInput_BatteryStateName']:
 assert token in u,token
print('titan-input-dex-contract: OK')
