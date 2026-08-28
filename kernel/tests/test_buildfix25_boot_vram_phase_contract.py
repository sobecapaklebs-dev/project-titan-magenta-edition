from pathlib import Path
import sys
k=Path(sys.argv[1]); h=(k/'src/graphics/titan_vram_boot_guard.h').read_text(); c=(k/'src/core/titan_kernel.c').read_text()
for x in ['VRAM_PHASE_VIDEO_INIT','VRAM_PHASE_CORE_UI','VRAM_PHASE_FONT','VRAM_PHASE_WAVES_RIBBON','VRAM_PHASE_WELCOME','VRAM_PHASE_PROFILE','VRAM_PHASE_THEME_BASE','VRAM_PHASE_HOME','VRAM_PHASE_OPTIONAL_ASSETS']:
 assert x in h
assert 'TitanVramBootGuard_Initialize' in c and 'TitanVramBootGuard_EnterPhase' in c
print('titan-boot-vram-phase-contract: PASS')
