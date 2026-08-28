# Titan Unified Input — BuildFix28

## Status
Material BF28 input normalization core. Host validated; PS2 target/hardware validation remains external.

## Pipeline
`device/transport backend -> Titan Input Device -> normalized state -> shell/application/overlay`

The core keeps device identity independent from player assignment. Up to 8 devices and 4 players are represented without frame-path heap allocation. Disconnected backends are skipped and DS2/SIO2 remains the boot-safe fallback.

## Normalized state
The stable button namespace preserves the classic 16-bit PS2 ordering for Select/L3/R3/Start/D-pad/L2/R2/L1/R1/face buttons and extends it with System, Touchpad and Mic bits. Analog sticks, analog triggers, pointer delta and wheel are represented separately.

## Capabilities
Backends announce only what they materially expose: buttons, D-pad, sticks, triggers, rumble, light, battery, keyboard, pointer, wireless and wired transport.

## Hotplug
Connect/disconnect are bounded table operations. A stable backend hint allows reconnect to recover the same device identity where possible. Player assignment is explicit and survives normal frame updates.

## Frame cost
`TitanUnifiedInput_BeginFrame()` derives pressed/released edges from already-submitted state. It performs no allocation, filesystem access, network access or device enumeration.
