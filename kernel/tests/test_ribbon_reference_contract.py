#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
project=root.parent

def req(c,m):
    if not c: raise SystemExit(m)

h=(root/'src/services/titan_theme.h').read_text()
c=(root/'src/services/titan_theme.c').read_text()
s=(root/'src/ui/titan_xmb_spline.c').read_text()
expected={
 'ribbon-scarlet':('Ribbon Scarlet','148','14','25','52391'),
 'ribbon-wine':('Ribbon Vinho','226','82','132','51391'),
 'ribbon-cyan':('Ribbon Ciano','20','188','210','62117'),
 'ribbon-twilight':('Ribbon Crepusculo','190','48','220','47831'),
}
# The v8 reference-ribbon parser/ABI remains supported for external themes,
# but it is no longer the built-in release profile after hardware regression.
req('TITAN_THEME_BACKGROUND_RIBBON3D' in h and 'reference_ribbon_enabled' in h, 'Ribbon compatibility ABI missing')
for tok in ('ribbon_speed_milli','ribbon_thickness_milli','ribbon_fold_amplitude_milli','ribbon_twist_amplitude_milli','ribbon_span_milli'):
    req(tok in c, f'Ribbon compatibility parser missing {tok}')
req('TitanXmbSpline_BuildReferenceRibbonRow' in s, 'reference Ribbon compatibility renderer missing')
for folder,(name,r,g,b,seed) in expected.items():
    cfg=project/'themes'/folder/'theme.cfg'
    req(cfg.is_file(),f'missing theme directory: {folder}')
    t=cfg.read_text()
    for tok in (f'name={name}','version=8.0.1-hwfix2','background_engine=flow3d-v1',
                'primary_wave1_reference_ribbon=0','primary_wave1_snake=1',
                'primary_wave1_snake_speed_milli=2','primary_wave1_snake_extent_x_milli=520000',
                'primary_wave1_snake_extent_y_milli=320000','primary_wave1_snake_trail_frames_milli=4500',
                'vu1_deform_speed_milli=720','vu1_deform_frequency_milli=1500',
                'vu1_deform_amplitude_milli=7200','particle_alpha=0',f'wave_r={r}',
                f'wave_g={g}',f'wave_b={b}',f'primary_wave1_snake_seed={seed}'):
        req(tok in t,f'{folder}: {tok} missing')
print('ribbon-reference-contract: OK compatibility retained; hardware-proven Flow/Snake/VU1 is release profile')
