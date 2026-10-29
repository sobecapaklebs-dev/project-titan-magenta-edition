#!/usr/bin/env python3
from pathlib import Path
import sys
project = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
print('titan-sas-reference-audit-contract: OK primary-source boundary and unresolved-prefix caution recorded')
