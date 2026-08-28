import sys
from pathlib import Path
k=Path(sys.argv[1]); b=(k/'src/services/titan_software_badges.c').read_text(); a=(k/'src/services/titan_applications.c').read_text(); assert 'TitanApplications_GetClassificationName(classification)' in b; assert 'SAS compliant' not in b and 'SAS-Compliant' not in b; assert 'Titan SAS' in a; print('titan-sas-badge-contract: OK badge reports compatibility classification, not compliance claim')
