#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
app=(root/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text(errors='replace')
runner=(root/'kernel/tests/run_host_tests.sh').read_text(errors='replace')
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-wlaunchelf-recursive: FAIL: '+msg)
req('WLE_RECURSIVE_DEPTH 8u' in app and 'WleRecursiveFrame' in app, 'bounded iterative DFS state missing')
req('wl_recursive_step' in app and 'storage_list(frame->source, frame->next_index, &entry, 1u)' in app, 'cooperative one-entry traversal missing')
req('recursive_delete_phase' in app and 'wl_recursive_begin_delete' in app, 'two-phase move deletion missing')
copy=app[app.index('static void wl_recursive_step'):app.index('static void wl_begin_paste')]
req('recursive_move && !g_wle.recursive_delete_phase' in copy, 'move does not defer delete phase until copy completion')
req('storage_remove(source)' in copy and 'storage_remove(completed_source)' in copy, 'post-copy source cleanup missing')
req('wl_path_is_descendant' in app, 'self-descendant copy guard missing')
req('merge recursivo recusado' in app or 'merge recusado' in app, 'unsafe directory merge is not rejected')
req('host_recursive_test.c' in runner, 'recursive executable fixture missing from official runner')
req('fileXio' not in app and 'gsKit' not in app and 'ExecPS2' not in app, 'platform ownership bypass introduced')
print('buildfix27-wlaunchelf-recursive: OK bounded DFS, transactional files, two-phase move')
