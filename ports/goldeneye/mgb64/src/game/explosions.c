#include <ultra64.h>
#include <PR/os.h>
#include <PR/gbi.h>
#include <gbi_extension.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <limits.h>
#include "bg.h"
#include "bondview.h"
#include "boss.h"
#include "cheat_buttons.h"
#include "chrai.h"
#include "chrlv.h"
#include "chrobjhandler.h"
#include "dyn.h"
#include "explosions.h"
#include "fr.h"
#include "image_bank.h"
#include "othermodemicrocode.h"
#include "lvl.h"
#include "matrixmath.h"
#include "music.h"
#include "player.h"
#include "random.h"
#include "snd.h"
#include "stan.h"
#include "unk_0BC530.h"
#include <assets/GlobalImageTable.h>

#ifdef NATIVE_PORT
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "gfx_pc.h"
extern s32 get_cur_playernum(void);
extern s32 getPlayerPointerIndex(PropRecord *prop);
extern int g_frame_count_diag;
extern Mtxf *modelFindNodeMtx(Model *model, ModelNode *node, s32 arg2);
extern s32 modelGetRenderPosCount(Model *model);
extern RenderPosView *modelAllocRenderPos(Model *model);
extern void instcalcmatrices(ModelRenderData *renderdata, Model *model);
extern void portTraceBulletImpactCreate(s32 slot,
                                        s32 impact_type,
                                        s32 room,
                                        const PropRecord *prop,
                                        s32 model_pos,
                                        s32 clear,
                                        s32 width,
                                        s32 height,
                                        const coord3d *pos,
                                        const coord3d *normal,
                                        f32 size_x,
                                        f32 size_y,
                                        f32 normal_offset,
                                        const f32 normal_unit[3],
                                        const f32 axis_x[3],
                                        const f32 axis_y[3],
                                        const f32 raw_vertices[4][3],
                                        const s16 rounded_vertices[4][3]);
extern void portTraceBulletImpactRender(const PropRecord *prop,
                                        s32 world,
                                        s32 alpha_pass,
                                        s32 flat,
                                        s32 rendered,
                                        s32 last_impact,
                                        s32 current_slot);

static int portDisableBulletImpacts(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE007_DISABLE_BULLET_IMPACTS") != NULL;
    }
    return cached;
}

static int portDisablePropBulletImpacts(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE007_DISABLE_PROP_BULLET_IMPACTS") != NULL;
    }
    return cached;
}

static int portTraceBulletImpacts(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE007_TRACE_BULLET_IMPACTS") != NULL;
    }
    return cached;
}

static int portFlatBulletImpacts(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE007_FLAT_BULLET_IMPACTS") != NULL;
    }
    return cached;
}

static int portFlatPropBulletImpacts(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("GE007_FLAT_PROP_BULLET_IMPACTS") != NULL;
    }
    return cached;
}

static s32 portPropIsGlassLike(PropRecord *prop);

static int portUseFlatBulletImpacts(PropRecord *prop)
{
    if (portFlatBulletImpacts()) {
        return 1;
    }

    if (prop != NULL) {
        /* Tier-2 / N64 parity: ALL destroyable props (crates, barrels, screens,
         * glass) render their bullet impacts as the textured bullet-hole decal,
         * exactly like the original decomp -- which has no flat path at all -- and
         * like world/wall impacts. The original code unconditionally calls
         * texSelect() for every prop impact (see the #else branch of
         * explosionRenderBulletImpactOnProp); the flat path here is a port-only
         * workaround that drew a solid, untextured G_CC_SHADE quad in the impact's
         * "appearance" colour. Because ~17 of the 20 g_ImpactTypes entries have
         * apptype==1, that colour is a deterministic near-white (0xFF - rand%40)
         * -- i.e. the white squares the user saw -- with no texture alpha to carve
         * the hole shape. The textured path modulates the IA/RGBA bullet-hole
         * texture (G_CC_MODULATEIA) so its alpha cuts the decal exactly as on N64.
         *
         * The textured path is now self-contained: explosionRenderBulletImpactOnProp
         * emits a full RDP state-reset epilogue so it can no longer leak
         * texture/combine/rendermode/cycle state into following inline prop/child
         * geometry (precautionary hardening -- glass has run this path since
         * 76d4c48 with no observed leak). GE007_FLAT_PROP_BULLET_IMPACTS still
         * forces the legacy flat path for debugging/A-B; GE007_FLAT_BULLET_IMPACTS
         * forces flat globally (world + prop). */
        if (portFlatPropBulletImpacts()) {
            return 1;
        }

        return 0;
    }

    return 0;
}

static float portClampBulletImpactNormalOffset(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }

    return value > 20.0f ? 20.0f : value;
}

static float portGlobalBulletImpactNormalOffset(int *is_set)
{
    static int initialized = 0;
    static int has_value = 0;
    static float value = 0.0f;

    if (!initialized) {
        const char *env = getenv("GE007_BULLET_IMPACT_NORMAL_OFFSET");
        if (env && *env) {
            value = portClampBulletImpactNormalOffset(strtof(env, NULL));
            has_value = 1;
        }
        initialized = 1;
    }

    if (is_set != NULL) {
        *is_set = has_value;
    }

    return value;
}

static float portGlassBulletImpactNormalOffset(void)
{
    static int initialized = 0;
    static float value = 2.0f;

    if (!initialized) {
        const char *env = getenv("GE007_GLASS_BULLET_IMPACT_NORMAL_OFFSET");
        if (env && *env) {
            value = strtof(env, NULL);
        }
        value = portClampBulletImpactNormalOffset(value);
        initialized = 1;
    }

    return value;
}

static s32 portPropIsGlassLike(PropRecord *prop)
{
    if (prop == NULL || prop->obj == NULL) {
        return 0;
    }

    return prop->obj->type == PROPDEF_GLASS ||
           prop->obj->type == PROPDEF_TINTED_GLASS;
}

static s32 portImpactIsGlassCrack(s32 impact_type)
{
    return impact_type >= 0x11 && impact_type <= 0x13;
}

static float portBulletImpactNormalOffset(PropRecord *prop, s32 impact_type)
{
    int has_global_offset = 0;
    float global_offset = portGlobalBulletImpactNormalOffset(&has_global_offset);

    if (has_global_offset) {
        return global_offset;
    }

    if (portImpactIsGlassCrack(impact_type)) {
        return portGlassBulletImpactNormalOffset();
    }

    return 0.0f;
}

static f32 portGlassImpactAxisBoost(const coord3d *axis_view, f32 axis_len)
{
    f32 screen_len;
    f32 ratio;
    f32 boost;
    /* Keep glass cracks surface-aligned, but stop edge-on panes from
     * projecting prop-attached impacts down to a one-pixel sliver. */
    const f32 target_ratio = 0.70f;
    const f32 max_boost = 6.0f;

    if (axis_view == NULL || axis_len <= 0.0001f) {
        return 1.0f;
    }

    screen_len = sqrtf(axis_view->x * axis_view->x + axis_view->y * axis_view->y);
    ratio = screen_len / axis_len;

    if (ratio >= target_ratio) {
        return 1.0f;
    }

    boost = target_ratio / (ratio > 0.0001f ? ratio : 0.0001f);
    return boost > max_boost ? max_boost : boost;
}

static s32 portEnsurePropImpactRenderPos(PropRecord *prop, s8 *model_render_pos_index)
{
    ObjectRecord *obj;
    Model *model;
    s32 matrix_count;
    Mtxf *attach_mtx;
    ModelRenderData renderdata;

    if (prop == NULL || prop->obj == NULL || prop->obj->model == NULL || model_render_pos_index == NULL) {
        return 0;
    }

    obj = prop->obj;
    model = obj->model;
    matrix_count = modelGetRenderPosCount(model);

    if (matrix_count <= 0) {
        return 0;
    }

    if (*model_render_pos_index < 0 || *model_render_pos_index >= matrix_count) {
        *model_render_pos_index = 0;
    }

    if (model->render_pos != NULL) {
        return 1;
    }

    if (model->attachedto == NULL || model->attachedto_objinst == NULL) {
        return 0;
    }

    attach_mtx = modelFindNodeMtx(model->attachedto, model->attachedto_objinst, 0);
    if (attach_mtx == NULL) {
        return 0;
    }

    memset(&renderdata, 0, sizeof(renderdata));
    renderdata.zbufferenabled = TRUE;
    renderdata.flags = 3;
    renderdata.unk_matrix = attach_mtx;
    renderdata.mtxlist = (Mtxf *)modelAllocRenderPos(model);
    if (renderdata.mtxlist == NULL) {
        return 0;
    }

    instcalcmatrices(&renderdata, model);

    return model->render_pos != NULL;
}

static int portTraceExplosions(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *env = getenv("GE007_EXPLOSION_TRACE");
        cached = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }

    return cached;
}

static int portDisableFlyingParticles(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *env = getenv("GE007_DISABLE_FLYING_PARTICLES");
        cached = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }

    return cached;
}

static int portDisableScorchMarks(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *env = getenv("GE007_DISABLE_SCORCH_MARKS");
        cached = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }

    return cached;
}

static int portUseNativeExplosionFallback(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *env = getenv("GE007_NATIVE_EXPLOSION_FALLBACK");
        if (env != NULL && env[0] != '\0') {
            cached = env[0] != '0' ? 1 : 0;
        } else {
            env = getenv("GE007_ORIGINAL_EXPLOSION_TEXTURES");
            cached = (env != NULL && env[0] != '\0' && env[0] == '0') ? 1 : 0;
        }
    }

    return cached;
}

static int portExplosionPartFade(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *env = getenv("GE007_EXPLOSION_PART_FADE");
        /* Default ON: restore the original N64 per-part distance fade in
         * explosionRenderPart. Close fireball billboards are scaled down (f2)
         * and their centre pulled toward the player, so a point-blank blast
         * does not fill the screen with giant full-size quads. The port
         * previously stubbed f2=1.0 and used the raw part position.
         * GE007_EXPLOSION_PART_FADE=0 restores that flat full-size behavior. */
        cached = (env != NULL && env[0] == '0') ? 0 : 1;
    }

    return cached;
}

static void portExplosionTrace(PropRecord *source,
                               struct coord3d *target_pos,
                               s16 explosion_type,
                               s32 arg4,
                               s32 player,
                               u8 *rooms,
                               s32 arg7)
{
    extern int g_frame_count_diag;
    int room0 = -1;
    int room1 = -1;
    int room2 = -1;
    int room3 = -1;

    if (!portTraceExplosions() || target_pos == NULL) {
        return;
    }

    if (rooms != NULL) {
        room0 = rooms[0] != 0xFF ? rooms[0] : -1;
        room1 = rooms[0] != 0xFF && rooms[1] != 0xFF ? rooms[1] : -1;
        room2 = rooms[0] != 0xFF && rooms[1] != 0xFF && rooms[2] != 0xFF ? rooms[2] : -1;
        room3 = rooms[0] != 0xFF && rooms[1] != 0xFF && rooms[2] != 0xFF && rooms[3] != 0xFF ? rooms[3] : -1;
    }

    fprintf(stderr,
            "[EXPLOSION_TRACE] frame=%d type=%d player=%d arg4=%d arg7=%d source_type=%d "
            "source_pos=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f) rooms=[%d,%d,%d,%d]\n",
            g_frame_count_diag,
            explosion_type,
            player,
            arg4,
            arg7,
            source != NULL ? source->type : -1,
            source != NULL ? source->pos.x : 0.0f,
            source != NULL ? source->pos.y : 0.0f,
            source != NULL ? source->pos.z : 0.0f,
            target_pos->x,
            target_pos->y,
            target_pos->z,
            room0,
            room1,
            room2,
            room3);
    fflush(stderr);
}

static int g_ExplosionDamageTraceEnabled = -1;
static int g_ExplosionDamageTraceBudget = 0;
static int g_ExplosionDamageTraceFilterObj = INT_MIN;
static int g_ExplosionDamageTraceFilterPad = INT_MIN;
static int g_ExplosionDamageTraceFilterType = INT_MIN;

#define STATUE_HELICOPTER_EXPLOSION_TYPE 13
#define STATUE_NATALYA_CHRNUM 0
#define STATUE_NATALYA_RESCUED_FLAG 0x20000000

static int portShouldProtectStatueRescuedNatalyaFromExplosion(PropRecord *prop, s16 explosion_type)
{
    if (bossGetStageNum() != LEVELID_STATUE ||
        explosion_type != STATUE_HELICOPTER_EXPLOSION_TYPE ||
        prop == NULL ||
        prop->type != PROP_TYPE_CHR ||
        prop->chr == NULL ||
        prop->chr->chrnum != STATUE_NATALYA_CHRNUM) {
        return 0;
    }

    return chrHasStageFlag(NULL, STATUE_NATALYA_RESCUED_FLAG) ? 1 : 0;
}

static int portTraceExplosionDamage(void)
{
    const char *env;

    if (g_ExplosionDamageTraceEnabled >= 0) {
        return g_ExplosionDamageTraceEnabled;
    }

    env = getenv("GE007_EXPLOSION_DAMAGE_TRACE");
    g_ExplosionDamageTraceEnabled = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;

    env = getenv("GE007_EXPLOSION_DAMAGE_TRACE_BUDGET");
    g_ExplosionDamageTraceBudget = (env != NULL && env[0] != '\0') ? atoi(env) : 480;

    env = getenv("GE007_EXPLOSION_DAMAGE_TRACE_OBJ");
    if (env != NULL && env[0] != '\0') {
        g_ExplosionDamageTraceFilterObj = atoi(env);
    }

    env = getenv("GE007_EXPLOSION_DAMAGE_TRACE_PAD");
    if (env != NULL && env[0] != '\0') {
        g_ExplosionDamageTraceFilterPad = atoi(env);
    }

    env = getenv("GE007_EXPLOSION_DAMAGE_TRACE_TYPE");
    if (env != NULL && env[0] != '\0') {
        g_ExplosionDamageTraceFilterType = atoi(env);
    }

    return g_ExplosionDamageTraceEnabled;
}

static int portExplosionDamageTraceTypeMatches(s32 explosion_type)
{
    return g_ExplosionDamageTraceFilterType == INT_MIN ||
           g_ExplosionDamageTraceFilterType == explosion_type;
}

static int portExplosionDamageTraceObjectMatches(const struct ObjectRecord *obj)
{
    if (obj == NULL) {
        return 0;
    }

    if (g_ExplosionDamageTraceFilterObj != INT_MIN &&
        obj->obj != g_ExplosionDamageTraceFilterObj) {
        return 0;
    }

    if (g_ExplosionDamageTraceFilterPad != INT_MIN &&
        obj->pad != g_ExplosionDamageTraceFilterPad) {
        return 0;
    }

    return 1;
}

