#include <ultra64.h>
#include <memp.h>
#include <bondconstants.h>
#include <boss.h>
#include "math_atan2f.h"
#include "bondview_r.h"
#include "bondview.h"
#include "platform/port_env.h"
#include "random.h"
#include "game/bondinv.h"
#include "game/chr.h"
#include "game/chrai.h"
#include "game/front.h"
#include "game/gun.h"
#include "game/lvl_text.h"
#include "game/player.h"
#include "game/player_2.h"
#include "game/ramromreplay.h"
#include "game/stan.h"
#include "game/loadobjectmodel.h"
#include "game/objecthandler.h"


/**
 * Address 0x8002A780.
*/
struct coord3d default_start_position = { 0 };

u32 weaponLoadProjectileModels(ITEM_IDS modelid)
{
    s32 model;
  
    model = -1;
    switch(modelid)
    {
    case ITEM_THROWKNIFE:
        model = PROJECTILES_TYPE_KNIFE;
        break;
    case ITEM_GRENADELAUNCH:
        model = PROJECTILES_TYPE_GLAUNCH_ROUND;
        break;
    case ITEM_ROCKETLAUNCH:
        model = PROJECTILES_TYPE_ROCKET_ROUND;
        break;
    case ITEM_GRENADE:
        model = PROJECTILES_TYPE_GRENADE;
        break;
    case ITEM_TIMEDMINE:
        model = PROJECTILES_TYPE_TIMED_MINE;
        break;
    case ITEM_PROXIMITYMINE:
        model = PROJECTILES_TYPE_PROX_MINE;
        break;
    case ITEM_REMOTEMINE:
        model = PROJECTILES_TYPE_REMOTE_MINE;
        break;
    case ITEM_TANKSHELLS:
        model = PROJECTILES_TYPE_ROCKET_ROUND2;
        break;
    case ITEM_BOMBCASE:
        model = PROJECTILES_TYPE_BOMBCASE;
        break;
    case ITEM_PLASTIQUE:
        model = PROJECTILES_TYPE_PLASTIQUE;
        break;
    case ITEM_BUG:
        model = PROJECTILES_TYPE_BUG;
        break;
    case ITEM_MICROCAMERA:
        model = PROJECTILES_TYPE_MICROCAMERA;
    }

    if (-1 < model)
    {
        /* modelConvertN64Binary handles 64-bit PROMOTE for all model loads */
        return modelLoad(model);
    }
    return 0;
}

#define FLOAT_INIT 0

#if defined(VERSION_EU)
#define FIELD_6C_FACTOR 0.20039999485f
#define FIELD_3B8_FACTOR 0.118799984455f
#else
#define FIELD_6C_FACTOR 0.170000016689f
#define FIELD_3B8_FACTOR 0.100000023842f
#endif

#ifdef NATIVE_PORT
extern int g_deterministic;

static f32 portAdjustPadLookAngle(f32 raw_angle) {
    static int use_pi_offset = -1;
    if (use_pi_offset < 0) {
        /* Default to authored pad look vectors. The old unconditional +pi
         * workaround fixes some starts but flips others 180 degrees into
         * nearby walls; keep it as an explicit escape hatch instead. */
        use_pi_offset = (getenv("GE007_PAD_LOOK_PI") != NULL);
    }

    if (use_pi_offset) {
        raw_angle += M_PI;
        if (raw_angle >= M_TAU_F) raw_angle -= M_TAU_F;
    }

    return raw_angle;
}

static void portPrimeSpawnHeightState(void) {
    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) return;
    if (g_CurrentPlayer->field_29BC > 1.0f) return;

    /* bondviewLoadSetupIntroSection runs before bondviewPlayerSpawnRelated()
     * on the PC level-load path, so field_29BC is still its default sentinel.
     * Prime it from the already-initialized model height to avoid spawning a
     * frame at floor_y + 1.0 and snapping upward on the next update. */
    if (g_CurrentPlayer->standheight > 1.0f) {
        g_CurrentPlayer->field_29BC =
            (g_CurrentPlayer->standheight * g_playerPerm->player_perspective_height) + 7.0f;
    } else {
        g_CurrentPlayer->field_29BC =
            (g_playerPerm->player_perspective_height * 185.0f) - 10.0f;
    }
}

static s32 portIntroEnabled(void) {
    extern int g_pcDirectBootLevelActive;
    const char *enable_env = getenv("GE007_ENABLE_LEVEL_INTRO");
    const char *disable_env = getenv("GE007_DISABLE_LEVEL_INTRO");

    if (disable_env != NULL && disable_env[0] != '\0' &&
        !(disable_env[0] == '0' && disable_env[1] == '\0')) {
        return 0;
    }

    if (enable_env != NULL && enable_env[0] != '\0' &&
        !(enable_env[0] == '0' && enable_env[1] == '\0')) {
        return 1;
    }

    /* §4.4 item 2: restore the determinism check. Without it, `--ramrom
     * --deterministic` (g_pcDirectBootLevelActive == 0, unlike --level's
     * direct-boot path) plays the full authored intro, whose any-stick skip
     * (bondviewNativeIntroSkipRequested) is fed real replayed input every
     * frame -- a desync risk now closed there, but the intro's own camera
     * timers are additional non-essential state a deterministic/replay run
     * doesn't need. Default deterministic to the short FP handoff, same as a
     * direct level boot; GE007_ENABLE_LEVEL_INTRO=1 still forces it on above. */
    if (g_deterministic) {
        return 0;
    }

    /* Keep direct/dev boots on the short FP handoff, but normal gameplay should
     * follow the authored level intro path. */
    return g_pcDirectBootLevelActive ? 0 : 1;
}

