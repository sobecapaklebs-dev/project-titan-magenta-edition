# BuildFix30 HWFix2 — 720p / Flow Performance Restore

## Hardware symptom
The attached PS2 diagnostic session recorded 14.27 FPS, ~70 ms average frame time and ~80 ms in the render phase while services/UI/storage were negligible. VRAM telemetry also showed only 0xC000 (49,152) bytes free on the active 720p layout.

## Root causes
1. BF30 cold boot had regressed to `TITAN_VIDEO_720P_PATH_REFERENCE`, rebuilding two native 1280x720/768 PSM16 scanout surfaces instead of the already hardware-approved 960x540 composition path.
2. The built-in Flow themes had been replaced by the Ribbon v8 reference profile (`snake=0`, `reference_ribbon=1`, VU1 deformation disabled), moving much of the animated centerline/tangent work back to EE while producing a visually flatter result.
3. The optimized path itself submitted a blocking GS FINISH after the 960x540 composition and another after the 1280x720 upscale.

## HWFix2
- Release/default 720p = 960x540 internal PSM16 composition -> linear 1280x720 output.
- REFERENCE remains available for DEX A/B/fallback only.
- Invalid/corrupt 720p path preference fails safe to OPTIMIZED.
- The intermediate GS FINISH was removed; GIF ordering + TEXFLUSH provide texture coherency, with one final FINISH/fence at presentation.
- Scarlet/Wine/Cyan/Twilight restore the hardware-proven Network + Flow Themes RT1 Snake profile and VU1 fine deformation.
- Ribbon v8 parser/renderer remains supported for external compatibility but is no longer the built-in release profile.

## Hardware gate
Host contracts can prove source policy, geometry/VU1 capability and transactional safety, but final FPS/p95/GS wait/VRAM-free values must be captured again on the PS2.

## Performance technology observability

Hardware feedback identified that pikaVR and VlombisCore were effectively invisible in the shell even though diagnostics events existed.
HWFix2 therefore makes participation directly inspectable:

- Settings > System exposes `pikaVR` with ACTIVE/OFFLINE state, current VRAM free, optimized-path state, internal resolution, saved bytes, resident textures and cache hit/miss counts.
- Settings > System exposes `VlombisCore` with READY/ACTIVE state, measured event count, active capability mask and per-event counters.
- DEX Performance Overlay now shows live PIKA state/cache/VRAM savings and VLOM capability/event state next to frame timing.
- Export Debug Report now serializes the full pikaVR telemetry surface and VlombisCore measured-participation counters.
- Vlombis status is intentionally evidence-driven: defined capabilities are not shown as active until a material producer records participation.

Current BF30 hardware evidence proves pikaVR initialization (`PIKAVR 0x600`) and VRAM-pressure sampling (`0x650`). HWFix2/1.0-RC1 now wires real producers for all ten canonical Vlombis domains; each remains evidence-driven and will show READY until that producer actually executes on the physical session. See `PROJECT_TITAN_1_0_RC1_HARDWARE_GATE.md` for the complete producer matrix and final hardware gate.
