# BuildFix27 CP45 — Deterministic Target Environment Intake

Use `scripts/validate_ps2dev_bundle.py` only on an already materialized/extracted PS2DEV bundle.
The validator performs no downloads and never writes into the Project Titan source tree.

Success requires:
- PS2DEV and PS2SDK layout discovery;
- `mips64r5900el-ps2-elf-gcc` and `mips64r5900el-ps2-elf-ld`;
- `mipsel-none-elf-gcc` (or compatible `iop-gcc`);
- `dvp-as`;
- non-empty `ps2atad.irx`, `ps2hdd.irx`, `ps2fs.irx`;
- corresponding PS2SDK ATAD/APA/PFS Makefiles.

Only a fully valid bundle may produce the requested `env.sh`; incomplete material fails closed.
This is an intake/validation boundary, not a substitute for target compilation or hardware validation.
