# Project Titan RC16 — TitanEYE + Ribbon BuildFix22

Parent: `Project-Titan-0.3.92-rc16-TitanEYE-Ribbon-BuildFix21-source.tar.gz`

This BuildFix is a recovery/continuation of the interrupted performance/platform pass. TitanEYE, TGR and Ribbon/VU1 are preserved baseline components.

## Stable BuildFix22 units

- pikaVR resource identity, sampler semantics, telemetry and renderer counters.
- exact creation-time PSM8/CLUT selection with alpha-preserving GS color packing and PSM32 fallback.
- resident/retired/reclaimed lifetime accounting and physical eviction telemetry.
- software classification and capability-gated technology badges.
- measurable VlombisCore storage-scheduler evidence.
- SAS/mSAS metadata precedence and explicit execution model.
- filterable diagnostics categories without frame-loop logging.
- Package Installer spacing/bounds, generic UI bounds, UTF-8-safe ellipsis, Library two-line measured titles and DEX safe area.
- HOME full-theme vs SYSTEM functional-theme context and complementary scene/Ribbon transition alpha.
- 480i/720p/VRAM contracts preserving the existing renderer strategy.
- SDK documentation for pikaVR/VlombisCore capability truthfulness.

## Badge truth rule

Classification describes what software is/how it executes. Technology badges describe measured Titan participation. `pikaVR` and `VlombisCore` must not be displayed merely because a title is native or because its metadata names the technology.

The existing pre-launch external-engine splash occurs before a target session can produce Titan-native evidence, so BuildFix22 deliberately does not hard-code technology badges into that pre-launch path.

## Validation

- HOST VALIDATED: yes; complete historical + BuildFix22 host suite passed.
- CROSS-BUILD VALIDATED: no; R5900 PS2DEV toolchain and `dvp-as` are unavailable in this environment.
- PCSX2 VALIDATED: no; PCSX2 is unavailable in this environment.
- PS2 HARDWARE VALIDATED: no; requires physical target testing.
