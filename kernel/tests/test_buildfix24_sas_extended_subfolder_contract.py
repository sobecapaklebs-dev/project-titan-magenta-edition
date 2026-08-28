#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); c=(k/'src/services/titan_applications.c').read_text()
assert 'TitanApplications_DirectoryHasSubfolders' in c and 'TITAN_SAS_PROFILE_EXTENDED_COMPAT' in c
assert 'application->sas_has_subfolders == 0u' in c
print('titan-sas-extended-subfolder-contract: OK subfolders remain compatible but block classic compliance')
