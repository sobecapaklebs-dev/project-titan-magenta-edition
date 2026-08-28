#include <ultra64.h>
#include <limits.h>
#include <memp.h>
#include <assets/animationtable_data.h>
#include <random.h>
#include "bondview.h"
#include "chrai.h"
#include "chrobjdata.h"
#include "initanitable.h"
#include "lvl.h"
#include "matrixmath.h"
#include "objecthandler.h"
#include "player.h"
#include "bondhead.h"
#include "ge_debug.h"
#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdlib.h>
#include "ramromreplay.h"
#endif


/**
 * Address 0x80036AD0.
*/
struct init_bond_anim_unk g_BondMoveAnimationSetup[2] = {
    // address 0x80036AD0 = g_BondMoveAnimationSetup + 0
    {PTR_ANIM_bond_eye_walk, 9.5f, 27.0f, 0.0f, 0.0f, 1.5f},
    // address 0x80036AE8 = g_BondMoveAnimationSetup + 24
    {PTR_ANIM_sprinting, 7.5f, 17.0f, 0.0f, 1.5f, 100.0f}
};





// forward declarations

void bheadUpdatePos(coord3d *vel);
void bheadUpdateRot(coord3d *lookvel, coord3d *upvel);
void bheadSetdamp(f32 headdamp);
void bheadFlipAnimation(void);

// end forward declarations

#ifdef NATIVE_PORT
static s32 s_pcRamromDeferredBheadIdleRollCount = 0;

#ifdef REFRESH_PAL
#define BHEAD_NATIVE_ANIMRATE 1.2f
#else
#define BHEAD_NATIVE_ANIMRATE 1.0f
#endif

static s32 bheadNativeFiniteF32(f32 value)
{
    return value == value
        && value > -3.402823466e+38f
        && value < 3.402823466e+38f;
}

static s32 bheadNativeFrameF32Usable(f32 value)
{
    return value == value
        && value > -1000000.0f
        && value < 1000000.0f;
}

static s32 bheadNativeAnimPointerUsable(ModelAnimation *anim)
{
    uintptr_t base;
    uintptr_t addr;
    uintptr_t table_bytes;

    if (anim == NULL || ptr_animation_table == NULL) {
        return FALSE;
    }

    base = (uintptr_t)ptr_animation_table;
    addr = (uintptr_t)anim;
    table_bytes = sizeof(*ptr_animation_table);

    return addr >= base
        && addr <= base + table_bytes - sizeof(*anim);
}

static s32 bheadNativePlayerGaitModelUsable(void)
{
    Model *model;

    if (g_CurrentPlayer == NULL || PLAYER_MODEL(g_CurrentPlayer) == NULL) {
        return FALSE;
    }

    model = PLAYER_MODEL(g_CurrentPlayer);

    if (!mempPtrIsInBank(model, MEMPOOL_STAGE, sizeof(*model))) {
        return FALSE;
    }

    if (model->obj != &player_gait_object_header
        || player_gait_object_header.RootNode == NULL
        || model->datas == NULL
        || !mempPtrIsInBank(model->datas, MEMPOOL_STAGE, sizeof(uintptr_t))
        || !bheadNativeFiniteF32(model->scale)
        || !bheadNativeFiniteF32(model->speed)
        || !bheadNativeFiniteF32(model->playspeed)
        || model->scale <= 0.0f
        || model->scale > 100.0f
        || (model->animlooping != 0 && model->anim == NULL))
    {
        return FALSE;
    }

    return TRUE;
}

