#!/usr/bin/env python3
"""Build and verify Project Titan TPKG v1/v2 application packages.

TPKG v1 is the legacy external-ELF format. TPKG v2 adds Titan-native and
Titan-game .tmod applications that run inside the Project Titan process.
"""

from __future__ import annotations

import argparse
import binascii
import struct
from pathlib import Path, PurePosixPath

MAGIC = b"TPKG"
FORMAT_V1 = 1
FORMAT_V2 = 2
HEADER_SIZE = 256
ENTRY_SIZE = 144
MAX_FILES = 64
MAX_PACKAGE_SIZE = 256 * 1024 * 1024
ENTRYPOINT_V1_CAPACITY = 104
ENTRYPOINT_V2_CAPACITY = 96
APP_EXTERNAL_ELF = 0
APP_TITAN_NATIVE = 1
APP_SYSTEM_BROWSER = 2
APP_THEME = 3
APP_TITAN_GAME = 4
SYSTEM_BROWSER_ENTRYPOINT = "@system-browser"
THEME_ENTRYPOINT = "@theme"
TITAN_APP_API_VERSION = 1
ICON_NAME = "icon.rgb565"
TILE_NAME = "tile.rgb565"
BACKGROUND_NAME = "background.rgb565"
META_NAME = "titan_meta.ini"
ICON_BYTES = 64 * 64 * 2
TILE_BYTES = 256 * 256 * 2
APP_BACKGROUND_PRIMARY_BYTES = 640 * 480 * 2
APP_BACKGROUND_LEGACY_BYTES = 512 * 256 * 2
# scene-v2 themes retain their established storage contract. Application
# live-area backgrounds use the 480-line primary contract above.
THEME_SCENE_BYTES = 512 * 256 * 2
THEME_CONFIG_NAME = "theme.cfg"
THEME_PREVIEW_NAME = "preview.rgb565"
THEME_PREVIEW_BYTES = 256 * 128 * 2


def checked_text(value: str, capacity: int, field: str, allow_empty: bool = False) -> bytes:
    encoded = value.encode("utf-8")
    if (not allow_empty and not encoded) or len(encoded) >= capacity or any(c in value for c in "\r\n="):
        raise ValueError(f"invalid {field}")
    return encoded + bytes(capacity - len(encoded))


def checked_metadata_line(value: str, field: str, capacity: int) -> str:
    checked_text(value, capacity, field, allow_empty=True)
    return value


def checked_title_id(value: str) -> bytes:
    if not 3 <= len(value) < 16 or any(
        not (character.isascii() and (character.isupper() or character.isdigit()))
        and character not in "_-"
        for character in value
    ):
        raise ValueError("title-id must use 3-15 uppercase ASCII letters, digits, _ or -")
    return checked_text(value, 16, "title-id")


def checked_relative(path: PurePosixPath) -> str:
    text = path.as_posix()
    if (
        path.is_absolute()
        or not text
        or len(text.encode("utf-8")) >= 128
        or any(part in ("", ".", "..") for part in path.parts)
        or ":" in text
        or "\\" in text
    ):
        raise ValueError(f"unsafe package path: {text}")
    return text


def checked_entrypoint(value: str, app_type: int) -> str:
    if app_type == APP_SYSTEM_BROWSER:
        if value != SYSTEM_BROWSER_ENTRYPOINT:
            raise ValueError(f"system-browser entrypoint must be {SYSTEM_BROWSER_ENTRYPOINT}")
        return value
    if app_type == APP_THEME:
        if value != THEME_ENTRYPOINT:
            raise ValueError(f"theme entrypoint must be {THEME_ENTRYPOINT}")
        return value
    entrypoint = checked_relative(PurePosixPath(value))
    if "/" in entrypoint:
        raise ValueError("entrypoint must be in the package root")
    suffix = ".tmod" if app_type in (APP_TITAN_NATIVE, APP_TITAN_GAME) else ".elf"
    if not entrypoint.lower().endswith(suffix):
        raise ValueError(f"entrypoint must be a root-level {suffix} filename")
    cap = ENTRYPOINT_V2_CAPACITY if app_type in (APP_TITAN_NATIVE, APP_TITAN_GAME) else ENTRYPOINT_V1_CAPACITY
    if len(entrypoint.encode("utf-8")) >= cap:
        raise ValueError("entrypoint filename is too long")
    return entrypoint


