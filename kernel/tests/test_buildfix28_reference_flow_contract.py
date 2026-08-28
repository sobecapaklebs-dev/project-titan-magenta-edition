#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

h = (root/'src/services/titan_theme.h').read_text()
c = (root/'src/services/titan_theme.c').read_text()
spline = (root/'src/ui/titan_xmb_spline.c').read_text()
flow = (project/'themes/ribbon-scarlet/theme.cfg').read_text()
make = (root/'Makefile').read_text()
update = (root/'src/services/titan_system_update.h').read_text()

req('sweep_enabled' in h and 'sweep_speed' in h and 'sweep_orbit_x' in h and
    'sweep_orbit_y' in h and 'sweep_span_scale' in h and 'sweep_phase' in h,
    'theme ABI does not expose long-cycle ribbon sweep')
req('sweep_speed_milli' in c and 'sweep_orbit_x_milli' in c and
    'sweep_orbit_y_milli' in c and 'sweep_span_milli' in c and 'sweep_phase' in c,
    'theme parser does not expose long-cycle ribbon sweep controls')
req('sweep_rot_cos = -sweep_sin' in spline and
    'sweep_center_x += sweep_cos * primary_wave->sweep_orbit_x' in spline and
    ('local_x * sweep_rot_cos - local_y * sweep_rot_sin' in spline or
     'local_x * point_rot_cos - local_y * point_rot_sin' in spline),
    'Flow renderer is not orbiting/rotating one composite ribbon')
req('primary_wave_count=1' in flow and 'version=' in flow and
    ('primary_wave1_sweep=1' in flow or 'primary_wave1_wander=1' in flow or 'primary_wave1_snake=1' in flow or 'primary_wave1_reference_ribbon=1' in flow),
    'Scarlet Flow composite-ribbon reference profile missing')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update,
    'system update generation missing')
req('PUP_OUTPUT ?=' in make and '--build 36' in make and
    'build36-complex-theme-api-v3-compat7.pup' in make and
    (project/'themes/ribbon-scarlet/theme.cfg').is_file(),
    'BuildFix28 Flow capability or Build36 bootstrap metadata missing')
print('buildfix28-reference-flow-contract: OK')
