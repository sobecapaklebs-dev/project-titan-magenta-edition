#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def require(cond, msg):
    if not cond:
        raise SystemExit(msg)

theme_h = (root/'src/services/titan_theme.h').read_text()
theme_c = (root/'src/services/titan_theme.c').read_text()
spline = (root/'src/ui/titan_xmb_spline.c').read_text()
notif = (root/'src/ui/titan_notifications.c').read_text()
error_h = (root/'src/ui/titan_system_error.h').read_text()
error_c = (root/'src/ui/titan_system_error.c').read_text()
runtime = (root/'src/services/titan_app_runtime.c').read_text()
makefile = (root/'Makefile').read_text()
flow_cfg = (project/'themes/ribbon-scarlet/theme.cfg').read_text()
wle_test = (project/'apps/wlaunchelf/tests/host_lifecycle_test.c').read_text()

require('TITAN_THEME_MAX_PRIMARY_WAVES 3u' in theme_h and 'TitanThemePrimaryWave' in theme_h,
        'multi-primary-wave theme ABI missing')
require('primary_wave_count' in theme_c and 'primary_wave1' not in theme_c,
        'theme parser must support generic primary-wave keys')
require('primary_wave_count=' in flow_cfg and
        'primary_wave1_start_y_milli=' in flow_cfg,
        'Scarlet Flow no longer defines a theme-owned primary wave')
require('for (primary_wave_index = 0u;' in spline and
        'primary_wave->start_y' in spline and 'primary_wave->end_y' in spline,
        'Flow renderer is not iterating theme-owned primary waves')
require('TitanSystemError_Raise' in error_h and 'Erro do sistema' in error_c,
        'dedicated system-error popup missing')
require('kind == TITAN_NOTIFICATION_ERROR' in notif and 'TitanSystemError_Raise(title, message)' in notif,
        'system errors still enter notification cards')
require('kind == TITAN_NOTIFICATION_ACTIVITY || kind == TITAN_NOTIFICATION_SUCCESS' in notif,
        'routine notification cards were not reduced')
require('complete packed runtime image' in runtime and 'module.file_data' in runtime and
        'TitanAppRuntime_ReportRejectedDraws' in runtime,
        'Titan App readable-memory/render diagnostics fix missing')
require('text_draw_calls < 3u' in wle_test and 'draw-visible' in wle_test,
        'wLaunchELF host test does not assert visible draw commands')
require('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in (root/'src/services/titan_system_update.h').read_text(),
        'system update generation missing')
require((project/'themes/ribbon-scarlet/theme.cfg').is_file() and '--build 36' in makefile,
        'Flow capability or Build36 updater metadata missing')
print('buildfix24-flow-errors-wlaunchelf-contract: OK')