static s32 portChooseIntroCameraIndex(s32 camera_count) {
    const char *forced_env;

    if (camera_count <= 0) {
        return 0;
    }

    forced_env = getenv("GE007_INTRO_CAMERA_INDEX");
    if (forced_env != NULL && forced_env[0] != '\0') {
        char *end = NULL;
        long parsed = strtol(forced_env, &end, 10);
        if (end != forced_env && *end == '\0') {
            if (parsed < 0) {
                parsed = 0;
            } else if (parsed >= camera_count) {
                parsed = camera_count - 1;
            }
            return (s32)parsed;
        }

        fprintf(stderr,
                "[INTRO] Ignoring invalid GE007_INTRO_CAMERA_INDEX='%s'\n",
                forced_env);
    }

    /* §5 D28: stock (non-NATIVE_PORT, bondview_r.c:603-610) always rolls
     * randomGetNext() % camera_count here -- the RNG seed itself is what's
     * pinned deterministic (boss.c reseeds to a fixed value), not this call.
     * Special-casing determinism to index 0 was a port-only mechanism
     * divergence: it made every deterministic run show a fixed establishing
     * camera instead of the stock-shaped seeded pick.
     *
     * BUT only draw when the authored intro will actually play. On the port,
     * direct/deterministic boots take the short FP handoff (portIntroEnabled()
     * false), and the selected camera is then unused -- drawing here would
     * consume an RNG value that shifts the deterministic stream for the rest of
     * the level load, perturbing the gameplay sim baseline the deterministic
     * regression tests rely on. Stock always plays the intro on a menu start,
     * so rolling only when portIntroEnabled() matches stock exactly where an
     * intro is shown, while keeping direct-boot sim state byte-identical to the
     * pre-D28 behavior. GE007_INTRO_CAMERA_INDEX above still overrides for
     * tooling that pins a camera (e.g. the oracle route, which forces the
     * intro on). */
    if (!portIntroEnabled()) {
        return 0;
    }
    return (s32)(randomGetNext() % (u32)camera_count);
}

static void portSyncSpawnViewBasis(void) {
    if (g_CurrentPlayer == NULL) return;

    /* change_player_pos_to_target seeds applied_view/applied_view2 with a
     * generic +X/+Y basis. On the skip-to-gameplay path we overwrite
     * vv_theta/theta_transform from the authored start pad, so the first
     * rendered FP frame must also refresh the derived camera basis.
     *
     * Do not call bondviewUpdatePlayerCollisionPositionFields() here: early
     * load frames still have headpos=0, which clamps height to 30 and drags
     * the camera down to floor_y+30 before the normal head update runs.
     *
     * Seed headlook/headup and first-person weapon state in the same order as
     * the N64 path; seen-shot reactions are sensitive to the resulting bullet
     * ray. Do not force animation selection here: during clean-save frontend ->
     * gameplay handoff the animation table can be visible before the player
     * model state is safe for bheadAdjustAnimation. */
    bondviewApplyVertaTheta();
    bondviewPrimeSpawnHeadState();
}

static void portApplySpawnStateFromPad(PadRecord *pad);
static void portApplySpawnStateFromPosition(const struct coord3d *pos, StandTile *stan, f32 look_angle, f32 fallback_floor_y);

void portApplyGameplaySpawnFromIntro(void) {
    PadRecord *gameplay_pad = NULL;
    const char *source = NULL;

    if (getPlayerCount() != 1 || g_CurrentPlayer == NULL) {
        return;
    }

    if (startpadcount > 0) {
        gameplay_pad = g_Startpad[0];
        source = "authored spawn record";
    }

    if (!gameplay_pad) {
        return;
    }

    portApplySpawnStateFromPad(gameplay_pad);

    /* The authored gameplay pad is the source of truth for the intro-skip
     * handoff. Do not apply stage-specific yaw tweaks here: the gameplay
     * camera, collision basis, and first-person weapon state must all derive
     * from the same facing data to stay coherent on frame 0. */

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr, "[INTRO] Applied gameplay spawn from %s\n", source);
    }
}

int portWarpBondToPad(s32 padnum) {
    PadRecord *pad;

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return 0;
    }

    if (padnum < 0) {
        return 0;
    }

    if (isNotBoundPad(padnum)) {
        pad = &g_CurrentSetup.pads[padnum];
    } else {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
    }

    if (pad == NULL || pad->stan == NULL) {
        return 0;
    }

    portApplySpawnStateFromPad(pad);

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr,
                "[WARP] pad=%d pos=(%.1f,%.1f,%.1f) room=%d\n",
                padnum, pad->pos.x, pad->pos.y, pad->pos.z,
                pad->stan ? pad->stan->room : -1);
    }

    return 1;
}

static void portApplyCurrentPlayerVerticalOffset(f32 y_offset) {
    if (y_offset == 0.0f || g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    g_CurrentPlayer->prop->pos.y += y_offset;
    g_CurrentPlayer->bondprevpos.y += y_offset;
    g_CurrentPlayer->field_488.collision_position.y += y_offset;
    g_CurrentPlayer->field_488.pos3.y += y_offset;
    g_CurrentPlayer->field_488.pos.y += y_offset;
    g_CurrentPlayer->field_488.applied_view.y += y_offset;
    g_CurrentPlayer->field_488.applied_view2.y += y_offset;
    g_CurrentPlayer->field_3B8.f[1] = g_CurrentPlayer->field_488.pos.f[1] / FIELD_3B8_FACTOR;
}

int portWarpBondToPadOffset(s32 padnum, f32 right_offset, f32 forward_offset) {
    return portWarpBondToPadOffsetY(padnum, right_offset, forward_offset, 0.0f);
}

int portWarpBondToPadOffsetY(s32 padnum, f32 right_offset, f32 forward_offset, f32 y_offset) {
    PadRecord *pad;
    struct coord3d spawn_pos;
    f32 forward_x;
    f32 forward_z;
    f32 right_x;
    f32 right_z;
    f32 look_len;
    f32 raw_look_angle;
    f32 look_angle;

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return 0;
    }

    if (padnum < 0) {
        return 0;
    }

    if (isNotBoundPad(padnum)) {
        pad = &g_CurrentSetup.pads[padnum];
    } else {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
    }

    if (pad == NULL || pad->stan == NULL) {
        return 0;
    }

    forward_x = pad->look.f[0];
    forward_z = pad->look.f[2];
    look_len = sqrtf(forward_x * forward_x + forward_z * forward_z);

    if (look_len < 0.000001f) {
        forward_x = 0.0f;
        forward_z = 1.0f;
    } else {
        forward_x /= look_len;
        forward_z /= look_len;
    }

    right_x = forward_z;
    right_z = -forward_x;

    spawn_pos.x = pad->pos.x + right_x * right_offset + forward_x * forward_offset;
    spawn_pos.y = pad->pos.y;
    spawn_pos.z = pad->pos.z + right_z * right_offset + forward_z * forward_offset;

    raw_look_angle = M_TAU_F - atan2f(forward_x, forward_z);
    look_angle = portAdjustPadLookAngle(raw_look_angle);

    portApplySpawnStateFromPosition(&spawn_pos, pad->stan, look_angle, pad->pos.y);
    portApplyCurrentPlayerVerticalOffset(y_offset);

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr,
                "[WARP_PAD_OFFSET] pad=%d right=%.1f forward=%.1f y=%.1f pos=(%.1f,%.1f,%.1f) spawn=(%.1f,%.1f,%.1f) room=%d\n",
                padnum, right_offset, forward_offset, y_offset,
                pad->pos.x, pad->pos.y, pad->pos.z,
                spawn_pos.x, spawn_pos.y, spawn_pos.z,
                pad->stan ? pad->stan->room : -1);
    }

    return 1;
}

