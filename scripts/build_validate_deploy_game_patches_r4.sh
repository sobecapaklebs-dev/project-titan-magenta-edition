#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
USB="${1:-/run/media/sobecapaklebs/PS}"
WORKSPACE="$(cd "$ROOT/.." && pwd)"
GOLDENS="${TITAN_GOLDENS:-$WORKSPACE/Goldens}"
BACKUPS="$WORKSPACE/Backups"
LOGS="$WORKSPACE/Logs"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="$LOGS/gamepatch-r4-kernel-only-$STAMP.log"

mkdir -p "$BACKUPS" "$LOGS"

export PS2DEV="${PS2DEV:-$HOME/Projects/ps2dev}"
export PS2SDK="${PS2SDK:-$PS2DEV/ps2sdk}"
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"

fail() { echo "ERRO: $*" >&2; exit 1; }
hash() { sha256sum "$1" | awk '{print $1}'; }
find_golden() {
    local name="$1" p
    p="$GOLDENS/$name"
    if [[ -f "$p" ]]; then printf '%s\n' "$p"; return 0; fi
    p="$(find "$GOLDENS" -maxdepth 3 -type f -name "$name" -print -quit 2>/dev/null || true)"
    [[ -n "$p" ]] || return 1
    printf '%s\n' "$p"
}

[[ -d "$USB" ]] || fail "USB Titan nao montado em $USB"
if [[ ! -d "$GOLDENS" ]]; then
    GOLDENS="$(bash "$ROOT/scripts/find_titan_goldens.sh" "$WORKSPACE" || true)"
fi
[[ -n "${GOLDENS:-}" && -d "$GOLDENS" ]] || fail "Goldens de producao nao localizados automaticamente em $WORKSPACE"
export TITAN_GOLDENS="$GOLDENS"
echo "GOLDENS=$GOLDENS"
command -v make >/dev/null || fail "make nao encontrado"
command -v python3 >/dev/null || fail "python3 nao encontrado"
command -v sha256sum >/dev/null || fail "sha256sum nao encontrado"
[[ -x "$PS2SDK/bin/bin2c" ]] || fail "PS2SDK/bin2c ausente em $PS2SDK/bin/bin2c"
command -v mips64r5900el-ps2-elf-gcc >/dev/null || fail "toolchain EE PS2 nao encontrado no PATH"

GOLDEN_NEU="$(find_golden neutrino.elf)" || fail "Golden neutrino.elf nao encontrado em $GOLDENS"
GOLDEN_UL_ENGINE="$(find_golden titan_ul_engine.elf)" || fail "Golden titan_ul_engine.elf nao encontrado em $GOLDENS"
GOLDEN_UL_LOADER="$(find_golden titan_ul_loader.elf)" || fail "Golden titan_ul_loader.elf nao encontrado em $GOLDENS"

NEU_SHA="$(hash "$GOLDEN_NEU")"
UL_ENGINE_SHA="$(hash "$GOLDEN_UL_ENGINE")"
UL_LOADER_SHA="$(hash "$GOLDEN_UL_LOADER")"

USB_NEU="$USB/neutrino/neutrino.elf"
USB_UL_ENGINE="$USB/TITAN/titan_ul_engine.elf"
USB_UL_LOADER="$USB/TITAN/titan_ul_loader.elf"
for pair in \
    "$GOLDEN_NEU|$USB_NEU" \
    "$GOLDEN_UL_ENGINE|$USB_UL_ENGINE" \
    "$GOLDEN_UL_LOADER|$USB_UL_LOADER"; do
    src="${pair%%|*}"; dst="${pair#*|}"
    [[ -f "$dst" ]] || fail "runtime de producao ausente no USB: $dst (nenhum Golden sera copiado automaticamente)"
    [[ "$(hash "$src")" == "$(hash "$dst")" ]] || fail "USB nao esta byte-exato ao Golden: $dst; abortando antes de qualquer deploy"
done

echo "=== PROJECT TITAN GAME PATCHES R4 — KERNEL ONLY ===" | tee "$LOG"
echo "ROOT=$ROOT" | tee -a "$LOG"
echo "USB=$USB" | tee -a "$LOG"
echo "GOLDEN_NEUTRINO_SHA256=$NEU_SHA" | tee -a "$LOG"
echo "GOLDEN_UL_ENGINE_SHA256=$UL_ENGINE_SHA" | tee -a "$LOG"
echo "GOLDEN_UL_LOADER_SHA256=$UL_LOADER_SHA" | tee -a "$LOG"
echo "PKGS_REBUILT=NO" | tee -a "$LOG"
echo "NEUTRINO_REBUILT=NO" | tee -a "$LOG"
echo "UL_REBUILT=NO" | tee -a "$LOG"

