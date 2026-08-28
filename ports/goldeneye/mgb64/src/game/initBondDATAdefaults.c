#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <memp.h>
#include <bondtypes.h>
#include "chr.h"
#include "chrobjdata.h"
#include "initanitable.h"
#include "initBondDATAdefaults.h"
#include "objecthandler.h"
#include "player.h"
#include "bondhead.h"
#include "ge_debug.h"
#ifdef NATIVE_PORT
#include "ramromreplay.h"
#endif


//data
ModelRenderData D_8002A790 = {
    NULL,
    TRUE,
    0x00000003,
    NULL,

    NULL,
    0,
    0,
    0,

    0,
    0,
    0,
    0,

    0,
    {0,0,0,0},
    {0,0,0,0},
    CULLMODE_BOTH};

// forward declarations

void sub_GAME_7F0062C0(void *anim, s32 arg1, s32 arg2, s32 *arg3);

// end forward declarations

#ifdef NATIVE_PORT
static void scale_bondhead_matrix_translations(Mtxf *matrices, s32 count, f32 scale)
{
    s32 i;

    if (scale == 1.0f) {
        return;
    }

    for (i = 0; i < count; i++) {
        matrices[i].m[3][0] *= scale;
        matrices[i].m[3][1] *= scale;
        matrices[i].m[3][2] *= scale;
    }
}
#endif





/**
 * @param anim:
 * @param arg1:
 * @param arg2:
 * @param arg3: unknown type.
 *
 * Address 0x7F0062C0.
*/
void sub_GAME_7F0062C0(void *anim, s32 arg1, s32 arg2, s32 *arg3)
{
    // todo: is this a struct? see: sub_GAME_7F06D2E4
    s16 sp40[6];

    arg3[0] = 0;
    arg3[1] = 0;
    arg3[2] = 0;

    for (; arg1<arg2; arg1++)
    {
        sub_GAME_7F06D2E4(0, 0, &skeleton_guard, anim, arg1, &sp40[2]);
        arg3[0] += sp40[2];
        arg3[1] += sp40[3];
        arg3[2] += sp40[4];
    }
}



#ifdef REFRESH_PAL
#define ANIMRATE 1.2f
#define DAMPVAL 0.9166f
#define HEADSUM 11.990406f
#else
#define ANIMRATE 1.0f
#define DAMPVAL 0.93f
#define HEADSUM 14.285716f
#endif


