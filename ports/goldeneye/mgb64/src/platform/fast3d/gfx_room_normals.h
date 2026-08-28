#ifndef GFX_ROOM_NORMALS_H
#define GFX_ROOM_NORMALS_H

/* W1.E1 — Smooth env normals (CPU relight).
 *
 * Room geometry is static and ships with per-quad baked lighting whose per-face
 * discontinuities read as visible seams on large surfaces (Dam rock wall/ground).
 * This module recovers, once per room per level load, area-weighted vertex
 * normals from the room's raw display list + vertex pool, MERGING vertices whose
 * object-space positions are byte-identical (GE duplicates ridge/edge verts per
 * quad with independent UVs — that duplication is exactly what fragments the
 * baked normal, so merging by position is what erases the seam). The renderer
 * then relights ROOM shade colors against the static GlobalLight in the vertex
 * loader (see gfx_pc.c gfx_sp_vertex). Feature is opt-in (Video.EnvSmoothNormals
 * / GE007_ENV_SMOOTH_NORMALS); with it off the build is byte-identical.
 *
 * Layering: the pure builder core (gfx_room_normals_build) has zero game-state
 * dependencies and is unit-tested ROM-free. The lookup/reset wrapper reads
 * g_BgRoomInfo (render-reads-sim, allowed by rail R1) and is compiled out of the
 * unit test via GFX_ROOM_NORMALS_UNIT_TEST. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure builder core — NO game-state dependencies (unit-testable).
 *
 * Walks one or two raw big-endian F3DEX room display lists (`dl0`/`dl1`),
 * decoding G_VTX (0x04) segment loads and G_TRI1 (0xBF, index/10) / G_TRI4
 * (0xB1, raw nibbles) triangles exactly as lightfixture.c does. For every
 * triangle it accumulates the area-weighted geometric face normal (the raw edge
 * cross product, whose magnitude is 2*area) into per-position bins, merging pool
 * indices whose native int16 `ob[]` positions are byte-identical. Each bin is
 * normalized and quantized to int8 in [-127,127] and written to `out_n` for
 * every pool index sharing that position.
 *
 *   out_n       : caller-owned array of `count` int8[3] normals (written).
 *                 Vertices never referenced by a triangle, or degenerate, are
 *                 written {0,0,0} (the "no smooth normal" sentinel).
 *   count       : number of vertices in the pool.
 *   pool        : native (host-endian) vertex pool base; `ob[3]` is the first
 *                 field of each element (Vtx_t layout).
 *   vtx_stride  : element size in bytes (16 for Vtx).
 *   dl0/len0    : primary expanded room DL and its byte length.
 *   dl1/len1    : secondary expanded room DL (may be NULL/0).
 *
 * Returns the number of triangles processed. Object-space normals equal
 * world-space normals for rooms (uniform scale+translate), so no matrix. */
uint32_t gfx_room_normals_build(int8_t (*out_n)[3], uint32_t count,
                                const void *pool, uint32_t vtx_stride,
                                const uint8_t *dl0, uint32_t len0,
                                const uint8_t *dl1, uint32_t len1);

#ifndef GFX_ROOM_NORMALS_UNIT_TEST

/* Lazy, pool-pointer-keyed lookup. Returns the cached int8 unit normal for the
 * room vertex whose absolute address is `vtx_src_addr`, or NULL when the room /
 * address is out of range or that vertex has no smooth normal. Builds the room's
 * cache on first call and rebuilds automatically when the room's vertex pool
 * pointer changes across a level load (that pool-pointer identity IS the
 * level-unload invalidation). O(1) after the one-time per-room build — the
 * per-primitive contract (RENDERING_ARCHITECTURE.md §1) is respected. */
const int8_t *gfx_room_normal_lookup(int room_id, uintptr_t vtx_src_addr);

/* Loop-hoisted variant for gfx_sp_vertex: resolves the room's normal table and
 * the pool index of the run's FIRST vertex once per G_VTX command; the caller
 * then walks the table with base_idx+i (valid iff the source stride is
 * sizeof(Vtx), the only stride room loads use). Returns NULL when the run
 * cannot be relit. {0,0,0} entries are the "no smooth normal" sentinel and
 * must be skipped by the caller (same contract as gfx_room_normal_lookup). */
const int8_t (*gfx_room_normals_resolve(int room_id, uintptr_t vtx_src_addr,
                                        uint32_t *out_base_idx,
                                        uint32_t *out_count))[3];

/* Free all cached room normals (level unload / teardown; called from the PC
 * room loader's stage reset so recycled pool addresses can't serve stale
 * normals). */
void gfx_room_normals_reset(void);

#endif /* !GFX_ROOM_NORMALS_UNIT_TEST */

#ifdef __cplusplus
}
#endif

#endif /* GFX_ROOM_NORMALS_H */