static void portExplosionDamageTracePrintf(s32 explosion_type, const char *fmt, ...)
{
    va_list ap;

    if (!portTraceExplosionDamage() ||
        !portExplosionDamageTraceTypeMatches(explosion_type)) {
        return;
    }

    if (g_ExplosionDamageTraceBudget == 0) {
        return;
    }

    if (g_ExplosionDamageTraceBudget > 0) {
        g_ExplosionDamageTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[EXPLOSION_DAMAGE_TRACE] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static s32 portExplosionDamageTraceRoomU8(const u8 *rooms, s32 index)
{
    if (rooms == NULL || rooms[0] == 0xFF) {
        return -1;
    }

    for (s32 i = 0; i < index; i++) {
        if (rooms[i] == 0xFF) {
            return -1;
        }
    }

    return rooms[index] == 0xFF ? -1 : rooms[index];
}

static s32 portExplosionDamageTraceRoomS32(const s32 *rooms, s32 index)
{
    if (rooms == NULL || rooms[0] < 0) {
        return -1;
    }

    for (s32 i = 0; i < index; i++) {
        if (rooms[i] < 0) {
            return -1;
        }
    }

    return rooms[index] < 0 ? -1 : rooms[index];
}

static s32 portExplosionDamageTraceCandidateCount(void)
{
    s16 *propnumptr;
    s32 count = 0;

    if (ptr_list_object_lookup_indices == NULL) {
        return 0;
    }

    for (propnumptr = ptr_list_object_lookup_indices; *propnumptr >= 0; propnumptr++) {
        count++;
    }

    return count;
}

static s32 portExplosionDamageTracePropIndex(PropRecord *prop)
{
    if (prop == NULL) {
        return -1;
    }

    return prop - pos_data_entry;
}

static s32 portExplosionDamageTracePropObj(PropRecord *prop)
{
    if (prop != NULL &&
        (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON || prop->type == PROP_TYPE_DOOR) &&
        prop->obj != NULL) {
        return prop->obj->obj;
    }

    return -1;
}

static s32 portExplosionDamageTracePropPad(PropRecord *prop)
{
    if (prop != NULL &&
        (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON || prop->type == PROP_TYPE_DOOR) &&
        prop->obj != NULL) {
        return prop->obj->pad;
    }

    return -1;
}

static void portExplosionDamageTraceObjectCandidate(s32 explosion_type,
                                                    PropRecord *prop,
                                                    struct ObjectRecord *obj,
                                                    f32 xdist,
                                                    f32 ydist,
                                                    f32 zdist,
                                                    f32 horiz_range,
                                                    f32 vert_range,
                                                    s32 in_range,
                                                    f32 falloff,
                                                    f32 raw_damage,
                                                    f32 applied_damage,
                                                    s32 skip_runtime,
                                                    s32 skip_flags2,
                                                    s32 call_damage)
{
    if (!portTraceExplosionDamage() ||
        !portExplosionDamageTraceObjectMatches(obj)) {
        return;
    }

    portExplosionDamageTracePrintf(
        explosion_type,
        "frame=%d event=object_candidate prop_index=%d prop=%p prop_type=%d "
        "obj=%d type=%d pad=%d room=%d runtime=0x%08X flags=0x%08X flags2=0x%08X "
        "maxdamage=%.2f damage=%.2f pos=(%.2f,%.2f,%.2f) "
        "delta=(%.2f,%.2f,%.2f) range=(%.2f,%.2f) in_range=%d "
        "falloff=%.4f raw_damage=%.4f applied_damage=%.4f "
        "skip_runtime=%d skip_flags2=%d call=%d",
        g_frame_count_diag,
        portExplosionDamageTracePropIndex(prop),
        (void *)prop,
        prop != NULL ? prop->type : -1,
        obj != NULL ? obj->obj : -1,
        obj != NULL ? obj->type : -1,
        obj != NULL ? obj->pad : -1,
        prop != NULL && prop->stan != NULL ? prop->stan->room : -1,
        obj != NULL ? obj->runtime_bitflags : 0,
        obj != NULL ? obj->flags : 0,
        obj != NULL ? obj->flags2 : 0,
        obj != NULL ? obj->maxdamage : 0.0f,
        obj != NULL ? obj->damage : 0.0f,
        obj != NULL ? obj->runtime_pos.f[0] : 0.0f,
        obj != NULL ? obj->runtime_pos.f[1] : 0.0f,
        obj != NULL ? obj->runtime_pos.f[2] : 0.0f,
        xdist,
        ydist,
        zdist,
        horiz_range,
        vert_range,
        in_range,
        falloff,
        raw_damage,
        applied_damage,
        skip_runtime,
        skip_flags2,
        call_damage);
}
#endif

// bss
//CODE.bss:8007A100
// possibly   printf("Allocating %d bytes for glass data (%d bits)\n",DAT_83bd5fb0 * 0x88 + 0xf & 0xfffffff0,         DAT_83bd5fb0);
Mtx dword_CODE_bss_8007A100;

/**
 * g_SmokeBuffer = mempAllocBytesInBank(0x1FE0, MEMPOOL_STAGE);
 * printf("Allocating %d bytes for smoke data\n",0x9f60);
 * Address 0x8007A140.
*/
struct Smoke *g_SmokeBuffer;

/**
 * g_ExplosionBuffer = mempAllocBytesInBank(0x1740, MEMPOOL_STAGE);
 * printf("Allocating %d bytes for explosion data\n",0x2e80);
 * Address 0x8007A144.
*/
struct Explosion *g_ExplosionBuffer;

//CODE.bss:8007A148
s32 max_particles;
//CODE.bss:8007A14C
// printf("Allocating %d bytes for debris data (%d bits)\n", DAT_83bd2af0 * 0xa4, DAT_83bd2af0);
struct FlyingParticles *g_FlyingParticlesBuffer;

/**
 * g_ScorchBuffer = mempAllocBytesInBank(0x6E0, MEMPOOL_STAGE);
 * printf("Allocating %d bytes for scorch data\n",0xa50);
 * sizeof each entry == 0x58
 * Address 0x8007A150.
*/
struct Scorch *g_ScorchBuffer;

/**
 * g_BulletImpactBuffer = mempAllocBytesInBank(0x1F40, MEMPOOL_STAGE);
 * printf("Allocating %d bytes for wallhit data\n",0x3070);
 * Address 0x8007A154.
*/
struct BulletImpact *g_BulletImpactBuffer;

// data
//D:80040170
s32 g_NumExplosionEntries = 0;
//D:80040174
s32 g_NumSmokeEntries = 0;
//D:80040178
f32 g_SpExplosionDamageMult = 1.0;

#if defined(VERSION_EU)
s_smoketype g_SmokeTypes[] = {
   // dur, appr, dis, size, bgrate,    r,  g,    b, fgrate, propclouds
    {   1,   50,  99,    0,   0.0f, 128, 128, 128,   0.3f,     150 },
    { 400,   50,  37,   60,  0.02f,  80,  80,  96,   0.3f,     150 },
    { 400,   50,  42,   20,  0.01f, 128, 128, 128,   0.3f,     150 },
    { 525,   50, 100,  100,  0.01f, 192, 192, 192,   0.3f,     150 },
    { 525,   50,  50,   80,  0.02f,  64,  64,  64,   0.3f,     150 },
    { 640,   50,  42,  190,  0.15f,  64,  64,  64,   0.3f,     150 },
    { 750,   50,  58,  300,  0.01f,  64,  64,  64,   0.3f,     150 },
    {  50,   50,   7,   15,  0.03f, 255, 255, 255,   0.3f,     150 },
    {  17,    1,   5,   30,  0.03f, 255, 255, 255,   2.0f,      25 },
    {  21,    1,   6,   16,  0.03f, 224, 224, 224,   3.0f,      25 },
    { 750,   50,  58,  900,  0.01f,  64,  64,  64,   0.3f,     150 }
};
#else
//D:8004017C
s_smoketype g_SmokeTypes[] = {
   // dur, appr, dis,size, bgrate,   r,  g,    b, fgrate, propclouds
    {   1,   60,  99,   0,   0.0f, 128, 128, 128,   0.3f,     180},
    { 480,   60,  45,  60,  0.02f,  80,  80,  96,   0.3f,     180},
    { 480,   60,  50,  20,  0.01f, 128, 128, 128,   0.3f,     180},
    { 640,   60, 120, 100,  0.01f, 192, 192, 192,   0.3f,     180},
    { 640,   60,  60,  80,  0.02f,  64,  64,  64,   0.3f,     180},
    { 770,   60,  50, 190,  0.15f,  64,  64,  64,   0.3f,     180},
    { 900,   60,  70, 300,  0.01f,  64,  64,  64,   0.3f,     180},
    {  60,   60,   8,  15,  0.03f, 255, 255, 255,   0.3f,     180},
    {  20,    1,   6,  30,  0.03f, 255, 255, 255,   2.0f,      30},
    {  25,    1,   7,  16,  0.03f, 224, 224, 224,   3.0f,      30},
    { 900,   60,  70, 900,  0.01f,  64,  64,  64,   0.3f,     180}
};
#endif

#if defined(VERSION_EU)
s_explosiontype g_ExplosionTypes[] = {
   //hrange, vrange,    hchg,               vchg,           expsize, exprang, dmgrang,   dur, proprate, flarespd, nbits,  bitsize, bitdist, bithvel, bitvvel, smoketype,             sndid, damage
    {  0.1f,   0.1f,    0.0f,               0.0f,                   0.1f,    0.0f,    0.0f,     1,        1,     1.0f,     0,     0.1f,    0.0f,    0.0f,    0.0f,         0,              0x00,   0.0f},
    {  1.0f,   1.0f,    0.0f,               0.0f,                   1.0f,    0.0f,    0.0f,    25,        1,     1.0f,    10,     5.0f,    0.0f,    2.0f,    6.0f,         7,              0x00,   0.0f},
    { 20.0f,  20.0f,    0.0f,               0.0f,                  30.0f,   50.0f,   50.0f,    67,        1,     3.0f,    40,     6.0f,    5.0f,    0.7f,    6.0f,         2,  EXPLOSION_1B_SFX, 0.125f},
    { 50.0f,  50.0f,    0.0f,               0.0f,                  50.0f,  100.0f,  100.0f,    75,        1,     4.0f,    50,     6.0f,   10.0f,    1.0f,    6.0f,         2,  EXPLOSION_1C_SFX,   0.5f},
    { 60.0f,  80.0f,    1.20000004768f,     0.360000014305f,      100.0f,  150.0f,  280.0f,   100,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   1.0f},
    { 60.0f, 120.0f,    1.20000004768f,     0.360000014305f,      150.0f,  200.0f,  310.0f,   100,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 20.0f,  20.0f,    0.0f,               0.0f,                  22.0f,   40.0f,   40.0f,    67,        1,     3.0f,    40,     6.0f,    5.0f,    0.7f,    6.0f,         2,  EXPLOSION_1B_SFX,   0.5f},
    { 35.0f,  40.0f,    0.0f,               0.0f,                  35.0f,   70.0f,   70.0f,    75,        1,     4.0f,    50,     6.0f,   10.0f,    1.0f,    6.0f,         2,  EXPLOSION_1C_SFX,   1.0f},
    { 50.0f,  80.0f,    1.20000004768f,     0.360000014305f,       50.0f,  100.0f,  220.0f,   100,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 60.0f, 120.0f,    1.20000004768f,     0.360000014305f,       50.0f,  130.0f,  230.0f,   100,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 40.0f,  40.0f,    0.5f,               0.239999994635582f,    70.0f,  100.0f,  180.0f,   162,        4,     5.0f,   120,     6.0f,   30.0f,    2.5f,    6.0f,         4,  EXPLOSION_5A_SFX,   1.0f},
    { 50.0f,  50.0f,    0.699999988079071f, 0.5f,                 100.0f,  150.0f,  260.0f,   150,        1,     4.0f,   150,     6.0f,   30.0f,    3.0f,    6.0f,         4,  EXPLOSION_4A_SFX,   2.0f},
    { 70.0f,  60.0f,    1.20000004768372f,  0.699999988079071f,   150.0f,  225.0f,  320.0f,   150,        2,     5.0f,   150,     6.0f,   30.0f,    4.0f,   12.0f,         5,  EXPLOSION_4A_SFX,   4.0f},
    /* standard explosion for grenades and mines */
    { 80.0f,  60.0f,    2.40000009536743f,  0.899999976158142f,   200.0f,  300.0f,  480.0f,   150,        2,     5.0f,   200,     6.0f,   30.0f,    6.0f,   15.0f,         6,  EXPLOSION_4B_SFX,   4.0f},
    { 50.0f,  50.0f,    0.0f,               0.0f,                 120.0f,  200.0f,  400.0f,   125,        4,     4.0f,   150,     6.0f,   30.0f,    3.0f,    6.0f,         4,  EXPLOSION_4B_SFX,   4.0f},
    {  1.0f,   1.0f,    0.0f,               0.0f,                   1.0f,    0.0f,    0.0f,     1,        1,     1.0f,   150,     6.0f,   30.0f,    2.5f,    6.0f,         7,  EXPLOSION_2B_SFX,   0.0f},
    {  1.0f,   1.0f,    0.0f,               0.0f,                   1.0f,    0.0f,    0.0f,     1,        1,     1.0f,   100,     6.0f,   30.0f,    2.5f,    6.0f,         7,  EXPLOSION_2B_SFX,   0.0f},
    { 80.0f,  60.0f,   18.0f,               6.0f,                1500.0f, 2200.0f, 3600.0f,   250,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
    { 80.0f,  60.0f,    3.59999990463257f,  1.20000004768372f,    300.0f,  450.0f,  640.0f,    50,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
    /* facility remote mine */
    { 90.0f,  75.0f,    3.0f,               1.0f,                 250.0f,  375.0f,  600.0f,   150,        2,     5.0f,   200,     6.0f,   30.0f,    6.0f,   15.0f,         6,  EXPLOSION_4B_SFX,   4.0f},
    {160.0f, 120.0f,    7.19999980926514f,  2.40000009536743f,    600.0f,  450.0f,  640.0f,    50,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
};
#else
s_explosiontype g_ExplosionTypes[] = {
   //hrange, vrange,    hchg,  vchg,  expsize, exprang, dmgrang,   dur, proprate, flarespd, nbits,  bitsize, bitdist, bithvel, bitvvel, smoketype,             sndid, damage
    {  0.1f,   0.1f,    0.0f,  0.0f,     0.1f,    0.0f,    0.0f,     1,        1,     1.0f,     0,     0.1f,    0.0f,    0.0f,    0.0f,         0,              0x00,   0.0f},
    {  1.0f,   1.0f,    0.0f,  0.0f,     1.0f,    0.0f,    0.0f,    30,        1,     1.0f,    10,     5.0f,    0.0f,    2.0f,    6.0f,         7,              0x00,   0.0f},
    { 20.0f,  20.0f,    0.0f,  0.0f,    30.0f,   50.0f,   50.0f,    80,        1,     3.0f,    40,     6.0f,    5.0f,    0.7f,    6.0f,         2,  EXPLOSION_1B_SFX, 0.125f},
    { 50.0f,  50.0f,    0.0f,  0.0f,    50.0f,  100.0f,  100.0f,    90,        1,     4.0f,    50,     6.0f,   10.0f,    1.0f,    6.0f,         2,  EXPLOSION_1C_SFX,   0.5f},
    { 60.0f,  80.0f,    1.0f,  0.3f,   100.0f,  150.0f,  280.0f,   120,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   1.0f},
    { 60.0f, 120.0f,    1.0f,  0.3f,   150.0f,  200.0f,  310.0f,   120,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 20.0f,  20.0f,    0.0f,  0.0f,    22.0f,   40.0f,   40.0f,    80,        1,     3.0f,    40,     6.0f,    5.0f,    0.7f,    6.0f,         2,  EXPLOSION_1B_SFX,   0.5f},
    { 35.0f,  40.0f,    0.0f,  0.0f,    35.0f,   70.0f,   70.0f,    90,        1,     4.0f,    50,     6.0f,   10.0f,    1.0f,    6.0f,         2,  EXPLOSION_1C_SFX,   1.0f},
    { 50.0f,  80.0f,    1.0f,  0.3f,    50.0f,  100.0f,  220.0f,   120,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 60.0f, 120.0f,    1.0f,  0.3f,    50.0f,  130.0f,  230.0f,   120,        2,     5.0f,    80,     8.0f,   30.0f,    2.0f,    6.0f,         1,  EXPLOSION_4A_SFX,   2.0f},
    { 40.0f,  40.0f,    0.4f,  0.2f,    70.0f,  100.0f,  180.0f,   170,        4,     5.0f,   120,     6.0f,   30.0f,    2.5f,    6.0f,         4,  EXPLOSION_5A_SFX,   1.0f},
    { 50.0f,  50.0f,    0.6f,  0.4f,   100.0f,  150.0f,  260.0f,   180,        1,     4.0f,   150,     6.0f,   30.0f,    3.0f,    6.0f,         4,  EXPLOSION_4A_SFX,   2.0f},
    { 70.0f,  60.0f,    1.0f,  0.6f,   150.0f,  225.0f,  320.0f,   180,        2,     5.0f,   150,     6.0f,   30.0f,    4.0f,   12.0f,         5,  EXPLOSION_4A_SFX,   4.0f},
    { 80.0f,  60.0f,    2.0f,  0.7f,   200.0f,  300.0f,  480.0f,   180,        2,     5.0f,   200,     6.0f,   30.0f,    6.0f,   15.0f,         6,  EXPLOSION_4B_SFX,   4.0f},
    { 50.0f,  50.0f,    0.0f,  0.0f,   120.0f,  200.0f,  400.0f,   150,        4,     4.0f,   150,     6.0f,   30.0f,    3.0f,    6.0f,         4,  EXPLOSION_4B_SFX,   4.0f},
    {  1.0f,   1.0f,    0.0f,  0.0f,     1.0f,    0.0f,    0.0f,     1,        1,     1.0f,   150,     6.0f,   30.0f,    2.5f,    6.0f,         7,  EXPLOSION_2B_SFX,   0.0f},
    {  1.0f,   1.0f,    0.0f,  0.0f,     1.0f,    0.0f,    0.0f,     1,        1,     1.0f,   100,     6.0f,   30.0f,    2.5f,    6.0f,         7,  EXPLOSION_2B_SFX,   0.0f},
    { 80.0f,  60.0f,   15.0f,  5.0f,  1500.0f, 2200.0f, 3600.0f,   300,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
    { 80.0f,  60.0f,    3.0f,  1.0f,   300.0f,  450.0f,  640.0f,    60,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
    { 90.0f,  75.0f,    2.5f, 0.87f,   250.0f,  375.0f,  600.0f,   180,        2,     5.0f,   200,     6.0f,   30.0f,    6.0f,   15.0f,         6,  EXPLOSION_4B_SFX,   4.0f},
    {160.0f, 120.0f,    6.0f,  2.0f,   600.0f,  450.0f,  640.0f,    60,        1,     2.0f,     0,     0.0f,    0.0f,    0.0f,    0.0f,         0,  EXPLOSION_4B_SFX,   4.0f},
};
#endif

Gfx * g_ExplosionDisplayLists[] = {
    &globalDL_0x078,
    &globalDL_0x120,
    &globalDL_0x1c8,
    &globalDL_0x270,
    &globalDL_0x318,
    &globalDL_0x3c0,
    &globalDL_0x468,
    &globalDL_0x510,
    &globalDL_0x5b8,
    &globalDL_0x660,
    &globalDL_0x708,
    &globalDL_0x7b0,
    &globalDL_0x858,
    &globalDL_0x900,
    &globalDL_0x9a8
};

s32 g_NumParticleEntries = 0;
s32 g_NumScorchEntries = 0;
s32 g_NumImpactEntries = 0;

//D:8004080C
s_impacttype g_ImpactTypes[] = {
    {10.0f, 10.0f, 1, 2, 8},
    { 6.0f,  6.0f, 1, 2, 8},
    { 8.0f,  8.0f, 0, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    { 6.0f,  6.0f, 1, 2, 8},
    { 8.0f,  8.0f, 1, 2, 8},
    {12.0f, 12.0f, 1, 2, 8},
    { 6.0f,  6.0f, 1, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    {20.0f, 20.0f, 1, 2, 8},
    {24.0f, 24.0f, 1, 2, 8},
    { 6.0f,  6.0f, 1, 2, 1},
    { 6.0f,  6.0f, 1, 2, 1},
    {24.0f, 24.0f, 2, 2, 8},
    { 6.0f,  6.0f, 1, 2, 1},
    { 8.0f,  8.0f, 1, 2, 1},
    {12.0f, 12.0f, 1, 2, 1},
};

// unused / unreferenced (padding)
u32 D_800408FC = 0;

Vtx g_ExplosionRenderPartDefaultVertex = {0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };
Vtx g_SmokeRenderPartDefaultVertex = {0, 0, 0, 0, 0, 0, 0x0, 0x0, 0x0, 0x0 };
Vtx g_ScorchDefaultVertex = {0, 0, 0, 0, 0, 0, 0x0, 0x0, 0x0, 0xDC };
Vtx g_BulletImpactDefaultVertex = {0, 0, 0, 0, 0, 0, 0x0, 0x0, 0x0, 0xDC };


// rodata

/*** prototypes */

void explosionInitFlyingParticles(coord3d *spawnpos, f32 spawn_rand_scale, f32 spawn_horiz_drift_scale, f32 spawn_vert_drift_scale, f32 spawn_tex_scale);
s32 explosionRoundFloat(f32 arg0);
void explosionSetBulletImpactAlpha(s32 arg0);
void explosionInflictDamage(PropRecord *arg0, f32 arg1, f32 arg2);
void explosionInflictDamage(struct PropRecord *arg0, f32 arg1, f32 arg2);
void explosionScorchTick(struct coord3d *pos, f32 explosion_size, s16 room);
Gfx *explosionRenderPart(struct ExplosionPart *arg0, Gfx *gdl, struct coord3d *coord);
#ifdef NATIVE_PORT
static Gfx *explosionRenderPartNativeFallback(struct ExplosionPart *arg0,
                                              Gfx *gdl,
                                              struct coord3d *coord,
                                              s32 anim_index);
#endif

/*** *************************************************************************************************************/

/**
 * Named same as Perfect Dark.
 * NTSC 0x7F09C250
*/
#if defined(VERSION_JP) || defined(VERSION_EU)
s32
#else
void
#endif
explosionCreate(PropRecord *arg0, struct coord3d *target_pos, StandTile *target_stan, s16 explosion_type, s32 arg4, s32 player, u8 *rooms, s32 arg7)
{
    s_explosiontype *sp44;
    struct Explosion *sp40;
    f32 sp3C;
    f32 sp38;
    s32 var_v0;
    PropRecord *sp30;
    
    sp44 = &g_ExplosionTypes[explosion_type];
    sp40 = NULL;

#ifdef NATIVE_PORT
    portExplosionTrace(arg0, target_pos, explosion_type, arg4, player, rooms, arg7);
#endif
    
#if defined(VERSION_US)
    if ((explosion_type != 0x10) && (explosion_type != 1))
    {
        g_NumExplosionEntries = 6;
    }
#endif

    for (var_v0 = 0; var_v0 < EXPLOSION_BUFFER_LEN; var_v0++)
    {
        if (g_ExplosionBuffer[var_v0].prop == NULL)
        {
            sp40 = &g_ExplosionBuffer[var_v0];
            break;
        }
    }

    if (sp40 != NULL)
    {
        sp30 = chrpropAllocate();
        
#if defined(VERSION_JP) || defined(VERSION_EU)
        if ((explosion_type != 0x10) && (explosion_type != 1))
        {
            g_NumExplosionEntries = 6;
        }
#endif

        if ((s32) sp44->sndID > 0)
        {
            chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, (s16) sp44->sndID, NULL), target_pos);
        }

        if (sp30 != NULL)
        {
            sp30->type = PROP_TYPE_EXPLOSION;
            sp30->flags |= PROPFLAG_ONSCREEN;
            sp30->explosion = sp40;
            var_v0 = 0;
            sp30->stan = target_stan;
            sp30->pos.f[0] = target_pos->f[0];
            sp30->pos.f[1] = target_pos->f[1];
            sp30->pos.f[2] = target_pos->f[2];

            while (rooms[var_v0] != 0xff && var_v0 < 7)
            {
                sp30->rooms[var_v0] = rooms[var_v0];
                var_v0++;
            }

            sp30->rooms[var_v0] = 0xFF;

            if (arg7 != 0)
            {
                sp30->flags |= PROPFLAG_00000008;
            }

            chrpropActivateThisFrame(sp30);
            chrpropEnable(sp30);
            
            sp40->explosion_type = explosion_type;
            sp40->age = 0;
            sp40->unk3CA = -1;
            sp40->unk3CD = (u8) arg4;
            sp40->prop = sp30;
            sp40->source = arg0;
            sp40->player = (s8) player;

            if (arg4 != 0)
            {
                if ((arg0 != NULL) && (arg0->stan != NULL))
                {
                    sp40->pos.f[0] = arg0->pos.f[0];
                    sp40->pos.f[1] = stanGetPositionYValue(arg0->stan, arg0->pos.f[0], arg0->pos.f[2]) + 4.0f;
                    sp40->pos.f[2] = arg0->pos.f[2];
                    sp40->room = getTileRoom(arg0->stan);
                }
                else
                {
                    sp40->pos.f[0] = target_pos->f[0];
                    sp40->pos.f[1] = stanGetPositionYValue(target_stan, target_pos->f[0], target_pos->f[2]) + 4.0f;
                    sp40->pos.f[2] = target_pos->f[2];
                    sp40->room = getTileRoom(target_stan);
                }
            }
            else
            {
                sp40->pos.f[0] = 999999.9f;
            }

            sp3C = ((RANDOMFRAC() * 0.5f) + 1.0f) * sp44->explosion_size;
            sp38 = RANDOMFRAC() * M_TAU_F;

            sp40->parts[0].size = cosf(sp38) * sp3C;
            sp40->parts[0].rot = sinf(sp38) * sp3C;
            sp40->parts[0].frame = 1;
            sp40->parts[0].pos.f[0] = target_pos->f[0];
            sp40->parts[0].pos.f[1] = target_pos->f[1];
            sp40->parts[0].pos.f[2] = target_pos->f[2];

            for (var_v0 = 0; var_v0 < sp44->numshrapnelbits; var_v0++)
            {
                explosionInitFlyingParticles(target_pos, sp44->shrapnel_scatter_dist, sp44->shrapnel_hvel, sp44->shrapnel_vvel, sp44->shrapnel_size);
            }

            if (getPlayerCount() >= 2)
            {
                for (var_v0 = 0; var_v0 < SMOKE_BUFFER_LEN; var_v0++)
                {
                    if (g_SmokeBuffer[var_v0].prop != NULL)
                    {
                        struct Smoke *smoke = &g_SmokeBuffer[var_v0];

                        if (smoke->smoke_type != 7 && smoke->smoke_type != 8 && smoke->smoke_type != 9)
                        {
                            smoke->duration = g_SmokeTypes[smoke->smoke_type].duration;
                        }
                    }                    
                }
            }
        }
    }

#if defined(VERSION_JP) || defined(VERSION_EU)
    return sp40 != 0;
#endif
}


void setSixExplosionAndSmokeEntries(void) {
        g_NumExplosionEntries = 6;
        g_NumSmokeEntries = 6;
}


void explosionScreenShake(coord3d* source_pos, coord3d* source_mag, coord3d* result)
{
    PropRecord* explosion_prop;
    f32 angle;
    f32 diff_x;
    f32 diff_z;
    f32 mag_scalar_x;
    f32 mag_scalar_z;
    f32 diff_y;
    f32 explosion_mag;

    s32 i;
    f32 dist;
    f32 dist2;

    if (g_NumExplosionEntries == 0)
    {
        viShake(0.0f);
        return;
    }

    angle = 0.8f;
    mag_scalar_x = (cosf(angle) * source_mag->x) - (sinf(angle) * source_mag->f[2]);
    mag_scalar_z = (sinf(angle) * source_mag->x) + (cosf(angle) * source_mag->f[2]);

    explosion_mag = 0.0f;

    for (i = 0; i < EXPLOSION_BUFFER_LEN; i++)
    {
        explosion_prop = g_ExplosionBuffer[i].prop;
        if (explosion_prop != NULL)
        {
            diff_x = explosion_prop->pos.x - source_pos->x;
            diff_y = explosion_prop->pos.y - source_pos->y;
            diff_z = explosion_prop->pos.z - source_pos->z;
#ifndef VERSION_US
            dist = sqrtf((diff_x * diff_x) + (diff_y * diff_y) + (diff_z * diff_z));
            if (dist == 0.0f) { dist = 0.0001f; }
            dist2 = g_ExplosionTypes[g_ExplosionBuffer[i].explosion_type].explosion_size / dist;
#else
            dist = (diff_x * diff_x) + (diff_y * diff_y) + (diff_z * diff_z);
            dist2 = g_ExplosionTypes[g_ExplosionBuffer[i].explosion_type].explosion_size / sqrtf(dist);
#endif
            explosion_mag += dist2 * 15.0f;
        }
    }

#ifdef NATIVE_PORT
    /* Decouple shake from the raised EXPLOSION_BUFFER_LEN (6 -> 16): the loop
     * above sums explosion_mag over every occupied slot, so many simultaneous
     * close blasts would over-shake. Clamp the summed magnitude to roughly the
     * old 6-slot close-range saturation so denser firefights are NOT more
     * violent than vanilla. Applied before the smoke bump and the result
     * assignments so all downstream consumers see the clamped value. */
    {
        const f32 kPortShakeMagMax = 90.0f;
        if (explosion_mag > kPortShakeMagMax)
        {
            explosion_mag = kPortShakeMagMax;
        }
    }
#endif

    if (g_NumSmokeEntries > 0)
    {
        g_NumSmokeEntries--;
        explosion_mag++;
    }

    g_NumExplosionEntries--;
    if (g_NumExplosionEntries & 2)
    {
        result->y = explosion_mag;
        explosion_mag = -explosion_mag;
    }
    else
    {
        result->y = -explosion_mag;
    }

    result->x = explosion_mag * mag_scalar_x;
    result->z = explosion_mag * mag_scalar_z;

    viShake((f32) g_NumExplosionEntries * explosion_mag);
}

/***
 * see Perfect Dark void explosionInflictDamage(struct prop *expprop)
 * Address 0x7F09C9D8 (NTSC)
*/
void explosionInflictDamage(PropRecord *arg0, f32 horiz_range, f32 vert_range)
{
    s32 spE0[8];
    PropRecord *temp_s0;    
    s16 *var_s3;
    s_explosiontype *temp_s6;
    struct Explosion *temp_s2;
#ifdef NATIVE_PORT
    s32 trace_candidates = 0;
    s32 trace_scanned = 0;
    s32 trace_source_or_regen_skips = 0;
    s32 trace_object_candidates = 0;
    s32 trace_object_in_range = 0;
    s32 trace_object_flag_skips = 0;
    s32 trace_object_calls = 0;
    s32 trace_chr_candidates = 0;
    s32 trace_chr_in_range = 0;
    s32 trace_chr_applied = 0;
#endif

    temp_s2 = arg0->explosion;
    temp_s6 = &g_ExplosionTypes[temp_s2->explosion_type];

    if (temp_s2->age >= temp_s2->unk3CA)
    {
        chraiGetPropRoomIds(arg0, &spE0[0]);
        roomGetProps(&spE0[0]);

#ifdef NATIVE_PORT
        trace_candidates = portExplosionDamageTraceCandidateCount();
        portExplosionDamageTracePrintf(
            temp_s2->explosion_type,
            "frame=%d event=begin prop=%p type=%d age=%d next_age=%d "
            "pos=(%.2f,%.2f,%.2f) source=%p source_type=%d source_obj=%d source_pad=%d "
            "prop_rooms=[%d,%d,%d,%d] scan_rooms=[%d,%d,%d,%d] "
            "range=(%.2f,%.2f) base_damage=%.4f candidates=%d",
            g_frame_count_diag,
            (void *)arg0,
            temp_s2->explosion_type,
            temp_s2->age,
            temp_s2->unk3CA,
            arg0->pos.f[0],
            arg0->pos.f[1],
            arg0->pos.f[2],
            (void *)temp_s2->source,
            temp_s2->source != NULL ? temp_s2->source->type : -1,
            portExplosionDamageTracePropObj(temp_s2->source),
            portExplosionDamageTracePropPad(temp_s2->source),
            portExplosionDamageTraceRoomU8(arg0->rooms, 0),
            portExplosionDamageTraceRoomU8(arg0->rooms, 1),
            portExplosionDamageTraceRoomU8(arg0->rooms, 2),
            portExplosionDamageTraceRoomU8(arg0->rooms, 3),
            portExplosionDamageTraceRoomS32(spE0, 0),
            portExplosionDamageTraceRoomS32(spE0, 1),
            portExplosionDamageTraceRoomS32(spE0, 2),
            portExplosionDamageTraceRoomS32(spE0, 3),
            horiz_range,
            vert_range,
            temp_s6->damage,
            trace_candidates);
#endif

        for (var_s3 = ptr_list_object_lookup_indices; *var_s3 >= 0; var_s3++)
        {
            temp_s0 = &pos_data_entry[*var_s3];
#ifdef NATIVE_PORT
            trace_scanned++;
#endif

            if ((temp_s0 != temp_s2->source) && (temp_s0->timetoregen == 0))
            {
                if (temp_s0->type == PROP_TYPE_OBJ || temp_s0->type == PROP_TYPE_WEAPON || temp_s0->type == PROP_TYPE_DOOR)
                {
                    struct ObjectRecord *spCC;
                    f32 xdist;
                    f32 ydist;
                    f32 zdist;
                    s32 in_range;
                    
                    spCC = temp_s0->obj;
                    xdist = spCC->runtime_pos.f[0] - arg0->pos.f[0];
                    ydist = spCC->runtime_pos.f[1] - arg0->pos.f[1];
                    zdist = spCC->runtime_pos.f[2] - arg0->pos.f[2];
                    in_range = ((xdist <= horiz_range)
                        && (-horiz_range <= xdist)
                        && (ydist <= vert_range)
                        && (-vert_range <= ydist)
                        && (zdist <= horiz_range)
                        && (-horiz_range <= zdist));

#ifdef NATIVE_PORT
                    trace_object_candidates++;
#endif

                    if (in_range)
                    {
                        f32 xfrac;
                        f32 yfrac;
                        f32 zfrac;
                        f32 minfrac;
                        f32 falloff;
                        s32 skip_runtime;
                        s32 skip_flags2;
                        
                        xfrac = xdist / horiz_range;
                        yfrac = ydist / vert_range;
                        zfrac = zdist / horiz_range;

                        if (xfrac < 0.0f)
                        {
                            xfrac = -xfrac;
                        }
                        
                        if (yfrac < 0.0f)
                        {
                            yfrac = -yfrac;
                        }

                        if (zfrac < 0.0f)
                        {
                            zfrac = -zfrac;
                        }
                        
                        xfrac = 1.0f - xfrac;
                        yfrac = 1.0f - yfrac;
                        zfrac = 1.0f - zfrac;

                        minfrac = xfrac;

                        if (yfrac < minfrac)
                        {
                            minfrac = yfrac;
                        }
                        
                        if (zfrac < minfrac)
                        {
                            minfrac = zfrac;
                        }

                        falloff = minfrac;
                        minfrac = minfrac * EXPLOSION_DAMAGE_SCALER * temp_s6->damage;
                        skip_runtime = (spCC->runtime_bitflags & 0x1000) != 0;
                        skip_flags2 = (spCC->flags2 & 0x200400) != 0;

#ifdef NATIVE_PORT
                        trace_object_in_range++;
#endif

                        if (!skip_runtime && !skip_flags2)
                        {
                            f32 applied_damage = ((RANDOMFRAC() * 0.5f) + 1.0f) * minfrac;
#ifdef NATIVE_PORT
                            trace_object_calls++;
                            portExplosionDamageTraceObjectCandidate(
                                temp_s2->explosion_type,
                                temp_s0,
                                spCC,
                                xdist,
                                ydist,
                                zdist,
                                horiz_range,
                                vert_range,
                                1,
                                falloff,
                                minfrac,
                                applied_damage,
                                skip_runtime,
                                skip_flags2,
                                1);
#endif
                            maybe_detonate_object_and_its_children(
                                temp_s0,
                                applied_damage,
                                &spCC->runtime_pos,
                                0x1D,
                                (s32) temp_s2->player);
                        }
#ifdef NATIVE_PORT
                        else
                        {
                            trace_object_flag_skips++;
                            portExplosionDamageTraceObjectCandidate(
                                temp_s2->explosion_type,
                                temp_s0,
                                spCC,
                                xdist,
                                ydist,
                                zdist,
                                horiz_range,
                                vert_range,
                                1,
                                falloff,
                                minfrac,
                                0.0f,
                                skip_runtime,
                                skip_flags2,
                                0);
                        }
#endif
                    }
#ifdef NATIVE_PORT
                    else
                    {
                        portExplosionDamageTraceObjectCandidate(
                            temp_s2->explosion_type,
                            temp_s0,
                            spCC,
                            xdist,
                            ydist,
                            zdist,
                            horiz_range,
                            vert_range,
                            0,
                            0.0f,
                            0.0f,
                            0.0f,
                            0,
                            0,
                            0);
                    }
#endif

                }
                else if (temp_s0->type == PROP_TYPE_CHR || temp_s0->type == PROP_TYPE_VIEWER)
                {
                    f32 xdist;
                    f32 ydist;
                    f32 zdist;
                    
                    xdist = temp_s0->pos.f[0] - arg0->pos.f[0];
                    ydist = temp_s0->pos.f[1] - arg0->pos.f[1];
                    zdist = temp_s0->pos.f[2] - arg0->pos.f[2];
#ifdef NATIVE_PORT
                    trace_chr_candidates++;
#endif

                    if ((xdist <= horiz_range)
                        && (-horiz_range <= xdist)
                        && (ydist <= vert_range)
                        && (-vert_range <= ydist)
                        && (zdist <= horiz_range)
                        && (-horiz_range <= zdist))
                    {
                        f32 xfrac;
                        f32 yfrac;
                        f32 zfrac;
                        f32 minfrac;
                        
                        xfrac = xdist / horiz_range;
                        yfrac = ydist / vert_range;
                        zfrac = zdist / horiz_range;

                        if (xfrac < 0.0f)
                        {
                            xfrac = -xfrac;
                        }

                        if (yfrac < 0.0f)
                        {
                            yfrac = -yfrac;
                        }

                        if (zfrac < 0.0f)
                        {
                            zfrac = -zfrac;
                        }
                        
                        xfrac = 1.0f - xfrac;
                        yfrac = 1.0f - yfrac;
                        zfrac = 1.0f - zfrac;

                        minfrac = xfrac;

                        if (yfrac < minfrac)
                        {
                            minfrac = yfrac;
                        }
                        
                        if (zfrac < minfrac)
                        {
                            minfrac = zfrac;
                        }

                        minfrac *= minfrac;
                        minfrac = minfrac * EXPLOSION_DAMAGE_SCALER * temp_s6->damage;
#ifdef NATIVE_PORT
                        trace_chr_in_range++;
#endif

                        if (temp_s0->type == PROP_TYPE_CHR)
                        {
#ifdef NATIVE_PORT
                            if (portShouldProtectStatueRescuedNatalyaFromExplosion(
                                    temp_s0,
                                    temp_s2->explosion_type)) {
                                portExplosionDamageTracePrintf(
                                    temp_s2->explosion_type,
                                    "frame=%d event=chr_protected prop_index=%d prop=%p "
                                    "chrnum=%d type=%d rescued=1 damage=%.4f pos=(%.2f,%.2f,%.2f)",
                                    g_frame_count_diag,
                                    portExplosionDamageTracePropIndex(temp_s0),
                                    (void *)temp_s0,
                                    temp_s0->chr != NULL ? temp_s0->chr->chrnum : -1,
                                    temp_s2->explosion_type,
                                    minfrac,
                                    temp_s0->pos.f[0],
                                    temp_s0->pos.f[1],
                                    temp_s0->pos.f[2]);
                                continue;
                            }

                            trace_chr_applied++;
#endif
                            chrlvExplosionDamage(temp_s0->chr, &arg0->pos, minfrac, 1);
                        }
                        else
                        {
                            s32 sp90;
                            
                            if ((xdist != 0.0f) || (zdist != 0.0f))
                            {
                                f32 temp_f2_3 = sqrtf((xdist * xdist) + (zdist * zdist));
                                xdist *= 1.0f / temp_f2_3;
                                zdist *= 1.0f / temp_f2_3;
                            }
                            
                            sp90 = get_cur_playernum();
                            set_cur_player(getPlayerPointerIndex(temp_s0));
                            
                            if (getPlayerCount() == 1)
                            {
                                minfrac *= g_SpExplosionDamageMult;
                            }
                            
                            if (isBondInTank() == 1)
                            {
                                minfrac *= 2.0f;
                            }
                            
                            record_damage_kills(minfrac, xdist, zdist, (s32) temp_s2->player, 1);
                            set_cur_player(sp90);
#ifdef NATIVE_PORT
                            trace_chr_applied++;
#endif
                        }
                    }
                }
            }
#ifdef NATIVE_PORT
            else
            {
                trace_source_or_regen_skips++;
                if (temp_s0 != NULL &&
                    (temp_s0->type == PROP_TYPE_OBJ ||
                     temp_s0->type == PROP_TYPE_WEAPON ||
                     temp_s0->type == PROP_TYPE_DOOR) &&
                    portTraceExplosionDamage() &&
                    portExplosionDamageTraceObjectMatches(temp_s0->obj)) {
                    portExplosionDamageTracePrintf(
                        temp_s2->explosion_type,
                        "frame=%d event=object_skip prop_index=%d prop=%p prop_type=%d "
                        "obj=%d type=%d pad=%d reason=%s timetoregen=%d source=%p",
                        g_frame_count_diag,
                        portExplosionDamageTracePropIndex(temp_s0),
                        (void *)temp_s0,
                        temp_s0->type,
                        temp_s0->obj != NULL ? temp_s0->obj->obj : -1,
                        temp_s0->obj != NULL ? temp_s0->obj->type : -1,
                        temp_s0->obj != NULL ? temp_s0->obj->pad : -1,
                        temp_s0 == temp_s2->source ? "source" : "regen",
                        temp_s0->timetoregen,
                        (void *)temp_s2->source);
                }
            }
#endif
        }

        temp_s2->unk3CA = temp_s2->age + (temp_s6->duration >> 2);
#ifdef NATIVE_PORT
        portExplosionDamageTracePrintf(
            temp_s2->explosion_type,
            "frame=%d event=end prop=%p type=%d age=%d next_age=%d "
            "scanned=%d candidates=%d source_or_regen_skips=%d "
            "object_candidates=%d object_in_range=%d object_flag_skips=%d object_calls=%d "
            "chr_candidates=%d chr_in_range=%d chr_applied=%d",
            g_frame_count_diag,
            (void *)arg0,
            temp_s2->explosion_type,
            temp_s2->age,
            temp_s2->unk3CA,
            trace_scanned,
            trace_candidates,
            trace_source_or_regen_skips,
            trace_object_candidates,
            trace_object_in_range,
            trace_object_flag_skips,
            trace_object_calls,
            trace_chr_candidates,
            trace_chr_in_range,
            trace_chr_applied);
#endif
    }
#ifdef NATIVE_PORT
    else
    {
        portExplosionDamageTracePrintf(
            temp_s2->explosion_type,
            "frame=%d event=defer prop=%p type=%d age=%d next_age=%d "
            "pos=(%.2f,%.2f,%.2f) range=(%.2f,%.2f)",
            g_frame_count_diag,
            (void *)arg0,
            temp_s2->explosion_type,
            temp_s2->age,
            temp_s2->unk3CA,
            arg0->pos.f[0],
            arg0->pos.f[1],
            arg0->pos.f[2],
            horiz_range,
            vert_range);
    }
#endif
}





/***
 * see Perfect Dark u32 explosionTick(struct prop *prop)
 * 
 * NTSC address 0x7F09CEE8.
*/
s32 explosionTick(PropRecord* arg0)
{
    s32 var_s4;
    s32 j;
    s32 k;
    
    f32 hrange;
    f32 vrange;
    f32 temp_f20;
    f32 temp_f12;
    
    struct Explosion *exp;
    s_explosiontype *explosiontype;
    
    f32 lvupdate;
    s32 sp9C;
    struct coord3d sp90;
    struct coord3d sp84;
    
    
    exp = arg0->explosion;
    explosiontype = &g_ExplosionTypes[exp->explosion_type];
    
    if (g_ClockTimer == 0)
    {
        return 0;
    }
    
    lvupdate = (g_ClockTimer < 15) ? (f32) g_ClockTimer : 15.0f;

    if ((exp->age >= 8) && (exp->age < explosiontype->duration))
    {
        hrange = explosiontype->hrange + (explosiontype->hchange * exp->age);
        vrange = explosiontype->vrange + (explosiontype->vchange * exp->age);

        if (exp->explosion_type == 0xE)
        {
            if (exp->age < 0x20)
            {
                arg0->pos.f[1] += 10.0f * lvupdate;
            }
            
            if (exp->age >= 0x21)
            {
                hrange = (exp->age * 3.0f) + 40.0f;
                
                if (hrange > 300.0f)
                {
                    hrange = 300.0f;
                }
                
                vrange = 20.0f;
            }
        }

        sp9C = (s32) (((f32)explosiontype->propagationrate * (f32)exp->age) / (f32)explosiontype->duration) + 1;
        for (var_s4 = 0; var_s4 < sp9C; var_s4++)
        {
            for (j=0; j<EXPLOSION_PARTS_LEN; j++)
            {
                if (exp->parts[j].frame == 0)
                {
                    exp->parts[j].frame = 1;
    
                    exp->parts[j].pos.f[0] = arg0->pos.f[0] + ((RANDOMFRAC() - 0.5f) * hrange);
                    exp->parts[j].pos.f[1] = arg0->pos.f[1] + ((RANDOMFRAC() - 0.5f) * vrange);
                    exp->parts[j].pos.f[2] = arg0->pos.f[2] + ((RANDOMFRAC() - 0.5f) * hrange);
    
                    temp_f20 = ((RANDOMFRAC() * 0.5f) + 1.0f) * explosiontype->explosion_size;
                    temp_f12 = RANDOMFRAC() * M_TAU_F;
        
                    exp->parts[j].size = cosf(temp_f12) * temp_f20;
                    exp->parts[j].rot = sinf(temp_f12) * temp_f20;

                    break;
                }
            }
        }
        
        // see Perfect Dark void explosionGetBboxAtFrame(struct coord *lower, struct coord *upper, s32 frame, struct prop *prop)

        hrange = (hrange * 0.5f) + explosiontype->explosion_size * 1.5f;
        vrange = (vrange * 0.5f) + explosiontype->explosion_size * 1.5f;
        
        sp90.f[0] = arg0->pos.f[0] - hrange;
        sp90.f[1] = arg0->pos.f[1] - vrange;
        sp90.f[2] = arg0->pos.f[2] - hrange;
        
        sp84.f[0] = arg0->pos.f[0] + hrange;
        sp84.f[1] = arg0->pos.f[1] + vrange;
        sp84.f[2] = arg0->pos.f[2] + hrange;

        // end explosionGetBboxAtFrame.
        
        sub_GAME_7F03E27C(arg0, &sp90, &sp84, hrange);

        vrange = explosiontype->explosion_range + (((explosiontype->dmg_range - explosiontype->explosion_range) * (f32) exp->age) / (f32) explosiontype->duration);
        explosionInflictDamage(arg0, vrange, vrange);
    }
    
    for (k = 0; k < (s32) lvupdate; k++)
    {
        exp->age++;
        
        for (j=0; j<EXPLOSION_PARTS_LEN; j++)
        {
            if (exp->parts[j].frame > 0)
            {
                exp->parts[j].frame++;
            }
        }

        if (((exp->age == 0xF) && (exp->explosion_type == 0xE))
            || (((exp->age + 0x14) == (s16) explosiontype->duration) && (exp->explosion_type != 0xE)))
        {
            if ((exp->source != NULL) && (exp->source->stan != NULL))
            {
                if (exp->source->type == PROP_TYPE_OBJ)
                {
                    struct ObjectRecord *obj = exp->source->obj;
                    explosionCreateSmoke(&obj->runtime_pos, exp->source->stan, (s16) explosiontype->smoketype, exp->source->rooms, (arg0->flags & 8) != 0);
                }
                else
                {
                    explosionCreateSmoke(&exp->source->pos, exp->source->stan, (s16) explosiontype->smoketype, exp->source->rooms, (arg0->flags & 8) != 0);
                }
            }
            else
            {
                explosionCreateSmoke(&arg0->pos, arg0->stan, (s16) explosiontype->smoketype, arg0->rooms, (arg0->flags & 8) != 0);
            }
        }
        
        if ((exp->age == ((s16) explosiontype->duration >> 1)) && (exp->unk3CD != 0))
        {
            explosionScorchTick(&exp->pos, explosiontype->explosion_size * 4.0f, exp->room);
        }
    }

    if (exp->age >= explosiontype->duration + (s32) (16.0f * explosiontype->flareanimspeed))
    {
        exp->prop = NULL;
        return 1;
    }
    
    return 0;
}



/*
* Address: 0x7F09D4EC
*/
u8 explosionChrpropExplosionTick(PropRecord* prop)
{
    Mtxf* player_matrix;

    player_matrix = camGetWorldToScreenMtxf();
    prop->zDepth = -((((player_matrix->m[0][2] * prop->pos.x) + (player_matrix->m[1][2] * prop->pos.y)) + (player_matrix->m[2][2] * prop->pos.z)) + player_matrix->m[3][2]);

    if (prop->zDepth < 100.0f)
    {
        prop->zDepth *= 0.5f;
    }
    else
    {
        prop->zDepth -= 100.0f;
    }

    if (g_ClockTimer == 0)
    {
        return 0;
    }

    return 0;
}


/***
 * Perfect Dark Gfx *explosionRender(struct prop *prop, Gfx *gdl, bool xlupass)
 * 
 * NTSC address 0x7F09D5A0.
*/
Gfx *explosionRenderPropExplosion(PropRecord *prop, Gfx *gdl, s32 withalpha)
{
    s32 temp_s1;
    struct Explosion *temp_s5;
    struct coord3d *temp_s6;
    s32 var_s2;
    
    struct bbox2d sp70;
    
    s32 temp_f10;

    s32 i;
#ifdef NATIVE_PORT
    Gfx *trace_start = gdl;
#endif
    
    temp_s1 = prop->rooms[0];
    temp_s5 = prop->explosion;
    temp_s6 = getRoomPositionByIndex((s32) temp_s1);
    
    if (withalpha == 0)
    {
        return gdl;
    }
    else
    {
        if (sub_GAME_7F054A64(prop, &sp70) > 0)
        {
            gdl = bgScissorCurrentPlayerViewF(gdl, sp70.min.f[0], sp70.min.f[1], sp70.max.f[0], sp70.max.f[1]);
        }
        else
        {
            gdl = bgScissorCurrentPlayerViewDefault(gdl);
        }

        gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_FOG);
        gSPMatrix(gdl++, osVirtualToPhysical((void*)get_BONDdata_field_10E0()),
                  (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION | bondviewGetField10E0GbiFlags()));

        gdl = applyRoomMatrixToDisplayList(gdl, temp_s1);

        gSPSegment(gdl++, SPSEGMENT_GETITLE, osVirtualToPhysical(pGlobalimagetable));

        for (var_s2 = 14;
            var_s2 >= 0;
            var_s2--)
        {
#ifdef NATIVE_PORT
            if (portUseNativeExplosionFallback())
            {
                gDPPipeSync(gdl++);
                gDPSetCycleType(gdl++, G_CYC_1CYCLE);
                gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
                gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
                gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            }
            else
#endif
            {
            gSPDisplayList(gdl++, g_ExplosionDisplayLists[var_s2]);
            }
            
            for (i = 0; i < EXPLOSION_PARTS_LEN; i++)
            {
                if (temp_s5->parts[i].frame > 0
                    && var_s2 == (s32)( (f32)(temp_s5->parts[i].frame - 1) / g_ExplosionTypes[temp_s5->explosion_type].flareanimspeed ) )
                {
#ifdef NATIVE_PORT
                    if (portUseNativeExplosionFallback())
                    {
                        gdl = explosionRenderPartNativeFallback(&temp_s5->parts[i], gdl, temp_s6, var_s2);
                    }
                    else
#endif
                    {
                        gdl = explosionRenderPart(&temp_s5->parts[i], gdl, temp_s6);
                    }
                }
            }
        }

        gSPMatrix(gdl++, osVirtualToPhysical((void*)currentPlayerGetProjectionMatrix()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));

        temp_f10 = (s32) (g_ExplosionTypes[temp_s5->explosion_type].flareanimspeed * 15.0f);

        for (i = 0; i < EXPLOSION_PARTS_LEN; i++)
        {
            if (temp_f10 < temp_s5->parts[i].frame)
            {
                temp_s5->parts[i].frame = 0;
            }
        }
    }

#ifdef NATIVE_PORT
    gfx_register_effect_dl_range("explosion", trace_start, gdl);
#endif
    return gdl;
}

#ifdef NATIVE_PORT
static void explosionNativeSetVertex(Vtx *vertex,
                                     f32 x,
                                     f32 y,
                                     f32 z,
                                     struct coord3d *coord,
                                     u8 r,
                                     u8 g,
                                     u8 b,
                                     u8 a)
{
    vertex->v.ob[0] = (x * get_room_data_float1()) - coord->f[0];
    vertex->v.ob[1] = (y * get_room_data_float1()) - coord->f[1];
    vertex->v.ob[2] = (z * get_room_data_float1()) - coord->f[2];
    vertex->v.tc[0] = 0;
    vertex->v.tc[1] = 0;
    vertex->v.cn[0] = r;
    vertex->v.cn[1] = g;
    vertex->v.cn[2] = b;
    vertex->v.cn[3] = a;
}

static Gfx *explosionRenderPartNativeFallback(struct ExplosionPart *arg0,
                                              Gfx *gdl,
                                              struct coord3d *coord,
                                              s32 anim_index)
{
    Mtxf *mtx;
    struct coord3d *player_pos;
    struct coord3d right;
    struct coord3d up;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 distance;
    f32 radius;
    f32 base_angle;
    f32 ring_angle;
    f32 ring_right;
    f32 ring_up;
    f32 center_x;
    f32 center_y;
    f32 center_z;
    f32 fade;
    u8 center_red;
    u8 center_green;
    u8 center_blue;
    u8 center_alpha;
    u8 edge_red;
    u8 edge_green;
    u8 edge_blue;
    u8 edge_alpha;
    s32 i;
    Vtx *vertices;

    mtx = currentPlayerGetMatrix10D4();
    player_pos = bondviewGetCurrentPlayersPosition();

    dx = arg0->pos.f[0] - player_pos->f[0];
    dy = arg0->pos.f[1] - player_pos->f[1];
    dz = arg0->pos.f[2] - player_pos->f[2];

    distance = sqrtf((dx * dx) + (dy * dy) + (dz * dz));

    radius = sqrtf((arg0->size * arg0->size) + (arg0->rot * arg0->rot)) * 0.6f;
    if (radius < 12.0f)
    {
        radius = 12.0f;
    }
    base_angle = atan2f(arg0->rot, arg0->size);

    center_x = arg0->pos.f[0];
    center_y = arg0->pos.f[1];
    center_z = arg0->pos.f[2];

    right.f[0] = mtx->m[0][0];
    right.f[1] = mtx->m[0][1];
    right.f[2] = mtx->m[0][2];

    up.f[0] = mtx->m[1][0];
    up.f[1] = mtx->m[1][1];
    up.f[2] = mtx->m[1][2];

    fade = anim_index / 14.0f;
    if (fade < 0.0f)
    {
        fade = 0.0f;
    }
    else if (fade > 1.0f)
    {
        fade = 1.0f;
    }

    center_red = 255;
    center_green = (u8)(230.0f - (fade * 95.0f));
    center_blue = (u8)(92.0f - (fade * 64.0f));
    center_alpha = (u8)(215.0f - (fade * 105.0f));
    edge_red = 255;
    edge_green = (u8)(76.0f - (fade * 28.0f));
    edge_blue = 18;
    edge_alpha = (u8)(48.0f - (fade * 22.0f));

    if (center_green < 100)
    {
        center_green = 100;
    }
    if (center_blue < 24)
    {
        center_blue = 24;
    }
    if (center_alpha < 82)
    {
        center_alpha = 82;
    }
    if (edge_green < 36)
    {
        edge_green = 36;
    }
    if (edge_alpha < 16)
    {
        edge_alpha = 16;
    }

    vertices = dynAllocate7F0BD6C4(9);
#ifdef NATIVE_PORT
    if (vertices == NULL)
    {
        return gdl; /* dyn overflow: skip this explosion part */
    }
#endif

    explosionNativeSetVertex(&vertices[0], center_x, center_y, center_z, coord,
                             center_red, center_green, center_blue, center_alpha);

    for (i = 0; i < 8; i++)
    {
        ring_angle = base_angle + ((M_TAU_F / 8.0f) * i);
        ring_right = cosf(ring_angle) * radius;
        ring_up = sinf(ring_angle) * radius;

        explosionNativeSetVertex(&vertices[i + 1],
                                 center_x + (right.f[0] * ring_right) + (up.f[0] * ring_up),
                                 center_y + (right.f[1] * ring_right) + (up.f[1] * ring_up),
                                 center_z + (right.f[2] * ring_right) + (up.f[2] * ring_up),
                                 coord, edge_red, edge_green, edge_blue, edge_alpha);
    }

    if (portTraceExplosions())
    {
        extern int g_frame_count_diag;
        fprintf(stderr,
                "[EXPLOSION_RENDER_TRACE] frame=%d native_fallback=1 anim=%d part_frame=%d "
                "size=%.2f alpha=%u center=(%.2f,%.2f,%.2f) "
                "part=(%.2f,%.2f,%.2f) center_delta=(%.2f,%.2f,%.2f) dist=%.2f\n",
                g_frame_count_diag,
                anim_index,
                arg0->frame,
                radius,
                (unsigned int)center_alpha,
                center_x,
                center_y,
                center_z,
                arg0->pos.f[0],
                arg0->pos.f[1],
                arg0->pos.f[2],
                center_x - arg0->pos.f[0],
                center_y - arg0->pos.f[1],
                center_z - arg0->pos.f[2],
                distance);
        fflush(stderr);
    }

    gSPVertex(gdl++, osVirtualToPhysical(vertices), 9, 0);
    gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSP2Triangles(gdl++, 0, 3, 4, 0, 0, 4, 5, 0);
    gSP2Triangles(gdl++, 0, 5, 6, 0, 0, 6, 7, 0);
    gSP2Triangles(gdl++, 0, 7, 8, 0, 0, 8, 1, 0);

    return gdl;
}
#endif



/***
 * Perfect Dark Gfx *explosionRenderPart(struct explosion *exp, struct explosionpart *part, Gfx *gdl, struct coord *coord, s32 arg4)
 * 
 * NTSC address 0x7F09D82C.
*/
Gfx *explosionRenderPart(struct ExplosionPart *arg0, Gfx *gdl, struct coord3d *coord)
{
    s32 padding1;
    f32 f2;
    
    Vtx spA0;
    Mtxf *sp9C;
    struct coord3d *sp98;
    struct coord3d sp8C;
    struct coord3d sp80;
    struct coord3d sp74;
    struct coord3d sp68;  

    f32 sp64;
    f32 sp60;
    f32 sp5C;
    
    Vtx *vertices;
    
    f32 sp54;
    f32 sp50;
    
    f32 sp4c;
    f32 sp48;
    f32 sp44;
    
    f32 temp_f0;
    f32 var_f12;

    spA0 = g_ExplosionRenderPartDefaultVertex;

    sp9C = currentPlayerGetMatrix10D4();
    sp98 = bondviewGetCurrentPlayersPosition();

    sp64 = arg0->pos.f[0] - sp98->f[0];
    sp60 = arg0->pos.f[1] - sp98->f[1];
    sp5C = arg0->pos.f[2] - sp98->f[2];

    temp_f0 = sqrtf((sp64 * sp64) + (sp60 * sp60) + (sp5C * sp5C));
#ifdef NATIVE_PORT
    if (portExplosionPartFade())
    {
        var_f12 = temp_f0 * 0.5f;
        if (var_f12 > 100.0f)
        {
            var_f12 = 100.0f;
        }
        f2 = (temp_f0 == 0.0f) ? 0.0f : (temp_f0 - var_f12) / temp_f0;
    }
    else
    {
        f2 = 1.0f;
    }
#else
    var_f12 = temp_f0 * 0.5f;
    if (var_f12 > 100.0f)
    {
        var_f12 = 100.0f;
    }

    if (temp_f0 == 0)
    {
        f2 = 0.0f;
    }
    else
    {
        f2 = (temp_f0 - var_f12) / temp_f0;
    }
#endif

    sp54 = arg0->size * f2;
    sp50 = arg0->rot * f2;

#ifdef NATIVE_PORT
    if (portExplosionPartFade())
    {
        sp4c = sp98->f[0] + (sp64 * f2);
        sp48 = sp98->f[1] + (sp60 * f2);
        sp44 = sp98->f[2] + (sp5C * f2);
    }
    else
    {
        sp4c = arg0->pos.f[0];
        sp48 = arg0->pos.f[1];
        sp44 = arg0->pos.f[2];
    }
#else
    sp4c = sp98->f[0] + (sp64 * f2);
    sp48 = sp98->f[1] + (sp60 * f2);
    sp44 = sp98->f[2] + (sp5C * f2);
#endif

    vertices = dynAllocate7F0BD6C4(4);
#ifdef NATIVE_PORT
    if (vertices == NULL)
    {
        return gdl; /* dyn overflow: skip this explosion part */
    }
#endif

    vertices[0] = spA0;
    vertices[1] = spA0;
    vertices[2] = spA0;
    vertices[3] = spA0;

    sp8C.f[0] = sp9C->m[0][0] * sp54;
    sp8C.f[1] = sp9C->m[0][1] * sp54;
    sp8C.f[2] = sp9C->m[0][2] * sp54;

    sp80.f[0] = sp9C->m[0][0] * sp50;
    sp80.f[1] = sp9C->m[0][1] * sp50;
    sp80.f[2] = sp9C->m[0][2] * sp50;

    sp74.f[0] = sp9C->m[1][0] * sp54;
    sp74.f[1] = sp9C->m[1][1] * sp54;
    sp74.f[2] = sp9C->m[1][2] * sp54;

    sp68.f[0] = sp9C->m[1][0] * sp50;
    sp68.f[1] = sp9C->m[1][1] * sp50;
    sp68.f[2] = sp9C->m[1][2] * sp50;

    vertices[0].v.ob[0] = (sp4c - sp8C.f[0] - sp68.f[0]) * get_room_data_float1() - coord->f[0];
	vertices[0].v.ob[1] = (sp48 - sp8C.f[1] - sp68.f[1]) * get_room_data_float1() - coord->f[1];
	vertices[0].v.ob[2] = (sp44 - sp8C.f[2] - sp68.f[2]) * get_room_data_float1() - coord->f[2];
	vertices[0].v.tc[0] = 1760;
	vertices[0].v.tc[1] = 0;

	vertices[1].v.ob[0] = (sp4c + sp80.f[0] - sp74.f[0]) * get_room_data_float1() - coord->f[0];
	vertices[1].v.ob[1] = (sp48 + sp80.f[1] - sp74.f[1]) * get_room_data_float1() - coord->f[1];
	vertices[1].v.ob[2] = (sp44 + sp80.f[2] - sp74.f[2]) * get_room_data_float1() - coord->f[2];
	vertices[1].v.tc[0] = 0;
	vertices[1].v.tc[1] = 0;

	vertices[2].v.ob[0] = (sp4c + sp8C.f[0] + sp68.f[0]) * get_room_data_float1() - coord->f[0];
	vertices[2].v.ob[1] = (sp48 + sp8C.f[1] + sp68.f[1]) * get_room_data_float1() - coord->f[1];
	vertices[2].v.ob[2] = (sp44 + sp8C.f[2] + sp68.f[2]) * get_room_data_float1() - coord->f[2];
	vertices[2].v.tc[0] = 0;
	vertices[2].v.tc[1] = 1760;

	vertices[3].v.ob[0] = (sp4c - sp80.f[0] + sp74.f[0]) * get_room_data_float1() - coord->f[0];
	vertices[3].v.ob[1] = (sp48 - sp80.f[1] + sp74.f[1]) * get_room_data_float1() - coord->f[1];
	vertices[3].v.ob[2] = (sp44 - sp80.f[2] + sp74.f[2]) * get_room_data_float1() - coord->f[2];
	vertices[3].v.tc[0] = 1760;
	vertices[3].v.tc[1] = 1760;

#ifdef NATIVE_PORT
    if (portTraceExplosions())
    {
        extern int g_frame_count_diag;
        fprintf(stderr,
                "[EXPLOSION_RENDER_TRACE] frame=%d original_textures=1 part_frame=%d "
                "size=(%.2f,%.2f) center=(%.2f,%.2f,%.2f) "
                "part=(%.2f,%.2f,%.2f) center_delta=(%.2f,%.2f,%.2f) dist=%.2f\n",
                g_frame_count_diag,
                arg0->frame,
                sp54,
                sp50,
                sp4c,
                sp48,
                sp44,
                arg0->pos.f[0],
                arg0->pos.f[1],
                arg0->pos.f[2],
                sp4c - arg0->pos.f[0],
                sp48 - arg0->pos.f[1],
                sp44 - arg0->pos.f[2],
                temp_f0);
        fflush(stderr);
    }
#endif

    gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);

	gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);

	return gdl;
}



/***
 * Perfect Dark Gfx *smokeRenderPart(struct smoke *smoke, struct smokepart *part, Gfx *gdl, struct coord *coord, f32 size)
 * 
 * NTSC address 0x7F09DDA4.
*/
Gfx *explosionSmokeRenderPart(struct Smoke *smoke, struct SmokePart *smoke_part, Gfx *gdl, struct coord3d *arg3)
{
    Vtx *vertices;
    Vtx spC0;
    Mtxf *mtx;
    struct coord3d spB0;
    struct coord3d spA4;
    struct coord3d sp98;
    struct coord3d sp8C;
    f32 sp88;
    f32 sp84;
    f32 sp80;
    f32 sp7C;
    f32 sp78;
    u8 sp77;
    struct coord3d *sp70;
    f32 sp6C;
    f32 sp68;
    f32 sp64;
    f32 temp_f0;
    f32 range;
    f32 mult;
    f32 sp54;
    f32 sp50;
    f32 sp4C;
    
    spC0 = g_SmokeRenderPartDefaultVertex;

    mtx = currentPlayerGetMatrix10D4();
    sp70 = bondviewGetCurrentPlayersPosition();

    if (g_SmokeTypes[smoke->smoke_type].rateappear >= smoke_part->count)
    {
        sp77 = (smoke_part->alpha / (f32) g_SmokeTypes[smoke->smoke_type].rateappear) * smoke_part->count;
    }
    else
    {
        sp77 = smoke_part->alpha;
    }

    vertices = dynAllocate7F0BD6C4(4);
#ifdef NATIVE_PORT
    if (vertices == NULL)
    {
        return gdl; /* dyn overflow: skip this smoke part */
    }
#endif

    vertices[0] = spC0;
    vertices[1] = spC0;
    vertices[2] = spC0;
    vertices[3] = spC0;

    sp88 = cosf(smoke_part->rot) * smoke_part->size;
    sp84 = sinf(smoke_part->rot) * smoke_part->size;

    sp80 = smoke_part->pos.f[0] + (sinf(smoke_part->offset1) * 7.0f);
    sp7C = smoke_part->pos.f[1];
    sp78 = smoke_part->pos.f[2] + (sinf(smoke_part->offset2) * 7.0f);

    sp6C = sp80 - sp70->f[0];
    sp68 = sp7C - sp70->f[1];
    sp64 = sp78 - sp70->f[2];

    temp_f0 = sqrtf((sp6C * sp6C) + (sp68 * sp68) + (sp64 * sp64));

    if (temp_f0 > 30000.0f)
    {
        return gdl;
    }

    range = temp_f0 * 0.5f;

	if (range > 100.0f) {
		range = 100.0f;
	}

	if (temp_f0 == 0.0f) {
		mult = 0.0f;
	} else {
		mult = (temp_f0 - range) / temp_f0;
	}

	sp88 = sp88 * mult;
	sp84 = sp84 * mult;

	sp80 = sp70->f[0] + sp6C * mult;
	sp7C = sp70->f[1] + sp68 * mult;
	sp78 = sp70->f[2] + sp64 * mult;

	spB0.f[0] = mtx->m[0][0] * sp88;
	spB0.f[1] = mtx->m[0][1] * sp88;
	spB0.f[2] = mtx->m[0][2] * sp88;
	spA4.f[0] = mtx->m[0][0] * sp84;
	spA4.f[1] = mtx->m[0][1] * sp84;
	spA4.f[2] = mtx->m[0][2] * sp84;
	sp98.f[0] = mtx->m[1][0] * sp88;
	sp98.f[1] = mtx->m[1][1] * sp88;
	sp98.f[2] = mtx->m[1][2] * sp88;
	sp8C.f[0] = mtx->m[1][0] * sp84;
	sp8C.f[1] = mtx->m[1][1] * sp84;
	sp8C.f[2] = mtx->m[1][2] * sp84;

    sp54 = ((sp80 - spB0.f[0] - sp8C.f[0]) * get_room_data_float1() - arg3->f[0]) * 10.0f;
	sp50 = ((sp7C - spB0.f[1] - sp8C.f[1]) * get_room_data_float1() - arg3->f[1]) * 10.0f;
	sp4C = ((sp78 - spB0.f[2] - sp8C.f[2]) * get_room_data_float1() - arg3->f[2]) * 10.0f;

    if (sp54 > 30000.0f
        || sp54 < -30000.0f
        || sp50 > 30000.0f
        || sp50 < -30000.0f
        || sp4C > 30000.0f
        || sp4C < -30000.0f)
    {
		return gdl;
	}

    vertices[0].v.ob[0] = sp54;
	vertices[0].v.ob[1] = sp50;
	vertices[0].v.ob[2] = sp4C;
	vertices[0].v.tc[0] = 1760;
	vertices[0].v.tc[1] = 0;
    vertices[0].v.cn[0] = g_SmokeTypes[smoke->smoke_type].r;
    vertices[0].v.cn[1] = g_SmokeTypes[smoke->smoke_type].g;
    vertices[0].v.cn[2] = g_SmokeTypes[smoke->smoke_type].b;
    vertices[0].v.cn[3] = sp77;

    vertices[1].v.ob[0] = ((((sp80 + spA4.f[0]) - sp98.f[0]) * get_room_data_float1()) - arg3->f[0]) * 10.0f;
    vertices[1].v.ob[1] = ((((sp7C + spA4.f[1]) - sp98.f[1]) * get_room_data_float1()) - arg3->f[1]) * 10.0f;
    vertices[1].v.ob[2] = ((((sp78 + spA4.f[2]) - sp98.f[2]) * get_room_data_float1()) - arg3->f[2]) * 10.0f;
    vertices[1].v.tc[0] = 0;
    vertices[1].v.tc[1] = 0;
    vertices[1].v.cn[0] = g_SmokeTypes[smoke->smoke_type].r;
    vertices[1].v.cn[1] = g_SmokeTypes[smoke->smoke_type].g;
    vertices[1].v.cn[2] = g_SmokeTypes[smoke->smoke_type].b;
    vertices[1].v.cn[3] = sp77;
    
    vertices[2].v.ob[0] = (((sp80 + spB0.f[0] + sp8C.f[0]) * get_room_data_float1()) - arg3->f[0]) * 10.0f;
    vertices[2].v.ob[1] = (((sp7C + spB0.f[1] + sp8C.f[1]) * get_room_data_float1()) - arg3->f[1]) * 10.0f;
    vertices[2].v.ob[2] = (((sp78 + spB0.f[2] + sp8C.f[2]) * get_room_data_float1()) - arg3->f[2]) * 10.0f;
    vertices[2].v.tc[0] = 0;
    vertices[2].v.tc[1] = 1760;
    vertices[2].v.cn[0] = g_SmokeTypes[smoke->smoke_type].r;
    vertices[2].v.cn[1] = g_SmokeTypes[smoke->smoke_type].g;
    vertices[2].v.cn[2] = g_SmokeTypes[smoke->smoke_type].b;
    vertices[2].v.cn[3] = sp77;
    
    vertices[3].v.ob[0] = ((((sp80 - spA4.f[0]) + sp98.f[0]) * get_room_data_float1()) - arg3->f[0]) * 10.0f;
    vertices[3].v.ob[1] = ((((sp7C - spA4.f[1]) + sp98.f[1]) * get_room_data_float1()) - arg3->f[1]) * 10.0f;
    vertices[3].v.ob[2] = ((((sp78 - spA4.f[2]) + sp98.f[2]) * get_room_data_float1()) - arg3->f[2]) * 10.0f;
    vertices[3].v.tc[0] = 1760;
    vertices[3].v.tc[1] = 1760;
    vertices[3].v.cn[0] = g_SmokeTypes[smoke->smoke_type].r;
    vertices[3].v.cn[1] = g_SmokeTypes[smoke->smoke_type].g;
    vertices[3].v.cn[2] = g_SmokeTypes[smoke->smoke_type].b;
    vertices[3].v.cn[3] = sp77;

    gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);

	gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);

	return gdl;
}


void explosionCreateSmoke(coord3d *pos, StandTile *stan, s16 smoke_type, u8 *rooms, s32 flags)
{
    struct Smoke *smoke;
    struct Smoke *smoke_tmp;
    s32 i;
    s32 player_count;
    PropRecord *prop;

    smoke = NULL;
    player_count = getPlayerCount();

    for (i = 0; i < 20; i++)
    {
        if (g_SmokeBuffer[i].prop == NULL)
        {
            smoke = &g_SmokeBuffer[i];
            break;
        }
        else if (player_count >= 2)
        {
            smoke_tmp = (i + g_SmokeBuffer);
            if (((smoke_tmp->smoke_type != 7) && (smoke_tmp->smoke_type != 8)) && (smoke_tmp->smoke_type != 9))
            {
                smoke_tmp->duration = (s16) g_SmokeTypes[smoke_tmp->smoke_type].duration;
            }
        }
    }

    if (smoke == NULL) { return; }

    prop = chrpropAllocate();
    if (prop == NULL) { return; }

    prop->type = 8;
    prop->flags |= 2;
    prop->smoke = smoke;
    prop->stan = stan;
    prop->pos.x = pos->x;
    prop->pos.y = pos->y;
    prop->pos.z = pos->z;

    for (i = 0; (rooms[i] != 0xFF) && (i < 7); i++)
    {
        prop->rooms[i] = rooms[i];
    }
    prop->rooms[i] = 0xFF;

    if (flags != 0)
    {
        prop->flags |= 8;
    }

    chrpropActivateThisFrame(prop);
    chrpropEnable(prop);
    smoke->prop = prop;
    smoke->duration = 0;
    smoke->smoke_type = smoke_type;
}


/***
 * Perfect Dark u32 smokeTick(struct prop *prop)
 * 
 * NTSC address 0x7F09E8AC.
*/
s32 explosionSmokeTick(PropRecord *arg0)
{
    f32 temp_f2;
    s32 i;
    s32 j;
    s32 k;
    f32 lvupdate;
    struct Smoke *smoke;
    struct SmokePart *part;
    struct coord3d bbmin;
	struct coord3d bbmax;
    f32 var_f14;
    s32 var_v1;
    
    smoke = arg0->smoke;

	if (g_ClockTimer == 0)
    {
		return 0;
	}

    lvupdate = (g_ClockTimer < 15) ? (f32) g_ClockTimer : 15.0f;

    for (i = 0; i < (s32) lvupdate; i++)
    {
        smoke->duration += 1;

		for (j = 0; j < SMOKE_PARTS_LEN; j++)
        {
            part = &smoke->parts[j];
            
			if (part->size != 0.0f)
            {                
				part->pos.f[1] += 0.3f;
                part->size += 0.15f;

                part->alpha -= g_SmokeTypes[smoke->smoke_type].fg_rotrate;
                part->count++;
                part->rot += part->deltarot;

                part->offset1 += 0.02f + RANDOMFRAC() * 0.01f;
				part->offset2 += 0.02f + RANDOMFRAC() * 0.01f;

                if (part->alpha < 4.0f)
                {
                    part->size = 0.0f;
                }
			}
		}

        if (smoke->duration < g_SmokeTypes[smoke->smoke_type].duration)
        {
            if (smoke->duration % g_SmokeTypes[smoke->smoke_type].ratedissolve == 1)
            {
        		for (j = 0; j < SMOKE_PARTS_LEN; j++)
                {
                    if (smoke->parts[j].size == 0.0f)
                    {
                        part = &smoke->parts[j];
                        
                        part->size = g_SmokeTypes[smoke->smoke_type].size * (RANDOMFRAC() * 0.5f + 1.0f);
                        part->alpha = (randomGetNext() % 70) + 110.0f;
						part->count = 0;
						part->rot = RANDOMFRAC() * M_TAU_F;
						part->deltarot = (0.5f - RANDOMFRAC()) * g_SmokeTypes[smoke->smoke_type].bg_rotrate;

						part->pos.x = arg0->pos.x;
                        part->pos.y = arg0->pos.y;
                        part->pos.z = arg0->pos.z;

						part->offset1 = RANDOMFRAC() * 0.5f;
						part->offset2 = RANDOMFRAC() * 0.5f;

						if (smoke->duration > g_SmokeTypes[smoke->smoke_type].duration - g_SmokeTypes[smoke->smoke_type].propagated_clouds)
                        {
							part->alpha *= (g_SmokeTypes[smoke->smoke_type].duration - smoke->duration) / (f32)g_SmokeTypes[smoke->smoke_type].propagated_clouds;
						}
                        
						break;
                    }
                }
            }
        }
    }

    bbmin.x = arg0->pos.x - 1.0f;
	bbmin.y = arg0->pos.y - 1.0f;
	bbmin.z = arg0->pos.z - 1.0f;

	bbmax.x = arg0->pos.x + 1.0f;
	bbmax.y = arg0->pos.y + 1.0f;
	bbmax.z = arg0->pos.z + 1.0f;

    var_f14 = 0.0f;

    for (j = 0; j < SMOKE_PARTS_LEN; j++)
    {
		if (smoke->parts[j].size != 0.0f)
        {
			for (k = 0; k < 3; k++)
            {
				if (bbmin.f[k] > smoke->parts[j].pos.f[k] - smoke->parts[j].size)
                {
					bbmin.f[k] = smoke->parts[j].pos.f[k] - smoke->parts[j].size;
				}
                else if (bbmax.f[k] < smoke->parts[j].pos.f[k] - smoke->parts[j].size)
                {
					bbmax.f[k] = smoke->parts[j].pos.f[k] - smoke->parts[j].size;
				}
			}
		}
	}

    temp_f2 = arg0->pos.f[0] - bbmin.f[0];
    if (temp_f2 > 0.0f)
    {
        var_f14 = temp_f2;
    }
        
    if (var_f14 < arg0->pos.f[2] - bbmin.f[2])
    {
        var_f14 = arg0->pos.f[2] - bbmin.f[2];
    }
    
    if (var_f14 < bbmax.f[0] - arg0->pos.f[0])
    {
        var_f14 = bbmax.f[0] - arg0->pos.f[0];
    }
    
    if (var_f14 < bbmax.f[2] - arg0->pos.f[2])
    {
        var_f14 = bbmax.f[2] - arg0->pos.f[2];
    }
    
    sub_GAME_7F03E27C(arg0, &bbmin, &bbmax, var_f14);

    if (smoke->duration > g_SmokeTypes[smoke->smoke_type].ratedissolve)
    {
		var_v1 = 1;

		for (j = 0; j < SMOKE_PARTS_LEN; j++)
        {
			if (smoke->parts[j].size > 0.0f)
            {
				var_v1 = 0;
				break;
			}
		}
    }
    else
    {
		var_v1 = 0;
	}
    
    if (var_v1 != 0)
    {
        smoke->prop = NULL;
        return 1;
    }
    
    return 0;
}


/*
* Address: 0x7F09EF9C
*/
u8 explosionChrpropSmokeTick(PropRecord* prop)
{
    Mtxf* player_matrix;

    player_matrix = camGetWorldToScreenMtxf();
    prop->zDepth = -((((player_matrix->m[0][2] * prop->pos.x) + (player_matrix->m[1][2] * prop->pos.y)) + (player_matrix->m[2][2] * prop->pos.z)) + player_matrix->m[3][2]);

    if (prop->zDepth < 100.0f)
    {
        prop->zDepth *= 0.5f;
    }
    else
    {
        prop->zDepth -= 100.0f;
    }

    return 0;
}


extern Gfx globalDL_0x000;

/***
 * NTSC address 0x7F09F03C.
*/
Gfx *explosionRenderPropSmoke(PropRecord *arg0, Gfx *gdl, s32 withalpha)
{
    struct Smoke *smoke;
    s32 i;
    struct bbox2d sp78;
    struct coord3d *temp_s5;
    s32 temp_s1;
#ifdef NATIVE_PORT
    Gfx *trace_start = gdl;
#endif


    temp_s1 = arg0->rooms[0];
    smoke = arg0->smoke;
    temp_s5 = getRoomPositionByIndex(temp_s1);
    
    if (withalpha == 0)
    {
        return gdl;
    }

    if (sub_GAME_7F054A64(arg0, &sp78) > 0)
    {
        gdl = bgScissorCurrentPlayerViewF(gdl, sp78.min.f[0], sp78.min.f[1], sp78.max.f[0], sp78.max.f[1]);
    }
    else
    {
        gdl = bgScissorCurrentPlayerViewDefault(gdl);
    }

    gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_FOG);

    gSPMatrix(gdl++, osVirtualToPhysical((void*)get_BONDdata_field_10E0()),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION | bondviewGetField10E0GbiFlags());

    gdl = applyRoomMatrixToDisplayList(gdl, temp_s1);

    gSPMatrix(gdl++, osVirtualToPhysical((void*)&dword_CODE_bss_8007A100), G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);

    gSPSegment(gdl++, SPSEGMENT_GETITLE, osVirtualToPhysical((void*)pGlobalimagetable));

    gSPDisplayList(gdl++, &globalDL_0x000);

    gDPSetColorDither(gdl++, G_CD_NOISE);

    for (i = 0; i < SMOKE_PARTS_LEN; i++)
    {
        if (smoke->parts[i].size > 0.0f)
        {
            gdl = explosionSmokeRenderPart(smoke, &smoke->parts[i], gdl, temp_s5);
        }
        else
        {
            smoke->parts[i].size = 0.0f;
        }
    }

    gDPSetColorDither(gdl++, G_CD_BAYER);

    gSPMatrix(gdl++, osVirtualToPhysical((void*)currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

#ifdef NATIVE_PORT
    gfx_register_effect_dl_range("smoke", trace_start, gdl);
#endif
    return gdl;
}


// https://decomp.me/scratch/RT8eu
void explosionInitFlyingParticles(coord3d *spawnpos, f32 spawn_rand_scale, f32 spawn_horiz_drift_scale, f32 spawn_vert_drift_scale, f32 spawn_tex_scale)
{
    // these are gray rectangles of dust created from shooting walls with guns that fall down with gravity
    // a bullet will create a group of them flying off the wall

    f32 rand1;
    f32 rand2;
    f32 rand3;
    s16 unk08_upper;
    s16 unk0A_upper;
    s32 rand_s8;

    rand1 = (2.0f * (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX))) - 1.0f;
    rand2 = ((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 1.12f) - 0.12f;
    rand3 = (2.0f * (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX))) - 1.0f;

    g_FlyingParticlesBuffer[g_NumParticleEntries].unk00 = 1;

    g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[0] = spawnpos->f[0] + (spawn_rand_scale * rand1);
    g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[1] = spawnpos->f[1] + (spawn_rand_scale * rand2);
    g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[2] = spawnpos->f[2] + (spawn_rand_scale * rand3);

    g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[0] = rand1 * spawn_horiz_drift_scale;
    g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[1] = rand2 * spawn_vert_drift_scale;
    g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[2] = rand3 * spawn_horiz_drift_scale;

    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.ob[0] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * spawn_tex_scale));
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.ob[1] = 0;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.ob[2] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * spawn_tex_scale));

    if (1)
    {
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.ob[0] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * spawn_tex_scale));
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.ob[1] = 0;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.ob[2] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * (-spawn_tex_scale)));

        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.ob[0] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * (-spawn_tex_scale)));
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.ob[1] = 0;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.ob[2] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * (-spawn_tex_scale)));

        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.ob[0] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * (-spawn_tex_scale)));
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.ob[1] = 0;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.ob[2] = (s16) ((s32) ((((((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.75f) + 0.75f) * spawn_tex_scale));
    }

    if (1) {}

    unk08_upper = (randomGetNext() & 3) << 8;
    unk0A_upper = (randomGetNext() & 3) << 8;

    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.tc[0] = unk08_upper + 0xE0;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.tc[1] = unk0A_upper + 0xE0;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.tc[0] = unk08_upper + 0xE0;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.tc[1] = unk0A_upper;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.tc[0] = unk08_upper;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.tc[1] = unk0A_upper;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.tc[0] = unk08_upper;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.tc[1] = unk0A_upper + 0xE0;

    if (randomGetNext() & 1)
    {
        rand_s8 = 0xFF - (randomGetNext() & 0x3F);
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[0] = rand_s8;
        rand_s8 = 0xFF - (randomGetNext() & 0x3F);
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[0] = rand_s8;
        rand_s8 = 0xFF - (randomGetNext() & 0x3F);
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[0] = rand_s8;
        rand_s8 = 0xFF - (randomGetNext() & 0x3F);
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[0] = rand_s8;
    }
    else
    {
        rand_s8 = randomGetNext() & 0x3F;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[0] = rand_s8;
        rand_s8 = randomGetNext() & 0x3F;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[0] = rand_s8;
        rand_s8 = randomGetNext() & 0x3F;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[0] = rand_s8;
        rand_s8 = randomGetNext() & 0x3F;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[2] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[1] = rand_s8;
        g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[0] = rand_s8;
    }

    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[0].v.cn[3] = 0xdc;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[1].v.cn[3] = 0xdc;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[2].v.cn[3] = 0xdc;
    g_FlyingParticlesBuffer[g_NumParticleEntries].vertex_list[3].v.cn[3] = 0xdc;

    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation.f[0] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * M_TAU_F;
    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation.f[1] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * M_TAU_F;
    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation.f[2] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * M_TAU_F;

    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation_drift.f[0] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.1f;
    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation_drift.f[1] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.1f;
    g_FlyingParticlesBuffer[g_NumParticleEntries].rotation_drift.f[2] = (((f32) randomGetNext()) * (1.0f / (f32)UINT_MAX)) * 0.1f;

#ifdef NATIVE_PORT
    if (portTraceExplosions())
    {
        fprintf(stderr,
                "[FLYING-PARTICLE-CREATE] slot=%d pos=(%.2f,%.2f,%.2f) drift=(%.2f,%.2f,%.2f) tex_scale=%.2f\n",
                g_NumParticleEntries,
                g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[0],
                g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[1],
                g_FlyingParticlesBuffer[g_NumParticleEntries].position.f[2],
                g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[0],
                g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[1],
                g_FlyingParticlesBuffer[g_NumParticleEntries].position_drift.f[2],
                spawn_tex_scale);
        fflush(stderr);
    }
#endif

    g_NumParticleEntries++;
    if (g_NumParticleEntries >= max_particles)
    {
        g_NumParticleEntries = 0;
    }
}


void explosionUpdateFlyingParticles(void)
{
    f32 scalar;
    s32 i;
    s32 j;

    if (g_ClockTimer < 0xF)
    {
        scalar = g_ClockTimer;
    }
    else
    {
        scalar = 15.0f;
    }


    for (i = 0; i < max_particles; i++)
    {
        if (g_FlyingParticlesBuffer[i].unk00 > 0)
        {
            g_FlyingParticlesBuffer[i].unk00 += (s32) scalar;

            g_FlyingParticlesBuffer[i].rotation.f[0] += g_FlyingParticlesBuffer[i].rotation_drift.f[0] * scalar;
            g_FlyingParticlesBuffer[i].rotation.f[1] += g_FlyingParticlesBuffer[i].rotation_drift.f[1] * scalar;
            g_FlyingParticlesBuffer[i].rotation.f[2] += g_FlyingParticlesBuffer[i].rotation_drift.f[2] * scalar;
            
            g_FlyingParticlesBuffer[i].position.f[0] += g_FlyingParticlesBuffer[i].position_drift.f[0] * scalar;
            g_FlyingParticlesBuffer[i].position.f[2] += g_FlyingParticlesBuffer[i].position_drift.f[2] * scalar;

            for (j = 0; j < (s32)scalar; j++)
            {
                // initially sends particles flying up
                g_FlyingParticlesBuffer[i].position.f[1] += g_FlyingParticlesBuffer[i].position_drift.f[1];

                // applies gravity so particles fall down
                if (g_FlyingParticlesBuffer[i].position_drift.f[1] > -3.75f)
                {
                    g_FlyingParticlesBuffer[i].position_drift.f[1] -= 0.2f;
                }
            }

            // handles particles life time
            if ((g_FlyingParticlesBuffer[i].unk00 >= 0x65) && (!(randomGetNext() & 0x1F) || (g_FlyingParticlesBuffer[i].unk00 == 0x12C)))
            {
                g_FlyingParticlesBuffer[i].unk00 = 0;
            }

            // position-related. deletes particles that are too low or too high.
            if ((g_FlyingParticlesBuffer[i].position.f[1] < -30000.0f) || (g_FlyingParticlesBuffer[i].position.f[1] > 30000.0f))
            {
                g_FlyingParticlesBuffer[i].unk00 = 0;
            }
        }
    }
}


extern Gfx globalDL_0xa50;

/***
 * NTSC address 0x7F0A0034.
*/
Gfx *explosionRenderFlyingParticles(Gfx *gdl)
{
    Mtxf sp80;
    s32 i;
    Mtx *temp_v0_2;
    struct FlyingParticles *particles;
    s32 rendered_count = 0;
#ifdef NATIVE_PORT
    Gfx *trace_start = gdl;
#endif

#ifdef NATIVE_PORT
    if (portDisableFlyingParticles())
    {
        return gdl;
    }
#endif
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gSPSegment(gdl++, SPSEGMENT_GETITLE, osVirtualToPhysical((void*)pGlobalimagetable));
    gSPDisplayList(gdl++, &globalDL_0xa50);
    gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);

    for (i = 0; i < max_particles; i++)
    {
        // HACK: regalloc has instructions backwards.
        particles = g_FlyingParticlesBuffer + i;
        
        if (particles->unk00 > 0)
        {
            matrix_4x4_set_position_and_rotation_around_xyz(&particles->position, &particles->rotation, &sp80);
            matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), &sp80);
            bondviewApplyLevelVisibilityScaleToModelView(&sp80);

            if ((sp80.m[3][0] < 20000.0f)
                && (sp80.m[3][0] > -20000.0f)
                && (sp80.m[3][1] < 20000.0f)
                && (sp80.m[3][1] > -20000.0f)
                && (sp80.m[3][2] < 20000.0f)
                && (sp80.m[3][2] > -20000.0f))
            {
                temp_v0_2 = dynAllocateMatrix();
#ifdef NATIVE_PORT
                if (dynIsOverflowMatrix(temp_v0_2))
                {
                    continue; /* dyn overflow: skip this particle, don't alias its matrix */
                }
#endif
                matrix_4x4_f32_to_s32(sp80.m, temp_v0_2->m);

                gSPMatrix(gdl++, osVirtualToPhysical((void*)temp_v0_2), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPVertex(gdl++, osVirtualToPhysical((void*)g_FlyingParticlesBuffer[i].vertex_list), 4, 0);
                gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
                rendered_count++;
            }
        }
    }

