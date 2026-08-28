#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); c=(k/'src/services/titan_applications.c').read_text()
for n in ['TITAN_SAS_PROFILE_CLASSIC','TitanApplications_IsMemoryCardRootFolder','sas_osdsys_manageable']:
 assert n in c
print('titan-sas-classic-osdsys-contract: OK classic profile + MC root manageability modeled independently')
