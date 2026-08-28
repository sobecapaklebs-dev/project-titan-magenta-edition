#!/usr/bin/env python3
"""Build and verify Project Titan TINS v1 installation containers.

TINS is deliberately simple and streamable on the PS2:
- fixed 64-byte header
- fixed 224-byte entry table
- contiguous uncompressed payload
- CRC32 for the table and for every file

The installer can therefore validate/copy one file at a time without loading the
container into EE RAM.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

MAGIC = b"TINS"
FORMAT = 1
HEADER_SIZE = 64
ENTRY_SIZE = 224
PATH_BYTES = 192
MAX_FILES = 192
FLAG_AUTONOMOUS = 0x00000001
KIND_SYSTEM = 1
KIND_BOOT = 2
KIND_EXPLOIT = 3  # historical format value; not selected by BF30 FMCB production path
KIND_BOOTSTRAP = 4
KIND_NAMES = {
    KIND_SYSTEM: "SYSTEM",
    KIND_BOOT: "BOOT",
    KIND_EXPLOIT: "EXPLOIT",
    KIND_BOOTSTRAP: "BOOTSTRAP",
}
NAME_KINDS = {v: k for k, v in KIND_NAMES.items()}


@dataclass(frozen=True)
class InputEntry:
    kind: int
    path: str
    source: Path
    flags: int = 0


def crc32_bytes(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            crc = zlib.crc32(block, crc)
    return crc & 0xFFFFFFFF


def checked_path(value: str) -> str:
    p = PurePosixPath(value)
    if not value or value.startswith("/") or ":" in value or "|" in value or ".." in p.parts:
        raise ValueError(f"unsafe TINS path: {value!r}")
    encoded = value.encode("utf-8")
    if len(encoded) >= PATH_BYTES:
        raise ValueError(f"TINS path too long: {value!r}")
    return value


def build_container(output: Path, entries: Iterable[InputEntry], autonomous: bool = False) -> None:
    items = list(entries)
    if not 1 <= len(items) <= MAX_FILES:
        raise ValueError(f"TINS requires 1..{MAX_FILES} files")

    prepared: list[tuple[InputEntry, int, int, int]] = []
    payload_offset = 0
    for item in items:
        if item.kind not in KIND_NAMES:
            raise ValueError(f"invalid TINS kind: {item.kind}")
        checked_path(item.path)
        if not item.source.is_file():
            raise ValueError(f"TINS source not found: {item.source}")
        size = item.source.stat().st_size
        if size > 0xFFFFFFFF:
            raise ValueError(f"TINS file exceeds 32-bit size: {item.source}")
        crc = crc32_file(item.source)
        prepared.append((item, payload_offset, size, crc))
        payload_offset += size
        if payload_offset > 0xFFFFFFFF:
            raise ValueError("TINS payload exceeds 4 GiB")

    table = bytearray()
    for item, offset, size, crc in prepared:
        path_raw = item.path.encode("utf-8")
        raw = bytearray(ENTRY_SIZE)
        struct.pack_into("<IIIIII", raw, 0, item.kind, item.flags, offset, size, crc, len(path_raw))
        raw[24:24 + len(path_raw)] = path_raw
        table += raw

    table_offset = HEADER_SIZE
    data_offset = table_offset + len(table)
    total_size = data_offset + payload_offset
    flags = FLAG_AUTONOMOUS if autonomous else 0

    header = bytearray(HEADER_SIZE)
    header[:4] = MAGIC
    struct.pack_into(
        "<IIIIIIIII",
        header,
        4,
        FORMAT,
        HEADER_SIZE,
        ENTRY_SIZE,
        len(prepared),
        flags,
        table_offset,
        data_offset,
        total_size,
        crc32_bytes(table),
    )
    header[40:48] = b"PTME0200"

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as out:
        out.write(header)
        out.write(table)
        for item, _, _, _ in prepared:
            with item.source.open("rb") as src:
                for block in iter(lambda: src.read(1024 * 1024), b""):
                    out.write(block)


def read_container(path: Path) -> tuple[dict[str, int], list[dict[str, int | str]]]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError("invalid TINS magic")
    fmt, header_size, entry_size, count, flags, table_off, payload_off, total_size, table_crc = struct.unpack_from(
        "<IIIIIIIII", data, 4
    )
    if fmt != FORMAT or header_size != HEADER_SIZE or entry_size != ENTRY_SIZE:
        raise ValueError("unsupported TINS format")
    if not 1 <= count <= MAX_FILES:
        raise ValueError("invalid TINS file count")
    if table_off != HEADER_SIZE or payload_off != HEADER_SIZE + count * ENTRY_SIZE or total_size != len(data):
        raise ValueError("invalid TINS layout")
    table = data[table_off:payload_off]
    if crc32_bytes(table) != table_crc:
        raise ValueError("invalid TINS table CRC")

    entries: list[dict[str, int | str]] = []
    previous_end = 0
    for index in range(count):
        off = table_off + index * ENTRY_SIZE
        kind, entry_flags, rel_off, size, crc, path_len = struct.unpack_from("<IIIIII", data, off)
        if kind not in KIND_NAMES or not 0 < path_len < PATH_BYTES:
            raise ValueError(f"invalid TINS entry {index}")
        name = data[off + 24:off + 24 + path_len].decode("utf-8")
        checked_path(name)
        if rel_off < previous_end or payload_off + rel_off + size > total_size:
            raise ValueError(f"invalid TINS bounds in entry {index}")
        payload = data[payload_off + rel_off:payload_off + rel_off + size]
        if crc32_bytes(payload) != crc:
            raise ValueError(f"invalid TINS payload CRC in entry {index}")
        previous_end = rel_off + size
        entries.append({"kind": kind, "kind_name": KIND_NAMES[kind], "flags": entry_flags, "offset": rel_off, "size": size, "crc32": crc, "path": name})
    return {"format": fmt, "flags": flags, "count": count, "payload_offset": payload_off, "total_size": total_size}, entries



def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_release_metadata(container: Path, output_dir: Path) -> tuple[Path, Path]:
    header, entries = read_container(container)
    output_dir.mkdir(parents=True, exist_ok=True)
    digest = sha256_file(container)
    sha_path = output_dir / f"{container.name}.sha256"
    manifest_path = output_dir / f"{container.name}.manifest.json"
    sha_path.write_text(f"{digest}  {container.name}\n", encoding="utf-8")
    manifest = {
        "schema": "project-titan-system-package-v1",
        "container": container.name,
        "sha256": digest,
        "tins_format": header["format"],
        "flags": header["flags"],
        "file_count": header["count"],
        "total_size": header["total_size"],
        "entries": entries,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return sha_path, manifest_path

def parse_spec(spec: str) -> InputEntry:
    # KIND:target=local-file
    if ":" not in spec or "=" not in spec:
        raise ValueError("--file requires KIND:target=local-file")
    kind_name, rest = spec.split(":", 1)
    target, local = rest.split("=", 1)
    kind = NAME_KINDS.get(kind_name.upper())
    if kind is None:
        raise ValueError(f"unknown TINS kind: {kind_name}")
    return InputEntry(kind, checked_path(target), Path(local).resolve())


def main() -> int:
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--output", type=Path, required=True)
    b.add_argument("--file", action="append", required=True)
    b.add_argument("--autonomous", action="store_true")
    v = sp.add_parser("verify")
    v.add_argument("container", type=Path)
    r = sp.add_parser("release-metadata")
    r.add_argument("container", type=Path)
    r.add_argument("--output-dir", type=Path, required=True)
    args = ap.parse_args()

    if args.cmd == "build":
        entries = [parse_spec(spec) for spec in args.file]
        build_container(args.output, entries, args.autonomous)
        header, parsed = read_container(args.output)
        print(f"TINS v{header['format']}: {args.output} files={header['count']} bytes={header['total_size']}")
        return 0

    if args.cmd == "release-metadata":
        sha_path, manifest_path = write_release_metadata(args.container, args.output_dir)
        print(f"release metadata: {sha_path} {manifest_path}")
        return 0

    header, entries = read_container(args.container)
    print(f"valid TINS v{header['format']}: files={header['count']} bytes={header['total_size']} flags=0x{header['flags']:08X}")
    for entry in entries:
        print(f"  {entry['kind_name']} {entry['path']} {entry['size']} bytes crc={entry['crc32']:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
