# Project Titan RT1 BuildFix13 — Asset Download UX / Semantic Art Fix

BF13 closes the hardware feedback from BF12 without changing Network Core, bootloader, themes themselves, game launch, installer, PUP or VMC behavior.

## Asset model

Titan intentionally consumes only two downloaded game-art classes:

- `ICON`: OPL-style `_ICO.png`, cached as 128x128 GS PSM16 TIMG.
- `BACKGROUND`: `_BG_00.png`, then `_SCR_00.png`, cached as 256x128 GS PSM16 TIMG.

`_COV`, `_COV2` and cover-JPEG fallbacks are not valid Titan icon/background providers. TIMG advances to v2 so BF12 cache entries generated from the old mapping fail validation and are naturally replaced.

## No-stall scheduling

Manual phases remain globally icons-first, then backgrounds. Within a phase, the first pass still follows selected -> visible -> remaining catalog. A transient failure/retry is requeued through a per-kind round-robin cursor, so one game cannot repeatedly block later games. HTTP/TLS payload I/O waits on short `select()` windows and re-checks cancellation/deadline between chunks. DNS results are cached per provider host for the runtime session.

## Download telemetry / recovery

The native HTTP client publishes current payload bytes. Asset Service derives current-transfer percentage and KiB/s and exposes it to Notifications. Notifications > Downloads shows overall asset progress plus current-transfer progress; the top ticker includes speed when available. BF12 retry, queued-start, cache-truth resume, catalog rebind and guarded worker restart remain enabled.

## UI corrections

- Library/download progress bars are positioned below notification text instead of crossing it.
- The shell no longer draws the default red separator in `DrawPanelBase`; header chrome belongs to the active theme.
- A valid 128x128 ICO fills the game tile. The no-art placeholder keeps its original compact Home size.
- BF12 DEX IP synchronization remains intact.

## Preservation

- `kernel/src/services/titan_network.c`: unchanged from BF12.
- `bootloader/`: unchanged from the frozen BF6 baseline.