static s32 bheadNativePlayerGaitFrameStateUsable(Model *model)
{
    s32 anim_frames;
    f32 max_frame;

    if (model == NULL
        || model->anim == NULL
        || ptr_animation_table == NULL
        || !bheadNativeAnimPointerUsable(model->anim)
        || !bheadNativeFrameF32Usable(model->unk28)
        || !bheadNativeFrameF32Usable(model->unk2c)
        || !bheadNativeFrameF32Usable(model->unk58)
        || !bheadNativeFrameF32Usable(model->unk5c)
        || !bheadNativeFrameF32Usable(model->speed)
        || !bheadNativeFrameF32Usable(model->newspeed)
        || !bheadNativeFrameF32Usable(model->oldspeed)
        || !bheadNativeFrameF32Usable(model->timespeed)
        || !bheadNativeFrameF32Usable(model->elapsespeed)
        || !bheadNativeFrameF32Usable(model->playspeed)
        || !bheadNativeFrameF32Usable(model->animrate)
        || !bheadNativeFrameF32Usable(model->animloopframe)
        || !bheadNativeFrameF32Usable(model->animloopmerge)
        || !bheadNativeFrameF32Usable(model->endframe))
    {
        return FALSE;
    }

    anim_frames = model->anim->unk04;

    if (anim_frames <= 0 || anim_frames > 10000) {
        return FALSE;
    }

    max_frame = (f32)(anim_frames - 1);

    if (model->unk28 < -1.0f
        || model->unk28 > max_frame + 1.0f
        || model->unk2c < -0.01f
        || model->unk2c > 1.01f
        || model->framea < 0
        || model->framea >= anim_frames
        || model->frameb < 0
        || model->frameb >= anim_frames
        || model->speed < -20.0f
        || model->speed > 20.0f
        || model->playspeed <= 0.0f
        || model->playspeed > 10.0f
        || model->animrate <= 0.0f
        || model->animrate > 10.0f
        || model->animloopframe < -1.0f
        || model->animloopframe > max_frame + 1.0f
        || model->animloopmerge < 0.0f
        || model->animloopmerge > 120.0f
        || model->endframe < -1.0f
        || model->endframe > max_frame + 1.0f)
    {
        return FALSE;
    }

    if (model->anim2 != NULL) {
        s32 anim2_frames;
        f32 max_frame2;

        if (!bheadNativeAnimPointerUsable(model->anim2)) {
            return FALSE;
        }

        anim2_frames = model->anim2->unk04;
        max_frame2 = (f32)(anim2_frames - 1);

        if (anim2_frames <= 0
            || anim2_frames > 10000
            || model->unk58 < -1.0f
            || model->unk58 > max_frame2 + 1.0f
            || model->unk5c < -0.01f
            || model->unk5c > 1.01f
            || model->frame2a < 0
            || model->frame2a >= anim2_frames
            || model->frame2b < 0
            || model->frame2b >= anim2_frames)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static s32 bheadNativeNormalizeHeadAnimIndex(void)
{
    if (g_CurrentPlayer == NULL
        || g_CurrentPlayer->headanim < 0
        || g_CurrentPlayer->headanim > 1)
    {
        return 0;
    }

    return g_CurrentPlayer->headanim;
}

static void bheadNativeClampCurrentHeadAnimIndex(void)
{
    if (g_CurrentPlayer != NULL
        && (g_CurrentPlayer->headanim < -1 || g_CurrentPlayer->headanim > 1))
    {
        g_CurrentPlayer->headanim = 0;
    }
}

/* §4.4 item 4: bondhead gait clamp gate. No ASM counterpart -- the stock
 * (non-NATIVE_PORT) gait-frame math is never range-clamped, and this native
 * clamp can nudge Bond's gait animation frame (and therefore head-bob-derived
 * shot origin) by a hair versus an unclamped run. Default ON (current
 * behavior); OFF under --faithful or GE007_BONDHEAD_GAIT_CLAMP=0. The
 * non-finite/degenerate-range safety net below stays active either way.
 * Non-static: main_pc.c's `--faithful` boot log reads this state. */
s32 bheadGaitClampEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *env = getenv("GE007_BONDHEAD_GAIT_CLAMP");
        extern int g_pcFaithfulSim;

        enabled = env ? (atoi(env) != 0) : (g_pcFaithfulSim ? 0 : 1);
    }

    return enabled;
}

static f32 bheadNativeClampMoveAnimFrame(s32 headanim, f32 frame)
{
    f32 loopframe;
    f32 endframe;

    if (headanim < 0 || headanim > 1) {
        headanim = 0;
    }

    loopframe = g_BondMoveAnimationSetup[headanim].loopframe;
    endframe = g_BondMoveAnimationSetup[headanim].endframe;

    if (!bheadNativeFrameF32Usable(frame) || endframe <= loopframe) {
        return loopframe;
    }

    if (!bheadGaitClampEnabled()) {
        return frame;
    }

    if (frame < loopframe) {
        return loopframe;
    }

    if (frame > endframe) {
        return endframe;
    }

    return frame;
}

static f32 bheadNativeCurrentMoveAnimFrame(s32 headanim, Model *model)
{
    f32 frame;
    f32 loopframe;
    f32 endframe;

    if (headanim < 0 || headanim > 1) {
        headanim = 0;
    }

    loopframe = g_BondMoveAnimationSetup[headanim].loopframe;
    endframe = g_BondMoveAnimationSetup[headanim].endframe;
    frame = g_CurrentPlayer != NULL ? g_CurrentPlayer->field_5C0 : loopframe;

    if (!bheadNativeFrameF32Usable(frame)
        || frame < loopframe
        || frame > endframe)
    {
        frame = model != NULL ? model->unk28 : loopframe;
    }

    frame = bheadNativeClampMoveAnimFrame(headanim, frame);

    if (g_CurrentPlayer != NULL) {
        g_CurrentPlayer->field_5C0 = frame;
    }

    return frame;
}

static void bheadNativeSyncPlayerGaitFrame(Model *model)
{
    s32 headanim;

    if (g_CurrentPlayer == NULL) {
        return;
    }

    headanim = bheadNativeNormalizeHeadAnimIndex();
    g_CurrentPlayer->field_5C0 = bheadNativeCurrentMoveAnimFrame(headanim, model);
}