def write_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def header_checksum(header: bytearray) -> int:
    copy = bytearray(header)
    copy[36:40] = bytes(4)
    return binascii.crc32(copy) & 0xFFFFFFFF


def collect_files(source: Path, generated_meta: bytes | None) -> list[tuple[str, bytes]]:
    files: list[tuple[str, bytes]] = []
    for candidate in sorted(source.rglob("*")):
        if not candidate.is_file():
            continue
        relative = checked_relative(PurePosixPath(candidate.relative_to(source).as_posix()))
        if relative in ("titan_app.ini", "titan_app.ini.tmp"):
            raise ValueError(f"reserved package path: {relative}")
        if generated_meta is not None and relative == META_NAME:
            raise ValueError(f"{META_NAME} is generated from --author/--description; remove the source copy")
        files.append((relative, candidate.read_bytes()))
    if generated_meta is not None:
        files.append((META_NAME, generated_meta))
        files.sort(key=lambda item: item[0])
    if not files or len(files) > MAX_FILES:
        raise ValueError(f"package requires 1-{MAX_FILES} files")
    return files


def validate_assets(files: list[tuple[str, bytes]]) -> None:
    by_name = dict(files)
    if ICON_NAME in by_name and len(by_name[ICON_NAME]) != ICON_BYTES:
        raise ValueError(f"{ICON_NAME} must be exactly 64x64 RGB565 ({ICON_BYTES} bytes)")
    if TILE_NAME in by_name and len(by_name[TILE_NAME]) != TILE_BYTES:
        raise ValueError(f"{TILE_NAME} must be exactly 256x256 RGB565 ({TILE_BYTES} bytes)")
    if BACKGROUND_NAME in by_name and len(by_name[BACKGROUND_NAME]) not in (
        APP_BACKGROUND_PRIMARY_BYTES, APP_BACKGROUND_LEGACY_BYTES
    ):
        raise ValueError(
            f"{BACKGROUND_NAME} must be 640x480 RGB565 primary "
            f"({APP_BACKGROUND_PRIMARY_BYTES} bytes) or 512x256 RGB565 legacy fallback "
            f"({APP_BACKGROUND_LEGACY_BYTES} bytes)"
        )