#ifdef NATIVE_PORT
    if (portTraceExplosions() && rendered_count > 0)
    {
        fprintf(stderr,
                "[FLYING-PARTICLE-RENDER] rendered=%d max=%d\n",
                rendered_count,
                max_particles);
        fflush(stderr);
	    }
#endif

#ifdef NATIVE_PORT
    if (rendered_count > 0)
    {
        gfx_register_effect_dl_range("flying_particle", trace_start, gdl);
    }
#endif

    return gdl;
}




/***
 * NTSC address 0x7F0A027C.
*/
void explosionScorchTick(struct coord3d *pos, f32 explosion_size, s16 room)
{
    Vtx sp58;
    f32 sp54;
    f32 sp50;
    f32 sp4C;
    u8 sp4B;
    struct coord3d *temp_s0;
    u32 temp_hi;

    sp58 = g_ScorchDefaultVertex;

    sp54 = RANDOMFRAC() * M_TAU_F;
    sp4B = 0xFF - (randomGetNext() % 80U);

    temp_s0 = getRoomPositionByIndex((s32) room);

#ifdef NATIVE_PORT
    /* MP scorch: record marks regardless of player count (buffer is always
     * allocated on PC). GE007_DISABLE_SCORCH_MARKS is the off switch. */
    if (!portDisableScorchMarks())
#else
    if (getPlayerCount() < 2)
#endif
    {
        if (explosion_size > 200.0f)
        {
            explosion_size = 200.0f;
        }

        explosion_size *= (0.8f + (0.2f * RANDOMFRAC()));
        
        pos->f[0] = (pos->f[0] * get_room_data_float1()) - temp_s0->f[0];
        pos->f[1] = (pos->f[1] * get_room_data_float1()) - temp_s0->f[1];
        pos->f[2] = (pos->f[2] * get_room_data_float1()) - temp_s0->f[2];

        explosion_size *= get_room_data_float1();

        sp50 = cosf(sp54) * explosion_size;
        sp4C = sinf(sp54) * explosion_size;

        g_ScorchBuffer[g_NumScorchEntries].roomid = room;
        g_ScorchBuffer[g_NumScorchEntries].pos.f[0] = pos->f[0];
        g_ScorchBuffer[g_NumScorchEntries].pos.f[1] = pos->f[1];
        g_ScorchBuffer[g_NumScorchEntries].pos.f[2] = pos->f[2];
        g_ScorchBuffer[g_NumScorchEntries].explosion_size = explosion_size;
        
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0] = sp58;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1] = sp58;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2] = sp58;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3] = sp58;

        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.ob[0] = (s16) (s32) (pos->f[0] + sp50);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.ob[1] = (s16) (s32) (pos->f[1] + 0.5f);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.ob[2] = (s16) (s32) (pos->f[2] + sp4C);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.tc[0] = 0;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.tc[1] = genericimage->width << 5;
        temp_hi = randomGetNext() % 50U;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.cn[2] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.cn[1] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.cn[0] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[0].v.cn[3] = sp4B;

        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.ob[0] = (s16) (s32) (pos->f[0] + sp4C);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.ob[1] = (s16) (s32) (pos->f[1] + 0.5f);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.ob[2] = (s16) (s32) (pos->f[2] - sp50);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.tc[0] = 0;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.tc[1] = 0;
        temp_hi = randomGetNext() % 50U;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.cn[2] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.cn[1] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.cn[0] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[1].v.cn[3] = sp4B;
        
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.ob[0] = (s16) (s32) (pos->f[0] - sp50);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.ob[1] = (s16) (s32) (pos->f[1] + 0.5f);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.ob[2] = (s16) (s32) (pos->f[2] - sp4C);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.tc[0] = genericimage->height << 5;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.tc[1] = 0;
        temp_hi = randomGetNext() % 50U;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.cn[2] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.cn[1] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.cn[0] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[2].v.cn[3] = sp4B;
        
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.ob[0] = (s16) (s32) (pos->f[0] - sp4C);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.ob[1] = (s16) (s32) (pos->f[1] + 0.5f);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.ob[2] = (s16) (s32) (pos->f[2] + sp50);
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.tc[0] = genericimage->width << 5;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.tc[1] = genericimage->height << 5;
        temp_hi = randomGetNext() % 50U;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.cn[2] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.cn[1] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.cn[0] = (u8) temp_hi;
        g_ScorchBuffer[g_NumScorchEntries].vertex_list[3].v.cn[3] = sp4B;

        g_NumScorchEntries++;
        if (g_NumScorchEntries >= SCORCH_BUFFER_LEN)
        {
            g_NumScorchEntries = 0;
        }

