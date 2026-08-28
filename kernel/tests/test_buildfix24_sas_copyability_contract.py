#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); h=(k/'src/services/titan_sas.h').read_text(); c=(k/'src/services/titan_applications.c').read_text()
assert 'TITAN_SAS_OSDSYS_COPY_SAFE' in h and 'TITAN_SAS_OSDSYS_COPY_UNSAFE' in h
assert 'sas_copyability' in c
print('titan-sas-copyability-contract: OK copyability separate from compliance')
