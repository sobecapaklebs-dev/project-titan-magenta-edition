from pathlib import Path
import sys
k=Path(sys.argv[1]); p=(k/'src/graphics/titan_pikavr.c').read_text(); g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
assert 'uint64_t used' in p and 'unavailable < g_pikavr.telemetry.vram_total' in p
assert 'sat_add' in g and 'physical_free>t.vram_reserved' in g
print('titan-vram-accounting-overflow-contract: PASS')
