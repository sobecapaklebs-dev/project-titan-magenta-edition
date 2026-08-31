# TitanEYE/Ribbon BuildFix25 — Hardware Test Checklist

## Before flashing/running
- Keep the known-functional bootloader/kernel/media available for rollback.
- Build this exact source with PS2SDK/R5900 and preserve compiler/link logs and SHA-256 of the ELF/package.
- Confirm the target build identifies the TitanEYE/Ribbon BuildFix25 lineage, not any historical BuildFix25.
- Confirm Reference is the 720p release default before boot.

## PHASE A — COLD BOOT (Reference only)
1. Boot BuildFix25 without touching the optimized selector.
2. Confirm video output remains stable.
3. Confirm Welcome renders.
4. Confirm Profile Selection renders.
5. Confirm HOME is reached.
6. Export the first diagnostics report.
7. Inspect `BOOT VRAM HISTORY`: VIDEO_INIT, FONT, CORE_UI, WAVES_RIBBON, THEME_BASE, WELCOME, PROFILE/HOME/OPTIONAL_ASSETS as reached.
8. Confirm no giant `vram_free` underflow value and no allocation-failure counter growth during normal boot.
9. Confirm font/text remains present before optional artwork.
10. Record any WARNING/CRITICAL transition and the phase that caused it.

## PHASE B — SHELL
11. Open Settings.
12. Open Library.
13. Open Applications.
14. Open Theme selector.
15. Open Package Installer.
16. Check DEX overlay/safe-area and telemetry.
17. Exercise a heavy theme if available. If pressure occurs, verify decoration degrades before UI/font/video.
18. Navigate repeatedly to ensure no pressure/reload oscillation.

## PHASE C — TITANEYE/GAME MODE
19. Launch TitanEYE using the already validated game content path.
20. Confirm game-mode entry.
21. Return to shell.
22. Confirm Reference composition restored.
23. Confirm Theme Context restored.
24. Export diagnostics and compare VRAM before launch vs after return for leaked residency.

## PHASE D — OPTIMIZED VALIDATION (only after A-C pass)
25. Enter DEX -> Graphics -> 720p Composition.
26. Switch Reference -> Optimized 960x540.
27. Check tearing, font sharpness, small glyphs, borders/focus, Ribbon, theme layers, clipping and safe-area.
28. Export A/B telemetry.
29. Switch Optimized -> Reference and confirm immediate renderability.
30. Repeat Reference <-> Optimized several times.
31. If a path switch fails, confirm successful rollback leaves previous path active and shell drawable.
32. Never interpret a double failure as success; capture the diagnostic state.

## Evidence to return
- exact ELF/package SHA-256;
- compiler/linker output and section sizes;
- first boot diagnostics report;
- Reference screenshots/photo/video;
- Optimized screenshots/photo/video only after safe phases pass;
- A/B telemetry exports;
- any crash/black-screen stage and last BOOT VRAM HISTORY snapshot.

Validation labels after test must be explicit: R5900 CROSS-BUILD VALIDATED and PS2 HARDWARE VALIDATED only if actually achieved.