#ifdef NATIVE_PORT
        if (portTraceExplosions())
        {
            s32 traced_slot = g_NumScorchEntries - 1;
            if (traced_slot < 0) {
                traced_slot = SCORCH_BUFFER_LEN - 1;
            }
            fprintf(stderr,
                    "[SCORCH-CREATE] slot=%d room=%d pos=(%.2f,%.2f,%.2f) size=%.2f alpha=%u\n",
                    traced_slot,
                    room,
                    g_ScorchBuffer[traced_slot].pos.f[0],
                    g_ScorchBuffer[traced_slot].pos.f[1],
                    g_ScorchBuffer[traced_slot].pos.f[2],
                    g_ScorchBuffer[traced_slot].explosion_size,
                    (unsigned int)g_ScorchBuffer[traced_slot].vertex_list[0].v.cn[3]);
            fflush(stderr);
        }
#endif
    }
}




/**
 * Address 0x7F0A0AB4.
*/
Gfx *explosionRenderScorchBuffer(Gfx *arg0)
{
    //temp_t6 = arg0;
    s32 i;
    s32 last_roomid = -1;
    s32 rendered_count = 0;
#ifdef NATIVE_PORT
    Gfx *trace_start = arg0;
#endif

#ifdef NATIVE_PORT
    if (portDisableScorchMarks())
    {
        return arg0;
    }
#endif
#ifndef NATIVE_PORT
    if (getPlayerCount() >= 2)
    {
        return arg0;
    }
    else
#endif
    {
        gSPSetGeometryMode(arg0++, G_CULL_BACK);
        gSPClearGeometryMode(arg0++, G_CULL_FRONT | G_FOG);
        gDPSetColorDither(arg0++, G_CD_NOISE);

        texSelect(&arg0, genericimage, 4, 1, 2);

        for (i=0; i<SCORCH_BUFFER_LEN; i++)
        {
            if (g_ScorchBuffer[i].roomid >= 0 && getROOMID_isRendered(g_ScorchBuffer[i].roomid))
            {
                if (last_roomid != g_ScorchBuffer[i].roomid)
                {
                    last_roomid = g_ScorchBuffer[i].roomid;
                    arg0 = applyRoomMatrixToDisplayList(arg0, g_ScorchBuffer[i].roomid);
                }

                /**
                 * Loads into the RSP vertex buffer the vertices that will be used by the gSP1Triangle commands that generates polygons.
                 *
                 * param v: the segment address of vertex list.
                 * param n: the number of vertices (1~32)
                 * param v0: Starting index in vertex buffer where vertices are to be loaded
                 * gSPVertex(Gfx *gdl, Vtx *v, u32 n, u32 v0)
                */
                gSPVertex(arg0++, osVirtualToPhysical((void*)g_ScorchBuffer[i].vertex_list), 4, 0);
                gSP2Triangles(arg0++,
                                0, 1, 2, 0,
                                0, 2, 3, 0);
                rendered_count++;
            }
        }

        gDPSetColorDither(arg0++, G_CD_BAYER);
    }

#ifdef NATIVE_PORT
    if (portTraceExplosions() && rendered_count > 0)
    {
        fprintf(stderr,
                "[SCORCH-RENDER] rendered=%d\n",
                rendered_count);
        fflush(stderr);
	    }
#endif

#ifdef NATIVE_PORT
    if (rendered_count > 0)
    {
        gfx_register_effect_dl_range("scorch", trace_start, arg0);
    }
#endif

    return arg0;
}