void sets_a_bunch_of_BONDdata_values_to_default(void)
{
    s32 i;
    s32 spD0[3];
    ModelRenderData sp90;
    Mtxf sp50;

#ifdef LEFTOVERDEBUG
    if ((s32) player_gait_object_header.numRecords >= 0x1F)
    {
        return_null();
    }
#endif

#ifdef NATIVE_PORT
    /* On N64, Model was embedded in the player struct (192 bytes at offset 0x598).
     * On PC, it's a pointer (Model *model). Allocate the Model struct.
     * Also, the rwdata array (datas) on N64 was stored in the player struct's
     * s32 field_654 area (4 bytes per pointer). On PC, pointers are 8 bytes,
     * so we allocate a separate array. */
    /* The stage mempool is reset before this load path, so any surviving
     * player->model pointer is stale. Rebuild the lightweight gait model
     * unconditionally instead of trusting a non-NULL value from prior state. */
    g_CurrentPlayer->model = mempAllocBytesInBank(sizeof(Model), MEMPOOL_STAGE);

    if (g_CurrentPlayer->model != NULL) {
        /* Allocate rwdata pointer array (player_gait_object has ~4 nodes max, allocate 32) */
        u32 *rwdata_buf;

        memset(g_CurrentPlayer->model, 0, sizeof(Model));
        rwdata_buf = mempAllocBytesInBank(sizeof(uintptr_t) * 32, MEMPOOL_STAGE);

        if (rwdata_buf != NULL) {
            memset(rwdata_buf, 0, sizeof(uintptr_t) * 32);
            animInit(PLAYER_MODEL(g_CurrentPlayer), &player_gait_object_header, rwdata_buf);
            modelSetScale(PLAYER_MODEL(g_CurrentPlayer), IDO_POINT_ONE);
            modelSetAnimPlaySpeed(PLAYER_MODEL(g_CurrentPlayer), ANIMRATE, 0.0f);
        } else {
            g_CurrentPlayer->model = NULL;
        }
    }
#else
    animInit(&g_CurrentPlayer->model, &player_gait_object_header, &g_CurrentPlayer->field_654);
    modelSetScale(&g_CurrentPlayer->model, IDO_POINT_ONE);

#if defined (BUGFIX_R1)
    modelSetAnimPlaySpeed(&g_CurrentPlayer->model, ANIMRATE, 0.0f);
#endif
#endif

    g_CurrentPlayer->headanim = 0;
#ifdef NATIVE_PORT
    g_CurrentPlayer->field_5C0 = g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe;
#endif

    g_CurrentPlayer->headdamp = DAMPVAL;

    g_CurrentPlayer->headwalkingtime60 = 0;
    g_CurrentPlayer->headamplitude = 1.0f;
    g_CurrentPlayer->sideamplitude = 1.0f;
    g_CurrentPlayer->headpos.f[0] = 0.0f;
    g_CurrentPlayer->headpos.f[1] = 0.0f;
    g_CurrentPlayer->headpos.f[2] = 0.0f;
    g_CurrentPlayer->headlook.f[0] = 0.0f;
    g_CurrentPlayer->headlook.f[1] = 0.0f;
    g_CurrentPlayer->headlook.f[2] = 0.0f;
    g_CurrentPlayer->headup.f[0] = 0.0f;
    g_CurrentPlayer->headup.f[1] = 0.0f;
    g_CurrentPlayer->headup.f[2] = 0.0f;
    g_CurrentPlayer->headpossum.f[0] = 0.0f;
    g_CurrentPlayer->headpossum.f[1] = 0.0f;
    g_CurrentPlayer->headpossum.f[2] = 0.0f;
    g_CurrentPlayer->headlooksum.f[0] = 0.0f;
    g_CurrentPlayer->headlooksum.f[1] = 0.0f;
    g_CurrentPlayer->headlooksum.f[2] = HEADSUM;


    g_CurrentPlayer->headupsum.f[0] = 0.0f;
    g_CurrentPlayer->headupsum.f[1] = HEADSUM;
    g_CurrentPlayer->headupsum.f[2] = 0.0f;
    g_CurrentPlayer->resetheadpos = TRUE;
    g_CurrentPlayer->resetheadrot = TRUE;
    g_CurrentPlayer->resetheadtick = TRUE;
    g_CurrentPlayer->headbodyoffset.f[0] = 0.0f;
    g_CurrentPlayer->headbodyoffset.f[1] = 0.0f;
    g_CurrentPlayer->headbodyoffset.f[2] = 0.0f;
    g_CurrentPlayer->standheight = 0.0f;
    g_CurrentPlayer->standbodyoffset.x = 0.0f;
    g_CurrentPlayer->standbodyoffset.y = 0.0f;
    g_CurrentPlayer->standbodyoffset.z = 0.0f;
    g_CurrentPlayer->standfrac = 0.0f;
    g_CurrentPlayer->standlook[0].f[0] = 0.0f;
    g_CurrentPlayer->standlook[0].f[1] = 0.0f;
    g_CurrentPlayer->standlook[0].f[2] = 1.0f;
    g_CurrentPlayer->standlook[1].f[0] = 0.0f;
    g_CurrentPlayer->standlook[1].f[1] = 0.0f;
    g_CurrentPlayer->standlook[1].f[2] = 1.0f;
    g_CurrentPlayer->standup[0].f[0] = 0.0f;
    g_CurrentPlayer->standup[0].f[1] = 1.0f;
    g_CurrentPlayer->standup[0].f[2] = 0.0f;
    g_CurrentPlayer->standup[1].f[0] = 0.0f;
    g_CurrentPlayer->standup[1].f[1] = 1.0f;
    g_CurrentPlayer->standup[1].f[2] = 0.0f;
    g_CurrentPlayer->standcnt = 0;

#ifdef NATIVE_PORT
    if (PLAYER_MODEL(g_CurrentPlayer) != NULL && ptr_animation_table != NULL) {
        for (i=0; i<2; i++)
        {
            sub_GAME_7F0062C0(
                ANIM_FROM_OFFSET(g_BondMoveAnimationSetup[i].anim_id),
                (s32)g_BondMoveAnimationSetup[i].loopframe,
                (s32)g_BondMoveAnimationSetup[i].endframe,
                spD0);

            g_BondMoveAnimationSetup[i].speedMultiplier = (f32) (((f32) spD0[2] * IDO_POINT_ONE) / (g_BondMoveAnimationSetup[i].endframe - g_BondMoveAnimationSetup[i].loopframe));
        }

        sp90 = D_8002A790;

        modelSetAnimation(PLAYER_MODEL(g_CurrentPlayer),
            ANIM_ADDR(idle),
            0, 0.0f, 0.5f, 0.0f);

        subcalcpos(PLAYER_MODEL(g_CurrentPlayer));
        matrix_4x4_set_identity(&sp50);

        sp90.unk_matrix = &sp50;
        sp90.mtxlist = &g_CurrentPlayer->bondheadmatrices[0];

        subcalcmatrices(&sp90, PLAYER_MODEL(g_CurrentPlayer));

#ifdef NATIVE_PORT
        /* Bond's temporary head matrices are built under an identity parent.
         * Translation rows are in model units — the N64 camera/height system
         * consumes them directly in model units (standheight ~160, not ~16).
         * Do NOT scale by model->scale here; that would divide by 10x and
         * sink the camera into the floor. */
        GEDBG("INIT_BHEADM[0] trans=(%.3f, %.3f, %.3f) scale=%.6f",
              g_CurrentPlayer->bondheadmatrices[0].m[3][0],
              g_CurrentPlayer->bondheadmatrices[0].m[3][1],
              g_CurrentPlayer->bondheadmatrices[0].m[3][2],
              PLAYER_MODEL(g_CurrentPlayer)->scale);
#endif
        g_CurrentPlayer->standheight = g_CurrentPlayer->bondheadmatrices[0].m[3][1];
        GEDBG("INIT_STANDHEIGHT=%.3f (from bondheadmatrices[0].m[3][1])",
              g_CurrentPlayer->standheight);
        GEABORT_IF(!(g_CurrentPlayer->standheight > -50000.0f && g_CurrentPlayer->standheight < 50000.0f),
                   "standheight out of expected range: %.3f", g_CurrentPlayer->standheight);
        g_CurrentPlayer->standbodyoffset.x = 0.0f;
        g_CurrentPlayer->standbodyoffset.y = g_CurrentPlayer->bondheadmatrices[1].m[3][1] - g_CurrentPlayer->bondheadmatrices[0].m[3][1];
        g_CurrentPlayer->standbodyoffset.z = g_CurrentPlayer->bondheadmatrices[1].m[3][2] - g_CurrentPlayer->bondheadmatrices[0].m[3][2];

        modelSetAnimation(
            PLAYER_MODEL(g_CurrentPlayer),
            ANIM_FROM_OFFSET(g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].anim_id),
            0,
            g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe,
            0.5f,
            0.0f);

        modelSetAnimLooping(PLAYER_MODEL(g_CurrentPlayer), g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe, 0.0f);
        modelSetAnimEndFrame(PLAYER_MODEL(g_CurrentPlayer), g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].endframe);
        modelSetAnimFlipFunction(PLAYER_MODEL(g_CurrentPlayer), bheadFlipAnimation);
