# pikaVR API Pre-Freeze — BuildFix29

pikaVR remains an internal resource manager beneath TGR. This is deliberate: simple developers should not need GS VRAM residency, FINISH retirement or CLUT bookkeeping to draw a texture.

The public contract is TGR. Safe automatic policy remains the default: lifetime/generation validation, residency, deduplication, format selection, state caching, deferred retirement and Reference Mode fallback are applied internally.

Advanced developers already have stable public hints through TGR semantics: PSM16/RGBA32 upload choice, sampler wrap/filter state, explicit texture release, viewport/scissor, render state and material paths. These are behavioral hints, not ownership transfer.

Pre-freeze result: no direct `pikaVR_*` public ABI is introduced in BF29. This prevents BF30 from freezing implementation details while preserving the existing advanced control surface.
