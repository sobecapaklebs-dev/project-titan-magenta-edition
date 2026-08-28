#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
irx = root / 'modules/titan-ul-engine/modules/pademu/bt_pademu.irx'
header = root / 'kernel/src/services/generated/titan_bt_pademu_irx.h'
kernel = root / 'kernel/kernel.elf'
for p in (irx, header, kernel):
    if not p.is_file():
        raise SystemExit(f'P104_EMBED_IDENTITY=FAIL missing {p}')
raw = irx.read_bytes()
text = header.read_text(errors='strict')
size_m = re.search(r'unsigned int size_titan_bt_pademu_irx\s*=\s*(\d+)\s*;', text)
array_m = re.search(r'titan_bt_pademu_irx\[\].*?=\s*\{(.*?)\};', text, re.S)
if not size_m or not array_m:
    raise SystemExit('P104_EMBED_IDENTITY=FAIL cannot parse generated BT PADEMU header')
header_size = int(size_m.group(1))
vals = re.findall(r'0x([0-9A-Fa-f]{1,2})', array_m.group(1))
embedded = bytes(int(v,16) for v in vals)
if header_size != len(raw) or len(embedded) != len(raw):
    raise SystemExit(f'P104_EMBED_IDENTITY=FAIL size source={len(raw)} header_size={header_size} array={len(embedded)}')
if embedded != raw:
    raise SystemExit('P104_EMBED_IDENTITY=FAIL bin2c header bytes differ from freshly built bt_pademu.irx')
kernel_bytes = kernel.read_bytes()
count = kernel_bytes.count(raw)
if count != 1:
    raise SystemExit(f'P104_EMBED_IDENTITY=FAIL kernel raw IRX occurrence count={count}, expected 1')
sha = hashlib.sha256(raw).hexdigest()
h = 2166136261
for b in raw:
    h ^= b
    h = (h * 16777619) & 0xffffffff
print(f'P104_BT_PADEMU_SIZE={len(raw)}')
print(f'P104_BT_PADEMU_SHA256={sha}')
print(f'P104_BT_PADEMU_FNV1A32={h:08x}')
print('P104_EMBED_HEADER=IDENTICAL')
print('P104_EMBED_KERNEL_CONTAINS_IRX=PASS')
print('P104_EMBED_IDENTITY=PASS')
