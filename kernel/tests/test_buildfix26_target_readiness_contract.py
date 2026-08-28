#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
script=(root/'tools/buildfix26_target_build.sh').read_text()
report=(root/'BUILDFIX26_TARGET_BUILD_REPORT.md').read_text()
check=(root/'BUILDFIX26_HARDWARE_TEST_CHECKLIST.md').read_text()
log=(root/'BUILDFIX26_TARGET_LOG_FORMAT.md').read_text()
assert 'mips64r5900el-ps2-elf-' in script
assert 'dvp-as' in script and 'kernel.elf' in script
assert 'STATUS=BLOCKED' in script
assert 'readelf' in script and 'undefined' in script
assert 'BLOCKED / NOT_EXECUTED' in report
assert 'TARGET_BUILD_VALIDATED' in report
assert 'Reference' in check and 'Optimized 960x540' in check
assert '10 lifecycle Debug Reports' in check
assert 'valid=0' in log and 'NOT_MEASURED' in log
assert '720P_DOUBLE_FAILURE' in log
print('buildfix26-target-readiness-contract: OK')
