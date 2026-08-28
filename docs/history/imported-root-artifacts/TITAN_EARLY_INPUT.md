# Titan Early Input — BuildFix28 final

Early Input is initialized after the minimum USB/storage bootstrap and participates during Health & Safety rather than waiting for HOME.

The modern backend bootstrap is fail-open and non-blocking for the boot UI. IRX loading runs in a fixed-stack EE worker. `TitanEarlyInput_Initialize()` returns without waiting for the worker, and the boot loop calls `TitanEarlyInput_Pump(uint64_t now_ms)` once per startup frame to observe progress. A 10-second time-based deadline marks the modern backend unavailable for that boot attempt without disabling PADMAN/DS2 or blocking Health & Safety.

Known/paired Bluetooth data can be restored before HOME. Xbox Bluetooth uses the extensible Titan/PADEMU HCI/L2CAP transport rather than a duplicate Bluetooth host stack. DS3, DS4 and DualSense reuse the same transport where their device backends apply.

A worker that is inside a loadfile RPC is not forcibly killed. Shutdown/recovery quiesces the input service through the normal Titan lifecycle. Physical pairing/reconnect latency and boot behavior remain target/hardware validation items.
