#!/usr/bin/env python3
from pathlib import Path

root=Path(__file__).resolve().parents[1]
installer=(root/'src/installer/titan_installer.c').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('TITAN_INSTALLER_IO_SLICE' in installer, 'bounded installer I/O slice missing')
for phase in (
    'TITAN_INSTALL_JOB_RECOVER',
    'TITAN_INSTALL_JOB_SKIP_PREFIX',
    'TITAN_INSTALL_JOB_READ_SOURCE',
    'TITAN_INSTALL_JOB_WRITE_STAGE',
    'TITAN_INSTALL_JOB_PUBLISH_ENTRY',
    'TITAN_INSTALL_JOB_ROLLBACK',
):
    req(phase in installer, 'cooperative phase missing: ' + phase)

req('static int install_job_step' in installer, 'cooperative step function missing')
req('install_payload(&s)' not in installer, 'legacy blocking install_payload still called')
req('render(&s);' in installer[installer.index('for (;;)'):], 'render loop missing during installation')
req('job->work_done += (unsigned int)io_result;' in installer, 'byte-granular progress missing')
req('install_job_update_progress(s);' in installer, 'streaming progress update missing')
req('job->pending_written' in installer, 'partial-write continuation missing')
req('job->cancel_requested' in installer and 'TITAN_INPUT_BUTTON_CIRCLE' in installer, 'cooperative cancellation hook missing')
req(
    'install_result = install_job_step(&s);' in installer or
    'install_worker_poll(&s)' in installer or
    'install_job_pump_main_thread(&s)' in installer,
    'installer transaction is not cooperatively advanced',
)
print('installer-cooperative-streaming-v2.0.6-baseline: OK')
