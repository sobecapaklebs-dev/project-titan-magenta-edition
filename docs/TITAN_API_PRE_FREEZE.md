# Titan API Pre-Freeze — BuildFix29

BuildFix29 treats the public developer surface as append-only. BuildFix30 must not need to move or reinterpret an existing public field.

## API levels

**High** — Titan App SDK. UI, notifications, files/data, HTTP, audio/media, text input, VMC/PFS brokered services and Unified Input queries. A beginner application does not need PS2 GS, DMA or FileXio ownership.

**Mid** — Titan Game Runtime/TGR, VFS/storage capabilities, session/runtime capabilities and renderer state. Suitable for native game ports that need a stable frame/input/audio/storage loop.

**Advanced** — explicit TGR render state, clip-space/material paths, texture format/sampler decisions and PS2-aware performance hints. Advanced code still uses Titan ownership rather than bypassing the kernel with private symbols.

## Unified Input

`TitanAppApiV1` remains ABI v1 and is append-only. BuildFix29 appends:

- `input_get_state(player, TitanAppNormalizedInput*)`
- `input_get_device(player, TitanAppInputDeviceInfo*)`

These expose 32-bit normalized buttons, sticks, triggers/pointer data, logical device identity, transport, player assignment, capabilities, VID/PID, battery state/validity, packet/reconnect/error/latency counters.

The physical PS/Home/Guide capability is visible as metadata, but `TITAN_APP_NBTN_SYSTEM` is reserved to Titan and is masked from application input while Session/Overlay owns system-button policy. Apps do not implement their own overlay hotkey.

## Compatibility rules

- Existing `TitanAppInput` and every pre-BF29 `TitanAppApiV1` field retain offsets.
- Apps gate newly appended fields with `struct_size`/`offsetof`.
- The authoritative public headers shipped in kernel, SDK, Project NES and wLaunchELF are byte-identical.
- Internal Session, Smart IGR, Overlay, pikaVR and VlombisCore symbols are not exported as unstable direct ABIs.
