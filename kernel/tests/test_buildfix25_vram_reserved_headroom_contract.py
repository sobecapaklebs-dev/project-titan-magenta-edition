from pathlib import Path
import sys
k=Path(sys.argv[1]); h=(k/'src/graphics/titan_vram_boot_guard.h').read_text(); c=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
assert 'TITAN_VRAM_WARNING_HEADROOM_BYTES (512u * 1024u)' in h
assert 'TITAN_VRAM_CRITICAL_HEADROOM_BYTES (256u * 1024u)' in h
assert 'TITAN_VRAM_RECOVERY_HEADROOM_BYTES' in h
assert 'physical_free>t.vram_reserved' in c
print('titan-vram-reserved-headroom-contract: PASS')
