#!/usr/bin/env python3
import random
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
pc = (root/'ports/goldeneye/mgb64/src/platform/fast3d/gfx_pc.c').read_text()
titan = (root/'ports/goldeneye/mgb64/src/platform/fast3d/gfx_titan.c').read_text()
heap = (root/'kernel/src/services/titan_game_runtime.c').read_text()
runner = (root/'kernel/tests/run_host_tests.sh').read_text()

# 1) Final hardware candidate keeps the proven symmetric GS +/-2W guard band
# on both axes. The abandoned P162.5 horizontal-only experiment must not be
# reintroduced after the release freeze.
assert 'v->x >= -w2 && v->x <= w2 && v->y >= -w2 && v->y <= w2' in pc
for fn in ('gfx_clip_triangle_to_wsafe_and_guardband', 'gfx_clip_triangle_to_guardband'):
    pos = pc.find(fn)
    assert pos >= 0
    body = pc[pos:pos+2600]
    assert 'GFX_CLIP_GB_BOTTOM' in body and 'GFX_CLIP_GB_TOP' in body

# 2) Final hardware candidate keeps the historical per-room VTX range scan.
# The experimental generation-scoped sorted VTX index is superseded and must
# not be reintroduced into the release-frozen source without fresh hardware
# validation.
assert 'static int gfx_find_room_for_vtx_addr(uintptr_t addr)' in pc
assert 'for (int room = 1; room < g_MaxNumRooms; room++)' in pc
assert 'ri->ptr_point_index' in pc and 'ri->usize_point_index_binary' in pc
for token in ('GFX_ROOM_VTX_RANGE_INDEX_MAX', 'gfx_room_vtx_index_lookup',
              's_room_vtx_range_index_overflow'):
    assert token not in pc

# 3) The final hardware-tested candidate uses the direct Titan combiner path.
# The experimental P162.5 differential fast-combiner header/harness was not
# part of the kernel/source state that produced the validated release binary.
assert '#include "gfx_titan_combiner_fast.h"' not in titan
assert 'static float titan_combiner_channel(' in titan
assert 'test_p1625_combiner_fast.c' not in runner
assert 'memset(&out[vertex], 0, sizeof(out[vertex]))' in titan

# 4) Final hardware candidate supersedes the experimental P162.5.x reserve
# policy with the P162.4.1 registryless chunk allocator. Preserve the exact
# final policy that produced the validated kernel instead of re-enabling the
# abandoned 10 MiB/4 MiB emergency reservations.
assert '#define TITAN_GAME_HEAP_CHUNK_MIN_BYTES     (64u * 1024u)' in heap
assert '#define TITAN_GAME_HEAP_MAX_CHUNKS          128u' in heap
assert 'g_p16241_runtime_tag[] = "P16241_REGISTRYLESS_P1624_CHUNKS"' in heap
assert 'TITAN_GAME_HEAP_PRIMARY_RESERVE_BYTES' not in heap
assert 'TITAN_GAME_HEAP_RESCUE_RESERVE_BYTES' not in heap
assert 'TITAN_GAME_HEAP_CHUNK_RESCUE_FLOOR_BYTES' not in heap
assert 'candidate /= 2u;' in heap and 'if (candidate < floor_bytes) candidate = floor_bytes;' in heap

# Numeric witness for the final symmetric +/-2W policy.
def inside(x,y,w):
    return w>0 and -2*w<=x<=2*w and -2*w<=y<=2*w
assert inside(1.5,0.0,1.0)
assert inside(0.0,1.5,1.0)
assert inside(0.0,-1.5,1.0)
assert not inside(0.0,2.1,1.0)

print('P162_FINAL_HARDWARE_STATE_CONTRACT=PASS')
print('room_lookup_policy=historical_linear_scan')
