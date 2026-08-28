# BuildFix27 Checkpoint CP46
STATUS: HOST_VALIDATED / FCEUMM_TARGET_SOURCE_BLOCKED
PREVIOUS_VALID: CP45
BLOCK: Project NES FCEUmm/libretro Titan-side adapter

Implemented:
- link-gated FCEUmm/libretro provider adapter with provider-owned context;
- libretro callback ownership isolated from Titan APIs;
- RGB565/0RGB1555/XRGB8888 conversion into frontend-owned RGB565 target;
- NES input translation through libretro joypad callbacks;
- buffered stereo PCM capture sized for mature 48 kHz frame batches;
- libretro save-RAM export/import/dirty tracking;
- deterministic retro_unload_game + retro_deinit lifecycle before ROM release;
- frontend-neutral provider_frame_ready instead of Legacy-only frame presentation;
- Project NES host tests promoted to -Werror and adapter ASan/UBSan fixture.

Validation:
- Project NES full host suite: PASS;
- FCEUmm adapter fake-core ASan/UBSan: PASS;
- BuildFix27 closure through pfs-iop-provenance: PASS before executor timeout;
- remaining BF27 tail gates: PASS.

No real FCEUmm source/core is claimed linked. Exact frozen GPL source bytes remain externally blocked.
