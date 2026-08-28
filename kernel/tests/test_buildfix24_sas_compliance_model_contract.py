#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve()
h=(k/'src/services/titan_applications.h').read_text()
c=(k/'src/services/titan_applications.c').read_text()
req=[
 ('unsigned int sas_compatible;', 'compatible field'),
 ('unsigned int sas_compliant;', 'compliant field'),
 ('application->sas_compatible = (compatible || compliant) ? 1u : 0u;', 'compliance implies compatibility normalization'),
 ('application->sas_compliant = compliant ? 1u : 0u;', 'compliance setter'),
 ('application->sas_compliant == 0u || application->sas_compatible != 0u', 'invalid-state validator'),
 ('TitanApplications_SetSasState(application, 1, 0);', 'compatible/noncompliant discovery path'),
 ('strncmp(line, "boot=", 5u)', 'canonical title.cfg boot key'),
]
for needle,label in req:
    if needle not in h+c:
        raise SystemExit(f'titan-sas-compliance-model-contract: FAIL missing {label}')
print('titan-sas-compliance-model-contract: OK compatible/compliant split + invariant + boot metadata')
