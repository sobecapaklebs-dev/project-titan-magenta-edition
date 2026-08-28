from pathlib import Path
root = Path(__file__).resolve().parents[1]
diag = (root/'src/services/titan_diagnostics.c').read_text()
hdr = (root/'src/services/titan_diagnostics.h').read_text()
ui = (root/'src/ui/titan_shell_scene.c').read_text()
assert 'TITAN_DIAG_PREVIOUS_PATH' in diag
assert 'titan-previous.bin' in diag
assert 'titan-previous-5.bin' in diag
assert 'diag_rotate_previous_sessions' in diag
assert 'TitanStorageIo_Rename(TITAN_DIAG_PATH, TITAN_DIAG_PREVIOUS_PATH)' in diag
assert 'TITAN_DIAG_DISK_VERSION 2u' in diag
assert 'TITAN_DIAG_FLAG_CLEAN_SHUTDOWN' in diag
assert 'TitanDiagnostics_MarkCleanShutdown' in hdr
assert 'TitanDiagnostics_ExportPreviousTextNow' in hdr
assert 'UNCLEAN_OR_RESET' in diag
assert 'CLEAN_SHUTDOWN' in diag
assert 'Export Previous Boot' in ui
assert 'titan-previous.txt' in ui
assert '#define TITAN_DEBUG_SETTINGS_ROW_COUNT 11' in ui
assert ui.count('TitanDiagnostics_MarkCleanShutdown();') >= 2
print('diagnostics-bf8-persistence: PASS')
