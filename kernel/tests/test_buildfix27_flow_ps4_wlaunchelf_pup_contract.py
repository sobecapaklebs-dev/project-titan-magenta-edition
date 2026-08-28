#!/usr/bin/env python3
from pathlib import Path
import sys
import re
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

h = (root/'src/services/titan_theme.h').read_text()
c = (root/'src/services/titan_theme.c').read_text()
spline = (root/'src/ui/titan_xmb_spline.c').read_text()
flow = (project/'themes/ribbon-scarlet/theme.cfg').read_text()
wle = (project/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text()
wlemake = (project/'apps/wlaunchelf/Makefile').read_text()
make = (root/'Makefile').read_text()
update = (root/'src/services/titan_system_update.h').read_text()

req('control1_y' in h and 'control2_y' in h and 'drift_y' in h and 'thickness_scale' in h,
    'theme-owned curved primary-wave controls missing')
req('control1_y_milli' in c and 'control2_y_milli' in c and 'drift_y_milli' in c and 'thickness_milli' in c,
    'theme parser does not expose curved Flow controls')
req('TitanXmbSpline_CubicBezierY' in spline and 'primary_wave->control1_y' in spline and
    'primary_wave->control2_y' in spline and 'primary_wave->drift_y' in spline,
    'Flow renderer is not using theme-owned cubic trajectories')
req('primary_wave_count=' in flow and 'primary_wave1_control1_y_milli=' in flow and
    'primary_wave1_control2_y_milli=' in flow,
    'Scarlet Flow theme-owned curve profile missing')
req('version=' in flow, 'Scarlet Flow version missing')
req('Hardware-visible canary' in wle and 'draw_rect(36.0f, 34.0f, 5.0f, 350.0f' in wle,
    'wLaunchELF hardware draw canary missing')
wlever=re.search(r'--version\s+(\d+)\.(\d+)\.(\d+)', wlemake)
req(wlever is not None and tuple(map(int,wlever.groups())) >= (0,1,2), 'wLaunchELF package version not advanced')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update, 'system-update generation missing')
# BuildFix27 established that these payloads can exist and be updated, but
# Build36 preserves the seven-entry BuildFix34 bootstrap envelope, including the
# existing wLaunch/Scarlet Flow payloads, so the running BuildFix34 sees the same TPUP geometry.
req('PUP_OUTPUT ?=' in make and '--build 36' in make and
    'build36-complex-theme-api-v3-compat7.pup' in make and
    (project/'apps/wlaunchelf/src/wlaunchelf_titan.c').is_file() and
    (project/'themes/ribbon-scarlet/theme.cfg').is_file(),
    'BuildFix27 capabilities or Build36 bootstrap metadata missing')
print('buildfix27-flow-ps4-wlaunchelf-pup-contract: OK')
