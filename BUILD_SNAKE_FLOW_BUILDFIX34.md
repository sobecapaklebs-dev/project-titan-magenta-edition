# Project Titan Magenta Edition RC14 — BuildFix34 Snake Flow

Base: BuildFix32 stochastic Flow + VU1 procedural deformation + forward-only TPUP updater + native wLaunchELF.

## Why this change

Hardware/video comparison showed that the PS4 Flow ribbon is not best modeled as a whole sheet translating/rotating between target states. Its visible motion is head-driven: the leading tip chooses a smooth trajectory and the rest of the ribbon follows that trajectory with delay. Turns therefore propagate through the body like a snake.

## Motion model

BuildFix34 adds a theme-owned head-driven trailing spine:

- deterministic pseudo-random waypoints around and outside the logical canvas;
- Catmull-Rom interpolation between waypoints for a continuous head trajectory;
- 65 spine samples, where each body point samples an older head position;
- configurable historical delay between adjacent samples;
- local tangent derived from neighboring historical positions;
- ribbon width is extruded along the local normal, so rotation is an emergent property of the travelled path rather than a global transform;
- per-waypoint width/visibility variation;
- VU1 EFU/ESIN remains responsible for fine surface deformation after the EE supplies the broad spine.

Scarlet Flow 6.0.0 uses:

```text
primary_wave1_snake=1
primary_wave1_snake_speed_milli=2
primary_wave1_snake_extent_x_milli=520000
primary_wave1_snake_extent_y_milli=320000
primary_wave1_snake_trail_frames_milli=4500
primary_wave1_snake_visibility_min_milli=380
primary_wave1_snake_seed=52391
```

The previous stochastic whole-sheet wander remains in the Theme Engine for compatibility/other themes, but Scarlet Flow disables it.

## VU1 balance

Fine deformation was reduced so the VU1 adds living surface motion without dominating the global trajectory:

```text
vu1_deform_speed_milli=720
vu1_deform_frequency_milli=1500
vu1_deform_amplitude_milli=7200
```

## Update

`TITAN_SYSTEM_UPDATE_CURRENT_BUILD` is now 34.

`make -C kernel pup` targets:

```text
Project-Titan-Magenta-Edition-0.3.90-buildfix34-snake-flow-wlaunchelf.pup
```

The PUP keeps the forward-only updater contract and includes the native wLaunchELF payload already used by previous builds.

## Validation in this environment

Passed before packaging:

- profile/VMC, storage fallback, Neutrino/UL, Package Installer
- Theme service
- xmb-spline runtime
- 480i/720p renderer contracts
- VU1 internal-edge and geometry contracts
- audio/UTF-8/source-hardening/Library contracts
- BuildFix25 forward-only updater contract
- wLaunchELF native-port contract
- BuildFix31 VU1 procedural Flow contract
- BuildFix32 stochastic capability compatibility contract
- BuildFix34 snake-flow contract

The monolithic host script exceeded the execution window after the BuildFix20 updater contract; the remaining relevant tail contracts were then executed separately and passed.

No PS2DEV MIPS/VU toolchain is installed in this environment, so the final `kernel.elf`, `.tmod` and `.pup` must be built on the user's PS2DEV host.
