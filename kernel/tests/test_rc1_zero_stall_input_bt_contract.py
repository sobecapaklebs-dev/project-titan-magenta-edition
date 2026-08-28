#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
if (root / 'src').is_dir() and (root / 'src/services').is_dir():
    kernel = root
    project = root.parent
else:
    project = root
    kernel = root / 'kernel'

hid = (kernel / 'src/services/titan_hid_input.c').read_text()
hidh = (kernel / 'src/services/titan_hid_input.h').read_text()
bridge = (kernel / 'src/services/titan_pademu_input_bridge.c').read_text()
bridgeh = (kernel / 'src/services/titan_pademu_input_bridge.h').read_text()
early = (kernel / 'src/services/titan_early_input.c').read_text()
sched = (kernel / 'src/services/titan_storage_scheduler.c').read_text()
diag = (kernel / 'src/services/titan_diagnostics.c').read_text()
scene = (kernel / 'src/ui/titan_shell_scene.c').read_text()
kern = (kernel / 'src/core/titan_kernel.c').read_text()
net = (kernel / 'src/services/titan_network.c').read_text()

errors = []
def need(cond, msg):
    if not cond:
        errors.append(msg)

# The 60 Hz input function must be RAM/cached-state only for HID. FileXio is
# globally serialized by PS2SDK and is therefore forbidden in this region.
update_start = hid.find('void TitanHidInput_Update(void)')
worker_start = hid.find('int TitanHidInput_StorageWorkerPump(void)', update_start)
need(update_start >= 0 and worker_start > update_start, 'HID update/worker functions missing')
if update_start >= 0 and worker_start > update_start:
    update = hid[update_start:worker_start]
    need('fileXioRead(' not in update, 'keyboard FileXio read remains in frame-path HID update')
    need('kbd_ring_pop' in update, 'frame-path keyboard does not consume RAM ring')
    need('PS2MOUSE_READ,SIF_RPC_M_NOWAIT' in update or 'mouse_poll()' in update,
         'mouse frame path is not async/cached')

worker_end = hid.find('void TitanHidInput_Shutdown', worker_start)
worker = hid[worker_start:worker_end] if worker_start >= 0 and worker_end > worker_start else ''
need('fileXioRead(' in worker and 'kbd_ring_push' in worker,
     'keyboard FileXio read is not isolated on storage-worker pump')
need('TitanHidInput_StorageWorkerPump' in hidh, 'HID storage-worker API missing')
need('TitanHidInput_StorageWorkerPump();' in sched, 'storage scheduler does not pump keyboard I/O')

# Pairing is a frame-triggered user action and must never wait for PADEMU IOP.
need('TITAN_PADEMU_RPC_BT_BEGIN_PAIRING,SIF_RPC_M_NOWAIT' in bridge,
     'Bluetooth pairing RPC is not non-blocking')
need('TITAN_PADEMU_RPC_BT_BEGIN_PAIRING,0' not in bridge,
     'blocking Bluetooth pairing RPC survived')
for token in ('g_pair_rpc_pending', 'TitanPademuInputBridge_PairRpcDone',
              'TitanPademuPairingRuntime', 'TitanPademuInputBridge_GetBluetoothPairingRuntime'):
    need(token in bridge + bridgeh, f'pairing async/runtime token missing: {token}')

# Existing async protections must remain present.
need('TITAN_PADEMU_RPC_GET_SNAPSHOT,SIF_RPC_M_NOWAIT' in bridge,
     'PADEMU snapshot regressed to blocking')
need('PS2MOUSE_READ,SIF_RPC_M_NOWAIT' in hid, 'mouse RPC regressed to blocking')
need('TITAN_AR9271_SCAN_RPC_GET_INFO,SIF_RPC_M_NOWAIT' in net,
     'AR9271 status RPC regressed to blocking')

# Misattributed services must stay outside INPUT so hardware traces are useful.
input_begin = kern.find('TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_INPUT)')
input_end = kern.find('TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_INPUT)', input_begin)
services_begin = kern.find('TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_SERVICES)', input_end)
services_end = kern.find('TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_SERVICES)', services_begin)
need(min(input_begin, input_end, services_begin, services_end) >= 0, 'phase markers missing')
if min(input_begin, input_end, services_begin, services_end) >= 0:
    ib = kern[input_begin:input_end]
    sb = kern[services_begin:services_end]
    for call in ('TitanDownloadService_Update();', 'TitanStoreService_Update();',
                 'TitanDiscService_UpdateMainThread('):
        need(call not in ib, f'{call} remains inside INPUT phase')
        need(call in sb, f'{call} missing from SERVICES phase')

# Hardware-facing diagnostics and honest next-boot semantics.
need('0x0310u' in early and 'EARLY_INPUT_STATE' in diag,
     'Early Input hardware state diagnostics missing')
need('0x0311u' in bridge and 'BT_PAIRING_STATE' in diag,
     'Bluetooth pairing hardware state diagnostics missing')
need('INPUT_STEP_STALL' in diag and 'TitanInput_DiagSlowStep' in (kernel / 'src/platform/ps2/titan_input.c').read_text(),
     'input sub-step stall correlation missing')
need('SERVICE_STEP_STALL' in diag and 'TitanKernel_DiagSlowService' in kern,
     'service sub-step stall correlation missing')
need('Bluetooth selected for next boot; reboot Titan, then pair' in scene,
     'pairing UI still pretends a next-boot backend is live now')
need('Starting PADEMU Bluetooth listening asynchronously' in scene,
     'pairing UI does not expose async start')
need('module rc=%d' in scene and 'pademu_module_id' in scene,
     'controller UI does not expose Early Input module result')

if errors:
    for e in errors:
        print('rc1-zero-stall-input-bt-contract: FAIL:', e)
    raise SystemExit(1)
print('rc1-zero-stall-input-bt-contract: PASS frame-safe HID + async pairing + hardware diagnostics')
