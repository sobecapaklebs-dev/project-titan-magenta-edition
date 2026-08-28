# Project Titan: Magenta Edition — BuildFix29 Final Source Status

## Frozen source-side state

BuildFix29 is source-complete and host-closed on top of the immutable BuildFix28 FINAL baseline. The final code state preserves all BuildFix28 functionality and adds the BuildFix29 platform/input/repository/download/store scope without introducing a second input core, second downloader, frame-path package scan or fake battery percentage.

### Input/platform
- Stable logical device identity remains separate from player assignment.
- First meaningful eligible input can deterministically claim Player 1 on Welcome.
- DS2 remains boot-safe fallback; PADEMU modern identity/battery telemetry is fail-open and slow-path.
- PS/Home/Guide is a Titan system event, separate from game-facing DS2 compatibility semantics.
- Native/Project NES/UL/Neutrino consume the System Button through their appropriate Titan session/overlay paths.
- Battery state and percentage validity are independent; unknown levels are not fabricated.

### Repository/download/store
- Repository Protocol v1 requires bounded parsing, PS2/R5900 target identity, package size and SHA-256.
- Download Service owns one bounded queue, storage-worker handoff, streaming SHA-256/size verification, retry/cancel/pause, structural package validation and atomic publication.
- HTTP byte-range resume is not claimed because Range support is not implemented.
- Store Foundation accepts repository catalogs, checks dependency presence and hands packages to the shared Download Service.
- Store/Download heavy resources and worker activation remain lazy when unused.

## Validation truth

Host regression is complete. R5900 target build, PCSX2 and physical hardware remain external gates and are not represented as PASS.
