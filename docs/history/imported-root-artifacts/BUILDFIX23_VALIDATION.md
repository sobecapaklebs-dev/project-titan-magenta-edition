# BuildFix23 Final Validation

BASELINE_PROVENANCE=CONFIRMED
BASELINE_BUILD=BuildFix22
BASELINE_SHA256=23adba976b2c77d0c41eac9efe2fce2a0fd1498f91997c6174fe92f4c7c071c6
NEXT_BUILD=BuildFix23

## Checkpoints
CP00..CP16 executed monotonically. CP04 evidence/default decision was recovered from the material CP04 manifest/report after its prior full-source archive failed to reach Library; the CP03 full-source snapshot was used only as the code carrier and CP04 append-only telemetry/headroom changes were restored before CP05. No BuildFix22 file was modified.

## 720p
Reference remains default. Visible output 1280x720p, PSM16 storage 1280x768 because 720 rows require 12 complete 64-line GS page rows. Optimized path remains opt-in: 960x540 visible internal, 960x576 storage, LINEAR half-texel/clamped scale to 1280x720p output. Reference framebuffer bytes 3,932,160; optimized framebuffer/composition bytes 3,072,000; recovered physical VRAM 860,160; reserved headroom 524,288; unreserved remainder 335,872. Target tearing/sharpness/frame-time remain hardware/PCSX2 validation gates, therefore no unsafe promotion occurred.

## UI
Transition transforms derive from base state + progress and formal ENTER/EXIT phases. Active draw target is cleared before composition to remove stale-frame dependency. Shared focus interpolation/unique target, cached text bounds, DEX exclusion anchors, collision primitive/categories and rapid-input model are contracted. Package Installer uses four 78px chrome-safe visible rows with ellipsis to prevent row/focus/footer collision; Applications/Library approved layouts are preserved.

## Theme context / engine
HOME full scene and SYSTEM palette+waves+functional UI are formal contexts. Active theme accent remains authoritative. Theme API v1/v2/v3 remains compatible; no v4 was created. Internal compiled scene supports 9 layer classes, stable z/declaration ordering, dirty flags, 128 declared frames but max 3 CURRENT/NEXT/RECENT residents, variable 40–2000ms durations, deterministic timeline modes, bounded streaming queue, explicit VRAM/RAM/prefetch/upload budgets with reserved-headroom enforcement, atlas validation/shared CLUT metadata, existing pikaVR exact palettization/content dedupe, normalized parallax and FULL/BALANCED/REDUCED decorative degradation. No new VU/PATH ownership.

## Host validation
All BuildFix23 CP contracts pass on the final source and are re-run after package extraction. Native renderer 480i/720p reference, Theme v2/v3/crossfade and TitanEYE port contracts pass. R5900 cross-build is attempted only when the toolchain is present; unavailable toolchain is reported, never converted into PASS. Target-only visual/tearing/performance measurements remain explicitly unmeasured.