int portWarpBondNearChr(s32 chrnum, f32 distance) {
    ChrRecord *chr;
    struct coord3d spawn_pos;
    struct coord3d guard_pos;
    struct coord3d offset;
    f32 offset_len;
    f32 look_angle;

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return 0;
    }

    chr = chrFindByLiteralId(chrnum);

    if (chr == NULL || chr->prop == NULL || chr->prop->stan == NULL) {
        return 0;
    }

    if (distance < 16.0f) {
        distance = 16.0f;
    }

    guard_pos = chr->prop->pos;
    offset.x = g_CurrentPlayer->prop->pos.x - guard_pos.x;
    offset.y = 0.0f;
    offset.z = g_CurrentPlayer->prop->pos.z - guard_pos.z;
    offset_len = sqrtf((offset.x * offset.x) + (offset.z * offset.z));

    if (offset_len < 1.0f) {
        offset.x = 0.0f;
        offset.z = -1.0f;
        offset_len = 1.0f;
    }

    spawn_pos.x = guard_pos.x + (offset.x / offset_len) * distance;
    spawn_pos.y = guard_pos.y;
    spawn_pos.z = guard_pos.z + (offset.z / offset_len) * distance;
    look_angle = portAdjustPadLookAngle(M_TAU_F - atan2f(guard_pos.x - spawn_pos.x,
                                                         guard_pos.z - spawn_pos.z));

    portApplySpawnStateFromPosition(&spawn_pos, chr->prop->stan, look_angle, guard_pos.y);

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr,
                "[WARP_CHR] chr=%d dist=%.1f guard=(%.1f,%.1f,%.1f) spawn=(%.1f,%.1f,%.1f) room=%d\n",
                chrnum, distance,
                guard_pos.x, guard_pos.y, guard_pos.z,
                spawn_pos.x, spawn_pos.y, spawn_pos.z,
                chr->prop->stan ? chr->prop->stan->room : -1);
    }

    return 1;
}

int portWarpBondNearChrAtAngle(s32 chrnum, f32 distance, f32 angle_deg) {
    ChrRecord *chr;
    struct coord3d spawn_pos;
    struct coord3d guard_pos;
    f32 angle_rad;
    f32 look_angle;

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return 0;
    }

    chr = chrFindByLiteralId(chrnum);

    if (chr == NULL || chr->prop == NULL || chr->prop->stan == NULL) {
        return 0;
    }

    if (distance < 16.0f) {
        distance = 16.0f;
    }

    guard_pos = chr->prop->pos;
    angle_rad = angle_deg * (M_TAU_F / 360.0f);

    spawn_pos.x = guard_pos.x + sinf(angle_rad) * distance;
    spawn_pos.y = guard_pos.y;
    spawn_pos.z = guard_pos.z + cosf(angle_rad) * distance;
    look_angle = portAdjustPadLookAngle(M_TAU_F - atan2f(guard_pos.x - spawn_pos.x,
                                                         guard_pos.z - spawn_pos.z));

    portApplySpawnStateFromPosition(&spawn_pos, chr->prop->stan, look_angle, guard_pos.y);

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr,
                "[WARP_CHR_ANGLE] chr=%d dist=%.1f angle=%.1f guard=(%.1f,%.1f,%.1f) spawn=(%.1f,%.1f,%.1f) room=%d\n",
                chrnum, distance, angle_deg,
                guard_pos.x, guard_pos.y, guard_pos.z,
                spawn_pos.x, spawn_pos.y, spawn_pos.z,
                chr->prop->stan ? chr->prop->stan->room : -1);
    }

    return 1;
}

/**
 * Apply canonical spawn state from a pad record after intro is skipped.
 * Mirrors the normal init flow at lines 527-580: computes floor height,
 * calls change_player_pos_to_target for collision/portal/view state,
 * then updates facing from the pad's look vector.
 */
static void portApplySpawnStateFromPad(PadRecord *pad) {
    struct coord3d spawn_pos;
    f32 floor_y;
    f32 raw_look_angle;
    f32 look_angle;

    if (!pad || !g_CurrentPlayer || !g_CurrentPlayer->prop) return;

    portPrimeSpawnHeightState();

    floor_y = bondviewYPositionRelated(pad->stan, pad->pos.x, pad->pos.z);
    if (floor_y == 0.0f) {
        floor_y = pad->pos.y;
    }

    /* Build position with height offset — matches line 528 canonical init */
    spawn_pos.x = pad->pos.x;
    spawn_pos.y = floor_y + g_CurrentPlayer->field_29BC;
    spawn_pos.z = pad->pos.z;

    /* Derive facing angle from pad look vector — matches line 517 */
    raw_look_angle = M_TAU_F - atan2f(pad->look.f[0], pad->look.f[2]);
    look_angle = portAdjustPadLookAngle(raw_look_angle);

    portApplySpawnStateFromPosition(&spawn_pos, pad->stan, look_angle, floor_y);

    if (getenv("GE007_VERBOSE")) {
        fprintf(stderr,
                "[SPAWN_APPLY] pad_pos=(%.1f,%.1f,%.1f) look=(%.2f,%.2f,%.2f) "
                "floor_y=%.1f raw_angle=%.1f angle=%.1f stan=%p room=%d\n",
                pad->pos.x, pad->pos.y, pad->pos.z,
                pad->look.f[0], pad->look.f[1], pad->look.f[2],
                floor_y, raw_look_angle, look_angle, (void*)pad->stan,
                pad->stan ? pad->stan->room : -1);
    }
}

