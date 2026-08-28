from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-neutrino-engine/ee/ee_core/src/padhook.c').read_text()
main=(r/'modules/titan-neutrino-engine/ee/ee_core/src/main.c').read_text()
o=(r/'modules/titan-engine-overlay/titan_engine_overlay.c').read_text()
for t in ['TitanOverlay_SuspendGameThreads','TitanOverlay_ResumeGameThreads','TitanOverlay_Run','DelayThread(8333)']:
    assert t in p,t
assert 'TITAN_ENGINE_OVERLAY_RESUME' in o
# Resume path must not reboot/reset title state.
run=p[p.index('static TitanEngineOverlayAction TitanOverlay_Run'):p.index('static void TitanOverlay_ReturnToTitan')]
for bad in ['LoadExecPS2(', 'ResetEE', 'SifIopReset', 'New_Reset_Iop2']:
    assert bad not in run,bad
assert 'TitanOverlay_ResumeGameThreads();' in run
# Hook is installed after the title ELF is loaded and before CleanExecPS2.
start=main.index('int r = SifLoadElf')
end=main.index('CleanExecPS2((void *)elf.epc', start)
segment=main[start:end]
assert 'Install_PadOpen_Hook' in segment
print('buildfix28-neutrino-overlay-resume-contract: OK')
