#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(".")
s=(root/"kernel/src/services/titan_network.c").read_text(encoding="utf-8")

def req(ok,msg):
    if not ok: raise SystemExit("FAIL: "+msg)

# R8 resolves the R6/R7 contradiction: zero is not enough to skip the
# initial DEV9 load, but successful execution survives a zero post-search.
if "RC5.6-R8 DEV9 authority" in s:
    req('0x25e' in s and '0x25f' in s,"raw search telemetry lost")
    req('dev9_search_id>0' in s,"zero still skips initial DEV9 load")
    req('g_dev9_loaded=TitanNetwork_LoadModule(' in s,"DEV9 exec authority missing")
    req('if(dev9_verify<=0)' not in s,"post-search can invalidate DEV9 exec")
    req('g_smap_loaded=TitanNetwork_LoadModule(' in s,"SMAP rebind missing")
    print("RC5_6_R6_IOP_SEARCH_ZERO_CONTRACT=SUPERSEDED_BY_R8_PASS")
elif "RC5.6-R7:" in s:
    req('0x25e' in s and '0x25f' in s,"R6 raw search telemetry lost")
    req('dev9_from_exec' in s,"R7 no longer distinguishes exec success")
    req('dev9_ok=dev9_search_id>=0;' in s,"R7 DEV9 search fallback missing")
    req('g_smap_loaded=TitanNetwork_LoadModule(' in s,"R7 SMAP rebind missing")
    print("RC5_6_R6_IOP_SEARCH_ZERO_CONTRACT=SUPERSEDED_BY_R7_PASS")
else:
    req("RC5.6-R6 module-search contract" in s,"R6 marker missing")
    req('dev9_resident=dev9_search_id>0;' in s,"DEV9 raw-id semantics missing")
    req('if(dev9_verify<=0) return 0;' in s,"DEV9 zero verification still accepted")
    req('0x25e' in s and '0x25f' in s,"raw DEV9/SMAP search telemetry missing")
    print("RC5_6_R6_IOP_SEARCH_ZERO_CONTRACT=PASS")
