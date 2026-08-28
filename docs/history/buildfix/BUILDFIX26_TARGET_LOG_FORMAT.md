# BuildFix26 Target Log / Failure Signature Format

The existing Debug Report remains the primary export. BuildFix26 adds low-overhead watermarks and does not perform filesystem writes per frame.

Capture fields when available:
- active video mode and 720p composition path;
- BOOT VRAM HISTORY phases and pressure/recovery state;
- GS VRAM used/free/min-free/peak, allocations/failures/reclaims/evictions/resident resources;
- EE measurement validity, used/free/peak/min-free/allocation-failure count;
- IOP measurement validity, used/free/min-free if a real backend exists, plus known resident module count;
- Theme active/resident resources and quality/degradation state;
- TitanEYE/game-mode enter/return state;
- Storage exclusive/worker state and Network status.

`valid=0` means **NOT_MEASURED**, never zero bytes used.

Failure signatures:
- `BOOT_RECOVERY_MINIMUM`: critical pre-HOME pressure/failure latched safe mode.
- `VRAM_OPTIONAL_REJECT`: decoration/preload denied; shell should remain usable.
- `VRAM_CRITICAL_ALLOC_FAIL`: critical resource could not allocate; target behavior must be explicit, not corruption.
- `720P_ROLLBACK_OK`: requested path failed, old renderable path restored.
- `720P_DOUBLE_FAILURE`: both primary and rollback failed; renderer state is invalid and must not claim success.
- `LIFECYCLE_MONOTONIC_GROWTH`: host analyzer detected repeated growth without plateau.
- `IOP_RAM_NOT_MEASURED`: expected until a target backend supplies authoritative IOP free-memory data.