def parse_theme_manifest(contents: bytes) -> dict[str, str]:
    try:
        text = contents.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValueError("theme.cfg must be UTF-8") from exc
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def validate_theme_manifest(files: list[tuple[str, bytes]], expected_title_id: str | None = None) -> None:
    by_name = dict(files)
    cfg = parse_theme_manifest(by_name[THEME_CONFIG_NAME])
    if cfg.get("format") != "1":
        raise ValueError("theme manifest format must be 1")
    manifest_id = cfg.get("id", "")
    checked_title_id(manifest_id)
    if expected_title_id is not None and manifest_id != expected_title_id:
        raise ValueError("theme manifest id must match package title-id")
    name = cfg.get("name", "")
    checked_text(name, 64, "theme name")
    shell_api = cfg.get("shell_api")
    if shell_api not in ("1", "2", "3", "4"):
        raise ValueError("theme shell_api must be 1, 2, 3 or 4")
    engine = cfg.get("background_engine")
    if engine not in ("default", "flow3d-v1", "ribbon3d-v1", "ribbon3d-v2", "scene-v2", "scene-v4"):
        raise ValueError("unsupported theme background_engine")
    if engine == "scene-v4":
        if shell_api != "4":
            raise ValueError("scene-v4 themes require shell_api=4")
        manifest = cfg.get("scene_v4_manifest", "")
        if not manifest:
            raise ValueError("scene-v4 theme is missing scene_v4_manifest")
        manifest = checked_relative(PurePosixPath(manifest))
        if manifest not in by_name:
            raise ValueError(f"scene-v4 manifest is missing: {manifest}")
        if len(by_name[manifest]) > 65536:
            raise ValueError("scene-v4 manifest exceeds 64 KiB")
        return
    if engine != "scene-v2":
        return
    if shell_api not in ("2", "3"):
        raise ValueError("scene-v2 themes require shell_api=2 or shell_api=3")

    def require_asset(key: str, expected_size: int | None = None) -> str:
        value = cfg.get(key, "")
        if not value:
            raise ValueError(f"scene-v2 theme is missing {key}")
        relative = checked_relative(PurePosixPath(value))
        if relative not in by_name:
            raise ValueError(f"theme asset declared by {key} is missing: {relative}")
        if expected_size is not None and len(by_name[relative]) != expected_size:
            raise ValueError(f"{relative} must be exactly 512x256 RGB565 ({expected_size} bytes)")
        return relative

    require_asset("scene_home", THEME_SCENE_BYTES)
    require_asset("scene_top", THEME_SCENE_BYTES)
    try:
        transition_count = int(cfg.get("scene_transition_count", "0"), 10)
    except ValueError as exc:
        raise ValueError("invalid scene_transition_count") from exc
    if not 0 <= transition_count <= 6:
        raise ValueError("scene_transition_count must be 0..6")
    for index in range(transition_count):
        require_asset(f"scene_transition_{index}", THEME_SCENE_BYTES)

    def parse_idle_count(key: str) -> int:
        try:
            value = int(cfg.get(key, "0"), 10)
        except ValueError as exc:
            raise ValueError(f"invalid {key}") from exc
        if not 0 <= value <= 8:
            raise ValueError(f"{key} must be 0..8")
        return value

    home_idle_count = parse_idle_count("scene_home_idle_count")
    top_idle_count = parse_idle_count("scene_top_idle_count")
    if home_idle_count + top_idle_count > 8:
        raise ValueError("scene idle frame budget exceeded: HOME + TOP additional frames must be <= 8")
    if (home_idle_count or top_idle_count) and shell_api != "3":
        raise ValueError("scene-v2 idle loops require shell_api=3")
    for state, count in (("home", home_idle_count), ("top", top_idle_count)):
        for index in range(count):
            require_asset(f"scene_{state}_idle_{index}", THEME_SCENE_BYTES)
        if count:
            ms_key = f"scene_{state}_idle_ms"
            try:
                frame_ms = int(cfg.get(ms_key, "120"), 10)
            except ValueError as exc:
                raise ValueError(f"invalid {ms_key}") from exc
            if not 40 <= frame_ms <= 2000:
                raise ValueError(f"{ms_key} must be 40..2000")
            mode = cfg.get(f"scene_{state}_idle_mode", "loop")
            if mode not in ("loop", "pingpong"):
                raise ValueError(f"scene_{state}_idle_mode must be loop or pingpong")

    for key in (
        "audio_music", "audio_notification", "audio_profile_entry",
        "audio_home_nav", "audio_top_nav", "audio_interface_nav",
        "audio_top_enter", "audio_shutdown",
    ):
        value = cfg.get(key, "")
        if value:
            relative = checked_relative(PurePosixPath(value))
            if relative not in by_name:
                raise ValueError(f"theme audio declared by {key} is missing: {relative}")
            if PurePosixPath(relative).suffix.lower() != ".wav":
                raise ValueError(f"theme audio must be WAV: {relative}")


