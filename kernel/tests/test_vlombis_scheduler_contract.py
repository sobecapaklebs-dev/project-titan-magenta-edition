#!/usr/bin/env python3
import pathlib, sys
root=pathlib.Path(sys.argv[1])
s=(root/'src/services/titan_storage_scheduler.c').read_text()
k=(root/'src/core/titan_kernel.c').read_text()
b=(root/'src/services/titan_software_badges.c').read_text()
h=(root/'src/services/titan_vlombiscore.h').read_text()
assert 'TitanVlombisCore_Initialize();' in k
for event in ['TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF','TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION','TITAN_VLOMBIS_EVENT_FOREGROUND_JOB']:
    assert event in s, event
assert 'vlombis_capabilities_active != 0u' in b and 'vlombis_measured_events != 0u' in b
for field in ['measured_events','exclusive_handoffs','background_preemptions','foreground_jobs','storage_arbitrations']:
    assert field in h, field
print('vlombis-scheduler-contract: OK real scheduler evidence gates badge')
