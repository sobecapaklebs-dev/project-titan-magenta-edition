#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
required=[
'kernel/src/services/titan_datetime.c','kernel/src/services/titan_theme_lifecycle.c','kernel/src/services/titan_disc_service.c','kernel/src/services/titan_disc_engine.c','kernel/src/services/titan_localization.c',
'kernel/src/services/titan_unified_input.c','kernel/src/services/titan_session_service.c','kernel/src/services/titan_download_service.c','kernel/src/services/titan_store_service.c',
'kernel/src/graphics/titan_pikavr.c','kernel/src/services/titan_vlombiscore.c','tools/titan_tins.py','tools/titan_api_freeze.py']
for rel in required: assert (root/rel).is_file(), rel
runner=(root/'kernel/tests/run_host_tests.sh').read_text()
for token in ('test_buildfix30_datetime','test_buildfix30_theme_lifecycle','test_buildfix30_disc_service','test_buildfix30_disc_engine','test_buildfix30_localization','test_buildfix30_boot_packaging','test_buildfix30_pup_power','test_buildfix30_api_freeze'):
    assert token in runner, token
for rel in ('kernel/src/services/titan_datetime.c','kernel/src/services/titan_theme_lifecycle.c','kernel/src/services/titan_disc_service.c','kernel/src/services/titan_disc_engine.c','kernel/src/services/titan_localization.c'):
    text=(root/rel).read_text(errors='replace')
    for bad in ('TODO','FIXME','NOT_IMPLEMENTED','fake-backend'):
        assert bad not in text, (rel,bad)
scene=(root/'kernel/src/ui/titan_shell_scene.c').read_text()
assert 'TitanDiscEngine_Shutdown();' in scene and 'TitanDiscService_Shutdown();' in scene
assert 'TitanLocalization_CycleLanguage' in scene
assert 'TITAN_THEME_LIFECYCLE_RETURN_HOME' in scene
print('buildfix30-release-regression-meta-contract: OK')
