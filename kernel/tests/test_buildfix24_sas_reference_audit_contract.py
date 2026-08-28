#!/usr/bin/env python3
from pathlib import Path
import sys
project = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
doc = (project / 'TITAN_SAS_REFERENCE_AUDIT.md').read_text(encoding='utf-8')
required = [
    'https://www.psx-place.com/threads/sas-standard-information-structure.44785/',
    'https://www.psx-place.com/resources/osdmenu.1588/',
    'https://ps2wiki.github.io/sas-apps-archive/',
    'SAS_COMPLIANT => SAS_COMPATIBLE',
    'INSTALL.PBT',
    'app.cfg` is Titan compatibility metadata/fallback only',
    'must **not invent additional normative prefixes**',
]
for item in required:
    if item not in doc:
        raise SystemExit(f'titan-sas-reference-audit-contract: FAIL missing {item!r}')
print('titan-sas-reference-audit-contract: OK primary-source boundary and unresolved-prefix caution recorded')
