# Theme Engine v4 Validation — BuildFix28 CP14

Status: runtime/renderer + flagship workload HOST_VALIDATED; PS2 target/hardware validation pending external toolchain/hardware.

PASS:
- Theme v4 runtime ASan+UBSan.
- fixed 32-layer parsing and stable z/declaration ordering.
- multilayer evaluation and renderer integration.
- parallax and independent per-layer LOOP/PINGPONG/ONCE timing.
- translation/scale/rotation/alpha transforms.
- HOME/TOP deltas driven by the shell's real top transition.
- deterministic rare-event gating.
- DECORATIVE -> ENHANCED -> SAFE graceful degradation.
- fixed rotation/upload staging; no allocation/FileXio in DrawV4.
- DEX/debug report exposes active/visible/animated/static layers, draw count, degradation, EE/VRAM, uploads, CLUT, pikaVR hit/miss/eviction and update/render timing.
- Theme API v1/v2/v3 compatibility remains intact through existing contracts.
- boot degradation and VRAM critical fallback contracts.
- theme context and transition crossfade contracts.

## Flagship composition workload
A project-owned synthetic Vice City-style stress fixture validates the architecture without bundling commercial artwork. It contains 14 independent RGB565 layers representing sky/cloud/skyline/ocean/reflection/vegetation/traffic/characters/foreground/neon/atmosphere plus two rare-event layers. Six or more independent animation periods are used; no fullscreen video, executable theme payload, or hundreds of fullscreen frames are involved.

Host gate result:
`theme-v4-flagship: OK 14-layer/independent-timeline/rare-event/top-transition/degradation`

The synthetic fixture is an engine validation workload, not the private artistic GTA VI theme asset package. Real PS2 FPS, GS/VRAM pressure and final private-theme visual validation remain HARDWARE_PENDING.