s32 explosionRoundFloat(f32 arg0)
{
    if (arg0 >= 0.0f)
    {
        return (s32) (arg0 + 0.5f);
    }

    return (s32) (arg0 - 0.5f);
}



void explosionClearBulletImpactRoomByFlag(PropRecord* arg0, s8 arg1)
{
    s32 i;
    for (i = 0; i < BULLET_IMPACT_BUFFER_LEN; i++)
    {
        if ((arg0 == g_BulletImpactBuffer[i].prop) && (arg1 == g_BulletImpactBuffer[i].room_clear_flag))
        {
            g_BulletImpactBuffer[i].room = -1;
        }
    }
}



void explosionClearBulletImpactRoom(PropRecord* arg0)
{
    s32 i;
    for (i = 0; i < BULLET_IMPACT_BUFFER_LEN; i++)
    {
        if ((arg0 == g_BulletImpactBuffer[i].prop) && (g_ImpactTypes[g_BulletImpactBuffer[i].impact_type].unk1 == 2))
        {
            g_BulletImpactBuffer[i].room = -1;
        }
    }
}


void explosionSetBulletImpactAlpha(s32 arg0)
{
    u32 val;
    s32 i;
    for (i = 0; i < SMOKE_PARTS_LEN; i++)
    {
        val = (u32) (((f32) i / 10.0f) * 255.0f);

        g_BulletImpactBuffer[arg0].vertex_list[3].v.cn[3] = val; // alpha
        g_BulletImpactBuffer[arg0].vertex_list[2].v.cn[3] = val; // alpha
        g_BulletImpactBuffer[arg0].vertex_list[1].v.cn[3] = val; // alpha
        g_BulletImpactBuffer[arg0].vertex_list[0].v.cn[3] = val; // alpha

        if (++arg0 >= BULLET_IMPACT_BUFFER_LEN)
        {
            arg0 = 0;
        }
    }
}


