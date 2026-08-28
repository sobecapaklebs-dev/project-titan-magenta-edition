#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
ds=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
h=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.h').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
rt=(root/'kernel/src/services/titan_bluetooth_runtime.c').read_text()
diag=(root/'kernel/src/services/titan_diagnostics.c').read_text()
build=(root/'scripts/build_all.sh').read_text()
verify=(root/'kernel/tools/verify_pademu_embed_identity.py').read_text()

# Race closure: BEGIN_SCAN intent is published before async PAD_INIT starts.
assert 'void ds34bt_note_pending_wireless_command(u32 command);' in h
call=rpc.index('ds34bt_note_pending_wireless_command(request.command);')
init=rpc.index('request_ok = pademu_transport_request_init();', call)
assert call < init
assert 'g_wireless_bootstrap_scan_pending' in ds
assert '0x50313034u' in ds

# Read BD_ADDR must recognize either the already-dispatched scan or the RPC
# bootstrap hint; optional LE entropy must not win that race.
read_idx=ds.index("else if (hci_buf[3] == 0x09 && hci_buf[4] == (0x04<<2)")
window=ds[read_idx:read_idx+3200]
assert 'g_wireless_scan_requested || g_wireless_bootstrap_scan_pending' in window
assert 'g_wireless_hci_startup_ready = 1;' in window
assert 'wireless_start_classic_scan_sequence' in window
assert window.index('g_wireless_scan_requested || g_wireless_bootstrap_scan_pending') < window.index('xbox_ble_request_rand();')

# ds34bt_init resets live state but must not erase a hint published before the
# worker starts. BEGIN_SCAN consumes it; module reset clears it.
init_idx=ds.index('int ds34bt_init(u8 pads, u8 options)')
cmd_idx=ds.index('int ds34bt_wireless_command', init_idx)
init_window=ds[init_idx:cmd_idx]
assert 'g_wireless_bootstrap_scan_pending = 0;' not in init_window
begin_idx=ds.index('if (command == TITAN_PADEMU_WIRELESS_BEGIN_SCAN)', ds.index('int ds34bt_wireless_command'))
begin_window=ds[begin_idx:begin_idx+1200]
assert 'g_wireless_bootstrap_scan_pending = 0;' in begin_window
reset_idx=ds.index('void ds34bt_reset()')
assert 'g_wireless_bootstrap_scan_pending = 0;' in ds[reset_idx:reset_idx+700]

# Runtime identity: log the exact embedded image immediately before LOADFILE.
assert '#define TITAN_BT_RUNTIME_IMAGE_EVENT 0x0317u' in rt
assert '#define TITAN_BT_RUNTIME_FINGERPRINT 0x50313034u' in rt
assert 'bt_runtime_fnv1a32' in rt
log_idx=rt.index('bt_runtime_image_diag();')
exec_idx=rt.index('g_bt_runtime.pademu_module_id = bt_runtime_exec(', log_idx)
assert log_idx < exec_idx
assert 'BT_RUNTIME_IMAGE' in diag and '0x0317u' in diag

# Build identity: freshly rebuilt IRX -> bin2c header -> kernel must be byte-identical.
assert 'verify_pademu_embed_identity.py' in build
assert 'bt_pademu_sha256=' in build
for token in ['P104_EMBED_HEADER=IDENTICAL','P104_EMBED_KERNEL_CONTAINS_IRX=PASS','kernel_bytes.count(raw)']:
    assert token in verify

# Tiny ownership model for the hardware race observed in P10.3.
rpc_pending=True
scan_requested=False
bootstrap_scan_pending=True  # published before PAD_INIT starts
read_bdaddr_prefers_classic = scan_requested or bootstrap_scan_pending
assert rpc_pending and read_bdaddr_prefers_classic
print('b30-p104-pending-scan-embed-identity: PASS RPC bootstrap closes Read_BD_ADDR race + exact embedded BT image fingerprinted')
