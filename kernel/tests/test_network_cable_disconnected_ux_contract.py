#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
scene=(root/'kernel/src/ui/titan_shell_scene.c').read_text(encoding='utf-8')
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
ver=(root/'kernel/include/titan_version.h').read_text(encoding='utf-8')
loc=(root/'kernel/src/services/titan_localization.c').read_text(encoding='utf-8')

def req(ok,msg):
    if not ok: raise SystemExit('FAIL: '+msg)

req('RC5.6-network-hwtrace-final' in ver,'backend recovery identity changed')
req('else if (!network->ethernet_link_known)\n                    values[4] = "RJ45 / SMAP";' in scene,
    'Network summary does not preserve UNKNOWN vs DOWN')
req('values[4] = network->ethernet_link_up\n                        ? "Cable connected"\n                        : "Cable disconnected";' in scene,
    'Network summary does not expose known link down')
req('!network->ethernet_link_known\n                ? "RJ45 / SMAP"\n                : (network->ethernet_link_up ? "Cable connected" : "Cable disconnected")' in scene,
    'LAN setup row conflates unknown link with cable disconnected')
req('second_value = "Cable connected";' in scene,'Connection Status does not name connected cable explicitly')
req('second_value = "Cable disconnected";' in scene,'Connection Status does not name disconnected cable explicitly')
req('{ "Cable disconnected", "Cabo desconectado", "Cable desconectado" }' in loc,
    'Cable disconnected localization missing')
req('{ "Cable connected", "Cabo conectado", "Cable conectado" }' in loc,
    'Cable connected localization missing')
# Backend guards: this UX patch must not re-introduce the old fatal AUTO call or old SMAP name.
req('NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in net,
    'old premature AUTO link-mode call reintroduced')
req('TitanNetwork_SetMainInterfaceCompat("SMA")' in net or '"SMA"' in net,
    'validated SMA backend alias disappeared')
print('NETWORK_CABLE_DISCONNECTED_UX_CONTRACT=PASS')
