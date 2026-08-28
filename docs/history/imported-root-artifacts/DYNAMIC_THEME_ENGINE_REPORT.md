# Dynamic Theme Engine Report — BuildFix23

## CP10 compiled scene graph
Internal compiled scene graph added without creating Theme API v4 and without rewriting existing v1/v2/v3 themes. Supported internal layer classes: BACKGROUND, MIDGROUND, FOREGROUND, SPRITE, LIGHT, PARTICLE, RIBBON, DECORATION, OVERLAY. Each compiled layer carries transform, alpha, visibility, clip/context-ready metadata, z-order, stable declaration order, blend mode, asset identity and parallax scalar. Dirty flags cover TRANSFORM, FRAME, ALPHA, VISIBILITY, ASSET and CONTEXT so static layers need not be rebuilt every frame.

The package->validate->compiled-scene->runtime-compositor architecture is internal capability at CP10; legacy themes remain on their existing visual paths unless they declare future compiled capabilities.

## CP11 frames / timelines / residency
Declared frame capacity is 128 while the runtime residency planner is explicitly capped at CURRENT/NEXT/RECENT (maximum 3), so package frame count no longer implies linear VRAM residency. Frame durations are individually validated at 40–2000ms. LOOP/PINGPONG remain; ONCE/HOLD/RANDOM_START/RANDOM_SEQUENCE are internal capabilities, with deterministic PRNG seed for testability. Timeline update performs no filesystem I/O.

## CP12 streaming / pikaVR / palette / atlas / dedupe
Theme streaming is represented by a bounded 16-request queue; render/timeline code does not perform filesystem I/O. Theme budgets explicitly separate VRAM, RAM, prefetch, upload and reserved headroom, and reservation checks include the permanent headroom floor. Existing pikaVR exact lossless palette builder and content fingerprint dedupe remain the palettization/dedupe mechanisms. Atlas regions carry asset identity/shared CLUT identity and are bounds-validated before runtime use.

## CP13 dynamic effects / parallax / degradation
Parallax consumes only normalized navigation/focus/transition/idle values and cannot read hardware/input directly. Particle controls expose max/spawn/update/draw budgets. Pressure policy degrades decorative work FULL→BALANCED→REDUCED while leaving input/audio/storage/launch and GS correctness outside this decorative budget. No new VU microprogram or PATH ownership was introduced; Ribbon v8/VU1/PATH1 remains untouched.

## CP14 toolchain / SDK
`titan_theme_asset.py` now has `compile-manifest`: host validation of layer count/type, declared frame count, variable durations, missing assets, SHA-256 identity/deduplication, peak CURRENT/NEXT/RECENT residency estimate and compact compiled manifest output. SDK notes document compatibility, working-set budgeting, declarative-only security boundary and reserved-headroom policy.
