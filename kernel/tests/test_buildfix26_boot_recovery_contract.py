from pathlib import Path
import sys
k=Path(sys.argv[1])
h=(k/'src/graphics/titan_boot_recovery.h').read_text()
c=(k/'src/graphics/titan_boot_recovery.c').read_text()
g=(k/'src/graphics/titan_vram_boot_guard.c').read_text()
b=(k/'src/ui/titan_shell_background.c').read_text()
assert 'TITAN_BOOT_RECOVERY_MINIMUM' in h
assert 'home_ready' in h
assert 'TitanBootRecovery_IsMinimum()' in g
assert 'TitanBootRecovery_NotifyCriticalAllocationFailure' in g
assert 'TitanBootRecovery_MarkHomeReady' in g
assert 'TitanBootRecovery_IsMinimum() ||' in b
assert 'pressure/reload loop' in c
assert 'TitanStorageIo' not in c and 'fopen' not in c and 'printf' not in c
print('titan-boot-recovery-contract: PASS')