bash "$ROOT/scripts/validate_game_patches_r4.sh" "$ROOT" 2>&1 | tee -a "$LOG"

# Fail before target compilation if the production Golden overlaps Titan's
# resident PS2RD reservation. Also inspect the deployed ee_core when present.
python3 - "$GOLDEN_NEU" "$USB/neutrino/modules/ee_core.elf" <<'PY' | tee -a "$LOG"
import struct,sys

def loads(path):
    b=open(path,'rb').read()
    if b[:4] != b'\x7fELF' or b[4] != 1 or b[5] != 1:
        raise SystemExit(f'ELF32 little-endian esperado: {path}')
    phoff=struct.unpack_from('<I',b,28)[0]; entsz=struct.unpack_from('<H',b,42)[0]; n=struct.unpack_from('<H',b,44)[0]
    out=[]
    for i in range(n):
        o=phoff+i*entsz
        typ,off,vaddr,paddr,filesz,memsz,flags,align=struct.unpack_from('<IIIIIIII',b,o)
        if typ==1: out.append((vaddr,vaddr+memsz,memsz))
    return out

def overlap(a,b,c,d): return a < d and c < b
for path in sys.argv[1:]:
    import os
    if not os.path.isfile(path):
        print('LOWMEM_PREFLIGHT_SKIP='+path); continue
    segs=loads(path)
    for start,end,_ in segs:
        if overlap(start,end,0x00080000,0x00084000):
            raise SystemExit(f'LOWMEM COLLISION: {path} LOAD {start:#x}-{end:#x} overlaps Titan PS2RD 0x80000-0x84000')
    print('LOWMEM_PREFLIGHT_PASS='+path)
PY

# Build only the embedded handoff stage2. No global clean: preserve every
# validated prebuilt object except the two files whose source actually changed.
find "$ROOT/bootloader/stage2/generated" -type f -exec touch {} + 2>/dev/null || true
touch "$ROOT/bootloader/stage2/src/stage2_video.o" 2>/dev/null || true
rm -f \
    "$ROOT/bootloader/stage2/src/main.o" \
    "$ROOT/bootloader/stage2/src/titan_ps2rd_engine.o" \
    "$ROOT/bootloader/stage2/stage2-debug.elf" \
    "$ROOT/bootloader/stage2/stage2.elf"
make -C "$ROOT/bootloader/stage2" all 2>&1 | tee -a "$LOG"

# Validate the freshly linked stage2 without relying on host binutils naming.
python3 - "$ROOT/bootloader/stage2/stage2.elf" <<'PY' | tee -a "$LOG"
import struct,sys
p=sys.argv[1]; b=open(p,'rb').read()
assert b[:4]==b'\x7fELF' and b[4]==1 and b[5]==1, 'stage2 not ELF32 LE'
entry=struct.unpack_from('<I',b,24)[0]; phoff=struct.unpack_from('<I',b,28)[0]
entsz=struct.unpack_from('<H',b,42)[0]; n=struct.unpack_from('<H',b,44)[0]
assert 0x94000 <= entry < 0x100000, hex(entry)
low=high=0
for i in range(n):
    typ,off,vaddr,paddr,filesz,memsz,flags,align=struct.unpack_from('<IIIIIIII',b,phoff+i*entsz)
    if typ != 1: continue
    end=vaddr+memsz
    if 0x80000 <= vaddr and end <= 0x84000: low += 1
    elif 0x94000 <= vaddr and end <= 0x100000: high += 1
    else: raise AssertionError(f'unsafe LOAD {vaddr:#x}-{end:#x}')
assert low >= 1 and high >= 1, (low,high)
print(f'STAGE2_LAYOUT_PASS entry={entry:#x} ps2rd_loads={low} stage2_loads={high}')
PY

# R4.4: deterministic rebuild set derived from a clean source(6).zip audit.
# DO NOT infer this from mtimes: R4.0-R4.3 may have touched prebuilt objects,
# which can make an obsolete object appear newer than its source.
HEALTH_C="$ROOT/kernel/src/services/titan_health_trace.c"
HEALTH_H="$ROOT/kernel/src/services/titan_health_trace.h"
[[ "$(hash "$HEALTH_C")" == "f476ff20cb301a3508b38b8dff59b1a0892f2492ee19d56822d22064aa48923c" ]] || \
    fail "titan_health_trace.c divergiu do baseline; nao vou recompilar politica de trace alterada"
