#!/usr/bin/env python3
"""Project Titan patch database converter.

Converts common PCSX2 PNACH, OPL/PS2RD CHT, and simple RAW address/value
sources into Project Titan CHT or ENH databases.

The generated database format is intentionally text-only:

    Entry Name
    AAAAAAAA VVVVVVVV

ENH additionally accepts metadata comments immediately before a heading:

    # @auto
    # @experimental

Safety defaults are fail-closed. Placeholder values, unsupported PNACH types,
reserved Titan low-memory collisions, malformed lines, and catalog limits abort
conversion instead of producing a silently broken database.
"""

from __future__ import annotations

import argparse
import dataclasses
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
from typing import Iterable, Sequence

VERSION = "1.0.1"
STARTUP_RE = re.compile(r"^(?P<prefix>[A-Za-z0-9]{4})_(?P<a>\d{3})\.(?P<b>\d{2})$")
SERIAL_RE = re.compile(r"(?i)\b(?P<prefix>[A-Z0-9]{4})[-_ ]?(?P<digits>\d{5})\b")
PNACH_RE = re.compile(
    r"^\s*patch\s*=\s*(?P<enabled>[012])\s*,\s*(?P<cpu>[A-Za-z0-9]+)\s*,\s*"
    r"(?P<address>[0-9A-Fa-fXx?]{1,8})\s*,\s*(?P<ptype>[A-Za-z0-9_]+)\s*,\s*"
    r"(?P<value>[0-9A-Fa-fXx?]{1,16})\s*$",
    re.IGNORECASE,
)
RAW_RE = re.compile(r"^\s*(?:0x)?(?P<address>[0-9A-Fa-f]{8})\s+(?:0x)?(?P<value>[0-9A-Fa-f]{8})\s*$")
PLACEHOLDER_RE = re.compile(r"[Xx?]")
QUOTED_ID_RE = re.compile(r'^\s*".*?/ID\s+([A-Za-z0-9_\.]+)"\s*$', re.IGNORECASE)
COMMENT_ONLY_RE = re.compile(r"^\s*(?://|#|;)(?P<text>.*)$")

ENTRY_CAPACITY = 64
NAME_CAPACITY = 96  # includes NUL in C; max visible bytes = 95
CATALOG_CODE_CAPACITY = 2048
PAYLOAD_PAIR_CAPACITY = 254
PS2RD_MAX_HOOKS = 5
PS2RD_MAX_CODES = 250
DEFAULT_RESERVED = ((0x00080000, 0x00083FFF),)


class ConvertError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class Code:
    address: int
    value: int
    source: str = ""
    line: int = 0


@dataclasses.dataclass
class Entry:
    name: str
    codes: list[Code]
    source: str
    auto: bool = False
    experimental: bool = False
    pnach_enabled: bool | None = None


@dataclasses.dataclass
class WarningItem:
    code: str
    message: str
    source: str = ""
    line: int = 0


def split_inline_comment(line: str) -> tuple[str, str]:
    candidates: list[tuple[int, int]] = []
    for token in ("//", "#", ";"):
        pos = line.find(token)
        if pos >= 0:
            candidates.append((pos, len(token)))
    if not candidates:
        return line.rstrip(), ""
    pos, size = min(candidates, key=lambda x: x[0])
    return line[:pos].rstrip(), line[pos + size :].strip()


def normalize_startup(text: str) -> str:
    text = text.strip()
    if STARTUP_RE.match(text):
        return text.upper()
    m = SERIAL_RE.search(text)
    if not m:
        raise ConvertError(
            f"STARTUP inválido: {text!r}. Use o formato SLUS_210.50 ou serial SLUS-21050."
        )
    digits = m.group("digits")
    return f"{m.group('prefix').upper()}_{digits[:3]}.{digits[3:]}"


