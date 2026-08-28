# TitanEYE/Ribbon BuildFix25 — First Hardware Test Profile

Validation status before target build: HOST VALIDATED. R5900 CROSS-BUILD NOT EXECUTED in the ChatGPT environment because the PS2 toolchain is unavailable.

Required target-build properties:
- 720p release cold boot: REFERENCE.
- Optimized 960x540: DEX validation opt-in only.
- DEX selector: available for A/B after the safe Reference path is confirmed.
- Boot VRAM guard: enabled in normal build; host-only allocator fault injection remains disabled unless `TITAN_VRAM_FAULT_INJECTION` is explicitly defined.
- Boot VRAM phase history: enabled and exportable through existing diagnostics.
- WARNING policy: 512 KiB usable headroom.
- CRITICAL policy: 256 KiB usable headroom.
- recovery hysteresis: 640 KiB.
- optimized reserved headroom: 524288 bytes preserved.
- critical Theme behavior: existing minimal/degraded scene, not a second Theme Engine.
- 720p switch: transactional; successful rollback restores initialized/renderable previous path; double failure leaves explicit invalid state.
- 480i: selector preference only, no live 480i resource rebuild.
- TitanEYE/Ribbon/VU1: unchanged by this hardening pass.

Do not promote Optimized to default in this target build.
