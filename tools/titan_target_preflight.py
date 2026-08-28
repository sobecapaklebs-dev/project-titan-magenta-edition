#!/usr/bin/env python3
"""Fail-fast validation of the external PS2 target environment used by BF30.

The release umbrella builds the Titan bootloader/kernel, integrated FMCB,
Project NES, wLaunchELF, Titan-Neutrino and Titan-UL.  This preflight checks
the external dependencies consumed by that *default* graph before the costly
host regression starts. Optional/debug-only backends remain Makefile-gated.
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"TARGET_PREFLIGHT_ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_file(path: Path, label: str, executable: bool = False) -> None:
    if not path.is_file():
        fail(f"missing {label}: {path}")
    if executable and not os.access(path, os.X_OK):
        fail(f"not executable {label}: {path}")


def usable_gskit(root: Path) -> bool:
    return (
        (root / "include/gsKit.h").is_file()
        and (root / "lib/libgskit.a").is_file()
        and (root / "lib/libdmakit.a").is_file()
    )


def resolve_gskit(ps2dev: Path, ps2sdk: Path) -> Path:
    candidates: list[Path] = []
    explicit = os.environ.get("GSKIT", "").strip()
    if explicit:
        candidates.append(Path(explicit).expanduser())
    candidates.extend((ps2dev / "gsKit", ps2sdk.parent / "gsKit", ps2dev.parent / "gsKit"))
    seen: set[str] = set()
    for candidate in candidates:
        try:
            candidate = candidate.resolve()
        except OSError:
            continue
        key = str(candidate)
        if key in seen:
            continue
        seen.add(key)
        if usable_gskit(candidate):
            return candidate

    # Match the bounded fallback used by build_titan_ul_engine.sh without
    # scanning outside PS2DEV.
    try:
        for header in ps2dev.glob("**/include/gsKit.h"):
            try:
                relative = header.relative_to(ps2dev)
            except ValueError:
                continue
            if len(relative.parts) > 6:  # equivalent to a bounded search depth
                continue
            candidate = header.parent.parent.resolve()
            if usable_gskit(candidate):
                return candidate
    except OSError:
        pass
    fail(
        "built gsKit not found; expected include/gsKit.h plus "
        "lib/libgskit.a and lib/libdmakit.a (set GSKIT to override)"
    )
    raise AssertionError("unreachable")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    ps2dev_raw = os.environ.get("PS2DEV", "")
    ps2sdk_raw = os.environ.get("PS2SDK", "")
    firmware_raw = os.environ.get("TITAN_AR9271_FW", "")
    if not ps2dev_raw or not ps2sdk_raw:
        fail("PS2DEV and PS2SDK must be exported")
    ps2dev = Path(ps2dev_raw).expanduser().resolve()
    ps2sdk = Path(ps2sdk_raw).expanduser().resolve()

    tools = [
        (ps2dev / "ee/bin/mips64r5900el-ps2-elf-gcc", "EE gcc"),
        (ps2dev / "ee/bin/mips64r5900el-ps2-elf-ar", "EE ar"),
        (ps2dev / "ee/bin/mips64r5900el-ps2-elf-strip", "EE strip"),
        (ps2dev / "iop/bin/mipsel-none-elf-gcc", "IOP gcc"),
        (ps2dev / "iop/bin/mipsel-none-elf-ar", "IOP ar"),
        (ps2dev / "iop/bin/mipsel-none-elf-strip", "IOP strip"),
        (ps2dev / "dvp/bin/dvp-as", "DVP assembler"),
        (ps2dev / "dvp/bin/dvp-objcopy", "DVP objcopy"),
        (ps2sdk / "bin/bin2c", "PS2SDK bin2c"),
    ]
    for path, label in tools:
        require_file(path, label, executable=True)
    for command in ("iopfixup", "ps2-packer"):
        if shutil.which(command) is None:
            fail(f"{command} is not available on PATH")

    for relative in (
        "Defs.make",
        "samples/Makefile.pref",
        "samples/Makefile.eeglobal",
        "samples/Makefile.iopglobal",
    ):
        require_file(ps2sdk / relative, f"PS2SDK build file {relative}")

    # Union of the unconditional/default IRX inputs used by bootloader/kernel,
    # Neutrino and UL.  Deliberately excluded: Neutrino MMCE (optional) and UL
    # debug-only UDPTTY/PPCTTY/IOPTRAP payloads.
    mandatory_irx = (
        "IEEE1394_bd.irx", "IEEE1394_bd_mini.irx", "ata_bd.irx",
        "audsrv.irx", "bdm.irx", "bdmfs_fatfs.irx", "eesync-nano.irx",
        "eesync.irx", "fileXio.irx", "freepad.irx", "freesio2.irx",
        "hdproatad.irx", "iLinkman.irx", "iomanX.irx", "libsd.irx",
        "mcman-old.irx", "mcman.irx", "mcserv-old.irx", "mcserv.irx",
        "mx4sio_bd.irx", "mx4sio_bd_mini.irx", "netman.irx",
        "poweroff.irx", "ps2atad.irx", "ps2dev9.irx", "ps2fs-osd.irx",
        "ps2fs.irx", "ps2hdd-bdm.irx", "ps2hdd-osd.irx", "ps2hdd.irx",
        "ps2ip-nm.irx", "ps2ips.irx", "ps2kbd.irx", "ps2mouse.irx",
        "smap.irx", "smbman.irx", "udnl-t300.irx", "udnl.irx",
        "usbd.irx", "usbd_mini.irx", "usbhdfsd.irx", "usbmass_bd.irx",
        "usbmass_bd_mini.irx",
    )
    irx_root = ps2sdk / "iop/irx"
    for name in mandatory_irx:
        require_file(irx_root / name, f"PS2SDK IRX {name}")

    # ps2sdk-ports dependencies consumed by the Titan kernel and default UL UI.
    ports_include = ps2sdk / "ports/include"
    ports_lib = ps2sdk / "ports/lib"
    for relative in (
        "png.h", "jpeglib.h", "freetype2/ft2build.h",
        "vorbis/vorbisfile.h", "ogg/ogg.h",
    ):
        require_file(ports_include / relative, f"ps2sdk-ports header {relative}")
    for name in (
        "libpng.a", "libjpeg.a", "libz.a", "libfreetype.a",
        "libvorbisfile.a", "libvorbis.a", "libogg.a",
    ):
        require_file(ports_lib / name, f"ps2sdk-ports library {name}")

    gskit = resolve_gskit(ps2dev, ps2sdk)

    for relative in (
        "third_party/fmcb/upstream/installer/irx/compiled/IOPRP.img",
        "third_party/fmcb/upstream/installer/irx/compiled/secrsif.irx",
        "third_party/fmcb/upstream/installer/irx/compiled/mctools.irx",
        "third_party/fmcb/resources/INSTALL/SYSTEM/FMCB.XLF",
    ):
        require_file(root / relative, f"FMCB release input {relative}")

    if not firmware_raw:
        fail("TITAN_AR9271_FW must point to the validated AR9271 firmware")
    firmware = Path(firmware_raw).expanduser().resolve()
    require_file(firmware, "TITAN_AR9271_FW")
    if firmware.stat().st_size == 0:
        fail(f"TITAN_AR9271_FW is empty: {firmware}")

    print(f"TARGET_PREFLIGHT_PS2DEV={ps2dev}")
    print(f"TARGET_PREFLIGHT_PS2SDK={ps2sdk}")
    print(f"TARGET_PREFLIGHT_GSKIT={gskit}")
    print(f"TARGET_PREFLIGHT_AR9271_FW={firmware}")
    print(f"TARGET_PREFLIGHT_IRX_COUNT={len(mandatory_irx)}")
    print("TARGET_PREFLIGHT_PASS=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
