from pathlib import Path
import sys
k=Path(sys.argv[1]); b=(k/'src/ui/titan_shell_background.c').read_text(); g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
block=b[b.index('BuildFix25 boot-safety'):b.index('if (TitanTheme_GetActive()->background_engine')]
assert 'TITAN_VRAM_PRESSURE_CRITICAL' in block
assert 'TitanShellBackground_DrawAtmosphere' in block
assert 'TitanXmbSpline_DrawWithOpacity' in block
assert 'DrawScene' not in block and 'DrawParticles' not in block
assert 'TITAN_VRAM_RECOVERY_HEADROOM_BYTES' in (k/'src/graphics/titan_vram_boot_guard.h').read_text()
assert 'old==TITAN_VRAM_PRESSURE_CRITICAL' in g
print('titan-vram-critical-fallback-contract: PASS')
