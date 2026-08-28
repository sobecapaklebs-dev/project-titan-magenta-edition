#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=(root/'kernel/src/services/titan_neutrino.c').read_text()
ss=(root/'kernel/src/services/titan_session_service.c').read_text()
runner=(root/'kernel/tests/run_host_tests.sh').read_text()
for token in ['TitanNeutrino_BeginExternalSession','TITAN_SESSION_ENGINE_UL','TITAN_SESSION_ENGINE_NEUTRINO','TITAN_APPLICATION_CLASS_UL_ENGINE','TITAN_APPLICATION_CLASS_NEUTRINO_ENGINE','TITAN_OVERLAY_RETURN_ONLY','TitanSession_Begin(&session)','TitanSession_Transition(TITAN_SESSION_LAUNCHING)']:
    assert token in s, token
assert 'TITAN_IGR_UNKNOWN &&\n        g_session.descriptor.overlay_level != TITAN_OVERLAY_RETURN_ONLY' in ss
assert 'titan_session_service.c' in runner and 'titan_time.c' in runner
print('buildfix28-external-session-contract: OK')
