from pathlib import Path
import sys
k=Path(sys.argv[1]); h=(k/'src/graphics/titan_pikavr.h').read_text(); c=(k/'src/graphics/titan_pikavr.c').read_text(); g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
assert 'palettized_bytes_saved' in h and 'dedupe_bytes_saved' in h
assert 'dedupe_saved +=' in c and 'palettized_saved +=' in c
assert 'vram_saved = TitanPikaVr_SaturateU32(saved)' in c
assert 't.palettized_bytes_saved' in g and 't.dedupe_bytes_saved' in g
print('titan-pikavr-startup-accounting-contract: PASS')
