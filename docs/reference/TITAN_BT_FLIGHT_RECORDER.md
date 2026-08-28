# Titan Bluetooth Flight Recorder — B30 P8

The PADEMU Bluetooth backend now records hardware-path evidence inside the IOP before the EE/Unified Input bridge can hide failures.

## Transport reference
The trace follows the established OPL DS34BT order: PADEMU settings -> transport init -> USB Bluetooth-class probe -> endpoint open -> set configuration -> HCI reset/scan -> connection -> L2CAP -> HID input.

## Safety
- 128-event fixed ring in IOP RAM; no heap allocation.
- No FileXio/ioman writes from USB/HCI callbacks.
- Producer protected with `CpuSuspendIntr`/`CpuResumeIntr`.
- Oldest event is overwritten only when the ring is full; a dropped counter is exported.
- EE drains at most 8 events every 30 frames with `SIF_RPC_M_NOWAIT` using the existing PADEMU RPC client.
- Snapshot input remains single-flight and non-blocking.

## Persisted diagnostics
The EE converts each IOP record into `INPUT ev=0313 PADEMU_IOP_TRACE`. `titan-last.txt` renders a readable `trace=` name. Ring drops/version/build mismatches use `ev=0314 PADEMU_TRACE_STATUS`.

Build fingerprint: `0x42543038` (`BT08`).

Important stages include `MODULE_START`, `ARGS`, `SIO2_POLICY`, `PAD_INIT_BEGIN/END`, `BT_DRIVER_REGISTER`, `BT_USB_PROBE`, `BT_USB_DEVICE`, `BT_USB_INTERFACE`, `BT_USB_ENDPOINT`, `BT_USB_SET_CONFIG`, `BT_USB_CONFIG_CB`, `BT_USB_RX_ARM`, `BT_HCI_COMMAND`, `BT_HCI_EVENT`, `BT_HCI_CONNECT`, `BT_L2CAP_RX`, `BT_HID_RUNNING`, `BT_HID_INPUT`, and `BT_PAIR_REQUEST`.

For USB device records, `x` packs VID:PID. Interface records pack class/subclass/protocol/endpoint-count. HCI command records expose opcode, command length, and control endpoint. HID input is sampled for the first four packets and then every 64 packets to avoid flooding the ring.