#ifdef NATIVE_PORT
        g_CurrentPlayer->field_5C0 = g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe;
#endif

#ifdef NATIVE_PORT
        if (!pcBheadDeferRamromFirstBlockIdleRoll())
#endif
        {
            bheadUpdateIdleRoll();
        }
    }
#else
    for (i=0; i<2; i++)
    {
        sub_GAME_7F0062C0(
            // match hack: addu address calculated backwards
            (void*)((s32)g_BondMoveAnimationSetup[i].anim_id + (s32)&ptr_animation_table->data),
            (s32)g_BondMoveAnimationSetup[i].loopframe,
            (s32)g_BondMoveAnimationSetup[i].endframe,
            &spD0);

        g_BondMoveAnimationSetup[i].speedMultiplier = (f32) (((f32) spD0[2] * IDO_POINT_ONE) / (g_BondMoveAnimationSetup[i].endframe - g_BondMoveAnimationSetup[i].loopframe));
    }

    sp90 = D_8002A790;

#ifdef NATIVE_PORT
    /* PTR_ANIM_idle == (offset of)&ANIM_DATA_idle; use the offset constant so the
     * port links without the ROM-derived data symbol (data is loaded from the ROM
     * at runtime). N64 path keeps the original symbol reference for matching. */
    modelSetAnimation(&g_CurrentPlayer->model, (struct ModelAnimation *)&ptr_animation_table->data[(s32)PTR_ANIM_idle], 0, 0.0f, 0.5f, 0.0f);
#else
    modelSetAnimation(&g_CurrentPlayer->model, (struct ModelAnimation *)&ptr_animation_table->data[(s32)&ANIM_DATA_idle], 0, 0.0f, 0.5f, 0.0f);
#endif

    subcalcpos(&g_CurrentPlayer->model);
    matrix_4x4_set_identity(&sp50);

    sp90.unk_matrix = &sp50;
    sp90.mtxlist = &g_CurrentPlayer->bondheadmatrices[0];

    subcalcmatrices(&sp90, &g_CurrentPlayer->model);

    g_CurrentPlayer->standheight = g_CurrentPlayer->bondheadmatrices[0].m[3][1];
    g_CurrentPlayer->standbodyoffset.x = 0.0f;
    g_CurrentPlayer->standbodyoffset.y = g_CurrentPlayer->bondheadmatrices[1].m[3][1] - g_CurrentPlayer->bondheadmatrices[0].m[3][1];
    g_CurrentPlayer->standbodyoffset.z = g_CurrentPlayer->bondheadmatrices[1].m[3][2] - g_CurrentPlayer->bondheadmatrices[0].m[3][2];

    modelSetAnimation(
        &g_CurrentPlayer->model,
        // match hack: addu address calculated backwards
        (struct ModelAnimation *) ((s32)g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].anim_id + (s32)&ptr_animation_table->data),
        0,
        g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe,
        0.5f,
        0.0f);

    modelSetAnimLooping(&g_CurrentPlayer->model, g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe, 0.0f);
    modelSetAnimEndFrame(&g_CurrentPlayer->model, g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].endframe);
    modelSetAnimFlipFunction(&g_CurrentPlayer->model, bheadFlipAnimation);

    bheadUpdateIdleRoll();
#endif
}
