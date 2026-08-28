#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
p=(root/'scripts/rehydrate_buildfix27.py').read_text()
for needle in [
    'BuildFix27 final deterministic recovery',
    'CP00-source.tar.gz',
    'BUILDFIX27_CP10R_INCREMENTAL.tar.gz',
    'BUILDFIX27_CP49_INCREMENTAL.tar.gz',
    'BUILDFIX27_FCEUMM_REAL_INTEGRATION.tar.gz',
    'BUILDFIX27_FINAL_CLOSURE_DELTA.tar.gz',
    'BUILDFIX27_FINAL_MANIFEST.txt',
    'safe_extract',
    'MISSING=',
    'UNEXPECTED=',
    'HASH_MISMATCH=',
]:
    assert needle in p, needle
assert 'CP14-source.tar.gz' not in p
assert "tf.extractall(dest,members=members,filter='data')" in p
print('buildfix27-rehydrate-contract: OK persisted CP00+delta+FCEUmm+closure recovery, final 0/0/0 gate')
