# Project Titan RT1 BuildFix14 — Asset Provider, Recovery & Full-Frame Fix

BF14 closes the hardware issues exposed by the BF13 asset run.

## Asset semantics and provider coverage
- Titan game assets remain exactly two classes: `ICON` and `BACKGROUND`. Covers are never used as tile icons.
- ICON provider order is square PSBBN game art by OPL/Game ID, then OSD-XMB `ICON0.PNG`. Both feed the 128x128 PSM16 tile cache.
- BACKGROUND provider order remains Luden02 `_BG_00.png`, Luden02 `_SCR_00.png`, then OSD-XMB `PIC2.PNG`.
- HTTP 404/410 across all providers is `UNAVAILABLE`, not a downloader error. It advances session progress and leaves the original placeholder.
- True transport/decode/cache failures remain errors and use bounded retries.

## Anti-stall recovery
- Manual phase download selection remains round-robin. A failed/timed-out title returns to the phase tail.
- A 20-second no-progress watchdog can abort one stuck HTTP transfer without cancelling the manual session. The item is requeued and later retried.
- Orphaned storage/download staging is recovered fail-closed; durable published TIMGs remain the source of truth.
- HTTP receive progress refreshes the pipeline heartbeat, so slow but active downloads are not treated as stalls.

## Notifications
- Session progress counts downloaded + unavailable + true failures as processed.
- `Sem arte` is displayed separately from `Erros`. Missing art is not presented as a network failure.
- Library and asset progress rails are 1-pixel inset rails. Overall/current-transfer rails share one baseline and cannot intersect the selected-card bottom outline.

## 720p full-frame mapping
- The authored 854x448 shell is mapped independently on X/Y to the complete 1280x720 active framebuffer.
- The old uniform 720p mapping that left unused top/bottom scanout bands is removed.

## Preservation
- Downloaded icon art fills game tiles; placeholder remains at the original small icon size.
- DEX/Title ID strip remains above artwork in z-order.
- DEX overlay IP remains sourced from Titan Network Core.
- `titan_network.c`, AR9271 IOP path and frozen BF6 bootloader are unchanged.
