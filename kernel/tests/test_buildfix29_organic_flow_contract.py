#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

spline = (root/'src/ui/titan_xmb_spline.c').read_text()
flow = (project/'themes/ribbon-scarlet/theme.cfg').read_text()
make = (root/'Makefile').read_text()
update = (root/'src/services/titan_system_update.h').read_text()

req('sweep_breathe' in spline and 'sweep_visibility' in spline and
    'point_rot_delta' in spline and 'flex_bend' in spline and
    'span_pulse' in spline,
    'organic Flow motion fields missing')
req('width_envelope = TitanXmbSpline_Lerp(0.54f, 1.12f, ribbon_shape)' in spline and
    'local_x * point_rot_cos - local_y * point_rot_sin' in spline,
    'ribbon no longer tapers/flexes organically')
req('version=' in flow and 'wave_alpha=' in flow and
    'highlight_alpha=' in flow and 'primary_wave1_amplitude_milli=' in flow and
    'flow_noise_milli=' in flow,
    'Scarlet Flow visibility/dynamics profile missing')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update,
    'system update generation missing')
req('PUP_OUTPUT ?=' in make and '--build ' in make,
    'PUP payload/version missing')
print('buildfix29-organic-flow-contract: OK')
