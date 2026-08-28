import sys
from pathlib import Path
k=Path(sys.argv[1]); c=(k/'src/services/titan_applications.c').read_text(); assert 'compatible || compliant' in c; print('titan-sas-compliance-implies-compatibility-contract: OK')
