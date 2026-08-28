#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
title = (root / 'ports/goldeneye/mgb64/src/game/title.c').read_text()
gfx = (root / 'ports/goldeneye/mgb64/src/platform/fast3d/gfx_pc.c').read_text()
hole = (root / 'ports/goldeneye/mgb64/src/game/unk_01BAE0.c').read_text()

def require(cond, msg):
    if not cond:
        raise SystemExit('P1623_GUNBARREL_PROVENANCE=FAIL ' + msg)

create = title.index('createGunbarrelRenderHole(barrelDisplayListPtr, 0x1E);')
register = title.index('gfx_register_pc_vertex_region(barrelDisplayListPtr, 0x1E * sizeof(Vtx));')
dlbuild = title.index('gunbarrelDlEnd = sub_GAME_7F01BFF8', register)
require(create < register < dlbuild, 'native Vtx provenance must be registered after generation and before DL build')
require('gfx_register_extra_pc_dl(gunbarrelgfxListPointer, gunbarrelDlBytes);' in title,
        'gunbarrel DL must remain explicitly PC-native')

# The reused title buffer really is first registered as N64 data by the Rareware path.
require('gfx_register_n64_dl_region((void *)virtualaddress, segment_size);' in title,
        'Rareware prefix N64 registration missing; provenance overlap assumption changed')

# PC-native provenance must dominate an overlapping stale N64 range at G_VTX dispatch.
require('bool addr_is_pc_vtx = gfx_addr_is_pc_native_vertex_data((uintptr_t)addr);' in gfx,
        'PC vertex classification missing')
require('bool addr_is_n64_vtx = gfx_addr_is_n64_data((uintptr_t)addr);' in gfx,
        'N64 vertex classification missing')
require('if (!addr_is_pc_vtx && addr_is_n64_vtx)' in gfx,
        'PC-native provenance no longer wins N64 overlap')

# Pin the authored gunbarrel geometry that makes endian corruption visually catastrophic.
require('sinf(temp_f20) * 64.0f' in hole and 'cosf(temp_f20) * -64.0f' in hole,
        'gunbarrel +/-64 geometry contract changed')
require('143.0f - (cosf(temp_f20) * -111.0f)' in hole,
        'gunbarrel vertex-shade gradient contract changed')
require('vertexList += 14;' in hole and 'gSPVertex(gdl++, vertexList, j, 0);' in hole,
        'gunbarrel overlapping 16/14 vertex-window topology changed')

# Aggregate hardware probe: zero per-vertex spam, but enough to prove provenance,
# coordinate magnitude, triangle survival and SHADE delivery into the backend VBO.
for token in (
    'P1623_GUN dl=%llu',
    'pc_native=%llu n64_overlap=%llu n64_decode=%llu',
    'src_obmax=%u loaded_obmax=%u',
    'rgb=%u-%u shade_vtx=%llu shade=%u-%u',
    's_p1623_window_gun_common_reject',
    's_p1623_window_gun_cull_fast_reject',
    's_p1623_window_gun_cull_exact_reject',
    's_p1623_window_gun_clipaway',
    's_p1623_window_gun_emit',
):
    require(token in gfx, f'missing gunbarrel probe token: {token}')

# Sanity witness: the historical wrong endian interpretation of +64 is +16384.
raw = (64).to_bytes(2, 'little', signed=True)
wrong = int.from_bytes(raw, 'big', signed=True)
require(wrong == 16384, f'endian witness changed unexpectedly: {wrong}')

print('P1623_GUNBARREL_PROVENANCE=PASS native Vtx overrides stale N64 region; endian witness 64->16384 pinned')
