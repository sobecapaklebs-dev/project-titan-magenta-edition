#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); c=(k/'src/services/titan_applications.c').read_text()
assert 'TITAN_APPLICATION_METADATA_APP_CFG' in c and 'TITAN_SAS_PROFILE_TITAN_FALLBACK' in c
print('titan-sas-appcfg-fallback-contract: OK app.cfg retained as Titan compatibility fallback, not compliance proof')
