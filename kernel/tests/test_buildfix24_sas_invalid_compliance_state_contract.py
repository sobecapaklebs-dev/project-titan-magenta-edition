import sys
from pathlib import Path
k=Path(sys.argv[1]); c=(k/'src/services/titan_applications.c').read_text(); assert 'application->sas_compliant == 0u || application->sas_compatible != 0u' in c; print('titan-sas-invalid-compliance-state-contract: OK')
