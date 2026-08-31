# BuildFix25 Boot/Resource Lifetime Audit — CP07

## Scope
TitanVideo init -> Welcome -> Profile -> HOME, plus SAS/Library discovery coupling to GS residency.

## Proven issue corrected
`TitanPlatform_Initialize()` performs `TitanVideo_Initialize()`, `TitanFont_Initialize()` and `TitanFont_UploadAtlas()`. The prior CP01 phase marker for `FONT` lived later in `titan_kernel.c`, so the actual font upload was counted under VIDEO_INIT. CP07 moves the FONT phase transition to the platform boundary immediately after video init and before font initialization/upload; the later misleading phase transition was removed.

## Audited findings
- Font atlas has one fixed source buffer and one fixed GS atlas allocation; video-mode restore reuploads from the recorded source, rather than allocating a second font atlas.
- `TitanApplications`/SAS discovery stores metadata and paths only and has no TitanVideo/pikaVR/VRAM allocator calls.
- `TitanAppAssets_Initialize()` clears CPU-side state only. App icon atlas upload occurs at a later commit point and reuses the already-reserved avatar-atlas resource; discovery itself does not allocate GS VRAM.
- scene-v2 CPU assets are freed before refresh/replacement. This is EE RAM and is deliberately not merged into GS VRAM accounting.
- advanced Theme timeline planner keeps CURRENT/NEXT/RECENT at max 3 resident frames even with 128 declared frames.
- no proven duplicate GS upload or lost transient handle requiring product refactor was found in this audit.

## Validation
- titan-boot-resource-lifetime-contract: PASS
- titan-sas-lazy-vram-contract: PASS
- theme-timeline-residency-contract: PASS

EE RAM and GS VRAM remain distinct accounting domains.
