# Project Titan BuildFix24 — PS2 Hardware Test Checklist

**Gate state before test:** PS2 HARDWARE VALIDATED = NO. 720P_DEFAULT = REFERENCE.

## Preconditions
1. Preserve the currently working bootloader/kernel rollback media.
2. Boot BuildFix24 with DEX enabled; enable Performance Overlay only while collecting metrics.
3. Confirm Debug Settings > 720p Composition initially reports **Reference**.
4. Never promote Optimized from this checklist alone; attach evidence/logs.

## A/B procedure
For every matrix screen in `PIKAVR_720P_TARGET_READINESS.md`:
1. Select Reference; wait for stable frame; inspect text, glyphs, borders, focus, clipping, waves/Ribbon/theme, flicker/tearing.
2. Export Debug Report.
3. Select Optimized 960x540 without reboot/rebuild; repeat the exact navigation and export Debug Report.
4. Capture photo/video where a visual difference exists.
5. Return to Reference after each suspicious result.

## Mandatory target gates
- [ ] no unacceptable tearing
- [ ] no unacceptable blur
- [ ] small text/glyphs legible
- [ ] thin borders/focus rectangle acceptable
- [ ] no severe frame-time regression
- [ ] no GS corruption/gaps/half-pixel artifacts
- [ ] Ribbon/VU1/PATH1 unchanged
- [ ] Theme HOME/SYSTEM fidelity acceptable
- [ ] reserved headroom remains present in optimized telemetry
- [ ] repeated Reference <-> Optimized switching is stable

## Game-mode / TitanEYE
- [ ] launch TitanEYE/game mode from shell
- [ ] shell resources released
- [ ] gameplay/render path correct
- [ ] return to shell succeeds
- [ ] theme restored
- [ ] 720p composition path restored
- [ ] VRAM restored/no progressive leak
- [ ] no stale shell/game frame

## 480i
- [ ] cold boot 480i
- [ ] Settings/Library/Applications readable
- [ ] game launch/return works
- [ ] changing DEX validation preference does not rebuild/corrupt active 480i

## Result recording
Record console model, cable/output device, build hash, selected path, debug report, photos/video, and PASS/FAIL per gate. Only after real evidence may `PS2 HARDWARE VALIDATED` be set.
