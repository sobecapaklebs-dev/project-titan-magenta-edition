/*
 * decor_native.c -- DL emitter for the scene-decoration layer (NATIVE_PORT).
 *
 * Contract (docs/design/remaster-aaa/09-surface-showcase.md §7):
 *  - Render-only. Reads only render-side state (current_model_pos, fog
 *    globals, the frame DL cursor); never writes sim state. The dyn gfx/vtx
 *    pools are render-only and double-buffered, so nothing here can feed
 *    back into the simulation.
 *  - Video.SceneDecor=0 (default) emits ZERO commands: faithful mode stays
 *    byte-identical by construction.
 *  - Draws at the seam right after bgLevelRender: Z-tests against rooms,
 *    composites under characters/effects/viewmodel/HUD. bgLevelRender leaves
 *    WEAPON matrices loaded, so we re-establish world PROJECTION/MODELVIEW
 *    (the room pattern) and restore the weapon matrices when done.
 *  - Room coordinate convention: MODELVIEW scale carries room_data_float2
 *    (world -> render units) and the translation is camera-relative
 *    (world*scale - current_model_pos), mirroring
 *    setupRoomTransformationMatrix.
 *  - Vertices live in persistent malloc'd blocks registered via
 *    gfx_register_pc_vertex_region (native Vtx), so the per-frame vtx pool
 *    is untouched; only DL commands consume the (256 KB) gfx pool, and the
 *    emitter budget-checks dynGetFreeGfx and skips (fail-closed, the dyn.c
 *    overflow contract) rather than overflow.
 */
#include <ultra64.h>
#include <PR/gbi.h>
#include <gbi_extension.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bg.h"
#include "bondview.h"
#include "boss.h"
#include "decor.h"
#include "decor_assets.h"
#include "dyn.h"
#include "fog.h"
#include "player.h"

extern s32 g_pcSceneDecor;         /* Video.SceneDecor (platform_sdl.c) */
extern char g_pcSceneDecorDir[];   /* Video.SceneDecorDir */
extern Mtx *g_viProjectionMatrix;

static DecorLevel s_level;
static s32 s_loadedStage = -2; /* -2 = never loaded; -1 = load failed/empty */
static Mtxf s_mtx[DECOR_MAX_INSTANCES];
static s32 s_budgetWarned;
static s32 s_traceDecor = -1;   /* GE007_TRACE_DECOR, resolved once */
static s32 s_onlyPrim = -2;     /* GE007_DECOR_ONLY_PRIM bisect diag, -2=unset */

static void decorEnsureLevel(void) {
    s32 stage = bossGetStageNum();
    if (stage == s_loadedStage) {
        return;
    }
    decorAssetsFree(&s_level);
    s_loadedStage = stage;
    s_budgetWarned = 0;
    {
        const char *slug = pcStageSlugForLevelId(stage);
        if (slug == NULL || !decorAssetsLoadLevel(g_pcSceneDecorDir, slug, &s_level)) {
            s_level.ninst = 0;
        }
    }
}

/* Commands this frame will append (matrices + vertex batches + tris + the
 * fixed state prologue/epilogue), for the fail-closed pool check. */
static s32 decorCommandEstimate(void) {
    s32 cmds = 64;
    s32 i, p, b;
    for (i = 0; i < s_level.ninst; i++) {
        const DecorModel *m = &s_level.models[s_level.inst[i].model];
        cmds += 2; /* matrix + slack */
        cmds += m->nmmesh * 2; /* modern path: matrix + mesh cmd per prim */
        for (p = 0; p < m->nprims; p++) {
            for (b = 0; b < m->prims[p].nbatches; b++) {
                cmds += 1 + (m->prims[p].batches[b].tcount + 1) / 2;
            }
        }
    }
    /* per-prim texture/state rebinds, prim-major loop */
    for (i = 0; i < s_level.nmodels; i++) {
        cmds += s_level.models[i].nprims * 16;
    }
    return cmds;
}

