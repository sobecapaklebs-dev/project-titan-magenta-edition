#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); c=(k/'src/services/titan_sas.c').read_text(); d=(k.parent/'TITAN_SAS_PREFIX_TABLE.md').read_text()
assert '"APP"' in c and 'TITAN_SAS_PREFIX_SOURCE_TNA_PRIMARY' in c
assert 'TitanSas_IsThreeLetterCompatibilityPrefix' in c
assert 'No additional normative prefixes' in d
print('titan-sas-prefix-recognition-contract: OK sourced APP table + non-normative 3-letter compatibility hint')
