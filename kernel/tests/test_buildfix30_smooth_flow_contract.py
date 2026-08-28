#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
project=root.parent
spline=(root/'src/ui/titan_xmb_spline.c').read_text()
flow=(project/'themes/ribbon-scarlet/theme.cfg').read_text()
make=(root/'Makefile').read_text()
update=(root/'src/services/titan_system_update.h').read_text()
def req(c,m):
    if not c: raise SystemExit(m)
req('TitanXmbSpline_WaveFloat' in spline and 'fraction = phase - (float)word' in spline, 'fractional LUT interpolation missing')
req('sweep_sin = TitanXmbSpline_WaveFloat(sweep_phase)' in spline and 'time_phase = (float)frame * speed * 0.34f' in spline, 'Flow still quantizes temporal phase')
req('curve_morph_a' in spline and 'primary_wave->control1_y + curve_morph_a' in spline and 'primary_wave->control2_y + curve_morph_b' in spline, 'global spline morph missing')
req('version=' in flow, 'Scarlet Flow version missing')
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update, 'system update generation missing')
req('PUP_OUTPUT ?=' in make and '--build ' in make, 'PUP target missing')
print('buildfix30-smooth-flow-contract: OK')
