/*
 * Clean-room libultra-style reflective look-at helpers for the matching target.
 */

#include "guint.h"

static void guSetLookAtLight(Light *light, float x, float y, float z, u8 green)
{
    light->l.dir[0] = FTOFRAC8(x);
    light->l.dir[1] = FTOFRAC8(y);
    light->l.dir[2] = FTOFRAC8(z);
    light->l.col[0] = 0x00;
    light->l.col[1] = green;
    light->l.col[2] = 0x00;
    light->l.pad1 = 0x00;
    light->l.colc[0] = 0x00;
    light->l.colc[1] = green;
    light->l.colc[2] = 0x00;
    light->l.pad2 = 0x00;
}

void guLookAtReflectF(float mf[4][4], LookAt *l,
                      float xEye, float yEye, float zEye,
                      float xAt, float yAt, float zAt,
                      float xUp, float yUp, float zUp)
{
    guLookAtF(mf, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);

    guSetLookAtLight(&l->l[0], mf[0][0], mf[1][0], mf[2][0], 0x00);
    guSetLookAtLight(&l->l[1], mf[0][1], mf[1][1], mf[2][1], 0x80);
}

void guLookAtReflect(Mtx *m, LookAt *l, float xEye, float yEye, float zEye,
                     float xAt, float yAt, float zAt,
                     float xUp, float yUp, float zUp)
{
    Matrix mf;

    guLookAtReflectF(mf, l, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    guMtxF2L(mf, m);
}
