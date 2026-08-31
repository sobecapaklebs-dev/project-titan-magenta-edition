#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
s = (root / "kernel/src/services/titan_network.c").read_text(encoding="utf-8")
start = s.index("static void TitanNetwork_InitInterfaces(void)")
end = s.index("void TitanNetwork_Initialize(void)", start)
init = s[start:end]
def req(ok, msg):
    if not ok:
        raise SystemExit("FAIL: " + msg)
req("RC5.6-R4: module-name lookup is diagnostic only." in init, "R4 marker missing")
req('netman_search_hint=SifSearchModuleByName("Network_Manager");' in init, "search telemetry missing")
req('g_netman_loaded=TitanNetwork_LoadModule(' in init, "embedded NETMAN load missing")
req('"NETMAN",titan_netman_irx,size_titan_netman_irx' in init, "wrong NETMAN image")
req(init.index('g_netman_loaded=TitanNetwork_LoadModule(') < init.index('TitanNetwork_EnsureEthernetDriver()'), "NETMAN must precede SMAP")
req(init.index('TitanNetwork_EnsureEthernetDriver()') < init.index('TitanNetwork_WaitForNetmanRpcServer()'), "SMAP must precede RPC probe")
req('if(netman_resident>=0)\n                g_netman_loaded=1;' not in init, "name lookup still authoritative")
req('/* Successful bind to NETMAN_RPC_NUMBER is the authoritative health proof. */\n    g_netman_loaded=1;' in init, "RPC success authority missing")
req(init.index('TitanNetwork_WaitForNetmanRpcServer()') < init.index('netman_init_result=NetManInit();'), "NetManInit before bounded RPC proof")
print("RC5_6_R4_NETMAN_RPC_AUTHORITY_CONTRACT=PASS")
