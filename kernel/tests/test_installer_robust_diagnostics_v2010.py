#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(errors='replace')
diag = (root / 'src/installer/titan_installer_diag.c').read_text(errors='replace')
diag_h = (root / 'src/installer/titan_installer_diag.h').read_text(errors='replace')
makefile = (root / 'Makefile').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.10-poc"' in installer, 'v2.0.10 version missing')
req('src/installer/titan_installer_diag.o' in makefile, 'diagnostic module not linked')
req('TITAN_INSTALLER_DIAG_BUFFER_SIZE (128u * 1024u)' in diag, '128 KiB EE RAM ring missing')
req('INSTALLER.LOG' in diag and 'INSTALLER.PREV.LOG' in diag, 'persistent primary/previous logs missing')
req('mc%d:/TITAN/INSTALLER.LOG' in diag, 'Memory Card fallback log missing')
req('WAVE_STUTTER' in diag, 'wave stutter event missing')
for cause in (
    'LIKELY_MISSED_VSYNC_AFTER_IO',
    'SYNC_IO_BLOCKED_MAIN_EE_THREAD',
    'SLOW_FILESYSTEM_RPC',
    'CRC_OR_CPU_WORK_BLOCKED_MAIN_EE_THREAD',
    'INSTALL_METADATA_OVERRAN_BUDGET',
    'RENDER_OR_VSYNC_WAIT',
    'INPUT_OR_IOP_SERVICE_STALL',
    'UNATTRIBUTED_MAIN_THREAD_GAP',
):
    req(cause in diag, 'missing stutter cause: ' + cause)
req('wave_effect=no_frame_rendered_for_%llums' in diag, 'wave visual consequence missing')
req('PERF_WINDOW' in diag and 'io_kib_s=' in diag, 'periodic performance summary missing')
req('SLOW_IO' in diag and 'IO_FAILURE' in diag, 'filesystem latency/error events missing')
req('SLOW_CPU_WORK' in diag and 'TitanInstallerDiag_RecordCpuWork' in installer, 'CRC/CPU timing diagnostics missing')
req('SOURCE_CRC_MISMATCH' in installer, 'source CRC diagnostics missing')
req('STAGING_VERIFY_MISMATCH' in installer, 'staging CRC diagnostics missing')
req('BACKUP_ALLOCATED' in installer and 'ENTRY_PUBLISHED' in installer, 'transaction path diagnostics missing')
req('ROLLBACK_BEGIN' in installer and 'INSTALL_TRANSACTION_FAILED' in installer, 'rollback/error diagnostics missing')
req('TitanInstallerDiag_BeginFrame' in installer, 'frame instrumentation not started')
req('TitanInstallerDiag_EndFrame' in installer, 'frame instrumentation not completed')
req('install_elapsed_ms' in installer and 'render_elapsed_ms' in installer and 'input_elapsed_ms' in installer, 'frame component timings missing')
for op in ('READ_TINS', 'WRITE_STAGE', 'READ_STAGE_VERIFY', 'RENAME_POSIX', 'RENAME_FILEXIO', 'REMOVE_POSIX', 'REMOVE_FILEXIO'):
    req(op in installer, 'I/O instrumentation missing: ' + op)
# Ensure per-frame diagnostics stay RAM-only. Final/checkpoint persistence must
# not be called from the frame-end logger itself.
endframe = diag.split('void TitanInstallerDiag_EndFrame', 1)[1].split('int TitanInstallerDiag_FlushCheckpoint', 1)[0]
req('open(' not in endframe and 'write(' not in endframe, 'frame logger performs persistent filesystem I/O')
req('TitanInstallerDiag_FlushCheckpoint("install-start")' in installer, 'pre-install checkpoint missing')
req('TitanInstallerDiag_FlushFinal' in installer, 'final log flush missing')
req('install_job_pump_main_thread' in installer and 'installer_io_worker' not in installer, 'single-owner hardware-safe I/O policy regressed')
print('installer-robust-diagnostics-v2.0.10: OK')
