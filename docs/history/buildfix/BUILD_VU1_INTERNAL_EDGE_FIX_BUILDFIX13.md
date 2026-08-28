# Project Titan Magenta Edition 0.3.90-rc14 — BuildFix13

## Hardware symptom
With the VU1 path ACTIVE, the XMB wave showed bright triangular/diamond seams resembling fish scales.

## Root cause
The PATH1 GIFtag used a textured, alpha-blended `GS_PRIM_TRIANGLE_STRIP` with the GS `AA1` primitive-edge antialias bit enabled. The silk texture already supplies an anti-aliased alpha profile. On real GS hardware, edge AA on the translucent strip makes internal triangle boundaries visible under blending.

## Fix
- Keep triangle strip, Gouraud shading, texturing, alpha blending and UV mapping.
- Disable only `AA1` in the VU1 wave `PRIM`.
- No texture, UI, audio or other curated asset is modified.
- Added `test_vu1_wave_no_internal_edges_contract.py` to prevent AA1 from being re-enabled accidentally.

## Expected hardware behavior
DEX overlay remains `VU1 ACTIVE`, but the internal triangle tessellation is no longer visible; only the smooth silk wave remains.
