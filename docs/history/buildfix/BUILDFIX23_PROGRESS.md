# BuildFix23 Progress Manifest

BASELINE: Project-Titan-0.3.92-rc16-TitanEYE-Ribbon-BuildFix22-source.tar.gz
BASELINE_SHA256: 23adba976b2c77d0c41eac9efe2fce2a0fd1498f91997c6174fe92f4c7c071c6
BASELINE_PROVENANCE: CONFIRMED
LATEST_VALID_CHECKPOINT: CP16
COMPLETED_BLOCKS: CP00-CP16 complete
CURRENT_BLOCK: FINAL
NEXT_PENDING_BLOCK: NONE
FILES_CHANGED: CP05 files plus titan_ui_layout_guard.[ch], shell focus/text/DEX integration, CP06 contracts, official host runner
CONTRACTS_PASSED: all BuildFix23 contracts; renderer native 480i/720p reference; Theme v2/v3/crossfade; TitanEYE port contract; CP05-CP14 contracts
CONTRACTS_FAILED: none after correcting test invocation root; full monolithic suite not used because historical runner exceeds execution window
KNOWN_ISSUES: optimized 720p target tearing/sharpness/frame cost requires PS2/PCSX2 validation; CP04 source telemetry recovered from CP03 snapshot + persisted CP04 evidence because prior CP04 full-source archive had not reached Library

720P_DEFAULT_PATH: REFERENCE (unchanged; CP04 owns promotion decision)
720P_REFERENCE_VRAM: 4194304 total reserved; 3932160 framebuffer bytes; 262144 compact/alignment region; 0 free
720P_OPTIMIZED_VRAM: planned framebuffer bytes 3072000; compact/alignment accounting and reserved-headroom policy pending CP04
720P_HEADROOM: raw framebuffer delta 860160 bytes before CP04 budgets
720P_RESERVED_HEADROOM: pending CP04

THEME_ENGINE_VERSION: Theme Shell API v3 + BuildFix23 internal compiled scene graph
THEME_API_VERSION: v1/v2/v3 compatible; no v4 created
THEME_API_CAPABILITIES: v1/v2/v3 preserved; compiled layers; up to 128 declared frames with <=3-frame CURRENT/NEXT/RECENT working set; LOOP/PINGPONG/ONCE/HOLD/deterministic random modes

NEXT_ACTION: PS2/PCSX2 target A/B validation for optimized 720p tearing/sharpness/frame cost; reference remains release default until those gates pass
