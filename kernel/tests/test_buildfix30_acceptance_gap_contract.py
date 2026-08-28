#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
installer=(root/'kernel/src/installer/titan_installer.c').read_text()
model=(root/'kernel/src/installer/titan_installer_model.c').read_text()
disc=(root/'kernel/src/services/titan_disc_service.c').read_text()
disch=(root/'kernel/src/services/titan_disc_service.h').read_text()
igr=(root/'kernel/src/services/titan_igr_compatibility.c').read_text()
engine=(root/'kernel/src/services/titan_disc_engine.c').read_text()

def req(x,msg):
    if not x: raise SystemExit('FAIL: '+msg)
req('English (não disponível nesta POC)' not in installer,'English still disabled')
for x in ('"Português (Brasil)"','"English"','"Español"'):
    req(x in installer,'missing installer language '+x)
req('s.language=s.selection' in installer and 'g_installer_ui_language=s.language' in installer,'language selection not applied')
req('g_installer_translations' in installer and 'installer_tr(' in installer,'installer translation layer missing')
req('TitanInstallerModel_IsAutonomousCompatible(&s->console)' in installer,'autonomous mode not compatibility gated')
for x in ('model_id_valid','romver[4]','TITAN_INSTALLER_CONSOLE_UNKNOWN'):
    req(x in model,'autonomous compatibility missing '+x)
req('read_metadata' in disch and 'Ps2ReadMetadata' in disc,'disc async local metadata lookup missing')
req('TITAN_DISC_METADATA_LOCAL_CACHE' in disc and 'TitanDisc_ParseMetadata' in disc,'disc metadata cache state/parser missing')
for x in ('icon_path','cover_path','background_path','compatibility_profile_hint'):
    req(x in disch,'disc metadata field missing '+x)
req('requested_profile >= 100u && requested_profile <= 102u' in igr,'disc IGR profile negotiation missing')
req('disc->compatibility_profile_hint' in engine,'disc engine does not consume compatibility profile')
print('buildfix30-acceptance-gap-contract: PASS')
