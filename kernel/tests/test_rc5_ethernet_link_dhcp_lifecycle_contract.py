#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
s=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8',errors='replace')

def req(v,m):
    if not v:
        raise SystemExit('rc5-ethernet-link-dhcp-lifecycle: FAIL: '+m)

req('TITAN_RC5_ETHERNET_LINK_DHCP_LIFECYCLE_R1' in s,'forward lifecycle marker missing')
for token in ('g_ethernet_dhcp_pending','g_ethernet_dhcp_started','g_ethernet_last_link_state','0x250','0x251'):
    req(token in s,'missing '+token)

sync=s[s.index('static int TitanNetwork_SyncLwipLinkState'):s.index('static int TitanNetwork_WifiBridgeRpc')]
req('tcpip_callback(' in sync,'link state is not marshalled through tcpip_callback')
req('if(link_up) netif_set_link_up' not in sync,'direct frame-thread link-up mutation remains')
req('else netif_set_link_down' not in sync,'direct frame-thread link-down mutation remains')

select=s[s.index('static int TitanNetwork_SelectMainInterface'):s.index('static int TitanNetwork_EnsureWifiDriver')]
req('g_ethernet_dhcp_pending=1' in select,'Ethernet selection does not arm DHCP')
req('TitanNetwork_RestartDhcp()' not in select,'Ethernet selection still races PHY with immediate DHCP')

init=s[s.index('static void TitanNetwork_InitInterfaces'):s.index('static int TitanNetwork_BindWifiRpc')]
req('g_network.preference!=TITAN_NETWORK_PREFER_ETHERNET' in init,'bootstrap still starts Ethernet DHCP before link')

update=s[s.index('void TitanNetwork_Update(void)'):s.index('void TitanNetwork_SetEnabled')]
transition=update[update.index('if(link_changed)'):update.index('else if(g_network.wifi_online')]
req('TitanNetwork_SyncLwipLinkState()' in transition,'physical link transition does not resync lwIP')
req('else if(link && g_ethernet_dhcp_pending && !g_ethernet_dhcp_started)' in transition,'DHCP is not started only after a stable link-up poll')
req('TitanNetwork_RestartDhcp()' in transition,'deferred DHCP start missing')
wait_block=update[update.index('if(!link){'):update.index('}else if(g_network.wifi_online && g_wifi_bridge_enabled){')]
req('else if(g_ethernet_dhcp_pending || !g_ethernet_dhcp_started)' in wait_block,'DHCP timeout starts before client lifecycle')
print('rc5-ethernet-link-dhcp-lifecycle: PASS')