/***
 * NTSC address 0x7F0A108C.
*/
void explosionCreateBulletImpact(struct coord3d *pos, struct coord3d *arg1, s16 impact_type, s16 room, PropRecord *prop, s8 model_render_pos_index, s8 room_clear_flag)
{
    Vtx spE0;
    f32 spDC;
    f32 spD8;
    f32 spD4;
    f32 temp_f6;
    f32 temp_f2_2;
    f32 temp_f12;
    f32 spC4;
    struct coord3d *temp_s0_2;
    f32 spBC;
    f32 spB8;
    f32 spB4;
    f32 spB0;
    s32 temp_v1;
    struct coord3d spA0;
    f32 sp9C;
    f32 sp98;
    RenderPosView *temp_s1;
    ObjectRecord *temp_s0;
    f32 temp_f0;
    f32 sp88;
    s32 var_s1;
    struct coord3d sp78;
    struct coord3d sp6C;
    f32 zero = 0.0f;
    s_impacttype *sp50;
    u8 var_s0;
    u8 sp62;
    u8 sp61;
#ifdef NATIVE_PORT
    f32 decal_lift;
#endif

#ifdef NATIVE_PORT
    if (portDisableBulletImpacts())
    {
        return;
    }

    if (prop != NULL && portDisablePropBulletImpacts())
    {
        return;
    }

#endif

    spE0 = g_BulletImpactDefaultVertex;

    if (cheatIsActive(CHEAT_PAINTBALL))
    {
        impact_type = 0x10;
    }

#ifdef NATIVE_PORT
    decal_lift = portBulletImpactNormalOffset(prop, impact_type);
#endif

    spA0.f[0] = pos->f[0];
    spA0.f[1] = pos->f[1];
    spA0.f[2] = pos->f[2];

    sp50 = &g_ImpactTypes[impact_type];

    sp9C = sp50->width;
    sp98 = sp50->height;

    if ((arg1->f[0] == 0.0f) && (arg1->f[2] == 0.0f))
    {
#ifdef NATIVE_PORT
        spDC = 0.0f;
        spD8 = arg1->f[1] < 0.0f
            ? -1.0f
            : (arg1->f[1] > 0.0f ? 1.0f : 0.0f);
        spD4 = 0.0f;
#endif
        spB8 = 0.0f;
        spB4 = 0.0f;
        spBC = 0.0f;
        spB0 = 1.0f;
        spC4 = 1.0f;
    }
    else
    {
        temp_f0 = sqrtf((arg1->f[0] * arg1->f[0]) + (arg1->f[1] * arg1->f[1]) + (arg1->f[2] * arg1->f[2]));

        spDC = arg1->f[0] / temp_f0;
        spD8 = arg1->f[1] / temp_f0;
        spD4 = arg1->f[2] / temp_f0;

        temp_f0 = sqrtf((spDC * spDC) + (spD4 * spD4));

        temp_f2_2 = spDC / temp_f0;
        temp_f12 = spD4 / temp_f0;
        
        spBC = -temp_f2_2;
        spB8 = spD8 * temp_f2_2;
        spB4 = -temp_f0;
        spB0 = spD8 * temp_f12;
        spC4 = temp_f12;
    }

#ifdef NATIVE_PORT
    if (decal_lift != 0.0f
        && ((arg1->f[0] != 0.0f) || (arg1->f[1] != 0.0f) || (arg1->f[2] != 0.0f))) {
        /* The original stores coplanar impact quads. On PC, glass crack
         * decals need a small normal lift to survive depth testing against
         * transparent panes; GE007_BULLET_IMPACT_NORMAL_OFFSET remains a
         * global diagnostic override. */
        spA0.f[0] += spDC * decal_lift;
        spA0.f[1] += spD8 * decal_lift;
        spA0.f[2] += spD4 * decal_lift;
    }
#endif

    if (prop != NULL)
    {
#ifdef NATIVE_PORT
        if (!portEnsurePropImpactRenderPos(prop, &model_render_pos_index)) {
            return;
        }
#endif
        temp_s0 = prop->obj;
        temp_s1 = &temp_s0->model->render_pos[model_render_pos_index];
        
        sp78.f[0] = spC4;
        sp78.f[1] = 0.0f;
        sp78.f[2] = spBC;
        
        sp6C.f[0] = spB8;
        sp6C.f[1] = spB4;
        sp6C.f[2] = spB0;
        
        mtx4RotateVecInPlace(&temp_s1->pos, &sp78);
        mtx4RotateVecInPlace(&temp_s1->pos, &sp6C);
        
        sp88 = sqrtf((sp78.f[0] * sp78.f[0]) + (sp78.f[1] * sp78.f[1]) + (sp78.f[2] * sp78.f[2]));
        temp_f6 = sqrtf((sp6C.f[0] * sp6C.f[0]) + (sp6C.f[1] * sp6C.f[1]) + (sp6C.f[2] * sp6C.f[2]));
        
        sp9C /= sp88;
        sp98 /= temp_f6;

#ifdef NATIVE_PORT
        if (portPropIsGlassLike(prop) && portImpactIsGlassCrack(impact_type))
        {
            f32 boost_x = portGlassImpactAxisBoost(&sp78, sp88);
            f32 boost_y = portGlassImpactAxisBoost(&sp6C, temp_f6);

            if (boost_x > 1.0f || boost_y > 1.0f)
            {
                sp9C *= boost_x;
                sp98 *= boost_y;

                if (portTraceBulletImpacts())
                {
                    fprintf(stderr,
                            "[BULLET-IMPACT-GLASS-BOOST] impact=%d boost=(%.3f,%.3f) "
                            "axis_len=(%.4f,%.4f) axis_screen=(%.4f,%.4f)\n",
                            impact_type,
                            boost_x,
                            boost_y,
                            sp88,
                            temp_f6,
                            sqrtf(sp78.f[0] * sp78.f[0] + sp78.f[1] * sp78.f[1]),
                            sqrtf(sp6C.f[0] * sp6C.f[0] + sp6C.f[1] * sp6C.f[1]));
                    fflush(stderr);
                }
            }
        }
#endif
        
        if ((sp50->unk2 < 2) && (sp50->unk1 == 2))
        {
            temp_s0->state |= PROPSTATE_2;
        }
        else
        {
            temp_s0->state |= PROPSTATE_DAMAGED;
        }
    }
    else
    {
        temp_s0_2 = getRoomPositionByIndex((s32) room);
        spA0.f[0] = (spA0.f[0] * get_room_data_float1()) - temp_s0_2->f[0];
        spA0.f[1] = (spA0.f[1] * get_room_data_float1()) - temp_s0_2->f[1];
        spA0.f[2] = (spA0.f[2] * get_room_data_float1()) - temp_s0_2->f[2];
        sp9C *= get_room_data_float1();
        sp98 *= get_room_data_float1();
    }

    g_BulletImpactBuffer[g_NumImpactEntries].prop = prop;
    g_BulletImpactBuffer[g_NumImpactEntries].model_render_pos_index = model_render_pos_index;
    g_BulletImpactBuffer[g_NumImpactEntries].room_clear_flag = room_clear_flag;
    g_BulletImpactBuffer[g_NumImpactEntries].room = room;
    g_BulletImpactBuffer[g_NumImpactEntries].impact_type = impact_type;

    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0] = spE0;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1] = spE0;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2] = spE0;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3] = spE0;

    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[0] = explosionRoundFloat((spA0.f[0] - (sp9C * spC4)) - (sp98 * spB8));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[1] = explosionRoundFloat((spA0.f[1] - zero) - (sp98 * spB4));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[2] = explosionRoundFloat((spA0.f[2] - (sp9C * spBC)) - (sp98 * spB0));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.tc[0] = 0;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.tc[1] = impactimages[impact_type].height << 5;
    
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[0] = explosionRoundFloat((spA0.f[0] - (sp9C * spC4)) + (sp98 * spB8));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[1] = explosionRoundFloat((spA0.f[1] - zero) + (sp98 * spB4));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[2] = explosionRoundFloat((spA0.f[2] - (sp9C * spBC)) + (sp98 * spB0));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.tc[0] = 0;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.tc[1] = 0;
    
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[0] = explosionRoundFloat(spA0.f[0] + (sp9C * spC4) + (sp98 * spB8));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[1] = explosionRoundFloat(spA0.f[1] + zero + (sp98 * spB4));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[2] = explosionRoundFloat(spA0.f[2] + (sp9C * spBC) + (sp98 * spB0));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.tc[0] = impactimages[impact_type].width << 5;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.tc[1] = 0;
    
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[0] = explosionRoundFloat((spA0.f[0] + (sp9C * spC4)) - (sp98 * spB8));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[1] = explosionRoundFloat((spA0.f[1] + zero) - (sp98 * spB4));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[2] = explosionRoundFloat((spA0.f[2] + (sp9C * spBC)) - (sp98 * spB0));
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.tc[0] = impactimages[impact_type].width << 5;
    g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.tc[1] = impactimages[impact_type].height << 5;
    
    for (var_s1 = 0; var_s1 < 4; var_s1++)
    {
        switch (sp50->apptype)
        {
            default:
                var_s0 = 0;
            break;

            case 1:
                temp_v1 = 0xFF - (randomGetNext() % 40U);
                sp61 = temp_v1;
                sp62 = temp_v1;
                var_s0 = temp_v1;
            break;

            case 0:
                temp_v1 = randomGetNext() % 40U;
                sp61 = temp_v1;
                sp62 = temp_v1;
                var_s0 = temp_v1;
            break;

            case 2:
                var_s0 = (randomGetNext() & 1) ? 0xff : 0;
                sp62 = (randomGetNext() & 1) ? 0xff : 0;
                sp61 = (randomGetNext() & 1) ? 0xff : 0;
            break;
        }

        g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[var_s1].v.cn[0] = var_s0;
        g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[var_s1].v.cn[1] = sp62;
        g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[var_s1].v.cn[2] = sp61;
        g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[var_s1].v.cn[3] = 0xff; // alpha
    }

