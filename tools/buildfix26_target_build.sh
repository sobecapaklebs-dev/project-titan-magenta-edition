#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/buildfix26-target-report}"
mkdir -p "$OUT"
REPORT="$OUT/BUILDFIX26_TARGET_BUILD_REPORT.txt"
: > "$REPORT"
log(){ printf '%s\n' "$*" | tee -a "$REPORT"; }

PREFIX="${EE_TOOLCHAIN_PREFIX:-mips64r5900el-ps2-elf-}"
CC="${PREFIX}gcc"; LD="${PREFIX}ld"; SIZE="${PREFIX}size"; READELF="${PREFIX}readelf"; NM="${PREFIX}nm"; OBJDUMP="${PREFIX}objdump"
DVP_AS="${DVP_AS:-${PS2DEV:-}/dvp/bin/dvp-as}"
DVP_OBJCOPY="${DVP_OBJCOPY:-${PS2DEV:-}/dvp/bin/dvp-objcopy}"

log "Project Titan BuildFix26 target-build probe"
log "root=$ROOT"
log "PS2DEV=${PS2DEV:-<unset>}"
log "PS2SDK=${PS2SDK:-<unset>}"
missing=0
for tool in "$CC" "$LD" "$SIZE" "$READELF" "$NM" "$OBJDUMP"; do
  if ! command -v "$tool" >/dev/null 2>&1; then log "MISSING=$tool"; missing=1; else log "FOUND=$tool"; fi
done
if [ -z "${PS2SDK:-}" ] || [ ! -d "${PS2SDK:-/nonexistent}" ]; then log "MISSING=PS2SDK"; missing=1; fi
if [ -z "${PS2DEV:-}" ] || [ ! -x "$DVP_AS" ] || [ ! -x "$DVP_OBJCOPY" ]; then log "MISSING=DVP toolchain ($DVP_AS / $DVP_OBJCOPY)"; missing=1; fi
if [ "$missing" -ne 0 ]; then
  log "STATUS=BLOCKED"
  log "VALIDATION=R5900_TARGET_BUILD_NOT_EXECUTED"
  exit 2
fi

log "STATUS=BUILDING"
make -C "$ROOT/kernel" clean >>"$REPORT" 2>&1
make -C "$ROOT/kernel" -j"${JOBS:-2}" >>"$REPORT" 2>&1
ELF="$ROOT/kernel/kernel.elf"
if [ ! -s "$ELF" ]; then log "STATUS=FAIL kernel.elf missing"; exit 3; fi

log "ELF=$ELF"
sha256sum "$ELF" | tee "$OUT/kernel.elf.sha256" >> "$REPORT"
"$SIZE" "$ELF" | tee "$OUT/kernel.size.txt" >> "$REPORT"
"$SIZE" -A "$ELF" | tee "$OUT/kernel.sections.size.txt" >> "$REPORT"
"$READELF" -h "$ELF" > "$OUT/kernel.readelf.header.txt"
"$READELF" -S "$ELF" > "$OUT/kernel.readelf.sections.txt"
"$READELF" -r "$ELF" > "$OUT/kernel.readelf.relocations.txt"
"$READELF" -s "$ELF" > "$OUT/kernel.readelf.symbols.txt"
"$NM" -u "$ELF" > "$OUT/kernel.undefined.txt" || true
"$OBJDUMP" -h "$ELF" > "$OUT/kernel.objdump.sections.txt"

VU_OBJ="$ROOT/kernel/src/graphics/vu1/titan_wave_vu1_micro.dvp.o"
VU_BIN="$ROOT/kernel/src/graphics/vu1/titan_wave_vu1_micro.bin"
for f in "$VU_OBJ" "$VU_BIN"; do
  if [ -s "$f" ]; then sha256sum "$f" >> "$OUT/vu-artifacts.sha256"; stat -c '%n %s bytes' "$f" >> "$OUT/vu-artifacts.txt"; else log "FAIL_MISSING_VU=$f"; exit 4; fi
done

find "$ROOT/kernel/iop" -type f -name '*.irx' -print0 2>/dev/null | sort -z | xargs -0 -r sha256sum > "$OUT/iop-modules.sha256"
find "$ROOT/kernel/iop" -type f -name '*.irx' -printf '%p %s bytes\n' 2>/dev/null | sort > "$OUT/iop-modules.txt"

if [ -s "$OUT/kernel.undefined.txt" ]; then
  log "UNDEFINED_SYMBOLS_PRESENT=YES (inspect kernel.undefined.txt; toolchain/ELF conventions may contain expected runtime imports)"
else
  log "UNDEFINED_SYMBOLS_PRESENT=NO"
fi
log "STATUS=TARGET_BUILD_COMPLETED"
log "VALIDATION=R5900_TARGET_BUILD_EXECUTED_REQUIRES_REPORT_REVIEW"