static s32 bheadNativeResetPlayerGaitAnimation(const char *reason, Model *model)
{
    s32 headanim;
    f32 saved_speed;

    if (g_CurrentPlayer == NULL
        || model == NULL
        || ptr_animation_table == NULL)
    {
        return FALSE;
    }

    headanim = bheadNativeNormalizeHeadAnimIndex();
    saved_speed = model->speed;

    if (!bheadNativeFrameF32Usable(saved_speed)
        || saved_speed < -20.0f
        || saved_speed > 20.0f)
    {
        saved_speed = 0.0f;
    }

    model->anim2 = NULL;
    model->unk58 = 0.0f;
    model->unk5c = 0.0f;
    model->speed2 = 0.0f;
    model->unk74 = 0.0f;
    model->unk78 = 0.0f;
    model->unk7c = 0.0f;
    model->unk80 = 0.0f;
    model->unk84 = 0.0f;
    model->unk88 = 0.0f;
    model->unk8c = 0;
    model->newspeed = saved_speed;
    model->oldspeed = saved_speed;
    model->timespeed = 0.0f;
    model->elapsespeed = 0.0f;

    modelSetAnimation(
        model,
        ANIM_FROM_OFFSET(g_BondMoveAnimationSetup[headanim].anim_id),
        (s32)g_CurrentPlayer->animFlipFlag,
        g_BondMoveAnimationSetup[headanim].loopframe,
        0.5f,
        0.0f);

    modelSetAnimLooping(model, g_BondMoveAnimationSetup[headanim].loopframe, 0.0f);
    modelSetAnimEndFrame(model, g_BondMoveAnimationSetup[headanim].endframe);
    modelSetAnimFlipFunction(model, bheadFlipAnimation);
    modelSetAnimPlaySpeed(model, BHEAD_NATIVE_ANIMRATE, 0.0f);
    modelSetAnimSpeed(model, saved_speed, 0.0f);
    g_CurrentPlayer->headanim = headanim;
    g_CurrentPlayer->field_5C0 = g_BondMoveAnimationSetup[headanim].loopframe;

    if (bheadNativePlayerGaitFrameStateUsable(model)) {
        static int reset_log_budget = 20;

        if (reset_log_budget > 0) {
            reset_log_budget--;
            fprintf(stderr,
                    "[BHEAD_RESET_GAIT] reason=%s model=%p headanim=%d speed=%.3f\n",
                    reason != NULL ? reason : "-",
                    (void *)model,
                    headanim,
                    saved_speed);
            fflush(stderr);
        }
        return TRUE;
    }

    return FALSE;
}

static void bheadNativeMaybePoisonPlayerGaitFrame(void)
{
    static s32 checked = FALSE;
    static s32 poisoned = FALSE;
    static s32 enabled = FALSE;
    const char *env;
    Model *model;

    if (!checked) {
        env = getenv("GE007_DIAG_POISON_BHEAD_FRAME");
        enabled = (env != NULL && env[0] != '\0' && env[0] != '0') ? TRUE : FALSE;
        checked = TRUE;
    }

    if (!enabled || poisoned || g_CurrentPlayer == NULL || PLAYER_MODEL(g_CurrentPlayer) == NULL) {
        return;
    }

    model = PLAYER_MODEL(g_CurrentPlayer);

    if (!mempPtrIsInBank(model, MEMPOOL_STAGE, sizeof(*model))) {
        return;
    }

    model->unk28 = -2.0155136e28f;
    model->unk58 = -2.0155136e28f;
    poisoned = TRUE;

    fprintf(stderr,
            "[BHEAD_DIAG_POISON_GAIT] model=%p frame=%.3f frame2=%.3f\n",
            (void *)model,
            model->unk28,
            model->unk58);
    fflush(stderr);
}

static s32 bheadNativeEnsurePlayerGaitModel(const char *reason)
{
    Model *model;

    if (!bheadNativePlayerGaitModelUsable()) {
        if (g_CurrentPlayer != NULL && g_CurrentPlayer->model != NULL) {
            GEDBG("BHEAD_INVALID_MODEL reason=%s model=%p",
                  reason != NULL ? reason : "-",
                  (void *)g_CurrentPlayer->model);
            g_CurrentPlayer->model = NULL;
        }

        return FALSE;
    }

    model = PLAYER_MODEL(g_CurrentPlayer);
    bheadNativeClampCurrentHeadAnimIndex();

    if (bheadNativePlayerGaitFrameStateUsable(model)) {
        bheadNativeSyncPlayerGaitFrame(model);
        return TRUE;
    }

    if (bheadNativeResetPlayerGaitAnimation(reason, model)) {
        return TRUE;
    }

    if (g_CurrentPlayer != NULL) {
        GEDBG("BHEAD_INVALID_FRAME_STATE reason=%s model=%p frame=%.3f frame2=%.3f speed=%.3f playspeed=%.3f",
              reason != NULL ? reason : "-",
              (void *)model,
              model != NULL ? model->unk28 : 0.0f,
              model != NULL ? model->unk58 : 0.0f,
              model != NULL ? model->speed : 0.0f,
              model != NULL ? model->playspeed : 0.0f);
        g_CurrentPlayer->model = NULL;
    }

    return FALSE;
}

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

s32 pcBheadDeferRamromFirstBlockIdleRoll(void)
{
    if (!pcRamromShouldDeferFirstBlockRngConsumers()) {
        return FALSE;
    }

    s_pcRamromDeferredBheadIdleRollCount++;
    return TRUE;
}

