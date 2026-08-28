#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def text(rel):
    return (root / rel).read_text(encoding='utf-8', errors='replace')

def req(ok, msg):
    if not ok:
        raise SystemExit('buildfix27-project-nes-storage-save-contract: FAIL: ' + msg)

core_h = text('apps/project-nes/src/project_nes_core.h')
app = text('apps/project-nes/src/project_nes.c')
life = text('apps/project-nes/tests/host_lifecycle_test.c')
save_test = text('apps/project-nes/tests/host_save_test.c')
runtime = text('kernel/src/services/titan_app_runtime.c')
make = text('apps/project-nes/Makefile')

for symbol in ('save_ram_size', 'save_ram_export', 'save_ram_import',
               'save_ram_dirty', 'save_ram_clear_dirty'):
    req(symbol in core_h, f'core boundary missing {symbol}')
req('TitanAppApiV1' not in core_h and 'titan_app_sdk.h' not in core_h,
    'core save boundary depends on Titan App API')

req('battery_backed' in app and '(flags6 & 2u)' in app,
    'iNES battery-backed SRAM flag is not tracked')
req('prg_ram_dirty' in app and 'if (n->battery_backed) n->prg_ram_dirty = 1u;' in app,
    'battery PRG RAM writes do not mark persistence dirty')
req('data/saves/' in app and '.srm' in app,
    'frontend does not derive a durable per-ROM save path')
req('pn_load_save' in app and 'pn_flush_save' in app,
    'frontend save load/flush lifecycle missing')
req('g_pn.api->file_write_data(g_pn.save_path' in app,
    'save persistence bypasses the app sandbox transactional file API')
req('storage_write(' not in app and 'storage_commit_replace(' not in app,
    'Project NES owns trusted VFS publication instead of the sandbox API')
req('PN_SAVE_FLUSH_FRAMES' in app and 'pn_save_tick' in app,
    'bounded periodic SRAM flush missing')

shutdown = app[app.index('void titan_app_shutdown(void)'):]
req('pn_flush_save(1)' in shutdown,
    'shutdown does not attempt a final dirty SRAM flush')
req(app.count('pn_flush_save(1)') >= 3,
    'pause/reset/shutdown save lifecycle does not have explicit flush points')
req('TitanStorageIo_AtomicReplace(temporary, path, backup)' in runtime,
    'Titan file_write_data lost atomic-replace publication semantics')

req('battery-backed iNES fixture' in save_test and '8192u' in save_test,
    'ROM-free battery SRAM host fixture missing')
req('data/saves/' in life and 'titan_app_shutdown();' in life and
    'g_save_writes' in life and 'g_save_size' in life and 'g_save_first' in life,
    'frontend lifecycle test does not verify shutdown SRAM publication')
req('host_save_test' in make,
    'Project NES host-test target does not execute SRAM fixture')

print('buildfix27-project-nes-storage-save-contract: OK durable SRAM boundary/lifecycle')
