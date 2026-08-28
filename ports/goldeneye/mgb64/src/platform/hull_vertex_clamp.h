/*
 * hull_vertex_clamp.h — the bbox-projection convex-hull vertex-count clamp of
 * sub_GAME_7F03ECC0 (retail ASM src/game/chrprop.c:8710-8712, VERSION_US glabel
 * src/game/chrprop.c:8243).
 *
 * Pure (ROM-free, SDL-free) mirror of the clamp policy, factored out so a
 * ROM-free unit test can guard it (FID-0096).
 *
 * PARITY-DIVERGENCE (documented; port clamp kept as the faithful default).
 * Retail projects the 8 bbox corners to 2D, picks 4 extreme corners, then
 * inserts up to one extra vertex per hull edge (cross-product test), for a
 * total of up to 8 vertices (a projected box hull is a hexagon at most, so 5-6
 * is reachable). Retail stores that true count into collision_data.unk00 with
 * NO clamp and translates all of them. The NONMATCHING port inserts
 * `if (count > 4) count = 4;` (src/game/chrprop.c:8229) before storing unk00.
 *
 * Why the port keeps the clamp as the faithful default: the collision_data
 * caller path (object/door, chrobjhandler.c:2927/45882) writes into
 * &collision_data->unk04 (a 4-vertex rect4f) followed by unk24..unk40 — spare
 * words the consumer chrpropTestPointInPolygon reads contiguously, so removing
 * the clamp there is memory-safe. BUT the TANK caller (chrobjhandler.c:11890)
 * writes into TankRecord.rect (bondtypes.h:3719), which is IMMEDIATELY followed
 * by live fields (unkA4 acceleration, unkA8, ...) in the port's 64-bit-grown
 * struct — NOT spare polygon slots. Vertices 5-8 there would corrupt tank state,
 * and the tank consumer (bondview.c:8881, reading unk80->unk00 vertices from
 * &tank->rect) would read tank acceleration as polygon coordinates. So the clamp
 * is a load-bearing port safety mitigation for the tank path; the default keeps
 * it and GE007_HULL_VERTS_RETAIL is an opt-in that removes it (retail-faithful
 * for the object/door path; NOT memory-safe for the tank path). See FID-0096.
 */
#ifndef MGB64_HULL_VERTEX_CLAMP_H
#define MGB64_HULL_VERTEX_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve the convex-hull output vertex count.
 *
 *   count           : the hull vertex count built by the projection (4..8).
 *   retail_unclamped : 0 => port default (clamp to 4, memory-safe for all
 *                     callers incl. the tank); != 0 => retail behavior (no
 *                     clamp) for the GE007_HULL_VERTS_RETAIL A/B control.
 *
 * Returns the count to store into collision_data.unk00 and emit vertices for.
 */
int hullVertexCount(int count, int retail_unclamped);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_HULL_VERTEX_CLAMP_H */