void pcBheadRunDeferredRamromFirstBlockIdleRoll(void)
{
    while (s_pcRamromDeferredBheadIdleRollCount > 0) {
        s_pcRamromDeferredBheadIdleRollCount--;
        bheadUpdateIdleRoll();
    }
}
#endif





void bheadFlipAnimation()
{
    g_CurrentPlayer->animFlipFlag = !g_CurrentPlayer->animFlipFlag;
}

void bheadUpdateIdleRoll()
{
    f32 mult = 1.0f / UINT_MAX;

	g_CurrentPlayer->standlook[g_CurrentPlayer->standcnt].f[0] = ((f32)randomGetNext() * mult - 0.5f) * 0.02f;
	g_CurrentPlayer->standlook[g_CurrentPlayer->standcnt].f[2] = 1;
	g_CurrentPlayer->standup[g_CurrentPlayer->standcnt].f[0] = ((f32)randomGetNext() * mult - 0.5f) * 0.02f;
	g_CurrentPlayer->standup[g_CurrentPlayer->standcnt].f[1] = 1;

	if (g_CurrentPlayer->standcnt)
    {
		g_CurrentPlayer->standlook[g_CurrentPlayer->standcnt].f[1] = (f32)randomGetNext() * mult * 0.01f;
		g_CurrentPlayer->standup[g_CurrentPlayer->standcnt].f[2] = (f32)randomGetNext() * mult * -0.01f;
	}
    else
    {
		g_CurrentPlayer->standlook[g_CurrentPlayer->standcnt].f[1] = (f32)randomGetNext() * mult * -0.01f;
		g_CurrentPlayer->standup[g_CurrentPlayer->standcnt].f[2] = (f32)randomGetNext() * mult * 0.01f;
	}

	g_CurrentPlayer->standcnt = 1 - g_CurrentPlayer->standcnt;
}

void bheadUpdatePos(coord3d *vel)
{
#if defined(VERSION_EU)
#define CURRENTPLAYERUPDATEHEADPOS_SCALE 0.916599988937f
#else
#define CURRENTPLAYERUPDATEHEADPOS_SCALE 0.93f
#endif
    s32 i;

    if (g_CurrentPlayer->resetheadpos)
    {
        g_CurrentPlayer->headpossum.f[0] = 0.0f;
        g_CurrentPlayer->headpossum.f[1] = (vel->f[1] / (1.0f - CURRENTPLAYERUPDATEHEADPOS_SCALE));
        g_CurrentPlayer->headpossum.f[2] = 0.0f;

        g_CurrentPlayer->resetheadpos = FALSE;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->headpossum.f[0] = ((CURRENTPLAYERUPDATEHEADPOS_SCALE * g_CurrentPlayer->headpossum.f[0]) + vel->f[0]);
        g_CurrentPlayer->headpossum.f[1] = ((CURRENTPLAYERUPDATEHEADPOS_SCALE * g_CurrentPlayer->headpossum.f[1]) + vel->f[1]);
        g_CurrentPlayer->headpossum.f[2] = ((CURRENTPLAYERUPDATEHEADPOS_SCALE * g_CurrentPlayer->headpossum.f[2]) + vel->f[2]);
    }

    g_CurrentPlayer->headpos.f[0] = (g_CurrentPlayer->headpossum.f[0] * (1.0f - CURRENTPLAYERUPDATEHEADPOS_SCALE));
    g_CurrentPlayer->headpos.f[1] = (g_CurrentPlayer->headpossum.f[1] * (1.0f - CURRENTPLAYERUPDATEHEADPOS_SCALE));
    g_CurrentPlayer->headpos.f[2] = (g_CurrentPlayer->headpossum.f[2] * (1.0f - CURRENTPLAYERUPDATEHEADPOS_SCALE));
#undef CURRENTPLAYERUPDATEHEADPOS_SCALE
}

