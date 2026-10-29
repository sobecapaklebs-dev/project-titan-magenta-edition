#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
script=(root/'tools/buildfix26_target_build.sh').read_text()
assert 'mips64r5900el-ps2-elf-' in script
assert 'dvp-as' in script and 'kernel.elf' in script
assert 'STATUS=BLOCKED' in script
assert 'readelf' in script and 'undefined' in script
print('buildfix26-target-readiness-contract: OK')
