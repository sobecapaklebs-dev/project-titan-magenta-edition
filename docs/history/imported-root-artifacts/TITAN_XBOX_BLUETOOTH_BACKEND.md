# Titan Xbox Bluetooth Backend — BuildFix28

## Status
SOURCE_IMPLEMENTED / HOST_VALIDATED / TARGET_AND_HARDWARE_VALIDATION_PENDING.

Xbox Bluetooth is not wired GIP. BF28 implements a distinct BLE HID/HOGP path that converges only after report decoding into Titan Unified Input / DS2 compatibility state.

## Transport
The existing PADEMU USB Bluetooth HCI transport remains the single dongle owner. `titan_ble_hogp` adds LE scan/create-connection, ATT, SMP, GATT HID discovery, report characteristic discovery, CCCD notification enablement, bonding and bonded reconnect. It reuses the existing HCI interrupt + ACL bulk pipes instead of introducing another USB Bluetooth host stack.

BLE Secure Connections uses controller-side HCI P-256 key generation/DHKey operations and Titan's portable AES-128/CMAC implementation for Bluetooth f4/f5/f6. Pairing randomness is sourced from HCI LE Rand before pairing. A controller/dongle without the required LE/Secure-Connections HCI capability is a specific transport incompatibility, not a reason to disable the Xbox backend globally.

Bond state includes peer address/type, LTK, IRK and identity address. The IOP backend exposes set/get bond hooks so Titan Early Input can preload a known bond before the Health & Safety loop and attempt a bounded reconnect.

## Reports
HOGP ATT notifications are routed to the Xbox Bluetooth HID report parser (report ID 0x01) and normalized to buttons, D-pad, sticks, LT/RT, Menu/View and Guide. Series Share is represented as the auxiliary normalized input where available.

## Provenance
Protocol behavior was cross-checked against Linux `drivers/input/joystick/xpad.c` for wired GIP and `atar-axis/xpadneo` for Bluetooth HID behavior. No xpad/xpadneo source is copied into the target; Titan contains an independent implementation of the required protocol subset. BlueKitchen BTstack was deliberately not adopted because its default free license is non-commercial-only.
