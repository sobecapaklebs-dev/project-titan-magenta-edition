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

req('wander_enabled' in h and 'wander_speed' in h and 'wander_extent_x' in h and
    'wander_extent_y' in h and 'wander_curve' in h and
    'wander_visibility_min' in h and 'wander_seed' in h,
    'theme ABI stochastic wander controls missing')
req('wander_speed_milli' in c and 'wander_extent_x_milli' in c and
    'wander_extent_y_milli' in c and 'wander_curve_milli' in c and
    'wander_visibility_min_milli' in c and 'wander_seed' in c,
    'theme parser stochastic wander controls missing')
req('TitanXmbSpline_Hash01' in spline and 'TitanXmbSpline_SmootherStep' in spline and
    'Deterministic hashing keeps the motion reproducible' in spline and
    'state_index * 0x9e3779b9u' in spline and
    'g_primary_wave_dynamic_visibility' in spline,
    'renderer is not using smooth deterministic stochastic states')
req('wander_enabled' in h and 'wander_speed_milli' in c and
    'TitanXmbSpline_Hash01' in spline and 'TitanXmbSpline_SmootherStep' in spline,
    'BuildFix32 stochastic wander capability regressed')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update,
    'system update generation missing')
print('buildfix32-stochastic-flow-contract: OK capability preserved')
