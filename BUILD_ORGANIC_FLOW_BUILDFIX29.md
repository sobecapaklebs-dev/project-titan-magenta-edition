# Project Titan Magenta Edition RC14 — BuildFix29 Organic Flow

This pass corrects the residual Flow issues observed on hardware after BuildFix28:

- the ribbon looked too rigid, as if it were being rotated like a slideshow;
- the ribbon kept nearly the same width from start to end;
- local deformation was too subtle to feel organic;
- visibility was too low.

## What changed

### 1) Organic motion rather than rigid rotation

`kernel/src/ui/titan_xmb_spline.c` now adds: 

- slow ribbon breathing (`sweep_breathe`)
- visibility pulsing (`sweep_visibility`)
- per-point flex (`flex_bend`)
- non-uniform span pulsing (`span_pulse`)
- tiny point-wise rotation deltas (`point_rot_delta`)

The result is still one PS4-inspired composite ribbon, but it no longer behaves like a single rigid card being rotated.

### 2) Tapered and living silhouette

The ribbon now derives a `ribbon_shape` envelope from the point position along its length. This drives:

- width taper toward the ends
- stronger center body
- more deformation near the living middle sections

So the ribbon no longer begins and ends with nearly the same apparent size.

### 3) Stronger visibility and dynamics

`themes/scarlet-flow/theme.cfg` advanced to **version 3.1.0** and increases: 

- `wave_alpha` 22 → 34
- `highlight_alpha` 38 → 62
- `primary_wave1_thickness_milli` 1450 → 1620
- `primary_wave1_amplitude_milli` 360 → 520
- `flow_amplitude_milli` 420 → 560
- `flow_noise_milli` 40 → 72
- `primary_wave1_drift_y_milli` 5000 → 11000

## Packaging

BuildFix29 advances `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` to **29**.
`make -C kernel pup` now targets:

`Project-Titan-Magenta-Edition-0.3.90-buildfix29-organic-flow-wlaunchelf.pup`

This environment still does not provide the PS2 MIPS toolchain, so only source, patch and host-side contract validation are produced here.
