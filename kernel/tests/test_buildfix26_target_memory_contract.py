from pathlib import Path
import sys
k=Path(sys.argv[1])
h=(k/'src/services/titan_target_memory.h').read_text()
c=(k/'src/services/titan_target_memory.c').read_text()
g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
s=(k/'src/ui/titan_shell_scene.c').read_text()
req=['ee_peak_used','ee_min_free','ee_allocation_failures','iop_ram_valid','iop_modules_known_resident','TitanTargetMemory_Capture','BOOT VRAM HISTORY','ee_valid=%u','iop_valid=%u']
for x in req:
    assert x in h+c+g+s, x
assert '#if defined(__EE__) || defined(_EE)' in c
assert 'sbrk(0)' in c
assert 'iop_ram_valid = 1u' not in c.split('#ifdef TITAN_TARGET_MEMORY_TEST')[0], 'do not fabricate IOP RAM validity'
print('titan-target-memory-contract: PASS')
