from pathlib import Path
root=Path(__file__).resolve().parents[2]
iop=(root/'kernel/iop/titan_ar9271/ar9271_scanner.c').read_text()
ee=(root/'kernel/src/services/titan_network.c').read_text()
asset=(root/'kernel/src/services/titan_asset_service.c').read_text()

# IOP RPC must request the transition, never execute NetMan link callbacks itself.
rpc=iop.split('if(command == TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE)',1)[1].split('if(command == TITAN_AR9271_SCAN_RPC_FR_FORCE_FREEZE)',1)[0]
assert 'g_netman_bridge_requested' in rpc
assert 'SignalSema(g_netman_ctl_sema)' in rpc
assert 'titan_netman_set_link' not in rpc
assert 'titan_netman_control_thread' in iop
assert 'thread.thread=titan_netman_control_thread' in iop

# Production traffic has one owner after TT1426 online proof completes.
rx=iop.split('if(g_wpa.secured && (et==ETH_P_IP||et==ETH_P_ARP))',1)[1].split('static void capture_assoc_response_rates',1)[0]
assert 'g_netman_bridge_active' in rx and 'titan_netman_rx_frame' in rx
assert 'return;' in rx.split('titan_netman_rx_frame',1)[1].split('titan_net_rx',1)[0]

# lwIP/ARP/DHCP must advertise the actual associated AR9271 station MAC.
assert 'memcpy(g_netman_mac,g_radio.macaddr,6);' in iop
assert 'memcpy(output,g_netman_mac,6);' in iop

# EE must not start DHCP in the same call that requests Wi-Fi bridge activation.
select=ee.split('static int TitanNetwork_SelectMainInterface',1)[1].split('static int TitanNetwork_EnsureWifiDriver',1)[0]
wifi_branch=select.split('else if(g_network.wifi_online',1)[1]
assert 'g_wifi_handoff_pending=restart_dhcp?1:0' in wifi_branch
assert 'TitanNetwork_RestartDhcp();' not in wifi_branch.split('}else if(g_network.wifi_rpc_bound)',1)[0]
assert 'TITAN_NETWORK_WIFI_HANDOFF_GRACE_FRAMES' in ee
assert 'g_wifi_handoff_pending && link' in ee
assert 'TitanNetwork_RestartDhcp();' in ee.split('g_wifi_handoff_pending && link',1)[1]

# Asset HTTPS cannot wake on the exact frame networking becomes ONLINE.
assert 'TITAN_ASSET_NETWORK_STABLE_FRAMES' in asset
assert 'online_since_frame' in asset

print('network-wifi-handoff-bf6: PASS')
