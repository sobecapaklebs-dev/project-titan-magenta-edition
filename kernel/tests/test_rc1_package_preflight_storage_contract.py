#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
if root.name == "kernel":
    root = root.parent

pkg = (root / "kernel/src/services/titan_package_installer.c").read_text(encoding="utf-8")
sched = (root / "kernel/src/services/titan_storage_scheduler.c").read_text(encoding="utf-8")

errors = []
for token in (
    "TitanPackageInstaller_OpenInstallSource",
    "TitanPackageInstaller_BuildUsbAliasPath",
    'strncmp(catalog_path, "mass0:/"',
    'strncmp(source, "mass:/"',
    'TitanPackageInstaller_SetPreflightError("OPEN")',
    'TitanPackageInstaller_SetPreflightError("SEEK")',
    'TitanPackageInstaller_SetPreflightError("READ")',
    'TitanPackageInstaller_SetPreflightError("STAGE_MKDIR")',
    'TitanPackageInstaller_SetPreflightError("STAGE_OCCUPIED")',
    'strncmp(prepare_message, "PKG preflight ", 14u)',
):
    if token not in pkg:
        errors.append("missing package-preflight token: " + token)

stage = pkg.split("case TITAN_PACKAGE_PREPARE_CREATE_STAGE:", 1)[1].split(
    "case TITAN_PACKAGE_PREPARE_READY:", 1
)[0]
if "errno == EEXIST" in pkg:
    errors.append("package installer still depends on FileXio errno/EEXIST")
if "TitanStorageIo_PathInfo64" not in stage:
    errors.append("stage generation is not probed explicitly before mkdir")

if "TITAN_STORAGE_PACKAGE_BUDGET (4u * 1024u)" not in sched:
    errors.append("fixed 4 KiB package quantum regressed")
if "TitanPackageInstaller_Update(" not in sched:
    errors.append("package installer update call missing from scheduler")
if "Package installation is intentionally NOT executed by this EE worker" not in sched:
    errors.append("worker-park ownership guard regressed")

if errors:
    for error in errors:
        print("rc1-package-preflight-storage: FAIL:", error)
    raise SystemExit(1)
print("rc1-package-preflight-storage: PASS alias recovery + errno-independent staging + precise failures + single-owner 4KiB lane")
