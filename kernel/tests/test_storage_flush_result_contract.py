#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else ".")
svc=root/"kernel"/"src"/"services"
files=[svc/"titan_early_input.c",svc/"titan_download_service.c"]
for f in files:
    s=f.read_text()
    assert "!TitanStorageIo_Flush(" not in s, f"boolean-negated TitanStorageIo_Flush survives in {f}"
assert "TitanStorageIo_Flush(fd)<0" in (svc/"titan_early_input.c").read_text()
assert "TitanStorageIo_Flush(descriptor) < 0" in (svc/"titan_download_service.c").read_text()
print("storage-flush-result-contract: PASS flush success(0) is not inverted")