static void portApplySpawnStateFromPosition(const struct coord3d *pos, StandTile *stan, f32 look_angle, f32 fallback_floor_y) {
    struct coord3d spawn_pos;
    f32 floor_y;

    if (pos == NULL || stan == NULL || g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    portPrimeSpawnHeightState();

    floor_y = bondviewYPositionRelated(stan, pos->x, pos->z);
    if (floor_y == 0.0f) {
        floor_y = fallback_floor_y;
    }

    /* Set floor/height state — matches lines 529, 533-534 */
    g_CurrentPlayer->field_70 = floor_y;
    g_CurrentPlayer->stanHeight = floor_y;
    g_CurrentPlayer->field_6C = floor_y / FIELD_6C_FACTOR;
    g_CurrentPlayer->field_7C = 0.0f;
    g_CurrentPlayer->vertical_bounce_adjust = 0.0f;
    g_CurrentPlayer->field_84 = 0.0f;
    g_CurrentPlayer->field_88 = 0.0f;
    g_CurrentPlayer->speedforwards = 0.0f;
    g_CurrentPlayer->speedsideways = 0.0f;
    g_CurrentPlayer->speedtheta = 0.0f;
    g_CurrentPlayer->speedverta = 0.0f;
    g_CurrentPlayer->bondshotspeed.x = 0.0f;
    g_CurrentPlayer->bondshotspeed.y = 0.0f;
    g_CurrentPlayer->bondshotspeed.z = 0.0f;
    g_CurrentPlayer->headpos.x = 0.0f;
    g_CurrentPlayer->headpos.y =
        (g_CurrentPlayer->standheight > 1.0f) ? g_CurrentPlayer->standheight : 160.33334f;
    g_CurrentPlayer->headpos.z = 0.0f;
    /* field_29BC is the authored spawn/camera height offset, not just the
     * current head bob height.  Preserve the value primed by the canonical
     * spawn path; recomputing it from standheight sinks Dam's first frame. */

    spawn_pos.x = pos->x;
    spawn_pos.y = floor_y + g_CurrentPlayer->field_29BC;
    spawn_pos.z = pos->z;

    /* Set collision struct via canonical helper — sets collision_position,
     * pos, pos3, current_tile_ptr, current_tile_ptr_for_portals,
     * applied_view, theta_transform, collision_radius (line 535) */
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &spawn_pos, stan);

    /* Override theta_transform with pad facing — matches lines 536-538 */
    g_CurrentPlayer->field_488.theta_transform.f[0] = -sinf(look_angle);
    g_CurrentPlayer->field_488.theta_transform.f[1] = FLOAT_INIT;
    g_CurrentPlayer->field_488.theta_transform.f[2] = cosf(look_angle);

    /* Update player facing — matches line 532 */
    g_CurrentPlayer->vv_theta = (look_angle * 360.0f) / M_TAU_F;

    /* Update prop position and previous position — matches lines 555-564 */
    g_CurrentPlayer->prop->pos.x = spawn_pos.x;
    g_CurrentPlayer->prop->pos.y = spawn_pos.y;
    g_CurrentPlayer->prop->pos.z = spawn_pos.z;
    g_CurrentPlayer->bondprevpos.x = spawn_pos.x;
    g_CurrentPlayer->bondprevpos.y = spawn_pos.y;
    g_CurrentPlayer->bondprevpos.z = spawn_pos.z;
    g_CurrentPlayer->prop->stan = stan;

    if (g_CurrentPlayer->ptr_char_objectinstance != NULL
        && g_CurrentPlayer->ptr_char_objectinstance->obj != NULL
        && g_CurrentPlayer->ptr_char_objectinstance->obj->RootNode != NULL)
    {
        setsuboffset(g_CurrentPlayer->ptr_char_objectinstance, &spawn_pos);
        setsubroty(g_CurrentPlayer->ptr_char_objectinstance,
                   get_curplay_horizontal_rotation_in_degrees());
    }

    /* Update field_3B8 — matches lines 578-580 */
    g_CurrentPlayer->field_3B8.f[0] = g_CurrentPlayer->field_488.pos.f[0] / FIELD_3B8_FACTOR;
    g_CurrentPlayer->field_3B8.f[1] = g_CurrentPlayer->field_488.pos.f[1] / FIELD_3B8_FACTOR;
    g_CurrentPlayer->field_3B8.f[2] = g_CurrentPlayer->field_488.pos.f[2] / FIELD_3B8_FACTOR;
    /* Rebuild the derived view basis from the newly-authored spawn facing.
     * This keeps applied_view/applied_view2 coherent with theta_transform
     * before the first gameplay frame consumes the handoff state. */
    portSyncSpawnViewBasis();

    /* Seed g_BgCurrentRoom from the spawn pad's stan tile room.
     * The BG visibility system reads g_BgCurrentRoom before the first
     * collision tick sets current_tile_ptr_for_portals. Without this,
     * g_BgCurrentRoom stays at its static initializer (room 1), which
     * breaks room visibility on levels where the player spawns far
     * from room 1 (e.g., Facility room 13). */
    {
        extern s32 g_BgCurrentRoom;
        if (stan != NULL) {
            g_BgCurrentRoom = stan->room;
        }
    }
}
#else
static void portPrimeSpawnHeightState(void)
{
}

static s32 portChooseIntroCameraIndex(s32 camera_count)
{
    if (camera_count <= 0) {
        return 0;
    }

    return randomGetNext() % camera_count;
}
#endif

