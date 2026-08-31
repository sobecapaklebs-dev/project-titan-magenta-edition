#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
s=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8',errors='replace')

def req(v,m):
    if not v:
        raise SystemExit('validated-ethernet-telemetry: FAIL: '+m)

for code in ('0x240','0x241','0x242','0x243','0x244','0x245','0x246','0x247','0x248','0x249','0x24b','0x250','0x251'):
    req(code in s,'missing diagnostic event '+code)

req('dev9_search_id=SifSearchModuleByName("dev9");' in s and 'dev9_from_exec' in s and 'g_dev9_loaded' in s and 'dev9_search_id>0' in s,'DEV9 resident/load distinction missing')
eth_driver=s[s.index('static int TitanNetwork_EnsureEthernetDriver'):s.index('static void TitanNetwork_InitInterfaces')]
req('TitanNetwork_LoadModule(' in eth_driver and '"SMAP",titan_smap_irx' in eth_driver,'SMAP load path changed/missing')
req('SifSearchModuleByName("SMAP_driver")' in eth_driver,'SMAP residency verification missing')
req('ps2ip_result=ps2ipInit(&ip,&nm,&gw)' in s,'ps2ip init telemetry missing')
req(s.count('netman_init_result=NetManInit();') == 1,'RC5.5 must use exactly one explicit idempotent NetManInit preflight before ps2ipInit')
req('link_ioctl=NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS' in s,'raw link ioctl result not retained')
req('g_diag_last_ethernet_dhcp_status' in s and 'ipinfo.dhcp_status' in s,'DHCP transition telemetry missing')
req('Ethernet initialized; waiting for RJ45 link / DHCP' in s,'boot state still Wi-Fi-specific for Ethernet')

retry=s[s.index('int TitanNetwork_Retry(void)'):s.index('const char *TitanNetwork_GetStateName',s.index('int TitanNetwork_Retry(void)'))]
retry_compact=''.join(retry.split())
req('TitanNetwork_EnsureEthernetDriver()' in retry,'Ethernet retry cannot re-open DEV9/SMAP')
req('TitanNetwork_SelectMainInterface(TITAN_NETWORK_PREFER_ETHERNET,1)' in retry_compact,'Ethernet retry does not re-arm interface/DHCP lifecycle')

neut=(root/'kernel/src/services/titan_neutrino.c').read_text(encoding='utf-8',errors='replace')
for token in ('-titan-igr=','-titan-igr-profile=','-titan-exit='):
    req(token not in neut,'external game IGR/exit arg reintroduced: '+token)
print('validated-ethernet-telemetry: PASS')
