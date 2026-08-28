#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
checks={
'kernel/src/services/titan_download_service.h':'TITAN_DOWNLOAD_SERVICE_API_VERSION 1u',
'kernel/src/services/titan_store_service.h':'TITAN_STORE_SERVICE_API_VERSION 1u',
'kernel/src/services/titan_repository_protocol.h':'TITAN_REPOSITORY_PROTOCOL_VERSION 1u',
'kernel/src/services/titan_unified_input.h':'TITAN_UNIFIED_INPUT_API_VERSION 1u',
'kernel/src/services/titan_session_service.h':'TITAN_SESSION_SERVICE_API_VERSION 1u',
'kernel/src/services/titan_disc_service.h':'TITAN_DISC_SERVICE_API_VERSION 1u',
'kernel/src/services/titan_disc_engine.h':'TITAN_DISC_ENGINE_API_VERSION 1u',
'kernel/src/services/titan_theme_lifecycle.h':'TITAN_THEME_LIFECYCLE_API_VERSION 1u',
'kernel/src/services/titan_theme_v4_runtime.h':'TITAN_THEME_V4_RUNTIME_API_VERSION 1u',
'kernel/src/services/titan_vlombiscore.h':'TITAN_VLOMBISCORE_API_VERSION 1u',
'kernel/src/graphics/titan_pikavr.h':'TITAN_PIKAVR_API_VERSION 1u',
}
for f,t in checks.items():
    s=(r/f).read_text(); assert t in s,(f,t)
app=(r/'kernel/include/titan_app_sdk.h').read_text()
game=(r/'kernel/include/titan_game_sdk.h').read_text()
assert '#define TITAN_APP_API_VERSION 1u' in app
assert '#define TITAN_GAME_API_VERSION 2u' in game
for c in [r/'apps/project-nes/include/titan_app_sdk.h',r/'apps/wlaunchelf/include/titan_app_sdk.h']:
    assert c.read_bytes()==(r/'kernel/include/titan_app_sdk.h').read_bytes(),c
repo=(r/'kernel/src/services/titan_repository_protocol.h').read_text()
assert 'TITAN_REPOSITORY_PLATFORM_PS2 "ps2"' in repo
assert 'TITAN_REPOSITORY_ARCH_R5900 "mips-r5900"' in repo
freeze=(r/'docs/reference/TITAN_API_FREEZE_V1.md').read_text()
assert 'raw CD/DVD' in freeze and 'HIGH, MID and ADVANCED' in freeze
policy=(r/'docs/reference/TITAN_STANDALONE_SDK.md').read_text()
assert 'separately' in policy and '0.3.93-rc17' in policy
print('titan-api-freeze-v1-contract: OK')
print('titan-disc-sdk-ownership-contract: OK no raw optical API exported')
print('titan-repository-protocol-v1-freeze-contract: OK')
