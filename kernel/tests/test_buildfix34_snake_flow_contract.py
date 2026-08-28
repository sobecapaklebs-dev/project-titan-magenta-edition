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

req('snake_enabled' in h and 'snake_trail_frames' in h and 'snake_seed' in h,
    'theme ABI snake controls missing')
req('snake_speed_milli' in c and 'snake_trail_frames_milli' in c and
    'snake_visibility_min_milli' in c, 'theme parser snake controls missing')
req('TitanXmbSpline_CatmullRom' in spline and
    'TitanXmbSpline_PrepareSnakeSpine' in spline and
    'older head position' in spline and 'tangent_x' in spline,
    'head-driven trailing spine implementation missing')
req('primary_wave1_snake=1' in flow and 'primary_wave1_wander=0' in flow and
    'primary_wave1_reference_ribbon=0' in flow and 'version=8.0.1-hwfix2' in flow,
    'hardware-proven Snake Flow profile is not active')
req('vu1_deform_speed_milli=720' in flow and
    'vu1_deform_frequency_milli=1500' in flow and
    'vu1_deform_amplitude_milli=7200' in flow,
    'hardware-proven VU1 deformation profile is not active')
req('reference_ribbon_enabled' in h and 'ribbon_speed_milli' in c and
    'TitanXmbSpline_BuildReferenceRibbonRow' in spline,
    'reference Ribbon compatibility path was removed')
print('snake-flow-capability-contract: OK hardware-proven Snake/VU1 active; reference compatibility retained')