void bondviewLoadSetupIntroSection(void)
{
    // declarations

    struct coord3d start_pos;
    f32 start_look_angle;
    StandTile *start_stan;
    struct SetupIntroEmpty *intro_record;
    s32 set_starting_weapon;
    s32 rand_camera_index;
    CreditsEntry *credits;
    s32 rand_pad_index;
    f32 stan_height;
    s32 i;
    s32 selected_intro_camera_index;
    struct SetupIntroItem *intro_item;
    struct SetupIntroSwirl *intro_swirl;
    struct SetupIntroWatch *intro_watch;
    struct SetupIntroCredits *intro_credits;
    s32 padding[5];

    // done with declarations

    start_pos = default_start_position;
    portPrimeSpawnHeightState();

    intro_record = (struct SetupIntroEmpty *)g_CurrentSetup.intro;
    g_isBondKIA = 0;
    g_bondviewForceDisarm = 0;
    resolution = 0;
    cameraBufferToggle = 0;
    cameraFrameCounter1 = 0;
    set_starting_weapon = 0;
    cameraFrameCounter2 = 0;
    start_look_angle = FLOAT_INIT;
    
    if (bossGetStageNum() == LEVELID_CUBA)
    {
#ifdef NATIVE_PORT
        resolution = (uintptr_t)mempAllocBytesInBank(0x46EA0, MEMPOOL_STAGE);
        resolution = (resolution + 0x3f) & ~(uintptr_t)0x3F;
#else
        resolution = (s32)mempAllocBytesInBank(0x46EA0, MEMPOOL_STAGE);
        resolution = (resolution + 0x3f) & ~0x3F;
#endif
        cameraFrameCounter1 = 1;
    }

    camera_80036438 = 0;
    credits_state = 0;
    credits_pointer = NULL;
    g_ForceBondMoveOffset.f[0] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[1] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[2] = FLOAT_INIT;
    g_SurroundBondWithExplosionsFlag = 0;
    startpadcount = 0;
    in_tank_flag = 0;
    g_WorldTankProp = 0;
    g_PlayerTankProp = NULL;
    g_PlayerTankYOffset = FLOAT_INIT;
    g_TankSfxState[0] = NULL;
    g_TankSfxState[1] = NULL;
    g_TankTurnSpeed = FLOAT_INIT;
    g_TankOrientationAngle = FLOAT_INIT;
    tank_turret_unused_angle = FLOAT_INIT;
    g_TankTurretVerticalAngle = FLOAT_INIT;
    g_TankTurretVerticalAngleRelated = FLOAT_INIT;
    g_TankTurretOrientationAngleRad = FLOAT_INIT;
    g_TankTurretOrientationAngleDeg = FLOAT_INIT;
    tank_turret_turn_speed = FLOAT_INIT;
    g_BondCanEnterTank = 0;
    g_TankTurretAngle = FLOAT_INIT;
    g_TankTurretTurn = FLOAT_INIT;
    g_ExplodeTankOnDeathFlag = 0;
    is_timer_active = 1;
    g_PlayerInvincible = 0;
    g_CameraMode = 0;
    g_CameraAfterCinema = 0;
    camera_fade_active = 0;
    stop_time_flag = 0;
    camera_transition_timer = FLOAT_INIT;
    intro_camera_index = CAMERAMODE_INTRO;
    selected_intro_camera_index = -1;
    g_IntroSwirl = NULL;
    ptr_random06cam_entry = NULL;
    g_CurrentSetupIntroCamera = NULL;
    g_SetupIntroCameraCount = 0;
    mission_timer = 0;
    watch_time_0 = 0;
    g_IntroAnimationIndex = 0;
    watch_transition_time = 0.9090909f;
    starting_weapon[GUNLEFT] = ITEM_UNARMED;
    starting_weapon[GUNRIGHT] = ITEM_UNARMED;
    
    if (intro_record != NULL)
    {
#ifdef NATIVE_PORT
        {
            static int trace_intro_walk = -1;
            if (trace_intro_walk < 0) {
                trace_intro_walk = (getenv("GE007_TRACE_INTRO_PARSE") != NULL);
            }
            if (trace_intro_walk) {
                fprintf(stderr,
                        "[INTRO-WALK] sizeof(camera)=%zu sizeof(swirl)=%zu sizeof(empty)=%zu intro=%p\n",
                        sizeof(struct SetupIntroCamera),
                        sizeof(struct SetupIntroSwirl),
                        sizeof(struct SetupIntroEmpty),
                        (void *)intro_record);
            }
        }
#endif
        while (intro_record->type != INTROTYPE_END)
        {
#ifdef NATIVE_PORT
            if (getenv("GE007_TRACE_INTRO_PARSE")) {
                fprintf(stderr,
                        "[INTRO-WALK] off=%ld type=%d ptr=%p\n",
                        (long)((u8 *)intro_record - (u8 *)g_CurrentSetup.intro),
                        intro_record->type,
                        (void *)intro_record);
            }
#endif
            switch (intro_record->type)
            {
                case INTROTYPE_SPAWN:
                {
                    if (g_CurrentSetup.pads != NULL
                        && (check_ramrom_flags() == ((struct SetupIntroSpawn*)intro_record)->is_demo_playback))
                    {
                        s32 sp_idx = ((struct SetupIntroSpawn*)intro_record)->index;
#ifdef NATIVE_PORT
                        if (getenv("GE007_VERBOSE")) {
                            printf("[STARTPAD] authored pad[%d], pad_pos=(%.1f,%.1f,%.1f)\n",
                                   sp_idx,
                                   g_CurrentSetup.pads[sp_idx].pos.f[0],
                                   g_CurrentSetup.pads[sp_idx].pos.f[1],
                                   g_CurrentSetup.pads[sp_idx].pos.f[2]);
                            fflush(stdout);
                        }
#endif
                        g_Startpad[startpadcount] = &g_CurrentSetup.pads[sp_idx];
                        startpadcount++;
                    }

#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroSpawn));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroSpawn));
#endif
                }
                break;

                case INTROTYPE_ITEM:
                {
                    intro_item = (struct SetupIntroItem*)intro_record;

                    if (check_ramrom_flags() == intro_item->is_demo_playback)
                    {
                        weaponLoadProjectileModels(intro_item->item_right);

                        if (intro_item->item_left >= 0)
                        {
                            weaponLoadProjectileModels(intro_item->item_left);
                            bondinvAddDoublesInvItem(intro_item->item_right, intro_item->item_left);
                        }
                        else
                        {
                            bondinvAddInvItem(intro_item->item_right);
                        }

                        if (set_starting_weapon == 0)
                        {
                            starting_weapon[GUNRIGHT] = intro_item->item_right;

                            if(intro_item->item_left);

                            set_starting_weapon = 1;
                            
                            if (intro_item->item_left >= 0)
                            {
                                starting_weapon[GUNLEFT] = intro_item->item_left;
                            }
                        }
                    }
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroItem));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroItem));
#endif
                }
                break;

                case INTROTYPE_AMMO:
                {
                    if (check_ramrom_flags() == ((struct SetupIntroAmmo*)intro_record)->is_demo_playback)
                    {
                        give_cur_player_ammo(((struct SetupIntroAmmo*)intro_record)->ammo_type, ((struct SetupIntroAmmo*)intro_record)->ammo_amount);
                    }
                   
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroAmmo));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroAmmo));
#endif
                }
                break;

                case INTROTYPE_SWIRL:
                {
                    intro_swirl = (struct SetupIntroSwirl*)intro_record;
                        
                    if (g_IntroSwirl == NULL)
                    {
                        g_IntroSwirl = intro_swirl;
                    }

                    intro_swirl->unk08.fval = intro_swirl->unk08.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk0C.fval = intro_swirl->unk0C.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk10.fval = intro_swirl->unk10.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk14.fval = intro_swirl->unk14.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk18.fval = intro_swirl->unk18.ival / M_U16_MAX_VALUE_F;
#ifdef NATIVE_PORT
                    if (getenv("GE007_VERBOSE")) {
                        printf("[INTRO_SWIRL] flags=0x%X ang=(%.4f,%.4f,%.4f) move=%.4f dist=%.4f pad=%d\n",
                               intro_swirl->unk04,
                               intro_swirl->unk08.fval, intro_swirl->unk0C.fval, intro_swirl->unk10.fval,
                               intro_swirl->unk14.fval, intro_swirl->unk18.fval,
                               intro_swirl->unk1C);
                        fflush(stdout);
                    }
#endif
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroSwirl));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroSwirl));
#endif
                }
                break;

                case INTROTYPE_ANIM:
                {
                    g_IntroAnimationIndex = ((struct SetupIntroAnim*)intro_record)->intro_anim;
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroAnim));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroAnim));
#endif
                }
                break;

                case INTROTYPE_CUFF:
                {
                    g_CurrentPlayer->bondtype = ((struct SetupIntroCuff*)intro_record)->bondtype;
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCuff));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCuff));
#endif
                }
                break;

                case INTROTYPE_CAMERA:
                {
                    if (get_cur_playernum() == 0)
                    {
                        ((struct SetupIntroCamera*)intro_record)->prev = g_CurrentSetupIntroCamera;
                        g_CurrentSetupIntroCamera = (struct SetupIntroCamera*)intro_record;
                        g_SetupIntroCameraCount = g_SetupIntroCameraCount + 1;

                        ((struct SetupIntroCamera*)intro_record)->unk04.fval = ((struct SetupIntroCamera*)intro_record)->unk04.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk08.fval = ((struct SetupIntroCamera*)intro_record)->unk08.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk0C.fval = ((struct SetupIntroCamera*)intro_record)->unk0C.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk10.fval = ((struct SetupIntroCamera*)intro_record)->unk10.ival / M_U16_MAX_VALUE_F;
                        ((struct SetupIntroCamera*)intro_record)->unk14.fval = ((struct SetupIntroCamera*)intro_record)->unk14.ival / M_U16_MAX_VALUE_F;

                        ((struct SetupIntroCamera*)intro_record)->lang1c.lang_ptr = (char *)langGet(((struct SetupIntroCamera*)intro_record)->lang1c.lang_index[1]);
                        if (((struct SetupIntroCamera*)intro_record)->lang20.lang_index != 0)
                        {
                            ((struct SetupIntroCamera*)intro_record)->lang20.lang_ptr = (char *)langGet((u16)((struct SetupIntroCamera*)intro_record)->lang20.lang_index);
                        }
#ifdef NATIVE_PORT
                        if (getenv("GE007_VERBOSE")) {
                            struct SetupIntroCamera *cam = (struct SetupIntroCamera*)intro_record;
                            printf("[INTRO_CAMERA] idx=%d pos=(%.2f,%.2f,%.2f) ang=(%.4f,%.4f) pad=%d text1=%p text2=%p\n",
                                   g_SetupIntroCameraCount - 1,
                                   cam->unk04.fval, cam->unk08.fval, cam->unk0C.fval,
                                   cam->unk10.fval, cam->unk14.fval, cam->unk18,
                                   (void *)cam->lang1c.lang_ptr, (void *)cam->lang20.lang_ptr);
                            fflush(stdout);
                        }
#endif
                    }
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCamera));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCamera));
#endif
                }
                break;

                case INTROTYPE_WATCH:
                {
                    intro_watch = (struct SetupIntroWatch*)intro_record;

                    watch_time_0 = 0;

                    if (intro_watch->minutes > 0)
                    {
                        watch_time_0 += (intro_watch->minutes % 60) * (60*60);
                    }

                    if (intro_watch->hours > 0)
                    {
                        watch_time_0 += ((intro_watch->hours % 12) * (60*60*60));
                    }

                    if (watch_time_0);
#ifdef NATIVE_PORT
                    if (getenv("GE007_VERBOSE")) {
                        printf("[INTRO_WATCH] hours=%d minutes=%d ticks=%d\n",
                               intro_watch->hours, intro_watch->minutes, watch_time_0);
                        fflush(stdout);
                    }
#endif
                    
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroWatch));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroWatch));
#endif
                }
                break;
                    
                case INTROTYPE_CREDITS:
                {
                    intro_credits = (struct SetupIntroCredits*)intro_record;
                    
#ifdef NATIVE_PORT
                    credits = (CreditsEntry*)((uintptr_t)g_ptrStageSetupFile + (u32)intro_credits->unk04);
#else
                    // hack: bad address math
                    credits = (CreditsEntry*)((s32)g_ptrStageSetupFile + (s32)intro_credits->unk04);
#endif
                    credits_pointer = credits;

                    // what is the point of this?
                    while (credits->TextId1 != 0 || credits->TextId2 != 0)
                    {
                        credits++;
                    }

#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCredits));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCredits));
#endif
                }
                break;

                default:
                {
                    #ifdef DEBUG
                        ossyncprintf("unknown bondstart type %d!\n",intro_record->type);
                    #endif
#ifdef NATIVE_PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroEmpty));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroEmpty));
#endif
                }
                break;

            }
        }
    }

    if (g_CurrentSetupIntroCamera != NULL)
    {
        s32 authored_intro_camera_index;

        ptr_random06cam_entry = g_CurrentSetupIntroCamera;
        authored_intro_camera_index = portChooseIntroCameraIndex(g_SetupIntroCameraCount);
        selected_intro_camera_index = authored_intro_camera_index;
        rand_camera_index = (g_SetupIntroCameraCount - 1) - authored_intro_camera_index;
        while (rand_camera_index > 0)
        {
            rand_camera_index--;
            ptr_random06cam_entry = ptr_random06cam_entry->prev;
        }
    }

