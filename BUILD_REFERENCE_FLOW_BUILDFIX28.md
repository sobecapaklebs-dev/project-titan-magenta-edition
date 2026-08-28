# Project Titan Magenta Edition RC14 — BuildFix28 Reference Flow

Base: BuildFix27 (`Project-Titan-Magenta-Edition-0.3.90-rc14-buildfix27-flow-ps4-wlaunchelf-pup-source`).

Reference: the user-supplied 1280x720 PS4 Flow video (`2026-08-13 01-11-14(1).mp4`, ~115.4 s).

## Reference-video finding

The previous BuildFix27 model was wrong: it rendered two independently visible primary ribbons crossing each other. In the supplied PS4 video the dominant motion is one broad continuous ribbon/sheet with closely spaced internal bands. Over a long cycle it moves around the viewport, changes from vertical to diagonal to horizontal orientations, enters/leaves different screen edges, and at times is mostly off-screen. The visible parallel bands belong to one composite ribbon rather than to independent main waves.

## Theme-engine changes

`TitanThemePrimaryWave` keeps all previous curve/thickness/amplitude controls and gains optional long-cycle sweep controls:

- `sweep_enabled`
- `sweep_speed`
- `sweep_orbit_x`
- `sweep_orbit_y`
- `sweep_span_scale`
- `sweep_phase`

Manifest keys are generic per primary wave:

- `primary_waveN_sweep`
- `primary_waveN_sweep_speed_milli`
- `primary_waveN_sweep_orbit_x_milli`
- `primary_waveN_sweep_orbit_y_milli`
- `primary_waveN_sweep_span_milli`
- `primary_waveN_sweep_phase`

This is additive: themes can still define up to three independent primary waves, and each can independently opt into the sweep behavior.

## Renderer changes

For swept ribbons, the existing 7-strip VU1 sheet is treated as a single composite ribbon in local space. A low-frequency LUT phase moves the ribbon center on an elliptical orbit; the local sheet is rotated tangent to that orbit. This naturally creates vertical/diagonal/horizontal poses and allows the ribbon to enter/leave any viewport edge without spawning a second competing sheet.

No additional permanent GS surface or VU1 buffer is allocated.

## Scarlet Flow 3.0

Scarlet Flow now uses:

- one primary ribbon (`primary_wave_count=1`);
- broad 7-strip depth layering;
- approximately 97-second orbital cycle at a 60 Hz shell (`sweep_speed_milli=22`);
- 270x155 logical-pixel orbital radii;
- 1.4x ribbon span;
- much lower local Gerstner/noise motion so the long sweep dominates;
- lower alpha/highlight values to match the translucent reference appearance.

## Update generation

BuildFix28 advances `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` to 28. `make -C kernel pup` builds:

`Project-Titan-Magenta-Edition-0.3.90-buildfix28-reference-flow-wlaunchelf.pup`

The PUP continues to carry the kernel, wLaunchELF package/direct installed files, and the Scarlet Flow manifest.

## Validation

Full host suite passes, including:

- theme service / persistence
- XMB/VU1 spline runtime and contracts
- renderer/720p contracts
- forward-only TPUP transaction + short-I/O + reread + rollback
- wLaunchELF native port/lifecycle contracts
- BuildFix24/25/27 compatibility contracts
- BuildFix28 reference-flow contract

The final MIPS `kernel.elf`, `.tmod`, `.pkg`, and `.pup` still require the user's PS2DEV toolchain.