static void decorFillMatrix(Mtxf *dst, const DecorInstance *in, f32 vquant) {
    f32 s = in->scale * room_data_float2 / vquant;
    f32 yaw = in->yaw_deg * (M_PI_F / 180.0f);
    f32 c = cosf(yaw) * s;
    f32 sn = sinf(yaw) * s;
    memset(dst, 0, sizeof(*dst));
    dst->m[0][0] = c;
    dst->m[0][2] = -sn;
    dst->m[1][1] = s;
    dst->m[2][0] = sn;
    dst->m[2][2] = c;
    dst->m[3][0] = in->pos[0] * room_data_float2 - g_CurrentPlayer->current_model_pos.f[0];
    dst->m[3][1] = in->pos[1] * room_data_float2 - g_CurrentPlayer->current_model_pos.f[1];
    dst->m[3][2] = in->pos[2] * room_data_float2 - g_CurrentPlayer->current_model_pos.f[2];
    dst->m[3][3] = 1.0f;
}

static int decor_log2i(int v) {
    int r = 0;
    while (v > 1) {
        v >>= 1;
        r++;
    }
    return r;
}

Gfx *decorRender(Gfx *gdl) {
    s32 i, p, b, t;
    s32 pass;

    if (!g_pcSceneDecor) {
        return gdl; /* OFF: zero commands, byte-identical frames */
    }
    if (s_traceDecor < 0) {
        const char *e = getenv("GE007_TRACE_DECOR");
        const char *o = getenv("GE007_DECOR_ONLY_PRIM");
        s_traceDecor = (e != NULL && *e != '\0');
        s_onlyPrim = (o != NULL && *o != '\0') ? atoi(o) : -1;
    }
    decorEnsureLevel();
    if (s_level.ninst == 0) {
        return gdl;
    }
    if (dynGetFreeGfx(gdl) < decorCommandEstimate() + 512) {
        if (!s_budgetWarned) {
            s_budgetWarned = 1;
            fprintf(stderr, "[DECOR] WARN gfx pool budget exceeded; decor skipped\n");
        }
        return gdl;
    }

    for (i = 0; i < s_level.ninst; i++) {
        decorFillMatrix(&s_mtx[i], &s_level.inst[i],
                        s_level.models[s_level.inst[i].model].vquant);
    }

    /* world-space matrices + room-style state (bgLevelRender left the weapon
     * matrices loaded; see bg.c tail). Rooms draw under field_10E0 -- the
     * combined WORLD-TO-CLIP (view x perspective) matrix; projmatrix(f) is
     * only the bare perspective and would leave the camera rotation out. */
    {
        void *world_to_clip = (void *)(uintptr_t)get_BONDdata_field_10E0();
        if (world_to_clip == NULL) {
            return gdl; /* view not set up yet this frame */
        }
        if (s_traceDecor) {
            fprintf(stderr,
                    "[DECOR-EMIT] w2c=%p float=%d inst0_row3=(%.1f %.1f %.1f) "
                    "free_gfx=%d est=%d\n",
                    world_to_clip, (int)bondviewField10E0IsFloat(),
                    s_mtx[0].m[3][0], s_mtx[0].m[3][1], s_mtx[0].m[3][2],
                    dynGetFreeGfx(gdl), decorCommandEstimate());
        }
        gSPMatrix(gdl++, world_to_clip,
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION |
                      (bondviewField10E0IsFloat() ? G_MTX_FLOAT_PORT : 0));
    }
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK |
                                  (g_FogSkyIsEnabled ? G_FOG : 0));
    gSPClearGeometryMode(gdl++, G_TEXTURE_GEN | G_LIGHTING);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gdl = fogSetRenderFogColor(gdl, 0);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);

    /* pass 0 = opaque prims, pass 1 = alpha-cutout prims (TEX_EDGE class,
     * double-sided): grouped so cull/rendermode toggles once per class */
    for (pass = 0; pass < 2; pass++) {
        int class_open = 0;
        for (i = 0; i < s_level.nmodels; i++) {
            DecorModel *m = &s_level.models[i];
            if (m->modern) {
                /* Full-fidelity path: one G_MODERNMESH per prim per instance
                 * under the instance MODELVIEW. The backend draws with float
                 * verts + mipmapped textures; no N64 texture/vertex commands
                 * (and no class state) are needed. */
                s32 k, inst;
                for (k = 0; k < m->nmmesh; k++) {
                    if ((m->mmesh[k].cutout ? 1 : 0) != pass) {
                        continue;
                    }
                    if (s_onlyPrim >= 0 && s_onlyPrim != k) {
                        continue;
                    }
                    for (inst = 0; inst < s_level.ninst; inst++) {
                        if (s_level.inst[inst].model != i) {
                            continue;
                        }
                        gSPMatrix(gdl++, &s_mtx[inst],
                                  G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH |
                                      G_MTX_FLOAT_PORT);
                        gdl->words.w0 = ((uint32_t)G_MODERNMESH) << 24;
                        gdl->words.w1 = (uintptr_t)&m->mmesh[k];
                        GFX_DL_REGISTER_PTR(gdl->words.w1); /* wasm32: record host ptr for DL resolve */
                        gdl++;
                    }
                }
                continue;
            }
            for (p = 0; p < m->nprims; p++) {
                const DecorPrim *pr = &m->prims[p];
                const DecorTexture *tx = &m->tex[pr->tex];
                s32 inst;
                if (pr->cutout != pass) {
                    continue;
                }
                if (s_onlyPrim >= 0 && s_onlyPrim != p) {
                    continue; /* GE007_DECOR_ONLY_PRIM bisect diag */
                }
                if (!class_open) {
                    class_open = 1;
                    gDPPipeSync(gdl++);
                    if (pass == 0) {
                        gDPSetRenderMode(gdl++,
                                         g_FogSkyIsEnabled ? G_RM_FOG_SHADE_A
                                                           : G_RM_AA_ZB_OPA_SURF,
                                         G_RM_AA_ZB_OPA_SURF2);
                    } else {
                        gDPSetRenderMode(gdl++,
                                         g_FogSkyIsEnabled ? G_RM_FOG_SHADE_A
                                                           : G_RM_AA_ZB_TEX_EDGE,
                                         G_RM_AA_ZB_TEX_EDGE2);
                        gSPClearGeometryMode(gdl++, G_CULL_BACK);
                    }
                }
                gDPPipeSync(gdl++);
                gDPLoadTextureBlock(gdl++, tx->rgba16, G_IM_FMT_RGBA,
                                    G_IM_SIZ_16b, tx->w, tx->h, 0,
                                    G_TX_WRAP, G_TX_WRAP, decor_log2i(tx->w),
                                    decor_log2i(tx->h), G_TX_NOLOD,
                                    G_TX_NOLOD);
                for (inst = 0; inst < s_level.ninst; inst++) {
                    if (s_level.inst[inst].model != i) {
                        continue;
                    }
                    gSPMatrix(gdl++, &s_mtx[inst],
                              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH |
                                  G_MTX_FLOAT_PORT);
                    for (b = 0; b < pr->nbatches; b++) {
                        const DecorBatch *bt = &pr->batches[b];
                        /* raw pointer, NOT osVirtualToPhysical: the port's
                         * gSPVertex stores the full uintptr_t, and the os
                         * shim returns u32 -- it would truncate a 64-bit
                         * heap pointer (static dyn-pool buffers dodge this;
                         * malloc'd decor blocks do not). */
                        GFX_DL_REGISTER_PTR(bt->verts); /* wasm32: record host ptr for DL resolve */
                        gSPVertex(gdl++, bt->verts, bt->vcount, 0);
                        for (t = 0; t + 1 < bt->tcount; t += 2) {
                            gSP2Triangles(gdl++, bt->tris[t][0], bt->tris[t][1],
                                          bt->tris[t][2], 0, bt->tris[t + 1][0],
                                          bt->tris[t + 1][1],
                                          bt->tris[t + 1][2], 0);
                        }
                        if (t < bt->tcount) {
                            /* odd count: emit the last tri twice as a pair --
                             * bit-for-bit the same encoding path as the rest
                             * (gSP2Triangles is the only tri form the native
                             * DL path is exercised with; see explosions.c) */
                            gSP2Triangles(gdl++, bt->tris[t][0], bt->tris[t][1],
                                          bt->tris[t][2], 0, bt->tris[t][0],
                                          bt->tris[t][1], bt->tris[t][2], 0);
                        }
                    }
                }
            }
        }
        if (class_open && pass == 1) {
            gSPSetGeometryMode(gdl++, G_CULL_BACK);
        }
    }

    /* restore the state bgLevelRender's tail established (weapon matrices) */
    gDPPipeSync(gdl++);
    gdl = fogRenderClearFogMode(gdl);
    gSPMatrix(gdl++, g_viProjectionMatrix,
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    return bondviewGfxPlayerField5cMatrix(gdl);
}
