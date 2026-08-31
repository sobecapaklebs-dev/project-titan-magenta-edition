#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(".")
s=(root/"kernel/src/services/titan_network.c").read_text(encoding="utf-8")
v=(root/"kernel/include/titan_version.h").read_text(encoding="utf-8")
def req(ok,msg):
    if not ok: raise SystemExit("FAIL: "+msg)

req(('RC5.6-dev9-exec-state-r8' in v) or ('RC5.6-network-audit-final' in v) or ('RC5.6-network-hwtrace-final' in v),"R8/final recovery identity missing")
req('static int g_dev9_loaded;' in s,"persistent DEV9 authority state missing")
req('if(g_dev9_loaded)\n        return 1;' in s,"second DEV9 check can re-execute module")
req('dev9_search_id>0' in s,"zero can still skip DEV9 load")
req('g_dev9_loaded=TitanNetwork_LoadModule(' in s,"DEV9 embedded exec missing")
req('"PS2DEV9",titan_ps2dev9_irx,size_titan_ps2dev9_irx' in s,"wrong DEV9 image")
req('if(dev9_verify<=0)' not in s,"post-load search can still invalidate exec")
req('g_network_test.dev9_ok=g_dev9_loaded?1:0;' in s,"status still trusts DEV9 name lookup")
req('g_network_test.smap_ok=g_smap_loaded?1:0;' in s,"status still trusts SMAP name lookup")
req('g_dev9_loaded=0;' in s,"DEV9 state is not reset on network init")

eth=s[s.index("static int TitanNetwork_EnsureEthernetDriver(void)"):
      s.index("static int TitanNetwork_WaitForNetmanRpcServer(void)")]
req('g_smap_loaded=TitanNetwork_LoadModule(' in eth,"SMAP rebind lost")
req('"SMAP",titan_smap_irx,size_titan_smap_irx' in eth,"wrong SMAP image")
print("RC5_6_R8_DEV9_EXEC_STATE_CONTRACT=PASS")
