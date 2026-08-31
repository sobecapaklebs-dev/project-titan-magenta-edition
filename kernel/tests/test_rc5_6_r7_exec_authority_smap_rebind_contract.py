#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(".")
s=(root/"kernel/src/services/titan_network.c").read_text(encoding="utf-8")
v=(root/"kernel/include/titan_version.h").read_text(encoding="utf-8")

def req(ok,msg):
    if not ok: raise SystemExit("FAIL: "+msg)

req(('RC5.6-exec-authority-smap-rebind-r7' in v) or ('RC5.6-dev9-exec-state-r8' in v) or ('RC5.6-network-audit-final' in v) or ('RC5.6-network-hwtrace-final' in v),"R7/R8 recovery identity missing")
req(('dev9_ok=dev9_search_id>=0;' in s) or ('dev9_search_id>0' in s and 'g_dev9_loaded' in s),"DEV9 authority contract missing")
req(('dev9_from_exec=dev9_ok?1:0;' in s) or ('dev9_from_exec=g_dev9_loaded?1:0;' in s),"DEV9 exec authority marker missing")
req('if(dev9_verify<=0)' not in s,"DEV9 post-search can still invalidate successful exec")
req('0x261' in s,"DEV9 post-load telemetry missing")

eth=s[s.index("static int TitanNetwork_EnsureEthernetDriver(void)"):
      s.index("static int TitanNetwork_WaitForNetmanRpcServer(void)")]
req('g_smap_loaded=TitanNetwork_LoadModule(' in eth,"embedded SMAP is not executed")
req('"SMAP",titan_smap_irx,size_titan_smap_irx' in eth,"wrong SMAP image")
req('if(g_smap_loaded){' in eth and 'g_network.ethernet_stack_loaded=1;' in eth and 'return 1;' in eth,
    "already-loaded local SMAP execution authority fast path missing")
req('g_smap_loaded=0;' not in eth.split('if(g_smap_loaded){',1)[1].split('g_ethernet_dhcp_wait_polls',1)[0],
    "SMAP name-search telemetry can still invalidate successful execution")
# First cold-path hint must not directly set g_smap_loaded.
cold=eth[eth.index("RC5.6-R7 SMAP rebind"):]
req('if(smap_resident>0)' not in cold and 'if(smap_resident>=0)' not in cold,
    "cold SMAP path still trusts module name instead of executing")
req('if(smap_verify<=0)' not in eth,"SMAP post-search can still invalidate successful exec")
req('Functional proof follows at NetManSetMainIF("SMA"), the Titan-safe SMAP alias.' in eth,
    "functional SMAP proof contract missing")

req(('g_network_test.dev9_ok=SifSearchModuleByName("dev9")>=0;' in s) or ('g_network_test.dev9_ok=g_dev9_loaded?1:0;' in s),
    "assurance DEV9 evidence missing")
req(('g_network_test.smap_ok=SifSearchModuleByName("SMAP_driver")>=0;' in s) or ('g_network_test.smap_ok=g_smap_loaded?1:0;' in s),
    "assurance SMAP evidence missing")
req('if(wifi_verify<0) g_network.wifi_irx_loaded=0;' in s,
    "working AR9271 path was not restored from R6 blanket semantics")

print("RC5_6_R7_EXEC_AUTHORITY_SMAP_REBIND_CONTRACT=PASS")
