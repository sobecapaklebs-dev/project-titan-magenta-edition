from pathlib import Path
import sys
k=Path(sys.argv[1]); h=(k/'src/graphics/titan_vram_boot_guard.h').read_text(); v=(k/'src/graphics/titan_video_memory.c').read_text(); t=(k/'tests/test_buildfix25_vram_fault_injection.c').read_text()
assert 'TITAN_VRAM_FAULT_INJECTION' in h and 'SetFaultInjection' in h
assert 'TitanVramBootGuard_ShouldInjectFailure(bytes)' in v
for x in ['theme-artwork','font-atlas','framebuffer','fail_after_n']:
 assert x in t or x in (k/'src/graphics/titan_vram_boot_guard.c').read_text()
print('titan-vram-fault-injection-contract: PASS')