#ifdef NATIVE_PORT
    {
        const f32 trace_normal_unit[3] = { spDC, spD8, spD4 };
        const f32 trace_axis_x[3] = { spC4, zero, spBC };
        const f32 trace_axis_y[3] = { spB8, spB4, spB0 };
        const f32 trace_raw_vertices[4][3] = {
            {
                (spA0.f[0] - (sp9C * spC4)) - (sp98 * spB8),
                (spA0.f[1] - zero) - (sp98 * spB4),
                (spA0.f[2] - (sp9C * spBC)) - (sp98 * spB0),
            },
            {
                (spA0.f[0] - (sp9C * spC4)) + (sp98 * spB8),
                (spA0.f[1] - zero) + (sp98 * spB4),
                (spA0.f[2] - (sp9C * spBC)) + (sp98 * spB0),
            },
            {
                spA0.f[0] + (sp9C * spC4) + (sp98 * spB8),
                spA0.f[1] + zero + (sp98 * spB4),
                spA0.f[2] + (sp9C * spBC) + (sp98 * spB0),
            },
            {
                (spA0.f[0] + (sp9C * spC4)) - (sp98 * spB8),
                (spA0.f[1] + zero) - (sp98 * spB4),
                (spA0.f[2] + (sp9C * spBC)) - (sp98 * spB0),
            },
        };
        const s16 trace_rounded_vertices[4][3] = {
            {
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[2],
            },
            {
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[1].v.ob[2],
            },
            {
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[2],
            },
            {
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[3].v.ob[2],
            },
        };

        portTraceBulletImpactCreate(g_NumImpactEntries,
                                    impact_type,
                                    room,
                                    prop,
                                    model_render_pos_index,
                                    room_clear_flag,
                                    impactimages[impact_type].width,
                                    impactimages[impact_type].height,
                                    &spA0,
                                    arg1,
                                    sp9C,
                                    sp98,
                                    decal_lift,
                                    trace_normal_unit,
                                    trace_axis_x,
                                    trace_axis_y,
                                    trace_raw_vertices,
                                    trace_rounded_vertices);
    }

    if (portTraceBulletImpacts())
    {
        fprintf(stderr,
                "[BULLET-IMPACT-CREATE] slot=%d impact=%d room=%d prop=%p model_pos=%d clear=%d wh=%dx%d "
                "pos=(%.2f,%.2f,%.2f) normal=(%.4f,%.4f,%.4f) size=(%.2f,%.2f) offset=%.4f "
                "v0=(%d,%d,%d) v2=(%d,%d,%d)\n",
                g_NumImpactEntries,
                impact_type,
                room,
                (void *)prop,
                model_render_pos_index,
                room_clear_flag,
                impactimages[impact_type].width,
                impactimages[impact_type].height,
                spA0.f[0],
                spA0.f[1],
                spA0.f[2],
                arg1->f[0],
                arg1->f[1],
                arg1->f[2],
                sp9C,
                sp98,
                decal_lift,
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[0].v.ob[2],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[0],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[1],
                g_BulletImpactBuffer[g_NumImpactEntries].vertex_list[2].v.ob[2]);
        fflush(stderr);
    }
