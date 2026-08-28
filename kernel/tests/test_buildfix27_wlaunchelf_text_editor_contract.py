#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
app = (root/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text(errors='replace')
runner = (root/'kernel/tests/run_host_tests.sh').read_text(errors='replace')

def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-wlaunchelf-text-editor: FAIL: '+msg)
req('WLE_TEXT_VIEW_CAPACITY (8u * 1024u)' in app, 'bounded 8KiB viewer missing')
req('WLE_TEXT_EDIT_CAPACITY 255u' in app, 'shared Text Input bound missing')
req('wl_suffix_text' in app and 'wl_text_payload_safe' in app and 'wl_open_text_viewer' in app, 'text viewer gate missing')
req('api->alloc(WLE_TEXT_VIEW_CAPACITY)' in app and 'api->release(g_wle.viewer_buffer)' in app, 'viewer allocation lifecycle not Titan-owned')
req('text_input_open("Editar texto"' in app and 'WLE_TEXT_EDIT_FILE' in app, 'shared Text Input editor path missing')
req('wl_publish_text_edit' in app and '".titantmp"' in app and 'storage_commit_replace' in app, 'atomic edit publication missing')
segment=app[app.index('static int wl_publish_text_edit'):app.index('static int wl_name_safe')]
req('storage_write(temporary' in segment, 'edit does not write temporary file')
req('storage_write(g_wle.viewer_path' not in segment, 'edit writes final path directly')
req('fileXio' not in app and 'gsKit' not in app and 'ExecPS2' not in app and 'audsrv' not in app, 'native ownership bypass introduced')
req('wl_draw_text_viewer' in app and 'draw_text' in app, 'Titan-rendered text viewer missing')
req('test_buildfix27_wlaunchelf_text_editor_contract.py' in runner, 'official runner gate missing')
print('buildfix27-wlaunchelf-text-editor: OK bounded viewer, shared input, atomic publication')
