#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
hdr=(root/'kernel/src/services/titan_network.h').read_text(encoding='utf-8')
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text(encoding='utf-8')
loc=(root/'kernel/src/services/titan_localization.c').read_text(encoding='utf-8')

u_start=ui.index('static int TitanShellScene_UpdateNetworkPage')
u_end=ui.index('static void TitanShellScene_OpenPackageInstaller',u_start)
u_body=ui[u_start:u_end]

t_start=ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_TEST)')
t_end=ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_STATUS)',t_start)
t_body=ui[t_start:t_end]

checks={
 'rc5.4 marker':'TITAN_RC5_4_ETHERNET_ASSURANCE_R1' in net,
 'real connection test API':'TitanNetwork_StartConnectionTest' in hdr and 'TitanNetwork_GetConnectionTestStatus' in hdr,
 'dedicated test worker':'TitanNetwork_ConnectionTestWorker' in net and 'g_network_test_stack' in net,
 'test worker bounded stack':'TITAN_NETWORK_TEST_STACK_BYTES (12u * 1024u)' in net,
 'test worker priority':'TITAN_NETWORK_TEST_PRIORITY 71' in net,
 'ethernet chain netman':'g_network_test.netman_ok=g_netman_loaded?1:0' in net,
 'ethernet chain dev9':'g_network_test.dev9_ok=g_dev9_loaded?1:0' in net,
 'ethernet chain smap':'g_network_test.smap_ok=g_smap_loaded?1:0' in net,
 'ethernet chain sm0':'ps2ip_getconfig("sm0",&ipinfo)==1' in net,
 'link status ioctl':'NETMAN_NETIF_IOCTL_GET_LINK_STATUS' in net,
 'link mode ioctl':'NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE' in net,
 'external proof 1.1.1.1':'0x01010101u' in net,
 'external proof 8.8.8.8':'0x08080808u' in net,
 'external proof requires dns response':'(response[2]&0x80u)==0u' in net,
 'ps2sdk getconfig exact-success restart':'if(get_ok!=1) return 0;' in net,
 'ps2sdk getconfig exact-success poll':'if(ps2ip_getconfig("sm0",&ipinfo)==1)' in net,
 'stale IP cleared':'TitanNetwork_ClearLiveAddress();' in net,
 'async preference API':'TitanNetwork_RequestPreference' in hdr and 'TitanNetwork_RequestPreference' in ui,
 'no direct retry in network UI':'TitanNetwork_Retry()' not in u_body,
 'test root actually starts':'(void)TitanNetwork_StartConnectionTest();' in ui,
 'test diagnostic has no row navigation': 'case TITAN_NETWORK_PAGE_TEST:\n            return 0;' in ui,
 'no fake Not completed':'Not completed' not in t_body,
 'test rows are not focused':t_body.count('offset_x,alpha,0.0f') >= 5,
 'physical link row':'"Physical Link"' in t_body,
 'runtime software assurance visible':'"Connection Type",software_detail' in t_body and 'software_result' in net,
 'localization test title':'{ "Test Internet Connection",' in loc,
 'localization IP title':'{ "Obtain IP Address",' in loc,
 'localization results':'{ "Checking...",' in loc and '{ "Successful",' in loc and '{ "Failed",' in loc,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(f'{k}: {"PASS" if v else "FAIL"}')
if failed: raise SystemExit('RC5.4 Ethernet assurance contract failed: '+', '.join(failed))
print('rc5.4-ethernet-assurance: PASS')