#ifdef NATIVE_PORT
    if (getenv("GE007_VERBOSE")) {
        printf("[INTRO_SUMMARY] spawns=%d cameras=%d swirl=%d selected_cam=%p\n",
               startpadcount, g_SetupIntroCameraCount, g_IntroSwirl != NULL,
               (void *)ptr_random06cam_entry);
        printf("[INTRO_CAMERA_INDEX] selected=%d deterministic=%d forced=%s\n",
               selected_intro_camera_index,
               g_deterministic,
               getenv("GE007_INTRO_CAMERA_INDEX") ? getenv("GE007_INTRO_CAMERA_INDEX") : "<auto>");
        if (ptr_random06cam_entry != NULL) {
            f32 cam_x = ptr_random06cam_entry->unk04.fval;
            f32 cam_y = ptr_random06cam_entry->unk08.fval;
            f32 cam_z = ptr_random06cam_entry->unk0C.fval;
            StandTile *cam_nearest = sub_GAME_7F0AFB78(&cam_x, &cam_y, &cam_z, 0.0f);
            printf("[INTRO_SELECTED] pos=(%.2f,%.2f,%.2f) ang=(%.4f,%.4f) pad=%d prev=%p\n",
                   ptr_random06cam_entry->unk04.fval,
                   ptr_random06cam_entry->unk08.fval,
                   ptr_random06cam_entry->unk0C.fval,
                   ptr_random06cam_entry->unk10.fval,
                   ptr_random06cam_entry->unk14.fval,
                   ptr_random06cam_entry->unk18,
                   (void *)ptr_random06cam_entry->prev);
            printf("[INTRO_SELECTED_ROOM] camera_room=%d\n",
                   cam_nearest ? cam_nearest->room : -1);
            if (g_CurrentSetup.pads != NULL && ptr_random06cam_entry->unk18 >= 0) {
                PadRecord *cam_pad = &g_CurrentSetup.pads[ptr_random06cam_entry->unk18];
                printf("[INTRO_SELECTED_PAD] pos=(%.1f,%.1f,%.1f) room=%d\n",
                       cam_pad->pos.f[0], cam_pad->pos.f[1], cam_pad->pos.f[2],
                       cam_pad->stan ? cam_pad->stan->room : -1);
            }
        }
        fflush(stdout);
    }
