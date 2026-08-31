# Project Titan Magenta Edition RC14 — BuildFix31 VU1 Procedural Flow

BuildFix31 moves the fine, high-frequency Flow deformation into the PlayStation 2 VU1 rather than evaluating every carrier on the EE.

## Architecture

The EE still owns the low-frequency state that is naturally coupled to the shell and theme system:

- long orbital sweep
- cubic-Bezier spine morph
- depth/perspective envelope
- broad Gerstner base mesh
- fallback geometry when VU1 is unavailable

For each VU1 strip the EE now supplies only three procedural scalars:

- `procedural_phase`
- `procedural_frequency`
- `procedural_amplitude`

`TitanWaveVu1_PrepareLayerPoints()` converts those scalars into a folded sine angle plus its quadrature cosine angle. The phase is folded by quadrant into 0..pi/2, which is inside the documented ESIN validity interval, while the quadrant sign is moved into signed amplitude fields. Center Z/W and normal Z/W are scratch transport lanes and never reach GS XYZ2.

## VU1 microprogram

`titan_wave_vu1.vsm` now executes two EFU evaluations per point (sine and cosine-as-phase-shifted-sine):

```text
ESIN P, center.z
WAITP
MFP.y ...
ESIN P, center.w
WAITP
MFP.x ...
```

The results are scaled with signed X/Y amplitudes, added to the center, and only then are the two ribbon rails extruded and emitted via PATH1/XGKICK. The microprogram still uses the existing 65-point / 7-strip data-memory map and does not allocate additional GS VRAM.

VU1 ESIN has non-trivial latency, but it runs on the dedicated EFU pipeline and the workload is small: two evaluations for each of 65 points per strip. The existing FMAC/load work is scheduled around the first EFU operation and `WAITP` is used explicitly before consuming P, as required by the VU instruction contract.

## Theme Engine

Flow3D manifests gain:

```text
vu1_deform_speed_milli
vu1_deform_frequency_milli
vu1_deform_amplitude_milli
```

Scarlet Flow is version 4.0.0 and enables the VU1 carrier. Other themes default to zero VU1 amplitude, so they preserve previous behavior until explicitly opting in.

## Packaging

BuildFix31 advances `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` to 31 and `make -C kernel pup` targets:

`Project-Titan-Magenta-Edition-0.3.90-buildfix31-vu1-procedural-flow-wlaunchelf.pup`

The final MIPS/VU binary must still be compiled on the user's PS2DEV host because this execution environment does not include `dvp-as` or the PS2 EE cross-compiler.
