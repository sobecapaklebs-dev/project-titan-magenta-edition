from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-neutrino-engine/ee/ee_core/src/padhook.c').read_text()
m=(r/'modules/titan-neutrino-engine/ee/ee_core/Makefile').read_text()
c=(r/'modules/titan-neutrino-engine/common/include/eecore_config.h').read_text()
l=(r/'modules/titan-neutrino-engine/ee/loader/src/main.c').read_text()
k=(r/'kernel/src/services/titan_neutrino.c').read_text()
for t in ['TitanOverlay_Run','TitanEngineOverlay_Render','TitanOverlayProfileFlags','TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE','Install_PadOpen_Hook']:
    assert t in p or t in c,t
assert 'titan_engine_overlay.o' in m and '../../../titan-engine-overlay' in m
# The loader still accepts Titan's overlay policy options for explicit callers;
# the kernel's validated golden launch path intentionally does not append them.
for t in ['-titan-igr=', '-titan-igr-profile=', '-titan-exit=']:
    assert t in l,t
assert 'TitanOverlay_Run' in p and 'TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE' in p
print('buildfix28-neutrino-overlay-contract: OK')
