# Titan Controller Compatibility Matrix — BuildFix29 source/host state

`HOST PASS` means source/protocol/normalization contracts passed; it is not physical PS2 validation.

| Device | Transport | Input | System | Battery source policy | Early Input | Host | Hardware |
|---|---|---|---|---|---|---|---|
| DualShock 2 | PADMAN | yes | n/a | unsupported unless backend proves otherwise | baseline fallback | PASS | pending regression |
| DualShock 3 | USB | yes | PS | backend reports only; no invented % | source path | PASS | pending |
| DualShock 3 | Bluetooth | yes | PS | backend report | source path | PASS | pending |
| DualShock 4 | USB | yes | PS | backend report when available | source path | PASS | pending |
| DualShock 4 | Bluetooth | yes | PS | backend report | source path | PASS | pending |
| DualSense | USB | yes | PS | protocol report when valid; otherwise Unknown | source path | PASS | pending |
| DualSense | Bluetooth | yes | PS | protocol report when valid | source path | PASS | pending |
| Xbox One BT-capable | USB GIP | yes | Guide | unsupported/unknown unless protocol supplies reliable value | USB path | PASS | pending |
| Xbox One BT-capable | Bluetooth/BLE | yes | Guide | Unknown unless valid report supplies value | source path | PASS | pending |
| Xbox Series | USB GIP | yes | Guide | unsupported/unknown unless reliable | USB path | PASS | pending |
| Xbox Series | Bluetooth/BLE | yes | Guide | Unknown unless valid report supplies value | source path | PASS | pending |
| Keyboard | USB HID/PS2SDK | keys/modifiers | n/a | n/a | not controller boot requirement | PASS | pending |
| Mouse | USB HID/PS2SDK | motion/buttons/wheel where available | n/a | n/a | n/a | PASS | pending |

Older Xbox One models without Bluetooth are not advertised as Bluetooth-capable. USB GIP and Xbox Bluetooth/BLE remain distinct transports.
