# TINS v1 — Titan Installation System container

Magic: `TINS`
Endian: little-endian
Maximum files: 192
Maximum container size: < 4 GiB (FAT32/32-bit offsets)

## Header — 64 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0x00 | 4 | magic `TINS` |
| 0x04 | 4 | format = 1 |
| 0x08 | 4 | header_size = 64 |
| 0x0C | 4 | entry_size = 224 |
| 0x10 | 4 | file_count |
| 0x14 | 4 | flags; bit 0 = autonomous payload present |
| 0x18 | 4 | table_offset = 64 |
| 0x1C | 4 | payload_offset |
| 0x20 | 4 | total_size |
| 0x24 | 4 | entry-table CRC32 |
| 0x28 | 24 | reserved/version marker |

## Entry — 224 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0x00 | 4 | kind: 1 SYSTEM, 2 BOOT, 3 EXPLOIT |
| 0x04 | 4 | entry flags |
| 0x08 | 4 | payload-relative offset |
| 0x0C | 4 | size |
| 0x10 | 4 | file CRC32 |
| 0x14 | 4 | UTF-8 path length |
| 0x18 | 192 | path storage |
| 0xD8 | 8 | reserved |

Paths are relative, may not contain `:` or a `..` path component, and are validated again by the PS2 installer.
