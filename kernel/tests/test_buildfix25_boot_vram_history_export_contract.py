from pathlib import Path
import sys
k=Path(sys.argv[1]); s=(k/'src/ui/titan_shell_scene.c').read_text(); c=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
assert 'BOOT VRAM HISTORY' in s
assert 'TitanVramBootGuard_GetHistoryCount' in s
assert 'phase==g_guard.phase' in c
assert 'TITAN_VRAM_PHASE_PROFILE' in s and 'TITAN_VRAM_PHASE_HOME' in s
print('titan-boot-vram-history-export-contract: PASS')
