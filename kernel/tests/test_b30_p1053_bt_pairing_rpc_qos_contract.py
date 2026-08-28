#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()

def need(c,m):
    assert c,m

need('TitanPademuInputBridge_SnapshotHasConnected' in bridge,'adaptive snapshot activity detector missing')
need('TitanPademuInputBridge_WirelessHandshakeActive' in bridge,'wireless handshake pressure detector missing')
need('!g_bond_storage_write_pending && !g_bond_ack_requested' in bridge,'dirty bond snapshot is still polled repeatedly')
need('!TitanPademuInputBridge_WirelessHandshakeActive() && (g_frame%120u)==7u' in bridge,'bond polling not excluded/throttled during pairing')
need('bt_active ? ((g_frame&1u)!=0u) : (!handshake && (g_frame%15u)==3u)' in bridge,'BT snapshots are not adaptive/pairing-suppressed')
need('usb_active ? ((g_frame&1u)==0u) : ((g_frame%15u)==11u)' in bridge,'empty USB PADEMU is still polled at realtime cadence')
need('(g_frame - g_wireless_diag_frame) >= 120u' in bridge,'wireless diagnostic heartbeat is not coalesced')
need('request.command != TITAN_PADEMU_WIRELESS_QUERY' in rpc,'normal Wireless QUERY still floods IOP trace')
need('SIF_RPC_M_NOWAIT' in bridge,'pairing QoS regressed to blocking SIF RPC')
print('b30-p1053-bt-pairing-rpc-qos: PASS adaptive snapshots; bond/query suppression; recorder retention')
