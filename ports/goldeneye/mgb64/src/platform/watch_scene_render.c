/*
 * watch_scene_render.c — see watch_scene_render.h.
 *
 * 16.0f / 11.0f folds to exactly 0x3FBA2E8C (== retail's immediate a3), and the
 * legacy 1.456f folds to 0x3FBA5E35 (== the pre-fix port literal). The tint word
 * is the same big-endian (r<<24)|(g<<16)|(b<<8)|a packing the verified gun
 * subdraw sibling uses (gun.c:15002-15006). See FID-0068.
 */
#include "watch_scene_render.h"

float watchScenePerspAspect(int legacy)
{
    if (legacy) {
        return 1.456f;      /* port defect: 0x3FBA5E35 */
    }
    return 16.0f / 11.0f;   /* retail: 0x3FBA2E8C = 1.4545455 (c_perspaspect) */
}

unsigned watchSceneTintWord(int watch_state, int r, int g, int b, int a)
{
    if (watch_state == 5 || watch_state == 12) {
        return 205u;        /* raise/lower: black tint, alpha 205 darkening */
    }
    return ((unsigned)(r & 0xFF) << 24)
         | ((unsigned)(g & 0xFF) << 16)
         | ((unsigned)(b & 0xFF) << 8)
         | (unsigned)(a & 0xFF);
}
