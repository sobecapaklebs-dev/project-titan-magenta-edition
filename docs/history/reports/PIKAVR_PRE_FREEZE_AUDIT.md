# pikaVR Pre-Freeze Audit — BuildFix28

Status: HOST_AUDITED / TARGET_PERFORMANCE_PENDING.

The BF28 audit confirms one VRAM/resource ownership chain: TGR/Titan game texture pool owns generation-checked handles and deferred retirement; pikaVR consumes those handles for fingerprint dedupe, exact RGBA32 -> indexed+CLUT optimization, sampler-state caching and telemetry. No second VRAM manager or independent generation namespace is introduced.

Validated invariants:
- Reference Mode fast path bypasses dedupe/registry work;
- texture aliases are reference-counted and physical eviction is counted only when backing storage is actually reclaimed after retirement/FINISH;
- sampler cache keys include texture handle, sampler lane, wrap and filtering;
- exact palette conversion preserves GS-packed alpha and falls back when >256 distinct GS colors are required;
- total VRAM, texture budget, framebuffer, texture, CLUT, theme, transient, alignment and reserved headroom accounting remain separate;
- historical alpha/viewport/split-screen/ST-Q/RGBAQ/XYZ2/GIF packing contracts are not changed by BF28.

Format review: current material optimization path uses PSM32/PSM16 source semantics and exact indexed PSMT8+CLUT generation. PSMT4 is not falsely advertised as an implemented conversion path in the current API. It remains eligible only after an actual <=16-color converter and GS upload path exist; BF28 does not expose a fake hint.

API freeze recommendation: retain pikaVR as an internal optimization API for BF28; developer-facing TGR remains the stable contract. Final public freeze belongs to the later platform/API freeze stage after R5900 measurements.
