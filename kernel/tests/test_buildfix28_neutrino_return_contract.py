from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'modules/titan-neutrino-engine/ee/ee_core/src/padhook.c').read_text()
main=(r/'modules/titan-neutrino-engine/ee/ee_core/src/main.c').read_text()
l=(r/'modules/titan-neutrino-engine/ee/loader/src/main.c').read_text()
k=(r/'kernel/src/services/titan_neutrino.c').read_text()
for t in ['TitanOverlay_ReturnToTitan','LoadExecPS2(eec.TitanExitPath','TITAN_ENGINE_OVERLAY_RETURN']:
    assert t in p,t
# Neutrino's resident LoadExec re-entry is the cleanup path: wipe title memory,
# reboot IOP, load destination, then CleanExecPS2.
for t in ['WipeUserMemory','New_Reset_Iop2','SifLoadElf','CleanExecPS2']:
    assert t in main,t
assert 'TitanExitPath' in l and '-titan-exit=mc0:/BOOT/BOOT.ELF' in k
assert 'eec.TitanOverlayLevel == 1' in p  # UNKNOWN return-only still returns.
print('buildfix28-neutrino-return-contract: OK')