#endif

    bondinvAddInvItem(ITEM_FIST);

    if (set_starting_weapon == 0)
    {
        starting_weapon[GUNRIGHT] = ITEM_FIST;
    }

    g_CurrentPlayer->field_78 = FLOAT_INIT;
    g_CurrentPlayer->field_7C = -0.0001f;
    g_CurrentPlayer->field_80 = FLOAT_INIT;

    if (startpadcount > 0)
    {
        if ((getPlayerCount() >= 2) && (startpadcount > 0))
        {
            rand_pad_index = bondviewGetRandomSpawnPadIndex();
        }
        else
        {
            rand_pad_index = 0;
        }

        start_pos.f[0] = g_Startpad[rand_pad_index]->pos.f[0];
        start_pos.f[2] = g_Startpad[rand_pad_index]->pos.f[2];

#ifdef XBLADEBUG
    #error fix XBLADEBUG
        //if (*((&g_Startpad)[local_74] + 0x28) == 0) {
        //    assertPrint_8291E690
        //              (".\\ported\\bondview_r.cpp",0x171,"Assertion failed: g_Startpad[sp]->stan");
        //}
#endif

        start_stan = g_Startpad[rand_pad_index]->stan;

        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->field_29BC + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
#ifdef NATIVE_PORT
        if (getenv("GE007_VERBOSE")) {
            coord3d pad_probe = g_Startpad[rand_pad_index]->pos;
            StandTile *nearest_pad_stan = sub_GAME_7F0AFB78(&pad_probe.f[0], &pad_probe.f[1], &pad_probe.f[2], 0.0f);
            f32 nearest_pad_floor = nearest_pad_stan
                ? bondviewYPositionRelated(nearest_pad_stan, g_Startpad[rand_pad_index]->pos.f[0], g_Startpad[rand_pad_index]->pos.f[2])
                : 0.0f;
            printf("[INTRO_SPAWN] pad_idx=%d pad_pos=(%.1f,%.1f,%.1f) start_pos=(%.1f,%.1f,%.1f) stan_height=%.1f field_29BC=%.1f field_70=%.1f start_stan=%p\n",
                   rand_pad_index,
                   g_Startpad[rand_pad_index]->pos.f[0], g_Startpad[rand_pad_index]->pos.f[1], g_Startpad[rand_pad_index]->pos.f[2],
                   start_pos.f[0], start_pos.f[1], start_pos.f[2],
                   stan_height, g_CurrentPlayer->field_29BC, g_CurrentPlayer->field_70,
                   (void*)start_stan);
            printf("[INTRO_SPAWN_TILE] plink=%s start_room=%d nearest_room=%d pad_y=%.1f start_floor=%.1f nearest_floor=%.1f\n",
                   g_Startpad[rand_pad_index]->plink ? g_Startpad[rand_pad_index]->plink : "<null>",
                   start_stan ? start_stan->room : -1,
                   nearest_pad_stan ? nearest_pad_stan->room : -1,
                   g_Startpad[rand_pad_index]->pos.f[1],
                   stan_height,
                   nearest_pad_floor);
            fflush(stdout);
        }
#endif
        {
            f32 raw_start_look_angle = M_TAU_F - atan2f(g_Startpad[rand_pad_index]->look.f[0], g_Startpad[rand_pad_index]->look.f[2]);
#ifdef NATIVE_PORT
            start_look_angle = portAdjustPadLookAngle(raw_start_look_angle);
            if (getenv("GE007_VERBOSE")) {
                fprintf(stderr,
                        "[INTRO_LOOK] pad_idx=%d look=(%.2f,%.2f,%.2f) raw_angle=%.3f final_angle=%.3f\n",
                        rand_pad_index,
                        g_Startpad[rand_pad_index]->look.f[0],
                        g_Startpad[rand_pad_index]->look.f[1],
                        g_Startpad[rand_pad_index]->look.f[2],
                        raw_start_look_angle, start_look_angle);
            }
#else
            start_look_angle = raw_start_look_angle;
#endif
        }
    }
    else
    {
        start_stan = sub_GAME_7F0AFB78(&start_pos.f[0], &start_pos.f[1], &start_pos.f[2], 30.0f);
        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->field_29BC + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
    }

    g_CurrentPlayer->vv_theta = (start_look_angle * 360.0f) / M_TAU_F;
    g_CurrentPlayer->stanHeight = stan_height;
    g_CurrentPlayer->field_6C = stan_height / FIELD_6C_FACTOR;
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &start_pos, start_stan);
    g_CurrentPlayer->field_488.theta_transform.f[0] = -sinf(start_look_angle);
    g_CurrentPlayer->field_488.theta_transform.f[1] = FLOAT_INIT;
    g_CurrentPlayer->field_488.theta_transform.f[2] = cosf(start_look_angle);
    sub_GAME_7F089718(D_800364D0);
    dword_CODE_bss_80079DA0 = 0;
    

    for (i=0; i<BSS_80079DA8_LENGTH; i++)
    {
        dword_CODE_bss_80079DA4 = 0;
        dword_CODE_bss_80079DA8[i] = 0;
    }

    bondviewResetIntroCameraMessageDialogs();
    bondviewResetUpperTextDisplay();
    g_CurrentPlayer->prop = chrpropAllocate();
    g_CurrentPlayer->prop->obj = NULL;
    g_CurrentPlayer->prop->type = PROP_TYPE_VIEWER;
    
    g_CurrentPlayer->prop->pos.f[0] = 
        g_CurrentPlayer->bondprevpos.f[0] = start_pos.f[0];

    g_CurrentPlayer->prop->pos.f[1] =
        g_CurrentPlayer->bondprevpos.f[1] = start_pos.f[1];

    g_CurrentPlayer->prop->pos.f[2] =
        g_CurrentPlayer->bondprevpos.f[2] = start_pos.f[2];

    g_CurrentPlayer->prop->stan = start_stan;
