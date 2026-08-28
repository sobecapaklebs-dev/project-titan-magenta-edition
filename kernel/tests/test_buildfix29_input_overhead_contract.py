#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); inp=(r/'kernel/src/platform/ps2/titan_input.c').read_text(); bridge=(r/'kernel/src/services/titan_pademu_input_bridge.c').read_text(); ov=(r/'kernel/src/services/titan_platform_overhead.c').read_text()
assert 'TitanOverhead_Begin(TITAN_OVERHEAD_INPUT' in inp
assert 'TitanOverhead_End(TITAN_OVERHEAD_INPUT' in inp
# P3: fresh modern-controller input must not be throttled to one snapshot every
# 15 frames. The bridge is bounded instead by a single asynchronous RPC in
# flight, so the frame path never waits while input latency stays low.
assert '(g_frame % 15u) != 1u' not in bridge
assert 'g_usb_snapshot_rpc_pending' in bridge
assert 'g_bt_snapshot_rpc_pending' in bridge
compact = bridge.replace(' ', '').replace('\n', '')
# P10.2: each PADEMU backend has its own single-flight snapshot RPC. USB
# remains independent; Bluetooth snapshot is serialized with BT control/trace
# traffic on the dedicated BT RPC client.
assert '!g_usb_snapshot_rpc_pending' in compact
assert '!g_bt_snapshot_rpc_pending' in compact
assert 'g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending' in compact
assert 'SIF_RPC_M_NOWAIT' in bridge
assert 'if(!g_overhead.enabled' in ov.replace(' ','')
assert 'sceSifCallRpc' in bridge and 'TITAN_PADEMU_RPC_GET_SNAPSHOT' in bridge
print('titan-input-overhead-contract: OK async per-backend single-flight snapshots; BT control serialized; no 15-frame throttle')
