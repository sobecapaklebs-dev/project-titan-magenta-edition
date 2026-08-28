#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
script = (root / "scripts/validate_pfs_target_prereqs.py").read_text()
prov = (root / "docs/history/buildfix/BUILDFIX27_PFS_IOP_PROVENANCE.md").read_text()
platform = (root / "kernel/src/platform/ps2/titan_platform_pfs.c").read_text()
reqs = {
    "validator names ps2hdd.irx": "ps2hdd.irx" in script,
    "validator names ps2fs.irx": "ps2fs.irx" in script,
    "validator requires APA source Makefile": "iop/hdd/apa/Makefile" in script,
    "validator requires PFS source Makefile": "iop/hdd/pfs/Makefile" in script,
    "validator checks IOP compiler": "mipsel-none-elf-gcc" in script,
    "provenance pins official PS2SDK ref": "a13b5971ec0e39c7ba8b8559b80a4e81c8425352" in prov,
    "provenance records ps2hdd output": "ps2hdd.irx" in prov,
    "provenance records ps2fs output": "ps2fs.irx" in prov,
    "platform remains device-gated": 'TitanPlatformPfs_HasDevice("hdd")' in platform and 'TitanPlatformPfs_HasDevice("pfs")' in platform,
    "platform loads only behind embedded-material gate": "TITAN_HAVE_PFS_EMBEDDED" in platform and "SifExecModuleBuffer" in platform,
    "platform remains device-verified after loading": "TitanPlatformPfs_DevicesReady" in platform,
}
failed = [k for k,v in reqs.items() if not v]
if failed:
    raise SystemExit("buildfix27-pfs-iop-provenance-contract: FAIL: " + "; ".join(failed))
print("buildfix27-pfs-iop-provenance-contract: OK")