[[ "$(hash "$HEALTH_H")" == "bb84254149ced8cd650162e385985b62bd9edbb164d61d97d4578043254c55ac" ]] || \
    fail "titan_health_trace.h divergiu do baseline; nao vou recompilar politica de trace alterada"
echo "HEALTH_TRACE_POLICY_MODIFIED=NO" | tee -a "$LOG"
echo "HEALTH_TRACE_C_SOURCE_BASELINE=PASS" | tee -a "$LOG"
echo "HEALTH_TRACE_H_SOURCE_BASELINE=PASS" | tee -a "$LOG"

# These seven objects are stale in the untouched source(6).zip itself. Keep
# this list content-derived from that clean archive, not from the current
# workspace timestamps.
TITAN_BASELINE_STALE_KERNEL_OBJS=(
    src/services/titan_dex.o
    src/services/titan_game_gsm.o
    src/services/titan_health_trace.o
    src/services/titan_localization.o
    src/services/titan_network.o
    src/services/titan_neutrino.o
    src/ui/titan_shell_scene.o
)

# These are the objects whose source is owned by the R4 game-patch change.
TITAN_R4_KERNEL_OBJS=(
    src/core/titan_kernel.o
    src/services/titan_localization.o
    src/services/titan_neutrino.o
    src/services/titan_exec_handoff.o
    src/services/titan_game_patches.o
    src/ui/titan_shell_scene.o
)

# Build a stable de-duplicated union. This remains correct even when a prior
# broken runner changed every object mtime with touch(1).
TITAN_STALE_KERNEL_OBJS=()
for obj in "${TITAN_BASELINE_STALE_KERNEL_OBJS[@]}" "${TITAN_R4_KERNEL_OBJS[@]}"; do
    found=0
    for existing in "${TITAN_STALE_KERNEL_OBJS[@]:-}"; do
        [[ "$existing" == "$obj" ]] && found=1 && break
    done
    (( found )) || TITAN_STALE_KERNEL_OBJS+=("$obj")
done

printf 'KERNEL_REBUILD_POLICY=DETERMINISTIC_SOURCE6\n' | tee -a "$LOG"
printf 'KERNEL_REBUILD_OBJECT_COUNT=%d\n' "${#TITAN_STALE_KERNEL_OBJS[@]}" | tee -a "$LOG"
printf 'KERNEL_REBUILD_OBJECT=%s\n' "${TITAN_STALE_KERNEL_OBJS[@]}" | tee -a "$LOG"

# Generated inputs and unrelated validated prebuilt objects are frozen only
# AFTER the stale list above has been captured.
find "$ROOT/kernel/src" -type f -path '*/generated/*' -exec touch {} + 2>/dev/null || true
find "$ROOT/kernel" -type f -name '*.o' -exec touch {} + 2>/dev/null || true
touch \
    "$ROOT/kernel/third_party/.titan-tls/lib/libbearssl.a" \
    "$ROOT/kernel/third_party/.titan-tls/image-deps-ready" \
    "$ROOT/third_party/fmcb/integrated/build/libtitanfmcb_integrated.a" 2>/dev/null || true

# Force regeneration of the stage2 embed and every stale/R4-owned object.
rm -f "$ROOT/kernel/src/services/generated/titan_launch_stage2_elf.h" "$ROOT/kernel/kernel.elf"
for obj in "${TITAN_STALE_KERNEL_OBJS[@]}"; do
    rm -f "$ROOT/kernel/$obj"
done
# R4.4 hard gate for the regression that exposed timestamp contamination.
rm -f "$ROOT/kernel/src/services/titan_health_trace.o"
[[ ! -e "$ROOT/kernel/src/services/titan_health_trace.o" ]] || fail "nao foi possivel remover titan_health_trace.o antigo"
echo "HEALTH_TRACE_OLD_OBJECT_REMOVED=PASS" | tee -a "$LOG"