def startup_from_paths(paths: Sequence[Path]) -> str | None:
    found: set[str] = set()
    for p in paths:
        for candidate in (p.name, *[parent.name for parent in p.parents[:3]]):
            m = SERIAL_RE.search(candidate)
            if m:
                found.add(normalize_startup(m.group(0)))
    if len(found) == 1:
        return next(iter(found))
    if len(found) > 1:
        raise ConvertError("Mais de um serial/STARTUP foi detectado nos inputs: " + ", ".join(sorted(found)))
    return None


def humanize_stem(path: Path) -> str:
    stem = path.stem
    # Remove common CRC / serial prefixes and suffixes.
    stem = re.sub(r"(?i)^(?:[A-Z0-9]{4}[-_]\d{5}[-_]?)", "", stem)
    stem = re.sub(r"(?i)^[0-9A-F]{8}[_-]", "", stem)
    stem = re.sub(r"(?i)_(?:[0-9A-F]{8})_", " ", stem)
    stem = re.sub(r"(?i)_(?:graphics|cheats?|codes?|patch|pnach)$", "", stem)
    stem = re.sub(r"[_-]+", " ", stem).strip()
    if not stem:
        stem = path.stem
    return " ".join(w.capitalize() if w.islower() else w for w in stem.split())


def sanitize_name(name: str) -> str:
    name = re.sub(r"\s+", " ", name.strip())
    name = name.strip('"\'')
    if not name:
        name = "Unnamed Patch"
    encoded = name.encode("utf-8")
    if len(encoded) > NAME_CAPACITY - 1:
        # Preserve a valid UTF-8 prefix.
        encoded = encoded[: NAME_CAPACITY - 1]
        while True:
            try:
                name = encoded.decode("utf-8")
                break
            except UnicodeDecodeError:
                encoded = encoded[:-1]
        name = name.rstrip()
    return name


def parse_hex_exact(text: str, field: str, source: str, line: int) -> int:
    if PLACEHOLDER_RE.search(text):
        raise ConvertError(f"{source}:{line}: placeholder {field}={text!r}; preencha o valor antes de converter")
    try:
        return int(text, 16)
    except ValueError as exc:
        raise ConvertError(f"{source}:{line}: {field} hexadecimal inválido: {text!r}") from exc


def convert_pnach_code(ptype: str, address: int, value: int, source: str, line: int) -> Code:
    kind = ptype.lower()
    if kind == "extended":
        if address > 0xFFFFFFFF or value > 0xFFFFFFFF:
            raise ConvertError(f"{source}:{line}: extended exige address/value de 32 bits")
        return Code(address & 0xFFFFFFFF, value & 0xFFFFFFFF, source, line)

    # PCSX2 direct writes -> PS2RD RAW code types.
    if address > 0x01FFFFFF:
        raise ConvertError(
            f"{source}:{line}: endereço {address:08X} fora da faixa direta para PNACH {ptype}; "
            "use extended se o arquivo já contém um código RAW"
        )
    if kind == "byte":
        if value > 0xFF:
            raise ConvertError(f"{source}:{line}: byte value > FF")
        raw_address = address
    elif kind == "short":
        if value > 0xFFFF:
            raise ConvertError(f"{source}:{line}: short value > FFFF")
        raw_address = 0x10000000 | address
    elif kind in {"word", "long"}:
        if value > 0xFFFFFFFF:
            raise ConvertError(f"{source}:{line}: word value > FFFFFFFF")
        raw_address = 0x20000000 | address
    else:
        raise ConvertError(f"{source}:{line}: tipo PNACH não suportado no PS2RD: {ptype!r}")
    return Code(raw_address & 0xFFFFFFFF, value & 0xFFFFFFFF, source, line)


def meaningful_comment(text: str) -> bool:
    t = text.strip()
    if not t:
        return False
    low = t.lower()
    if low.startswith(("patch", "author", "credits", "source", "crc", "gametitle")):
        return False
    return True


