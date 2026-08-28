# Titan Early Input Final — BuildFix29

Early Input starts after minimum USB/storage bootstrap and participates during Health & Safety. Backend IRX loading is performed by the fixed-stack EE worker; initialization returns without waiting and `TitanEarlyInput_Pump(now_ms)` observes bounded progress from the boot frame loop. A 10-second deadline fails open. PADMAN/DS2 remains available.

Known/paired DS3, DS4, DualSense and compatible Xbox Bluetooth devices may reconnect before HOME. Identity, connection/battery state and assignment candidacy are retained into Welcome/HOME rather than recreated unnecessarily. Controller notifications are suppressed during startup and emitted once HOME is ready.


## RC1 hardware boot-safety correction

The Titan shell now loads PADEMU in **transport-only** mode. The embedded PADEMU IRX initializes the selected USB/Bluetooth transport and telemetry RPC but does not install its game-facing LOADCORE/SIO2MAN hooks while Titan itself is running. PADMAN remains the sole SIO2 owner for the shell. Titan UL/Neutrino game launch keeps the historical PADEMU hook path unchanged.

Early Input's EE loader worker is armed during service initialization but is not started until the runtime pump begins, after critical kernel/service bootstrap has completed. A stuck adapter/module can therefore fail the modern backend without preventing kernel initialization.
