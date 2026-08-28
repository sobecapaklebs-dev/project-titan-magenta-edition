# Project Titan Magenta Edition RC14 — BuildFix32 Stochastic Flow

BuildFix32 is based on BuildFix31 and the PS4 Flow reference video supplied by the user. The reference was sampled repeatedly across its ~115 s duration rather than inferred from a few screenshots.

## What the reference shows

The ribbon does **not** orbit the viewport on a fixed path. Across the reference it:

- emerges from different edges/regions;
- changes orientation only occasionally;
- changes curvature while moving;
- changes apparent width/span;
- sometimes becomes faint or mostly leaves the screen;
- has continuous fine deformation, but the whole ribbon is not a rigid arrow travelling around an ellipse.

## New motion model

Scarlet Flow 5.0 disables the legacy `sweep` orbit and enables `wander`. Every ~8.3 seconds at 60 Hz a new deterministic pseudo-random target state is selected. The renderer uses quintic smootherstep interpolation to reach the next state without teleporting.

A target state controls:

- center X/Y (with extents large enough to place the center off-screen);
- orientation;
- two independent Bezier control-point offsets;
- local tilt;
- span;
- thickness;
- visibility.

The state sequence is deterministic from `wander_seed`, so themes are reproducible and require no mutable RNG service.

## VU1

The BuildFix31 VU1 EFU/ESIN deformation remains active for fine surface motion. Scarlet Flow reduces its speed/frequency/amplitude so the VU1 provides local deformation rather than dominating whole-ribbon motion.

## Theme controls

Per primary wave:

```text
primary_waveN_wander=0|1
primary_waveN_wander_speed_milli=...
primary_waveN_wander_extent_x_milli=...
primary_waveN_wander_extent_y_milli=...
primary_waveN_wander_curve_milli=...
primary_waveN_wander_visibility_min_milli=...
primary_waveN_wander_seed=...
```

Legacy `sweep` remains available for other themes, and themes may still define up to three primary waves.

## Packaging

Build generation is 32 and `make -C kernel pup` targets:

`Project-Titan-Magenta-Edition-0.3.90-buildfix32-stochastic-flow-wlaunchelf.pup`