def parse_pnach(path: Path) -> tuple[list[Entry], list[WarningItem]]:
    try:
        text = path.read_text(encoding="utf-8-sig", errors="strict")
    except UnicodeDecodeError:
        text = path.read_text(encoding="latin-1")

    entries: list[Entry] = []
    warnings: list[WarningItem] = []
    blocks: list[list[tuple[int, str]]] = []
    block: list[tuple[int, str]] = []
    pending_comments: list[str] = []

    def flush() -> None:
        nonlocal block, pending_comments
        if block:
            blocks.append(block)
        block = []
        pending_comments = []

    # Preserve comment-only lines inside a block by prefixing them to the first
    # code record. Blank lines are the safest generic separator used by PNACH
    # collections such as CodyB3's Burnout 3 files.
    staged_comment_lines: list[tuple[int, str]] = []
    for line_no, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if not stripped:
            flush()
            staged_comment_lines = []
            continue
        cm = COMMENT_ONLY_RE.match(raw)
        if cm and not PNACH_RE.match(stripped):
            staged_comment_lines.append((line_no, cm.group("text").strip()))
            continue
        core, _ = split_inline_comment(raw)
        if PNACH_RE.match(core.strip()):
            if staged_comment_lines:
                block.extend((ln, f"# {txt}") for ln, txt in staged_comment_lines)
                staged_comment_lines = []
            block.append((line_no, raw))
        else:
            # PNACH metadata is ignored, but an unknown non-comment line is
            # retained as a warning rather than converted as a Titan heading.
            low = stripped.lower()
            if low.startswith(("gametitle=", "comment=", "author=", "patches=")):
                continue
            warnings.append(WarningItem("PNACH_IGNORED_LINE", f"linha PNACH ignorada: {stripped}", str(path), line_no))
    flush()

    if not blocks:
        raise ConvertError(f"{path}: nenhum patch PNACH reconhecido")

    default_name = humanize_stem(path)
    for bi, raw_block in enumerate(blocks, 1):
        codes: list[Code] = []
        comments: list[str] = []
        enabled_values: list[int] = []
        for line_no, raw in raw_block:
            if raw.lstrip().startswith("#"):
                c = raw.lstrip()[1:].strip()
                if meaningful_comment(c):
                    comments.append(c)
                continue
            core, inline = split_inline_comment(raw)
            m = PNACH_RE.match(core.strip())
            if not m:
                continue
            if m.group("cpu").upper() != "EE":
                raise ConvertError(f"{path}:{line_no}: apenas patches EE são suportados; recebeu {m.group('cpu')}")
            address = parse_hex_exact(m.group("address"), "address", str(path), line_no)
            value = parse_hex_exact(m.group("value"), "value", str(path), line_no)
            code = convert_pnach_code(m.group("ptype"), address, value, str(path), line_no)
            codes.append(code)
            enabled_values.append(int(m.group("enabled")))
            if meaningful_comment(inline):
                comments.append(inline)

        if not codes:
            continue
        name = default_name
        # Prefer inline comments attached to actual patch lines over a generic
        # comment-only file/block banner. Then reject parameter-only labels.
        inline_comments: list[str] = []
        for line_no, raw in raw_block:
            if raw.lstrip().startswith("#"):
                continue
            _, inline = split_inline_comment(raw)
            if meaningful_comment(inline):
                inline_comments.append(inline)
        candidates = inline_comments + comments
        for candidate in candidates:
            if not re.match(r"(?i)^(?:float\s+value|default|value\b|line\b)", candidate):
                name = candidate
                break
        if len(blocks) > 1 and not comments:
            name = f"{default_name} {bi}"
        pnach_enabled: bool | None = None
        if enabled_values:
            pnach_enabled = any(v != 0 for v in enabled_values)
        entries.append(
            Entry(
                name=sanitize_name(name),
                codes=codes,
                source=str(path),
                pnach_enabled=pnach_enabled,
            )
        )
    return entries, warnings