#ifdef NATIVE_PORT
    if (getenv("GE007_VERBOSE")) {
        printf("[PROP_CREATED] prop->pos=(%.1f,%.1f,%.1f) start_pos=(%.1f,%.1f,%.1f) field_488.col_pos=(%.1f,%.1f,%.1f)\n",
               g_CurrentPlayer->prop->pos.f[0], g_CurrentPlayer->prop->pos.f[1], g_CurrentPlayer->prop->pos.f[2],
               start_pos.f[0], start_pos.f[1], start_pos.f[2],
               g_CurrentPlayer->field_488.collision_position.f[0],
               g_CurrentPlayer->field_488.collision_position.f[1],
               g_CurrentPlayer->field_488.collision_position.f[2]);
        fflush(stdout);
    }
#endif
    chrpropActivate(g_CurrentPlayer->prop);
    chrpropEnable(g_CurrentPlayer->prop);
    g_CurrentPlayer->field_3B8.f[0] = (g_CurrentPlayer->field_488.pos.f[0] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[1] = (g_CurrentPlayer->field_488.pos.f[1] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[2] = (g_CurrentPlayer->field_488.pos.f[2] / FIELD_3B8_FACTOR);
    
    if (getPlayerCount() == 1)
    {
#ifdef NATIVE_PORT
        /* D2: stock (the #else below) enters CAMERAMODE_INTRO unconditionally
         * whenever an intro is requested; this port additionally required
         * g_IntroSwirl != NULL, so a cinema-only intro (camera data present,
         * no swirl control points) never played at all and fell straight to
         * the FP handoff below. GE007_NO_CINEMA_INTRO_FIX=1 restores the old
         * (swirl-required) gate for A/B. Every retail stage ships swirl data
         * (T3 coverage), so this branch cannot be exercised against retail
         * content -- see the SWIRL-side no-data fallback audit in
         * bondview.c's bondviewFrozenCameraTick for the companion fix. */
        if (portIntroEnabled() &&
            (g_IntroSwirl != NULL || !port_env_set("GE007_NO_CINEMA_INTRO_FIX", NULL))) {
            if (getenv("GE007_VERBOSE")) {
                fprintf(stderr, "[INTRO] authentic level intro enabled\n");
            }
            bondviewSetCameraMode(CAMERAMODE_INTRO);
        } else {
            /* Native direct-boot defaults to the short FP handoff. Authored
             * intro parity is still available with GE007_ENABLE_LEVEL_INTRO=1. */
            if (portIntroEnabled() && getenv("GE007_VERBOSE")) {
                fprintf(stderr, "[INTRO] authentic intro requested but no swirl data; falling back to gameplay spawn\n");
            }
            /* Finalize the gameplay spawn before entering FP mode.
             * The FP handoff seeds camera/collision/weapon state from the
             * current prop + collision values, so the authored gameplay spawn
             * must already be installed before bondviewSetCameraMode(FP). */
            portApplyGameplaySpawnFromIntro();
            bondviewSetCameraMode(CAMERAMODE_FP);
        }
#else
        bondviewSetCameraMode(CAMERAMODE_INTRO);
#endif
    }
    else
    {
        bondviewSetCameraMode(CAMERAMODE_MP);
    }

    g_bondviewBondDeathAnimationsCount = 0;
    while (g_bondviewBondDeathAnimations[g_bondviewBondDeathAnimationsCount] != 0)
    {
        g_bondviewBondDeathAnimationsCount++;
    }
    
    g_CurrentPlayer->startnewbonddie = TRUE;
    g_CurrentPlayer->redbloodfinished = FALSE;
    g_CurrentPlayer->deathanimfinished = FALSE;
    camera_mode = CAMERAMODE_NONE;
}
