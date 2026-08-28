# BuildFix25 Progress

BASELINE_PROVENANCE: CONFIRMED
BASELINE_BUILD: TitanEYE/Ribbon BuildFix24 FINAL
BASELINE_SHA256: 9951a6b67b482ed21ae4f32f046075e39db245863debeed4a03156d01f1e8d23
LATEST_VALID_CHECKPOINT: CP11
COMPLETED_BLOCKS: CP00 provenance; CP01 boot VRAM telemetry; CP02 thresholds/allocation policy; CP03 safe theme degradation; CP04 VRAM fault injection; CP05 rollback repair; CP06 fault matrix/preference hardening; CP07 lifetime audit; CP08 regressions; CP09 R5900 gate; CP10 hardware-test profile; CP11 final host/package validation
CURRENT_BLOCK: COMPLETE
NEXT_PENDING_BLOCK: NONE
FILES_CHANGED: CP04 files plus titan_video.c transactional rollback correction and CP05 contracts
CONTRACTS_PASSED: all BuildFix24 python contracts; current BuildFix23 Theme Context/timeline/scene graph; TitanEYE; Ribbon; Theme v2/v3/crossfade; SAS execution model; Vlombis scheduler; renderer 480i/720p; pikaVR 720p reference/scaling/headroom
CONTRACTS_FAILED: none in authoritative/current suite; monolithic runner exceeds command window but complete coverage passed in validated chunks
KNOWN_ISSUES: R5900/PS2SDK/dvp-as unavailable; PCSX2 unavailable; PS2 hardware validation not executed
720P_DEFAULT_PATH: REFERENCE
720P_OPTIMIZED_STATUS: DEX validation opt-in only
720P_RESERVED_HEADROOM: 524288 bytes
R5900_STATUS: NOT EXECUTED — toolchain unavailable
PCSX2_STATUS: NOT EXECUTED — executable unavailable
PS2_HARDWARE_STATUS: NOT EXECUTED
NEXT_ACTION: compile this exact source with PS2DEV/R5900 toolchain, then execute the four-phase hardware checklist; keep 720p Reference until target evidence
