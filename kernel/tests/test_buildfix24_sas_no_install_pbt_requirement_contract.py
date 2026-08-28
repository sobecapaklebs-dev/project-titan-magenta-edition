import sys
from pathlib import Path
k=Path(sys.argv[1]); c=(k/'src/services/titan_applications.c').read_text(); layout=(k/'tests/test_buildfix24_sas_layouts.c').read_text()
assert 'INSTALL.PBT' not in c
assert 'INSTALL.PBT' in layout and 'sas_compatible==0u' in layout
print('titan-sas-no-install-pbt-requirement-contract: OK INSTALL.PBT is neither requirement nor proof')
