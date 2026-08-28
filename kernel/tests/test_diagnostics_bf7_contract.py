from pathlib import Path
r=Path(__file__).resolve().parents[1]
diag=(r/'src/services/titan_diagnostics.c').read_text()
h=(r/'src/services/titan_diagnostics.h').read_text()
k=(r/'src/core/titan_kernel.c').read_text()
s=(r/'src/services/titan_storage_scheduler.c').read_text()
ui=(r/'src/ui/titan_shell_scene.c').read_text()
mk=(r/'Makefile').read_text()
assert 'src/services/titan_diagnostics.o' in mk
assert 'TITAN_DIAG_EVENT_CAPACITY 2048u' in diag
assert 'TitanDiagnostics_PhaseBegin' in h and 'phase_services_us' in h
assert 'TitanDiagnostics_BeginFrame(context.frame_count)' in k
assert 'TITAN_DIAG_PHASE_INPUT' in k and 'TITAN_DIAG_PHASE_AUDIO' in k and 'TITAN_DIAG_PHASE_SERVICES' in k and 'TITAN_DIAG_PHASE_UI' in k and 'TITAN_DIAG_PHASE_RENDER' in k
assert 'TitanDiagnostics_StorageWorkerPump' in s
assert 'TITAN_DIAG_SPIKE_US' in diag and 'TITAN_DIAG_WATCHDOG' in diag
assert 'titan-current.bin' in diag and 'titan-last.txt' in diag
assert 'Export Full Diagnostics' in ui
assert 'FPS %u.%02u' in ui and 'P95 %ums' in ui
print('diagnostics-bf7-contract: PASS')
