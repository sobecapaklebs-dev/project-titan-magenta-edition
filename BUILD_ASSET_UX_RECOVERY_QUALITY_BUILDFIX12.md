# Project Titan RT1 BuildFix12 — Asset UX, Recovery & Quality

BF12 closes the downloaded-artwork front before the shell-fluidity optimization pass.

## Download/recovery contract
- A manual request is never rejected merely because a previous network/storage tail is finishing; it is queued and starts when the pipeline becomes safe.
- Library catalog commits no longer cancel a manual asset session. The session rebinds to the new catalog through startup hashes and preserves identities already completed.
- Manual transient network/cache failures retry up to three attempts with a short cooperative delay. Permanent provider misses are final for that pass.
- A guarded Asset Network Worker failure may recreate the isolated worker and resume the current manual session from already-published cache state.
- Cache clear can also wait behind a live asset tail instead of reporting a false generic busy state.
- Manual and automatic ordering are icons-first. Within each class the selected/visible games precede the rest of the catalog.

## Artwork quality / tile layering
- Icon TIMG changes from 32x32 to 128x128 GS PSM16. Existing 32x32 TIMGs fail exact-size validation and are redownloaded automatically.
- Remote icon sources below 128x128 and landscape/background sources below 256x128 are rejected instead of being stretched into visibly blurred art.
- Game tiles are filled by downloaded artwork. Every Home game tile streams the resident 128x128 image through the existing scratch texture with no new permanent GS VRAM reservation.
- The fast 256x64 transient atlas remains available for Library/fallback views; each 32x32 cell is box-filtered from the 128x128 resident icon rather than being sourced from a 32x32 download.
- DEX/Title ID metadata is rendered after game art and before the focus frame: physically at the tile bottom, above the artwork in z-order.

## Notifications / DEX
- Asset downloads have their own background-download activity instead of sharing the Library activity slot.
- The top-menu notification ticker shows live asset progress and current game/stage; the Notifications > Downloads row shows processed/total/progress and final state.
- Interrupted/partial sessions publish a resumable warning; successful sessions publish completion.
- The DEX overlay IP is synchronized every service frame from Titan Network Core and shows Offline when the network is not ONLINE.

## Preserved boundaries
- `kernel/src/services/titan_network.c` is byte-for-byte unchanged from BF11/BF8.
- `bootloader/` is byte-for-byte unchanged from the frozen BF6 baseline.
- No theme, installer, PUP, VMC or game-launch behavior was changed.
