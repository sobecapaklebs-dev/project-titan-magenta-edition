# BuildFix27 Checkpoint CP11
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP10R
BLOCK: Project NES Storage/Saves + lifecycle

- Added optional provider-owned save RAM import/export/dirty callbacks; core boundary remains Titan-API-free.
- Legacy NROM fallback recognizes iNES battery-backed PRG RAM and exposes its 8 KiB save region.
- Frontend derives deterministic per-ROM `data/saves/*.srm` sandbox paths.
- Save restore occurs before provider reset; dirty SRAM flushes periodically and on pause/reset/shutdown.
- Persistence uses existing Titan `file_write_data` transactional publication; dirty state clears only after complete successful write.
- No direct FileXio/trusted VFS ownership added to Project NES.
- ROM-free battery SRAM and shutdown-persistence host fixtures pass.
- FCEUmm provider remains NOT_MATERIALIZED; no unsupported mapper/core claim introduced.

Contracts:
- buildfix27-project-nes-storage-save-contract: PASS
- buildfix27-cp10-recovery-contract: PASS
- strict host lifecycle/save compilation (`-Wall -Wextra -Werror`): PASS
- Project NES host core/audio/lifecycle/save suite: PASS

NEXT: CP12 ROM-free mapper/region/SRAM/audio conformance + provider readiness.
