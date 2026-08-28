#!/usr/bin/env python3
"""Guard the explicit SDK/libultra-lineage compatibility inventory.

This project still carries SDK-shaped compatibility material for the in-progress
N64 matching target. That surface is intentional, documented, and should not
grow accidentally. This guard hard-fails when a tracked file appears under the
SDK-shaped directories without being added to the inventory below.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


SDK_COMPAT_DIRS = (
    "include/PR",
    "src/libultra",
    "src/libultrare",
)

LD_SECTION_FILES = (
    ("text", "ld/lib.text.ld.inc"),
    ("data", "ld/lib.data.ld.inc"),
    ("rodata", "ld/lib.rodata.ld.inc"),
    ("bss", "ld/lib.bss.ld.inc"),
)

ALLOWED_LINKER_AUDIO_OBJECT = "src/libultra/audio/clean_compat.o"

LINKER_AUDIO_REF_RE = re.compile(
    r"build/OUTCODE/"
    r"(?P<path>src/libultra(?:re)?/audio/(?P<object>[^/\s)]+)\.o)"
    r"\s+\(\.(?P<section>text|data|rodata|bss)\);"
)

ALLOWED_SDK_COMPAT_PATHS_TEXT = """
include/PR/R4300.h
include/PR/abi.h
include/PR/gbi.h
include/PR/gu.h
include/PR/libaudio.h
include/PR/mbi.h
include/PR/os.h
include/PR/os_internal.h
include/PR/rcp.h
include/PR/region.h
include/PR/sptask.h
include/PR/ucode.h
include/PR/ultratypes.h
src/libultra/audio/clean_compat.c
src/libultra/audio/seqp.h
src/libultra/audio/synthInternals.h
src/libultra/gu/align.c
src/libultra/gu/cosf.c
src/libultra/gu/coss.c
src/libultra/gu/guint.h
src/libultra/gu/libm_vals.s
src/libultra/gu/lookat.c
src/libultra/gu/lookatref.c
src/libultra/gu/mtxutil.c
src/libultra/gu/normalize.c
src/libultra/gu/ortho.c
src/libultra/gu/perspective.c
src/libultra/gu/rotate.c
src/libultra/gu/scale.c
src/libultra/gu/sinf.c
src/libultra/gu/sins.c
src/libultra/gu/sintable.h
src/libultra/gu/sqrtf.s
src/libultra/gu/translate.c
src/libultra/io/ai.c
src/libultra/io/aigetlen.c
src/libultra/io/aisetfreq.c
src/libultra/io/contpfs.c
src/libultra/io/contquery.c
src/libultra/io/controller.h
src/libultra/io/crc.c
src/libultra/io/dp.c
src/libultra/io/dpctr.c
src/libultra/io/dpsetnextbuf.c
src/libultra/io/dpsetstat.c
src/libultra/io/piacs.c
src/libultra/io/pidma.c
src/libultra/io/pigetcmdq.c
src/libultra/io/pigetstat.c
src/libultra/io/piint.h
src/libultra/io/pirawdma.c
src/libultra/io/pirawread.c
src/libultra/io/pirawwrite.c
src/libultra/io/piread.c
src/libultra/io/piwrite.c
src/libultra/io/si.c
src/libultra/io/siacs.c
src/libultra/io/siint.h
src/libultra/io/sirawdma.c
src/libultra/io/sirawread.c
src/libultra/io/sirawwrite.c
src/libultra/io/sp.c
src/libultra/io/spgetstat.c
src/libultra/io/sprawdma.c
src/libultra/io/spsetpc.c
src/libultra/io/spsetstat.c
src/libultra/io/sptaskyield.c
src/libultra/io/sptaskyielded.c
src/libultra/io/viblack.c
src/libultra/io/vigetcurrcontext.c
src/libultra/io/vigetcurrframebuf.c
src/libultra/io/vigetnextframebuf.c
src/libultra/io/viint.h
src/libultra/io/vimodentsclan1.c
src/libultra/io/virepeatline.c
src/libultra/io/visetevent.c
src/libultra/io/visetmode.c
src/libultra/io/visetspecial.c
src/libultra/io/visetxscale.c
src/libultra/io/visetyscale.c
src/libultra/io/viswapbuf.c
src/libultra/io/viswapcontext.c
src/libultra/libc/bcmp.s
src/libultra/libc/bcopy.s
src/libultra/libc/bzero.s
src/libultra/libc/ldiv.c
src/libultra/libc/ll.c
src/libultra/libc/llcvt.c
src/libultra/libc/string.c
src/libultra/libc/xldtob.c
src/libultra/libc/xlitob.c
src/libultra/libc/xstdio.h
src/libultra/os/createmesgqueue.c
src/libultra/os/createthread.c
src/libultra/os/exceptasm.h
src/libultra/os/exception.s
src/libultra/os/getcount.s
src/libultra/os/getcurrfaultthread.c
src/libultra/os/getfpccsr.s
src/libultra/os/getsr.s
src/libultra/os/getthreadpri.c
src/libultra/os/gettime.c
src/libultra/os/gettlbhi.s
src/libultra/os/interrupt.s
src/libultra/os/invaldcache.s
src/libultra/os/invalicache.s
src/libultra/os/jammesg.c
src/libultra/os/kdebugserver.c
src/libultra/os/osint.h
src/libultra/os/parameters.s
src/libultra/os/probetlb.s
src/libultra/os/recvmesg.c
src/libultra/os/resetglobalintmask.c
src/libultra/os/sendmesg.c
src/libultra/os/setcompare.s
src/libultra/os/seteventmesg.c
src/libultra/os/setfpccsr.s
src/libultra/os/sethwinterrupt.c
src/libultra/os/setintmask.s
src/libultra/os/setsr.s
src/libultra/os/setthreadpri.c
src/libultra/os/settimer.c
src/libultra/os/startthread.c
src/libultra/os/stopthread.c
src/libultra/os/thread.c
src/libultra/os/timerintr.c
src/libultra/os/unmaptlb.s
src/libultra/os/virtualtophysical.c
src/libultra/os/writebackdcache.s
src/libultra/os/writebackdcacheall.s
src/libultra/os/yieldthread.c
src/libultrare/Makefile.libultrare
src/libultrare/io/aisetnextbuf.c
src/libultrare/io/conteeplongread.c
src/libultrare/io/conteeplongwrite.c
src/libultrare/io/conteepprobe.c
src/libultrare/io/conteepread.c
src/libultrare/io/conteepwrite.c
src/libultrare/io/contramread.c
src/libultrare/io/contramwrite.c
src/libultrare/io/contreaddata.c
src/libultrare/io/controller.c
src/libultrare/io/devmgr.c
src/libultrare/io/epirawdma.c
src/libultrare/io/epirawwrite.c
src/libultrare/io/leodiskinit.c
src/libultrare/io/leointerrupt.c
src/libultrare/io/pfsinit.c
src/libultrare/io/pfsisplug.c
src/libultrare/io/pimgr.c
src/libultrare/io/sptask.c
src/libultrare/io/vi.c
src/libultrare/io/vimgr.c
src/libultrare/io/vimodepallan1.c
src/libultrare/io/vitbl.c
src/libultrare/libc/xprintf.c
src/libultrare/libultrare.h
src/libultrare/os/destroythread.c
src/libultrare/os/initialize.c
src/libultrare/ultra80069080.s
"""


def parse_allowed() -> set[str]:
    return {
        line.strip()
        for line in ALLOWED_SDK_COMPAT_PATHS_TEXT.splitlines()
        if line.strip() and not line.strip().startswith("#")
    }


def inventory_entries() -> list[str]:
    return [
        line.strip()
        for line in ALLOWED_SDK_COMPAT_PATHS_TEXT.splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]


def git_tracked_paths(root: Path):
    try:
        out = subprocess.check_output(
            ["git", "ls-files", *SDK_COMPAT_DIRS],
            cwd=root,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return sorted(line for line in out.splitlines() if line)


def filesystem_paths(root: Path) -> list[str]:
    paths: list[str] = []
    for dirname in SDK_COMPAT_DIRS:
        base = root / dirname
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file():
                paths.append(path.relative_to(root).as_posix())
    return sorted(paths)


def current_paths(root: Path) -> list[str]:
    tracked = git_tracked_paths(root)
    if tracked is not None:
        return tracked
    return filesystem_paths(root)


def live_linker_line(line: str) -> str:
    """Return the part of a linker-script line that is active object syntax."""
    active = line.split("/*", 1)[0].strip()
    if not active or active.startswith("#") or active.startswith("//"):
        return ""
    return active


def check_matching_libaudio_linker_surface(root: Path) -> list[str]:
    failures: list[str] = []

    for expected_section, relpath in LD_SECTION_FILES:
        path = root / relpath
        if not path.is_file():
            failures.append(f"missing matching-target linker section file: {relpath}")
            continue

        clean_sections: list[str] = []
        for lineno, raw_line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            match = LINKER_AUDIO_REF_RE.search(live_linker_line(raw_line))
            if not match:
                continue

            object_path = match.group("path")
            section = match.group("section")
            if object_path == ALLOWED_LINKER_AUDIO_OBJECT:
                clean_sections.append(section)
                continue

            failures.append(
                f"{relpath}:{lineno} references removed SDK/Rare libaudio object "
                f"{object_path}; matching-target audio must route through "
                f"{ALLOWED_LINKER_AUDIO_OBJECT}"
            )

        if clean_sections != [expected_section]:
            failures.append(
                f"{relpath} must contain exactly one "
                f"build/OUTCODE/{ALLOWED_LINKER_AUDIO_OBJECT} (.{expected_section}); "
                f"entry; found sections: {', '.join(clean_sections) or 'none'}"
            )

    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args(argv)

    root = Path(args.repo_root).resolve()
    entries = inventory_entries()
    allowed = set(entries)
    current = set(current_paths(root))

    extra = sorted(current - allowed)
    missing = sorted(path for path in allowed if not (root / path).is_file())
    duplicate_count = len(entries) - len(allowed)
    linker_failures = check_matching_libaudio_linker_surface(root)

    if duplicate_count:
        print("FAIL: duplicate SDK inventory entries detected", file=sys.stderr)
        return 1

    if extra or missing or linker_failures:
        if extra:
            print("FAIL: unreviewed SDK/libultra-lineage paths:", file=sys.stderr)
            for path in extra:
                print(f"  {path}", file=sys.stderr)
        if missing:
            print("FAIL: SDK inventory entries missing from tree:", file=sys.stderr)
            for path in missing:
                print(f"  {path}", file=sys.stderr)
        if linker_failures:
            print("FAIL: matching-target libaudio linker surface drifted:", file=sys.stderr)
            for failure in linker_failures:
                print(f"  {failure}", file=sys.stderr)
        print(
            "Update tools/check_sdk_inventory.py and the public provenance docs "
            "when intentionally adding or removing SDK-shaped compatibility files.",
            file=sys.stderr,
        )
        return 1

    print(f"PASS: SDK/libultra compatibility inventory matches {len(current)} tracked paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
