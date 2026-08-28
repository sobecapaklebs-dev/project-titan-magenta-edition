# Titan Input Backends — BuildFix28

| Backend | Transport | Source status | Notes |
|---|---|---|---|
| DualShock 2 | SIO2/PADMAN | integrated | Baseline/fallback; published into Unified Input. |
| DualShock 3 | USB / Bluetooth | existing PADEMU source | Existing DS34 translator retained. |
| DualShock 4 | USB / Bluetooth | existing PADEMU source | Existing DS34 translator retained. |
| DualSense | USB | integrated source | Sony VID/PID recognition plus 0x01 input translation into DS2/Unified-compatible semantics. |
| DualSense | Bluetooth | integrated source | Existing PADEMU L2CAP reused; 0x31 report identifies generation before generation-specific commands. |
| Xbox One/Series | protocol parser | integrated source | GIP input parser present; PS2 USB transport/backend is a later BF28 unit. |
| Keyboard | planned BF28 unit | pending | Will use PS2SDK input facilities without replacing DS2. |
| Mouse | planned BF28 unit | pending | Will use PS2SDK input facilities without forcing a global shell cursor. |

No modern backend is declared hardware-validated by host contracts alone.
