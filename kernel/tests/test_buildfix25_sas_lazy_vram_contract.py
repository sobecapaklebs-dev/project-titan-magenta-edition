from pathlib import Path
import sys
k=Path(sys.argv[1]); apps=(k/'src/services/titan_applications.c').read_text(); sas=(k/'src/services/titan_sas.c').read_text()
for src in (apps,sas):
    assert 'TitanVideo_Upload' not in src
    assert 'TitanPikaVr_' not in src
    assert 'TitanVideoMemory_Allocate' not in src
print('titan-sas-lazy-vram-contract: PASS')