def parse_raw_database(path: Path) -> tuple[list[Entry], list[WarningItem], str | None]:
    try:
        text = path.read_text(encoding="utf-8-sig", errors="strict")
    except UnicodeDecodeError:
        text = path.read_text(encoding="latin-1")

    entries: list[Entry] = []
    warnings: list[WarningItem] = []
    current: Entry | None = None
    detected_startup: str | None = None
    pending_auto = False
    pending_experimental = False

    for line_no, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if not stripped:
            continue
        q = QUOTED_ID_RE.match(stripped)
        if q:
            try:
                detected_startup = normalize_startup(q.group(1))
            except ConvertError:
                pass
            continue
        cm = COMMENT_ONLY_RE.match(stripped)
        if cm:
            meta = cm.group("text")
            low = meta.lower()
            if "@auto" in low:
                pending_auto = True
            if "@experimental" in low:
                pending_experimental = True
            continue
        core, _ = split_inline_comment(raw)
        m = RAW_RE.match(core.strip())
        if m:
            if current is None:
                current = Entry(humanize_stem(path), [], str(path), pending_auto, pending_experimental)
                entries.append(current)
                pending_auto = pending_experimental = False
            current.codes.append(
                Code(int(m.group("address"), 16), int(m.group("value"), 16), str(path), line_no)
            )
            continue
        # Treat non-code text as an OPL/Titan entry heading.
        current = Entry(sanitize_name(core.strip()), [], str(path), pending_auto, pending_experimental)
        entries.append(current)
        pending_auto = pending_experimental = False

    entries = [e for e in entries if e.codes]
    if not entries:
        raise ConvertError(f"{path}: nenhum par RAW AAAAAAAA VVVVVVVV encontrado")
    return entries, warnings, detected_startup


def parse_input(path: Path) -> tuple[list[Entry], list[WarningItem], str | None]:
    suffix = path.suffix.lower()
    if suffix == ".pnach":
        entries, warnings = parse_pnach(path)
        return entries, warnings, None
    return parse_raw_database(path)


def dedupe_names(entries: list[Entry]) -> None:
    used: dict[str, int] = {}
    for entry in entries:
        base = sanitize_name(entry.name)
        key = base.casefold()
        count = used.get(key, 0) + 1
        used[key] = count
        if count > 1:
            entry.name = sanitize_name(f"{base} ({count})")
        else:
            entry.name = base


def is_master_name(name: str) -> bool:
    low = name.casefold()
    needles = (
        "master code", "mastercode", "enable code", "must be on",
        "codigo mestre", "código mestre", "codigo maestro", "código maestro",
    )
    return any(n in low for n in needles)


def parse_range(text: str) -> tuple[int, int]:
    try:
        a, b = text.split(":", 1)
        start = int(a, 0)
        end = int(b, 0)
    except Exception as exc:
        raise argparse.ArgumentTypeError("range deve ser START:END, ex. 0x80000:0x83fff") from exc
    if not (0 <= start <= end <= 0xFFFFFFFF):
        raise argparse.ArgumentTypeError("range inválido")
    return start, end


def code_target_address(address: int) -> int | None:
    # PS2RD common write/conditional code families encode target address in
    # the lower 25 bits. This is used only as a conservative collision audit.
    top = (address >> 28) & 0xF
    if top in {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xD, 0xE}:
        return address & 0x01FFFFFF
    return None


def mips_jump_target(value: int) -> int | None:
    opcode = (value >> 26) & 0x3F
    if opcode in {2, 3}:  # J / JAL
        return (value & 0x03FFFFFF) << 2
    return None


def in_ranges(value: int, ranges: Sequence[tuple[int, int]]) -> tuple[int, int] | None:
    for start, end in ranges:
        if start <= value <= end:
            return start, end
    return None