def build(args: argparse.Namespace) -> None:
    source = args.source.resolve()
    if not source.is_dir():
        raise ValueError(f"source directory not found: {source}")

    if args.type == "titan-native":
        app_type = APP_TITAN_NATIVE
    elif args.type == "titan-game":
        app_type = APP_TITAN_GAME
    elif args.type == "system-browser":
        app_type = APP_SYSTEM_BROWSER
    elif args.type == "theme":
        app_type = APP_THEME
    else:
        app_type = APP_EXTERNAL_ELF
    format_version = FORMAT_V2 if app_type != APP_EXTERNAL_ELF or args.format == 2 else FORMAT_V1
    if app_type in (APP_TITAN_NATIVE, APP_TITAN_GAME, APP_SYSTEM_BROWSER, APP_THEME) and format_version != FORMAT_V2:
        raise ValueError("Titan-native/game/system/theme packages require TPKG v2")

    entrypoint_arg = args.entrypoint or args.executable
    if app_type == APP_SYSTEM_BROWSER and not entrypoint_arg:
        entrypoint_arg = SYSTEM_BROWSER_ENTRYPOINT
    if app_type == APP_THEME and not entrypoint_arg:
        entrypoint_arg = THEME_ENTRYPOINT
    if not entrypoint_arg:
        raise ValueError("--entrypoint is required (legacy ELF builds may use --executable)")
    entrypoint = checked_entrypoint(entrypoint_arg, app_type)

    author = checked_metadata_line(args.author or "", "author", 48)
    description = checked_metadata_line(args.description or "", "description", 112)
    generated_meta = None
    if author or description:
        generated_meta = f"author={author}\ndescription={description}\n".encode("utf-8")

    files = collect_files(source, generated_meta)
    validate_assets(files)
    names = {name for name, _ in files}
    if app_type not in (APP_SYSTEM_BROWSER, APP_THEME) and entrypoint not in names:
        raise ValueError(f"entrypoint is not present in source: {entrypoint}")
    if app_type == APP_THEME:
        if THEME_CONFIG_NAME not in names:
            raise ValueError(f"theme packages require {THEME_CONFIG_NAME}")
        if args.output.suffix.lower() != ".tthm":
            raise ValueError("theme package output must use the .tthm extension")
        forbidden = {".elf", ".tmod", ".irx", ".so", ".vsm"}
        if any(PurePosixPath(name).suffix.lower() in forbidden for name in names):
            raise ValueError("theme packages cannot contain executable or microprogram files")
        by_name = dict(files)
        if THEME_PREVIEW_NAME in by_name and len(by_name[THEME_PREVIEW_NAME]) != THEME_PREVIEW_BYTES:
            raise ValueError(f"{THEME_PREVIEW_NAME} must be exactly 256x128 RGB565")
        validate_theme_manifest(files, args.title_id)
    elif format_version == FORMAT_V2 and ICON_NAME not in names:
        raise ValueError(f"TPKG v2 packages require {ICON_NAME} for the Home tile")

    table = bytearray()
    payload = bytearray()
    for name, contents in files:
        encoded_name = name.encode("utf-8")
        entry = bytearray(ENTRY_SIZE)
        entry[: len(encoded_name)] = encoded_name
        write_u32(entry, 128, len(payload))
        write_u32(entry, 132, len(contents))
        write_u32(entry, 136, binascii.crc32(contents) & 0xFFFFFFFF)
        write_u32(entry, 140, 0)
        table.extend(entry)
        payload.extend(contents)

    package_size = HEADER_SIZE + len(table) + len(payload)
    if package_size > MAX_PACKAGE_SIZE:
        raise ValueError("package exceeds 256 MiB")

    header = bytearray(HEADER_SIZE)
    header[:4] = MAGIC
    write_u32(header, 4, format_version)
    write_u32(header, 8, HEADER_SIZE)
    write_u32(header, 12, ENTRY_SIZE)
    write_u32(header, 16, len(files))
    write_u32(header, 20, HEADER_SIZE)
    write_u32(header, 24, HEADER_SIZE + len(table))
    write_u32(header, 28, package_size)
    write_u32(header, 32, binascii.crc32(table) & 0xFFFFFFFF)
    header[40:56] = checked_title_id(args.title_id)
    header[56:136] = checked_text(args.title, 80, "title")
    header[136:152] = checked_text(args.version, 16, "version")
    if format_version == FORMAT_V1:
        header[152:256] = checked_text(entrypoint, ENTRYPOINT_V1_CAPACITY, "entrypoint")
    else:
        header[152:248] = checked_text(entrypoint, ENTRYPOINT_V2_CAPACITY, "entrypoint")
        write_u32(header, 248, app_type)
        write_u32(
            header, 252,
            TITAN_APP_API_VERSION
            if app_type in (APP_TITAN_NATIVE, APP_TITAN_GAME) else 0
        )
    write_u32(header, 36, header_checksum(header))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.stem + ".tmp" + args.output.suffix)
    temporary.write_bytes(header + table + payload)
    verify_file(temporary)
    temporary.replace(args.output)
    print(f"package: {args.output}")
    print(f"format:  TPKG v{format_version}")
    print(f"type:    {args.type}")
    print(f"files:   {len(files)}")
    print(f"bytes:   {package_size}")


