# Titan Modern Controller Matrix — BuildFix28 final source status

`SOURCE_READY` / `IMPLEMENTED` means the transport/protocol/normalization source exists and passed host contracts. It does **not** mean physical-PS2 validation.

| Device | USB | Bluetooth | Normalized basic input | Output extras | Final validation state |
|---|---|---|---|---|---|
| DualShock 2 | PADMAN baseline | n/a | COMPLETE | baseline rumble preserved | hardware regression pending |
| DualShock 3 | existing PADEMU | existing PADEMU | Unified Input bridge | retained existing rumble/battery paths | hardware pending |
| DualShock 4 | existing PADEMU | existing PADEMU | Unified Input bridge | retained rumble/light/battery paths | hardware pending |
| DualSense | IMPLEMENTED | IMPLEMENTED over Titan/PADEMU BT transport | buttons, D-pad, sticks, L2/R2, Create, Options, PS | advanced haptics/adaptive triggers not required; output extras hardware pending | host contracts PASS; hardware pending |
| Xbox One Bluetooth-capable | IMPLEMENTED GIP USB | IMPLEMENTED Bluetooth/BLE backend | buttons, D-pad, sticks, LT/RT, Menu/View/Guide | output extras hardware pending | host contracts PASS; hardware pending |
| Xbox Series | IMPLEMENTED GIP USB | IMPLEMENTED Bluetooth/BLE backend | buttons, D-pad, sticks, LT/RT, Menu/View/Guide/Share where reported | output extras hardware pending | host contracts PASS; hardware pending |
| Keyboard | PS2SDK USB backend | n/a | key up/down, modifiers, text mapping path | n/a | host contracts PASS; hardware pending |
| Mouse | PS2SDK USB backend | n/a | movement, buttons, wheel where available | n/a | host contracts PASS; hardware pending |

Older Xbox One controllers that lack Bluetooth are not falsely advertised as Bluetooth-capable. USB GIP and Bluetooth/BLE are separate transports and converge only at Titan Unified Input.

DualSense Bluetooth does not infer DS4 solely from the shared `Wireless Controller` name; generation-specific initialization waits for a report that identifies DS4 versus DualSense.
