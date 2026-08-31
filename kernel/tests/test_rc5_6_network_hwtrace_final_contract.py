#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
ver=(root/'kernel/include/titan_version.h').read_text(encoding='utf-8')
ul=(root/'modules/titan-ul-engine/src/ethsupport.c').read_text(encoding='utf-8')
def req(ok,msg):
    if not ok: raise SystemExit('FAIL: '+msg)
start=net.index('static void TitanNetwork_InitInterfaces(void)')
end=net.index('void TitanNetwork_Initialize(void)',start)
init=net[start:end]
req('RC5.6-network-hwtrace-final' in ver,'hardware-trace recovery identity missing')
req('NetManSetMainIF("SMA")' in init,'SMA alias selection missing')
req('NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in init,
    'bootstrap still forces AUTO before SMAP asynchronous initialization completes')
req('NETMAN_NETIF_IOCTL_GET_LINK_STATUS' in net,'physical link polling removed')
req('0x264' in init,'deferred/default AUTO telemetry missing')
req('static int CurrentMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;' in ul,
    'protected UL/OPL AUTO baseline changed')
req('if (CurrentMode != mode)' in ul and 'NetManSetLinkMode(mode)' in ul,
    'protected UL/OPL conditional link-mode policy changed')
print('RC5_6_NETWORK_HWTRACE_FINAL_CONTRACT=PASS')
