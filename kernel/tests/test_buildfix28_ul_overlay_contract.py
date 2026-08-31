from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-ul-engine/ee_core/src/padhook.c').read_text()
c=(r/'modules/titan-ul-engine/ee_core/include/coreconfig.h').read_text()
o=(r/'modules/titan-ul-engine/src/opl.c').read_text()
k=(r/'kernel/src/services/titan_neutrino.c').read_text()
for t in ['TitanOverlay_Run','TitanEngineOverlay_Render','TitanOverlayProfileFlags','TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE']:
 assert t in p or t in c,t
assert 'plan->arguments[plan->argument_count++] = g_opl_ul_loader;' in k
assert 'plan->arguments[plan->argument_count++] = (char *)game->startup;' in k
assert 'plan->arguments[plan->argument_count++] = "titan-ul";' in k
assert '-titan-igr=return-only' not in k and '-titan-igr-profile=0' not in k
assert 'metadata->overlay_level = 2' in o
print('buildfix28-ul-overlay-contract: OK')
