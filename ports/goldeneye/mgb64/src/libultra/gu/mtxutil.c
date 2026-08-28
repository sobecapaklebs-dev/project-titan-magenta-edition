/*
 * Clean-room libultra-style matrix conversion and identity helpers for the
 * matching target.
 */

#include "guint.h"

void guMtxF2L(float mf[4][4], Mtx *m)
{
    int row;
    int pair;
    s32 fixed_a;
    s32 fixed_b;
    u32 *whole_words;
    u32 *frac_words;

    whole_words = (u32 *)&m->m[0][0];
    frac_words = (u32 *)&m->m[2][0];

    for (row = 0; row < 4; row++) {
        for (pair = 0; pair < 2; pair++) {
            fixed_a = FTOFIX32(mf[row][pair * 2]);
            fixed_b = FTOFIX32(mf[row][pair * 2 + 1]);
            *whole_words++ = ((u32)fixed_a & 0xffff0000) | (((u32)fixed_b >> 16) & 0x0000ffff);
            *frac_words++ = (((u32)fixed_a << 16) & 0xffff0000) | ((u32)fixed_b & 0x0000ffff);
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx *m)
{
    int row;
    int pair;
    u32 whole;
    u32 frac;
    u32 fixed_a;
    u32 fixed_b;
    u32 *whole_words;
    u32 *frac_words;

    whole_words = (u32 *)&m->m[0][0];
    frac_words = (u32 *)&m->m[2][0];

    for (row = 0; row < 4; row++) {
        for (pair = 0; pair < 2; pair++) {
            whole = *whole_words++;
            frac = *frac_words++;
            fixed_a = (whole & 0xffff0000) | ((frac >> 16) & 0x0000ffff);
            fixed_b = ((whole << 16) & 0xffff0000) | (frac & 0x0000ffff);
            mf[row][pair * 2] = FIX32TOF((s32)fixed_a);
            mf[row][pair * 2 + 1] = FIX32TOF((s32)fixed_b);
        }
    }
}

void guMtxIdentF(float mf[4][4])
{
    int row;
    int col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            mf[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
}

void guMtxIdent(Mtx *m)
{
    Matrix mf;

    guMtxIdentF(mf);
    guMtxF2L(mf, m);
}