void bheadUpdateRot(coord3d *lookvel, coord3d *upvel)
{
	s32 i;

	if (g_CurrentPlayer->resetheadrot)
    {
		g_CurrentPlayer->headlooksum.f[0] = lookvel->f[0] / (1.0f - g_CurrentPlayer->headdamp);
		g_CurrentPlayer->headlooksum.f[1] = lookvel->f[1] / (1.0f - g_CurrentPlayer->headdamp);
		g_CurrentPlayer->headlooksum.f[2] = lookvel->f[2] / (1.0f - g_CurrentPlayer->headdamp);

        g_CurrentPlayer->headupsum.f[0] = upvel->f[0] / (1.0f - g_CurrentPlayer->headdamp);
		g_CurrentPlayer->headupsum.f[1] = upvel->f[1] / (1.0f - g_CurrentPlayer->headdamp);
		g_CurrentPlayer->headupsum.f[2] = upvel->f[2] / (1.0f - g_CurrentPlayer->headdamp);

		g_CurrentPlayer->resetheadrot = FALSE;
	}

	for (i = 0; i < g_ClockTimer; i++)
    {
		g_CurrentPlayer->headlooksum.f[0] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headlooksum.f[0] + lookvel->f[0];
		g_CurrentPlayer->headlooksum.f[1] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headlooksum.f[1] + lookvel->f[1];
		g_CurrentPlayer->headlooksum.f[2] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headlooksum.f[2] + lookvel->f[2];

        g_CurrentPlayer->headupsum.f[0] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headupsum.f[0] + upvel->f[0];
		g_CurrentPlayer->headupsum.f[1] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headupsum.f[1] + upvel->f[1];
		g_CurrentPlayer->headupsum.f[2] = g_CurrentPlayer->headdamp * g_CurrentPlayer->headupsum.f[2] + upvel->f[2];
	}

	g_CurrentPlayer->headlook.f[0] = g_CurrentPlayer->headlooksum.f[0] * (1.0f - g_CurrentPlayer->headdamp);
	g_CurrentPlayer->headlook.f[1] = g_CurrentPlayer->headlooksum.f[1] * (1.0f - g_CurrentPlayer->headdamp);
	g_CurrentPlayer->headlook.f[2] = g_CurrentPlayer->headlooksum.f[2] * (1.0f - g_CurrentPlayer->headdamp);

    g_CurrentPlayer->headup.f[0] = g_CurrentPlayer->headupsum.f[0] * (1.0f - g_CurrentPlayer->headdamp);
	g_CurrentPlayer->headup.f[1] = g_CurrentPlayer->headupsum.f[1] * (1.0f - g_CurrentPlayer->headdamp);
	g_CurrentPlayer->headup.f[2] = g_CurrentPlayer->headupsum.f[2] * (1.0f - g_CurrentPlayer->headdamp);
}

