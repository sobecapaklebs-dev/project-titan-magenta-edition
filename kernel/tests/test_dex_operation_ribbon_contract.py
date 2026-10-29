#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'src/services/titan_diagnostics.h').read_text()
diag = (ROOT / 'src/services/titan_diagnostics.c').read_text()
scene = (ROOT / 'src/ui/titan_shell_scene.c').read_text()
assets = (ROOT / 'src/services/titan_asset_service.c').read_text()

checks = {
    'generic-status-api': all(x in header for x in (
        'TitanDiagnostics_ReportOperation', 'TitanDiagnostics_ClearOperation',
        'TitanDiagnostics_GetOperationStatus')),
    'bounded-stage-fields': all(x in header for x in (
        'TITAN_DIAG_OPERATION_AREA_CAPACITY',
        'TITAN_DIAG_OPERATION_STAGE_CAPACITY',
        'TITAN_DIAG_OPERATION_MESSAGE_CAPACITY')),
    'thread-safe-status-copy': 'diag_operation_lock();' in diag and
        'diag_operation_unlock();' in diag and 'memcpy(status, &g_diag.operation_status' in diag,
    'error-persists-until-replaced': 'g_diag.operation_status.level != TITAN_DIAG_ERROR' in diag,
    'header-ribbon-matches-dex-mode': 'static void TitanShellScene_DrawDiagnosticsOperationStatus' in scene and
        'if (!TitanDex_IsEnabled()' in scene,
    'not-gated-by-large-dex-panel': 'TitanDex_IsOverlayEnabled()' not in
        scene[scene.index('static void TitanShellScene_DrawDiagnosticsOperationStatus'):
              scene.index('static void TitanShellScene_DrawHeader')],
    'ribbon-near-clock': 'box_x = (float)TITAN_UI_WIDTH - 396.0f' in scene and
        'TitanShellScene_DrawDiagnosticsOperationStatus(frame, alpha);' in scene,
    'progress-bar-visible': 'status.progress_percent >= 0' in scene and 'progress_width' in scene,
    'assets-report-find-read-decode-ready': all(x in assets for x in (
        '"PROBE"', '"FOUND"', '"READ"', '"DECODE"', '"READY"', '"DECODE ERROR"')),
}

failed = []
for name, passed in checks.items():
    print(f'dex-operation-ribbon: {"PASS" if passed else "FAIL"} {name}')
    if not passed:
        failed.append(name)
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print(f'dex-operation-ribbon: PASS {len(checks)}/{len(checks)}')
