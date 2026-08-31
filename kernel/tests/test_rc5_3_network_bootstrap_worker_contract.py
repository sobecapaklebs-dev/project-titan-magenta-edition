#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
hdr=(root/'kernel/src/services/titan_health_trace.h').read_text(encoding='utf-8')
start=net.index('void TitanNetwork_Update(void)')
end=net.index('void TitanNetwork_SetEnabled', start)
body=net[start:end]
checks={
 'health trace wired':'#include "titan_health_trace.h"' in net,
 'dedicated worker':'TitanNetwork_BootstrapWorker' in net and 'TitanNetwork_StartBootstrapWorker' in net,
 'worker thread creation':'CreateThread(&thread)' in net and 'StartThread(id,0)' in net,
 'worker self cleanup':'ExitDeleteThread();' in net,
 'bounded stack':'TITAN_NETWORK_BOOTSTRAP_STACK_BYTES (16u * 1024u)' in net,
 'explicit priority':'TITAN_NETWORK_BOOTSTRAP_PRIORITY 70' in net,
 'frame loop no synchronous init':'TitanNetwork_InitInterfaces();' not in body,
 'frame loop starts worker':'TitanNetwork_StartBootstrapWorker()' in body,
 'module load boundary':'TITAN_HT_NETWORK_MODULE_EXEC_BEFORE' in net and 'TITAN_HT_NETWORK_MODULE_EXEC_AFTER' in net,
 'module search boundary':'TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE' in net and 'TITAN_HT_NETWORK_MODULE_SEARCH_AFTER' in net,
 'ps2ip boundary':'TITAN_HT_NETWORK_PS2IP_INIT_BEFORE' in net and 'TITAN_HT_NETWORK_PS2IP_INIT_AFTER' in net,
 'mainif boundary':'TITAN_HT_NETWORK_SELECT_IF_BEFORE' in net and 'TITAN_HT_NETWORK_SELECT_IF_AFTER' in net,
 'stable trace codes':all(x in hdr for x in ('TITAN_HT_NETWORK_BOOT_WORKER_ENTER','TITAN_HT_NETWORK_MODULE_EXEC_BEFORE','TITAN_HT_NETWORK_PS2IP_INIT_BEFORE','TITAN_HT_NETWORK_SELECT_IF_BEFORE','TITAN_HT_NETWORK_BOOT_WORKER_EXIT')),
 'DEV9 exec-backed evidence preserved':'g_dev9_loaded' in net and 'dev9_search_id>0' in net,
 'legacy SMAP evidence preserved':'SifSearchModuleByName("SMAP_driver")' in net,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(f'{k}: {"PASS" if v else "FAIL"}')
if failed: raise SystemExit('RC5.3 network bootstrap worker contract failed: '+', '.join(failed))
print('rc5.3-network-bootstrap-worker: PASS')
