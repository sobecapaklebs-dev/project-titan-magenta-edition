#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
if root.name == "kernel":
    root = root.parent

def text(rel):
    return (root / rel).read_text(encoding="utf-8")

pn = text("apps/project-nes/Makefile")
wle = text("apps/wlaunchelf/Makefile")
eye = text("ports/goldeneye/Makefile.titan")
builder = text("ports/goldeneye/tools/build_private_package.py")
runtime = text("kernel/src/services/titan_app_runtime.c")
checker = text("tools/check_titan_native_n32.py")

errors = []

for name, data in (("Project NES", pn), ("wLaunchELF", wle), ("TitanEYE", eye)):
    if "-mabi=32" in data:
        errors.append(f"{name}: O32 compile flag survived")
    if "-mabi=n32" not in data:
        errors.append(f"{name}: N32 compile flag missing")
    if "check_titan_native_n32.py" not in data:
        errors.append(f"{name}: built artifact is not ABI-gated")

if "$(EE_CC) -march=r5900 -mabi=n32 -nostdlib -r -Wl,--no-relax -o $@ $(OBJECTS)" not in eye:
    errors.append("TitanEYE: relocatable link is not EE GCC/N32")
if "-m elf32lr5900" in eye:
    errors.append("TitanEYE: explicit O32 linker emulation survived")

if "check_titan_native_n32.py" not in builder:
    errors.append("TitanEYE: private builder can package an unchecked .tmod")
if '"--version", "1.0.2"' not in builder:
    errors.append("TitanEYE: private N32 package version is not 1.0.2")
if "--version 0.1.3" not in wle or "version=0.1.3" not in wle:
    errors.append("wLaunchELF: N32 version is not 0.1.3")

if "(header->flags & TITAN_TMOD_EF_MIPS_ABI2) == 0u || abi != 0u" not in runtime:
    errors.append("runtime: strict N32/ABI2 guard missing")
if "Modulo Titan Native requer ABI N32/ABI2" not in runtime:
    errors.append("runtime: explicit ABI error missing")

for token in ("EF_MIPS_ABI2", "EF_MIPS_ABI_MASK", "EF_MIPS_ABI_O32", "PIC/CPIC forbidden"):
    if token not in checker:
        errors.append("checker: missing " + token)

if errors:
    for e in errors:
        print("rc1-titan-native-n32-unification: FAIL:", e)
    raise SystemExit(1)

print("rc1-titan-native-n32-unification: PASS Project NES + wLaunchELF + TitanEYE = R5900 N32/ABI2")
