import sys
from pathlib import Path
k=Path(sys.argv[1]); t=(k/'tests/test_buildfix24_sas_layouts.c').read_text(); assert 'sas_compatible==1u&&a->sas_compliant==0u' in t; assert 'TITAN_SAS_PROFILE_EXTENDED_COMPAT' in t; print('titan-sas-compatible-noncompliant-contract: OK')
