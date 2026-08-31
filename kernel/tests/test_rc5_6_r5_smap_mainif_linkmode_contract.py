#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
s=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
start=s.index('static void TitanNetwork_InitInterfaces(void)')
end=s.index('void TitanNetwork_Initialize(void)',start)
init=s[start:end]
def req(ok,msg):
    if not ok: raise SystemExit('FAIL: '+msg)
req('smap_select_result=NetManSetMainIF("SMA");' in init,'Titan-safe SMAP alias selection missing')
req(init.index('smap_select_result=NetManSetMainIF("SMA");') < init.index('ps2ipInit(&ip,&nm,&gw);'),
    'SMAP must be selected before ps2ip GET_MAC path')
req('NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in init,
    'obsolete premature AUTO SetLinkMode call is still in bootstrap')
req('0x264' in init,'deferred/default AUTO policy telemetry missing')
req('SMAPSetLinkMode() deliberately returns -ENXIO' in init,
    'hardware-trace rationale missing')
print('RC5_6_R5_SMAP_MAINIF_LINKMODE_CONTRACT=PASS (superseded by hw-trace closure)')
