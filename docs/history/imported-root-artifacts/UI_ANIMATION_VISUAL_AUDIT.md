# UI Animation Visual Audit — BuildFix23

## CP05 — transition compositor / ghosting

- Formal transition phases: ENTER_BEGIN, ENTER_PROGRESS, ENTER_END, EXIT_BEGIN, EXIT_PROGRESS, EXIT_END.
- Source/destination transforms are pure functions of base state + scalar progress through `TitanUiTransition_Sample`.
- No transition transform is accumulated from the previous rendered frame.
- Frame ownership is explicit: the new draw target is cleared before shell source, destination, overlays, or notifications compose.
- Existing `TitanVideo_ResetTransform()` boundaries remain in place after source and destination transforms.
- Existing complementary HOME/SYSTEM theme crossfade contract remains valid.
- Double-buffer reference 480i/720p presentation remains unchanged.
- Optimized 720p remains opt-in and is not promoted by this checkpoint.

### Ghosting root-cause hardening
The shell previously depended on every composition path fully covering the draw framebuffer. Alpha transitions, early-return app/startup paths, and partial destination composition made that ownership implicit. CP05 removes that stale-pixel dependency by clearing the active draw target at frame begin, then composing background/context -> source transition -> destination transition -> permitted overlays -> present.

Host contracts prove ordering and deterministic transform derivation. Perceived motion and target GS timing still require hardware/PCSX2 validation.

## CP06 — focus, text bounds, safe area, collision
- Shared focus interpolation/unique-target resolver added for major shell focus lanes.
- Host rapid-input burst model covers Settings, Library, Applications, TOP, Folders and submenus and asserts exactly one bounded focus target.
- Formal text-bounds structure contains measured width/height, max width, max lines, clip rect, ellipsis and padding.
- Fixed 64-entry metrics cache removes repeated identical measurements from text-fitting hot paths.
- DEX overlay now uses four legal anchors plus header/footer/mandatory-content exclusion regions instead of a hard-coded position assumption.
- Host collision primitive and required semantic categories are contracted; artwork/background are intentionally excluded.

## CP07 — Applications / Library / Package Installer / DEX
- Package Installer keeps the approved row visual but uses four 78px chrome-safe visible rows; this removes focus-outline overlap while remaining above the help/footer region.
- Package filenames and metadata retain measured max-width + ellipsis and now use the CP06 metrics cache.
- Applications/Library keep existing approved layouts; bounds contract verifies bounded text/focus lanes and DEX safe-anchor integration.
- DEX overlay continues through CP06 exclusion-region placement rather than fixed collision-prone coordinates.
