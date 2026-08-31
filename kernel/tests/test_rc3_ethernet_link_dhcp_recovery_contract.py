#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
s=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8',errors='replace')

def req(v,msg):
    if not v:
        raise SystemExit('rc3-ethernet-recovery: FAIL: '+msg)

for token in (
    'TITAN_RC5_ETHERNET_LINK_DHCP_LIFECYCLE_R1',
    'TitanNetwork_SyncLwipLinkState','tcpip_callback',
    'netif_set_link_up','netif_set_link_down',
    'TITAN_NETWORK_ETH_DHCP_RETRY_POLLS','TITAN_NETWORK_ETH_DHCP_MAX_RETRIES',
    'g_ethernet_dhcp_wait_polls','g_ethernet_dhcp_retry_count',
    'g_ethernet_dhcp_pending','g_ethernet_dhcp_started',
    'RJ45 link active; restarting DHCP','RJ45 link active; DHCP did not respond',
    '0x24e','0x24f','0x250','0x251',
):
    req(token in s,'missing '+token)

dhcp=s[s.index('static int TitanNetwork_RestartDhcp'):s.index('static void TitanNetwork_UpdateLwipMac')]
req('disable_result==1 && enable_result==1' in dhcp,'ps2ip_setconfig success is not exact')
req('enable_result>=0' not in dhcp and 'enable_result >= 0' not in dhcp,'>=0 ps2ip bug remains')

select=s[s.index('static int TitanNetwork_SelectMainInterface'):s.index('static int TitanNetwork_EnsureWifiDriver')]
req('NetManSetMainIF(name)' in select,'main interface selection missing')
req('TitanNetwork_SyncLwipLinkState' in select,'lwIP link not synchronized')
req('TitanNetwork_RestartDhcp()' not in select,'interface selection still starts DHCP immediately')

sync=s[s.index('static int TitanNetwork_SyncLwipLinkState'):s.index('static int TitanNetwork_WifiBridgeRpc')]
req('tcpip_callback' in sync,'lwIP link update is not queued on TCP/IP thread')

init=s[s.index('static void TitanNetwork_InitInterfaces'):s.index('static int TitanNetwork_BindWifiRpc')]
req('g_network.preference!=TITAN_NETWORK_PREFER_ETHERNET' in init,'Ethernet DHCP is not gated out of bootstrap')
print('rc3-ethernet-recovery: PASS')
