#!/usr/bin/env python3
"""Build Project Titan Installer media using a single TINS system container."""
from __future__ import annotations

import argparse
import filecmp
import shutil
from pathlib import Path

from titan_tins import (
    InputEntry,
    KIND_BOOT,
    KIND_BOOTSTRAP,
    KIND_SYSTEM,
    build_container,
    read_container,
)


def runtime_entries(runtime: Path) -> list[InputEntry]:
    entries: list[InputEntry] = []
    for p in sorted(runtime.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(runtime).as_posix()
        # The root kernel.elf is canonical. The validated UL runtime, however,
        # deliberately ships both compatibility names and both must survive TINS.
        if rel == "TITAN/kernel.elf":
            continue
        entries.append(InputEntry(KIND_SYSTEM, rel, p))
    return entries


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--runtime", type=Path, help="built runtime package; default: dist/Project Titan Magenta Edition")
    ap.add_argument("--installer-elf", type=Path, help="default: <project>/titan-installer.elf")
    ap.add_argument("--bootloader-elf", type=Path, help="default: <project>/BOOT.ELF")
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--fmcb-build", type=Path, help="directory from tools/build_titan_fmcb.py; enables FMCB autonomous support")
    args = ap.parse_args()

    project = args.project.resolve()
    runtime = (args.runtime or project / "dist" / "Project Titan Magenta Edition").resolve()
    installer = (args.installer_elf or project / "titan-installer.elf").resolve()
    bootloader = (args.bootloader_elf or project / "BOOT.ELF").resolve()
    out = args.output.resolve()

    ul_engine = runtime / "TITAN/titan_ul_engine.elf"
    ul_loader = runtime / "TITAN/titan_ul_loader.elf"
    neutrino = runtime / "neutrino/neutrino.elf"
    required = [installer, bootloader, runtime / "kernel.elf", ul_engine, ul_loader, neutrino]
    missing = [str(x) for x in required if not x.is_file()]
    if missing:
        raise SystemExit("Missing required build artifact(s):\n  " + "\n  ".join(missing))
    if not filecmp.cmp(ul_engine, ul_loader, shallow=False):
        raise SystemExit(
            "Validated UL compatibility pair diverged: titan_ul_engine.elf and "
            "titan_ul_loader.elf must contain the same runtime bytes"
        )

    if out.exists():
        shutil.rmtree(out)
    root = out / "TITAN_INSTALL"
    root.mkdir(parents=True)
    shutil.copy2(installer, root / "titan-installer.elf")

    entries = runtime_entries(runtime)
    entries.append(InputEntry(KIND_BOOT, "BOOT.ELF", bootloader))
    autonomous = False

    if args.fmcb_build:
        fmcb_build = args.fmcb_build.resolve()
        titan_entry = fmcb_build / "titan-fmcb-entry.elf"
        fmcb_resources = project / "third_party/fmcb/resources/INSTALL"
        titan_cnf = project / "third_party/fmcb/titan/FREEMCB.TITAN.CNF"
        titan_marker = project / "third_party/fmcb/titan/TITAN_FMCB.MANAGED"
        integrated_lib = fmcb_build / "libtitanfmcb_integrated.a"
        required_fmcb = [integrated_lib, titan_entry, fmcb_resources / "SYSTEM/FMCB.XLF", titan_cnf, titan_marker]
        missing_fmcb = [str(x) for x in required_fmcb if not x.is_file()]
        if missing_fmcb:
            raise SystemExit("Incomplete FMCB autonomous build:\n  " + "\n  ".join(missing_fmcb))
        fmcb_media = root / "FMCB"
        shutil.copytree(fmcb_resources, fmcb_media / "INSTALL", dirs_exist_ok=True)
        shutil.copy2(titan_cnf, fmcb_media / "INSTALL/SYS-CONF/FREEMCB.CNF")
        # Conservative pre-write Memory Card budget. The integrated FMCB backend
        # still runs upstream CalculateRequiredSpace() before its own writes; this
        # metadata lets the Titan installer fail before staging any MC payload.
        fmcb_files = [p for p in (fmcb_media / "INSTALL").rglob("*") if p.is_file()]
        fmcb_bytes = sum(p.stat().st_size for p in fmcb_files)
        bootstrap_bytes = bootloader.stat().st_size + titan_entry.stat().st_size + titan_cnf.stat().st_size + titan_marker.stat().st_size
        file_overhead = (len(fmcb_files) + 16) * 2048
        fresh_required = fmcb_bytes + bootstrap_bytes + file_overhead + 131072
        adopt_required = bootstrap_bytes + (8 * 2048) + 65536
        (fmcb_media / "TITAN_SPACE.REQ").write_text(
            f"format=1\nfresh_required_bytes={fresh_required}\nadopt_required_bytes={adopt_required}\n",
            encoding="ascii",
        )
        entries.extend([
            InputEntry(KIND_BOOTSTRAP, "TITAN/BOOT/TITAN_FMCB_ENTRY.ELF", titan_entry),
            InputEntry(KIND_BOOTSTRAP, "SYS-CONF/FREEMCB.CNF", titan_cnf),
            InputEntry(KIND_BOOTSTRAP, "SYS-CONF/TITAN_FMCB.MANAGED", titan_marker),
        ])
        autonomous = True

    container = root / "TITAN_SYSTEM.TINS"
    build_container(container, entries, autonomous=autonomous)
    header, parsed = read_container(container)

    (root / "README.txt").write_text(
        "Project Titan Magenta Edition Installer TINS v1\n"
        "Launch mass:/TITAN_INSTALL/titan-installer.elf.\n"
        "All installation payload files are stored inside TITAN_SYSTEM.TINS.\n"
        "Semi-autonomous mode installs the Titan bootloader as mcX:/BOOT/BOOT.ELF.\n"
        "Existing BOOT.ELF is preserved as BOOT.TITAN.BAK before publication.\n"
        "Autonomous mode, when present, uses the Free McBoot engine internally inside this same Titan installer environment.\n"
        "No secondary FMCB installer is launched or packaged; unsupported/unknown consoles expose semi-autonomous mode only.\n"
        "Autonomous mode is fail-closed above ROM 2.20 and for unknown console identity.\n",
        encoding="utf-8",
    )
    print(f"Installer media created: {out}")
    print(f"TINS entries: {len(parsed)}")
    print(f"TINS bytes: {header['total_size']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
