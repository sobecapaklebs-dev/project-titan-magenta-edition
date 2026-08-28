#!/usr/bin/env python3
import pathlib, sys
r=pathlib.Path(sys.argv[1])
h=(r/'src/services/titan_applications.h').read_text()
c=(r/'src/services/titan_applications.c').read_text()
assert 'TITAN_APPLICATION_EXECUTION_NATIVE_IN_PROCESS' in h
assert 'TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF' in h
assert 'application->classification == TITAN_APPLICATION_CLASS_SAS' in c
assert 'return TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF;' in c
assert 'External ELF handoff' in c
print('sas-execution-model-contract: OK classification separated from execution model')