def zero_terminated(raw: bytes) -> str:
    if b"\0" not in raw:
        raise ValueError("unterminated metadata")
    return raw.split(b"\0", 1)[0].decode("utf-8")


def verify_file(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE or len(data) > MAX_PACKAGE_SIZE:
        raise ValueError("invalid package size")
    header = bytearray(data[:HEADER_SIZE])
    if header[:4] != MAGIC:
        raise ValueError("invalid package magic")
    version, header_size, entry_size, count, table_offset, payload_offset, size = struct.unpack_from(
        "<7I", header, 4
    )
    if (
        version not in (FORMAT_V1, FORMAT_V2)
        or header_size != HEADER_SIZE
        or entry_size != ENTRY_SIZE
        or not 1 <= count <= MAX_FILES
        or table_offset != HEADER_SIZE
        or payload_offset != HEADER_SIZE + count * ENTRY_SIZE
        or size != len(data)
    ):
        raise ValueError("invalid package layout")
    if struct.unpack_from("<I", header, 36)[0] != header_checksum(header):
        raise ValueError("header checksum mismatch")
    table = data[table_offset:payload_offset]
    if struct.unpack_from("<I", header, 32)[0] != binascii.crc32(table) & 0xFFFFFFFF:
        raise ValueError("table checksum mismatch")

    if version == FORMAT_V1:
        app_type = APP_EXTERNAL_ELF
        api_version = 0
        entrypoint = zero_terminated(header[152:256])
    else:
        entrypoint = zero_terminated(header[152:248])
        app_type, api_version = struct.unpack_from("<2I", header, 248)
        if app_type not in (APP_EXTERNAL_ELF, APP_TITAN_NATIVE, APP_SYSTEM_BROWSER, APP_THEME, APP_TITAN_GAME):
            raise ValueError("unknown v2 app type")
        if api_version != (TITAN_APP_API_VERSION if app_type in (APP_TITAN_NATIVE, APP_TITAN_GAME) else 0):
            raise ValueError("invalid API version")
    checked_entrypoint(entrypoint, app_type)
    title_id = zero_terminated(header[40:56])

    found_entrypoint = 0
    expected_offset = 0
    seen: set[str] = set()
    file_sizes: dict[str, int] = {}
    package_files: list[tuple[str, bytes]] = []
    for index in range(count):
        entry = table[index * ENTRY_SIZE : (index + 1) * ENTRY_SIZE]
        relative = checked_relative(PurePosixPath(zero_terminated(entry[:128])))
        offset, length, checksum, flags = struct.unpack_from("<4I", entry, 128)
        if relative in seen or flags != 0 or offset != expected_offset or offset + length > len(data) - payload_offset:
            raise ValueError("invalid package entry")
        seen.add(relative)
        file_sizes[relative] = length
        contents = data[payload_offset + offset : payload_offset + offset + length]
        if binascii.crc32(contents) & 0xFFFFFFFF != checksum:
            raise ValueError(f"file checksum mismatch: {relative}")
        package_files.append((relative, contents))
        found_entrypoint += relative == entrypoint
        expected_offset += length
    if app_type == APP_SYSTEM_BROWSER:
        if entrypoint != SYSTEM_BROWSER_ENTRYPOINT or found_entrypoint != 0:
            raise ValueError("system-browser package must not embed an executable")
    elif app_type == APP_THEME:
        if entrypoint != THEME_ENTRYPOINT or found_entrypoint != 0:
            raise ValueError("theme package must not embed an executable")
        if path.suffix.lower() != ".tthm":
            raise ValueError("theme package must use the .tthm extension")
        if THEME_CONFIG_NAME not in file_sizes:
            raise ValueError(f"theme package is missing {THEME_CONFIG_NAME}")
        if THEME_PREVIEW_NAME in file_sizes and file_sizes[THEME_PREVIEW_NAME] != THEME_PREVIEW_BYTES:
            raise ValueError(f"invalid {THEME_PREVIEW_NAME} size")
        forbidden = {".elf", ".tmod", ".irx", ".so", ".vsm"}
        if any(PurePosixPath(name).suffix.lower() in forbidden for name in file_sizes):
            raise ValueError("theme package contains executable or microprogram content")
        validate_theme_manifest(package_files, title_id)
    elif found_entrypoint != 1:
        raise ValueError("package entrypoint is missing or duplicated")
    if expected_offset != len(data) - payload_offset:
        raise ValueError("package payload contains unreferenced bytes")
    if ICON_NAME in file_sizes and file_sizes[ICON_NAME] != ICON_BYTES:
        raise ValueError(f"invalid {ICON_NAME} size")
    if TILE_NAME in file_sizes and file_sizes[TILE_NAME] != TILE_BYTES:
        raise ValueError(f"invalid {TILE_NAME} size")
    if BACKGROUND_NAME in file_sizes and file_sizes[BACKGROUND_NAME] not in (
        APP_BACKGROUND_PRIMARY_BYTES, APP_BACKGROUND_LEGACY_BYTES
    ):
        raise ValueError(f"invalid {BACKGROUND_NAME} size")
    if version == FORMAT_V2 and app_type != APP_THEME and ICON_NAME not in file_sizes:
        raise ValueError(f"TPKG v2 package is missing {ICON_NAME}")

    title = zero_terminated(header[56:136])
    version_text = zero_terminated(header[136:152])
    type_name = (
        "titan-native" if app_type == APP_TITAN_NATIVE
        else "titan-game" if app_type == APP_TITAN_GAME
        else "system-browser" if app_type == APP_SYSTEM_BROWSER
        else "theme" if app_type == APP_THEME
        else "elf"
    )
    print(f"valid:   {path}")
    print(f"app:     {title} ({title_id}) v{version_text}")
    print(f"format:  TPKG v{version}")
    print(f"type:    {type_name}")
    print(f"entry:   {entrypoint}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    builder = subparsers.add_parser("build")
    builder.add_argument("--source", required=True, type=Path)
    builder.add_argument("--output", required=True, type=Path)
    builder.add_argument("--title-id", required=True)
    builder.add_argument("--title", required=True)
    builder.add_argument("--version", required=True)
    builder.add_argument("--type", choices=("elf", "titan-native", "titan-game", "system-browser", "theme"), default="elf")
    builder.add_argument("--format", type=int, choices=(1, 2), default=1,
                         help="ELF packages may use v1 or v2; titan-native/game/system-browser/theme always use v2")
    builder.add_argument("--entrypoint")
    builder.add_argument("--executable", help="legacy alias for --entrypoint on ELF packages")
    builder.add_argument("--author")
    builder.add_argument("--description")

    verifier = subparsers.add_parser("verify")
    verifier.add_argument("package", type=Path)
    args = parser.parse_args()
    if args.command == "build":
        build(args)
    else:
        verify_file(args.package)


if __name__ == "__main__":
    main()
