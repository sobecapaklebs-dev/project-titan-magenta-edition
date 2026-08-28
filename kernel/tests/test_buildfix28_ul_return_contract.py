from pathlib import Path
r=Path(__file__).resolve().parents[2]; p=(r/'modules/titan-ul-engine/ee_core/src/padhook.c').read_text()
for t in ['TitanOverlay_ForceLegacy','IGR_PerformLegacyAction','oplIGRShutdown(0)','Reset_Iop("", 0)','IGR_Exit(0)']:
 assert t in p,t
assert 'action == TITAN_ENGINE_OVERLAY_RETURN' in p
print('buildfix28-ul-return-contract: OK')
