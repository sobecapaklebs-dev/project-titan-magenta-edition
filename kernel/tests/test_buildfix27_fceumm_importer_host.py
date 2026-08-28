#!/usr/bin/env python3
import importlib.util, io, tarfile, tempfile
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
spec=importlib.util.spec_from_file_location('impf',root/'scripts/import_fceumm_sources.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
assert m.git_blob_sha(b'hello\n') == 'ce013625030ba8dba906f756967f9e9ca394464a'
with tempfile.TemporaryDirectory() as td:
    bad=Path(td)/'bad.tar.gz'
    with tarfile.open(bad,'w:gz') as tf:
        ti=tarfile.TarInfo('../escape'); data=b'x'; ti.size=len(data); tf.addfile(ti,io.BytesIO(data))
    try: m.verify_archive(bad,'modern')
    except ValueError: pass
    else: raise AssertionError('path traversal archive accepted')
    wrong=Path(td)/'wrong.tar.gz'
    rootname='libretro-fceumm-'+m.PROFILES['modern']['commit']
    with tarfile.open(wrong,'w:gz') as tf:
        for rel in m.PROFILES['modern']['critical']:
            data=b'not-the-frozen-blob\n'; ti=tarfile.TarInfo(rootname+'/'+rel); ti.size=len(data); tf.addfile(ti,io.BytesIO(data))
    try: m.verify_archive(wrong,'modern')
    except ValueError as e: assert 'mismatch' in str(e) or 'GPL' in str(e)
    else: raise AssertionError('wrong frozen snapshot accepted')
print('buildfix27-fceumm-importer-host: OK frozen pins/blob verification/archive safety')
