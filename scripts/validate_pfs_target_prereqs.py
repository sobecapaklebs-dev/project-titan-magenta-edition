#!/usr/bin/env python3
"""BuildFix27 CP40: validate material target prerequisites for HDD/PFS.

Host builds are allowed to report BLOCKED. Target integration is ready only when
PS2SDK/IOP toolchain plus official ps2hdd.irx and ps2fs.irx are materially present.
No downloads or builds are attempted by this validator.
"""
from __future__ import annotations
import argparse, os, shutil, sys
from pathlib import Path

EXPECTED = {
    "ps2atad": Path("iop/irx/ps2atad.irx"),
    "ps2hdd": Path("iop/irx/ps2hdd.irx"),
    "ps2fs": Path("iop/irx/ps2fs.irx"),
}
SOURCE_MAKEFILES = {
    "ps2atad": Path("iop/dev9/atad/Makefile"),
    "ps2hdd": Path("iop/hdd/apa/Makefile"),
    "ps2fs": Path("iop/hdd/pfs/Makefile"),
}

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--require-ready", action="store_true", help="return nonzero unless target prerequisites are material")
    args = ap.parse_args()

    ps2dev = os.environ.get("PS2DEV", "")
    ps2sdk = os.environ.get("PS2SDK", "")
    sdk = Path(ps2sdk) if ps2sdk else None
    checks: list[tuple[str, bool, str]] = []
    checks.append(("PS2DEV", bool(ps2dev and Path(ps2dev).exists()), ps2dev or "<unset>"))
    checks.append(("PS2SDK", bool(sdk and sdk.exists()), ps2sdk or "<unset>"))

    # Current PS2SDK installs the IOP compiler under PS2DEV/iop/bin; PATH is also accepted.
    iop_cc_candidates = []
    if ps2dev:
        iop_cc_candidates.extend([
            Path(ps2dev) / "iop/bin/mipsel-none-elf-gcc",
            Path(ps2dev) / "iop/bin/iop-gcc",
        ])
    path_cc = shutil.which("mipsel-none-elf-gcc") or shutil.which("iop-gcc")
    cc_ok = any(p.exists() for p in iop_cc_candidates) or bool(path_cc)
    checks.append(("IOP_CC", cc_ok, path_cc or next((str(p) for p in iop_cc_candidates if p.exists()), "<not found>")))

    for key, rel in SOURCE_MAKEFILES.items():
        p = sdk / rel if sdk else Path("/__missing__")
        checks.append((f"{key}_source", bool(sdk and p.is_file()), str(p) if sdk else "<PS2SDK unset>"))
    for key, rel in EXPECTED.items():
        p = sdk / rel if sdk else Path("/__missing__")
        checks.append((f"{key}_irx", bool(sdk and p.is_file() and p.stat().st_size > 0), str(p) if sdk else "<PS2SDK unset>"))

    ready = all(ok for _, ok, _ in checks)
    for name, ok, detail in checks:
        print(f"{name}: {'OK' if ok else 'BLOCKED'} {detail}")
    print("PFS_TARGET_READY=" + ("1" if ready else "0"))
    if args.require_ready and not ready:
        return 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