def validate_safety(
    entries: list[Entry],
    reserved: Sequence[tuple[int, int]],
    *,
    allow_reserved: bool,
) -> list[WarningItem]:
    warnings: list[WarningItem] = []
    if len(entries) > ENTRY_CAPACITY:
        raise ConvertError(f"catálogo tem {len(entries)} entradas; Titan suporta no máximo {ENTRY_CAPACITY}")
    total_codes = sum(len(e.codes) for e in entries)
    if total_codes > CATALOG_CODE_CAPACITY:
        raise ConvertError(f"catálogo tem {total_codes} pares; limite de banco é {CATALOG_CODE_CAPACITY}")

    for entry in entries:
        for code in entry.codes:
            target = code_target_address(code.address)
            if target is not None:
                hit = in_ranges(target, reserved)
                if hit:
                    message = (
                        f"{entry.name}: código {code.address:08X} {code.value:08X} escreve/consulta "
                        f"0x{target:08X}, dentro da faixa Titan 0x{hit[0]:08X}-0x{hit[1]:08X}"
                    )
                    if not allow_reserved:
                        raise ConvertError(f"{code.source}:{code.line}: {message}; use --allow-reserved somente após relocação/auditoria")
                    warnings.append(WarningItem("RESERVED_ADDRESS", message, code.source, code.line))
            jump = mips_jump_target(code.value)
            if jump is not None:
                hit = in_ranges(jump, reserved)
                if hit:
                    message = (
                        f"{entry.name}: valor {code.value:08X} contém J/JAL para 0x{jump:08X}, "
                        f"dentro da faixa Titan 0x{hit[0]:08X}-0x{hit[1]:08X}"
                    )
                    if not allow_reserved:
                        raise ConvertError(f"{code.source}:{code.line}: {message}; code cave precisa ser relocada")
                    warnings.append(WarningItem("RESERVED_JUMP_TARGET", message, code.source, code.line))

    # Aggregate budget is not a catalog error because entries are toggled
    # independently, but report what would happen if everything were enabled.
    ordered = [code for e in entries for code in e.codes]
    hooks = 0
    regular = 0
    next_code_can_be_hook = True
    for code in ordered:
        address = code.address
        if (address & 0xFE000000) == 0x90000000 and next_code_can_be_hook:
            hooks += 1
        else:
            regular += 1
        next_code_can_be_hook = (address & 0xF0000000) in {0x40000000, 0x30000000}
    if len(ordered) > PAYLOAD_PAIR_CAPACITY or hooks > PS2RD_MAX_HOOKS or regular > PS2RD_MAX_CODES:
        warnings.append(
            WarningItem(
                "ALL_ENABLED_OVERFLOW",
                f"se todas as entradas forem ligadas ao mesmo tempo: pairs={len(ordered)}, hooks={hooks}, regular={regular}; "
                f"runtime aceita no máximo pairs={PAYLOAD_PAIR_CAPACITY}, hooks={PS2RD_MAX_HOOKS}, regular={PS2RD_MAX_CODES}",
            )
        )
    return warnings


def matches_any(name: str, patterns: Sequence[str]) -> bool:
    low = name.casefold()
    return any(fnmatch.fnmatch(low, p.casefold()) for p in patterns)


def apply_metadata(entries: list[Entry], args: argparse.Namespace) -> None:
    if args.kind == "cheat":
        # CHT is always opt-in in Titan; metadata is intentionally ignored.
        for e in entries:
            e.auto = False
            e.experimental = False
        return
    for e in entries:
        if args.auto_all or matches_any(e.name, args.auto_entry):
            e.auto = True
        if args.experimental_all or matches_any(e.name, args.experimental_entry):
            e.experimental = True
        if args.pnach_enabled_as_auto and e.pnach_enabled:
            e.auto = True
        # Experimental always wins over auto at runtime; preserving @auto is
        # useful metadata but it will not auto-enable.


