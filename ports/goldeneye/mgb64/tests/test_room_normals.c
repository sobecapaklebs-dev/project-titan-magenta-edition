/* ROM-free unit test for the smooth-env-normals builder core (W1.E1.T1).
 *
 * A synthetic "tent": two quad panels meeting at a shared ridge edge. The ridge
 * vertices are DUPLICATED per panel (distinct pool indices, byte-identical
 * positions) exactly as GE rooms duplicate edge verts across quads with
 * independent UVs. The position-merge must therefore produce BYTE-IDENTICAL
 * normals at the duplicated ridge indices, while the non-shared base vertices of
 * the two panels keep their own (opposite-Z) normals. That difference is the
 * whole point: merging by position is what erases the per-quad seam. */

#include "gfx_room_normals.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NDEBUG-proof check: assert() vanishes in Release builds, which would make
 * this test pass vacuously. CHECK always evaluates and always fails loudly. */
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* Native (host-endian) 16-byte Vtx: ob[3] int16 at offset 0. */
static void put_pos(uint8_t *pool, uint32_t idx, int16_t x, int16_t y, int16_t z) {
    int16_t ob[3] = { x, y, z };
    memcpy(pool + (size_t)idx * 16, ob, sizeof ob);
}

/* One 8-byte G_VTX loading the whole pool from segment 0x0E offset 0. */
static void emit_vtx(uint8_t *cmd) {
    memset(cmd, 0, 8);
    cmd[0] = 0x04;             /* G_VTX */
    cmd[4] = 0x0E;             /* addr = 0x0E000000 -> pool base index 0 */
}

/* One 8-byte G_TRI1; indices are encoded *10 (the builder does /10). */
static void emit_tri1(uint8_t *cmd, uint32_t a, uint32_t b, uint32_t c) {
    memset(cmd, 0, 8);
    cmd[0] = 0xBF;             /* G_TRI1 */
    cmd[5] = (uint8_t)(a * 10);
    cmd[6] = (uint8_t)(b * 10);
    cmd[7] = (uint8_t)(c * 10);
}

int main(void) {
    /* Pool: 8 verts. 0..3 = left panel, 4..7 = right panel.
     * Ridge R0=(0,10,0) at indices 0 and 4; R1=(10,10,0) at indices 1 and 5. */
    uint8_t pool[8 * 16];
    memset(pool, 0, sizeof pool);
    put_pos(pool, 0, 0, 10, 0);    /* R0 (left copy)  */
    put_pos(pool, 1, 10, 10, 0);   /* R1 (left copy)  */
    put_pos(pool, 2, 10, 0, -10);  /* L1 (left base)  */
    put_pos(pool, 3, 0, 0, -10);   /* L0 (left base)  */
    put_pos(pool, 4, 0, 10, 0);    /* R0 (right copy) */
    put_pos(pool, 5, 10, 10, 0);   /* R1 (right copy) */
    put_pos(pool, 6, 10, 0, 10);   /* RB1 (right base)*/
    put_pos(pool, 7, 0, 0, 10);    /* RB0 (right base)*/

    /* DL: 1 G_VTX + 4 G_TRI1, wound so every face normal points +Y (up). */
    uint8_t dl[5 * 8];
    emit_vtx(&dl[0 * 8]);
    emit_tri1(&dl[1 * 8], 0, 1, 2); /* left  -> (0,+,-) */
    emit_tri1(&dl[2 * 8], 0, 2, 3); /* left  -> (0,+,-) */
    emit_tri1(&dl[3 * 8], 4, 7, 6); /* right -> (0,+,+) */
    emit_tri1(&dl[4 * 8], 4, 6, 5); /* right -> (0,+,+) */

    int8_t out_n[8][3];
    uint32_t tris = gfx_room_normals_build(out_n, 8, pool, 16, dl, sizeof dl,
                                           NULL, 0);

    /* 1. All four triangles were decoded and processed. */
    CHECK(tris == 4);

    /* 2. Position-merge invariant: duplicated ridge positions => IDENTICAL
     *    normals (the seam fix). */
    CHECK(memcmp(out_n[0], out_n[4], 3) == 0);
    CHECK(memcmp(out_n[1], out_n[5], 3) == 0);

    /* 3. The ridge normal is a real, averaged normal pointing straight up:
     *    the two panels' opposite-Z faces cancel in Z, leaving +Y only. */
    CHECK(!(out_n[0][0] == 0 && out_n[0][1] == 0 && out_n[0][2] == 0));
    CHECK(out_n[0][0] == 0);
    CHECK(out_n[0][2] == 0);
    CHECK(out_n[0][1] > 120);

    /* 4. Non-shared base vertices keep their own panel's normal — DIFFERENT
     *    positions must NOT be merged: the left base has -Z, the right +Z. */
    CHECK(out_n[2][2] < -50); /* L1  (left base)  */
    CHECK(out_n[6][2] > 50);  /* RB1 (right base) */
    CHECK(out_n[2][1] > 50);  /* still mostly up  */
    CHECK(out_n[6][1] > 50);

    /* 5. Degenerate / empty inputs are safe and produce zeroed output. */
    int8_t z_n[2][3];
    memset(z_n, 0x7F, sizeof z_n);
    uint32_t zt = gfx_room_normals_build(z_n, 2, pool, 16, NULL, 0, NULL, 0);
    CHECK(zt == 0);
    CHECK(z_n[0][0] == 0 && z_n[0][1] == 0 && z_n[0][2] == 0);
    CHECK(gfx_room_normals_build(NULL, 0, NULL, 0, NULL, 0, NULL, 0) == 0);

    printf("test_room_normals: PASS (tris=%u ridge=(%d,%d,%d) leftbase=(%d,%d,%d) "
           "rightbase=(%d,%d,%d))\n",
           tris, out_n[0][0], out_n[0][1], out_n[0][2], out_n[2][0], out_n[2][1],
           out_n[2][2], out_n[6][0], out_n[6][1], out_n[6][2]);
    return 0;
}
