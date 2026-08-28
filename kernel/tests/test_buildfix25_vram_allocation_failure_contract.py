from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video_memory.c').read_text(); g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
assert 'TitanVramBootGuard_AllowAllocation(name, bytes)' in c
assert c.count('TitanVramBootGuard_RecordAllocation(name, bytes, 0)') >= 4
assert 'allocation_failures_critical' in (k/'src/graphics/titan_vram_boot_guard.h').read_text()
assert 'theme")||strstr(n,"decor")' in g
print('titan-vram-allocation-failure-contract: PASS')
