from pathlib import Path
import sys
k=Path(sys.argv[1]); h=(k/'src/graphics/titan_vram_boot_guard.h').read_text(); c=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
for x in ['vram_total','vram_used','vram_free','vram_reserved','vram_framebuffers','vram_textures','vram_theme','vram_font','vram_clut','vram_transient','resident_textures','resident_theme_frames','largest_recent_allocation','allocation_failures','reclaims','evictions','palettized_bytes_saved','dedupe_bytes_saved']:
 assert x in h
assert 'TitanDiagnostics_Event' in c and 'TitanVramBootGuard_Capture' in c
print('titan-boot-vram-budget-contract: PASS')
