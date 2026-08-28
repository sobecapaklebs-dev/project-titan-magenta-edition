#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
tele=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
rpch=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.h').read_text()
pademu=(root/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
diag=(root/'kernel/src/services/titan_diagnostics.c').read_text()

# Shared wire ABI: trace reply fits the existing 256-byte EE buffer by construction.
assert '#define TITAN_PADEMU_RPC_GET_TRACE 3' in tele
assert '#define TITAN_PADEMU_TRACE_MAX_EVENTS 8u' in tele
assert 'TITAN_PADEMU_TRACE_BUILD_ID 0x42543038u' in tele
assert 'TitanPademuTraceEvent events[TITAN_PADEMU_TRACE_MAX_EVENTS]' in tele

# IOP recorder: bounded ring, interrupt-safe producer, no filesystem or heap activity.
assert 'TITAN_PADEMU_TRACE_RING_CAPACITY 128u' in rpc
assert 'CpuSuspendIntr(&oldstate)' in rpc and 'CpuResumeIntr(oldstate)' in rpc
assert 'g_trace_dropped++' in rpc
assert 'pademu_trace_drain(&g_tx.trace)' in rpc
assert 'iomanX_' not in rpc and 'malloc(' not in rpc and 'free(' not in rpc
assert 'void pademu_trace_record' in rpch

# Earliest module/ABI evidence is recorded before transport init.
for token in ['TITAN_PADEMU_TRACE_MODULE_START','TITAN_PADEMU_TRACE_ARGS','TITAN_PADEMU_TRACE_EXPORT_REGISTER',
              'TITAN_PADEMU_TRACE_SIO2_POLICY','TITAN_PADEMU_TRACE_PAD_INIT_BEGIN','TITAN_PADEMU_TRACE_PAD_INIT_END',
              'TITAN_PADEMU_TRACE_RPC_START']:
    assert token in pademu, token
assert pademu.find('TITAN_PADEMU_TRACE_MODULE_START') < pademu.find('if (argc > 1)')
worker=pademu[pademu.find('static void pademu_transport_init_worker'):pademu.find('int pademu_transport_request_init')]
assert worker.find('TITAN_PADEMU_TRACE_PAD_INIT_BEGIN') < worker.find('ok = PAD_INIT') < worker.find('TITAN_PADEMU_TRACE_PAD_INIT_END')

# OPL-aligned DS34BT path: USB class probe -> endpoints -> config -> HCI -> L2CAP/HID.
for token in ['TITAN_PADEMU_TRACE_BT_INIT_BEGIN','TITAN_PADEMU_TRACE_BT_DRIVER_REGISTER',
              'TITAN_PADEMU_TRACE_BT_USB_PROBE','TITAN_PADEMU_TRACE_BT_USB_DEVICE',
              'TITAN_PADEMU_TRACE_BT_USB_INTERFACE','TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT',
              'TITAN_PADEMU_TRACE_BT_USB_ENDPOINT','TITAN_PADEMU_TRACE_BT_USB_SET_CONFIG',
              'TITAN_PADEMU_TRACE_BT_USB_CONFIG_CB','TITAN_PADEMU_TRACE_BT_USB_RX_ARM',
              'TITAN_PADEMU_TRACE_BT_HCI_COMMAND','TITAN_PADEMU_TRACE_BT_HCI_EVENT',
              'TITAN_PADEMU_TRACE_BT_HCI_CONNECT','TITAN_PADEMU_TRACE_BT_L2CAP_RX',
              'TITAN_PADEMU_TRACE_BT_HID_RUNNING','TITAN_PADEMU_TRACE_BT_HID_INPUT',
              'TITAN_PADEMU_TRACE_BT_PAIR_REQUEST']:
    assert token in bt, token
assert 'USB_CLASS_WIRELESS_CONTROLLER' in bt
assert 'USB_SUBCLASS_RF_CONTROLLER' in bt
assert 'USB_PROTOCOL_BLUETOOTH_PROG' in bt
assert 'UsbOpenEndpointAligned' in bt and 'UsbSetDeviceConfiguration' in bt
assert 'UsbControlTransfer(bt_dev.controlEndp, REQ_HCI_OUT' in bt

# EE drain stays single-flight/NOWAIT and persists into the normal diagnostics pipeline.
assert 'g_trace_rpc_pending' in bridge and 'g_trace_rpc_done' in bridge
assert 'TITAN_PADEMU_RPC_GET_TRACE,SIF_RPC_M_NOWAIT' in bridge.replace(' ','')
assert '(g_frame%30u)==1u' in bridge.replace(' ','')
assert 'TITAN_DIAG_INPUT,0x0313u' in bridge.replace(' ','')
assert 'PADEMU_IOP_TRACE' in diag and 'PADEMU_TRACE_STATUS' in diag
assert 'diag_pademu_trace_name' in diag
assert 'BT_HCI_COMMAND' in diag and 'BT_HID_INPUT' in diag

# The recorder must not regress the shell-safe transport-only policy.
# Transport-only safety is checked semantically: source formatting/control-flow
# may differ while preserving the same policy.
_start_pos = pademu.find('int _start')
assert _start_pos >= 0
_start_src = pademu[_start_pos:]
assert 'titan_transport_only' in _start_src
assert re.search(r'\binstall_sio2hook\s*\(\s*\)', _start_src)
assert 'TITAN_PADEMU_TRACE_SIO2_POLICY' in _start_src
assert 'ok = PAD_INIT(pad_enable, pad_options);' in pademu
assert 'PAD_INIT(pad_enable, pad_options)' not in _start_src.split('int install_sio2hook()',1)[0]

# P8 IOP memcpy lowering guard: struct assignment can make GCC emit libc memcpy,
# which is intentionally unavailable under USE_SMSUTILS.
assert 'block->events[count++] = g_trace_ring[g_trace_tail];' not in rpc
assert 'mips_memcpy(&block->events[count]' in rpc

print('b30-p8-bt-flight-recorder: PASS OPL-aligned bounded async IOP->EE BT trace')
