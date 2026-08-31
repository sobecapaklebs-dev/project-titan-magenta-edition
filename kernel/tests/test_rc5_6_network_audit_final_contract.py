#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
hdr=(root/'kernel/src/services/titan_network.h').read_text(encoding='utf-8')
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text(encoding='utf-8')
loc=(root/'kernel/src/services/titan_localization.c').read_text(encoding='utf-8')
ver=(root/'kernel/include/titan_version.h').read_text(encoding='utf-8')

def req(ok,msg):
    if not ok:
        raise SystemExit('FAIL: '+msg)

req(('RC5.6-network-audit-final' in ver) or ('RC5.6-network-hwtrace-final' in ver),'consolidated recovery identity missing')

# SMAP selection: preserve ABI and module identity, but make the NetIF name
# compatible with NETMAN's 3-characters-plus-NUL public EE selection helper.
mk=(root/'kernel/Makefile').read_text(encoding='utf-8')
tool=(root/'kernel/tools/titan_patch_smap_netif_name.py').read_text(encoding='utf-8')
req('TITAN_SMAP_SOURCE_IRX' in mk and 'TITAN_SMAP_PATCH_TOOL' in mk,
    'SMAP alias generation is not part of the build graph')
req('titan_patch_smap_netif_name.py' in mk,'SMAP alias patch tool is not invoked by Makefile')
req('needle=b"SMAP\\x00\\x00\\x00\\x00"' in tool and
    'replacement=b"SMA\\x00\\x00\\x00\\x00\\x00"' in tool,
    'SMAP alias patch is not the one-byte bounded transformation')
req('SMAP_driver' in tool,'SMAP module identity preservation check missing')
init=net[net.index('static void TitanNetwork_InitInterfaces'):
         net.index('static void TitanNetwork_BootstrapWorker')]
req('NetManSetMainIF("SMA")' in init,'SMAP is not selected through Titan-safe alias')
req('NetManSetMainIF("SMAP")' not in init,'invalid four-character SMAP selection remains active')
req('name=preference==TITAN_NETWORK_PREFER_ETHERNET?"SMA":"WFI";' in net,
    'runtime Ethernet/Wi-Fi switching does not use the safe SMAP alias')

# Search is telemetry only after successful SMAP execution.
eth=net[net.index('static int TitanNetwork_EnsureEthernetDriver'):
        net.index('static int TitanNetwork_WaitForNetmanRpcServer')]
fast=eth[eth.index('if(g_smap_loaded){'):eth.index('g_ethernet_dhcp_wait_polls')]
req('SifSearchModuleByName("SMAP_driver")' in fast,'SMAP telemetry search missing')
req('g_smap_loaded=0;' not in fast,'SMAP telemetry can still invalidate execution authority')
retry=net[net.index('int TitanNetwork_Retry(void)'):net.index('int TitanNetwork_StartConnectionTest')]
req('if(g_smap_loaded && smap_retry_hint<0) g_smap_loaded=0;' not in retry,
    'retry still invalidates SMAP from a name lookup')

# One live truth model, including UNKNOWN-vs-DOWN PHY state.
for field in ('dev9_ready','netman_iop_ready','netman_rpc_ready','netman_ee_ready',
              'ps2ip_ready','main_interface_ready','ethernet_link_known'):
    req(('int '+field+';') in hdr, field+' live truth field missing')
req('link_valid=(link_ioctl==NETMAN_NETIF_ETH_LINK_STATE_UP' in net,
    'live PHY validity is not tracked separately from link down')
req('g_network.ethernet_link_known=link_valid?1:0;' in net,
    'live PHY known state is not persisted')

# Connection test snapshot has explicit validity and cannot contaminate live services.
req('int valid;' in hdr,'connection-test validity flag missing')
req('static void TitanNetwork_InvalidateConnectionTest(void)' in net,
    'connection-test invalidation missing')
services=ui[ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SERVICES)'):
            ui.index('static void TitanShellScene_OpenSettingsCategoryAnimated')]
req('TitanNetwork_GetConnectionTestStatus' not in services,
    'Network Services Status still consumes a stale test snapshot')
req('network->netman_rpc_ready' in services and 'network->main_interface_ready' in services,
    'Network Services Status is not live-state driven')

test_ui=ui[ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_TEST)'):
           ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_STATUS)')]
req('"Physical Link",physical_detail,physical_result' in test_ui,
    'Physical Link still displays software/backend detail')
req('"Connection Type",software_detail' in test_ui,
    'software/backend detail is not attached to the software-facing row')
req('test->valid' in test_ui,'test UI does not reject stale snapshots')

status=ui[ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_STATUS)'):
          ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SERVICES)')]
req('network->ethernet_link_known' in status,'connection-status UI conflates unknown PHY with DOWN')
req('second_label = "Physical Link"' in status and 'second_label = "SSID"' in status,
    'connection-status second row is not interface-specific')

# Settings rows localize descriptions/values too, not labels only.
req('TitanLocalization_Compat(line1 != 0 ? line1 : "")' in ui and
    'TitanLocalization_Compat(line2 != 0 ? line2 : "")' in ui,
    'Settings descriptions/values are not localized consistently')
req('"Connection settings changed; run the test again."' in loc,
    'new network truth-model UI text lacks localization')
req('"Ethernet physical link status unavailable"' in loc,
    'unknown PHY status lacks localization')

# Physical R8/final trace closure: SMA selection succeeds, but frozen SMAP
# returns -ENXIO if SetLinkMode(AUTO) is called before SmapIsInitialized.
req('NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in net,
    'premature fatal AUTO link-mode call returned after hardware closure')
req('TITAN_DIAG_NETMAN,0x264,0' in net,
    'default AUTO/deferred link-mode telemetry missing')

print('RC5_6_NETWORK_AUDIT_FINAL_CONTRACT=PASS')
