import sys
from pathlib import Path
k=Path(sys.argv[1]); c=(k/'src/services/titan_applications.c').read_text(); h=(k/'src/services/titan_applications.h').read_text(); assert 'TitanApplications_IsSasCompatible' in c+h; assert 'TitanApplications_IsSasCompliant' in c+h; assert 'TitanApplications_ApplySasStructure' in c; print('titan-sas-library-contract: OK discovery retains compatibility while exposing compliance')
