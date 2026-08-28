from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-ul-engine/ee_core/src/padhook.c').read_text()
o=(r/'modules/titan-engine-overlay/titan_engine_overlay.c').read_text()
for t in ['TitanOverlay_SuspendGameThreads','TitanOverlay_ResumeGameThreads','TitanOverlay_Run','DelayThread(8333)']:
 assert t in p,t
assert 'TITAN_ENGINE_OVERLAY_RESUME' in o
branch=p[p.index('BuildFix28: a normal Titan overlay trigger'):p.index('// If power button or legacy combo')]
assert 'iResetEE' not in branch and 'R_EE_GS_CSR' not in branch
assert 'TitanOverlay_ResumeGameThreads();' in p
print('buildfix28-ul-overlay-resume-contract: OK')
