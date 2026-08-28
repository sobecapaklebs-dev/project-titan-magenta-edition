# Titan In-Game Overlay Resource Budget — BuildFix28

Overlay state is fixed-size. It does not keep HOME or Theme Engine resident. Native metrics expose EE/IOP/VRAM budget fields, open/close counts and rendered frames. External engines use fixed resident state and minimal GS rendering.

Closed overlay performs no rendering/layout and allocates nothing per frame. Capability may degrade `FULL -> MINIMAL -> RETURN_ONLY -> LEGACY/DISABLED` when memory or GS restoration cannot be proven. Real PS2 EE/IOP/VRAM and latency measurements remain hardware-validation gates.
