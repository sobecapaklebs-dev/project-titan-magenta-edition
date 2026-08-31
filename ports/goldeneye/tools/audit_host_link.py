#!/usr/bin/env python3
"""Compile and partially link the exact TitanEYE source list with host GCC.

This is a structural audit for missing declarations, duplicate symbols, image
budget and forbidden undefined imports. It does not replace the R5900 build.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


ALLOWED_UNDEFINED = {
    "_GLOBAL_OFFSET_TABLE_",  # x86 host ABI only; R5900 build is non-PIC
    "__isoc23_strtoull",     # host libc spelling; PS2 newlib exports strtoull
    "abs", "atoi", "ceilf", "cos", "cosf", "fabsf", "floorf", "lround",
    "memcmp", "memcpy", "memmove", "memset", "powf", "qsort", "sin", "sinf",
    "snprintf", "sqrt", "sqrtf", "strcat", "strchr", "strcmp", "strcpy",
    "strlen", "strncmp", "strncpy", "strstr", "strtod", "strtof", "strtol",
    "strtoul", "strtoull", "vsnprintf",
}

DEFINES = (
    "PROJECT_TITAN_TGR", "NATIVE_PORT", "NONMATCHING", "PORT_FIXME_STUBS",
    "PORT_SOUNDPLAYER_REAL", "_LANGUAGE_C", "VERSION_US", "LANG_US",
    "REFRESH_NTSC", "LEFTOVERDEBUG", "LEFTOVERSPECTRUM", "BUGFIX_R0",
    "BYTEMATCH",
)


def run(command: list[str], cwd: Path, capture: bool = False) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    return completed.stdout if capture else ""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="directory containing Makefile.titan",
    )
    parser.add_argument("--report", type=Path, help="write the summary here")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port = args.port_root.resolve()
    project = port.parents[1]
    makefile = port / "Makefile.titan"
    if not makefile.is_file():
        raise SystemExit(f"host-link-audit: missing {makefile}")

    for tool in ("make", "gcc", "ld", "nm", "size"):
        if shutil.which(tool) is None:
            raise SystemExit(f"host-link-audit: required host tool missing: {tool}")

    source_output = run(
        ["make", "-s", "-f", makefile.name, "print-sources"], port, capture=True
    )
    sources = [line.strip() for line in source_output.splitlines() if line.strip()]
    if len(sources) < 200:
        raise SystemExit(f"host-link-audit: incomplete source list ({len(sources)})")

    mgb = port / "mgb64"
    include_dirs = (
        mgb / "include", mgb / "include/PR", mgb / "src", mgb / "src/game",
        mgb / "src/platform", mgb / "src/platform/fast3d",
        mgb / "src/libultra/audio", mgb / "assets", mgb / "lib/glad/include",
        mgb, project / "kernel/include",
    )
    common = [
        "gcc", "-std=gnu11", "-Os", "-g0", "-w", "-U_FORTIFY_SOURCE",
        "-D_FORTIFY_SOURCE=0", "-fno-common", "-fno-builtin",
        "-fno-stack-protector", "-fms-extensions", "-Wno-unused-variable",
        "-Wno-unused-function", "-Wno-unused-but-set-variable",
        "-Wno-missing-braces", "-Wno-parentheses", "-Wno-unknown-pragmas",
        "-Wno-implicit-function-declaration", "-Wno-missing-declarations",
        "-Wno-int-conversion", "-Wno-incompatible-pointer-types",
        "-Wno-empty-body", "-Wno-switch",
    ]
    common.extend(f"-D{name}" for name in DEFINES)
    common.extend(f"-I{path}" for path in include_dirs)

    with tempfile.TemporaryDirectory(prefix="titaneye-host-audit-") as temporary:
        build = Path(temporary)
        # glibc hosts do not ship newlib's private sys/reent.h, while the
        # module intentionally owns one opaque _reent instance.  Supply the
        # smallest audit-only declaration; the target build still uses the
        # real PS2 newlib header.
        host_include = build / "host-include"
        (host_include / "sys").mkdir(parents=True)
        (host_include / "sys/reent.h").write_text(
            "#ifndef TITANEYE_HOST_REENT_H\n"
            "#define TITANEYE_HOST_REENT_H\n"
            "struct _reent { int _errno; };\n"
            "#endif\n",
            encoding="utf-8",
        )
        audit_common = common + [
            f"-I{host_include}",
            "-include", str(port / "titan_ge_stdio_compat.h"),
        ]
        objects: list[Path] = []
        for index, relative in enumerate(sources):
            source = port / relative
            if not source.is_file():
                raise SystemExit(f"host-link-audit: missing source: {relative}")
            obj = build / f"{index:03d}.o"
            run(audit_common + ["-c", str(source), "-o", str(obj)], port)
            objects.append(obj)

        image = build / "titaneye-host-audit.o"
        run(["ld", "-r", "-o", str(image), *(str(obj) for obj in objects)], port)

        undefined_output = run(["nm", "-u", str(image)], port, capture=True)
        undefined = sorted(
            {line.split()[-1] for line in undefined_output.splitlines() if line.split()}
        )
        forbidden = sorted(set(undefined) - ALLOWED_UNDEFINED)
        if forbidden:
            raise SystemExit(
                "host-link-audit: forbidden undefined imports: " + ", ".join(forbidden)
            )

        size_output = run(["size", "-B", str(image)], port, capture=True)
        size_line = size_output.splitlines()[-1].split()
        text_bytes, data_bytes, bss_bytes, total_bytes = map(int, size_line[:4])
        if total_bytes > 8 * 1024 * 1024:
            raise SystemExit(
                f"host-link-audit: relocated image budget exceeded ({total_bytes} bytes)"
            )

    report = "\n".join(
        (
            "TitanEYE host link audit: OK",
            f"sources={len(sources)}",
            f"text={text_bytes}",
            f"data={data_bytes}",
            f"bss={bss_bytes}",
            f"total={total_bytes}",
            "undefined=" + ",".join(undefined),
            "note=structural host audit; R5900 and hardware validation still required",
        )
    ) + "\n"
    print(report, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
