#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
hid = (root / "src/services/titan_hid_input.c").read_text()
net = (root / "src/services/titan_network.c").read_text()
kernel = (root / "src/core/titan_kernel.c").read_text()

errors = []

if "PS2MOUSE_READ,SIF_RPC_M_NOWAIT" not in hid:
    errors.append("mouse telemetry is not non-blocking")
if "PS2MOUSE_READ,0," in hid:
    errors.append("synchronous PS2MOUSE_READ remains in frame path")
if "g_mouse_rpc_pending" not in hid or "mouse_rpc_done" not in hid:
    errors.append("mouse async completion state missing")

if "TITAN_AR9271_SCAN_RPC_GET_INFO,SIF_RPC_M_NOWAIT" not in net:
    errors.append("AR9271 GET_INFO is not non-blocking")
if "g_wifi_info_rpc" not in net or "g_wifi_info_rpc_pending" not in net:
    errors.append("dedicated async Wi-Fi status RPC client missing")
if "SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_GET_INFO,0" in net:
    errors.append("synchronous AR9271 GET_INFO remains on control client")

input_begin = kernel.find("TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_INPUT)")
input_end = kernel.find("TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_INPUT)", input_begin)
services_begin = kernel.find("TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_SERVICES)", input_end)
services_end = kernel.find("TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_SERVICES)", services_begin)
if min(input_begin, input_end, services_begin, services_end) < 0:
    errors.append("diagnostic phase markers missing")
else:
    input_block = kernel[input_begin:input_end]
    services_block = kernel[services_begin:services_end]
    for call in ("TitanDownloadService_Update();",
                 "TitanStoreService_Update();",
                 "TitanDiscService_UpdateMainThread("):
        if call in input_block:
            errors.append(f"{call} still attributed to INPUT")
        if call not in services_block:
            errors.append(f"{call} not attributed to SERVICES")

if errors:
    for e in errors:
        print("rc1-async-frame-rpc-contract: FAIL:", e)
    raise SystemExit(1)

print("rc1-async-frame-rpc-contract: PASS mouse + AR9271 status RPC async, phase attribution corrected")