def render_database(startup: str, kind: str, entries: Sequence[Entry]) -> str:
    ext = "CHT" if kind == "cheat" else "ENH"
    lines: list[str] = [
        f"# Project Titan {ext} database",
        f"# STARTUP: {startup}",
        "# Generated by titan_patch_convert.py",
        "",
    ]
    for entry in entries:
        if kind == "enhancement":
            if entry.auto:
                lines.append("# @auto")
            if entry.experimental:
                lines.append("# @experimental")
        lines.append(entry.name)
        for code in entry.codes:
            lines.append(f"{code.address:08X} {code.value:08X}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def collect_inputs(raw_inputs: Sequence[str], recursive: bool) -> list[Path]:
    result: list[Path] = []
    for item in raw_inputs:
        p = Path(item).expanduser()
        if not p.exists():
            raise ConvertError(f"input não existe: {p}")
        if p.is_dir():
            pattern = "**/*" if recursive else "*"
            for child in sorted(p.glob(pattern)):
                if child.is_file() and child.suffix.lower() in {".pnach", ".cht", ".enh", ".raw", ".txt"}:
                    result.append(child)
        elif p.is_file():
            result.append(p)
    # De-duplicate resolved paths while keeping order.
    out: list[Path] = []
    seen: set[Path] = set()
    for p in result:
        rp = p.resolve()
        if rp not in seen:
            seen.add(rp)
            out.append(rp)
    if not out:
        raise ConvertError("nenhum arquivo suportado encontrado")
    return out


def build_report(startup: str, kind: str, inputs: Sequence[Path], entries: Sequence[Entry], warnings: Sequence[WarningItem], output: Path) -> dict:
    return {
        "tool": "titan_patch_convert.py",
        "version": VERSION,
        "startup": startup,
        "kind": kind,
        "output": str(output),
        "output_sha256": sha256_file(output),
        "inputs": [str(p) for p in inputs],
        "entry_count": len(entries),
        "pair_count_catalog": sum(len(e.codes) for e in entries),
        "entries": [
            {
                "name": e.name,
                "pairs": len(e.codes),
                "auto": e.auto,
                "experimental": e.experimental,
                "master": is_master_name(e.name),
                "source": e.source,
            }
            for e in entries
        ],
        "warnings": [dataclasses.asdict(w) for w in warnings],
    }


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="Converte PNACH/OPL CHT/RAW para bancos CHT/ENH do Project Titan.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("inputs", nargs="+", help="arquivo(s) ou diretório(s) de entrada")
    ap.add_argument("--kind", choices=("cheat", "enhancement", "enh"), required=True)
    ap.add_argument("--startup", help="STARTUP, ex. SLUS_210.50; se omitido tenta detectar pelo nome/ID")
    ap.add_argument("-o", "--output", help="arquivo de saída; default <STARTUP>.cht/.enh")
    ap.add_argument("--recursive", action="store_true", help="varrer diretórios recursivamente")
    ap.add_argument("--install", metavar="USB_ROOT", help="copiar resultado para USB_ROOT/CHT ou USB_ROOT/ENH com SHA")
    ap.add_argument("--auto-all", action="store_true", help="ENH: marcar todas as entradas como @auto")
    ap.add_argument("--auto-entry", action="append", default=[], metavar="GLOB", help="ENH: marcar nomes que casam com glob como @auto")
    ap.add_argument("--experimental-all", action="store_true", help="ENH: marcar todas como @experimental")
    ap.add_argument("--experimental-entry", action="append", default=[], metavar="GLOB", help="ENH: marcar nomes que casam com glob como @experimental")
    ap.add_argument("--pnach-enabled-as-auto", action="store_true", help="ENH: patch=1 do PNACH vira @auto")
    ap.add_argument("--reserved", action="append", type=parse_range, default=[], metavar="START:END", help="faixa extra de RAM reservada")
    ap.add_argument("--allow-reserved", action="store_true", help="permitir colisões de RAM, emitindo warning (perigoso)")
    ap.add_argument("--report", help="salvar relatório JSON")
    ap.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")
    return ap


def main(argv: Sequence[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    if args.kind == "enh":
        args.kind = "enhancement"
    try:
        inputs = collect_inputs(args.inputs, args.recursive)
        entries: list[Entry] = []
        warnings: list[WarningItem] = []
        detected_startups: set[str] = set()
        for p in inputs:
            parsed, w, detected = parse_input(p)
            entries.extend(parsed)
            warnings.extend(w)
            if detected:
                detected_startups.add(detected)

        startup = normalize_startup(args.startup) if args.startup else None
        path_startup = startup_from_paths(inputs)
        if startup is None:
            if len(detected_startups) == 1:
                startup = next(iter(detected_startups))
            elif len(detected_startups) > 1:
                raise ConvertError("inputs CHT têm STARTUPs diferentes: " + ", ".join(sorted(detected_startups)))
            elif path_startup:
                startup = path_startup
            else:
                raise ConvertError("não consegui detectar STARTUP; informe --startup SLUS_210.50")
        else:
            for detected in detected_startups:
                if detected != startup:
                    raise ConvertError(f"STARTUP solicitado {startup} difere do /ID encontrado no input: {detected}")
            if path_startup and path_startup != startup:
                warnings.append(WarningItem("SERIAL_NAME_MISMATCH", f"nome do input sugere {path_startup}, mas --startup={startup}"))

        dedupe_names(entries)
        apply_metadata(entries, args)
        reserved = list(DEFAULT_RESERVED) + list(args.reserved)
        warnings.extend(validate_safety(entries, reserved, allow_reserved=args.allow_reserved))

        extension = ".cht" if args.kind == "cheat" else ".enh"
        output = Path(args.output).expanduser() if args.output else Path.cwd() / f"{startup}{extension}"
        output.parent.mkdir(parents=True, exist_ok=True)
        content = render_database(startup, args.kind, entries)
        if len(content.encode("utf-8")) > 128 * 1024:
            raise ConvertError("arquivo gerado excede o limite de texto de 128 KiB do Titan")
        output.write_text(content, encoding="utf-8", newline="\n")

        install_path: Path | None = None
        if args.install:
            root = Path(args.install).expanduser()
            if not root.is_dir():
                raise ConvertError(f"USB_ROOT não é diretório montado: {root}")
            sub = "CHT" if args.kind == "cheat" else "ENH"
            dest_dir = root / sub
            dest_dir.mkdir(parents=True, exist_ok=True)
            install_path = dest_dir / f"{startup}{extension}"
            temp = dest_dir / (output.name + ".titan-new")
            shutil.copy2(output, temp)
            if sha256_file(temp) != sha256_file(output):
                temp.unlink(missing_ok=True)
                raise ConvertError("SHA divergiu durante staging no USB")
            os.replace(temp, install_path)
            try:
                os.sync()
            except AttributeError:
                pass
            if sha256_file(install_path) != sha256_file(output):
                raise ConvertError("SHA divergiu após publicação no USB")

        report = build_report(startup, args.kind, inputs, entries, warnings, output)
        if install_path:
            report["installed_to"] = str(install_path)
            report["installed_sha256"] = sha256_file(install_path)
        report_path = Path(args.report).expanduser() if args.report else output.with_suffix(output.suffix + ".report.json")
        report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

        print(f"TITAN_PATCH_CONVERT=PASS")
        print(f"STARTUP={startup}")
        print(f"KIND={args.kind}")
        print(f"ENTRIES={len(entries)}")
        print(f"CATALOG_PAIRS={sum(len(e.codes) for e in entries)}")
        print(f"OUTPUT={output}")
        print(f"OUTPUT_SHA256={sha256_file(output)}")
        print(f"REPORT={report_path}")
        if install_path:
            print(f"INSTALLED_TO={install_path}")
            print(f"USB_SHA256={sha256_file(install_path)}")
        for w in warnings:
            loc = f"{w.source}:{w.line}: " if w.source else ""
            print(f"WARNING[{w.code}] {loc}{w.message}", file=sys.stderr)
        return 0
    except ConvertError as exc:
        print(f"ERRO: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
