#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
network=(root/"kernel/src/services/titan_network.c").read_text(encoding="utf-8")
pfs=(root/"kernel/src/platform/ps2/titan_platform_pfs.c").read_text(encoding="utf-8")
def req(v,msg):
    if not v: raise SystemExit("rc3-iop-module-start: FAIL: "+msg)
req("TITAN_RC4_IOP_ETHERNET_FIX_R1" in network,"network marker missing")
req("result==TITAN_IOP_MODULE_RESIDENT_END" in network,"resident result not accepted")
req("result==TITAN_IOP_MODULE_REMOVABLE_END" in network,"removable-resident result not accepted")
loader=network[network.index("static int TitanNetwork_LoadModule"):network.index("static int TitanNetwork_HexNibble")]
req("result>=0" not in loader and "result >= 0" not in loader,"old >=0 module-result bug remains")
req('SifSearchModuleByName("Network_Manager")' in network,"NETMAN residency not verified")
req('SifSearchModuleByName("SMAP_driver")' in network,"SMAP residency not verified")
req('SifSearchModuleByName("titan_ar9271")' in network,"AR9271 residency not verified")
req("TITAN_RC4_IOP_START_RESULT_FIX_R1" in pfs,"PFS marker missing")
pfs_exec=pfs[pfs.index("static int TitanPlatformPfs_ExecModule"):pfs.index("static int TitanPlatformPfs_EnsureDrivers")]
req("result >= 0" not in pfs_exec,"PFS old >=0 result bug remains")
req("(result == 0 || result == 2)" in pfs_exec,"PFS resident/removable semantics missing")
print("rc3-iop-module-start: PASS")
