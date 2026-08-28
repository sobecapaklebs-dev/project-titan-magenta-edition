# Project Titan Magenta Edition 0.3.90 RC14 — BuildFix24

## Scope

BuildFix24 is based on the hardware-safe BuildFix23 updater tree and combines three hardware-driven changes:

1. fix the Titan-native wLaunchELF window that opened/responded but rendered no inner content;
2. make Flow3D wave placement an explicit theme property and allow multiple primary waves;
3. split system/component errors away from ordinary notification cards.

## wLaunchELF black-window fix

The app lifecycle was already alive on hardware: the Titan App window appeared, input worked and the user could exit cleanly. The failure was therefore narrowed to the rendering command path rather than a hang.

Titan App read ownership now accepts the complete packed loaded module image and its retained file backing for read-only strings/constants. Writable ownership remains restricted to writable sections and Titan-owned heap. This preserves memory isolation while allowing compiler/linker constant pools to be passed to Titan UI primitives.

The runtime also reports the first rejected app draw through the new System Error popup. A remaining hardware-specific invalid draw will therefore be visible instead of producing a silent black content plane.

wLaunchELF is packaged as version 0.1.1. Its host lifecycle regression now requires both panel and text draw activity.

## Theme-owned primary waves

`TitanThemeFlowStyle` now supports up to three `TitanThemePrimaryWave` descriptors. Each descriptor contains:

- `start_y`
- `end_y`
- `amplitude_scale`
- `alpha_scale`
- `phase_offset`

Theme configuration keys use milli-units for floats:

```text
primary_wave_count=2
primary_wave1_start_y_milli=108000
primary_wave1_end_y_milli=310000
primary_wave1_amplitude_milli=820
primary_wave1_alpha_milli=1000
primary_wave1_phase=0

primary_wave2_start_y_milli=346000
primary_wave2_end_y_milli=174000
primary_wave2_amplitude_milli=620
primary_wave2_alpha_milli=680
primary_wave2_phase=47
```

The renderer reuses the existing VU/GS scratch allocation for each primary wave sequentially. Multi-wave themes therefore do not allocate an additional framebuffer or permanent GS surface.

Scarlet Flow is advanced to 1.1.0 and uses two opposing non-horizontal sweeps, eliminating the previous hard-coded central horizontal Flow ribbon.

## System Error popup / notification policy

A new `titan_system_error` UI service owns system/component failures. Existing callers using `TITAN_NOTIFICATION_ERROR` are automatically routed to this popup and never enter the notification-card queue.

The popup:

- renders above the normal shell/notification layer;
- identifies the failing component and error text;
- accepts X or Circle to dismiss;
- coalesces repeats and keeps a short bounded pending queue.

Routine `ACTIVITY` and `SUCCESS` events no longer create cards. INFO/WARNING remain card-eligible when they are user-relevant. This keeps the card system sparse while the Home notification ticker continues to expose lightweight status.

## Update packaging

Current generation: `24`.

After the MIPS build, run:

```text
make -C kernel pup
```

Output:

```text
Project-Titan-Magenta-Edition-0.3.90-buildfix24-flow-errors-wlaunchelf.pup
```

Payload:

```text
kernel.elf
TITAN/packages/WLAUNCHELF.pkg
TITAN/themes/SCARLETFLOW/theme.cfg
```

The small updater self-test is now Build25 and can be generated with:

```text
make -C kernel pup-selftest
```

## Validation in this environment

Complete `kernel/tests/run_host_tests.sh`: PASS.

Notable results:

```text
theme-service: OK default + Scarlet Flow persistence
xmb-spline-runtime: OK draws=3602 y=[77.82, 376.74]
system-update-transaction: OK preflight CRC + short I/O + multi-file rollback
wlaunchelf-native-port-contract: OK
buildfix24-hardware-updater-contract: OK
buildfix24-flow-errors-wlaunchelf-contract: OK
```

No PS2DEV MIPS cross-compiler is installed in this environment. The final `kernel.elf`, wLaunchELF `.tmod`/`.pkg` and real BuildFix24 `.pup` must therefore be built on the user's PS2DEV host and then confirmed on physical hardware.
