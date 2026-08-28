# Titan System Packaging — BF30

`TITAN_SYSTEM.TINS` remains TINS v1: fixed streamable header/table, bounded file count, validated relative paths, table CRC32 and per-file CRC32. BF30 intentionally does not change the on-target format because the seekless installer path is hardware-sensitive and already established.

Release hardening adds host-side SHA-256 identity and deterministic JSON release metadata through `tools/titan_tins.py release-metadata`. This supplements rather than replaces the target stream CRC checks. Corrupted payloads fail verification; unsafe paths are rejected before packaging.

A release TINS must be built only from explicit validated bootloader/kernel/system inputs. If target binaries are unavailable because PS2DEV/PS2SDK/R5900/IOP toolchains are absent, BF30 ships source and exact external build instructions rather than fabricating binaries.