if (( ${#TITAN_STALE_KERNEL_OBJS[@]} > 0 )); then
    make -C "$ROOT/kernel" "${TITAN_STALE_KERNEL_OBJS[@]}" 2>&1 | tee -a "$LOG"
fi

# Link-time sanity for the exact regression that exposed stale objects.
HEALTH_OBJ="$ROOT/kernel/src/services/titan_health_trace.o"
if [[ -f "$HEALTH_OBJ" ]]; then
    NM_TOOL="$(command -v mips64r5900el-ps2-elf-nm || command -v nm || true)"
    [[ -n "$NM_TOOL" ]] || fail "nm nao encontrado para validar titan_health_trace.o"
    "$NM_TOOL" "$HEALTH_OBJ" | grep -Eq '[[:space:]]T[[:space:]]+TitanHealthTrace_SetEnabled$' || \
        fail "titan_health_trace.o continua stale: TitanHealthTrace_SetEnabled ausente"
    echo "HEALTH_TRACE_OBJECT_API=PASS" | tee -a "$LOG"
fi

make -C "$ROOT/kernel" kernel.elf 2>&1 | tee -a "$LOG"
[[ -s "$ROOT/kernel/kernel.elf" ]] || fail "kernel/kernel.elf nao foi gerado"

# The build log itself is an enforcement gate: this feature must not compile
# packages or either production engine source tree.
if grep -E 'make(\[[0-9]+\])?: Entering directory .*modules/titan-(neutrino|ul)-engine|make(\[[0-9]+\])?: Entering directory .*/apps/|PROJECTNES\.pkg|WLAUNCHELF\.pkg|neutrino-titan-patches\.elf' "$LOG" >/dev/null; then
    grep -E 'modules/titan-(neutrino|ul)-engine|/apps/|\.pkg|neutrino-titan-patches' "$LOG" >&2 || true
    fail "build saiu do escopo kernel/stage2"
fi

# Re-run immutable source and Golden gates after compilation.
python3 "$ROOT/kernel/tests/test_game_patch_golden_contract.py" "$ROOT" | tee -a "$LOG"
[[ "$(hash "$GOLDEN_NEU")" == "$NEU_SHA" ]] || fail "Golden Neutrino host mudou durante build"
[[ "$(hash "$GOLDEN_UL_ENGINE")" == "$UL_ENGINE_SHA" ]] || fail "Golden UL engine host mudou durante build"
[[ "$(hash "$GOLDEN_UL_LOADER")" == "$UL_LOADER_SHA" ]] || fail "Golden UL loader host mudou durante build"

mkdir -p "$USB/CHT" "$USB/ENH" "$USB/TITAN/gamepatches"
BACKUP_DIR="$BACKUPS/GamePatches-R4-$STAMP"
mkdir -p "$BACKUP_DIR"
[[ ! -f "$USB/kernel.elf" ]] || cp -a "$USB/kernel.elf" "$BACKUP_DIR/kernel.elf.before"

KERNEL_SHA="$(hash "$ROOT/kernel/kernel.elf")"
cp -f "$ROOT/kernel/kernel.elf" "$USB/kernel.elf.titan-r4.tmp"
sync
mv -f "$USB/kernel.elf.titan-r4.tmp" "$USB/kernel.elf"
sync
[[ "$(hash "$USB/kernel.elf")" == "$KERNEL_SHA" ]] || fail "SHA kernel package != USB"

# Final production runtime verification: read-only, no engine copy/deploy.
[[ "$(hash "$USB_NEU")" == "$NEU_SHA" ]] || fail "Golden Neutrino USB mudou"
[[ "$(hash "$USB_UL_ENGINE")" == "$UL_ENGINE_SHA" ]] || fail "Golden UL engine USB mudou"
[[ "$(hash "$USB_UL_LOADER")" == "$UL_LOADER_SHA" ]] || fail "Golden UL loader USB mudou"

echo "KERNEL_SHA256=$KERNEL_SHA" | tee -a "$LOG"
echo "USB_KERNEL_SHA256=$(hash "$USB/kernel.elf")" | tee -a "$LOG"
echo "GOLDEN_NEUTRINO_PRESERVED=PASS" | tee -a "$LOG"
echo "GOLDEN_UL_PRESERVED=PASS" | tee -a "$LOG"
echo "ENGINE_SOURCE_REBUILT=NO" | tee -a "$LOG"
echo "PKGS_REBUILT=NO" | tee -a "$LOG"
echo "DEPLOYED_FILES=kernel.elf" | tee -a "$LOG"
echo "GAME_PATCH_R4_KERNEL_ONLY=PASS" | tee -a "$LOG"
echo "LOG=$LOG"
