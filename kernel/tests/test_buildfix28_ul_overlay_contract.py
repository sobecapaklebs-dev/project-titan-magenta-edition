from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-ul-engine/ee_core/src/padhook.c').read_text()
c=(r/'modules/titan-ul-engine/ee_core/include/coreconfig.h').read_text()
o=(r/'modules/titan-ul-engine/src/opl.c').read_text()
k=(r/'kernel/src/services/titan_neutrino.c').read_text()
for t in ['TitanOverlay_Run','TitanEngineOverlay_Render','TitanOverlayProfileFlags','TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE']:
 assert t in p or t in c,t
assert '-titan-igr=return-only' in k and '-titan-igr-profile=0' in k
assert 'metadata->overlay_level = 2' in o
print('buildfix28-ul-overlay-contract: OK')
