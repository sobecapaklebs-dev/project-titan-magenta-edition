/* W1.E1 — Smooth env normals: CPU room-normal cache builder.
 * See gfx_room_normals.h for the design rationale and the layering split. */

#include "gfx_room_normals.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 *  Pure builder core — no game-state dependencies (unit-testable).    *
 * ------------------------------------------------------------------ */

/* Big-endian 32-bit read from a raw F3DEX byte stream (mirrors bgReadBe32 /
 * lightfixture.c lf_read_be32). */
static uint32_t rn_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Native int16 object-space position of pool vertex `idx` (ob[] is the first
 * field of Vtx_t; the pool is host-endian, matching how lightfixture.c reads
 * return_ptr_vertex_of_entry_room()[idx].v.ob[]). */
static const int16_t *rn_ob(const void *pool, uint32_t stride, uint32_t idx) {
    return (const int16_t *)((const uint8_t *)pool + (size_t)idx * stride);
}

uint32_t gfx_room_normals_build(int8_t (*out_n)[3], uint32_t count,
                                const void *pool, uint32_t vtx_stride,
                                const uint8_t *dl0, uint32_t len0,
                                const uint8_t *dl1, uint32_t len1) {
    if (out_n == NULL || count == 0) {
        return 0;
    }
    memset(out_n, 0, (size_t)count * 3);
    if (pool == NULL || vtx_stride == 0) {
        return 0;
    }

    /* bin[i] = representative pool index for i's position (position merge).
     * acc[rep*3 + c] = summed area-weighted face normal for that position. */
    int32_t *bin = (int32_t *)malloc((size_t)count * sizeof(int32_t));
    double *acc = (double *)calloc((size_t)count * 3, sizeof(double));
    if (bin == NULL || acc == NULL) {
        free(bin);
        free(acc);
        return 0;
    }

    /* Open-addressing hash: packed position -> pool index. */
    uint32_t cap = 16;
    while (cap < count * 2u) {
        cap <<= 1;
    }
    int32_t *ht = (int32_t *)malloc((size_t)cap * sizeof(int32_t));
    if (ht == NULL) {
        free(bin);
        free(acc);
        return 0;
    }
    for (uint32_t i = 0; i < cap; i++) {
        ht[i] = -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        const int16_t *ob = rn_ob(pool, vtx_stride, i);
        uint64_t key = ((uint64_t)(uint16_t)ob[0] << 32) |
                       ((uint64_t)(uint16_t)ob[1] << 16) |
                       (uint64_t)(uint16_t)ob[2];
        uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (cap - 1);
        int32_t rep = -1;
        while (ht[h] >= 0) {
            uint32_t j = (uint32_t)ht[h];
            const int16_t *obj = rn_ob(pool, vtx_stride, j);
            if (obj[0] == ob[0] && obj[1] == ob[1] && obj[2] == ob[2]) {
                rep = (int32_t)j;
                break;
            }
            h = (h + 1) & (cap - 1);
        }
        if (rep < 0) {
            ht[h] = (int32_t)i;
            rep = (int32_t)i;
        }
        bin[i] = rep;
    }

    /* Walk the DL(s); accumulate area-weighted face normals into rep bins. */
    uint32_t tri_total = 0;
    const uint8_t *dls[2] = { dl0, dl1 };
    uint32_t lens[2] = { len0, len1 };
    for (int d = 0; d < 2; d++) {
        const uint8_t *dl = dls[d];
        uint32_t len = lens[d];
        if (dl == NULL || len < 8) {
            continue;
        }
        const uint8_t *end = dl + len;
        int64_t cur_base = -1; /* current G_VTX base as a pool index */
        int64_t cur_v0 = 0;    /* G_VTX destination slot (cmd[1] & 0xF): DMEM
                                * slot v0 maps to pool index cur_base, so a
                                * triangle slot s maps to cur_base + (s - v0).
                                * Matches the hit test's vtx_base_offset
                                * subtraction (lightfixture.c lf_verify). */
        for (const uint8_t *p = dl; p + 8 <= end; p += 8) {
            uint8_t op = p[0];
            if (op == 0x04 /* G_VTX */) {
                uint32_t addr = rn_be32(p + 4);
                if ((addr & 0xFF000000u) == 0x0E000000u) {
                    /* Segment 0x0E low 24 bits = byte offset into the pool
                     * (return_ptr_vertex_of_entry_room, lightfixture.c:150). */
                    cur_base = (int64_t)((addr & 0x00FFFFFFu) / vtx_stride);
                    cur_v0 = (int64_t)(p[1] & 0xF);
                } else {
                    cur_base = -1; /* absolute host ptr — cannot map, skip run */
                }
                continue;
            }

            uint32_t idx[3];
            int nsub;
            uint32_t sub[4][3];
            if (op == 0xBF /* G_TRI1 */) {
                /* indices at bytes 5,6,7, each /10 (base-GBI DMEM stride). */
                sub[0][0] = p[5] / 10u;
                sub[0][1] = p[6] / 10u;
                sub[0][2] = p[7] / 10u;
                nsub = 1;
            } else if (op == 0xB1 /* G_TRI4 */) {
                uint32_t w0 = rn_be32(p + 0);
                uint32_t w1 = rn_be32(p + 4);
                for (int k = 0; k < 4; k++) {
                    sub[k][0] = (w1 >> (8 * k)) & 0xF;      /* raw nibbles */
                    sub[k][1] = (w1 >> (8 * k + 4)) & 0xF;
                    sub[k][2] = (w0 >> (4 * k)) & 0xF;
                }
                nsub = 4;
            } else {
                continue;
            }
            if (cur_base < 0) {
                continue;
            }

            for (int s = 0; s < nsub; s++) {
                int64_t si0 = cur_base + (int64_t)sub[s][0] - cur_v0;
                int64_t si1 = cur_base + (int64_t)sub[s][1] - cur_v0;
                int64_t si2 = cur_base + (int64_t)sub[s][2] - cur_v0;
                if (si0 < 0 || si1 < 0 || si2 < 0) {
                    continue; /* slot below v0 — not part of this load */
                }
                idx[0] = (uint32_t)si0;
                idx[1] = (uint32_t)si1;
                idx[2] = (uint32_t)si2;
                if (idx[0] >= count || idx[1] >= count || idx[2] >= count) {
                    continue;
                }

                const int16_t *o0 = rn_ob(pool, vtx_stride, idx[0]);
                const int16_t *o1 = rn_ob(pool, vtx_stride, idx[1]);
                const int16_t *o2 = rn_ob(pool, vtx_stride, idx[2]);
                double e1[3] = { (double)o1[0] - o0[0], (double)o1[1] - o0[1],
                                 (double)o1[2] - o0[2] };
                double e2[3] = { (double)o2[0] - o0[0], (double)o2[1] - o0[1],
                                 (double)o2[2] - o0[2] };
                /* Area-weighted: |cross| = 2*triangle area. */
                double fn[3] = {
                    e1[1] * e2[2] - e1[2] * e2[1],
                    e1[2] * e2[0] - e1[0] * e2[2],
                    e1[0] * e2[1] - e1[1] * e2[0],
                };
                for (int c = 0; c < 3; c++) {
                    acc[(size_t)bin[idx[0]] * 3 + c] += fn[c];
                    acc[(size_t)bin[idx[1]] * 3 + c] += fn[c];
                    acc[(size_t)bin[idx[2]] * 3 + c] += fn[c];
                }
                tri_total++;
            }
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        const double *v = &acc[(size_t)bin[i] * 3];
        double mag = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (mag <= 1e-6) {
            continue; /* leaves {0,0,0} sentinel */
        }
        for (int c = 0; c < 3; c++) {
            double q = v[c] / mag * 127.0;
            long r = lround(q);
            if (r > 127) {
                r = 127;
            } else if (r < -127) {
                r = -127;
            }
            out_n[i][c] = (int8_t)r;
        }
    }

    free(bin);
    free(acc);
    free(ht);
    return tri_total;
}

/* ------------------------------------------------------------------ *
 *  Game-facing wrapper — lazy per-room cache keyed on the pool ptr.   *
 *  Reads g_BgRoomInfo (render-reads-sim; excluded from the unit test).*
 * ------------------------------------------------------------------ */

#ifndef GFX_ROOM_NORMALS_UNIT_TEST

#include <stdio.h>
#include <time.h>

#include <PR/gbi.h>
#include "bg.h"

/* MAXROOMCOUNT is 139/150; a fixed cap avoids pulling bondconstants.h and is
 * validated against g_MaxNumRooms at lookup. */
#define GFX_ROOM_NORMALS_MAX_ROOMS 256

struct RoomNormalsEntry {
    const void *pool_base; /* identity key: room's ptr_point_index */
    uint32_t count;        /* vertices in the pool */
    int8_t (*n)[3];        /* count entries, or NULL */
    uint8_t built;         /* attempted a build for the current pool_base */
};

static struct RoomNormalsEntry g_room_normals[GFX_ROOM_NORMALS_MAX_ROOMS];
static int g_env_normals_diag = -1; /* latch-once, like GE007_VERBOSE */
static int g_env_normals_rooms_built = 0;

static int gfx_env_normals_diag_enabled(void) {
    if (g_env_normals_diag < 0) {
        const char *e = getenv("GE007_ENV_NORMALS_DIAG");
        g_env_normals_diag = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return g_env_normals_diag;
}

void gfx_room_normals_reset(void) {
    for (int i = 0; i < GFX_ROOM_NORMALS_MAX_ROOMS; i++) {
        free(g_room_normals[i].n);
        g_room_normals[i].n = NULL;
        g_room_normals[i].pool_base = NULL;
        g_room_normals[i].count = 0;
        g_room_normals[i].built = 0;
    }
    g_env_normals_rooms_built = 0;
}

static void gfx_room_normals_build_room(int room_id,
                                        struct RoomNormalsEntry *e,
                                        const void *pool) {
    const s_room_info *ri = &g_BgRoomInfo[room_id];

    free(e->n);
    e->n = NULL;
    e->count = 0;
    e->built = 1;
    e->pool_base = pool;

    if (pool == NULL || ri->usize_point_index_binary <= 0) {
        return;
    }
    uint32_t count = (uint32_t)ri->usize_point_index_binary / (uint32_t)sizeof(Vtx);
    if (count == 0) {
        return;
    }
    int8_t (*n)[3] = (int8_t (*)[3])calloc((size_t)count, 3);
    if (n == NULL) {
        return;
    }

    struct timespec t0 = {0}, t1 = {0};
#ifndef PROJECT_TITAN_TGR
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
    uint32_t tris = gfx_room_normals_build(
        n, count, pool, (uint32_t)sizeof(Vtx),
        (const uint8_t *)ri->ptr_expanded_mapping_info,
        (uint32_t)ri->usize_primary_DL_binary,
        (const uint8_t *)ri->ptr_secondary_expanded_mapping_info,
        (uint32_t)ri->usize_secondary_DL_binary);
#ifndef PROJECT_TITAN_TGR
    clock_gettime(CLOCK_MONOTONIC, &t1);
#endif

    e->n = n;
    e->count = count;
    g_env_normals_rooms_built++;

    if (gfx_env_normals_diag_enabled()) {
        double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                    (double)(t1.tv_nsec - t0.tv_nsec) / 1.0e6;
        fprintf(stderr,
                "[ENV-NORMALS] room=%d verts=%u tris=%u build=%.3fms rooms=%d %s\n",
                room_id, count, tris, ms, g_env_normals_rooms_built,
                ms < 5.0 ? "built<5ms" : "SLOW");
        fflush(stderr);
    }
}

/* Resolve the room's normal table ONCE per G_VTX run (the per-vertex work in
 * gfx_sp_vertex then reduces to an array index + bounds compare). Returns the
 * normal array and writes the pool index of `vtx_src_addr` (the run's first
 * vertex) to *out_base_idx and the table size to *out_count, or NULL when the
 * run can't be relit (unknown room, stale/failed build, address outside the
 * pool, misaligned). Semantics identical to per-vertex gfx_room_normal_lookup
 * for the room-geometry stride (sizeof(Vtx)); the caller advances the index by
 * 1 per vertex, which equals the old per-vertex address math iff the source
 * stride is sizeof(Vtx) — the only stride room loads use. */
const int8_t (*gfx_room_normals_resolve(int room_id, uintptr_t vtx_src_addr,
                                        uint32_t *out_base_idx,
                                        uint32_t *out_count))[3] {
    if (room_id <= 0 || room_id >= GFX_ROOM_NORMALS_MAX_ROOMS) {
        return NULL;
    }
    if (room_id >= g_MaxNumRooms) {
        return NULL;
    }

    struct RoomNormalsEntry *e = &g_room_normals[room_id];
    const void *pool = g_BgRoomInfo[room_id].ptr_point_index;
    if (!e->built || e->pool_base != pool) {
        gfx_room_normals_build_room(room_id, e, pool);
    }
    if (e->n == NULL || e->count == 0 || e->pool_base == NULL) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)e->pool_base;
    if (vtx_src_addr < base) {
        return NULL;
    }
    uintptr_t off = vtx_src_addr - base;
    if (off % sizeof(Vtx) != 0) {
        return NULL;
    }
    uint32_t idx = (uint32_t)(off / sizeof(Vtx));
    if (idx >= e->count) {
        return NULL;
    }
    *out_base_idx = idx;
    *out_count = e->count;
    return (const int8_t (*)[3])e->n;
}

const int8_t *gfx_room_normal_lookup(int room_id, uintptr_t vtx_src_addr) {
    uint32_t base_idx = 0, count = 0;
    const int8_t (*n)[3] = gfx_room_normals_resolve(room_id, vtx_src_addr,
                                                    &base_idx, &count);
    if (n == NULL) {
        return NULL;
    }
    const int8_t *nn = n[base_idx];
    if (nn[0] == 0 && nn[1] == 0 && nn[2] == 0) {
        return NULL; /* {0,0,0} = no smooth normal */
    }
    return nn;
}

#endif /* !GFX_ROOM_NORMALS_UNIT_TEST */
