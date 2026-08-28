#!/usr/bin/env python3
import pathlib, sys
root=pathlib.Path(sys.argv[1])
h=(root/'src/services/titan_diagnostics.h').read_text()
c=(root/'src/services/titan_diagnostics.c').read_text()
p=(root/'src/graphics/titan_pikavr.c').read_text()
for name in ['TITAN_DIAG_PIKAVR','TITAN_DIAG_SAS','TITAN_DIAG_BADGES','TITAN_DIAG_VLOMBIS']:
    assert name in h, name
assert 'TitanDiagnostics_SetMaximumLevel' in h and 'TitanDiagnostics_SetSubsystemEnabled' in h
assert 'level > g_diag.maximum_level' in c
assert 'g_diag.subsystem_mask' in c
for label in ['PIKAVR','SAS','BADGES','VLOMBIS']:
    assert f'"{label}"' in c
assert 'TITAN_DIAG_PIKAVR' in p
# No diagnostics event is allowed in the renderer-frame counter functions.
for fn in ['TitanPikaVr_BeginRendererFrame','TitanPikaVr_RecordDraw','TitanPikaVr_RecordMaterialPass','TitanPikaVr_RecordRibbonPath1Qwords','TitanPikaVr_EndRendererFrame']:
    body=p.split(fn,1)[1].split('}',1)[0]
    assert 'TitanDiagnostics_Event' not in body, fn
print('diagnostics-categories-contract: OK')
