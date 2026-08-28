# Project Titan Magenta Edition RC14 — BuildFix27

Base: BuildFix25 forward-only TPUP source.

## Hardware gate cleared

The BuildFix26 updater self-test PUP installed successfully on the user's physical PS2 while running BuildFix25. BuildFix27 therefore returns to the intended normal delivery model: **one real `.pup` update**, no manual kernel replacement.

## Scarlet Flow 2.0.0

The old Flow3D primary-wave contract only described a left Y and right Y, so each primary sheet still had a straight underlying baseline. BuildFix27 extends each theme-owned primary wave with:

- `start_y`
- `control1_y`
- `control2_y`
- `end_y`
- `drift_y`
- `thickness_scale`
- `amplitude_scale`
- `alpha_scale`
- `phase_offset`

The renderer evaluates a cubic Bezier vertical centerline from these four Y anchors, then layers the existing low-amplitude Gerstner/noise displacement on top. Up to three primary sheets remain supported without allocating additional persistent GS/VU surfaces.

Scarlet Flow 2.0.0 uses two crossing sheets: one sweeps from the lower-left through a deep arc into the upper-right; the second follows a softer counter-curve. This is designed to match the layered, crossing, gently moving visual grammar of the PS4 Flow background rather than the previous horizontal center-band behavior.

## wLaunchELF 0.1.2

- keeps the complete-loaded-module readable-span fix from BuildFix24;
- keeps dedicated System Error reporting for rejected draw calls;
- adds a Magenta vertical draw canary using `draw_rect()`, which has no app-owned string pointer. On hardware this makes failures diagnosable even if text primitives are rejected;
- reduces the app inner panel opacity so the window does not present as an unexplained solid-black surface.

## Real BuildFix27 PUP payload

`make -C kernel pup` builds a TPUP build 27 containing:

```text
kernel.elf
TITAN/packages/WLAUNCHELF.pkg
TITAN/apps/TITNWLE01/wlaunchelf.tmod
TITAN/apps/TITNWLE01/icon.rgb565
TITAN/apps/TITNWLE01/titan_app.ini
TITAN/apps/TITNWLE01/UPSTREAM_NOTICE.txt
TITAN/themes/SCARLETFLOW/theme.cfg
```

The installed wLaunchELF app is updated directly in the same rollback-safe PUP transaction. `WLAUNCHELF.pkg` is retained as a recovery/reinstall package.

## Host validation

Full `kernel/tests/run_host_tests.sh`: PASS.

Highlights:

```text
theme-service: OK default + Scarlet Flow persistence
xmb-spline-runtime: OK draws=3602 y=[68.48, 378.17]
system-update-transaction: OK forward-only TPUP stream + short I/O + staged reread + rollback
wlaunchelf-native-port-contract: OK
buildfix27-flow-ps4-wlaunchelf-pup-contract: OK
```

The environment used to prepare this source does not have the user's PS2DEV R5900 cross-compiler, therefore the final `kernel.elf`, `.tmod`, `.pkg` and BuildFix27 `.pup` must be built on the user's PS2DEV machine.
