#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

vsm = (root/'src/graphics/vu1/titan_wave_vu1.vsm').read_text()
wave_c = (root/'src/graphics/titan_wave_vu1.c').read_text()
wave_h = (root/'src/graphics/titan_wave_vu1.h').read_text()
spline = (root/'src/ui/titan_xmb_spline.c').read_text()
theme_h = (root/'src/services/titan_theme.h').read_text()
theme_c = (root/'src/services/titan_theme.c').read_text()
flow = (project/'themes/ribbon-scarlet/theme.cfg').read_text()
make = (root/'Makefile').read_text()
update = (root/'src/services/titan_system_update.h').read_text()

req(vsm.count('esin P,') >= 2 and vsm.count('waitp') >= 2 and
    'mfp.y vf08, P' in vsm and 'mfp.x vf09, P' in vsm,
    'VU1 EFU ESIN procedural carriers missing')
req('TitanWaveVu1_FoldSinPhase' in wave_c and
    'TITAN_WAVE_VU1_PI_OVER_64' in wave_c and
    'normal.z/normal.w carry signed procedural amplitudes' in wave_c,
    'EE-to-VU1 folded ESIN parameter packing missing')
req('procedural_phase' in wave_h and 'procedural_frequency' in wave_h and
    'procedural_amplitude' in wave_h,
    'VU1 layer procedural contract missing')
req('vu1_deform_speed' in theme_h and 'vu1_deform_frequency' in theme_h and
    'vu1_deform_amplitude' in theme_h and
    'vu1_deform_speed_milli' in theme_c and
    'vu1_deform_frequency_milli' in theme_c and
    'vu1_deform_amplitude_milli' in theme_c,
    'Theme Engine does not own VU1 deformation controls')
req('Fine per-point breathing moved to VU1 in BuildFix31' in spline and
    'The high-frequency flex carrier is now produced by VU1 EFU/ESIN' in spline,
    'EE local deformation was not transferred to VU1')
req('vu1_deform_speed_milli=' in flow and
    'vu1_deform_frequency_milli=' in flow and
    'vu1_deform_amplitude_milli=' in flow,
    'Scarlet Flow VU1 profile missing')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update,
    'system update generation missing')
req('build36-complex-theme-api-v3-compat7.pup' in make and '--build 36' in make,
    'Build36 bootstrap PUP target missing')
print('buildfix31-vu1-procedural-flow-contract: OK')