void bheadSetdamp(f32 headdamp)
{
	if (headdamp != g_CurrentPlayer->headdamp)
    {
		f32 divisor = 1.0f - headdamp;

        g_CurrentPlayer->headlooksum.f[0] = (g_CurrentPlayer->headlooksum.f[0] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;
		g_CurrentPlayer->headlooksum.f[1] = (g_CurrentPlayer->headlooksum.f[1] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;
		g_CurrentPlayer->headlooksum.f[2] = (g_CurrentPlayer->headlooksum.f[2] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;

        g_CurrentPlayer->headupsum.f[0] = (g_CurrentPlayer->headupsum.f[0] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;
		g_CurrentPlayer->headupsum.f[1] = (g_CurrentPlayer->headupsum.f[1] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;
		g_CurrentPlayer->headupsum.f[2] = (g_CurrentPlayer->headupsum.f[2] * (1.0f - g_CurrentPlayer->headdamp)) / divisor;

        g_CurrentPlayer->headdamp = headdamp;
	}
}


/**
 * Address 0x80036B00.
*/
coord3d initialHeadPosition = { 0.0f, 0.0f, 0.0f };

/**
 * Address 0x80036B0C.
*/
coord3d headLookDirection = { 0.0f, 0.0f, 1.0f };

/**
 * Address 0x80036B18.
*/
coord3d headUpDirection = { 0.0f, 1.0f, 0.0f };

/**
 * Address 0x80036B24.
*/
ModelRenderData headModelRenderData = {NULL,
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
                              {0, 0, 0, 0},
                              {0, 0, 0, 0},
                              CULLMODE_BOTH};
/**
 * Address 0x80036B64.
*/
coord3d D_80036B64 = { 0.0f, 0.0f, 0.0f };




/**
 * Updates the head movement based on the player's speed and animation state.
 */
void bheadUpdate(f32 percent_speed, f32 speedsideways)
{
    coord3d headpos;
    coord3d lookvel;
    coord3d upvel;
    f32 abs_anim_speed;
    ModelRenderData renderData;
    Mtxf sp40;
    coord3d offset;
    u32 isMergable;

    headpos = initialHeadPosition;
    lookvel = headLookDirection;
    upvel = headUpDirection;

#ifdef NATIVE_PORT
    bheadNativeMaybePoisonPlayerGaitFrame();

    /* Guard: player model may not be initialized if animInit was not called.
     * Without the model, we can't compute headpos from bone matrices.
     * Use the existing standheight (set by initBondDATAdefaults from the model
     * matrices) as the headpos Y component. If standheight was never set,
     * use a safe default. headpos.f[1] IS the camera height above field_70. */
    if (!bheadNativeEnsurePlayerGaitModel("update")) {
        /* standheight is already set correctly by initBondDATAdefaults if
         * the model matrices were computed. Use it directly. */
        if (bheadNativeFiniteF32(g_CurrentPlayer->standheight)
            && g_CurrentPlayer->standheight > 1.0f) {
            headpos.f[1] = g_CurrentPlayer->standheight;
        } else {
            headpos.f[1] = 162.0f; /* fallback if never initialized */
        }
        bheadUpdatePos(&headpos);
        bheadUpdateIdleRoll();
        return;
    }
#endif
    abs_anim_speed = modelGetAbsAnimSpeed(PLAYER_MODEL(g_CurrentPlayer));

    if (g_CurrentPlayer->headanim == 0)
    {
        if (abs_anim_speed > 0.7f)
        {
            g_CurrentPlayer->headamplitude = 1.0f;
        }
        else if (abs_anim_speed > 0.1f)
        {
            g_CurrentPlayer->headamplitude = (((abs_anim_speed - 0.1f) * 0.6f) / 0.59999996f) + 0.4f;
        }
        else
        {
            g_CurrentPlayer->headamplitude = 0.4f;
        }

        g_CurrentPlayer->sideamplitude = g_CurrentPlayer->headamplitude;
    }
    else if (g_CurrentPlayer->headanim == 1)
    {
        g_CurrentPlayer->headamplitude = 0.9f;
        g_CurrentPlayer->sideamplitude = 0.5f;
    }
    else
    {
        g_CurrentPlayer->headamplitude = 1.0f;
        g_CurrentPlayer->sideamplitude = g_CurrentPlayer->headamplitude;
    }

    renderData = headModelRenderData;
    offset = D_80036B64;

    isMergable = modelIsAnimMergingEnabled();

    g_CurrentPlayer->resetheadtick = FALSE;

    modelSetAnimMergingEnabled(0);
    modelTickAnimQuarterSpeed(PLAYER_MODEL(g_CurrentPlayer), g_ClockTimer, 1);
#ifdef NATIVE_PORT
    bheadNativeSyncPlayerGaitFrame(PLAYER_MODEL(g_CurrentPlayer));
#endif
    modelSetAnimMergingEnabled((s32) isMergable);

    subcalcpos(PLAYER_MODEL(g_CurrentPlayer));
    matrix_4x4_set_identity(&sp40);

    renderData.unk_matrix = &sp40;
    renderData.mtxlist = &g_CurrentPlayer->bondheadmatrices[0];

    subcalcmatrices(&renderData, PLAYER_MODEL(g_CurrentPlayer));

#ifdef NATIVE_PORT
    /* Player head matrices are generated in model units under an identity
     * parent. The N64 camera/height system uses these model-unit translations
     * directly (standheight ~160).  Do NOT scale by model->scale — that
     * would divide by 10x, producing standheight ~16 and sinking the camera
     * into the floor. */
    {
        static int bhead_log = 0;
        if (bhead_log < 3) {
            GEDBG("BHEADM[0] trans=(%.3f, %.3f, %.3f) standheight=%.3f",
                  g_CurrentPlayer->bondheadmatrices[0].m[3][0],
                  g_CurrentPlayer->bondheadmatrices[0].m[3][1],
                  g_CurrentPlayer->bondheadmatrices[0].m[3][2],
                  g_CurrentPlayer->standheight);
            bhead_log++;
        }
    }
#endif

    g_CurrentPlayer->headbodyoffset.f[0] = g_CurrentPlayer->standbodyoffset.x;
    g_CurrentPlayer->headbodyoffset.f[1] = g_CurrentPlayer->standbodyoffset.y;
    g_CurrentPlayer->headbodyoffset.f[2] = g_CurrentPlayer->standbodyoffset.z;

    getsuboffset(PLAYER_MODEL(g_CurrentPlayer), (coord3d *) &offset);

    offset.f[0] -= g_CurrentPlayer->bondheadmatrices[0].m[3][0];
    offset.f[2] -= g_CurrentPlayer->bondheadmatrices[0].m[3][2];

    setsuboffset(PLAYER_MODEL(g_CurrentPlayer), (coord3d *) &offset);

    if (abs_anim_speed > 0.0f)
    {
        g_CurrentPlayer->bondheadmatrices[0].m[3][0] += speedsideways;
        g_CurrentPlayer->bondheadmatrices[0].m[3][2] *= percent_speed;

        if (g_ClockTimer > 0)
        {
            g_CurrentPlayer->bondheadmatrices[0].m[3][0] /= g_GlobalTimerDelta;
            g_CurrentPlayer->bondheadmatrices[0].m[3][2] /= g_GlobalTimerDelta;
        }

#ifdef NATIVE_PORT
        /* The translation rows above have already been rescaled into game
         * units. Do not apply model->scale a second time here.
         *
         * On N64, subcalcpos's move callback (sub_GAME_7F01FC10) constrains
         * the root bone Y to the floor, so bondheadmatrices[0].m[3][1] stays
         * near standheight with only subtle walk bob. On NATIVE_PORT, the
         * move callback doesn't fully constrain Y, allowing animation root
         * displacement (~14 units on Cradle slopes) to leak through and cause
         * violent camera bobbing. Clamp the Y deviation to a reasonable walk
         * bob range (N64 walk bob is ~2-3 units). */
        {
            f32 bone_y = g_CurrentPlayer->bondheadmatrices[0].m[3][1];
            f32 dev = bone_y - g_CurrentPlayer->standheight;
            f32 max_bob = 4.0f;
            if (dev > max_bob) dev = max_bob;
            if (dev < -max_bob) dev = -max_bob;
            headpos.f[0] = g_CurrentPlayer->bondheadmatrices[0].m[3][0] * g_CurrentPlayer->headamplitude;
            headpos.f[1] = (dev * g_CurrentPlayer->headamplitude) + g_CurrentPlayer->standheight;
            headpos.f[2] = g_CurrentPlayer->bondheadmatrices[0].m[3][2] * g_CurrentPlayer->headamplitude;
        }
#else
        headpos.f[0] =   g_CurrentPlayer->bondheadmatrices[0].m[3][0] * g_CurrentPlayer->headamplitude;
        headpos.f[1] = ((g_CurrentPlayer->bondheadmatrices[0].m[3][1] - g_CurrentPlayer->standheight) * g_CurrentPlayer->headamplitude) + g_CurrentPlayer->standheight;
        headpos.f[2] =   g_CurrentPlayer->bondheadmatrices[0].m[3][2] * g_CurrentPlayer->headamplitude;
#endif

        if (g_CurrentPlayer->headanim >= 0)
        {
            lookvel.f[0] = g_CurrentPlayer->bondheadmatrices[0].m[2][0] * g_CurrentPlayer->sideamplitude;
            lookvel.f[1] = g_CurrentPlayer->bondheadmatrices[0].m[2][1] * g_CurrentPlayer->headamplitude;
            lookvel.f[2] = ((g_CurrentPlayer->bondheadmatrices[0].m[2][2] - 1.0f) * g_CurrentPlayer->headamplitude) + 1.0f;

            upvel.f[0] = g_CurrentPlayer->bondheadmatrices[0].m[1][0] * g_CurrentPlayer->headamplitude;
            upvel.f[1] = ((g_CurrentPlayer->bondheadmatrices[0].m[1][1] - 1.0f) * g_CurrentPlayer->headamplitude) + 1.0f;
            upvel.f[2] = g_CurrentPlayer->bondheadmatrices[0].m[1][2] * g_CurrentPlayer->headamplitude;

            g_CurrentPlayer->headwalkingtime60 += g_ClockTimer;

#if defined(VERSION_EU)
            if (g_CurrentPlayer->headwalkingtime60 > TICKS_PER_SECOND)
            {
                bheadSetdamp(0.916599988937f);
            }
            else
            {
                bheadSetdamp(0.987999975681f);
            }
#else
            if (g_CurrentPlayer->headwalkingtime60 > TICKS_PER_SECOND)
            {
                bheadSetdamp(0.93f);
            }
            else
            {
                bheadSetdamp(0.99f);
            }
#endif
        }
        else
        {
            lookvel.f[0] = g_CurrentPlayer->bondheadmatrices[0].m[2][0];
            lookvel.f[1] = g_CurrentPlayer->bondheadmatrices[0].m[2][1];
            lookvel.f[2] = g_CurrentPlayer->bondheadmatrices[0].m[2][2];

            upvel.f[0] = g_CurrentPlayer->bondheadmatrices[0].m[1][0];
            upvel.f[1] = g_CurrentPlayer->bondheadmatrices[0].m[1][1];
            upvel.f[2] = g_CurrentPlayer->bondheadmatrices[0].m[1][2];

            bheadSetdamp(0.85f);
        }
    }
    else
    {
        g_CurrentPlayer->headbodyoffset.f[0] = g_CurrentPlayer->standbodyoffset.x;
        g_CurrentPlayer->headbodyoffset.f[1] = g_CurrentPlayer->standbodyoffset.y;
        g_CurrentPlayer->headbodyoffset.f[2] = g_CurrentPlayer->standbodyoffset.z;

        headpos.f[0] = 0.0f;
        headpos.f[1] = g_CurrentPlayer->standheight;
        headpos.f[2] = 0.0f;

        g_CurrentPlayer->headwalkingtime60 = 0;
#if defined(VERSION_EU)
        bheadSetdamp(0.987999975681f);
#else
        bheadSetdamp(0.99f);
#endif
        g_CurrentPlayer->standfrac += (0.008333334f + (0.025000002f * bondviewGetBondBreathing())) * g_GlobalTimerDelta;

        if (g_CurrentPlayer->standfrac >= 1.0f)
        {
            bheadUpdateIdleRoll();
            g_CurrentPlayer->standfrac -= 1.0f;
        }

        // result = x vector plus ((y - x vector) * scaler)
        // lookvel = ...
        sub_GAME_7F05AE00(
            (vec3d *)&g_CurrentPlayer->standlook[g_CurrentPlayer->standcnt],
            (vec3d *)&g_CurrentPlayer->standlook[1 - g_CurrentPlayer->standcnt],
            g_CurrentPlayer->standfrac,
            &lookvel);

        lookvel.f[0] *= (1.0f + (5.0f * bondviewGetBondBreathing()));
        lookvel.f[1] *= (1.0f + (5.0f * bondviewGetBondBreathing()));

        // result = x vector plus ((y - x vector) * scaler)
        // upvel = ...
        sub_GAME_7F05AE00(
            (vec3d *)&g_CurrentPlayer->standup[g_CurrentPlayer->standcnt],
            (vec3d *)&g_CurrentPlayer->standup[1 - g_CurrentPlayer->standcnt],
            g_CurrentPlayer->standfrac,
            &upvel);

        upvel.f[0] *= (1.0f + (5.0f * bondviewGetBondBreathing()));
        upvel.f[2] *= (1.0f + (5.0f * bondviewGetBondBreathing()));
    }

    bheadUpdatePos(&headpos);
    bheadUpdateRot(&lookvel, &upvel);
}



/**
 * Adjusts Bond's head animation based on movement speed.
 * 
 * @param speed The movement speed to adjust the animation to.
 */
void bheadAdjustAnimation(f32 speed)
{
    s32 i;
    f32 startframe;

#ifdef NATIVE_PORT
    bheadNativeMaybePoisonPlayerGaitFrame();

    if (!bheadNativeEnsurePlayerGaitModel("adjust")) {
        return;
    }
#endif

    speed *= g_BondMoveAnimationSetup[1].speedMultiplier;

    for (i=0; i<2; i++)
    {
        if (speed <= g_BondMoveAnimationSetup[i].unk14 * g_BondMoveAnimationSetup[i].speedMultiplier)
        {
            if (i != g_CurrentPlayer->headanim)
            {
                startframe = 0.0f;

                if (g_CurrentPlayer->headanim >= 0)
                {
#ifdef NATIVE_PORT
                    f32 currentframe = bheadNativeCurrentMoveAnimFrame(
                        g_CurrentPlayer->headanim,
                        PLAYER_MODEL(g_CurrentPlayer));

                    startframe = (currentframe - g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe)
                        / (g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].endframe - g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe);
#else
                    startframe = (g_CurrentPlayer->field_5C0 - g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe)
                        / (g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].endframe - g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe);
#endif

                    startframe = g_BondMoveAnimationSetup[i].loopframe + ((g_BondMoveAnimationSetup[i].endframe - g_BondMoveAnimationSetup[i].loopframe) * startframe);
#ifdef NATIVE_PORT
                    startframe = bheadNativeClampMoveAnimFrame(i, startframe);
#endif
                }

                modelSetAnimation(
                    PLAYER_MODEL(g_CurrentPlayer),
#ifdef NATIVE_PORT
                    ANIM_FROM_OFFSET(g_BondMoveAnimationSetup[i].anim_id),
#else
                    // match hack: addu address backwards
                    (struct ModelAnimation *) ((s32)g_BondMoveAnimationSetup[i].anim_id + (s32)&ptr_animation_table->data),
#endif
                    (s32) g_CurrentPlayer->animFlipFlag,
                    startframe,
                    0.5f,
                    12.0f);

                modelSetAnimLooping(PLAYER_MODEL(g_CurrentPlayer), g_BondMoveAnimationSetup[i].loopframe, 0.0f);
                modelSetAnimEndFrame(PLAYER_MODEL(g_CurrentPlayer), g_BondMoveAnimationSetup[i].endframe);
                modelSetAnimFlipFunction(PLAYER_MODEL(g_CurrentPlayer), bheadFlipAnimation);
                g_CurrentPlayer->headanim = i;
#ifdef NATIVE_PORT
                g_CurrentPlayer->field_5C0 = startframe;
#endif
            }

            speed /= g_BondMoveAnimationSetup[i].speedMultiplier;

            modelSetAnimSpeed(PLAYER_MODEL(g_CurrentPlayer), speed * 0.5f, 0.0f);
            return;
        }
    }
}


/**
 * Starts a new death animation for Bond's head.
 * 
 * @param animNum The animation to play.
 * @param flip Whether to flip the animation.
 * @param startFrame The starting frame of the animation.
 * @param speed The speed of the animation.
 */
void bheadStartDeathAnimation(struct ModelAnimation *animnum, s32 flip, f32 fstarttime, f32 speed)
{
#ifdef NATIVE_PORT
    if (!bheadNativeEnsurePlayerGaitModel("death")) {
        return;
    }
#endif
    modelSetAnimation(PLAYER_MODEL(g_CurrentPlayer), animnum, flip, fstarttime, speed * 0.5f, 12.0f);
    g_CurrentPlayer->headanim = -1;
}


/**
 * Sets the speed of the current head animation.
 * 
 * @param speed The speed to set for the head animation.
 */
void bheadSetSpeed(f32 speed)
{
#ifdef NATIVE_PORT
    if (!bheadNativeEnsurePlayerGaitModel("speed")) {
        return;
    }
#endif
    modelSetAnimSpeed(PLAYER_MODEL(g_CurrentPlayer), speed * 0.5f, 0.0f);
}


/**
 * Calculates the breathing value for Bond's head animation.
 * 
 * @return The calculated breathing value.
 */
f32 bheadGetBreathingValue(void)
{
	if (g_CurrentPlayer->headanim >= 0) {
        // bondviewGetBondBreathing() * (1/80) + (1/240)
		f32 baseBreathing = bondviewGetBondBreathing() * 0.012500001f + 0.004166667f;
#ifdef NATIVE_PORT
		if (!bheadNativeEnsurePlayerGaitModel("breathing")) return baseBreathing;
#endif
		f32 animSpeed = modelGetAbsAnimSpeed(PLAYER_MODEL(g_CurrentPlayer));

		if (animSpeed > 0) {
			f32 calculatedBreathing = animSpeed / (g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].endframe - g_BondMoveAnimationSetup[g_CurrentPlayer->headanim].loopframe);

			if (calculatedBreathing < baseBreathing) {
				calculatedBreathing = baseBreathing;
			}

			return calculatedBreathing;
		}

		return baseBreathing;
	}

	return 0;
}