#endif

    g_NumImpactEntries++;
    if (g_NumImpactEntries >= BULLET_IMPACT_BUFFER_LEN)
    {
#ifdef NATIVE_PORT
        if (portTraceBulletImpacts())
        {
            fprintf(stderr,
                    "[BULLET-IMPACT-WRAP] next_slot=%d len=%d\n",
                    0,
                    BULLET_IMPACT_BUFFER_LEN);
            fflush(stderr);
        }
#endif
        g_NumImpactEntries = 0;
    }
    
    explosionSetBulletImpactAlpha(g_NumImpactEntries);
    
    g_BulletImpactBuffer[g_NumImpactEntries].room = -1;
}





/***
 * Perfect Dark Gfx *smokeRender(struct prop *prop, Gfx *gdl, bool xlupass)
 * 
 * NTSC address 0x7F0A1A94.
*/
Gfx *explosionRenderBulletImpactOnProp(Gfx *gdl, PropRecord *arg1, s32 arg2)
{
    s32 padding1;
    s32 padding2;
    
    s32 i; // var_s4
    s32 sp50;
    struct Scorch *sp4C;
    s32 sp48;
    RenderPosView *render_pos;
    s32 padding3;

    s16 var_s5;
    s32 impact_type;
    s32 var_v0;
    s32 rendered_count;
#ifdef NATIVE_PORT
    s32 use_flat_impacts;
    Gfx *trace_start;
    const char *effect_label;
    s32 prop_matrix_count;
#endif

    var_s5 = -1;
    sp50 = 0;
    sp4C = NULL;
    sp48 = -1;
    rendered_count = 0;
#ifdef NATIVE_PORT
    prop_matrix_count = 0;
    use_flat_impacts = portUseFlatBulletImpacts(arg1);
    trace_start = gdl;
    effect_label = arg1 == NULL
        ? "bullet_impact_world"
        : (use_flat_impacts ? "bullet_impact_prop_flat" : "bullet_impact_prop_textured");
#endif

#ifdef NATIVE_PORT
    if (portDisableBulletImpacts())
    {
        return gdl;
    }

    if (arg1 != NULL && portDisablePropBulletImpacts())
    {
        return gdl;
    }
#endif
    
    if (arg1 != NULL)
    {
        sp4C = arg1->scorch;
#ifdef NATIVE_PORT
        if (sp4C == NULL || sp4C->model == NULL || sp4C->model->render_pos == NULL)
        {
            return gdl;
        }

        prop_matrix_count = modelGetRenderPosCount(sp4C->model);
        if (prop_matrix_count <= 0)
        {
            return gdl;
        }

        bondviewRegisterLevelVisibilityScaledRenderPos(sp4C->model);
#endif
    }

    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gDPSetColorDither(gdl++, G_CD_NOISE);

#ifdef NATIVE_PORT
    if (use_flat_impacts)
    {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetTextureLUT(gdl++, G_TT_NONE);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
    }
#endif

    for (i = 0; i < BULLET_IMPACT_BUFFER_LEN; i++)
    {
        if (arg1 == g_BulletImpactBuffer[i].prop)
        {
            if (g_BulletImpactBuffer[i].room >= 0)
            {
                if (arg1 || getROOMID_isRendered(g_BulletImpactBuffer[i].room))
                {
                    impact_type = g_BulletImpactBuffer[i].impact_type;
                    
                    if (arg2)
                    {
                        var_v0 = g_ImpactTypes[impact_type].unk2 < 2 && g_ImpactTypes[impact_type].unk1 == 2;
                    }
                    else
                    {
                        var_v0 = (g_ImpactTypes[impact_type].unk2 >= 2) || g_ImpactTypes[impact_type].unk1 != 2;
                    }
    
                    if (var_v0)
                    {
                        sp50 = 1;
    
                        if (arg1 != NULL)
                        {
#ifdef NATIVE_PORT
                            if (g_BulletImpactBuffer[i].model_render_pos_index < 0
                                || g_BulletImpactBuffer[i].model_render_pos_index >= prop_matrix_count)
                            {
                                continue;
                            }
#endif
                            if (var_s5 != g_BulletImpactBuffer[i].model_render_pos_index)
                            {
                                render_pos = &sp4C->model->render_pos[g_BulletImpactBuffer[i].model_render_pos_index];
                                var_s5 = g_BulletImpactBuffer[i].model_render_pos_index;
#ifdef NATIVE_PORT
                                if (portTraceBulletImpacts())
                                {
                                    f32 sx = sqrtf(
                                        render_pos->pos.m[0][0] * render_pos->pos.m[0][0] +
                                        render_pos->pos.m[0][1] * render_pos->pos.m[0][1] +
                                        render_pos->pos.m[0][2] * render_pos->pos.m[0][2]);
                                    f32 sy = sqrtf(
                                        render_pos->pos.m[1][0] * render_pos->pos.m[1][0] +
                                        render_pos->pos.m[1][1] * render_pos->pos.m[1][1] +
                                        render_pos->pos.m[1][2] * render_pos->pos.m[1][2]);
                                    f32 sz = sqrtf(
                                        render_pos->pos.m[2][0] * render_pos->pos.m[2][0] +
                                        render_pos->pos.m[2][1] * render_pos->pos.m[2][1] +
                                        render_pos->pos.m[2][2] * render_pos->pos.m[2][2]);

                                    fprintf(stderr,
                                            "[BULLET-IMPACT-MTX] model_pos=%d pos=(%.2f,%.2f,%.2f) scale=(%.4f,%.4f,%.4f)\n",
                                            var_s5,
                                            render_pos->pos.m[3][0],
                                            render_pos->pos.m[3][1],
                                            render_pos->pos.m[3][2],
                                            sx,
                                            sy,
                                            sz);
                                    fflush(stderr);
                                }
#endif
#ifdef NATIVE_PORT
                                gSPMatrix(gdl++, osVirtualToPhysical(render_pos), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW | G_MTX_FLOAT_PORT);
#else
                                gSPMatrix(gdl++, osVirtualToPhysical(render_pos), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
#endif
                            }
                        }
                        else
                        {
                            if (var_s5 != g_BulletImpactBuffer[i].room)
                            {
                                var_s5 = g_BulletImpactBuffer[i].room;
#ifdef NATIVE_PORT
                                gdl = applyRoomMatrixToDisplayListForWorldImpact(gdl, g_BulletImpactBuffer[i].room);
#else
                                gdl = applyRoomMatrixToDisplayList(gdl, g_BulletImpactBuffer[i].room);
#endif
                            }
                        }
    
                        if (sp48 != impact_type)
                        {
#ifdef NATIVE_PORT
                            if (!use_flat_impacts)
                            {
#endif
                            texDebugPushSourceTag("bullet_impact");
                            texSelect(&gdl, &impactimages[impact_type], g_ImpactTypes[impact_type].unk1, g_ImpactTypes[impact_type].unk2, 2U);
                            texDebugPopSourceTag();
#ifdef NATIVE_PORT
                            }
#endif
                            sp48 = impact_type;
                        }

                        gSPVertex(gdl++, osVirtualToPhysical(g_BulletImpactBuffer[i].vertex_list), 4, 0);
                        gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
                        rendered_count++;
                    }
                }
            }
        }
    }

    if ((arg1 != NULL) && (sp50 == 0))
    {
        sp4C->unk02 &= ~(1 << arg2);
    }

    if (sp50)
    {
#ifdef NATIVE_PORT
        /* The textured impact path (texSelect) leaves the RDP in textured state:
         * texture ON, combine = G_CC_MODULATEIA, a DECAL render mode, LUT/LOD set,
         * and -- for any mipmapped impact image -- cycle = G_CYC_2CYCLE. Prop
         * impacts are emitted INLINE in the per-prop display list (chrobjhandler.c),
         * immediately before the child-prop recursion and sibling props, so that
         * state must not leak. Most following geometry is models that re-establish
         * their own material setup, but raw inline DL (e.g. door words) inherits
         * combine/cycle, so normalise the RDP to a neutral, untextured baseline
         * here. This makes the now-default textured prop path self-contained for
         * every caller (the inline prop path AND the isolated world pass) --
         * precautionary hardening that keeps the textured default safe. NB: this
         * is a neutral OPA baseline, NOT the legacy flat path's XLU_DECAL mode;
         * the explicit cycle reset covers the mipmapped-impact G_CYC_2CYCLE case
         * that the partial SETOTHERMODE_H writes below would otherwise miss. */
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetTextureLUT(gdl++, G_TT_NONE);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
#endif
        /* Bullet impacts render as double-sided quads, but the surrounding
         * room/world passes assume normal backface culling. Restore that
         * state here so firing decals do not leak disabled culling into
         * subsequent scene geometry. */
        gSPSetGeometryMode(gdl++, G_CULL_BACK);
    }

    gDPSetColorDither(gdl++, G_CD_BAYER);

#ifdef NATIVE_PORT
    portTraceBulletImpactRender(arg1,
                                arg1 == NULL ? 1 : 0,
                                arg2,
                                use_flat_impacts,
                                rendered_count,
                                sp48,
                                g_NumImpactEntries);

    if (portTraceBulletImpacts() && rendered_count > 0)
    {
        fprintf(stderr,
                "[BULLET-IMPACT-RENDER] world=%d alpha_pass=%d flat=%d rendered=%d last_impact=%d current_slot=%d\n",
                arg1 == NULL ? 1 : 0,
                arg2,
                use_flat_impacts,
                rendered_count,
                sp48,
                g_NumImpactEntries);
        fflush(stderr);
	    }
#endif

#ifdef NATIVE_PORT
    if (rendered_count > 0)
    {
        gfx_register_effect_dl_range(effect_label, trace_start, gdl);
    }
#endif

    return gdl;
}





Gfx * explosionCallRenderBulletImpactOnProp(Gfx *arg0)
{
#ifdef NATIVE_PORT
    arg0 = explosionRenderBulletImpactOnProp(arg0, NULL, 0);
    return explosionRenderBulletImpactOnProp(arg0, NULL, 1);
#else
    return explosionRenderBulletImpactOnProp(arg0, NULL, 0);
#endif
}
