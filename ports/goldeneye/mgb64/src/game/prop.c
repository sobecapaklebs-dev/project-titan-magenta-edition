#include <ultra64.h>
#include <memp.h>
#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ge_debug.h"
#include "platform/port_env.h"
#include "platform/setup_pnames.h"
#endif
#include "game/mp_weapon.h"
#include "game/player_2.h"
#include "game/bondview_r.h"
#include "bg.h"
#include "bondview_r.h"
#include "bondinv.h"
#include "cheat_buttons.h"
#include "chr.h"
#include "chrai.h"
#include "chrlv.h"
#include "chrobjhandler.h"
#include "front.h"
#include "inititemslots.h"
#include "initobjects.h"
#include "initpathtablesomething.h"
#include "limits.h"
#include "loadobjectmodel.h"
#include "lvl_text.h"
#include "lvl.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "mp_weapon.h"
#include "ob.h"
#include "objective.h"
#include "objective_status.h"
#include "objecthandler.h"
#include "player.h"
#include "prop.h"
#include "stan.h"
#include "token.h"

/**
 * EU .bss 0x80068480
*/
ITEM_IDS lastmpweaponnum;

#ifdef NATIVE_PORT
static int g_MonitorTraceEnabled = -1;
static int g_MonitorTraceBudget = -1;
static int g_DoorSetupTraceEnabled = -1;
static int g_DoorSetupTraceBudget = -1;
static int g_SetupLinkTraceEnabled = -1;
static int g_SetupLinkTraceBudget = -1;
static int g_TintedGlassTraceEnabled = -1;
static int g_TintedGlassTraceBudget = -1;
static int g_HatTraceEnabled = -1;
static int g_HatTraceBudget = -1;

static int monitorTraceEnabled(void)
{
    const char *value;

    if (g_MonitorTraceEnabled >= 0) {
        return g_MonitorTraceEnabled;
    }

    value = getenv("GE007_MONITOR_TRACE");
    g_MonitorTraceEnabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;

    value = getenv("GE007_MONITOR_TRACE_BUDGET");
    g_MonitorTraceBudget = (value != NULL && value[0] != '\0') ? atoi(value) : 200;

    return g_MonitorTraceEnabled;
}

static void monitorTracePrintf(const char *fmt, ...)
{
    va_list ap;

    if (!monitorTraceEnabled()) {
        return;
    }

    if (g_MonitorTraceBudget == 0) {
        return;
    }

    if (g_MonitorTraceBudget > 0) {
        g_MonitorTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[MONITOR_TRACE] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static int hatTraceEnabled(void)
{
    const char *value;

    if (g_HatTraceEnabled >= 0) {
        return g_HatTraceEnabled;
    }

    value = getenv("GE007_HAT_TRACE");
    g_HatTraceEnabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;

    value = getenv("GE007_HAT_TRACE_BUDGET");
    g_HatTraceBudget = (value != NULL && value[0] != '\0') ? atoi(value) : 200;

    return g_HatTraceEnabled;
}

static void hatTracePrintf(const char *fmt, ...)
{
    va_list ap;

    if (!hatTraceEnabled()) {
        return;
    }

    if (g_HatTraceBudget == 0) {
        return;
    }

    if (g_HatTraceBudget > 0) {
        g_HatTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[HAT_TRACE] setup ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static int doorSetupTraceEnabled(void)
{
    const char *value;

    if (g_DoorSetupTraceEnabled >= 0) {
        return g_DoorSetupTraceEnabled;
    }

    value = getenv("GE007_DOOR_SETUP_TRACE");
    g_DoorSetupTraceEnabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;

    value = getenv("GE007_DOOR_SETUP_TRACE_BUDGET");
    g_DoorSetupTraceBudget = (value != NULL && value[0] != '\0') ? atoi(value) : 100;

    return g_DoorSetupTraceEnabled;
}

static void doorSetupTracePrintf(const char *fmt, ...)
{
    va_list ap;

    if (!doorSetupTraceEnabled()) {
        return;
    }

    if (g_DoorSetupTraceBudget == 0) {
        return;
    }

    if (g_DoorSetupTraceBudget > 0) {
        g_DoorSetupTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[DOOR_SETUP_TRACE] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static int setupLinkTraceEnabled(void)
{
    const char *value;

    if (g_SetupLinkTraceEnabled >= 0) {
        return g_SetupLinkTraceEnabled;
    }

    value = getenv("GE007_SETUP_LINK_TRACE");
    g_SetupLinkTraceEnabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;

    value = getenv("GE007_SETUP_LINK_TRACE_BUDGET");
    g_SetupLinkTraceBudget = (value != NULL && value[0] != '\0') ? atoi(value) : 200;

    return g_SetupLinkTraceEnabled;
}

static void setupLinkTracePrintf(const char *fmt, ...)
{
    va_list ap;

    if (!setupLinkTraceEnabled()) {
        return;
    }

    if (g_SetupLinkTraceBudget == 0) {
        return;
    }

    if (g_SetupLinkTraceBudget > 0) {
        g_SetupLinkTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[SETUP_LINK_TRACE] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static int tintedGlassTraceEnabled(void)
{
    const char *value;

    if (g_TintedGlassTraceEnabled >= 0) {
        return g_TintedGlassTraceEnabled;
    }

    value = getenv("GE007_TRACE_TINTED_GLASS");
    g_TintedGlassTraceEnabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;

    value = getenv("GE007_TRACE_TINTED_GLASS_BUDGET");
    g_TintedGlassTraceBudget = (value != NULL && value[0] != '\0') ? atoi(value) : 120;

    return g_TintedGlassTraceEnabled;
}

static void tintedGlassTracePrintf(const char *fmt, ...)
{
    va_list ap;

    if (!tintedGlassTraceEnabled()) {
        return;
    }

    if (g_TintedGlassTraceBudget == 0) {
        return;
    }

    if (g_TintedGlassTraceBudget > 0) {
        g_TintedGlassTraceBudget--;
    }

    va_start(ap, fmt);
    fprintf(stderr, "[TINTED_GLASS_TRACE] setup ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
}

static f32 setupFixed16StoredInFloatToFloat(f32 value)
{
    s32 raw;

    memcpy(&raw, &value, sizeof(raw));
    return raw / 65536.0f;
}
#endif

//.section .rodata

const char aNochr[] = "-nochr";
const char aNoprop[] = "-noprop";
const char aNoobj[] = "-noobj";
const char aNoprop_0[] = "-noprop";

const u32 only_read_by_stageload[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char aMp_[] = "mp_";

// forward declarations

s32 load_proptype(PROPDEF_TYPE type);
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1);
void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex);
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex);
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex);
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex);
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex);
void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex);
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex);
void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex);
void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex);
void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4);
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2);
void chrpropDelist(PropRecord *prop);
void alloc_init_GUARDdata_entries(s32 count);
void sub_GAME_7F005540(s32 arg0);
void sub_GAME_7F005624(s32 arg0);
/* Defined in bg.c:18963. Prototype needed here so the prop.c:3331/4131 calls are
 * declared — fixes -Wimplicit-function-declaration (post-audit regression from
 * 429391c) that breaks -DPORT_STRICT builds. Signature matches the definition. */
s32 sub_GAME_7F0B9E04(coord3d *arg0, coord3d *arg1);
void alloc_false_GUARDdata_to_exec_global_action(void);

#ifdef NATIVE_PORT
static s32 cctvGetTargetPad(const CCTVRecord *cctv)
{
    return cctv != NULL ? cctv->cctv_pad : -1;
}
#else
static s32 cctvGetTargetPad(const CCTVRecord *cctv)
{
    return cctv != NULL ? cctv->pad : -1;
}
#endif

s32 load_proptype(PROPDEF_TYPE type)
{
    PropDefHeaderRecord *propdef = (PropDefHeaderRecord *) g_CurrentSetup.propDefs;
    s32 count = 0;

    if (propdef != NULL)
    {
        while (propdef->type != PROPDEF_END)
        {
            if (propdef->type == (type & 0xFF))
            {
                count ++;
            }
            propdef = &propdef[sizepropdef((PropDefHeaderRecord* ) propdef)];
        }
    }
    return count;
}


/**
 * perfect dark padGetCentre (pad.c)
 *
 * NTSC address 0x7F001BD4.
*/
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1)
{
    struct coord3d normal;
    f32 scale;
    struct bbox bb;
    f32 temp;

    bb.zmax = pad->bbox.xmin;
    bb.zmin = pad->bbox.xmax;
    bb.ymax = pad->bbox.ymin;
    bb.ymin = pad->bbox.ymax;
    bb.xmax = pad->bbox.zmin;
    bb.xmin = pad->bbox.zmax;

    normal.f[0] = (pad->up.f[1] * pad->look.f[2]) - (pad->up.f[2] * pad->look.f[1]);
    normal.f[1] = (pad->up.f[2] * pad->look.f[0]) - (pad->up.f[0] * pad->look.f[2]);
    normal.f[2] = (pad->up.f[0] * pad->look.f[1]) - (pad->up.f[1] * pad->look.f[0]);

    temp = (normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1]) + (normal.f[2] * normal.f[2]);
    scale = 1.0f / sqrtf(temp);

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg1->f[0] = pad->pos.f[0] + (
			(bb.zmax + bb.zmin) * normal.f[0] +
			(bb.ymax + bb.ymin) * pad->up.f[0] +
			(bb.xmax + bb.xmin) * pad->look.f[0]) * 0.5f;

	arg1->f[1] = pad->pos.f[1] + (
			(bb.zmax + bb.zmin) * normal.f[1] +
			(bb.ymax + bb.ymin) * pad->up.f[1] +
			(bb.xmax + bb.xmin) * pad->look.f[1]) * 0.5f;

	arg1->f[2] = pad->pos.f[2] + (
			(bb.zmax + bb.zmin) * normal.f[2] +
			(bb.ymax + bb.ymin) * pad->up.f[2] +
			(bb.xmax + bb.xmin) * pad->look.f[2]) * 0.5f;

}

/**
 * NTSC address 0x7F001D9C.
*/
void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex)
{
    s32 padding;
    s32 spF0;
    f32 var_f0;
    struct coord3d spE0;
    struct StandTile *spDC;
    struct coord3d spD0;
    StandTile *spCC;
    Mtxf sp8C;
    struct coord3d sp80;
    struct PropRecord *var_v0;
    f32 sp78;
    s32 sp74;
    struct BoundPadRecord *var_s0;
    ChrRecord *sp6C;
    ModelRoData_BoundingBoxRecord *temp_v0_3;
    struct PadRecord *sp64;
    struct PropRecord *sp60;
    s32 padding2;
    f32 sp58;
    f32 sp54;
    f32 sp50;
    s32 padding3;
    f32 sp48;

    spF0 = arg1->obj;
    var_s0 = NULL;

    /* Note: PROPFLAG_ASSIGNEDTOCHR objects are now handled normally —
     * guards are spawned in the same pass (expand_09_characters). */
    modelLoad(spF0);

    sp78 = arg1->extrascale * 0.00390625f;

    arg1->damage = *(s32*)&arg1->damage / 65536.0f;

    if (getPlayerCount() >= 2)
    {
        sp74 = 1;

        if ((get_scenario() == SCENARIO_TLD) && (arg1->obj == PROP_FLAG))
        {
            sp74 = 0;
        }
        else if ((get_scenario() == SCENARIO_MWTGG) && (arg1->obj == PROP_CHRGOLDEN))
        {
            sp74 = 0;
        }

        if (sp74 != 0)
        {
            arg1->state |= 4; // respawn enabled
        }
    }

    if (arg1->flags & PROPFLAG_INSIDEANOTHEROBJ)
    {
        if (arg1->type == PROP_TYPE_SMOKE)
        {
            sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
        }
        else
        {
            objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
        }

        modelSetScale(arg1->model, arg1->model->scale * sp78);
    }
    else if (arg1->flags & PROPFLAG_ASSIGNEDTOCHR)
    {
        sp6C = chrFindByLiteralId(arg1->pad);

        if ((sp6C != NULL) && (sp6C->prop != NULL) && (sp6C->model != NULL))
        {
            if (arg1->type == 8)
            {
                var_v0 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                var_v0 = objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            chrpropReparent(var_v0, sp6C->prop);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: no chr number %d for obj number %d!\n",arg1->pad,cmdindex + 1);
        }
        #endif
    }
    else
    {
        if (isNotBoundPad(arg1->pad))
        {
            sp64 = &g_CurrentSetup.pads[arg1->pad];

            matrix_4x4_7F059908(&sp8C, 0.0f, 0.0f, 0.0f, -sp64->look.f[0], -sp64->look.f[1], -sp64->look.f[2], sp64->up.f[0], sp64->up.f[1], sp64->up.f[2]);

            spD0.f[0] = sp64->pos.f[0];
            spD0.f[1] = sp64->pos.f[1];
            spD0.f[2] = sp64->pos.f[2];

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }
            else
            {
                // same as above?

                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }

            spCC = sp64->stan;
        }
        else
        {
#ifdef NATIVE_PORT
            if (g_CurrentSetup.boundpads == NULL) {
                return;
            }
#endif
            var_s0 = &g_CurrentSetup.boundpads[getBoundPadNum(arg1->pad)];

            matrix_4x4_7F059908(&sp8C, 0.0f, 0.0f, 0.0f, -var_s0->look.f[0], -var_s0->look.f[1], -var_s0->look.f[2], var_s0->up.f[0], var_s0->up.f[1], var_s0->up.f[2]);

            if (!(arg1->flags2 & PROPFLAG2_00000001))
            {
                sub_GAME_7F001BD4(var_s0, &spD0);

                sp80.f[0] = spD0.f[0] + (var_s0->up.f[0] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[1] = spD0.f[1] + (var_s0->up.f[1] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[2] = spD0.f[2] + (var_s0->up.f[2] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));

                spCC = var_s0->stan;

                if (walkTilesBetweenPoints_NoCallback(&spCC, var_s0->pos.f[0], var_s0->pos.f[2], spD0.f[0], spD0.f[2]) == 0)
                {
                    spD0.f[0] = var_s0->pos.f[0];
                    spD0.f[1] = var_s0->pos.f[1];
                    spD0.f[2] = var_s0->pos.f[2];

                    spCC = var_s0->stan;

                    if (!(arg1->flags & PROPFLAG_ONSCREEN) && !(arg1->flags & PROPFLAG_00001000))
                    {
                        // removed
                        #ifdef DEBUG
                            osSyncPrintf("object number %d not positioned correctly!\n",cmdindex + 1);
                        #endif
                    }
                }
            }
            else
            {
                spD0.f[0] = var_s0->pos.f[0];
                spD0.f[1] = var_s0->pos.f[1];
                spD0.f[2] = var_s0->pos.f[2];

                spCC = var_s0->stan;

                sub_GAME_7F001BD4(var_s0, &sp80);

                sp80.f[0] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[0];
                sp80.f[1] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[1];
                sp80.f[2] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[2];
            }
        }

        if (sub_GAME_7F056850(&spD0, spCC, 0.0f, &spE0, &spDC) != 0)
        {
            if (arg1->type == PROP_TYPE_SMOKE)
            {
                sp60 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                sp60 = objInitWithAutoModel(arg1);
            }

            if (var_s0 != NULL)
            {
                temp_v0_3 = chrobjGetBboxFromObjectRecord(arg1);
                if (temp_v0_3 != NULL)
                {
                    sp58 = 1.0f;
                    sp54 = 1.0f;
                    sp50 = 1.0f;

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000020))
                    {
                        if (temp_v0_3->Bounds.xmin < temp_v0_3->Bounds.xmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000040))
                    {
                        if (temp_v0_3->Bounds.ymin < temp_v0_3->Bounds.ymax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                            else
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000080))
                    {
                        if (temp_v0_3->Bounds.zmin < temp_v0_3->Bounds.zmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                        }
                    }

                    var_f0 = sp58;

                    if (sp54 < var_f0)
                    {
                        var_f0 = sp54;
                    }

                    if (sp50 < var_f0)
                    {
                        var_f0 = sp50;
                    }

                    sp48 = sp58;

                    if (sp58 < sp54)
                    {
                        sp48 = sp54;
                    }

                    if (sp48 < sp50)
                    {
                        sp48 = sp50;
                    }

                    if (arg1->flags & PROPFLAG_00000010)
                    {
                        sp50 = var_f0;
                        sp54 = var_f0;
                        sp58 = var_f0;
                    }
                    else
                    {
                        if (!(arg1->flags & PROPFLAG_00000020))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                                {
                                    sp58 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                            {
                                sp58 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000040))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                                {
                                    sp50 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                            {
                                sp54 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000080))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                                {
                                    sp54 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                            {
                                sp50 = sp48;
                            }
                        }
                    }

                    sp58 /= sp48;
                    sp54 /= sp48;
                    sp50 /= sp48;

                    if ((sp58 <= 0.000001f) || (sp54 <= 0.000001f) || (sp50 <= 0.000001f))
                    {
                        #ifdef DEBUG
                        osSyncPrintf("Scale warning: object number %d has a small scale: %f,%f,%f\n",cmdindex +1, sp58,sp54,sp50);
                        #endif
                        sp50 = 1.0f;
                        sp54 = 1.0f;
                        sp58 = 1.0f;
                    }

                    matrix_column_1_scalar_multiply(sp58, sp8C.m[0]);
                    matrix_column_2_scalar_multiply(sp54, sp8C.m[0]);
                    matrix_column_3_scalar_multiply_2(sp50, sp8C.m[0]);

                    modelSetScale(arg1->model, arg1->model->scale * sp48);
                }
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            matrix_scalar_multiply(arg1->model->scale, sp8C.m[0]);

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sub_GAME_7F040BA0(arg1, &spE0, &sp8C, spDC, &sp80);
            }
            else
            {
                sub_GAME_7F04088C(arg1, &spE0, &sp8C, spDC, &sp80);
            }

            setupUpdateObjectRoomPosition(arg1);
            chrpropActivate(sp60);
            chrpropEnable(sp60);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: prop obj number %d not reset!\n",cmdindex + 1);
        }
        #endif
    }
}

/**
 * NTSC address 0x7F002738.
 * PAL address 0x7F002738.
*/
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex)
{
    s32 padding;
    bool hastoken;
    ChrRecord* chr;
    bool giveweapon;
    s32 temp_a0;
    struct s_mp_weapon_set* weapon_set;

    if ((weapon->flags & PROPFLAG_ASSIGNEDTOCHR))
    {
        chr = chrFindByLiteralId(weapon->pad);

        if (chr && chr->prop && chr->model)
        {
            if (cheatIsActive(CHEAT_ENEMY_ROCKETS))
            {
                switch ((s8)weapon->weaponnum)
                {
                    case ITEM_KNIFE:
                    case ITEM_THROWKNIFE:
                    case ITEM_WPPK:
                    case ITEM_WPPKSIL:
                    case ITEM_TT33:
                    case ITEM_SKORPION:
                    case ITEM_AK47:
                    case ITEM_UZI:
                    case ITEM_MP5K:
                    case ITEM_MP5KSIL:
                    case ITEM_SPECTRE:
                    case ITEM_M16:
                    case ITEM_FNP90:
                    case ITEM_SHOTGUN:
                    case ITEM_AUTOSHOT:
                    case ITEM_SNIPERRIFLE:
                    case ITEM_RUGER:
                    case ITEM_GOLDENGUN:
                    case ITEM_SILVERWPPK:
                    case ITEM_GOLDWPPK:
                    case ITEM_LASER:
                    case ITEM_WATCHLASER:
                    case ITEM_REMOTEMINE:
                    case ITEM_TRIGGER:
                    case ITEM_TASER:
                        weapon->weaponnum = ITEM_ROCKETLAUNCH;
                        weapon->obj = PROP_CHRROCKETLAUNCH;
                        weapon->extrascale = 256;
                        break;
                }
            }

            weaponLoadProjectileModels((s8)weapon->weaponnum);
            sub_GAME_7F052030(weapon, chr);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakeweaponobj: no chr number %d for obj number %d!\n",weapon->pad, cmdindex + 1);
        }
        #endif
    }
    else
    {
        hastoken = 1;
        giveweapon = 1;

        if (getPlayerCount() >= 2)
        {
            lastmpweaponnum = -1;

            switch ((u8)weapon->weaponnum)
            {
                case ITEM_UNARMED + 0xF0:
                case ITEM_FIST + 0xF0:
                case ITEM_KNIFE + 0xF0:
                case ITEM_THROWKNIFE + 0xF0:
                case ITEM_WPPK + 0xF0:
                case ITEM_WPPKSIL + 0xF0:
                case ITEM_TT33 + 0xF0:
                case ITEM_SKORPION + 0xF0:
                    weapon_set = getPtrMPWeaponSetData();

                    temp_a0 = (u8)weapon->weaponnum - 0xF0;
                    lastmpweaponnum = temp_a0;

                    weapon->weaponnum = weapon_set[temp_a0].itemID;
                    weapon->obj = weapon_set[temp_a0].propID;
#if defined(VERSION_EU)
                    weapon->extrascale = (weapon_set[temp_a0].size16);
#else
                    weapon->extrascale = (weapon_set[temp_a0].size * 256.0f);
#endif

                    giveweapon = weapon_set[temp_a0].allowpickup;

                    break;

                case ITEM_TOKEN:

                    hastoken = 1;
                    giveweapon = 1;

                    if (get_scenario() != SCENARIO_TLD)
                    {
                        giveweapon = 0;
                    }
                    break;
            }
        }

        if ((weapon->weaponnum != ITEM_UNARMED) && giveweapon)
        {
            weaponLoadProjectileModels(weapon->weaponnum);
            domakedefaultobj(arg0, (struct ObjectRecord*)weapon, cmdindex);
        }
    }
}

//i should be object hat
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex)
{
    if (hat->flags & PROPFLAG_ASSIGNEDTOCHR) {
        ChrRecord* chr = chrFindByLiteralId(hat->pad);
#ifdef NATIVE_PORT
        hatTracePrintf(
            "cmd=%d obj=%d pad=%d flags=0x%08x flags2=0x%08x assigned=1 chr=%p chrnum=%d chr_prop=%p chr_model=%p",
            cmdindex,
            hat->obj,
            hat->pad,
            (unsigned int)hat->flags,
            (unsigned int)hat->flags2,
            (void *)chr,
            chr != NULL ? chr->chrnum : -1,
            chr != NULL ? (void *)chr->prop : NULL,
            chr != NULL ? (void *)chr->model : NULL);
#endif
        if (chr && chr->prop && chr->model) {
            hatAssignToChr((HatRecord *)hat, chr);
#ifdef NATIVE_PORT
            hatTracePrintf(
                "assigned cmd=%d obj=%d target_chr=%d hat_prop=%p hat_model=%p chr_hat=%p",
                cmdindex,
                hat->obj,
                chr->chrnum,
                (void *)hat->prop,
                (void *)hat->model,
                (void *)chr->handle_positiondata_hat);
#endif
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakehatobj: no chr number %d for obj number %d!\n",hat->pad, cmdindex + 1);
        }
        #endif
    } else {
#ifdef NATIVE_PORT
        hatTracePrintf(
            "cmd=%d obj=%d pad=%d flags=0x%08x flags2=0x%08x assigned=0 default_obj=1",
            cmdindex,
            hat->obj,
            hat->pad,
            (unsigned int)hat->flags,
            (unsigned int)hat->flags2);
#endif
        domakedefaultobj(arg0, hat, cmdindex);
    }
}

//i should be object key
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex)
{
    domakedefaultobj(arg0, key, cmdindex);
}


/**
 * NTSC address 0x7F002A3C.
*/
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex)
{
    struct coord3d *temp_a2;
    struct PadRecord *sp50;
    struct coord3d sp44;
    Mtxf *sp3C;
    s32 targetPad;

    domakedefaultobj(arg0, (struct ObjectRecord*)arg1, cmdindex);

    targetPad = cctvGetTargetPad(arg1);

    if (targetPad >= 0)
    {
#ifdef NATIVE_PORT
        temp_a2 = (struct coord3d *)modelGetSwitchDataSafe(
            arg1->model != NULL ? arg1->model->obj : NULL,
            0,
            sizeof(struct coord3d));
        if (temp_a2 == NULL)
        {
            return;
        }
#else
        temp_a2 = (struct coord3d*)arg1->model->obj->Switches[0]->Data;
#endif

        if (isNotBoundPad(targetPad))
        {
            sp50 = &g_CurrentSetup.pads[targetPad];
        }
        else
        {
            sp50 = (struct PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(targetPad)];
        }

        sp44.f[0] = temp_a2->f[0];
        sp44.f[1] = temp_a2->f[1];
        sp44.f[2] = temp_a2->f[2];

        mtx4RotateVecInPlace(&arg1->mtx, &sp44);

        sp3C = &arg1->unk84;

        sp44.f[0] += arg1->prop->pos.f[0];
        sp44.f[1] += arg1->prop->pos.f[1];
        sp44.f[2] += arg1->prop->pos.f[2];

        matrix_4x4_7F059908(sp3C, 0.0f, 0.0f, 0.0f, sp44.f[0] - sp50->pos.f[0], sp44.f[1] - sp50->pos.f[1], sp44.f[2] - sp50->pos.f[2], 0.0f, 1.0f, 0.0f);
        matrix_scalar_multiply(arg1->model->scale, sp3C->m[0]);

        if (arg1->convert_to_f32 == 0)
        {
            arg1->convert_to_f32 = 1;
            arg1->unkCC = (*(s32*)&arg1->unkCC * M_TAU_F) / 65536.0f;
            arg1->unkD0 = (*(s32*)&arg1->unkD0 * M_TAU_F) / 65536.0f;
            arg1->unkDC = (*(s32*)&arg1->unkDC * M_TAU_F) / 65536.0f;
            arg1->unkE8 = *(s32*)&arg1->unkE8;
        }

        arg1->unkD4 = 0;
        arg1->unkD8 = 0.0f;
        arg1->unkC8 = arg1->unkCC;
        arg1->unkC4 = atan2f(sp44.f[0] - sp50->pos.f[0], sp44.f[2] - sp50->pos.f[2]);
        arg1->timer = 0;

#ifdef NATIVE_PORT
        monitorTracePrintf(
            "kind=cctv_setup obj=%d model=%p name=%s pad=%d switch0=%p origin=(%.2f,%.2f,%.2f)",
            arg1->obj,
            (void *)arg1->model,
            (arg1->model && arg1->model->obj && arg1->model->obj->debugName) ? arg1->model->obj->debugName : "<unnamed>",
            targetPad,
            (void *)modelGetSwitchNodeSafe(arg1->model != NULL ? arg1->model->obj : NULL, 0),
            sp44.f[0], sp44.f[1], sp44.f[2]);
#endif
    }
}

void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex)
{
    s8 *beam;

    domakedefaultobj(stageID, (ObjectRecord *) autogun, cmdindex);

#ifdef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * 7.5398226f) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    autogun->unkAC = 0;
    autogun->unkB8 = -1;
    autogun->unkBC = -1;
    autogun->unkC0 = -1;
    autogun->unkC4 = NULL;
    autogun->unkC8 = NULL;
    autogun->unk90 = 0.0f;
    autogun->unk94 = 0.0f;
    autogun->rot_related = 0.0f;
    autogun->unk9C = 0.0f;
    autogun->unkA0 = 0.0f;
    autogun->unk98 = 0.0f;
    autogun->unkB0 = 0.0f;
    autogun->unkB4 = 0.0f;

#ifndef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * M_TAU_F) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    beam          = mempAllocBytesInBank(0x30U, MEMPOOL_STAGE);
    autogun->beam = (struct beam *)beam;
    *beam = -1;

    autogun->is_active = FALSE;
    autogun->unkD4 = 0.0f;

    if (autogun->padID >= 0)
    {
        s32 stack1;
        f32 xdiff;
        f32 ydiff;
        f32 zdiff;
        PadRecord *pad;
        PropRecord *prop;

        if (autogun->padID < 0x2710)
        {
            if (1);
            pad = &g_CurrentSetup.pads[autogun->padID];
        }
        else
        {
            pad = (struct PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(autogun->padID)];
        }

        prop = autogun->prop;

        xdiff = pad->pos.x - prop->pos.x;
        ydiff = pad->pos.y - prop->pos.y;
        zdiff = pad->pos.z - prop->pos.z;

        autogun->rot_related = atan2f(xdiff, zdiff);
        autogun->unk98 = atan2f(ydiff, sqrtf((xdiff * xdiff) + (zdiff * zdiff)));
    }
}


//i should be object rack
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex)
{
    domakedefaultobj(arg0, rack, cmdindex);
}


void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex)
{
    MonitorRecord *record;
    s32 unused;
    s32 modelnum;
    ObjectRecord *owner;
    PropRecord *prop;
    f32 scale;

    monitor->Monitor = g_MonitorAnimController;
    record = &monitor->Monitor;
    monitorSetImageByNum(&monitor->Monitor, monitor->ImageNum);

#ifdef NATIVE_PORT
    monitorTracePrintf(
        "kind=monitor_setup obj=%d pad=%d image=%d ownerOffset=%d ownerPart=%d flags=0x%08x flags2=0x%08x",
        monitor->obj,
        monitor->pad,
        monitor->ImageNum,
        monitor->OwnerOffset,
        monitor->OwnerPart,
        (unsigned int)monitor->flags,
        (unsigned int)monitor->flags2);
#endif

    if (monitor->pad < 0 && (monitor->flags & PROPFLAG_INSIDEANOTHEROBJ) == 0)
    {
        modelnum = monitor->obj;
        owner = (struct ObjectRecord *)setupGetPtrToCommandByIndex(cmdindex + monitor->OwnerOffset);

        modelLoad(modelnum);

        scale = monitor->extrascale * (1.0f / 256.0f);
        monitor->damage = *(s32*)&monitor->damage / M_U16_MAX_VALUE_F;

        if (getPlayerCount() >= 2)
        {
            monitor->state |= PROPSTATE_RESPAWN;
        }

        prop = objInitWithAutoModel((ObjectRecord*)monitor);
        monitor->embedment = embedmentAllocate();

        if (prop && monitor->embedment)
        {
            monitor->runtime_bitflags |= RUNTIMEBITFLAG_EMBEDDED;
            modelSetScale(monitor->model, monitor->model->scale * scale);
            monitor->model->attachedto = owner->model;

#ifdef NATIVE_PORT
            monitor->model->attachedto_objinst = modelGetSwitchNodeSafe(
                owner != NULL && owner->model != NULL ? owner->model->obj : NULL,
                monitor->OwnerPart);
#else
            if (monitor->OwnerPart == 0)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[0];
            }
            else if (monitor->OwnerPart == 1)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[1];
            }
            else if (monitor->OwnerPart == 2)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[2];
            }
            else
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[3];;
            }
#endif

            if (monitor->model->attachedto_objinst != NULL)
            {
                chrpropReparent(prop, owner->prop);
                matrix_4x4_set_rotation_around_x(0.36651915f, (Mtxf*)&monitor->embedment->matrix);
                matrix_scalar_multiply(monitor->model->scale / owner->model->scale, (f32*)&monitor->embedment->matrix);
            }
        }
    }
    else
    {
        domakedefaultobj(stageID, (ObjectRecord*)monitor, cmdindex);
    }

    if ((monitor->flags & PROPFLAG_MONITOR_RENDERPOSTBG) && monitor->prop)
    {
        monitor->prop->flags |= PROPFLAG_RENDERPOSTBG;
    }
}


void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex)
{
    monitor->Monitor[0] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[0], monitor->ImageNums[0]);

    monitor->Monitor[1] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[1], monitor->ImageNums[1]);

    monitor->Monitor[2] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[2], monitor->ImageNums[2]);

    monitor->Monitor[3] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[3], monitor->ImageNums[3]);

#ifdef NATIVE_PORT
    monitorTracePrintf(
        "kind=multi_monitor_setup obj=%d pad=%d images=%u,%u,%u,%u flags=0x%08x flags2=0x%08x",
        monitor->obj,
        monitor->pad,
        monitor->ImageNums[0],
        monitor->ImageNums[1],
        monitor->ImageNums[2],
        monitor->ImageNums[3],
        (unsigned int)monitor->flags,
        (unsigned int)monitor->flags2);
#endif

    domakedefaultobj(stageID, (struct ObjectRecord *)monitor, cmdindex);

#ifdef NATIVE_PORT
    monitorTracePrintf(
        "kind=multi_monitor_spawned obj=%d pad=%d prop=%p model=%p modelnum=%d state=0x%04x",
        monitor->obj,
        monitor->pad,
        (void *)monitor->prop,
        (void *)monitor->model,
        monitor->obj,
        (unsigned int)monitor->state);
#endif
}

void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4)
{
    StandTile *sp4C;
    struct coord3d normal;
    s32 padding;
    struct coord3d center;
    StandTile *sp2C;
    f32 scale;

    sub_GAME_7F001BD4(arg0, &center);
    sp2C = (StandTile *)arg0->stan;

    if (walkTilesBetweenPoints_NoCallback(&sp2C, arg0->pos.f[0], arg0->pos.f[2], center.f[0], center.f[2]) == 0)
    {
        sp2C = (StandTile *)arg0->stan;
        center.f[0] = arg0->pos.f[0];
        center.f[1] = arg0->pos.f[1];
        center.f[2] = arg0->pos.f[2];
    }

    normal.f[0] = (arg0->up.f[1] * arg0->look.f[2]) - (arg0->up.f[2] * arg0->look.f[1]);
    normal.f[1] = (arg0->up.f[2] * arg0->look.f[0]) - (arg0->up.f[0] * arg0->look.f[2]);
    normal.f[2] = (arg0->up.f[0] * arg0->look.f[1]) - (arg0->up.f[1] * arg0->look.f[0]);

    scale = 1.0f / sqrtf(((normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1])) + (normal.f[2] * normal.f[2]));
    sp4C = sp2C;

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg3->f[0] = center.f[0] + (normal.f[0] * 50.0f);
    arg3->f[1] = center.f[1];
    arg3->f[2] = center.f[2] + (normal.f[2] * 50.0f);


    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg3->f[0], arg3->f[2]);

    if (1);

    *arg1 = (s32) sp4C->room;
    sp4C = sp2C;

    arg4->f[0] = center.f[0] - (normal.f[0] * 50.0f);
    arg4->f[1] = center.f[1];
    arg4->f[2] = center.f[2] - (normal.f[2] * 50.0f);

    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg4->f[0], arg4->f[2]);

    if (1);

    *arg2 = (s32) sp4C->room;

    if (*arg2 == *arg1)
    {
        *arg2 = -1;
    }
}


extern f32 g_DoorScale;
/**
 *
 * NTSC ADDRESS: 7F003480
 * PAL ADDRESS: 7F0033F0
 * Perfect Dark: void setupCreateDoor(struct doorobj *door, s32 cmdindex)
*/
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2)
{
    s32 padding; // no sp
    s32 modelnum;
    struct BoundPadRecord *pad;
    StandTile *sp1C8_stan;
    PropRecord *prop;
    struct coord3d sp1B8;
    s32 portalnum; //sp1b4
    s32 sp1B0;
    s32 sp1AC;
    struct coord3d sp1A0;
    struct coord3d sp194;
    struct PortalMetric sp180;
    struct ModelRoData_BoundingBoxRecord *temp_v0;
    struct coord3d sp170;
    StandTile *sp16C;
    Mtxf sp12C;
    f32 temp_f2; // no sp
    ModelFileHeader *sp124;
    struct coord3d sp118;                           /* compiler-managed */
    StandTile *sp114_stan;
    Mtxf spD4;
    struct coord3d spC8;
    Mtxf sp88;
    struct coord3d sp7C;
    struct bbox bb2;
    f32 pad_x_extent;
    f32 pad_y_extent;
    f32 pad_z_extent;
    f32 model_x_extent;
    f32 model_y_extent;
    f32 model_z_extent;
    f32 xscale;
    f32 yscale;
    f32 zscale;
    f32 scale;
    //StandTile *stan;
    u8 *padding2;

    modelnum = door->obj;

    portalnum = -1;
    sp1B0 = -1;
    sp1AC = -1;

    modelLoad(modelnum);

    pad = &(g_CurrentSetup.boundpads[door->pad]);

    if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
    {
        sub_GAME_7F00324C(pad, &sp1B0, &sp1AC, &sp1A0, &sp194);

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) && (sp1B0 >= 0) && (sp1AC >= 0))
        {
            portalnum = sub_GAME_7F0B9CC8(sp1B0, sp1AC, &sp1A0, &sp194);
        }
    }

    if (g_DoorScale != 1.0f)
    {
        if (portalnum >= 0)
        {
            sub_GAME_7F0B96CC(portalnum, &sp180);
            sp180.min *= get_room_data_float2();

            temp_f2 = (pad->pos.f[0] * sp180.normal.f[0]) + (pad->pos.f[1] * sp180.normal.f[1]) + (pad->pos.f[2] * sp180.normal.f[2]);

            if (g_DoorScale < 1.0f)
            {
                temp_f2 = (temp_f2 - sp180.min) * (1.0f - g_DoorScale);
                sp170.f[0] = pad->pos.f[0] - (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] - (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] - (sp180.normal.f[2] * temp_f2);
            }
            else
            {
                temp_f2 = (temp_f2 - sp180.min) * (g_DoorScale - 1.0f);
                sp170.f[0] = pad->pos.f[0] + (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] + (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] + (sp180.normal.f[2] * temp_f2);
            }

            sp16C = pad->stan;
            if (walkTilesBetweenPoints_NoCallback(&sp16C, pad->pos.f[0], pad->pos.f[2], sp170.f[0], sp170.f[2]) != 0)
            {
                pad->stan = sp16C;
                pad->pos.f[0] = sp170.f[0];
                pad->pos.f[1] = sp170.f[1];
                pad->pos.f[2] = sp170.f[2];
                pad->bbox.xmin *= g_DoorScale;
                pad->bbox.xmax *= g_DoorScale;
            }
 #ifdef DEBUG
            else
            {
                osSyncPrintf("volume for door object number %d did not have depth changed!\n",arg2 + 1);
            }
            #endif
        }
        else
        {
            pad->bbox.xmin *= g_DoorScale;
            pad->bbox.xmax *= g_DoorScale;
        }
    }

    if (sub_GAME_7F056850(&pad->pos, pad->stan, 0.0f, &sp1B8, &sp1C8_stan) != 0)
    {
        matrix_4x4_7F059908(&sp12C, 0, 0, 0, -pad->look.f[0], -pad->look.f[1], -pad->look.f[2], pad->up.f[0], pad->up.f[1], pad->up.f[2]);
        sp124 = PitemZ_entries[modelnum].header;
        sp114_stan = sp1C8_stan;

        bb2.zmax = pad->bbox.xmin;
        bb2.zmin = pad->bbox.xmax; //78
        bb2.ymax = pad->bbox.ymin; //74
        bb2.ymin = pad->bbox.ymax; //70
        bb2.xmax = pad->bbox.zmin; //6c
        bb2.xmin = pad->bbox.zmax; //68

        matrix_4x4_set_rotation_around_x(M_HALF_PI, &spD4);
        matrix_4x4_set_rotation_around_z(M_HALF_PI, &sp88);
        matrix_4x4_multiply_in_place(&sp88, &spD4);
        matrix_4x4_multiply_in_place(&sp12C, &spD4);
        sub_GAME_7F001BD4(pad, &sp118);

        temp_v0 = (struct ModelRoData_BoundingBoxRecord *)sp124->RootNode->Child->Data;

        pad_x_extent = bb2.ymin - bb2.ymax;
        pad_y_extent = bb2.xmin - bb2.xmax;
        pad_z_extent = bb2.zmin - bb2.zmax;
        model_x_extent = temp_v0->Bounds.xmax - temp_v0->Bounds.xmin;
        model_y_extent = temp_v0->Bounds.ymax - temp_v0->Bounds.ymin;
        model_z_extent = temp_v0->Bounds.zmax - temp_v0->Bounds.zmin;

        if (pad_x_extent < 0.0f) pad_x_extent = -pad_x_extent;
        if (pad_y_extent < 0.0f) pad_y_extent = -pad_y_extent;
        if (pad_z_extent < 0.0f) pad_z_extent = -pad_z_extent;
        if (model_x_extent < 0.0f) model_x_extent = -model_x_extent;
        if (model_y_extent < 0.0f) model_y_extent = -model_y_extent;
        if (model_z_extent < 0.0f) model_z_extent = -model_z_extent;

        if ((model_x_extent <= 0.000001f) || (model_y_extent <= 0.000001f) || (model_z_extent <= 0.000001f))
        {
            xscale = 1.0f;
            yscale = 1.0f;
            zscale = 1.0f;
        }
        else
        {
            xscale = pad_x_extent / model_x_extent;
            yscale = pad_y_extent / model_y_extent;
            zscale = pad_z_extent / model_z_extent;
        }

#ifdef NATIVE_PORT
        doorSetupTracePrintf(
            "obj=%d type=%u pad=%u padExtents=(%.3f,%.3f,%.3f) modelExtents=(%.3f,%.3f,%.3f) fit=(%.3f,%.3f,%.3f) bboxPad=(x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f) bboxModel=(x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f)",
            door->obj,
            (unsigned int)door->doorType,
            (unsigned int)door->pad,
            pad_x_extent,
            pad_y_extent,
            pad_z_extent,
            model_x_extent,
            model_y_extent,
            model_z_extent,
            xscale,
            yscale,
            zscale,
            bb2.xmin,
            bb2.xmax,
            bb2.ymin,
            bb2.ymax,
            bb2.zmin,
            bb2.zmax,
            temp_v0->Bounds.xmin,
            temp_v0->Bounds.xmax,
            temp_v0->Bounds.ymin,
            temp_v0->Bounds.ymax,
            temp_v0->Bounds.zmin,
            temp_v0->Bounds.zmax);
#endif

        if ((xscale <= 0.000001f) || (yscale <= 0.000001f) || (zscale <= 0.000001f))
        {
            #ifdef DEBUG
            osSyncPrintf("Scale warning: door object number %d has a small scale: %f,%f,%f\n",arg2 +1, xscale,yscale,zscale);
            #endif
            xscale =
                yscale =
                zscale = 1.0f;
        }

        /* Match the generic prop path: keep the uniform fit in model->scale
         * and leave only the residual anisotropic fit in the basis matrix.
         * Doors already get their base Pitem scale in doorInit, so applying
         * the full fitted scale to both the matrix and model scale would
         * multiply the visible size twice. */
        scale = xscale;

        if (scale < yscale)
        {
            scale = yscale;
        }

        if (scale < zscale)
        {
            scale = zscale;
        }

        if (scale > 0.000001f)
        {
            xscale /= scale;
            yscale /= scale;
            zscale /= scale;
        }
        else
        {
            scale = 1.0f;
        }

        matrix_column_1_scalar_multiply(xscale, spD4.m[0]);
        matrix_column_2_scalar_multiply(yscale, spD4.m[0]);
        matrix_column_3_scalar_multiply_2(zscale, spD4.m[0]);

        spC8.f[0] = sp118.f[0];
        spC8.f[1] = sp118.f[1];
        spC8.f[2] = sp118.f[2];

        if (!(door->flags2 & 1))
        {
            if (walkTilesBetweenPoints_NoCallback(&sp114_stan, sp1B8.f[0], sp1B8.f[2], sp118.f[0], sp118.f[2]) != 0)
            {
                sp1C8_stan = sp114_stan;
            }
            else
            {
                sp118.f[0] = sp1B8.f[0];
                sp118.f[2] = sp1B8.f[2];

                if (!(door->flags & 0x1000)) // prop flag PROPFLAG_00001000 "Absolute Position"
                {
                    #ifdef DEBUG
                    osSyncPrintf("door object number %d not positioned correctly!\n",arg2 +1);
                    #endif

                }
            }
        }
        else
        {
            sp118.f[0] = sp1B8.f[0];
            sp118.f[1] = sp1B8.f[1];
            sp118.f[2] = sp1B8.f[2];
        }

        if ((door->doorType == 4) || (door->doorType == 8))
        {
            sp7C.f[0] = pad->look.f[0] * (bb2.xmin - bb2.xmax);
            sp7C.f[1] = pad->look.f[1] * (bb2.xmin - bb2.xmax);
            sp7C.f[2] = pad->look.f[2] * (bb2.xmin - bb2.xmax);
        }
        else
        {
            sp7C.f[0] = pad->up.f[0] * (bb2.ymax - bb2.ymin);
            sp7C.f[1] = pad->up.f[1] * (bb2.ymax - bb2.ymin);
            sp7C.f[2] = pad->up.f[2] * (bb2.ymax - bb2.ymin);
        }

        // These values are stored in the setup files as integers, but at
		// runtime they are floats. Hence reading a "float" as an integer,
		// converting it to a float and writing it back to the same property.
		door->maxFrac = *(s32 *) &door->maxFrac / 65536.0f;
		door->perimFrac = *(s32 *) &door->perimFrac / 65536.0f;
#if defined(VERSION_EU)
        door->accel = (*(s32 *) &door->accel) * 1.2f / 65536.0f;
		door->decel = (*(s32 *) &door->decel) * 1.2f / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) * 1.2f / 65536.0f;
#else
		door->accel = (*(s32 *) &door->accel) / 65536.0f;
		door->decel = (*(s32 *) &door->decel) / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) / 65536.0f;
#endif

        prop = doorInit(door, &sp118, &spD4, sp1C8_stan, &sp7C, &spC8, scale);
        if (door->flags & PROPFLAG_CULL_BEHIND_DOOR)
        {
            door->portalNumber = portalnum;
            if ((portalnum >= 0) && (door->openPosition == 0.0f))
            {
                doorDeactivatePortal(door);
            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No portal for door object number %d ",arg2 + 1);
            }
            #endif
        }

        prop->rooms[0] = prop->stan->room;
        chrpropRegisterRoom(prop, prop->stan->room);
        prop->rooms[1] = 0xFFU;
        prop->rooms[2] = 0xFFU;

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
        {
            if (sp1B0 != prop->stan->room)
            {
                if (sp1B0 >= 0)
                {
                    prop->rooms[1] = sp1B0;
                    chrpropRegisterRoom(prop, sp1B0);
                }
            }
            else if (sp1AC >= 0)
            {
                prop->rooms[1] = sp1AC;
                chrpropRegisterRoom(prop, sp1AC);
            }

            if (prop->rooms[1] != 0xff && 1)
            {
                if (!prop->stan->room)
                {
                    #ifdef DEBUG
                        osSyncPrintf("3 rooms for door object number %d\n",arg2 + 1);
                    #endif
                }

            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No 2nd room for door object number %d\n",arg2 + 1);
            }
            #endif
        }

        chrpropActivate(prop);
        chrpropEnable(prop);

        if (door->linkedDoorOffset != 0)
        {
            door->linkedDoor = (struct DoorRecord *)setupGetPtrToCommandByIndex(door->linkedDoorOffset + arg2);
        }
    }
    else
    {
        door->prop = NULL;
        #ifdef DEBUG
            osSyncPrintf("proplvreset: prop door object number %d not reset!\n",arg2 + 1);
        #endif
    }

}



#ifdef NONMATCHING
#ifdef PORT_FIXME_STUBS
#ifdef NATIVE_PORT
/**
 * PC-specific minimal setup loader.
 *
 * The setup file from ROM has a 10-entry header of 32-bit big-endian offsets
 * (N64 struct stagesetup = 10 × u32 = 40 bytes). Each offset, when added to
 * the base address, gives a pointer into the loaded file.
 *
 * Additionally, embedded struct arrays (PadRecord, BoundPadRecord, etc.) use
 * 32-bit N64 pointer fields that differ from 64-bit PC structs. The pad data
 * specifically has PadRecord entries at 44 bytes stride on N64 (3×coord3d + 2×u32)
 * vs 48+ bytes on PC (3×coord3d + 2×ptr). We read the 32-bit layout and build
 * PC-native PadRecord arrays.
 *
 * Sub-struct pointer fields within pads (plink, stan) must be resolved separately:
 * - plink: char* into the setup file → base + offset
 * - stan: resolved by init_pathtable_something() at runtime
 */
static inline u32 bswap32(u32 v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

/* Read a big-endian float from raw ROM bytes */
static inline f32 be_float(const u8 *p) {
    u32 v = bswap32(*(const u32 *)p);
    f32 f;
    memcpy(&f, &v, 4);
    return f;
}

/* Read a big-endian u16 from raw ROM bytes */
static inline u16 be_u16(const u8 *p) {
    return (u16)((p[0] << 8) | p[1]);
}

/* Read a big-endian s16 from raw ROM bytes */
static inline s16 be_s16(const u8 *p) {
    return (s16)be_u16(p);
}

/* Read a big-endian u32 from raw ROM bytes */
static inline u32 be_u32(const u8 *p) {
    return bswap32(*(const u32 *)p);
}

/* Read a big-endian s32 from raw ROM bytes */
static inline s32 be_s32(const u8 *p) {
    return (s32)be_u32(p);
}

/* --- N64 PropDef sizes in bytes (for walking raw N64 setup data) ---
 * These must match the original N64 struct sizes (32-bit pointers). */
#define N64_OBJECT_RECORD_BYTES             0x80
#define N64_MONITOR_RECORD_BYTES            0x74
#define N64_MONITOR_OWNER_OFFSET_OFFSET     0xF4
#define N64_MONITOR_OWNER_PART_OFFSET       0xF8
#define N64_MONITOR_IMAGE_NUM_OFFSET        0xFC
#define N64_MULTI_MONITOR_IMAGE_NUMS_OFFSET (N64_OBJECT_RECORD_BYTES + (N64_MONITOR_RECORD_BYTES * 4))

static s32 sizepropdef_n64_bytes(u8 type) {
    switch (type) {
        case PROPDEF_GUARD:         return 28;   /* 7 words */
        case PROPDEF_DOOR:          return 256;  /* 64 words */
        case PROPDEF_DOOR_SCALE:    return 8;    /* 2 words */
        case PROPDEF_PROP:          return 128;  /* 32 words */
        case PROPDEF_GLASS:         return 128;  /* 32 words */
        case PROPDEF_TINTED_GLASS:  return 0x25 * 4;  /* 148 bytes */
        case PROPDEF_SAFE:          return 0x20 * 4;
        case PROPDEF_GAS_RELEASING: return 0x20 * 4;
        case PROPDEF_KEY:           return 0x21 * 4;  /* 132 bytes */
        case PROPDEF_ALARM:         return 0x20 * 4;
        case PROPDEF_CCTV:          return 0x3b * 4;  /* 236 bytes */
        case PROPDEF_MAGAZINE:      return 0x21 * 4;
        case PROPDEF_COLLECTABLE:   return 0x22 * 4;  /* 136 bytes */
        case PROPDEF_MONITOR:       return 0x40 * 4;  /* 256 bytes */
        case PROPDEF_MULTI_MONITOR: return 0x95 * 4;
        case PROPDEF_RACK:          return 0x20 * 4;
        case PROPDEF_AUTOGUN:       return 0x36 * 4;
        case PROPDEF_LINK:          return 3 * 4;
        case PROPDEF_HAT:           return 0x20 * 4;
        case PROPDEF_GUARD_ATTRIBUTE: return 3 * 4;
        case PROPDEF_SWITCH:        return 4 * 4;
        case PROPDEF_SAFE_ITEM:     return 5 * 4;
        case PROPDEF_AMMO:          return 0x2d * 4;
        case PROPDEF_ARMOUR:        return 0x22 * 4;
        case PROPDEF_TAG:           return 4 * 4;
        case PROPDEF_RENAME:        return 10 * 4;
        case PROPDEF_OBJECTIVE_START:  return 4 * 4;
        case PROPDEF_OBJECTIVE_END:    return 1 * 4;
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:      return 2 * 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:   return 2 * 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:       return 2 * 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:           return 4 * 4;
        case PROPDEF_OBJECTIVE_NULL:                 return 1 * 4;
        case PROPDEF_OBJECTIVE_ENTER_ROOM:           return 4 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: return 5 * 4;
        case PROPDEF_OBJECTIVE_COPY_ITEM:            return 1 * 4;
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:       return 4 * 4;
        case PROPDEF_LOCK_DOOR:     return 4 * 4;
        case PROPDEF_VEHICHLE:      return 0x2c * 4;
        case PROPDEF_AIRCRAFT:      return 0x2d * 4;
        case PROPDEF_TANK:          return 0x38 * 4;
        case PROPDEF_CAMERAPOS:     return 7 * 4;
        case PROPDEF_UNK41:         return 0x20 * 4;
        case PROPDEF_END:           return 4;
        default:                    return 4;   /* minimum: just the header */
    }
}

/* Convert PropDefHeaderRecord from N64 big-endian bytes */
static inline void propdef_convert_header(const u8 *src, PropDefHeaderRecord *dst) {
    dst->extrascale = be_u16(src + 0);
    dst->state = src[2];
    dst->type = src[3];
}

/* Convert ObjectRecord base (N64 offset 0x00-0x7F → PC ObjectRecord).
 * N64 layout: 128 bytes with 4-byte pointers at offsets 0x10, 0x14, 0x68, 0x6C.
 * PC layout: 144 bytes with 8-byte pointers. */
static void propdef_convert_objectrecord(const u8 *n64, ObjectRecord *pc) {
    memset(pc, 0, sizeof(ObjectRecord));
    /* Header + non-pointer fields before first pointer */
    pc->extrascale = be_u16(n64 + 0);
    pc->state = n64[2];
    pc->type = n64[3];
    pc->obj = be_s16(n64 + 4);
    pc->pad = be_s16(n64 + 6);
    pc->flags = be_u32(n64 + 8);
    pc->flags2 = be_u32(n64 + 12);
    /* N64 offsets 0x10, 0x14 = prop, model pointers — NULL on PC (runtime) */
    pc->prop = NULL;
    pc->model = NULL;
    /* Mtxf at N64 offset 0x18 (24) — 16 floats */
    for (s32 i = 0; i < 16; i++) {
        ((f32 *)&pc->mtx)[i] = be_float(n64 + 0x18 + i * 4);
    }
    /* runtime_pos at N64 offset 0x58 — 3 floats */
    pc->runtime_pos.f[0] = be_float(n64 + 0x58);
    pc->runtime_pos.f[1] = be_float(n64 + 0x5C);
    pc->runtime_pos.f[2] = be_float(n64 + 0x60);
    /* runtime_bitflags at N64 offset 0x64 */
    pc->runtime_bitflags = be_u32(n64 + 0x64);
    /* N64 offsets 0x68, 0x6C = collision, projectile pointers — NULL */
    pc->ptr_allocated_collisiondata_block = NULL;
    pc->projectile = NULL;
    /* maxdamage at N64 offset 0x70 */
    pc->maxdamage = be_float(n64 + 0x70);
    pc->damage = be_float(n64 + 0x74);
    /* shadecol/nextcol at N64 offsets 0x78, 0x7C — u8 arrays, no swap */
    memcpy(&pc->shadecol, n64 + 0x78, 4);
    memcpy(&pc->nextcol, n64 + 0x7C, 4);
}

/* --- PC sizeof for each propdef type (for buffer allocation) ---
 * Uses sizeof() for all types with struct definitions.
 * Types inheriting ObjectRecord have larger PC sizes due to 64-bit pointers. */
static s32 sizepropdef_pc_bytes(u8 type) {
    switch (type) {
        case PROPDEF_GUARD:         return (s32)sizeof(GuardRecord);
        case PROPDEF_DOOR:          return (s32)sizeof(DoorRecord);
        case PROPDEF_DOOR_SCALE:    return (s32)sizeof(GlobalDoorScaleRecord);
        case PROPDEF_PROP:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_GLASS:         return (s32)sizeof(ObjectRecord);
        case PROPDEF_COLLECTABLE:   return (s32)sizeof(WeaponObjRecord);
        case PROPDEF_KEY:           return (s32)sizeof(KeyRecord);
        case PROPDEF_TINTED_GLASS:  return (s32)sizeof(TintedGlassRecord);
        case PROPDEF_ARMOUR:        return (s32)sizeof(BodyArmourRecord);
        case PROPDEF_MONITOR:       return (s32)sizeof(MonitorObjRecord);
        case PROPDEF_MULTI_MONITOR: return (s32)sizeof(MultiMonitorObjRecord);
        case PROPDEF_AUTOGUN:       return (s32)sizeof(AutogunRecord);
        case PROPDEF_AMMO:          return (s32)sizeof(MultiAmmoCrateRecord);
        case PROPDEF_VEHICHLE:      return (s32)sizeof(VehichleRecord);
        case PROPDEF_AIRCRAFT:      return (s32)sizeof(AircraftRecord);
        case PROPDEF_TANK:          return (s32)sizeof(TankRecord);
        case PROPDEF_CCTV:          return (s32)sizeof(CCTVRecord);
        case PROPDEF_MAGAZINE:      return (s32)sizeof(AmmoCrateRecord);
        case PROPDEF_SAFE_ITEM:     return (s32)sizeof(SafeObjectRecord);
        /* ObjectRecord-only types: no specific struct, but contain pointers */
        case PROPDEF_SAFE:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_GAS_RELEASING: return (s32)sizeof(ObjectRecord);
        case PROPDEF_ALARM:         return (s32)sizeof(ObjectRecord);
        case PROPDEF_RACK:          return (s32)sizeof(ObjectRecord);
        case PROPDEF_HAT:           return (s32)sizeof(ObjectRecord);
        case PROPDEF_UNK41:         return (s32)sizeof(ObjectRecord);
        /* Types with runtime-only pointer fields that expand on 64-bit */
        case PROPDEF_TAG:           return (s32)sizeof(TagObjectRecord);
        case PROPDEF_RENAME:        return (s32)sizeof(RenameObjectRecord);
        case PROPDEF_LINK:          return (s32)sizeof(LinkRecord);
        case PROPDEF_SWITCH:        return (s32)sizeof(SwitchRecord);
        case PROPDEF_LOCK_DOOR:     return (s32)sizeof(LockDoorRecord);
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: return (s32)sizeof(struct setup_objective_text);
        case PROPDEF_OBJECTIVE_ENTER_ROOM:     return (s32)sizeof(struct criteria_roomentered);
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: return (s32)sizeof(struct criteria_deposit);
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:     return (s32)sizeof(struct criteria_picture);
        /* No-pointer types: round up to 8-byte alignment so that subsequent
         * pointer-bearing records start at an 8-byte boundary in the flat buffer. */
        case PROPDEF_GUARD_ATTRIBUTE: return 16;  /* was 12 */
        case PROPDEF_CAMERAPOS:     return ((s32)sizeof(CutsceneRecord) + 7) & ~7;  /* 28→32 */
        case PROPDEF_OBJECTIVE_START:  return (s32)sizeof(struct objective_entry);
        case PROPDEF_OBJECTIVE_END:    return 8;  /* was 4 */
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:      return 2 * 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:   return 2 * 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:       return 2 * 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:       return 2 * 4;
        case PROPDEF_OBJECTIVE_NULL:                 return 8;  /* was 4 */
        case PROPDEF_OBJECTIVE_COPY_ITEM:            return 8;  /* was 4 */
        case PROPDEF_END:           return 8;  /* was 4 */
        default:                    return 8;  /* was 4 */
    }
}

/* Convert N64 propDefs buffer to PC-native struct layout.
 * Returns a newly allocated buffer with PC-sized records, or NULL on failure.
 * The caller must free the returned buffer. */
static PropDefHeaderRecord *propdef_convert_n64_to_pc(const u8 *n64_data) {
    const u8 *src;
    u8 *dst;
    u8 type;
    s32 n64_size, pc_size;
    s32 total_pc_size = 0;
    s32 record_count = 0;

    /* First pass: compute total PC buffer size */
    src = n64_data;
    for (;;) {
        type = src[3]; /* type is u8 at byte offset 3, no swap needed */
        n64_size = sizepropdef_n64_bytes(type);
        pc_size = sizepropdef_pc_bytes(type);
        total_pc_size += pc_size;
        record_count++;
        if (type == PROPDEF_END) break;
        src += n64_size;
        if (record_count > 10000) {
            fprintf(stderr, "[SETUP-PC] ERROR: propDef record count overflow\n");
            return NULL;
        }
    }

    /* Allocate PC buffer */
    u8 *pc_buf = (u8 *)calloc(1, total_pc_size);
    if (!pc_buf) return NULL;

    /* Second pass: convert each record */
    src = n64_data;
    dst = pc_buf;
    for (s32 rec = 0; rec < record_count; rec++) {
        type = src[3];
        n64_size = sizepropdef_n64_bytes(type);
        pc_size = sizepropdef_pc_bytes(type);

        switch (type) {
            /* --- ObjectRecord-only types (N64=128, PC=sizeof(ObjectRecord)=144) --- */
            case PROPDEF_PROP:
            case PROPDEF_GLASS:
            case PROPDEF_ALARM:
            case PROPDEF_HAT:
            case PROPDEF_SAFE:
            case PROPDEF_GAS_RELEASING:
            case PROPDEF_RACK:
            case PROPDEF_UNK41:
                propdef_convert_objectrecord(src, (ObjectRecord *)dst);
                break;

            /* --- GuardRecord (N64=28, PC=sizeof(GuardRecord)=32) --- */
            case PROPDEF_GUARD: {
                GuardRecord *g = (GuardRecord *)dst;
                memset(g, 0, sizeof(GuardRecord));
                g->extrascale = be_u16(src + 0);
                g->state = src[2];
                g->type = src[3];
                g->chrnum = be_u16(src + 4);
                g->PadID = be_u16(src + 6);
                g->BodyID = be_u16(src + 8);
                g->AIListID = be_u16(src + 10);
                g->Preset = be_u16(src + 12);
                g->chrpreset1 = be_u16(src + 14);
                g->health = be_u16(src + 16);
                g->ReactionTime = be_u16(src + 18);
                g->bitflags = be_u16(src + 20);
                g->HeadID = be_s16(src + 22);
                g->Data = NULL; /* runtime */
                break;
            }

            /* --- DoorRecord (N64=256, PC=sizeof(DoorRecord)=296) --- */
            case PROPDEF_DOOR: {
                DoorRecord *d = (DoorRecord *)dst;
                memset(d, 0, sizeof(DoorRecord));
                /* ObjectRecord base */
                propdef_convert_objectrecord(src, (ObjectRecord *)d);
                /* Door-specific fields (N64 offsets relative to start) */
                d->linkedDoorOffset = be_s32(src + 0x80);
                d->maxFrac = be_float(src + 0x84);
                d->perimFrac = be_float(src + 0x88);
                d->accel = be_float(src + 0x8C);
                d->decel = be_float(src + 0x90);
                d->maxSpeed = be_float(src + 0x94);
                d->doorFlags = be_u16(src + 0x98);
                d->doorType = be_u16(src + 0x9A);
                d->keyflags = be_u32(src + 0x9C);
                d->autoCloseFrames = be_u32(src + 0xA0);
                d->doorOpenSound = be_u32(src + 0xA4);
                d->frac = be_float(src + 0xA8);
                d->unkac = be_float(src + 0xAC);
                d->unkb0 = be_float(src + 0xB0);
                d->openPosition = be_float(src + 0xB4);
                d->speed = be_float(src + 0xB8);
                d->openstate = (s8)src[0xBC];
                d->unkbd = src[0xBD];
                d->calculatedopacity = be_s16(src + 0xBE);
                d->TintDist = be_s32(src + 0xC0);
                d->CullDist = be_s16(src + 0xC4);
                d->soundType = (s8)src[0xC6];
                d->fadeTime60 = (s8)src[0xC7];
                /* N64 pointers at 0xC8, 0xCC — NULL on PC */
                d->linkedDoor = NULL;
                d->unkcc = NULL;
                /* bbox at N64 offset 0xD0 (28 bytes) */
                d->bbox.ModelNumber = be_u32(src + 0xD0);
                d->bbox.Bounds.xmin = be_float(src + 0xD4);
                d->bbox.Bounds.xmax = be_float(src + 0xD8);
                d->bbox.Bounds.ymin = be_float(src + 0xDC);
                d->bbox.Bounds.ymax = be_float(src + 0xE0);
                d->bbox.Bounds.zmin = be_float(src + 0xE4);
                d->bbox.Bounds.zmax = be_float(src + 0xE8);
                d->openedTime = be_u32(src + 0xEC);
                d->portalNumber = be_s32(src + 0xF0);
                /* N64 pointers at 0xF4, 0xF8 — NULL */
                d->openSoundState = NULL;
                d->closeSoundState = NULL;
                d->lastcalc60i = be_s32(src + 0xFC);
                break;
            }

            /* --- GlobalDoorScaleRecord (N64=8, PC=8) --- */
            case PROPDEF_DOOR_SCALE: {
                GlobalDoorScaleRecord *ds = (GlobalDoorScaleRecord *)dst;
                ds->extrascale = be_u16(src + 0);
                ds->state = src[2];
                ds->type = src[3];
                ds->Scale = be_s32(src + 4);
                break;
            }

            /* --- WeaponObjRecord / COLLECTABLE (N64=136, PC=sizeof) --- */
            case PROPDEF_COLLECTABLE: {
                WeaponObjRecord *w = (WeaponObjRecord *)dst;
                memset(w, 0, sizeof(WeaponObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)w);
                w->weaponnum = (s8)src[0x80];
                w->LinkedWeaponType = (s8)src[0x81];
                w->timer = be_s16(src + 0x82);
                w->dualweapon = NULL; /* runtime ptr at N64 0x84 */
                break;
            }

            /* --- MultiAmmoCrateRecord / AMMO (N64=180, PC=sizeof) ---
             * Tail is 13 {u16 modelnum, u16 quantity} pairs. Convert each lane
             * as a u16 (modelnum at +0, quantity at +2, 4-byte stride) so the
             * authored pairs survive intact. A generic u32 word-swap would
             * reverse the two lanes within each pair, handing out proximity
             * mines where retail gives remote. Both retail loops (collect path
             * 7F050338 and collectability gate 7F0509B8) read slots[].quantity
             * via this 4-byte stride; the quantities[] overlay is never read
             * for gameplay. */
            case PROPDEF_AMMO: {
                MultiAmmoCrateRecord *m = (MultiAmmoCrateRecord *)dst;
                memset(m, 0, sizeof(MultiAmmoCrateRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                for (s32 i = 0; i < AMMOTYPE_GLOBAL_MAX; i++) {
                    m->slots[i].modelnum = be_u16(src + 0x80 + i * 4);
                    m->slots[i].quantity = be_u16(src + 0x82 + i * 4);
                }
                if (ge_dbg_enabled()) {
                    fprintf(stderr, "[SETUP-PC] AMMO crate pad=%d slots:", m->pad);
                    for (s32 i = 0; i < AMMOTYPE_GLOBAL_MAX; i++) {
                        fprintf(stderr, " %04x/%u", m->slots[i].modelnum, m->slots[i].quantity);
                    }
                    fprintf(stderr, "\n");
                }
                break;
            }

            /* --- KeyRecord (N64=132, PC=sizeof) --- */
            case PROPDEF_KEY: {
                KeyRecord *k = (KeyRecord *)dst;
                memset(k, 0, sizeof(KeyRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)k);
                /* Extra field at N64 0x80: union {s8 keyID; u32 keyflags} */
                k->keyflags = be_u32(src + 0x80);
                break;
            }

            /* --- TintedGlassRecord (N64=148, PC=sizeof) ---
             * unk90 is stored as raw fixed-point bits until the spawn pass,
             * matching the original loader's later fixed16 conversion. */
            case PROPDEF_TINTED_GLASS: {
                TintedGlassRecord *tg = (TintedGlassRecord *)dst;
                s32 raw_unk90;

                memset(tg, 0, sizeof(TintedGlassRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)tg);
                tg->TintDist = be_s32(src + 0x80);
                tg->CullDist = be_s32(src + 0x84);
                tg->calculatedopacity = be_s32(src + 0x88);
                tg->portalnum = be_s32(src + 0x8C);
                raw_unk90 = be_s32(src + 0x90);
                memcpy(&tg->unk90, &raw_unk90, sizeof(raw_unk90));
                break;
            }

            /* --- CCTVRecord (N64=236, PC=sizeof(CCTVRecord)) ---
             * Native ObjectRecord is wider, and the authored target pad at N64
             * 0x80 is not the ObjectRecord home pad. Convert each setup field
             * into the native offsets instead of using the generic extra-copy. */
            case PROPDEF_CCTV: {
                CCTVRecord *c = (CCTVRecord *)dst;
                s32 raw_unkcc;
                s32 raw_unkd0;
                s32 raw_unkdc;
                s32 raw_unke8;

                memset(c, 0, sizeof(CCTVRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)c);
                c->cctv_pad = be_s32(src + 0x80);
                for (s32 i = 0; i < 16; i++) {
                    ((f32 *)&c->unk84)[i] = be_float(src + 0x84 + i * 4);
                }
                c->unkC4 = be_float(src + 0xC4);
                c->unkC8 = be_float(src + 0xC8);
                raw_unkcc = be_s32(src + 0xCC);
                raw_unkd0 = be_s32(src + 0xD0);
                memcpy(&c->unkCC, &raw_unkcc, sizeof(raw_unkcc));
                memcpy(&c->unkD0, &raw_unkd0, sizeof(raw_unkd0));
                c->unkD4 = be_s32(src + 0xD4);
                c->unkD8 = be_float(src + 0xD8);
                raw_unkdc = be_s32(src + 0xDC);
                memcpy(&c->unkDC, &raw_unkdc, sizeof(raw_unkdc));
                c->timer = be_s32(src + 0xE0);
                c->convert_to_f32 = be_s32(src + 0xE4);
                raw_unke8 = be_s32(src + 0xE8);
                memcpy(&c->unkE8, &raw_unke8, sizeof(raw_unke8));
                break;
            }

            /* --- BodyArmourRecord (N64=136, PC=sizeof) --- */
            case PROPDEF_ARMOUR: {
                BodyArmourRecord *ba = (BodyArmourRecord *)dst;
                memset(ba, 0, sizeof(BodyArmourRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)ba);
                ba->initialamount = be_float(src + 0x80);
                ba->amount = be_float(src + 0x84);
                break;
            }

            /* --- MonitorObjRecord (N64=256, PC=sizeof(MonitorObjRecord)) ---
             * MonitorRecord contains runtime pointers which expand on 64-bit PC.
             * setupSingleMonitor() initializes the animation record at runtime, so
             * only the non-runtime owner/image fields should be imported here. */
            case PROPDEF_MONITOR: {
                MonitorObjRecord *m = (MonitorObjRecord *)dst;
                memset(m, 0, sizeof(MonitorObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                m->OwnerOffset = be_s32(src + N64_MONITOR_OWNER_OFFSET_OFFSET);
                m->OwnerPart = be_s32(src + N64_MONITOR_OWNER_PART_OFFSET);
                m->ImageNum = be_s32(src + N64_MONITOR_IMAGE_NUM_OFFSET);
                break;
            }

            /* --- MultiMonitorObjRecord (N64=596, PC=sizeof(MultiMonitorObjRecord)) ---
             * The four MonitorRecord entries are runtime animation state; preserve
             * the setup image selectors and let setupMultiMonitor() build the records. */
            case PROPDEF_MULTI_MONITOR: {
                MultiMonitorObjRecord *m = (MultiMonitorObjRecord *)dst;
                memset(m, 0, sizeof(MultiMonitorObjRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)m);
                memcpy(m->ImageNums, src + N64_MULTI_MONITOR_IMAGE_NUMS_OFFSET, sizeof(m->ImageNums));
                break;
            }

            /* --- TagObjectRecord (N64=16, PC=24): 2 runtime pointers expand --- */
            case PROPDEF_TAG: {
                TagObjectRecord *t = (TagObjectRecord *)dst;
                /* memset not needed — calloc'd buffer */
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                t->ID = be_u16(src + 4);
                t->OffsetToObj = be_s16(src + 6);
                t->NextTag = NULL;     /* N64 offset 8, runtime */
                t->TaggedObject = NULL; /* N64 offset 12, runtime */
                break;
            }

            /* --- LinkRecord (N64=12, PC=32): 3 union/ptr fields expand --- */
            case PROPDEF_LINK: {
                LinkRecord *l = (LinkRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                l->Index1 = be_s32(src + 4);  /* union with PropRecord* first */
                l->Index2 = be_s32(src + 8);  /* union with PropRecord* second */
                l->next = NULL;               /* not in N64 data */
                break;
            }

            /* --- SwitchRecord = LinkRecord (N64=16, PC=32) --- */
            case PROPDEF_SWITCH: {
                SwitchRecord *s = (SwitchRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                s->Index1 = be_s32(src + 4);
                s->Index2 = be_s32(src + 8);
                s->next = NULL;  /* N64 offset 12 */
                break;
            }

            /* --- LockDoorRecord (N64=16, PC=32): 3 pointers expand --- */
            case PROPDEF_LOCK_DOOR: {
                LockDoorRecord *ld = (LockDoorRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                ld->Index1 = be_s32(src + 4);  /* union with DoorRecord* door */
                ld->Index2 = be_s32(src + 8);  /* union with ObjectRecord* lock */
                ld->next = NULL;               /* N64 offset 12, runtime */
                break;
            }

            /* --- RenameObjectRecord (N64=40, PC=48): 1 pointer at end --- */
            case PROPDEF_RENAME: {
                RenameObjectRecord *r = (RenameObjectRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                r->TagID = be_s32(src + 4);
                r->unk08 = be_s32(src + 8);
                r->unk0c = be_s32(src + 12);
                r->unk10 = be_s32(src + 16);
                r->unk14 = be_s32(src + 20);
                r->unk18 = be_s32(src + 24);
                r->unk1c = be_s32(src + 28);
                r->unk20 = be_s32(src + 32);
                r->renobj = NULL; /* N64 offset 36, runtime */
                break;
            }

            /* --- WatchMenuObjectiveTextRecord (N64=16, PC=24 as setup_objective_text):
             * The spawn code casts to setup_objective_text which has a next pointer --- */
            case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: {
                struct setup_objective_text *ot = (struct setup_objective_text *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                ot->unk04 = be_s32(src + 4);
                ot->unk08 = be_s32(src + 8);
                ot->next = NULL; /* N64 offset 12, runtime */
                break;
            }

            /* --- SafeObjectRecord (N64=20, PC=40): 4 pointer/union fields --- */
            case PROPDEF_SAFE_ITEM: {
                SafeObjectRecord *si = (SafeObjectRecord *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                si->Index1 = be_s32(src + 4);  /* union with item ptr */
                si->Index2 = be_s32(src + 8);  /* union with safe ptr */
                si->Index3 = be_s32(src + 12); /* union with door ptr */
                si->next = NULL;               /* N64 offset 16, runtime */
                break;
            }

            /* --- Objective types with next pointers --- */
            case PROPDEF_OBJECTIVE_PHOTOGRAPH: {
                struct criteria_picture *cp = (struct criteria_picture *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cp->tag_id = be_s32(src + 4);
                cp->flag = be_s32(src + 8);
                cp->next = NULL; /* N64 offset 12, runtime */
                break;
            }

            case PROPDEF_OBJECTIVE_ENTER_ROOM: {
                struct criteria_roomentered *cr = (struct criteria_roomentered *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cr->pad = be_u32(src + 4);
                cr->status = be_u32(src + 8);
                cr->next = NULL; /* N64 offset 12, runtime */
                break;
            }

            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: {
                struct criteria_deposit *cd = (struct criteria_deposit *)dst;
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                cd->weaponnum = be_s32(src + 4);
                cd->padid = be_s32(src + 8);
                cd->flag = be_s32(src + 12);
                cd->next = NULL; /* N64 offset 16, runtime */
                break;
            }

            /* --- Simple non-expanding types: byte-swap u32 words --- */
            case PROPDEF_GUARD_ATTRIBUTE:
            case PROPDEF_CAMERAPOS:
            case PROPDEF_OBJECTIVE_START:
            case PROPDEF_OBJECTIVE_END:
            case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
            case PROPDEF_OBJECTIVE_FAIL_CONDITION:
            case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            case PROPDEF_OBJECTIVE_NULL:
            case PROPDEF_OBJECTIVE_COPY_ITEM: {
                /* These types have no pointers and same size on N64/PC. */
                propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                for (s32 w = 1; w < n64_size / 4; w++) {
                    ((u32 *)dst)[w] = be_u32(src + w * 4);
                }
                break;
            }

            /* --- VehichleRecord (N64=176, PC=sizeof(VehichleRecord)) ---
             * Extension has 3 pointers: ailist, path, Sound */
            case PROPDEF_VEHICHLE: {
                VehichleRecord *v = (VehichleRecord *)dst;
                memset(v, 0, sizeof(VehichleRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)v);
                v->ailist = (AIRecord *)(uintptr_t)be_u32(src + 0x80);  /* setup AI-list id, resolved during spawn */
                v->aioffset = be_u16(src + 0x84);
                v->aireturnlist = be_s16(src + 0x86);
                v->speed = be_float(src + 0x88);
                v->wheelxrot = be_float(src + 0x8C);
                v->wheelyrot = be_float(src + 0x90);
                v->speedaim = be_float(src + 0x94);
                v->speedtime60 = be_float(src + 0x98);
                v->turnrot60 = be_float(src + 0x9C);
                v->roty = be_float(src + 0xA0);
                v->path = NULL;   /* N64 0xA4: runtime pointer */
                v->nextstep = be_s32(src + 0xA8);
                v->Sound = NULL;  /* N64 0xAC: runtime pointer */
                break;
            }

            /* --- AircraftRecord (N64=180, PC=sizeof(AircraftRecord)) ---
             * Extension has 3 pointers: ailist, path, Sound */
            case PROPDEF_AIRCRAFT: {
                AircraftRecord *a = (AircraftRecord *)dst;
                memset(a, 0, sizeof(AircraftRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)a);
                a->ailist = (AIRecord *)(uintptr_t)be_u32(src + 0x80);  /* setup AI-list id, resolved during spawn */
                a->aioffset = be_u16(src + 0x84);
                a->aireturnlist = be_s16(src + 0x86);
                a->rotoryrot = be_float(src + 0x88);
                a->rotaryspeed = be_float(src + 0x8C);
                a->rotaryspeedaim = be_float(src + 0x90);
                a->rotaryspeedtime = be_float(src + 0x94);
                a->speed = be_float(src + 0x98);
                a->speedaim = be_float(src + 0x9C);
                a->speedtime60 = be_float(src + 0xA0);
                a->yrot = be_float(src + 0xA4);
                a->nextstep = be_s32(src + 0xA8);
                a->path = NULL;   /* N64 0xAC: runtime pointer */
                a->Sound = NULL;  /* N64 0xB0: runtime pointer */
                break;
            }

            /* --- TankRecord (N64=224, PC=sizeof(TankRecord)) ---
             * Extension has 1 pointer: unk80 (collision_data) */
            case PROPDEF_TANK: {
                TankRecord *t = (TankRecord *)dst;
                memset(t, 0, sizeof(TankRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)t);
                t->unk80 = NULL;  /* N64 0x80: runtime pointer */
                /* rect4f is union { coord2d[4]; f32[8]; } — 8 floats at N64 0x84 */
                for (s32 i = 0; i < 8; i++) {
                    t->rect.f[i] = be_float(src + 0x84 + i * 4);
                }
                /* Remaining fields at N64 0xA4-0xDC: all s32/f32 */
                t->unkA4 = be_s32(src + 0xA4);
                t->unkA8 = be_s32(src + 0xA8);
                t->unkAC = be_s32(src + 0xAC);
                t->unkB0 = be_s32(src + 0xB0);
                t->unkB4 = be_s32(src + 0xB4);
                t->unkB8 = be_s32(src + 0xB8);
                t->unkBC = be_s32(src + 0xBC);
                t->unkC0 = be_s32(src + 0xC0);
                t->is_firing_tank = be_s32(src + 0xC4);
                t->turret_vertical_angle = be_float(src + 0xC8);
                t->turret_orientation_angle = be_float(src + 0xCC);
                t->unkD0 = be_float(src + 0xD0);
                t->stan_y = be_float(src + 0xD4);
                t->unkD8 = be_s32(src + 0xD8);
                t->tank_orientation_angle = be_float(src + 0xDC);
                break;
            }

            /* --- AutogunRecord (N64=216, PC=sizeof(AutogunRecord)) ---
             * Extension has 3 pointers: unkC4, unkC8, beam */
            case PROPDEF_AUTOGUN: {
                AutogunRecord *ag = (AutogunRecord *)dst;
                memset(ag, 0, sizeof(AutogunRecord));
                propdef_convert_objectrecord(src, (ObjectRecord *)ag);
                ag->padID = be_s32(src + 0x80);
                ag->rot_related = be_float(src + 0x84);
                ag->unk88 = be_float(src + 0x88);
                ag->unk8C = be_float(src + 0x8C);
                ag->unk90 = be_float(src + 0x90);
                ag->unk94 = be_float(src + 0x94);
                ag->unk98 = be_float(src + 0x98);
                ag->unk9C = be_float(src + 0x9C);
                ag->unkA0 = be_float(src + 0xA0);
                ag->speed = be_float(src + 0xA4);
                ag->aimdist = be_float(src + 0xA8);
                ag->unkAC = be_s32(src + 0xAC);
                ag->unkB0 = be_float(src + 0xB0);
                ag->unkB4 = be_float(src + 0xB4);
                ag->unkB8 = be_s32(src + 0xB8);
                ag->unkBC = be_s32(src + 0xBC);
                ag->unkC0 = be_s32(src + 0xC0);
                ag->unkC4 = NULL;  /* N64 0xC4: runtime pointer */
                ag->unkC8 = NULL;  /* N64 0xC8: runtime pointer */
                ag->beam = NULL;   /* N64 0xCC: runtime pointer */
                ag->is_active = be_s32(src + 0xD0);
                ag->unkD4 = be_float(src + 0xD4);
                break;
            }

            /* --- PROPDEF_END --- */
            case PROPDEF_END: {
                PropDefHeaderRecord *e = (PropDefHeaderRecord *)dst;
                propdef_convert_header(src, e);
                break;
            }

            /* --- ObjectRecord-extending types: convert base + bswap extra words ---
             * For types we don't have specific conversion code for,
             * convert ObjectRecord base and byte-swap remaining words.
             * Pointer fields in the extra area will be wrong but are runtime-only. */
            default: {
                if (n64_size >= 128) {
                    /* Likely inherits ObjectRecord */
                    propdef_convert_objectrecord(src, (ObjectRecord *)dst);
                    /* Byte-swap extra words after ObjectRecord (N64 offset 0x80+) */
                    s32 extra_n64_bytes = n64_size - 128;
                    s32 extra_start_pc = (s32)sizeof(ObjectRecord);
                    for (s32 w = 0; w < extra_n64_bytes / 4; w++) {
                        if (extra_start_pc + w * 4 + 4 <= pc_size) {
                            *(u32 *)(dst + extra_start_pc + w * 4) = be_u32(src + 128 + w * 4);
                        }
                    }
                } else {
                    /* Small unknown type: copy header + bswap words */
                    propdef_convert_header(src, (PropDefHeaderRecord *)dst);
                    for (s32 w = 1; w < n64_size / 4 && w * 4 < pc_size; w++) {
                        ((u32 *)dst)[w] = be_u32(src + w * 4);
                    }
                }
                break;
            }
        }

        src += n64_size;
        dst += pc_size;
    }

    if (ge_dbg_enabled()) {
        fprintf(stderr, "[SETUP-PC] propDefs: converted %d records (%d bytes N64 → %d bytes PC)\n",
                record_count, (s32)(src - n64_data), total_pc_size);
    }
    return (PropDefHeaderRecord *)pc_buf;
}

/* Track calloc'd buffers so we can free them on stage reload */
static PadRecord *s_pc_pads = NULL;
static BoundPadRecord *s_pc_boundpads = NULL;
static struct SetupIntroEmpty *s_pc_intro = NULL;
static AIListRecord *s_pc_ailists = NULL;
static waypoint *s_pc_waypoints = NULL;
static waygroup *s_pc_waygroups = NULL;
static PathRecord *s_pc_patrolpaths = NULL;
static u8 *s_pc_propdefs = NULL;
static struct pname *s_pc_padnames = NULL;
static struct pname *s_pc_boundpadnames = NULL;

/* FID-0037: re-lay-out a setup padnames/boundpadnames table (header word
 * hdr_index: 8 = padnames, 9 = boundpadnames) into a host-native, NULL-terminated
 * struct pname array. The on-disk table is big-endian 32-bit file-local offsets;
 * N64 struct pname is 4 bytes but the host union is 8, so it cannot be cast in
 * place (same reason the pads are re-laid-out). Mirrors retail prop.c:3849-3865
 * (resolve the table) + 4000-4014 (relocate each .p). `legacy` reproduces the
 * port defect (prop.c:2538 left the table NULL). The pure byte-swap/relocation
 * lives in the unit-tested helper setupPnames*(). */
static struct pname *build_pc_pnames(u8 *base, int hdr_index, int legacy,
                                     struct pname **track)
{
    u32 table_off = setupPnamesTableOffset((const uint8_t *)base, hdr_index, legacy);
    size_t n, i;
    struct pname *arr;

    if (table_off == 0) {
        return NULL;
    }
    n = setupPnamesCount((const uint8_t *)base, table_off, 100000);
    arr = (struct pname *)calloc(n + 1, sizeof(struct pname));
    if (arr == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        arr[i].p = (char *)setupPnamesResolve((const uint8_t *)base, table_off, i);
    }
    arr[n].p = NULL; /* NULL-terminated (retail loops until .p == 0) */
    *track = arr;
    return arr;
}

void proplvreset2(s32 stageId)
{
    ItemModelFileRecord *pitem;
    s32 withchrs;
    s32 withobjs;
    char strResource[0x100] = "";
    u8 *base;
    const u32 *hdr32;

    /* Free previous stage's PC-native buffers */
    if (s_pc_pads) { free(s_pc_pads); s_pc_pads = NULL; }
    if (s_pc_boundpads) { free(s_pc_boundpads); s_pc_boundpads = NULL; }
    if (s_pc_intro) { free(s_pc_intro); s_pc_intro = NULL; }
    if (s_pc_ailists) { free(s_pc_ailists); s_pc_ailists = NULL; }
    if (s_pc_waypoints) { free(s_pc_waypoints); s_pc_waypoints = NULL; }
    if (s_pc_waygroups) { free(s_pc_waygroups); s_pc_waygroups = NULL; }
    if (s_pc_patrolpaths) { free(s_pc_patrolpaths); s_pc_patrolpaths = NULL; }
    if (s_pc_propdefs) { free(s_pc_propdefs); s_pc_propdefs = NULL; }
    if (s_pc_padnames) { free(s_pc_padnames); s_pc_padnames = NULL; }
    if (s_pc_boundpadnames) { free(s_pc_boundpadnames); s_pc_boundpadnames = NULL; }

    withchrs = (tokenFind(1, "-nochr") == NULL && tokenFind(1, "-noprop") == NULL);
    withobjs = (tokenFind(1, "-noobj") == NULL && tokenFind(1, "-noprop") == NULL);

    g_DoorScale = 1.0f;

    for (pitem = PitemZ_entries; pitem->header != NULL; pitem++)
    {
        modelResetHeaderRuntimeState(pitem->header);
    }

    if (stageId > LEVELID_MAX + 1 || !setup_text_pointers[stageId])
    {
        g_CurrentSetup.pathwaypoints = NULL;
        g_CurrentSetup.waypointgroups = NULL;
        g_CurrentSetup.intro = NULL;
        g_CurrentSetup.propDefs = NULL;
        g_CurrentSetup.patrolpaths = NULL;
        g_CurrentSetup.ailists = NULL;
        g_CurrentSetup.pads = NULL;
        g_CurrentSetup.boundpads = NULL;
        g_CurrentSetup.padnames = NULL;
        g_CurrentSetup.boundpadnames = NULL;
        alloc_init_GUARDdata_entries(0);
        sub_GAME_7F005540(0);
        sub_GAME_7F005624(0);
        alloc_false_GUARDdata_to_exec_global_action();
        return;
    }

    strResource[0] = setup_text_pointers[stageId][0];
    strResource[1] = 0;
    if (getPlayerCount() >= 2)
        strcat(strResource, "mp_");
    strcat(strResource, setup_text_pointers[stageId] + 1);

    g_ptrStageSetupFile = _fileNameLoadToBank(strResource, FILELOADMETHOD_DEFAULT, 256, 4);
    base = (u8 *)g_ptrStageSetupFile;

#ifdef NATIVE_PORT
    if (getenv("GE007_SETUP_DIAG")) {
        fprintf(stderr, "[SETUP-DIAG] stage=%d resource='%s' base=%p\n",
                stageId, strResource, (void *)base);
        if (base) {
            const u32 *diag_hdr = (const u32 *)base;
            fprintf(stderr,
                    "[SETUP-DIAG] hdr offs: wp=0x%08X wg=0x%08X intro=0x%08X prop=0x%08X patrol=0x%08X ai=0x%08X pads=0x%08X bpads=0x%08X\n",
                    bswap32(diag_hdr[0]), bswap32(diag_hdr[1]), bswap32(diag_hdr[2]), bswap32(diag_hdr[3]),
                    bswap32(diag_hdr[4]), bswap32(diag_hdr[5]), bswap32(diag_hdr[6]), bswap32(diag_hdr[7]));
        }
    }
    if (!base) {
        fprintf(stderr, "[PROP] proplvreset2: setup file '%s' not found for stage %d, using empty setup\n", strResource, stageId);
        g_CurrentSetup.pathwaypoints = NULL;
        g_CurrentSetup.waypointgroups = NULL;
        g_CurrentSetup.intro = NULL;
        g_CurrentSetup.propDefs = NULL;
        g_CurrentSetup.patrolpaths = NULL;
        g_CurrentSetup.ailists = NULL;
        g_CurrentSetup.pads = NULL;
        g_CurrentSetup.boundpads = NULL;
        g_CurrentSetup.padnames = NULL;
        g_CurrentSetup.boundpadnames = NULL;
        alloc_init_GUARDdata_entries(0);
        sub_GAME_7F005540(0);
        sub_GAME_7F005624(0);
        alloc_false_GUARDdata_to_exec_global_action();
        return;
    }
#endif

    langLoadToAddr(langGetLangBankIndexFromStagenum(stageId));

    /* The setup file header is 10 × 32-bit big-endian offsets.
     * Each non-zero offset + base gives a pointer into the file. */
    hdr32 = (const u32 *)base;

    #define SETUP_PTR(idx, type) \
        (bswap32(hdr32[idx]) ? (type *)(base + bswap32(hdr32[idx])) : NULL)

    /* Only resolve pointers for data we've byte-swapped.
     * Leave others NULL to prevent crashes from un-swapped data. */
    g_CurrentSetup.pathwaypoints = SETUP_PTR(0, waypoint);   /* Converted below */
    g_CurrentSetup.waypointgroups = SETUP_PTR(1, waygroup); /* Converted below */
    g_CurrentSetup.intro    = SETUP_PTR(2, struct SetupIntroEmpty);
    g_CurrentSetup.propDefs = SETUP_PTR(3, PropDefHeaderRecord);
    g_CurrentSetup.patrolpaths = SETUP_PTR(4, PathRecord);  /* Converted below */
    g_CurrentSetup.ailists  = SETUP_PTR(5, AIListRecord);   /* Converted below */
    g_CurrentSetup.pads     = SETUP_PTR(6, PadRecord);
    g_CurrentSetup.boundpads = SETUP_PTR(7, BoundPadRecord);
    /* FID-0037: padnames (word 8) / boundpadnames (word 9) — big-endian offset
     * tables re-laid-out to host-native struct pname arrays. Faithful default;
     * GE007_NO_PADNAMES_FIX restores the port defect (both NULL). */
    {
        int pnames_legacy = port_env_set("GE007_NO_PADNAMES_FIX",
            "restore the port defect of leaving setup padnames/boundpadnames NULL "
            "instead of resolving the big-endian offset tables (FID-0037)");
        g_CurrentSetup.padnames      = build_pc_pnames(base, 8, pnames_legacy, &s_pc_padnames);
        g_CurrentSetup.boundpadnames = build_pc_pnames(base, 9, pnames_legacy, &s_pc_boundpadnames);
    }

#ifdef NATIVE_PORT
    if (getenv("GE007_SETUP_DIAG")) {
        fprintf(stderr,
                "[SETUP-DIAG] resolved: intro=%p propDefs=%p pads=%p boundpads=%p\n",
                (void *)g_CurrentSetup.intro, (void *)g_CurrentSetup.propDefs,
                (void *)g_CurrentSetup.pads, (void *)g_CurrentSetup.boundpads);
    }
#endif

    #undef SETUP_PTR

    /* --- Byte-swap pad data ---
     * N64 PadRecord layout (44 bytes, all big-endian):
     *   0x00: pos (3 × f32)     = 12 bytes
     *   0x0C: up   (3 × f32)    = 12 bytes
     *   0x18: look (3 × f32)    = 12 bytes
     *   0x24: plink (u32 offset) = 4 bytes
     *   0x28: stan  (u32 ptr)    = 4 bytes
     * Total: 44 bytes on N64 = same as sizeof(PadRecord) on N64
     *
     * On 64-bit PC, sizeof(PadRecord) = 48 (or more with alignment).
     * We CANNOT iterate with PadRecord* stride — must use raw 44-byte stride.
     *
     * Strategy: byte-swap floats and pointer fields IN PLACE using the N64 stride.
     * Then we need to re-layout the pads into PC-sized PadRecords.
     * For now, use a simpler approach: since plink and stan are at the end,
     * and the float data (36 bytes) is the same size, we can swap in-place
     * and treat the plink/stan as 32-bit values read via casting.
     *
     * ACTUALLY: On 64-bit, PadRecord.plink is 8 bytes and PadRecord.stan is 8 bytes,
     * so sizeof(PadRecord) = 48. The ROM data has 44-byte entries. We MUST re-layout.
     */
    if (g_CurrentSetup.pads)
    {
        /* Count pads by scanning raw 44-byte entries for null plink sentinel */
        u8 *raw = (u8 *)g_CurrentSetup.pads;
        s32 n_pads = 0;
        f32 roomscale = get_room_data_float2();
        PadRecord *new_pads;
        s32 i;

        while (1) {
            u32 plink_off = bswap32(*(u32 *)(raw + n_pads * 44 + 0x24));
            if (plink_off == 0) break;
            n_pads++;
            if (n_pads > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: pad count overflow\n"); break; }
        }

        /* Allocate PC-native PadRecord array (n_pads + 1 sentinel) */
        new_pads = (PadRecord *)calloc(n_pads + 1, sizeof(PadRecord));
        s_pc_pads = new_pads;

        for (i = 0; i < n_pads; i++) {
            u8 *entry = raw + i * 44;
            new_pads[i].pos.f[0]  = be_float(entry + 0x00) * roomscale;
            new_pads[i].pos.f[1]  = be_float(entry + 0x04) * roomscale;
            new_pads[i].pos.f[2]  = be_float(entry + 0x08) * roomscale;
            new_pads[i].up.f[0]   = be_float(entry + 0x0C);
            new_pads[i].up.f[1]   = be_float(entry + 0x10);
            new_pads[i].up.f[2]   = be_float(entry + 0x14);
            new_pads[i].look.f[0] = be_float(entry + 0x18);
            new_pads[i].look.f[1] = be_float(entry + 0x1C);
            new_pads[i].look.f[2] = be_float(entry + 0x20);

            /* plink: 32-bit offset into setup file → PC pointer */
            {
                u32 plink_off = bswap32(*(u32 *)(entry + 0x24));
                new_pads[i].plink = plink_off ? (char *)(base + plink_off) : NULL;
            }

            /* stan: resolved by init_pathtable_something below */
            new_pads[i].stan = NULL;

            /* Resolve stan tile for this pad */
            init_pathtable_something(&new_pads[i], new_pads[i].plink, &new_pads[i].stan);
        }
        /* Sentinel: plink=NULL marks end */
        new_pads[n_pads].plink = NULL;
        new_pads[n_pads].stan = NULL;

        /* Diagnostic: count how many pads resolved their stan tile */
        {
            s32 resolved = 0, null_plink = 0, null_stan = 0;
            for (i = 0; i < n_pads; i++) {
                if (new_pads[i].plink == NULL) null_plink++;
                if (new_pads[i].stan != NULL) resolved++;
                else null_stan++;
            }
            if (ge_dbg_enabled()) {
                printf("[SETUP-PC] Pads: %d total, %d with stan tile, %d without (%d null plink)\n",
                    n_pads, resolved, null_stan, null_plink);
                /* Show first few plink strings */
                for (i = 0; i < n_pads && i < 5; i++) {
                    if (new_pads[i].plink) {
                        printf("[SETUP-PC]   pad[%d] plink='%.30s' stan=%p pos=(%.0f,%.0f,%.0f)\n",
                            i, new_pads[i].plink, (void*)new_pads[i].stan,
                            new_pads[i].pos.f[0], new_pads[i].pos.f[1], new_pads[i].pos.f[2]);
                    }
                }
                fflush(stdout);
            }
        }

        g_CurrentSetup.pads = new_pads;
    }

    /* --- Byte-swap bound pad data ---
     * N64 BoundPadRecord layout (68 bytes, 0x44, all big-endian):
     *   0x00: pos (3 × f32)     = 12 bytes
     *   0x0C: up  (3 × f32)     = 12 bytes
     *   0x18: look (3 × f32)    = 12 bytes
     *   0x24: plink (u32 offset) = 4 bytes
     *   0x28: stan  (u32 ptr)    = 4 bytes
     *   0x2C: bbox (6 × f32)    = 24 bytes (xmin, xmax, ymin, ymax, zmin, zmax)
     * Total: 68 bytes on N64
     *
     * On 64-bit PC, sizeof(BoundPadRecord) is larger due to 8-byte pointers.
     * Must re-layout from 68-byte N64 stride to PC-native BoundPadRecord.
     */
    if (g_CurrentSetup.boundpads)
    {
        u8 *raw = (u8 *)g_CurrentSetup.boundpads;
        s32 n_bpads = 0;
        f32 roomscale = get_room_data_float2();
        BoundPadRecord *new_bpads;
        s32 i;

        /* Count bound pads — sentinel is plink == 0 */
        while (1) {
            u32 plink_off = bswap32(*(u32 *)(raw + n_bpads * 68 + 0x24));
            if (plink_off == 0) break;
            n_bpads++;
            if (n_bpads > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: boundpad count overflow\n"); break; }
        }

        /* Allocate PC-native BoundPadRecord array (n_bpads + 1 sentinel) */
        new_bpads = (BoundPadRecord *)calloc(n_bpads + 1, sizeof(BoundPadRecord));
        s_pc_boundpads = new_bpads;

        for (i = 0; i < n_bpads; i++) {
            u8 *entry = raw + i * 68;
            new_bpads[i].pos.f[0]  = be_float(entry + 0x00) * roomscale;
            new_bpads[i].pos.f[1]  = be_float(entry + 0x04) * roomscale;
            new_bpads[i].pos.f[2]  = be_float(entry + 0x08) * roomscale;
            /* BoundPadRecord shares PadRecord's canonical layout exactly:
             * position, then up vector, then look vector. Swapping these on
             * decode corrupts door/bound volumes and their portal math. */
            new_bpads[i].up.f[0]   = be_float(entry + 0x0C);
            new_bpads[i].up.f[1]   = be_float(entry + 0x10);
            new_bpads[i].up.f[2]   = be_float(entry + 0x14);
            new_bpads[i].look.f[0] = be_float(entry + 0x18);
            new_bpads[i].look.f[1] = be_float(entry + 0x1C);
            new_bpads[i].look.f[2] = be_float(entry + 0x20);

            /* plink: 32-bit offset into setup file → PC pointer */
            {
                u32 plink_off = bswap32(*(u32 *)(entry + 0x24));
                new_bpads[i].plink = plink_off ? (char *)(base + plink_off) : NULL;
            }

            /* stan: resolved below */
            new_bpads[i].stan = NULL;

            /* bbox: 6 floats, scaled by roomscale */
            new_bpads[i].bbox.xmin = be_float(entry + 0x2C) * roomscale;
            new_bpads[i].bbox.xmax = be_float(entry + 0x30) * roomscale;
            new_bpads[i].bbox.ymin = be_float(entry + 0x34) * roomscale;
            new_bpads[i].bbox.ymax = be_float(entry + 0x38) * roomscale;
            new_bpads[i].bbox.zmin = be_float(entry + 0x3C) * roomscale;
            new_bpads[i].bbox.zmax = be_float(entry + 0x40) * roomscale;

            /* Resolve stan tile for this bound pad */
            init_pathtable_something((PadRecord *)&new_bpads[i], new_bpads[i].plink, &new_bpads[i].stan);
        }
        /* Sentinel: plink=NULL marks end */
        new_bpads[n_bpads].plink = NULL;
        new_bpads[n_bpads].stan = NULL;

        g_CurrentSetup.boundpads = new_bpads;

        fprintf(stderr, "[SETUP-PC] Converted %d bound pads\n", n_bpads);
    }

    /* --- Byte-swap waypoints (pathwaypoints) ---
     * N64 waypoint layout (16 bytes, all big-endian):
     *   0x00: padID      (s32)        = 4 bytes
     *   0x04: neighbours (u32 offset) = 4 bytes  → s32* on PC (8 bytes)
     *   0x08: groupNum   (s32)        = 4 bytes
     *   0x0C: dist       (s32)        = 4 bytes
     * Sentinel: padID < 0 (0xFFFFFFFF works as -1 in both endians)
     *
     * On 64-bit PC, sizeof(waypoint) = 24 due to pointer expansion.
     * Must re-layout from 16-byte N64 stride to PC-native waypoint.
     */
    if (g_CurrentSetup.pathwaypoints)
    {
        u8 *raw = (u8 *)g_CurrentSetup.pathwaypoints;
        s32 n_wp = 0;
        waypoint *new_wp;
        s32 i, j;

        /* Count waypoints — sentinel is padID < 0 (works in both endians for -1) */
        while (1) {
            s32 padID_be;
            memcpy(&padID_be, raw + n_wp * 16 + 0x00, 4);
            if ((s32)bswap32((u32)padID_be) < 0) break;
            n_wp++;
            if (n_wp > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: waypoint count overflow\n"); break; }
        }

        /* Allocate PC-native waypoint array (n_wp + 1 sentinel) */
        new_wp = (waypoint *)calloc(n_wp + 1, sizeof(waypoint));
        s_pc_waypoints = new_wp;

        for (i = 0; i < n_wp; i++) {
            u8 *entry = raw + i * 16;
            u32 nb_off;

            new_wp[i].padID = (s32)bswap32(*(u32 *)(entry + 0x00));

            /* neighbours: offset into setup file → PC pointer to s32 array */
            nb_off = bswap32(*(u32 *)(entry + 0x04));
            if (nb_off) {
                new_wp[i].neighbours = (s32 *)(base + nb_off);
                /* Byte-swap the neighbours array in-place (s32[], terminated by -1) */
                for (j = 0; (s32)bswap32((u32)new_wp[i].neighbours[j]) >= 0; j++) {
                    new_wp[i].neighbours[j] = (s32)bswap32((u32)new_wp[i].neighbours[j]);
                }
                /* Also swap the -1 sentinel so it reads correctly */
                new_wp[i].neighbours[j] = (s32)bswap32((u32)new_wp[i].neighbours[j]);
            } else {
                new_wp[i].neighbours = NULL;
            }

            new_wp[i].groupNum = (s32)bswap32(*(u32 *)(entry + 0x08));
            new_wp[i].dist     = (s32)bswap32(*(u32 *)(entry + 0x0C));
        }
        /* Sentinel: padID = -1 */
        new_wp[n_wp].padID = -1;
        new_wp[n_wp].neighbours = NULL;
        new_wp[n_wp].groupNum = 0;
        new_wp[n_wp].dist = 0;

        g_CurrentSetup.pathwaypoints = new_wp;
        fprintf(stderr, "[SETUP-PC] Converted %d waypoints\n", n_wp);
    }

    /* --- Byte-swap waygroups (waypointgroups) ---
     * N64 waygroup layout (12 bytes, all big-endian):
     *   0x00: neighbours (u32 offset) = 4 bytes  → s32* on PC (8 bytes)
     *   0x04: waypoints  (u32 offset) = 4 bytes  → s32* on PC (8 bytes)
     *   0x08: dist       (s32)        = 4 bytes
     * Sentinel: neighbours offset == 0
     *
     * On 64-bit PC, sizeof(waygroup) = 24 due to two pointer expansions.
     */
    if (g_CurrentSetup.waypointgroups)
    {
        u8 *raw = (u8 *)g_CurrentSetup.waypointgroups;
        s32 n_wg = 0;
        waygroup *new_wg;
        s32 i, j;

        /* Count waygroups — sentinel is neighbours offset == 0 */
        while (1) {
            u32 nb_off = bswap32(*(u32 *)(raw + n_wg * 12 + 0x00));
            if (nb_off == 0) break;
            n_wg++;
            if (n_wg > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: waygroup count overflow\n"); break; }
        }

        /* Allocate PC-native waygroup array (n_wg + 1 sentinel) */
        new_wg = (waygroup *)calloc(n_wg + 1, sizeof(waygroup));
        s_pc_waygroups = new_wg;

        for (i = 0; i < n_wg; i++) {
            u8 *entry = raw + i * 12;
            u32 nb_off, wp_off;

            /* neighbours: offset → pointer, byte-swap sub-array */
            nb_off = bswap32(*(u32 *)(entry + 0x00));
            if (nb_off) {
                new_wg[i].neighbours = (s32 *)(base + nb_off);
                for (j = 0; (s32)bswap32((u32)new_wg[i].neighbours[j]) >= 0; j++) {
                    new_wg[i].neighbours[j] = (s32)bswap32((u32)new_wg[i].neighbours[j]);
                }
                new_wg[i].neighbours[j] = (s32)bswap32((u32)new_wg[i].neighbours[j]); /* swap sentinel */
            } else {
                new_wg[i].neighbours = NULL;
            }

            /* waypoints: offset → pointer, byte-swap sub-array */
            wp_off = bswap32(*(u32 *)(entry + 0x04));
            if (wp_off) {
                new_wg[i].waypoints = (s32 *)(base + wp_off);
                for (j = 0; (s32)bswap32((u32)new_wg[i].waypoints[j]) >= 0; j++) {
                    new_wg[i].waypoints[j] = (s32)bswap32((u32)new_wg[i].waypoints[j]);
                }
                new_wg[i].waypoints[j] = (s32)bswap32((u32)new_wg[i].waypoints[j]); /* swap sentinel */
            } else {
                new_wg[i].waypoints = NULL;
            }

            new_wg[i].dist = (s32)bswap32(*(u32 *)(entry + 0x08));
        }
        /* Sentinel: neighbours=NULL */
        new_wg[n_wg].neighbours = NULL;
        new_wg[n_wg].waypoints = NULL;
        new_wg[n_wg].dist = 0;

        g_CurrentSetup.waypointgroups = new_wg;
        fprintf(stderr, "[SETUP-PC] Converted %d waygroups\n", n_wg);
    }

    /* --- Byte-swap patrol paths ---
     * N64 PathRecord layout (8 bytes):
     *   0x00: waypoints (u32 offset) = 4 bytes  → s32* on PC (8 bytes)
     *   0x04: ID        (u8)         = 1 byte
     *   0x05: isLoop    (u8)         = 1 byte
     *   0x06: len       (u16)        = 2 bytes (computed, not from file)
     * Sentinel: waypoints offset == 0
     *
     * On 64-bit PC, sizeof(PathRecord) = 16 due to pointer expansion.
     * The waypoints array contains s32 waypoint IDs (not pointers), terminated by -1.
     */
    if (g_CurrentSetup.patrolpaths)
    {
        u8 *raw = (u8 *)g_CurrentSetup.patrolpaths;
        s32 n_pp = 0;
        PathRecord *new_pp;
        s32 i, j;

        /* Count patrol paths — sentinel is waypoints offset == 0 */
        while (1) {
            u32 wp_off = bswap32(*(u32 *)(raw + n_pp * 8 + 0x00));
            if (wp_off == 0) break;
            n_pp++;
            if (n_pp > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: patrolpath count overflow\n"); break; }
        }

        /* Allocate PC-native PathRecord array (n_pp + 1 sentinel) */
        new_pp = (PathRecord *)calloc(n_pp + 1, sizeof(PathRecord));
        s_pc_patrolpaths = new_pp;

        for (i = 0; i < n_pp; i++) {
            u8 *entry = raw + i * 8;
            u32 wp_off;

            /* waypoints: offset → pointer to s32 array of waypoint IDs */
            wp_off = bswap32(*(u32 *)(entry + 0x00));
            if (wp_off) {
                new_pp[i].waypoints = (s32 *)(base + wp_off);
                /* Byte-swap waypoint IDs in-place and count them */
                for (j = 0; (s32)bswap32((u32)new_pp[i].waypoints[j]) >= 0; j++) {
                    new_pp[i].waypoints[j] = (s32)bswap32((u32)new_pp[i].waypoints[j]);
                }
                /* Swap the -1 sentinel too */
                new_pp[i].waypoints[j] = (s32)bswap32((u32)new_pp[i].waypoints[j]);
                new_pp[i].len = (u16)j;
            } else {
                new_pp[i].waypoints = NULL;
                new_pp[i].len = 0;
            }

            new_pp[i].ID     = *(entry + 0x04);
            new_pp[i].isLoop = *(entry + 0x05);
        }
        /* Sentinel: waypoints=NULL */
        new_pp[n_pp].waypoints = NULL;
        new_pp[n_pp].ID = 0;
        new_pp[n_pp].isLoop = 0;
        new_pp[n_pp].len = 0;

        g_CurrentSetup.patrolpaths = new_pp;
        fprintf(stderr, "[SETUP-PC] Converted %d patrol paths\n", n_pp);
    }

    /* --- Byte-swap AI lists ---
     * N64 AIListRecord layout (8 bytes, all big-endian):
     *   0x00: ailist (u32 offset) = 4 bytes  → AIRecord* on PC (8 bytes)
     *   0x04: ID     (u32)        = 4 bytes
     * Sentinel: ailist offset == 0
     *
     * On 64-bit PC, sizeof(AIListRecord) = 16 due to pointer expansion.
     * The ailist pointer references bytecode within the setup file — no further
     * byte-swap needed for the bytecode itself (read byte-by-byte with CharArrayTo16).
     *
     * Previously verified: 28 entries for Dam, offsets correct.
     * Previously crashed when patrolpaths was NULL — now safe.
     */
    if (g_CurrentSetup.ailists)
    {
        u8 *raw = (u8 *)g_CurrentSetup.ailists;
        s32 n_ai = 0;
        AIListRecord *new_ai;
        s32 i;

        /* Count AI lists — sentinel is ailist offset == 0 */
        while (1) {
            u32 ai_off = bswap32(*(u32 *)(raw + n_ai * 8 + 0x00));
            if (ai_off == 0) break;
            n_ai++;
            if (n_ai > 10000) { fprintf(stderr, "[SETUP-PC] ERROR: ailist count overflow\n"); break; }
        }

        /* Allocate PC-native AIListRecord array (n_ai + 1 sentinel) */
        new_ai = (AIListRecord *)calloc(n_ai + 1, sizeof(AIListRecord));
        s_pc_ailists = new_ai;

        for (i = 0; i < n_ai; i++) {
            u8 *entry = raw + i * 8;
            u32 ai_off = bswap32(*(u32 *)(entry + 0x00));
            new_ai[i].ailist = ai_off ? (AIRecord *)(base + ai_off) : NULL;
            new_ai[i].ID = (int)bswap32(*(u32 *)(entry + 0x04));
        }
        /* Sentinel: ailist=NULL, ID=0 */
        new_ai[n_ai].ailist = NULL;
        new_ai[n_ai].ID = 0;

        g_CurrentSetup.ailists = new_ai;
        fprintf(stderr, "[SETUP-PC] Converted %d AI lists\n", n_ai);
    }

    /* --- Byte-swap intro data ---
     * Intro records are variable-length, packed sequentially in the setup file.
     * Each starts with an s32 type field. N64 data is big-endian.
     * SetupIntroCamera has pointer fields, so N64 size (40) != PC size (56).
     * All other intro types have identical layout on N64 and PC.
     *
     * Strategy: walk N64 data using N64 strides, build a PC-native buffer
     * with correct struct sizes. This is the same approach used for pads.
     */
    if (g_CurrentSetup.intro)
    {
        int trace_intro_parse = 0;
#ifdef NATIVE_PORT
        trace_intro_parse = (getenv("GE007_TRACE_INTRO_PARSE") != NULL);
#endif
        /* N64 sizes for each intro record type (no pointer fields except Camera) */
        static const s32 n64_intro_sizes[] = {
            12, /* INTROTYPE_SPAWN */
            16, /* INTROTYPE_ITEM */
            16, /* INTROTYPE_AMMO */
            32, /* INTROTYPE_SWIRL */
             8, /* INTROTYPE_ANIM */
             8, /* INTROTYPE_CUFF */
            40, /* INTROTYPE_CAMERA (has pointer fields: 40 on N64, 56 on PC) */
            12, /* INTROTYPE_WATCH */
             8, /* INTROTYPE_CREDITS */
             4, /* INTROTYPE_END */
        };

        /* PC sizes for each intro record type */
        static const s32 pc_intro_sizes[] = {
            (s32)sizeof(struct SetupIntroSpawn),
            (s32)sizeof(struct SetupIntroItem),
            (s32)sizeof(struct SetupIntroAmmo),
            (s32)sizeof(struct SetupIntroSwirl),
            (s32)sizeof(struct SetupIntroAnim),
            (s32)sizeof(struct SetupIntroCuff),
            (s32)sizeof(struct SetupIntroCamera),
            (s32)sizeof(struct SetupIntroWatch),
            (s32)sizeof(struct SetupIntroCredits),
            (s32)sizeof(struct SetupIntroEmpty),  /* END */
        };

        /* First pass: count records and compute PC buffer size */
        u8 *n64_ptr = (u8 *)g_CurrentSetup.intro;
        s32 pc_total_size = 0;
        s32 n_intro_records = 0;
        s32 intro_ok = 1;

        while (intro_ok) {
            s32 n64_type = (s32)bswap32(*(u32 *)n64_ptr);

            if (trace_intro_parse && n_intro_records < 32) {
                fprintf(stderr,
                        "[INTRO-PARSE] rec=%d off=%ld type=%d raw=%08x\n",
                        n_intro_records,
                        (long)(n64_ptr - (u8 *)g_CurrentSetup.intro),
                        n64_type,
                        bswap32(*(u32 *)n64_ptr));
            }

            if (n64_type < 0 || n64_type >= INTROTYPE_MAX) {
                fprintf(stderr, "[SETUP-PC] ERROR: bad intro type %d at byte offset %ld\n",
                        n64_type, (long)(n64_ptr - (u8 *)g_CurrentSetup.intro));
                intro_ok = 0;
                break;
            }

            pc_total_size += pc_intro_sizes[n64_type];
            n_intro_records++;

            if (n64_type == INTROTYPE_END)
                break;

            n64_ptr += n64_intro_sizes[n64_type];

            if (n_intro_records > 10000) {
                fprintf(stderr, "[SETUP-PC] ERROR: intro record count overflow\n");
                intro_ok = 0;
                break;
            }
        }

        if (intro_ok) {
            u8 *pc_buf = (u8 *)calloc(1, pc_total_size);
            s_pc_intro = (struct SetupIntroEmpty *)pc_buf;
            u8 *pc_ptr = pc_buf;
            s32 rec;
            n64_ptr = (u8 *)g_CurrentSetup.intro;

            if (trace_intro_parse) {
                fprintf(stderr,
                        "[INTRO-PARSE] total_records=%d total_size=%d\n",
                        n_intro_records, pc_total_size);
            }

            /* Second pass: copy and byte-swap each record */
            for (rec = 0; rec < n_intro_records; rec++) {
                s32 n64_type = (s32)bswap32(*(u32 *)n64_ptr);
                u8 *rec_start = n64_ptr;

                switch (n64_type) {
                    case INTROTYPE_SPAWN: {
                        struct SetupIntroSpawn *dst = (struct SetupIntroSpawn *)pc_ptr;
                        dst->type = INTROTYPE_SPAWN;
                        dst->index = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->is_demo_playback = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        n64_ptr += 12;
                        pc_ptr += sizeof(struct SetupIntroSpawn);
                        break;
                    }
                    case INTROTYPE_ITEM: {
                        struct SetupIntroItem *dst = (struct SetupIntroItem *)pc_ptr;
                        dst->type = INTROTYPE_ITEM;
                        dst->item_right = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->item_left = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        dst->is_demo_playback = (s32)bswap32(*(u32 *)(n64_ptr + 12));
                        n64_ptr += 16;
                        pc_ptr += sizeof(struct SetupIntroItem);
                        break;
                    }
                    case INTROTYPE_AMMO: {
                        struct SetupIntroAmmo *dst = (struct SetupIntroAmmo *)pc_ptr;
                        dst->type = INTROTYPE_AMMO;
                        dst->ammo_type = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->ammo_amount = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        dst->is_demo_playback = (s32)bswap32(*(u32 *)(n64_ptr + 12));
                        n64_ptr += 16;
                        pc_ptr += sizeof(struct SetupIntroAmmo);
                        break;
                    }
                    case INTROTYPE_SWIRL: {
                        struct SetupIntroSwirl *dst = (struct SetupIntroSwirl *)pc_ptr;
                        dst->type = INTROTYPE_SWIRL;
                        dst->unk04 = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->unk08.ival = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        dst->unk0C.ival = (s32)bswap32(*(u32 *)(n64_ptr + 12));
                        dst->unk10.ival = (s32)bswap32(*(u32 *)(n64_ptr + 16));
                        dst->unk14.ival = (s32)bswap32(*(u32 *)(n64_ptr + 20));
                        dst->unk18.ival = (s32)bswap32(*(u32 *)(n64_ptr + 24));
                        dst->unk1C = (s32)bswap32(*(u32 *)(n64_ptr + 28));
                        n64_ptr += 32;
                        pc_ptr += sizeof(struct SetupIntroSwirl);
                        break;
                    }
                    case INTROTYPE_ANIM: {
                        struct SetupIntroAnim *dst = (struct SetupIntroAnim *)pc_ptr;
                        dst->type = INTROTYPE_ANIM;
                        dst->intro_anim = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        n64_ptr += 8;
                        pc_ptr += sizeof(struct SetupIntroAnim);
                        break;
                    }
                    case INTROTYPE_CUFF: {
                        struct SetupIntroCuff *dst = (struct SetupIntroCuff *)pc_ptr;
                        dst->type = INTROTYPE_CUFF;
                        dst->bondtype = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        n64_ptr += 8;
                        pc_ptr += sizeof(struct SetupIntroCuff);
                        break;
                    }
                    case INTROTYPE_CAMERA: {
                        struct SetupIntroCamera *dst = (struct SetupIntroCamera *)pc_ptr;
                        u32 lang1c_raw;
                        dst->type = INTROTYPE_CAMERA;
                        dst->unk04.ival = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->unk08.ival = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        dst->unk0C.ival = (s32)bswap32(*(u32 *)(n64_ptr + 12));
                        dst->unk10.ival = (s32)bswap32(*(u32 *)(n64_ptr + 16));
                        dst->unk14.ival = (s32)bswap32(*(u32 *)(n64_ptr + 20));
                        dst->unk18 = (s32)bswap32(*(u32 *)(n64_ptr + 24));
                        /* lang1c: N64 bytes 28-31 = two BE u16 language indices */
                        lang1c_raw = bswap32(*(u32 *)(n64_ptr + 28));
                        dst->lang1c.lang_index[0] = (u16)(lang1c_raw >> 16);
                        dst->lang1c.lang_index[1] = (u16)(lang1c_raw & 0xFFFF);
                        /* lang20: N64 bytes 32-35 = s32 language index */
                        dst->lang20.lang_index = (s32)bswap32(*(u32 *)(n64_ptr + 32));
                        /* prev: set at runtime by bondviewLoadSetupIntroSection */
                        dst->prev = NULL;
                        n64_ptr += 40;
                        pc_ptr += sizeof(struct SetupIntroCamera);
                        break;
                    }
                    case INTROTYPE_WATCH: {
                        struct SetupIntroWatch *dst = (struct SetupIntroWatch *)pc_ptr;
                        dst->type = INTROTYPE_WATCH;
                        dst->hours = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        dst->minutes = (s32)bswap32(*(u32 *)(n64_ptr + 8));
                        n64_ptr += 12;
                        pc_ptr += sizeof(struct SetupIntroWatch);
                        break;
                    }
                    case INTROTYPE_CREDITS: {
                        struct SetupIntroCredits *dst = (struct SetupIntroCredits *)pc_ptr;
                        dst->type = INTROTYPE_CREDITS;
                        dst->unk04 = (s32)bswap32(*(u32 *)(n64_ptr + 4));
                        n64_ptr += 8;
                        pc_ptr += sizeof(struct SetupIntroCredits);
                        break;
                    }
                    case INTROTYPE_END: {
                        struct SetupIntroEmpty *dst = (struct SetupIntroEmpty *)pc_ptr;
                        dst->type = INTROTYPE_END;
                        n64_ptr += 4;
                        pc_ptr += sizeof(struct SetupIntroEmpty);
                        break;
                    }
                }

                /* [INTRO-DIGEST]: machine-readable per-record dump for the
                 * emulator-free parse-digest gate (tools/intro_parse_digest.py),
                 * emitted under the SAME trace_intro_parse gate as the
                 * human-readable [INTRO-PARSE] line above (no new env var;
                 * zero cost when GE007_TRACE_INTRO_PARSE is unset).
                 *
                 * We dump the RAW big-endian N64 SOURCE words for this record
                 * (converted to host-endian signed ints), not the widened PC
                 * struct's fields. The PC structs synthesize pointer-typed
                 * views that do not exist in the N64 stream at all
                 * (SetupIntroCamera's `prev` is set at runtime to NULL, and
                 * lang1c/lang20 are unions whose alternate member is a
                 * char* that is never written by this widening code — only
                 * the .lang_index views are). Reading straight from the
                 * source bytes therefore sidesteps pointer contamination
                 * entirely and needs no per-type struct-layout bookkeeping
                 * here; it's also stable across PC struct padding changes.
                 * w[0] is the record's type word (redundant with "type"
                 * above) — kept so "w" is a literal, mechanical dump of the
                 * record's N64 words rather than a hand-picked field list.
                 */
                if (trace_intro_parse) {
                    s32 rec_n64_words = n64_intro_sizes[n64_type] / 4;
                    s32 wi;
                    fprintf(stderr,
                            "[INTRO-DIGEST] {\"i\":%d,\"type\":%d,\"n64_off\":%ld,\"w\":[",
                            rec, n64_type, (long)(rec_start - (u8 *)g_CurrentSetup.intro));
                    for (wi = 0; wi < rec_n64_words; wi++) {
                        fprintf(stderr, "%s%d", wi ? "," : "",
                                (s32)bswap32(*(u32 *)(rec_start + wi * 4)));
                    }
                    fprintf(stderr, "]}\n");
                }
            }

            if (trace_intro_parse) {
                fprintf(stderr, "[INTRO-DIGEST-END] {\"count\":%d}\n", n_intro_records);
            }

            g_CurrentSetup.intro = (struct SetupIntroEmpty *)pc_buf;
        } else {
            g_CurrentSetup.intro = NULL;
            fprintf(stderr, "[SETUP-PC] WARNING: intro data parse failed, setting to NULL\n");
        }
    }

    /* --- PropDefs: convert N64 → PC layout, then count ---
     * propDefs pointer resolved above (SETUP_PTR) and points at raw N64
     * big-endian data. Convert to PC-native layout (64-bit pointers,
     * little-endian fields) before the counting pass. After conversion,
     * sizepropdef() returns PC sizes and load_proptype walks PC data.
     */
    if (g_CurrentSetup.propDefs) {
        PropDefHeaderRecord *pc_defs = propdef_convert_n64_to_pc(
            (const u8 *)g_CurrentSetup.propDefs);
        if (pc_defs) {
            s_pc_propdefs = (u8 *)pc_defs;
            g_CurrentSetup.propDefs = pc_defs;
        } else {
            fprintf(stderr, "[SETUP-PC] ERROR: propDef conversion failed\n");
            g_CurrentSetup.propDefs = NULL;
        }
    }

    {
        s32 guardcount = 0;
        s32 collectcount = 0;

        if (g_CurrentSetup.propDefs) {
            guardcount += load_proptype(PROPDEF_GUARD);
            collectcount += load_proptype(PROPDEF_DOOR);
            collectcount += load_proptype(PROPDEF_PROP);
            collectcount += load_proptype(PROPDEF_GLASS);
            collectcount += load_proptype(PROPDEF_TINTED_GLASS);
            collectcount += load_proptype(PROPDEF_SAFE);
            collectcount += load_proptype(PROPDEF_ALARM);
            collectcount += load_proptype(PROPDEF_MAGAZINE);
            collectcount += load_proptype(PROPDEF_COLLECTABLE);
            collectcount += load_proptype(PROPDEF_KEY);
            collectcount += load_proptype(PROPDEF_HAT);
            collectcount += load_proptype(PROPDEF_CCTV);
            collectcount += load_proptype(PROPDEF_AUTOGUN);
            collectcount += load_proptype(PROPDEF_RACK);
            collectcount += load_proptype(PROPDEF_MONITOR);
            collectcount += load_proptype(PROPDEF_MULTI_MONITOR);
            collectcount += load_proptype(PROPDEF_ARMOUR);
            collectcount += load_proptype(PROPDEF_GAS_RELEASING);
            collectcount += load_proptype(PROPDEF_AMMO);
            collectcount += load_proptype(PROPDEF_VEHICHLE);
            collectcount += load_proptype(PROPDEF_TANK);
            guardcount += load_proptype(PROPDEF_AIRCRAFT);
        }

        fprintf(stderr, "[SETUP-PC] propDefs: %d guards, %d objects\n",
                guardcount, collectcount);

        alloc_init_GUARDdata_entries(guardcount);
        sub_GAME_7F005540(collectcount);
        sub_GAME_7F005624(guardcount);
    }

    /* --- Per-player link slot allocation --- */
    {
        s32 p;
        for (p = 0; p < getPlayerCount(); p++) {
            set_cur_player(p);
            alloc_additional_item_slots(load_proptype(PROPDEF_LINK));
        }
    }

    /* --- Spawn pass: walk PC-native propDefs and initialize objects --- */
    if (g_CurrentSetup.propDefs)
    {
        PropDefHeaderRecord *phead;
        s32 flags;
        s32 pdefIndex;

        flags = 1 << (lvlGetSelectedDifficulty() + 4);
        if (getPlayerCount() >= 2)
            flags |= 1 << (getPlayerCount() + 20);

        phead = g_CurrentSetup.propDefs;
        pdefIndex = 0;

        while (phead->type != PROPDEF_END)
        {
            switch (phead->type)
            {
                case PROPDEF_GUARD:
                    if (withchrs)
                        expand_09_characters(stageId, (struct GuardRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_GUARD_ATTRIBUTE:
                {
                    GuardAttributeRecord *pdef_guarda = (GuardAttributeRecord *)phead;
                    ChrRecord *attr_chr = chrFindByLiteralId(pdef_guarda->chrnum);
                    if (attr_chr && attr_chr->prop && attr_chr->model)
                        attr_chr->grenadeprob = (u8)pdef_guarda->GrenadeProb;
                    break;
                }
                case PROPDEF_DOOR:
                {
                    if (withobjs && !(((struct DoorRecord *)phead)->flags2 & flags))
                        setupDoor(stageId, (struct DoorRecord *)phead, pdefIndex);
                    break;
                }
                case PROPDEF_DOOR_SCALE:
                    g_DoorScale = ((struct GlobalDoorScaleRecord *)phead)->Scale / 65536.0f;
                    break;
                case PROPDEF_COLLECTABLE:
                    if (withchrs && !(((struct WeaponObjRecord *)phead)->flags2 & flags))
                        weaponAssignToHome(stageId, (struct WeaponObjRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_KEY:
                    if (withchrs && !(((struct KeyRecord *)phead)->flags2 & flags))
                        setupKey(stageId, (struct ObjectRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_HAT:
                    if (withchrs && !(((struct ObjectRecord *)phead)->flags2 & flags))
                        setupHat(stageId, (struct ObjectRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_CCTV:
                    if (withobjs && !(((struct CCTVRecord *)phead)->flags2 & flags))
                        setupCctv(stageId, (struct CCTVRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_AUTOGUN:
                    if (withobjs && !(((struct AutogunRecord *)phead)->flags2 & flags))
                        setupAutogun(stageId, (struct AutogunRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_RACK:
                    if (withobjs && !(((struct ObjectRecord *)phead)->flags2 & flags))
                        setupHangingMonitors(stageId, (struct ObjectRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_MONITOR:
                    if (withobjs && !(((struct MonitorObjRecord *)phead)->flags2 & flags))
                        setupSingleMonitor(stageId, (struct MonitorObjRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_MULTI_MONITOR:
                    if (withobjs && !(((struct MultiMonitorObjRecord *)phead)->flags2 & flags))
                        setupMultiMonitor(stageId, (struct MultiMonitorObjRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_ARMOUR:
                {
                    struct BodyArmourRecord *pdef_ba = (struct BodyArmourRecord *)phead;
                    if (withobjs && (pdef_ba->flags2 & flags) == 0)
                    {
                        pdef_ba->initialamount = *(s32 *)&pdef_ba->initialamount / 65536.0f;
                        pdef_ba->amount = pdef_ba->initialamount;
                        domakedefaultobj(stageId, (struct ObjectRecord *)phead, pdefIndex);
                    }
                    break;
                }
                case PROPDEF_PROP:
                case PROPDEF_ALARM:
                case PROPDEF_MAGAZINE:
                case PROPDEF_GAS_RELEASING:
                case PROPDEF_UNK41:
                case PROPDEF_GLASS:
                case PROPDEF_SAFE:
                    if (withobjs && !(((ObjectRecord *)phead)->flags2 & flags))
                        domakedefaultobj(stageId, (struct ObjectRecord *)phead, pdefIndex);
                    break;
                case PROPDEF_TINTED_GLASS:
                {
                    struct TintedGlassRecord *pdef_tintg = (struct TintedGlassRecord *)phead;
                    if (withobjs && !((pdef_tintg)->flags2 & flags))
                    {
                        if (pdef_tintg->flags & PROPFLAG_GLASS_HASPORTAL)
                        {
                            if (!(isNotBoundPad(pdef_tintg->pad)) && g_CurrentSetup.boundpads != NULL)
                            {
                                struct coord3d up;
                                struct coord3d up2;
                                BoundPadRecord *pad3d;
                                f32 scale;

                                pad3d = &g_CurrentSetup.boundpads[getBoundPadNum(pdef_tintg->pad)];

                                sub_GAME_7F001BD4(pad3d, &up);

                                scale = 10.0f;

                                up2.x = (scale * pad3d->up.x) + up.x;
                                up2.y = (scale * pad3d->up.y) + up.y;
                                up2.z = (scale * pad3d->up.z) + up.z;
                                up.x -= scale * pad3d->up.x;
                                up.y -= scale * pad3d->up.y;
                                up.z -= scale * pad3d->up.z;
                                pdef_tintg->portalnum = sub_GAME_7F0B9E04(&up, &up2);
                                pdef_tintg->unk90 = setupFixed16StoredInFloatToFloat(pdef_tintg->unk90);
                            }
                            else
                            {
                                pdef_tintg->unk90 = setupFixed16StoredInFloatToFloat(pdef_tintg->unk90);
                            }
                        }

                        tintedGlassTracePrintf(
                            "spawn index=%d obj=%d pad=%d flags=0x%08x flags2=0x%08x "
                            "tint=%d cull=%d opacity=%d portal=%d unk90=%.4f has_boundpad=%d",
                            pdefIndex,
                            pdef_tintg->obj,
                            pdef_tintg->pad,
                            (unsigned int)pdef_tintg->flags,
                            (unsigned int)pdef_tintg->flags2,
                            pdef_tintg->TintDist,
                            pdef_tintg->CullDist,
                            pdef_tintg->calculatedopacity,
                            pdef_tintg->portalnum,
                            pdef_tintg->unk90,
                            (!(isNotBoundPad(pdef_tintg->pad)) && g_CurrentSetup.boundpads != NULL) ? 1 : 0);

                        domakedefaultobj(stageId, (struct ObjectRecord *)pdef_tintg, pdefIndex);
                    }
                    break;
                }
                case PROPDEF_AMMO:
                {
                    struct MultiAmmoCrateRecord *pdef_macr = (struct MultiAmmoCrateRecord *)phead;
                    s32 ammoqty = 1;
                    s32 slot;

                    /* MP: the crate inherits contents from the weapon-set slot
                     * established by the preceding weapon record (reference
                     * body parity). lastmpweaponnum guard is PC-defensive. */
                    if (getPlayerCount() >= 2 && lastmpweaponnum >= 0)
                    {
                        struct s_mp_weapon_set *mpweapon = &getPtrMPWeaponSetData()[lastmpweaponnum];

                        ammoqty = mpweapon->ammoamount;
                        if (mpweapon->ammotype > 0)
                        {
                            pdef_macr->slots[mpweapon->ammotype - 1].quantity = ammoqty;
                        }
                    }

                    if ((ammoqty > 0) && withobjs && !(pdef_macr->flags2 & flags))
                    {
                        for (slot = 0; slot < AMMOTYPE_GLOBAL_MAX; slot++)
                        {
                            if (pdef_macr->slots[slot].quantity > 0 && pdef_macr->slots[slot].modelnum != 0xFFFF)
                            {
                                modelLoad(pdef_macr->slots[slot].modelnum);
                            }
                        }

                        domakedefaultobj(stageId, (struct ObjectRecord *)pdef_macr, pdefIndex);
                    }
                    break;
                }
                case PROPDEF_TANK:
                    if (withobjs && !(((struct TankRecord *)phead)->flags2 & flags))
                    {
                        struct TankRecord *pdef_tank = (struct TankRecord *)phead;
                        domakedefaultobj(stageId, (struct ObjectRecord *)pdef_tank, pdefIndex);
                    }
                    break;
                case PROPDEF_VEHICHLE:
                    if (withobjs && !(((struct VehichleRecord *)phead)->flags2 & flags))
                    {
                        struct VehichleRecord *pdef_veh = (struct VehichleRecord *)phead;
                        domakedefaultobj(stageId, (struct ObjectRecord *)pdef_veh, pdefIndex);
                        pdef_veh->speed = 0.0f;
                        pdef_veh->wheelxrot = 0.0f;
                        pdef_veh->wheelyrot = 0.0f;
                        pdef_veh->speedaim = 0.0f;
                        pdef_veh->turnrot60 = 0.0f;
                        pdef_veh->roty = 0.0f;
                        pdef_veh->speedtime60 = -1.0f;
                        pdef_veh->ailist = ailistFindById((s32)(uintptr_t)pdef_veh->ailist);
                        pdef_veh->aioffset = 0;
                        pdef_veh->aireturnlist = -1;
                        pdef_veh->path = NULL;
                        pdef_veh->nextstep = 0;
                        pdef_veh->Sound = NULL;
                    }
                    break;
                case PROPDEF_AIRCRAFT:
                    if (withobjs && !(((struct AircraftRecord *)phead)->flags2 & flags))
                    {
                        struct AircraftRecord *pdef_air = (struct AircraftRecord *)phead;
                        domakedefaultobj(stageId, (struct ObjectRecord *)pdef_air, pdefIndex);
                        pdef_air->speed = 0.0f;
                        pdef_air->speedaim = 0.0f;
                        pdef_air->rotoryrot = 0.0f;
                        pdef_air->rotaryspeed = 0.0f;
                        pdef_air->rotaryspeedaim = 0.0f;
                        pdef_air->yrot = 0.0f;
                        pdef_air->speedtime60 = -1.0f;
                        pdef_air->rotaryspeedtime = -1.0f;
                        pdef_air->ailist = ailistFindById((s32)(uintptr_t)pdef_air->ailist);
                        pdef_air->aioffset = 0;
                        pdef_air->aireturnlist = -1;
                        pdef_air->nextstep = 0;
                        pdef_air->path = NULL;
                        pdef_air->Sound = NULL;
                    }
                    break;
                case PROPDEF_TAG:
                {
                    struct TagObjectRecord *pdef_tag = (struct TagObjectRecord *)phead;
                    struct ObjectRecord *taggedobj = setupCommandGetObject(stageId, (s32)pdef_tag->OffsetToObj + pdefIndex);
                    pdef_tag->TaggedObject = taggedobj;
                    if (taggedobj)
                        taggedobj->runtime_bitflags |= 0x10;
                    set_parent_cur_tag_entry(pdef_tag);
                    break;
                }
                case PROPDEF_RENAME:
                {
                    struct RenameObjectRecord *pdef_ren = (struct RenameObjectRecord *)phead;
                    struct ObjectRecord *targetobj = setupCommandGetObject(stageId, pdef_ren->TagID + pdefIndex);
                    pdef_ren->renobj = targetobj;
                    if (targetobj)
                        targetobj->runtime_bitflags |= 0x400;
                    bondinvAddTextOverride((struct textoverride *)pdef_ren);
                    break;
                }
                case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
                    setup_briefing_text_entry_parent((struct setup_objective_text *)phead);
                    break;
                case PROPDEF_CAMERAPOS:
                {
                    struct CutsceneRecord *pdef_cam = (struct CutsceneRecord *)phead;
                    pdef_cam->pos.f[0] = *(s32 *)&pdef_cam->pos.f[0] / 100.0f;
                    pdef_cam->pos.f[1] = *(s32 *)&pdef_cam->pos.f[1] / 100.0f;
                    pdef_cam->pos.f[2] = *(s32 *)&pdef_cam->pos.f[2] / 100.0f;
                    pdef_cam->theta = *(s32 *)&pdef_cam->theta / 65536.0f;
                    pdef_cam->verta = *(s32 *)&pdef_cam->verta / 65536.0f;
                    break;
                }
                case PROPDEF_OBJECTIVE_START:
                    add_ptr_to_objective((struct objective_entry*)phead);
                    break;
                case PROPDEF_OBJECTIVE_ENTER_ROOM:
                    set_parent_cur_obj_enter_room((struct criteria_roomentered *)phead);
                    break;
                case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
                    set_parent_cur_obj_deposited_in_room((struct criteria_deposit *)phead);
                    break;
                case PROPDEF_OBJECTIVE_PHOTOGRAPH:
                    set_parent_cur_obj_photograph((struct criteria_picture *)phead);
                    break;
            }

            phead = (PropDefHeaderRecord *)((u32*)phead + sizepropdef(phead));
            pdefIndex++;
        }

        /* Second pass: inside-another-obj reparenting and weapon links */
        phead = g_CurrentSetup.propDefs;
        pdefIndex = 0;
        while (phead->type != PROPDEF_END)
        {
            switch (phead->type)
            {
                case PROPDEF_PROP:
                case PROPDEF_KEY:
                case PROPDEF_MAGAZINE:
                case PROPDEF_COLLECTABLE:
                case PROPDEF_MONITOR:
                case PROPDEF_AMMO:
                case PROPDEF_ARMOUR:
                case PROPDEF_GAS_RELEASING:
                case PROPDEF_UNK41:
                case PROPDEF_GLASS:
                case PROPDEF_SAFE:
                case PROPDEF_TINTED_GLASS:
                {
                    struct ObjectRecord *objr = (struct ObjectRecord *)phead;
                    if (objr->prop && (objr->flags & PROPFLAG_INSIDEANOTHEROBJ))
                    {
                        s32 offset = objr->pad;
                        struct ObjectRecord *inobj = setupCommandGetObject(stageId, offset + pdefIndex);
                        if (inobj && inobj->prop)
                        {
                            objr->runtime_bitflags |= RUNTIMEBITFLAG_HASOWNER;
                            modelSetScale(objr->model, objr->model->scale);
                            chrpropReparent(objr->prop, inobj->prop);
                        }
                    }
                    break;
                }
                case PROPDEF_LINK:
                {
                    struct LinkRecord *pdef_link = (struct LinkRecord *)phead;
                    struct WeaponObjRecord *guna = (struct WeaponObjRecord *)setupGetPtrToCommandByIndex(pdef_link->Index1 + pdefIndex);
                    struct WeaponObjRecord *gunb = (struct WeaponObjRecord *)setupGetPtrToCommandByIndex(pdef_link->Index2 + pdefIndex);
                    if (guna && gunb && guna->type == PROPDEF_COLLECTABLE && gunb->type == PROPDEF_COLLECTABLE)
                        propweaponSetDual(guna, gunb);
                    break;
                }
                case PROPDEF_SWITCH:
                {
                    struct LinkRecord *pdef_switch = (struct LinkRecord *)phead;
                    s32 index1 = pdef_switch->Index1;
                    s32 index2 = pdef_switch->Index2;
                    struct ObjectRecord *switchobj = (struct ObjectRecord *)setupCommandGetObject(stageId, pdefIndex + index1);
                    struct ObjectRecord *door = (struct ObjectRecord *)setupGetPtrToCommandByIndex(pdefIndex + index2);
                    if (switchobj && switchobj->prop && door && door->type == PROPDEF_DOOR && door->prop)
                    {
                        pdef_switch->first = switchobj->prop;
                        pdef_switch->second = door->prop;
                        initSetLevelLoadPropSwitch(pdef_switch);
                        switchobj->runtime_bitflags |= RUNTIMEBITFLAG_00000001;
                        setupLinkTracePrintf(
                            "kind=switch index=%d switch_index=%d switch_obj=%d switch_pad=%d door_index=%d door_obj=%d door_pad=%d switch_runtime=0x%08x",
                            pdefIndex,
                            pdefIndex + index1,
                            switchobj->obj,
                            switchobj->pad,
                            pdefIndex + index2,
                            door->obj,
                            door->pad,
                            (unsigned int)switchobj->runtime_bitflags);
                    }
                    else
                    {
                        setupLinkTracePrintf(
                            "kind=switch_failed index=%d switch_index=%d door_index=%d switch=%p door=%p",
                            pdefIndex,
                            pdefIndex + index1,
                            pdefIndex + index2,
                            (void *)switchobj,
                            (void *)door);
                    }
                    break;
                }
                case PROPDEF_SAFE_ITEM:
                {
                    struct SafeObjectRecord *pdef_safe = (struct SafeObjectRecord *)phead;
                    s32 index1 = pdef_safe->Index1;
                    s32 index2 = pdef_safe->Index2;
                    s32 index3 = pdef_safe->Index3;
                    struct ObjectRecord *safe_item = setupCommandGetObject(stageId, pdefIndex + index1);
                    struct SafeRecord *safe = (struct SafeRecord *)setupCommandGetObject(stageId, pdefIndex + index2);
                    struct DoorRecord *door = (struct DoorRecord *)setupCommandGetObject(stageId, pdefIndex + index3);

                    if (safe_item
                        && safe_item->prop
                        && safe
                        && safe->prop
                        && safe->type == PROPDEF_SAFE
                        && door
                        && door->prop
                        && door->type == PROPDEF_DOOR)
                    {
                        pdef_safe->item = safe_item;
                        pdef_safe->safe = safe;
                        pdef_safe->door = door;

                        initSetLevelLoadPropSafeItem(pdef_safe);
                        safe_item->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                        door->flags2 |= PROPFLAG2_LINKEDTOSAFE;

                        setupLinkTracePrintf(
                            "kind=safe_item index=%d item_index=%d item_obj=%d item_pad=%d safe_index=%d safe_obj=%d safe_pad=%d door_index=%d door_obj=%d door_pad=%d item_flags2=0x%08x door_flags2=0x%08x",
                            pdefIndex,
                            pdefIndex + index1,
                            safe_item->obj,
                            safe_item->pad,
                            pdefIndex + index2,
                            safe->obj,
                            safe->pad,
                            pdefIndex + index3,
                            door->obj,
                            door->pad,
                            (unsigned int)safe_item->flags2,
                            (unsigned int)door->flags2);
                    }
                    else
                    {
                        setupLinkTracePrintf(
                            "kind=safe_item_failed index=%d item_index=%d safe_index=%d door_index=%d item=%p safe=%p door=%p",
                            pdefIndex,
                            pdefIndex + index1,
                            pdefIndex + index2,
                            pdefIndex + index3,
                            (void *)safe_item,
                            (void *)safe,
                            (void *)door);
                    }
                    break;
                }
                case PROPDEF_LOCK_DOOR:
                {
                    struct LockDoorRecord *pdef_lock_door = (struct LockDoorRecord *)phead;
                    s32 index1 = pdef_lock_door->Index1;
                    s32 index2 = pdef_lock_door->Index2;
                    struct DoorRecord *door = (struct DoorRecord *)setupCommandGetObject(stageId, pdefIndex + index1);
                    struct ObjectRecord *lock = setupCommandGetObject(stageId, pdefIndex + index2);

                    if (door
                        && door->prop
                        && lock
                        && lock->prop
                        && door->type == PROPDEF_DOOR)
                    {
                        pdef_lock_door->door = door;
                        pdef_lock_door->lock = lock;
                        initSetLevelLoadPropLockDoor(pdef_lock_door);
                        door->runtime_bitflags |= RUNTIMEBITFLAG_PADLOCKEDDOOR;

                        setupLinkTracePrintf(
                            "kind=lock_door index=%d door_index=%d door_obj=%d door_pad=%d lock_index=%d lock_obj=%d lock_pad=%d door_runtime=0x%08x",
                            pdefIndex,
                            pdefIndex + index1,
                            door->obj,
                            door->pad,
                            pdefIndex + index2,
                            lock->obj,
                            lock->pad,
                            (unsigned int)door->runtime_bitflags);
                    }
                    else
                    {
                        setupLinkTracePrintf(
                            "kind=lock_door_failed index=%d door_index=%d lock_index=%d door=%p lock=%p",
                            pdefIndex,
                            pdefIndex + index1,
                            pdefIndex + index2,
                            (void *)door,
                            (void *)lock);
                    }
                    break;
                }
            }

            phead = (PropDefHeaderRecord *)((u32*)phead + sizepropdef(phead));
            pdefIndex++;
        }

    }

    alloc_false_GUARDdata_to_exec_global_action();
}
#else
void proplvreset2(s32 stageId) { (void)stageId; }
#endif /* NATIVE_PORT */
#else

// https://decomp.me/scratch/abV9k 99.64%

/*
* mismatch seems to be related to the index variables.
* here are some macros to test theories.
*/
#define DECLARE_i_01_TOP s32 i1 = 0;
#define DECLARE_i_01_LOCAL
#define V_i_01 i1
#define DECLARE_i_02_TOP s32 i2 = 0;
#define DECLARE_i_02_LOCAL
#define V_i_02 i2
#define DECLARE_i_03_TOP s32 i3 = 0;
#define DECLARE_i_03_LOCAL
#define V_i_03 i3
#define DECLARE_i_04_TOP
#define DECLARE_i_04_LOCAL
#define V_i_04 i1
#define DECLARE_i_05_TOP s32 i5 = 0;
#define DECLARE_i_05_LOCAL
#define V_i_05 i5
#define DECLARE_i_06_TOP
#define DECLARE_i_06_LOCAL
#define V_i_06 i1
#define DECLARE_i_07_TOP
#define DECLARE_i_07_LOCAL
#define V_i_07 i1
#define DECLARE_i_08_TOP  s32 i8;
#define DECLARE_i_08_LOCAL
#define V_i_08 i8
#define DECLARE_i_09 s32 i9;
#define V_i_09 i9

// Perfect Dark void setupLoadFiles(s32 stagenum)
void proplvreset2(enum LEVELID stageId)
{
    ItemModelFileRecord *pitem;
    s32 withchrs;
    s32 withobjs;

    withchrs = (tokenFind(1, "-nochr") == NULL && tokenFind(1, "-noprop") == NULL);
    withobjs = (tokenFind(1, "-noobj") == NULL && tokenFind(1, "-noprop") == NULL);

    g_DoorScale = 1.0f;

    for (pitem = PitemZ_entries; pitem < &PitemZ_entries[sizeof(PitemZ_entries) / sizeof(ItemModelFileRecord) -1]; pitem++)
    {
        modelResetHeaderRuntimeState(pitem->header); // modelstate
    }

    if (stageId <= LEVELID_MAX + 1 && setup_text_pointers[stageId])
    {
        char strResource[0x100] = ""; // new empty string
        s32 guardcount = 0;
        s32 collectcount = 0;

        DECLARE_i_01_TOP
        DECLARE_i_02_TOP
        DECLARE_i_03_TOP
        DECLARE_i_04_TOP
        f32 roompos_1; // sp 0x15c
        DECLARE_i_05_TOP
        DECLARE_i_06_TOP
        DECLARE_i_07_TOP
        DECLARE_i_08_TOP
        f32 roompos_2; // sp 0x150

        struct stagesetup *local_stage;

        strResource[0] = setup_text_pointers[stageId][0]; // Get U from "U"setupxxxZ
        strResource[1] = 0;           // make sure string is terminated

        if (getPlayerCount() >= 2)
        {
            strcat(strResource, "mp_"); //  U + "mp_"
        }

        strcat(strResource, setup_text_pointers[stageId] + 1); // Add remaining text back U[mp_] + setupxxxZ

        g_ptrStageSetupFile = _fileNameLoadToBank(strResource, FILELOADMETHOD_DEFAULT, 256, 4); // FILELOADMETHOD_DEFAULT get pointer to setup object (10 words)
        local_stage = g_ptrStageSetupFile;

        langLoadToAddr(langGetLangBankIndexFromStagenum(stageId));

        // deep copy struct, need to manually itterate, as g_CurrentSetup = resource doesnt work
        // Remember Setups store "Offsets" so we need to convert to RAM Address
        g_CurrentSetup.pathwaypoints = TRANSFORM3(pathwaypoints);
        g_CurrentSetup.waypointgroups = TRANSFORM3(waypointgroups);
        g_CurrentSetup.intro = TRANSFORM3(intro);
        g_CurrentSetup.propDefs = TRANSFORM3(propDefs);
        g_CurrentSetup.patrolpaths = TRANSFORM3(patrolpaths);
        g_CurrentSetup.ailists = TRANSFORM3(ailists);
        g_CurrentSetup.pads = TRANSFORM3(pads);
        g_CurrentSetup.boundpads = TRANSFORM3(boundpads);

        if (local_stage->padnames != 0)
        {
            g_CurrentSetup.padnames = TRANSFORM3(padnames);
        }
        else
        {
            g_CurrentSetup.padnames = NULL;
        }

        if (local_stage->boundpadnames != 0)
        {
            g_CurrentSetup.boundpadnames = TRANSFORM3(boundpadnames);
        }
        else
        {
            g_CurrentSetup.boundpadnames = NULL;
        }

        // --------------------------------

        if (g_CurrentSetup.pathwaypoints)
        {
            DECLARE_i_01_LOCAL

            for (V_i_01 = 0; g_CurrentSetup.pathwaypoints[V_i_01].padID >= 0; V_i_01++)
            {
                g_CurrentSetup.pathwaypoints[V_i_01].neighbours = TRANSFORM4(g_CurrentSetup.pathwaypoints[V_i_01].neighbours);
            }
        }

        if (g_CurrentSetup.waypointgroups)
        {
            DECLARE_i_02_LOCAL

            for (V_i_02 = 0; g_CurrentSetup.waypointgroups[V_i_02].neighbours; V_i_02++)
            {
                g_CurrentSetup.waypointgroups[V_i_02].neighbours = TRANSFORM4(g_CurrentSetup.waypointgroups[V_i_02].neighbours);
                g_CurrentSetup.waypointgroups[V_i_02].waypoints = TRANSFORM4(g_CurrentSetup.waypointgroups[V_i_02].waypoints);
            }
        }

        // Convert ailist pointers from file-local to proper pointers
        if (g_CurrentSetup.ailists)
        {
            DECLARE_i_03_LOCAL

            for (V_i_03 = 0; g_CurrentSetup.ailists[V_i_03].ailist != 0; V_i_03++)
            {
                g_CurrentSetup.ailists[V_i_03].ailist = TRANSFORM4(g_CurrentSetup.ailists[V_i_03].ailist);
            }
        }

        if (g_CurrentSetup.patrolpaths)
        {
            DECLARE_i_04_LOCAL

            for (V_i_04 = 0; g_CurrentSetup.patrolpaths[V_i_04].waypoints != NULL; V_i_04++)
            {
                DECLARE_i_05_LOCAL

                g_CurrentSetup.patrolpaths[V_i_04].waypoints = TRANSFORM4(g_CurrentSetup.patrolpaths[V_i_04].waypoints);

                for (V_i_05 = 0; g_CurrentSetup.patrolpaths[V_i_04].waypoints[V_i_05] >= 0; V_i_05++)
                {
                }

                g_CurrentSetup.patrolpaths[V_i_04].len = V_i_05;
            }
        }

        // --------------------------------

        if (g_CurrentSetup.pads)
        {
            //f32 roompos_1; // sp 0x15c
            struct PadRecord *pad;
            //s32 stan_result;

            roompos_1 = get_room_data_float2();
            pad = g_CurrentSetup.pads;

            for (; pad->plink != NULL; pad++)
            {
                pad->plink = TRANSFORM4(pad->plink);
                pad->pos.f[0] *= roompos_1;
                pad->pos.f[1] *= roompos_1;
                pad->pos.f[2] *= roompos_1;

                init_pathtable_something(pad, pad->plink, &pad->stan);

                if (1);
                //if(pad);
#ifdef DEBUG
                if (stan_result == 0)
                {
                    osSyncPrintf("pad number %d has no stan! (%s)\n", i, pad[i].plink);
                }
                else if (stan_result == 2)
                {
                    osSyncPrintf("pad number %d changed stan from %s to %s\n", i, pad[i].plink, GetStanName(pad[i].stan));
                }
#endif
            }
        }


        if (g_CurrentSetup.boundpads)
        {
            struct BoundPadRecord *vol;
            //f32 roompos_2; // sp 0x150
            //s32 stan_result;

            roompos_2 = get_room_data_float2();
            vol = g_CurrentSetup.boundpads;

            for (; vol->plink != NULL; vol++)
            {
                vol->plink = TRANSFORM4(vol->plink);

                vol->pos.f[0] *= roompos_2;
                vol->pos.f[1] *= roompos_2;
                vol->pos.f[2] *= roompos_2;
                vol->bbox.xmin *= roompos_2;
                vol->bbox.xmax *= roompos_2;
                vol->bbox.ymin *= roompos_2;
                vol->bbox.ymax *= roompos_2;
                vol->bbox.zmin *= roompos_2;
                vol->bbox.zmax *= roompos_2;

                init_pathtable_something((struct PadRecord*)vol, vol->plink, &vol->stan);

                if(1);
                //if (vol);

#ifdef DEBUG
                if (stan_result == 0)
                {
                    osSyncPrintf("vol number %d has no stan! (%s)\n", i, vol[i].plink);
                }
                else if (stan_result == 2)
                {
                    osSyncPrintf("vol number %d changed stan from %s to %s\n", i, vol[i].plink, GetStanName(vol[i].stan));
                }
#endif
            }
        }

        {
            DECLARE_i_06_LOCAL
            DECLARE_i_07_LOCAL

            if (g_CurrentSetup.padnames)
            {
                for (V_i_06 = 0; g_CurrentSetup.padnames[V_i_06].p; V_i_06++)
                {
                    g_CurrentSetup.padnames[V_i_06].p = TRANSFORM4(g_CurrentSetup.padnames[V_i_06].p);
                }
            }

            if (g_CurrentSetup.boundpadnames)
            {
                for (V_i_07 = 0; g_CurrentSetup.boundpadnames[V_i_07].p; V_i_07++)
                {
                    g_CurrentSetup.boundpadnames[V_i_07].p = TRANSFORM4(g_CurrentSetup.boundpadnames[V_i_07].p);
                }
            }
        }

        // PD rejoins here


        if (withchrs)
        {
            alloc_init_GUARDdata_entries(load_proptype(PROPDEF_GUARD));
            guardcount += load_proptype(PROPDEF_GUARD);
            collectcount += load_proptype(PROPDEF_COLLECTABLE);
            collectcount += load_proptype(PROPDEF_KEY);
            collectcount += load_proptype(PROPDEF_HAT);
        }
        else
        {
            alloc_init_GUARDdata_entries(0); // chrmgrConfigure
        }

        if (withobjs)
        {
            // load std props for all stages
            collectcount += load_proptype(PROPDEF_DOOR);
            collectcount += load_proptype(PROPDEF_CCTV);
            collectcount += load_proptype(PROPDEF_AUTOGUN);
            collectcount += load_proptype(PROPDEF_RACK);
            collectcount += load_proptype(PROPDEF_MONITOR);
            collectcount += load_proptype(PROPDEF_MULTI_MONITOR);
            collectcount += load_proptype(PROPDEF_ARMOUR);
            collectcount += load_proptype(PROPDEF_PROP);
            collectcount += load_proptype(PROPDEF_GLASS);
            collectcount += load_proptype(PROPDEF_TINTED_GLASS);
            collectcount += load_proptype(PROPDEF_SAFE);
            collectcount += load_proptype(PROPDEF_UNK41);
            collectcount += load_proptype(PROPDEF_GAS_RELEASING);
            collectcount += load_proptype(PROPDEF_ALARM);
            collectcount += load_proptype(PROPDEF_MAGAZINE);
            collectcount += load_proptype(PROPDEF_AMMO);
            collectcount += load_proptype(PROPDEF_VEHICHLE);
            collectcount += load_proptype(PROPDEF_TANK);
            guardcount += load_proptype(PROPDEF_AIRCRAFT);
        }

        sub_GAME_7F005540(collectcount); // without plane
        sub_GAME_7F005624(guardcount);

        //if (1)
        {
            DECLARE_i_08_LOCAL

            for (V_i_08 = 0; V_i_08 < getPlayerCount(); V_i_08++)
            {
                set_cur_player(V_i_08);
                alloc_additional_item_slots(load_proptype(PROPDEF_LINK));
            }
        }

        if (g_CurrentSetup.propDefs)
        {
            PropDefHeaderRecord *phead;
            s32 flags;
            s32 pdefIndex;
            //s32 padding;

            flags = 1 << (lvlGetSelectedDifficulty() + 4);

            if (getPlayerCount() >= 2)
            {
                flags |= 1 << (getPlayerCount() + 20);
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while (phead->type != PROPDEF_END)
            {
                switch (phead->type)
                {
                    // switch 1
                    case PROPDEF_GUARD_ATTRIBUTE: // switch 1
                    {
                        GuardAttributeRecord *pdef_guarda;
                        u8 prob;
                        ChrRecord *chr;

                        pdef_guarda = (GuardAttributeRecord *)phead;
                        prob = (u8)pdef_guarda->GrenadeProb;
                        chr  = chrFindByLiteralId(pdef_guarda->chrnum);

                        if (chr && chr->prop && chr->model)
                        {
                            chr->grenadeprob = prob;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("grenade prob: no chr number %d for obj number %d! ", phead->unkB, pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_GUARD: // switch 1
                        if (withchrs)
                        {
                            expand_09_characters(stageId, (struct GuardRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_DOOR: // switch 1
                        if (withobjs && !(((struct DoorRecord *)phead)->flags2 & flags))
                        {
                            setupDoor(stageId, (struct DoorRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_DOOR_SCALE: // switch 1
                        g_DoorScale = ((struct GlobalDoorScaleRecord *)phead)->Scale / 65536.0f;
                        break;
                    case PROPDEF_COLLECTABLE: // switch 1
                        if (withchrs && !(((struct WeaponObjRecord *)phead)->flags2 & flags))
                        {
                            weaponAssignToHome(stageId, (struct WeaponObjRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_KEY: // switch 1
                        if (withchrs && !(((struct KeyRecord *)phead)->flags2 & flags))
                        {
                            setupKey(stageId, (struct ObjectRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_HAT: // switch 1
                        if (withchrs && !(((struct ObjectRecord *)phead)->flags2 & flags))
                        {
                            setupHat(stageId, (struct ObjectRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_CCTV: // switch 1
                        if (withobjs && !(((struct CCTVRecord *)phead)->flags2 & flags))
                        {
                            setupCctv(stageId, (struct CCTVRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AUTOGUN: // switch 1
                        if (withobjs && !(((struct AutogunRecord *)phead)->flags2 & flags))
                        {
                            setupAutogun(stageId, (struct AutogunRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_RACK: // switch 1
                        if (withobjs && !(((struct ObjectRecord *)phead)->flags2 & flags))
                        {
                            setupHangingMonitors(stageId, (struct ObjectRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MONITOR: // switch 1
                        if (withobjs && !(((struct MonitorObjRecord *)phead)->flags2 & flags))
                        {
                            setupSingleMonitor(stageId, (struct MonitorObjRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MULTI_MONITOR: // switch 1
                        if (withobjs && !(((struct MultiMonitorObjRecord *)phead)->flags2 & flags))
                        {
                            setupMultiMonitor(stageId, (struct MultiMonitorObjRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_ARMOUR: // switch 1
                    {
                        struct BodyArmourRecord *pdef_ba = (struct BodyArmourRecord *)phead;

#if defined(VERSION_US)
                        if (withobjs && (pdef_ba->flags2 & flags) == 0)
#elif defined(VERSION_JP) || defined(VERSION_EU)
                        if (withobjs && ((pdef_ba->flags2 & flags) == 0 || j_text_trigger))
#else
    #error version
#endif
                        {
                            pdef_ba->initialamount = *(s32 *)&pdef_ba->initialamount / 65536.0f;
                            pdef_ba->amount        = pdef_ba->initialamount;

                            domakedefaultobj(stageId, (struct ObjectRecord *)phead, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TINTED_GLASS: // switch 1
                    {
                        struct TintedGlassRecord *pdef_tintg = (struct TintedGlassRecord *)phead;
                        if (withobjs && !((pdef_tintg)->flags2 & flags))
                        {
                            if (pdef_tintg->flags & PROPFLAG_GLASS_HASPORTAL)
                            {
                                if (!(isNotBoundPad(pdef_tintg->pad)))
                                {
                                    struct coord3d up;
                                    struct coord3d up2;
                                    BoundPadRecord *pad3d;
                                    f32 scale;

                                    pad3d = &g_CurrentSetup.boundpads[getBoundPadNum(pdef_tintg->pad)];

                                    sub_GAME_7F001BD4(pad3d, &up);

                                    if(pad3d);

                                    scale = 10.0f;

                                    up2.x = (scale * pad3d->up.x) + up.x;
                                    up2.y = (scale * pad3d->up.y) + up.y;
                                    up2.z = (scale * pad3d->up.z) + up.z;
                                    up.x -= scale * pad3d->up.x;
                                    up.y -= scale * pad3d->up.y;
                                    up.z -= scale * pad3d->up.z;
                                    pdef_tintg->portalnum = sub_GAME_7F0B9E04(&up, &up2);
                                    pdef_tintg->unk90 = *(s32 *)&pdef_tintg->unk90 / 65536.0f;
                                }
                            }
                            domakedefaultobj(stageId, (struct ObjectRecord *)pdef_tintg, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_PROP:          // switch 1
                    case PROPDEF_ALARM:         // switch 1
                    case PROPDEF_MAGAZINE:      // switch 1
                    case PROPDEF_GAS_RELEASING: // switch 1
                    case PROPDEF_UNK41:         // switch 1
                    case PROPDEF_GLASS:         // switch 1
                    case PROPDEF_SAFE:          // switch 1
                        if (withobjs && !(((ObjectRecord *)phead)->flags2 & flags))
                        {
                            domakedefaultobj(stageId, (struct ObjectRecord *)phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AMMO: // switch 1
                    {
                        struct MultiAmmoCrateRecord *pdef_macr = (struct MultiAmmoCrateRecord *)phead;

                        s32 ammoqty = 1;
                        DECLARE_i_09

                        if (getPlayerCount() >= 2)
                        {
                            struct s_mp_weapon_set *mpweapon = &getPtrMPWeaponSetData()[lastmpweaponnum];

                            ammoqty = mpweapon->ammoamount;
                            if(mpweapon->ammotype);
                            pdef_macr->slots[mpweapon->ammotype - 1].quantity = ammoqty;
                        }

                        if ((ammoqty > 0) && withobjs && !(pdef_macr->flags2 & flags))
                        {
                            for (V_i_09 = 0; V_i_09 < (AMMOTYPE_GLOBAL_MAX ); V_i_09++)
                            {
                                if (pdef_macr->slots[V_i_09].quantity > 0 && pdef_macr->slots[V_i_09].modelnum != 0xFFFF)
                                {
                                    modelLoad(pdef_macr->slots[V_i_09].modelnum);
                                }
                            }

                            domakedefaultobj(stageId, (struct ObjectRecord *)pdef_macr, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TANK: // switch 1
                        if (withobjs && !(((struct TankRecord *)phead)->flags2 & flags))
                        {
                            struct TankRecord *pdef_tank = (struct TankRecord *)phead;
                            struct PropRecord *tank_prop;
                            f32 stan_y = 0.0f;
                            s32 paddinggg[4];

                            weaponLoadProjectileModels(0x20);
                            domakedefaultobj(stageId, (struct ObjectRecord *)pdef_tank, pdefIndex);
                            pdef_tank->turret_vertical_angle = 0.0f;
                            pdef_tank->turret_orientation_angle = 0.0f;
                            pdef_tank->tank_orientation_angle = M_TAU_F - atan2f(pdef_tank->mtx.m[2][0], pdef_tank->mtx.m[2][2]);
                            tank_prop = pdef_tank->prop;
                            if (tank_prop)
                            {
                                stan_y = stanGetPositionYValue(tank_prop->stan, tank_prop->pos.f[0], tank_prop->pos.f[2]);
                            }
                            pdef_tank->stan_y = stan_y;
                            pdef_tank->unkD0  = stan_y / 0.17000002f;
                        }
                        break;
                    case PROPDEF_VEHICHLE: // switch 1
                        if (withobjs && !(((struct VehichleRecord *)phead)->flags2 & flags))
                        {
                            struct VehichleRecord *pdef_veh = (struct VehichleRecord *)phead;
                            //struct Model *model;
                            domakedefaultobj(stageId, (struct ObjectRecord *)pdef_veh, pdefIndex);

                            //model = pdef_veh->model;
                            if (pdef_veh->model != NULL)
                            {
#ifdef NATIVE_PORT
                                ModelNode *vehicle_switch5 = modelGetSwitchNodeSafe(pdef_veh->model->obj, 5);
                                if (vehicle_switch5 != NULL)
                                {
                                    modelGetNodeRwData(pdef_veh->model, vehicle_switch5)->Raw.unk00 = (pdef_veh->flags & 0x10000000) == 0;
                                }
#else
                                if (pdef_veh->model->obj->Switches[5] != NULL)
                                {
                                    modelGetNodeRwData(pdef_veh->model, pdef_veh->model->obj->Switches[5])->Raw.unk00 = (pdef_veh->flags & 0x10000000) == 0;
                                }
#endif
                            }

                            pdef_veh->speed        = 0.0f;
                            pdef_veh->wheelxrot    = 0.0f;
                            pdef_veh->wheelyrot    = 0.0f;
                            pdef_veh->speedaim     = 0.0f;
                            pdef_veh->turnrot60    = 0.0f;
                            pdef_veh->roty         = 0.0f;
                            pdef_veh->speedtime60  = -1.0f;
                            pdef_veh->ailist       = ailistFindById(pdef_veh->ailist);
                            pdef_veh->aioffset     = 0;
                            pdef_veh->aireturnlist = -1;
                            pdef_veh->path         = 0;
                            pdef_veh->nextstep     = 0;
                            pdef_veh->Sound        = 0;
                        }
                        break;
                    case PROPDEF_AIRCRAFT: // switch 1
                        if (withobjs && !(((struct AircraftRecord *)phead)->flags2 & flags))
                        {
                            struct AircraftRecord *pdef_air = (struct AircraftRecord *)phead;
                            domakedefaultobj(stageId, (struct ObjectRecord *)pdef_air, pdefIndex);
                            pdef_air->speed           = 0.0f;
                            pdef_air->speedaim        = 0.0f;
                            pdef_air->rotoryrot       = 0.0f;
                            pdef_air->rotaryspeed     = 0.0f;
                            pdef_air->rotaryspeedaim  = 0.0f;
                            pdef_air->yrot            = 0.0f;
                            pdef_air->speedtime60     = -1.0f;
                            pdef_air->rotaryspeedtime = -1.0f;
                            pdef_air->ailist          = ailistFindById(pdef_air->ailist);
                            pdef_air->aioffset        = 0;
                            pdef_air->aireturnlist    = -1;
                            pdef_air->nextstep        = 0;
                            pdef_air->path            = 0;
                            pdef_air->Sound           = 0;
                        }
                        break;
                    case PROPDEF_TAG: // switch 1
                    {
                        struct TagObjectRecord *pdef_tag;
                        struct ObjectRecord *taggedobj;

                        pdef_tag = (struct TagObjectRecord *)phead;
                        taggedobj  = setupCommandGetObject(stageId, (s32)pdef_tag->OffsetToObj + pdefIndex);
                        pdef_tag->TaggedObject = taggedobj;
                        if (taggedobj)
                        {
                            taggedobj->runtime_bitflags |= 0x10; // RUNTIMEBITFLAG_TAGGED
                        }
                        set_parent_cur_tag_entry(pdef_tag);
                        break;
                    }
                    case PROPDEF_RENAME: // switch 1
                    {
                        struct RenameObjectRecord *pdef_ren;
                        struct ObjectRecord *targetobj;

                        pdef_ren = (struct RenameObjectRecord *)phead;
                        targetobj = setupCommandGetObject(stageId, pdef_ren->TagID + pdefIndex);
                        pdef_ren->renobj = targetobj;
                        if (targetobj)
                        {
                            targetobj->runtime_bitflags |= 0x400; // RUNTIMEBITFLAG_HASTEXTOVERRIDE
                        }
                        bondinvAddTextOverride((struct textoverride *)pdef_ren);
                        break;
                    }
                    case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: // switch 1
                        setup_briefing_text_entry_parent((struct setup_objective_text *)phead);
                        break;
                    case PROPDEF_CAMERAPOS: // switch 1
                    {
                        struct CutsceneRecord *pdef_cam = (struct CutsceneRecord *)phead;
                        pdef_cam->pos.f[0] = *(s32 *)&pdef_cam->pos.f[0] / 100.0f;
                        pdef_cam->pos.f[1] = *(s32 *)&pdef_cam->pos.f[1] / 100.0f;
                        pdef_cam->pos.f[2] = *(s32 *)&pdef_cam->pos.f[2] / 100.0f;
                        pdef_cam->theta = *(s32 *)&pdef_cam->theta / 65536.0f;
                        pdef_cam->verta = *(s32 *)&pdef_cam->verta / 65536.0f;
                        break;
                    }
                    case PROPDEF_OBJECTIVE_START: // switch 1
                        add_ptr_to_objective((struct objective_entry*)phead);
                        break;
                    case PROPDEF_OBJECTIVE_ENTER_ROOM: // switch 1
                        set_parent_cur_obj_enter_room((struct criteria_roomentered *)phead);
                        break;
                    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: // switch 1
                        set_parent_cur_obj_deposited_in_room((struct criteria_deposit *)phead);
                        break;
                    case PROPDEF_OBJECTIVE_PHOTOGRAPH: // switch 1
                        set_parent_cur_obj_photograph((struct criteria_picture *)phead);
                        break;
                }
                //phead = sizepropdef(phead) + phead;
                phead = (PropDefHeaderRecord *)((u32*)phead + sizepropdef(phead));

                pdefIndex++;
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while (phead->type != PROPDEF_END)
            {
                switch (phead->type)
                {
                    case PROPDEF_PROP:
                    case PROPDEF_KEY:
                    case PROPDEF_MAGAZINE:
                    case PROPDEF_COLLECTABLE:
                    case PROPDEF_MONITOR:
                    case PROPDEF_AMMO:
                    case PROPDEF_ARMOUR:
                    case PROPDEF_GAS_RELEASING:
                    case PROPDEF_UNK41:
                    case PROPDEF_GLASS:
                    case PROPDEF_SAFE:
                    case PROPDEF_TINTED_GLASS:
                    {
                        struct ObjectRecord *pdef_tintg = (struct ObjectRecord *)phead;

                        if (pdef_tintg->prop && (pdef_tintg->flags & PROPFLAG_INSIDEANOTHEROBJ)) // PROPFLAG_INSIDEANOTHEROBJ
                        {
                            s32 offset = pdef_tintg->pad;
                            struct ObjectRecord *inobj = setupCommandGetObject(stageId, offset + pdefIndex);

                            if (inobj && inobj->prop)
                            {
                                //struct Model *inobjmdl = pdef_tintg->model;
                                pdef_tintg->runtime_bitflags |= RUNTIMEBITFLAG_HASOWNER; // RUNTIMEBITFLAG_HASOWNER
                                modelSetScale(pdef_tintg->model, pdef_tintg->model->scale);
                                chrpropReparent(pdef_tintg->prop, inobj->prop);
                            }
#ifdef DEBUG

                            else
                            {
                                osSyncPrintf("inobj link not found for object number %d\n", pdefIndex + 1);
                            }
#endif
                        }
                        break;
                    }
                    case PROPDEF_LINK:
                    {
                        struct LinkRecord *pdef_link = (struct LinkRecord *)phead;
                        struct WeaponObjRecord *guna = (struct WeaponObjRecord *)setupGetPtrToCommandByIndex(pdef_link->Index1 + pdefIndex);
                        struct WeaponObjRecord *gunb = (struct WeaponObjRecord *)setupGetPtrToCommandByIndex(pdef_link->Index2 + pdefIndex);
                        if (guna && gunb)
                        {
                            if (guna->type == PROPDEF_COLLECTABLE && gunb->type == PROPDEF_COLLECTABLE)
                            {
                                propweaponSetDual(guna, gunb);
                            }
#ifdef DEBUG
                            else
                            {
                                osSyncPrintf("link type wrong for doublegun object number %d\n", pdefIndex + 1);
                            }
                        }
                        else
                        {
                            osSyncPrintf("link not found for doublegun object number %d\n", pdefIndex + 1);

#endif
                        }
                        break;
                    }
                    case PROPDEF_SWITCH:
                    {
                        struct LinkRecord *pdef_switch;
                        struct ObjectRecord *doorA;
                        struct ObjectRecord *doorB;

                        s32 index1;
                        s32 index2;

                        pdef_switch  = (struct LinkRecord *)phead;

                        index1 = pdef_switch->Index1;
                        index2 = pdef_switch->Index2;

                        doorA = (struct ObjectRecord *)setupCommandGetObject(stageId, pdefIndex + index1);
                        doorB = (struct ObjectRecord *)setupGetPtrToCommandByIndex(pdefIndex + index2);

                        if (doorA && doorA->prop && doorB && doorB->type == PROPDEF_DOOR && doorB->prop)
                        {
                            pdef_switch->first = doorA->prop;
                            pdef_switch->second = doorB->prop;
                            initSetLevelLoadPropSwitch(pdef_switch);
                            doorA->runtime_bitflags |= RUNTIMEBITFLAG_00000001; // linkeddoor
                        }

#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_SAFE_ITEM:
                    {

                        s32 index1;
                        struct SafeObjectRecord *pdef_safe;
                        s32 index2;
                        s32 index3;
                        struct ObjectRecord *safe_item;
                        struct SafeRecord *safe;
                        struct DoorRecord *door;

                        pdef_safe = (struct SafeObjectRecord *)phead;

                        index1 = pdef_safe->Index1;
                        index2 = pdef_safe->Index2;
                        index3 = pdef_safe->Index3;

                        safe_item = setupCommandGetObject(stageId, pdefIndex + index1);
                        safe = (struct SafeRecord *)setupCommandGetObject(stageId, pdefIndex + index2);
                        door = (struct DoorRecord *)setupCommandGetObject(stageId, pdefIndex + index3);

                        if (safe_item
                            && safe_item->prop
                            && safe
                            && safe->prop
                            && safe->type == PROPDEF_SAFE
                            && door
                            && door->prop
                            && door->type == PROPDEF_DOOR)
                        {
                            pdef_safe->item = safe_item;
                            pdef_safe->safe = safe;
                            pdef_safe->door = door;

                            initSetLevelLoadPropSafeItem(pdef_safe);
                            safe_item->flags2 |= PROPFLAG2_LINKEDTOSAFE; // OBJFLAG2_LINKEDTOSAFE
                            door->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("safelink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_LOCK_DOOR:
                    {
                        struct LockDoorRecord *pdef_lock_door;
                        struct DoorRecord *door;
                        struct ObjectRecord *lock;
                        s32 index1;
                        s32 index2;


                        pdef_lock_door = (struct LockDoorRecord *)phead;

                        index1 = pdef_lock_door->Index1;
                        index2 = pdef_lock_door->Index2;

                        door = (struct DoorRecord *)setupCommandGetObject(stageId, pdefIndex + index1);
                        lock = setupCommandGetObject(stageId, pdefIndex + index2);

                        if (door
                            && door->prop
                            && lock
                            && lock->prop
                            && door->type == PROPDEF_DOOR)
                        {
                            pdef_lock_door->door = door;
                            pdef_lock_door->lock = lock;
                            initSetLevelLoadPropLockDoor(pdef_lock_door);
                            door->runtime_bitflags |= RUNTIMEBITFLAG_PADLOCKEDDOOR; // padlock
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlock object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                }
                //phead = sizepropdef(phead) + phead;
                phead = (PropDefHeaderRecord *)((u32*)phead + sizepropdef(phead));

                pdefIndex += 1;
            } //while !PDEF_END

        } //if setup.pdef
    }
    else
    {
        g_CurrentSetup.pathwaypoints = NULL;
        g_CurrentSetup.waypointgroups = NULL;
        g_CurrentSetup.intro = 0;
        g_CurrentSetup.propDefs = 0;
        g_CurrentSetup.patrolpaths = NULL;
        g_CurrentSetup.ailists = NULL;
        g_CurrentSetup.pads = NULL;
        g_CurrentSetup.boundpads = NULL;
        g_CurrentSetup.padnames = NULL;
        g_CurrentSetup.boundpadnames = NULL;
        alloc_init_GUARDdata_entries(0);
        sub_GAME_7F005540(0);
        sub_GAME_7F005624(0);
    }

    alloc_false_GUARDdata_to_exec_global_action();
}
#endif /* PORT_FIXME_STUBS */

#else

#ifdef VERSION_US
GLOBAL_ASM(
.late_rodata
glabel jpt_object_type_expansion
.word door_expand
.word door_scale_expand
.word obj_03_expand
.word key_expand
.word obj_03_expand
.word surv_camera_expand
.word obj_03_expand
.word item_expand
.word actor_expand
.word single_screen_display_expand
.word multi_screen_display_expand
.word rack_expand
.word autogun_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word hat_expand
.word actor_attr_expand
.word other_obj_expand
.word ammo_box_expand
.word body_armor_expand
.word tag_expand
.word type17_objective_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word type1E_subobjective_expand
.word other_obj_expand
.word type20_subobjective_expand
.word type21_subobjective_expand
.word other_obj_expand
.word type23_solo_brief_text
.word obj_03_expand
.word type25_rename
.word other_obj_expand
.word type27_wheeled_vehicle
.word type28_aircraft
.word obj_03_expand
.word obj_03_expand
.word obj_03_expand
.word other_obj_expand
.word type2D_tank
.word type2E_viewport_pos
.word type2F_tinted_glass

glabel D_8004F024
.word 0x40c90fdb
glabel D_8004F028
.word 0x3e2e147c

glabel jpt_8004F02C
.word .L7F004B6C
.word .L7F004B6C
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004B6C
.word .L7F004B6C
.word .L7F004DE4
.word .L7F004B6C
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004BD8
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004C2C
.word .L7F004B6C
.word .L7F004B6C
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004B6C
.word .L7F004DE4
.word .L7F004D70
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004B6C
.word .L7F004B6C
.word .L7F004B6C
.word .L7F004CA4
.word .L7F004DE4
.word .L7F004DE4
.word .L7F004B6C

.text
glabel proplvreset2
/* 038720 7F003BF0 27BDFD80 */  addiu $sp, $sp, -0x280
/* 038724 7F003BF4 AFB50034 */  sw    $s5, 0x34($sp)
/* 038728 7F003BF8 0080A825 */  move  $s5, $a0
/* 03872C 7F003BFC AFBF0044 */  sw    $ra, 0x44($sp)
/* 038730 7F003C00 3C058005 */  lui   $a1, %hi(aNochr)
/* 038734 7F003C04 AFBE0040 */  sw    $fp, 0x40($sp)
/* 038738 7F003C08 AFB7003C */  sw    $s7, 0x3c($sp)
/* 03873C 7F003C0C AFB60038 */  sw    $s6, 0x38($sp)
/* 038740 7F003C10 AFB40030 */  sw    $s4, 0x30($sp)
/* 038744 7F003C14 AFB3002C */  sw    $s3, 0x2c($sp)
/* 038748 7F003C18 AFB20028 */  sw    $s2, 0x28($sp)
/* 03874C 7F003C1C AFB10024 */  sw    $s1, 0x24($sp)
/* 038750 7F003C20 AFB00020 */  sw    $s0, 0x20($sp)
/* 038754 7F003C24 F7B40018 */  sdc1  $f20, 0x18($sp)
/* 038758 7F003C28 24A5ED90 */  addiu $a1, %lo(aNochr) # addiu $a1, $a1, -0x1270
/* 03875C 7F003C2C 0C0029A8 */  jal   tokenFind
/* 038760 7F003C30 24040001 */   li    $a0, 1
/* 038764 7F003C34 2C460001 */  sltiu $a2, $v0, 1
/* 038768 7F003C38 10C00005 */  beqz  $a2, .L7F003C50
/* 03876C 7F003C3C 24040001 */   li    $a0, 1
/* 038770 7F003C40 3C058005 */  lui   $a1, %hi(aNoprop)
/* 038774 7F003C44 0C0029A8 */  jal   tokenFind
/* 038778 7F003C48 24A5ED98 */   addiu $a1, %lo(aNoprop) # addiu $a1, $a1, -0x1268
/* 03877C 7F003C4C 2C460001 */  sltiu $a2, $v0, 1
.L7F003C50:
/* 038780 7F003C50 3C058005 */  lui   $a1, %hi(aNoobj)
/* 038784 7F003C54 AFA60278 */  sw    $a2, 0x278($sp)
/* 038788 7F003C58 24A5EDA0 */  addiu $a1, %lo(aNoobj) # addiu $a1, $a1, -0x1260
/* 03878C 7F003C5C 0C0029A8 */  jal   tokenFind
/* 038790 7F003C60 24040001 */   li    $a0, 1
/* 038794 7F003C64 2C430001 */  sltiu $v1, $v0, 1
/* 038798 7F003C68 10600005 */  beqz  $v1, .L7F003C80
/* 03879C 7F003C6C 24040001 */   li    $a0, 1
/* 0387A0 7F003C70 3C058005 */  lui   $a1, %hi(aNoprop_0)
/* 0387A4 7F003C74 0C0029A8 */  jal   tokenFind
/* 0387A8 7F003C78 24A5EDA8 */   addiu $a1, %lo(aNoprop_0) # addiu $a1, $a1, -0x1258
/* 0387AC 7F003C7C 2C430001 */  sltiu $v1, $v0, 1
.L7F003C80:
/* 0387B0 7F003C80 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 0387B4 7F003C84 44812000 */  mtc1  $at, $f4
/* 0387B8 7F003C88 0060F025 */  move  $fp, $v1
/* 0387BC 7F003C8C 3C018003 */  lui   $at, %hi(g_DoorScale)
/* 0387C0 7F003C90 3C038004 */  lui   $v1, %hi(PitemZ_entries+0xFF0)
/* 0387C4 7F003C94 3C028004 */  lui   $v0, %hi(PitemZ_entries)
/* 0387C8 7F003C98 2442A228 */  addiu $v0, %lo(PitemZ_entries) # addiu $v0, $v0, -0x5dd8
/* 0387CC 7F003C9C 2463B218 */  addiu $v1, $v1, %lo(PitemZ_entries+0xFF0)
/* 0387D0 7F003CA0 E424A3C0 */  swc1  $f4, %lo(g_DoorScale)($at)
.L7F003CA4:
/* 0387D4 7F003CA4 8C4E0000 */  lw    $t6, ($v0)
/* 0387D8 7F003CA8 2442000C */  addiu $v0, $v0, 0xc
/* 0387DC 7F003CAC 0043082B */  sltu  $at, $v0, $v1
/* 0387E0 7F003CB0 1420FFFC */  bnez  $at, .L7F003CA4
/* 0387E4 7F003CB4 ADC00000 */   sw    $zero, ($t6)
/* 0387E8 7F003CB8 2AA1003B */  slti  $at, $s5, 0x3b
/* 0387EC 7F003CBC 10200454 */  beqz  $at, .L7F004E10
/* 0387F0 7F003CC0 00157880 */   sll   $t7, $s5, 2
/* 0387F4 7F003CC4 3C188003 */  lui   $t8, %hi(setup_text_pointers)
/* 0387F8 7F003CC8 271874E4 */  addiu $t8, %lo(setup_text_pointers) # addiu $t8, $t8, 0x74e4
/* 0387FC 7F003CCC 01F88021 */  addu  $s0, $t7, $t8
/* 038800 7F003CD0 8E020000 */  lw    $v0, ($s0)
/* 038804 7F003CD4 3C198005 */  lui   $t9, %hi(only_read_by_stageload)
/* 038808 7F003CD8 27B10174 */  addiu $s1, $sp, 0x174
/* 03880C 7F003CDC 1040044C */  beqz  $v0, .L7F004E10
/* 038810 7F003CE0 2739EDB0 */   addiu $t9, %lo(only_read_by_stageload) # addiu $t9, $t9, -0x1250
/* 038814 7F003CE4 02205025 */  move  $t2, $s1
/* 038818 7F003CE8 272900FC */  addiu $t1, $t9, 0xfc
.L7F003CEC:
/* 03881C 7F003CEC 8F210000 */  lw    $at, ($t9)
/* 038820 7F003CF0 2739000C */  addiu $t9, $t9, 0xc
/* 038824 7F003CF4 254A000C */  addiu $t2, $t2, 0xc
/* 038828 7F003CF8 AD41FFF4 */  sw    $at, -0xc($t2)
/* 03882C 7F003CFC 8F21FFF8 */  lw    $at, -8($t9)
/* 038830 7F003D00 AD41FFF8 */  sw    $at, -8($t2)
/* 038834 7F003D04 8F21FFFC */  lw    $at, -4($t9)
/* 038838 7F003D08 1729FFF8 */  bne   $t9, $t1, .L7F003CEC
/* 03883C 7F003D0C AD41FFFC */   sw    $at, -4($t2)
/* 038840 7F003D10 8F210000 */  lw    $at, ($t9)
/* 038844 7F003D14 0000A025 */  move  $s4, $zero
/* 038848 7F003D18 00009825 */  move  $s3, $zero
/* 03884C 7F003D1C AD410000 */  sw    $at, ($t2)
/* 038850 7F003D20 904B0000 */  lbu   $t3, ($v0)
/* 038854 7F003D24 A3A00175 */  sb    $zero, 0x175($sp)
/* 038858 7F003D28 0FC26919 */  jal   getPlayerCount
/* 03885C 7F003D2C A3AB0174 */   sb    $t3, 0x174($sp)
/* 038860 7F003D30 28410002 */  slti  $at, $v0, 2
/* 038864 7F003D34 14200004 */  bnez  $at, .L7F003D48
/* 038868 7F003D38 3C058005 */   lui   $a1, %hi(aMp_)
/* 03886C 7F003D3C 24A5EEB0 */  addiu $a1, %lo(aMp_) # addiu $a1, $a1, -0x1150
/* 038870 7F003D40 0C0029FF */  jal   strcat
/* 038874 7F003D44 02202025 */   move  $a0, $s1
.L7F003D48:
/* 038878 7F003D48 8E050000 */  lw    $a1, ($s0)
/* 03887C 7F003D4C 02202025 */  move  $a0, $s1
/* 038880 7F003D50 0C0029FF */  jal   strcat
/* 038884 7F003D54 24A50001 */   addiu $a1, $a1, 1
/* 038888 7F003D58 02202025 */  move  $a0, $s1
/* 03888C 7F003D5C 24050001 */  li    $a1, 1
/* 038890 7F003D60 24060100 */  li    $a2, 256
/* 038894 7F003D64 0FC2F341 */  jal   _fileNameLoadToBank
/* 038898 7F003D68 24070004 */   li    $a3, 4
/* 03889C 7F003D6C 3C038007 */  lui   $v1, %hi(g_ptrStageSetupFile)
/* 0388A0 7F003D70 24635D28 */  addiu $v1, %lo(g_ptrStageSetupFile) # addiu $v1, $v1, 0x5d28
/* 0388A4 7F003D74 AC620000 */  sw    $v0, ($v1)
/* 0388A8 7F003D78 00408825 */  move  $s1, $v0
/* 0388AC 7F003D7C 0FC30578 */  jal   langGetLangBankIndexFromStagenum
/* 0388B0 7F003D80 02A02025 */   move  $a0, $s5
/* 0388B4 7F003D84 0FC30742 */  jal   langLoadToAddr
/* 0388B8 7F003D88 00402025 */   move  $a0, $v0
/* 0388BC 7F003D8C 8E2C0000 */  lw    $t4, ($s1)
/* 0388C0 7F003D90 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0)
/* 0388C4 7F003D94 24E75D00 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d00
/* 0388C8 7F003D98 022C6821 */  addu  $t5, $s1, $t4
/* 0388CC 7F003D9C ACED0000 */  sw    $t5, ($a3)
/* 0388D0 7F003DA0 8E2E0004 */  lw    $t6, 4($s1)
/* 0388D4 7F003DA4 022E7821 */  addu  $t7, $s1, $t6
/* 0388D8 7F003DA8 ACEF0004 */  sw    $t7, 4($a3)
/* 0388DC 7F003DAC 8E380008 */  lw    $t8, 8($s1)
/* 0388E0 7F003DB0 02384021 */  addu  $t0, $s1, $t8
/* 0388E4 7F003DB4 ACE80008 */  sw    $t0, 8($a3)
/* 0388E8 7F003DB8 8E29000C */  lw    $t1, 0xc($s1)
/* 0388EC 7F003DBC 0229C821 */  addu  $t9, $s1, $t1
/* 0388F0 7F003DC0 ACF9000C */  sw    $t9, 0xc($a3)
/* 0388F4 7F003DC4 8E2A0010 */  lw    $t2, 0x10($s1)
/* 0388F8 7F003DC8 022A5821 */  addu  $t3, $s1, $t2
/* 0388FC 7F003DCC ACEB0010 */  sw    $t3, 0x10($a3)
/* 038900 7F003DD0 8E2C0014 */  lw    $t4, 0x14($s1)
/* 038904 7F003DD4 022C6821 */  addu  $t5, $s1, $t4
/* 038908 7F003DD8 ACED0014 */  sw    $t5, 0x14($a3)
/* 03890C 7F003DDC 8E2E0018 */  lw    $t6, 0x18($s1)
/* 038910 7F003DE0 022E7821 */  addu  $t7, $s1, $t6
/* 038914 7F003DE4 ACEF0018 */  sw    $t7, 0x18($a3)
/* 038918 7F003DE8 8E38001C */  lw    $t8, 0x1c($s1)
/* 03891C 7F003DEC 02384021 */  addu  $t0, $s1, $t8
/* 038920 7F003DF0 ACE8001C */  sw    $t0, 0x1c($a3)
/* 038924 7F003DF4 8E220020 */  lw    $v0, 0x20($s1)
/* 038928 7F003DF8 10400003 */  beqz  $v0, .L7F003E08
/* 03892C 7F003DFC 02224821 */   addu  $t1, $s1, $v0
/* 038930 7F003E00 10000002 */  b     .L7F003E0C
/* 038934 7F003E04 ACE90020 */   sw    $t1, 0x20($a3)
.L7F003E08:
/* 038938 7F003E08 ACE00020 */  sw    $zero, 0x20($a3)
.L7F003E0C:
/* 03893C 7F003E0C 8E220024 */  lw    $v0, 0x24($s1)
/* 038940 7F003E10 10400003 */  beqz  $v0, .L7F003E20
/* 038944 7F003E14 0222C821 */   addu  $t9, $s1, $v0
/* 038948 7F003E18 10000002 */  b     .L7F003E24
/* 03894C 7F003E1C ACF90024 */   sw    $t9, 0x24($a3)
.L7F003E20:
/* 038950 7F003E20 ACE00024 */  sw    $zero, 0x24($a3)
.L7F003E24:
/* 038954 7F003E24 8CE50000 */  lw    $a1, ($a3)
/* 038958 7F003E28 50A00010 */  beql  $a1, $zero, .L7F003E6C
/* 03895C 7F003E2C 8CE50004 */   lw    $a1, 4($a3)
/* 038960 7F003E30 8CAA0000 */  lw    $t2, ($a1)
/* 038964 7F003E34 00001825 */  move  $v1, $zero
/* 038968 7F003E38 00A01025 */  move  $v0, $a1
/* 03896C 7F003E3C 0542000B */  bltzl $t2, .L7F003E6C
/* 038970 7F003E40 8CE50004 */   lw    $a1, 4($a3)
/* 038974 7F003E44 8C4B0004 */  lw    $t3, 4($v0)
.L7F003E48:
/* 038978 7F003E48 24630010 */  addiu $v1, $v1, 0x10
/* 03897C 7F003E4C 01716021 */  addu  $t4, $t3, $s1
/* 038980 7F003E50 AC4C0004 */  sw    $t4, 4($v0)
/* 038984 7F003E54 8CED0000 */  lw    $t5, ($a3)
/* 038988 7F003E58 01A31021 */  addu  $v0, $t5, $v1
/* 03898C 7F003E5C 8C4E0000 */  lw    $t6, ($v0)
/* 038990 7F003E60 05C3FFF9 */  bgezl $t6, .L7F003E48
/* 038994 7F003E64 8C4B0004 */   lw    $t3, 4($v0)
/* 038998 7F003E68 8CE50004 */  lw    $a1, 4($a3)
.L7F003E6C:
/* 03899C 7F003E6C 50A00015 */  beql  $a1, $zero, .L7F003EC4
/* 0389A0 7F003E70 8CE60014 */   lw    $a2, 0x14($a3)
/* 0389A4 7F003E74 8CAF0000 */  lw    $t7, ($a1)
/* 0389A8 7F003E78 00001825 */  move  $v1, $zero
/* 0389AC 7F003E7C 00A01025 */  move  $v0, $a1
/* 0389B0 7F003E80 51E00010 */  beql  $t7, $zero, .L7F003EC4
/* 0389B4 7F003E84 8CE60014 */   lw    $a2, 0x14($a3)
/* 0389B8 7F003E88 8CA40000 */  lw    $a0, ($a1)
/* 0389BC 7F003E8C 0091C021 */  addu  $t8, $a0, $s1
.L7F003E90:
/* 0389C0 7F003E90 AC580000 */  sw    $t8, ($v0)
/* 0389C4 7F003E94 8CE80004 */  lw    $t0, 4($a3)
/* 0389C8 7F003E98 01031021 */  addu  $v0, $t0, $v1
/* 0389CC 7F003E9C 8C490004 */  lw    $t1, 4($v0)
/* 0389D0 7F003EA0 2463000C */  addiu $v1, $v1, 0xc
/* 0389D4 7F003EA4 0131C821 */  addu  $t9, $t1, $s1
/* 0389D8 7F003EA8 AC590004 */  sw    $t9, 4($v0)
/* 0389DC 7F003EAC 8CEA0004 */  lw    $t2, 4($a3)
/* 0389E0 7F003EB0 01431021 */  addu  $v0, $t2, $v1
/* 0389E4 7F003EB4 8C440000 */  lw    $a0, ($v0)
/* 0389E8 7F003EB8 5480FFF5 */  bnezl $a0, .L7F003E90
/* 0389EC 7F003EBC 0091C021 */   addu  $t8, $a0, $s1
/* 0389F0 7F003EC0 8CE60014 */  lw    $a2, 0x14($a3)
.L7F003EC4:
/* 0389F4 7F003EC4 50C00010 */  beql  $a2, $zero, .L7F003F08
/* 0389F8 7F003EC8 8CE20010 */   lw    $v0, 0x10($a3)
/* 0389FC 7F003ECC 8CCB0000 */  lw    $t3, ($a2)
/* 038A00 7F003ED0 00002825 */  move  $a1, $zero
/* 038A04 7F003ED4 00C01025 */  move  $v0, $a2
/* 038A08 7F003ED8 5160000B */  beql  $t3, $zero, .L7F003F08
/* 038A0C 7F003EDC 8CE20010 */   lw    $v0, 0x10($a3)
/* 038A10 7F003EE0 8CC30000 */  lw    $v1, ($a2)
/* 038A14 7F003EE4 00716021 */  addu  $t4, $v1, $s1
.L7F003EE8:
/* 038A18 7F003EE8 AC4C0000 */  sw    $t4, ($v0)
/* 038A1C 7F003EEC 8CED0014 */  lw    $t5, 0x14($a3)
/* 038A20 7F003EF0 24A50008 */  addiu $a1, $a1, 8
/* 038A24 7F003EF4 01A51021 */  addu  $v0, $t5, $a1
/* 038A28 7F003EF8 8C430000 */  lw    $v1, ($v0)
/* 038A2C 7F003EFC 5460FFFA */  bnezl $v1, .L7F003EE8
/* 038A30 7F003F00 00716021 */   addu  $t4, $v1, $s1
/* 038A34 7F003F04 8CE20010 */  lw    $v0, 0x10($a3)
.L7F003F08:
/* 038A38 7F003F08 50400020 */  beql  $v0, $zero, .L7F003F8C
/* 038A3C 7F003F0C 8CEA0018 */   lw    $t2, 0x18($a3)
/* 038A40 7F003F10 8C4E0000 */  lw    $t6, ($v0)
/* 038A44 7F003F14 00002825 */  move  $a1, $zero
/* 038A48 7F003F18 00403025 */  move  $a2, $v0
/* 038A4C 7F003F1C 51C0001B */  beql  $t6, $zero, .L7F003F8C
/* 038A50 7F003F20 8CEA0018 */   lw    $t2, 0x18($a3)
/* 038A54 7F003F24 8C440000 */  lw    $a0, ($v0)
/* 038A58 7F003F28 00917821 */  addu  $t7, $a0, $s1
.L7F003F2C:
/* 038A5C 7F003F2C ACCF0000 */  sw    $t7, ($a2)
/* 038A60 7F003F30 8CF80010 */  lw    $t8, 0x10($a3)
/* 038A64 7F003F34 00001825 */  move  $v1, $zero
/* 038A68 7F003F38 03053021 */  addu  $a2, $t8, $a1
/* 038A6C 7F003F3C 8CC40000 */  lw    $a0, ($a2)
/* 038A70 7F003F40 8C880000 */  lw    $t0, ($a0)
/* 038A74 7F003F44 00801025 */  move  $v0, $a0
/* 038A78 7F003F48 05020009 */  bltzl $t0, .L7F003F70
/* 038A7C 7F003F4C A4C30006 */   sh    $v1, 6($a2)
/* 038A80 7F003F50 8C490004 */  lw    $t1, 4($v0)
.L7F003F54:
/* 038A84 7F003F54 24630001 */  addiu $v1, $v1, 1
/* 038A88 7F003F58 24420004 */  addiu $v0, $v0, 4
/* 038A8C 7F003F5C 0523FFFD */  bgezl $t1, .L7F003F54
/* 038A90 7F003F60 8C490004 */   lw    $t1, 4($v0)
/* 038A94 7F003F64 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0)
/* 038A98 7F003F68 24E75D00 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d00
/* 038A9C 7F003F6C A4C30006 */  sh    $v1, 6($a2)
.L7F003F70:
/* 038AA0 7F003F70 8CF90010 */  lw    $t9, 0x10($a3)
/* 038AA4 7F003F74 24A50008 */  addiu $a1, $a1, 8
/* 038AA8 7F003F78 03253021 */  addu  $a2, $t9, $a1
/* 038AAC 7F003F7C 8CC40000 */  lw    $a0, ($a2)
/* 038AB0 7F003F80 5480FFEA */  bnezl $a0, .L7F003F2C
/* 038AB4 7F003F84 00917821 */   addu  $t7, $a0, $s1
/* 038AB8 7F003F88 8CEA0018 */  lw    $t2, 0x18($a3)
.L7F003F8C:
/* 038ABC 7F003F8C 1140001D */  beqz  $t2, .L7F004004
/* 038AC0 7F003F90 00000000 */   nop
/* 038AC4 7F003F94 0FC2D20C */  jal   get_room_data_float2
/* 038AC8 7F003F98 00000000 */   nop
/* 038ACC 7F003F9C 3C108007 */  lui   $s0, %hi(g_CurrentSetup+0x18)
/* 038AD0 7F003FA0 8E105D18 */  lw    $s0, %lo(g_CurrentSetup+0x18)($s0)
/* 038AD4 7F003FA4 46000086 */  mov.s $f2, $f0
/* 038AD8 7F003FA8 8E020024 */  lw    $v0, 0x24($s0)
/* 038ADC 7F003FAC 10400015 */  beqz  $v0, .L7F004004
/* 038AE0 7F003FB0 00000000 */   nop
/* 038AE4 7F003FB4 C6060000 */  lwc1  $f6, ($s0)
.L7F003FB8:
/* 038AE8 7F003FB8 C60A0004 */  lwc1  $f10, 4($s0)
/* 038AEC 7F003FBC C6120008 */  lwc1  $f18, 8($s0)
/* 038AF0 7F003FC0 46023202 */  mul.s $f8, $f6, $f2
/* 038AF4 7F003FC4 02222821 */  addu  $a1, $s1, $v0
/* 038AF8 7F003FC8 AE050024 */  sw    $a1, 0x24($s0)
/* 038AFC 7F003FCC 46025402 */  mul.s $f16, $f10, $f2
/* 038B00 7F003FD0 02002025 */  move  $a0, $s0
/* 038B04 7F003FD4 26060028 */  addiu $a2, $s0, 0x28
/* 038B08 7F003FD8 46029102 */  mul.s $f4, $f18, $f2
/* 038B0C 7F003FDC E6080000 */  swc1  $f8, ($s0)
/* 038B10 7F003FE0 E6100004 */  swc1  $f16, 4($s0)
/* 038B14 7F003FE4 E6040008 */  swc1  $f4, 8($s0)
/* 038B18 7F003FE8 0FC019F0 */  jal   init_pathtable_something
/* 038B1C 7F003FEC E7A2015C */   swc1  $f2, 0x15c($sp)
/* 038B20 7F003FF0 8E020050 */  lw    $v0, 0x50($s0)
/* 038B24 7F003FF4 2610002C */  addiu $s0, $s0, 0x2c
/* 038B28 7F003FF8 C7A2015C */  lwc1  $f2, 0x15c($sp)
/* 038B2C 7F003FFC 5440FFEE */  bnezl $v0, .L7F003FB8
/* 038B30 7F004000 C6060000 */   lwc1  $f6, ($s0)
.L7F004004:
/* 038B34 7F004004 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0)
/* 038B38 7F004008 24E75D00 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d00
/* 038B3C 7F00400C 8CEC001C */  lw    $t4, 0x1c($a3)
/* 038B40 7F004010 51800033 */  beql  $t4, $zero, .L7F0040E0
/* 038B44 7F004014 8CE50020 */   lw    $a1, 0x20($a3)
/* 038B48 7F004018 0FC2D20C */  jal   get_room_data_float2
/* 038B4C 7F00401C 00000000 */   nop
/* 038B50 7F004020 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0)
/* 038B54 7F004024 24E75D00 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d00
/* 038B58 7F004028 8CF0001C */  lw    $s0, 0x1c($a3)
/* 038B5C 7F00402C 46000086 */  mov.s $f2, $f0
/* 038B60 7F004030 8E020024 */  lw    $v0, 0x24($s0)
/* 038B64 7F004034 5040002A */  beql  $v0, $zero, .L7F0040E0
/* 038B68 7F004038 8CE50020 */   lw    $a1, 0x20($a3)
/* 038B6C 7F00403C C6060000 */  lwc1  $f6, ($s0)
.L7F004040:
/* 038B70 7F004040 C60A0004 */  lwc1  $f10, 4($s0)
/* 038B74 7F004044 C6120008 */  lwc1  $f18, 8($s0)
/* 038B78 7F004048 46023202 */  mul.s $f8, $f6, $f2
/* 038B7C 7F00404C C606002C */  lwc1  $f6, 0x2c($s0)
/* 038B80 7F004050 02222821 */  addu  $a1, $s1, $v0
/* 038B84 7F004054 46025402 */  mul.s $f16, $f10, $f2
/* 038B88 7F004058 C60A0030 */  lwc1  $f10, 0x30($s0)
/* 038B8C 7F00405C AE050024 */  sw    $a1, 0x24($s0)
/* 038B90 7F004060 46029102 */  mul.s $f4, $f18, $f2
/* 038B94 7F004064 E6080000 */  swc1  $f8, ($s0)
/* 038B98 7F004068 C6120034 */  lwc1  $f18, 0x34($s0)
/* 038B9C 7F00406C 46023202 */  mul.s $f8, $f6, $f2
/* 038BA0 7F004070 E6100004 */  swc1  $f16, 4($s0)
/* 038BA4 7F004074 C6060038 */  lwc1  $f6, 0x38($s0)
/* 038BA8 7F004078 46025402 */  mul.s $f16, $f10, $f2
/* 038BAC 7F00407C E6040008 */  swc1  $f4, 8($s0)
/* 038BB0 7F004080 C60A003C */  lwc1  $f10, 0x3c($s0)
/* 038BB4 7F004084 46029102 */  mul.s $f4, $f18, $f2
/* 038BB8 7F004088 E608002C */  swc1  $f8, 0x2c($s0)
/* 038BBC 7F00408C C6120040 */  lwc1  $f18, 0x40($s0)
/* 038BC0 7F004090 46023202 */  mul.s $f8, $f6, $f2
/* 038BC4 7F004094 E6100030 */  swc1  $f16, 0x30($s0)
/* 038BC8 7F004098 02002025 */  move  $a0, $s0
/* 038BCC 7F00409C 46025402 */  mul.s $f16, $f10, $f2
/* 038BD0 7F0040A0 E6040034 */  swc1  $f4, 0x34($s0)
/* 038BD4 7F0040A4 26060028 */  addiu $a2, $s0, 0x28
/* 038BD8 7F0040A8 46029102 */  mul.s $f4, $f18, $f2
/* 038BDC 7F0040AC E6080038 */  swc1  $f8, 0x38($s0)
/* 038BE0 7F0040B0 E610003C */  swc1  $f16, 0x3c($s0)
/* 038BE4 7F0040B4 E6040040 */  swc1  $f4, 0x40($s0)
/* 038BE8 7F0040B8 0FC019F0 */  jal   init_pathtable_something
/* 038BEC 7F0040BC E7A20150 */   swc1  $f2, 0x150($sp)
/* 038BF0 7F0040C0 8E020068 */  lw    $v0, 0x68($s0)
/* 038BF4 7F0040C4 26100044 */  addiu $s0, $s0, 0x44
/* 038BF8 7F0040C8 C7A20150 */  lwc1  $f2, 0x150($sp)
/* 038BFC 7F0040CC 5440FFDC */  bnezl $v0, .L7F004040
/* 038C00 7F0040D0 C6060000 */   lwc1  $f6, ($s0)
/* 038C04 7F0040D4 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0)
/* 038C08 7F0040D8 24E75D00 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d00
/* 038C0C 7F0040DC 8CE50020 */  lw    $a1, 0x20($a3)
.L7F0040E0:
/* 038C10 7F0040E0 50A00010 */  beql  $a1, $zero, .L7F004124
/* 038C14 7F0040E4 8CE50024 */   lw    $a1, 0x24($a3)
/* 038C18 7F0040E8 8CAE0000 */  lw    $t6, ($a1)
/* 038C1C 7F0040EC 00001025 */  move  $v0, $zero
/* 038C20 7F0040F0 00A01825 */  move  $v1, $a1
/* 038C24 7F0040F4 51C0000B */  beql  $t6, $zero, .L7F004124
/* 038C28 7F0040F8 8CE50024 */   lw    $a1, 0x24($a3)
/* 038C2C 7F0040FC 8CA40000 */  lw    $a0, ($a1)
/* 038C30 7F004100 00917821 */  addu  $t7, $a0, $s1
.L7F004104:
/* 038C34 7F004104 AC6F0000 */  sw    $t7, ($v1)
/* 038C38 7F004108 8CF80020 */  lw    $t8, 0x20($a3)
/* 038C3C 7F00410C 24420004 */  addiu $v0, $v0, 4
/* 038C40 7F004110 03021821 */  addu  $v1, $t8, $v0
/* 038C44 7F004114 8C640000 */  lw    $a0, ($v1)
/* 038C48 7F004118 5480FFFA */  bnezl $a0, .L7F004104
/* 038C4C 7F00411C 00917821 */   addu  $t7, $a0, $s1
/* 038C50 7F004120 8CE50024 */  lw    $a1, 0x24($a3)
.L7F004124:
/* 038C54 7F004124 50A0000F */  beql  $a1, $zero, .L7F004164
/* 038C58 7F004128 8FAA0278 */   lw    $t2, 0x278($sp)
/* 038C5C 7F00412C 8CA80000 */  lw    $t0, ($a1)
/* 038C60 7F004130 00001025 */  move  $v0, $zero
/* 038C64 7F004134 00A01825 */  move  $v1, $a1
/* 038C68 7F004138 11000009 */  beqz  $t0, .L7F004160
/* 038C6C 7F00413C 01002025 */   move  $a0, $t0
/* 038C70 7F004140 00914821 */  addu  $t1, $a0, $s1
.L7F004144:
/* 038C74 7F004144 AC690000 */  sw    $t1, ($v1)
/* 038C78 7F004148 8CF90024 */  lw    $t9, 0x24($a3)
/* 038C7C 7F00414C 24420004 */  addiu $v0, $v0, 4
/* 038C80 7F004150 03221821 */  addu  $v1, $t9, $v0
/* 038C84 7F004154 8C640000 */  lw    $a0, ($v1)
/* 038C88 7F004158 5480FFFA */  bnezl $a0, .L7F004144
/* 038C8C 7F00415C 00914821 */   addu  $t1, $a0, $s1
.L7F004160:
/* 038C90 7F004160 8FAA0278 */  lw    $t2, 0x278($sp)
.L7F004164:
/* 038C94 7F004164 11400012 */  beqz  $t2, .L7F0041B0
/* 038C98 7F004168 00000000 */   nop
/* 038C9C 7F00416C 0FC006D4 */  jal   load_proptype
/* 038CA0 7F004170 24040009 */   li    $a0, 9
/* 038CA4 7F004174 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 038CA8 7F004178 00402025 */   move  $a0, $v0
/* 038CAC 7F00417C 0FC006D4 */  jal   load_proptype
/* 038CB0 7F004180 24040009 */   li    $a0, 9
/* 038CB4 7F004184 0040A025 */  move  $s4, $v0
/* 038CB8 7F004188 0FC006D4 */  jal   load_proptype
/* 038CBC 7F00418C 24040008 */   li    $a0, 8
/* 038CC0 7F004190 00409825 */  move  $s3, $v0
/* 038CC4 7F004194 0FC006D4 */  jal   load_proptype
/* 038CC8 7F004198 24040004 */   li    $a0, 4
/* 038CCC 7F00419C 02629821 */  addu  $s3, $s3, $v0
/* 038CD0 7F0041A0 0FC006D4 */  jal   load_proptype
/* 038CD4 7F0041A4 24040011 */   li    $a0, 17
/* 038CD8 7F0041A8 10000003 */  b     .L7F0041B8
/* 038CDC 7F0041AC 02629821 */   addu  $s3, $s3, $v0
.L7F0041B0:
/* 038CE0 7F0041B0 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 038CE4 7F0041B4 00002025 */   move  $a0, $zero
.L7F0041B8:
/* 038CE8 7F0041B8 13C0003A */  beqz  $fp, .L7F0042A4
/* 038CEC 7F0041BC 00000000 */   nop
/* 038CF0 7F0041C0 0FC006D4 */  jal   load_proptype
/* 038CF4 7F0041C4 24040001 */   li    $a0, 1
/* 038CF8 7F0041C8 02629821 */  addu  $s3, $s3, $v0
/* 038CFC 7F0041CC 0FC006D4 */  jal   load_proptype
/* 038D00 7F0041D0 24040006 */   li    $a0, 6
/* 038D04 7F0041D4 02629821 */  addu  $s3, $s3, $v0
/* 038D08 7F0041D8 0FC006D4 */  jal   load_proptype
/* 038D0C 7F0041DC 2404000D */   li    $a0, 13
/* 038D10 7F0041E0 02629821 */  addu  $s3, $s3, $v0
/* 038D14 7F0041E4 0FC006D4 */  jal   load_proptype
/* 038D18 7F0041E8 2404000C */   li    $a0, 12
/* 038D1C 7F0041EC 02629821 */  addu  $s3, $s3, $v0
/* 038D20 7F0041F0 0FC006D4 */  jal   load_proptype
/* 038D24 7F0041F4 2404000A */   li    $a0, 10
/* 038D28 7F0041F8 02629821 */  addu  $s3, $s3, $v0
/* 038D2C 7F0041FC 0FC006D4 */  jal   load_proptype
/* 038D30 7F004200 2404000B */   li    $a0, 11
/* 038D34 7F004204 02629821 */  addu  $s3, $s3, $v0
/* 038D38 7F004208 0FC006D4 */  jal   load_proptype
/* 038D3C 7F00420C 24040015 */   li    $a0, 21
/* 038D40 7F004210 02629821 */  addu  $s3, $s3, $v0
/* 038D44 7F004214 0FC006D4 */  jal   load_proptype
/* 038D48 7F004218 24040003 */   li    $a0, 3
/* 038D4C 7F00421C 02629821 */  addu  $s3, $s3, $v0
/* 038D50 7F004220 0FC006D4 */  jal   load_proptype
/* 038D54 7F004224 2404002A */   li    $a0, 42
/* 038D58 7F004228 02629821 */  addu  $s3, $s3, $v0
/* 038D5C 7F00422C 0FC006D4 */  jal   load_proptype
/* 038D60 7F004230 2404002F */   li    $a0, 47
/* 038D64 7F004234 02629821 */  addu  $s3, $s3, $v0
/* 038D68 7F004238 0FC006D4 */  jal   load_proptype
/* 038D6C 7F00423C 2404002B */   li    $a0, 43
/* 038D70 7F004240 02629821 */  addu  $s3, $s3, $v0
/* 038D74 7F004244 0FC006D4 */  jal   load_proptype
/* 038D78 7F004248 24040029 */   li    $a0, 41
/* 038D7C 7F00424C 02629821 */  addu  $s3, $s3, $v0
/* 038D80 7F004250 0FC006D4 */  jal   load_proptype
/* 038D84 7F004254 24040024 */   li    $a0, 36
/* 038D88 7F004258 02629821 */  addu  $s3, $s3, $v0
/* 038D8C 7F00425C 0FC006D4 */  jal   load_proptype
/* 038D90 7F004260 24040005 */   li    $a0, 5
/* 038D94 7F004264 02629821 */  addu  $s3, $s3, $v0
/* 038D98 7F004268 0FC006D4 */  jal   load_proptype
/* 038D9C 7F00426C 24040007 */   li    $a0, 7
/* 038DA0 7F004270 02629821 */  addu  $s3, $s3, $v0
/* 038DA4 7F004274 0FC006D4 */  jal   load_proptype
/* 038DA8 7F004278 24040014 */   li    $a0, 20
/* 038DAC 7F00427C 02629821 */  addu  $s3, $s3, $v0
/* 038DB0 7F004280 0FC006D4 */  jal   load_proptype
/* 038DB4 7F004284 24040027 */   li    $a0, 39
/* 038DB8 7F004288 02629821 */  addu  $s3, $s3, $v0
/* 038DBC 7F00428C 0FC006D4 */  jal   load_proptype
/* 038DC0 7F004290 2404002D */   li    $a0, 45
/* 038DC4 7F004294 02629821 */  addu  $s3, $s3, $v0
/* 038DC8 7F004298 0FC006D4 */  jal   load_proptype
/* 038DCC 7F00429C 24040028 */   li    $a0, 40
/* 038DD0 7F0042A0 0282A021 */  addu  $s4, $s4, $v0
.L7F0042A4:
/* 038DD4 7F0042A4 0FC01550 */  jal   sub_GAME_7F005540
/* 038DD8 7F0042A8 02602025 */   move  $a0, $s3
/* 038DDC 7F0042AC 0FC01589 */  jal   sub_GAME_7F005624
/* 038DE0 7F0042B0 02802025 */   move  $a0, $s4
/* 038DE4 7F0042B4 0FC26919 */  jal   getPlayerCount
/* 038DE8 7F0042B8 00008025 */   move  $s0, $zero
/* 038DEC 7F0042BC 1840000C */  blez  $v0, .L7F0042F0
/* 038DF0 7F0042C0 00000000 */   nop
.L7F0042C4:
/* 038DF4 7F0042C4 0FC26C43 */  jal   set_cur_player
/* 038DF8 7F0042C8 02002025 */   move  $a0, $s0
/* 038DFC 7F0042CC 0FC006D4 */  jal   load_proptype
/* 038E00 7F0042D0 2404000E */   li    $a0, 14
/* 038E04 7F0042D4 0FC01897 */  jal   alloc_additional_item_slots
/* 038E08 7F0042D8 00402025 */   move  $a0, $v0
/* 038E0C 7F0042DC 0FC26919 */  jal   getPlayerCount
/* 038E10 7F0042E0 26100001 */   addiu $s0, $s0, 1
/* 038E14 7F0042E4 0202082A */  slt   $at, $s0, $v0
/* 038E18 7F0042E8 1420FFF6 */  bnez  $at, .L7F0042C4
/* 038E1C 7F0042EC 00000000 */   nop
.L7F0042F0:
/* 038E20 7F0042F0 3C0B8007 */  lui   $t3, %hi(g_CurrentSetup+0x0c)
/* 038E24 7F0042F4 8D6B5D0C */  lw    $t3, %lo(g_CurrentSetup+0x0c)($t3)
/* 038E28 7F0042F8 116002DF */  beqz  $t3, .L7F004E78
/* 038E2C 7F0042FC 00000000 */   nop
/* 038E30 7F004300 0FC2FF04 */  jal   lvlGetSelectedDifficulty
/* 038E34 7F004304 00000000 */   nop
/* 038E38 7F004308 244C0004 */  addiu $t4, $v0, 4
/* 038E3C 7F00430C 240D0001 */  li    $t5, 1
/* 038E40 7F004310 0FC26919 */  jal   getPlayerCount
/* 038E44 7F004314 018DA004 */   sllv  $s4, $t5, $t4
/* 038E48 7F004318 28410002 */  slti  $at, $v0, 2
/* 038E4C 7F00431C 14200007 */  bnez  $at, .L7F00433C
/* 038E50 7F004320 00000000 */   nop
/* 038E54 7F004324 0FC26919 */  jal   getPlayerCount
/* 038E58 7F004328 00000000 */   nop
/* 038E5C 7F00432C 244E0014 */  addiu $t6, $v0, 0x14
/* 038E60 7F004330 240F0001 */  li    $t7, 1
/* 038E64 7F004334 01CFC004 */  sllv  $t8, $t7, $t6
/* 038E68 7F004338 0298A025 */  or    $s4, $s4, $t8
.L7F00433C:
/* 038E6C 7F00433C 3C128007 */  lui   $s2, %hi(g_CurrentSetup+0x0c)
/* 038E70 7F004340 8E525D0C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 038E74 7F004344 24010030 */  li    $at, 48
/* 038E78 7F004348 00009825 */  move  $s3, $zero
/* 038E7C 7F00434C 92430003 */  lbu   $v1, 3($s2)
/* 038E80 7F004350 24170034 */  li    $s7, 52
/* 038E84 7F004354 506101F6 */  beql  $v1, $at, .L7F004B30
/* 038E88 7F004358 92430003 */   lbu   $v1, 3($s2)
/* 038E8C 7F00435C 4480A000 */  mtc1  $zero, $f20
/* 038E90 7F004360 3416FFFF */  li    $s6, 65535
/* 038E94 7F004364 2468FFFF */  addiu $t0, $v1, -1
.L7F004368:
/* 038E98 7F004368 2D01002F */  sltiu $at, $t0, 0x2f
/* 038E9C 7F00436C 102001E3 */  beqz  $at, other_obj_expand
/* 038EA0 7F004370 00084080 */   sll   $t0, $t0, 2
/* 038EA4 7F004374 3C018005 */  lui   $at, %hi(jpt_object_type_expansion)
/* 038EA8 7F004378 00280821 */  addu  $at, $at, $t0
/* 038EAC 7F00437C 8C28EF68 */  lw    $t0, %lo(jpt_object_type_expansion)($at)
/* 038EB0 7F004380 01000008 */  jr    $t0
/* 038EB4 7F004384 00000000 */   nop
actor_attr_expand:
/* 038EB8 7F004388 9250000B */  lbu   $s0, 0xb($s2)
/* 038EBC 7F00438C 0FC08BF2 */  jal   chrFindByLiteralId
/* 038EC0 7F004390 8E440004 */   lw    $a0, 4($s2)
/* 038EC4 7F004394 104001D9 */  beqz  $v0, other_obj_expand
/* 038EC8 7F004398 00000000 */   nop
/* 038ECC 7F00439C 8C490018 */  lw    $t1, 0x18($v0)
/* 038ED0 7F0043A0 112001D6 */  beqz  $t1, other_obj_expand
/* 038ED4 7F0043A4 00000000 */   nop
/* 038ED8 7F0043A8 8C59001C */  lw    $t9, 0x1c($v0)
/* 038EDC 7F0043AC 132001D3 */  beqz  $t9, other_obj_expand
/* 038EE0 7F0043B0 00000000 */   nop
/* 038EE4 7F0043B4 100001D1 */  b     other_obj_expand
/* 038EE8 7F0043B8 A0500010 */   sb    $s0, 0x10($v0)
actor_expand:
/* 038EEC 7F0043BC 8FAA0278 */  lw    $t2, 0x278($sp)
/* 038EF0 7F0043C0 02A02025 */  move  $a0, $s5
/* 038EF4 7F0043C4 02402825 */  move  $a1, $s2
/* 038EF8 7F0043C8 114001CC */  beqz  $t2, other_obj_expand
/* 038EFC 7F0043CC 00000000 */   nop
/* 038F00 7F0043D0 0FC08DC3 */  jal   expand_09_characters
/* 038F04 7F0043D4 02603025 */   move  $a2, $s3
/* 038F08 7F0043D8 100001C8 */  b     other_obj_expand
/* 038F0C 7F0043DC 00000000 */   nop
door_expand:
/* 038F10 7F0043E0 13C001C6 */  beqz  $fp, other_obj_expand
/* 038F14 7F0043E4 00000000 */   nop
/* 038F18 7F0043E8 8E4B000C */  lw    $t3, 0xc($s2)
/* 038F1C 7F0043EC 02A02025 */  move  $a0, $s5
/* 038F20 7F0043F0 02402825 */  move  $a1, $s2
/* 038F24 7F0043F4 01746824 */  and   $t5, $t3, $s4
/* 038F28 7F0043F8 15A001C0 */  bnez  $t5, other_obj_expand
/* 038F2C 7F0043FC 00000000 */   nop
/* 038F30 7F004400 0FC00D20 */  jal   setupDoor
/* 038F34 7F004404 02603025 */   move  $a2, $s3
/* 038F38 7F004408 100001BC */  b     other_obj_expand
/* 038F3C 7F00440C 00000000 */   nop
door_scale_expand:
/* 038F40 7F004410 8E4C0004 */  lw    $t4, 4($s2)
/* 038F44 7F004414 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 038F48 7F004418 44815000 */  mtc1  $at, $f10
/* 038F4C 7F00441C 448C3000 */  mtc1  $t4, $f6
/* 038F50 7F004420 3C018003 */  lui   $at, %hi(g_DoorScale)
/* 038F54 7F004424 46803220 */  cvt.s.w $f8, $f6
/* 038F58 7F004428 460A4403 */  div.s $f16, $f8, $f10
/* 038F5C 7F00442C 100001B3 */  b     other_obj_expand
/* 038F60 7F004430 E430A3C0 */   swc1  $f16, %lo(g_DoorScale)($at)
item_expand:
/* 038F64 7F004434 8FAF0278 */  lw    $t7, 0x278($sp)
/* 038F68 7F004438 11E001B0 */  beqz  $t7, other_obj_expand
/* 038F6C 7F00443C 00000000 */   nop
/* 038F70 7F004440 8E4E000C */  lw    $t6, 0xc($s2)
/* 038F74 7F004444 02A02025 */  move  $a0, $s5
/* 038F78 7F004448 02402825 */  move  $a1, $s2
/* 038F7C 7F00444C 01D4C024 */  and   $t8, $t6, $s4
/* 038F80 7F004450 170001AA */  bnez  $t8, other_obj_expand
/* 038F84 7F004454 00000000 */   nop
/* 038F88 7F004458 0FC009CE */  jal   weaponAssignToHome
/* 038F8C 7F00445C 02603025 */   move  $a2, $s3
/* 038F90 7F004460 100001A6 */  b     other_obj_expand
/* 038F94 7F004464 00000000 */   nop
key_expand:
/* 038F98 7F004468 8FA80278 */  lw    $t0, 0x278($sp)
/* 038F9C 7F00446C 110001A3 */  beqz  $t0, other_obj_expand
/* 038FA0 7F004470 00000000 */   nop
/* 038FA4 7F004474 8E49000C */  lw    $t1, 0xc($s2)
/* 038FA8 7F004478 02A02025 */  move  $a0, $s5
/* 038FAC 7F00447C 02402825 */  move  $a1, $s2
/* 038FB0 7F004480 0134C824 */  and   $t9, $t1, $s4
/* 038FB4 7F004484 1720019D */  bnez  $t9, other_obj_expand
/* 038FB8 7F004488 00000000 */   nop
/* 038FBC 7F00448C 0FC00A87 */  jal   setupKey
/* 038FC0 7F004490 02603025 */   move  $a2, $s3
/* 038FC4 7F004494 10000199 */  b     other_obj_expand
/* 038FC8 7F004498 00000000 */   nop
hat_expand:
/* 038FCC 7F00449C 8FAA0278 */  lw    $t2, 0x278($sp)
/* 038FD0 7F0044A0 11400196 */  beqz  $t2, other_obj_expand
/* 038FD4 7F0044A4 00000000 */   nop
/* 038FD8 7F0044A8 8E4B000C */  lw    $t3, 0xc($s2)
/* 038FDC 7F0044AC 02A02025 */  move  $a0, $s5
/* 038FE0 7F0044B0 02402825 */  move  $a1, $s2
/* 038FE4 7F0044B4 01746824 */  and   $t5, $t3, $s4
/* 038FE8 7F0044B8 15A00190 */  bnez  $t5, other_obj_expand
/* 038FEC 7F0044BC 00000000 */   nop
/* 038FF0 7F0044C0 0FC00A6A */  jal   setupHat
/* 038FF4 7F0044C4 02603025 */   move  $a2, $s3
/* 038FF8 7F0044C8 1000018C */  b     other_obj_expand
/* 038FFC 7F0044CC 00000000 */   nop
surv_camera_expand:
/* 039000 7F0044D0 13C0018A */  beqz  $fp, other_obj_expand
/* 039004 7F0044D4 00000000 */   nop
/* 039008 7F0044D8 8E4C000C */  lw    $t4, 0xc($s2)
/* 03900C 7F0044DC 02A02025 */  move  $a0, $s5
/* 039010 7F0044E0 02402825 */  move  $a1, $s2
/* 039014 7F0044E4 01947824 */  and   $t7, $t4, $s4
/* 039018 7F0044E8 15E00184 */  bnez  $t7, other_obj_expand
/* 03901C 7F0044EC 00000000 */   nop
/* 039020 7F0044F0 0FC00A8F */  jal   setupCctv
/* 039024 7F0044F4 02603025 */   move  $a2, $s3
/* 039028 7F0044F8 10000180 */  b     other_obj_expand
/* 03902C 7F0044FC 00000000 */   nop
autogun_expand:
/* 039030 7F004500 13C0017E */  beqz  $fp, other_obj_expand
/* 039034 7F004504 00000000 */   nop
/* 039038 7F004508 8E4E000C */  lw    $t6, 0xc($s2)
/* 03903C 7F00450C 02A02025 */  move  $a0, $s5
/* 039040 7F004510 02402825 */  move  $a1, $s2
/* 039044 7F004514 01D4C024 */  and   $t8, $t6, $s4
/* 039048 7F004518 17000178 */  bnez  $t8, other_obj_expand
/* 03904C 7F00451C 00000000 */   nop
/* 039050 7F004520 0FC00B16 */  jal   setupAutogun
/* 039054 7F004524 02603025 */   move  $a2, $s3
/* 039058 7F004528 10000174 */  b     other_obj_expand
/* 03905C 7F00452C 00000000 */   nop
rack_expand:
/* 039060 7F004530 13C00172 */  beqz  $fp, other_obj_expand
/* 039064 7F004534 00000000 */   nop
/* 039068 7F004538 8E48000C */  lw    $t0, 0xc($s2)
/* 03906C 7F00453C 02A02025 */  move  $a0, $s5
/* 039070 7F004540 02402825 */  move  $a1, $s2
/* 039074 7F004544 01144824 */  and   $t1, $t0, $s4
/* 039078 7F004548 1520016C */  bnez  $t1, other_obj_expand
/* 03907C 7F00454C 00000000 */   nop
/* 039080 7F004550 0FC00B87 */  jal   setupHangingMonitors
/* 039084 7F004554 02603025 */   move  $a2, $s3
/* 039088 7F004558 10000168 */  b     other_obj_expand
/* 03908C 7F00455C 00000000 */   nop
single_screen_display_expand:
/* 039090 7F004560 13C00166 */  beqz  $fp, other_obj_expand
/* 039094 7F004564 00000000 */   nop
/* 039098 7F004568 8E59000C */  lw    $t9, 0xc($s2)
/* 03909C 7F00456C 02A02025 */  move  $a0, $s5
/* 0390A0 7F004570 02402825 */  move  $a1, $s2
/* 0390A4 7F004574 03345024 */  and   $t2, $t9, $s4
/* 0390A8 7F004578 15400160 */  bnez  $t2, other_obj_expand
/* 0390AC 7F00457C 00000000 */   nop
/* 0390B0 7F004580 0FC00B8F */  jal   setupSingleMonitor
/* 0390B4 7F004584 02603025 */   move  $a2, $s3
/* 0390B8 7F004588 1000015C */  b     other_obj_expand
/* 0390BC 7F00458C 00000000 */   nop
multi_screen_display_expand:
/* 0390C0 7F004590 13C0015A */  beqz  $fp, other_obj_expand
/* 0390C4 7F004594 00000000 */   nop
/* 0390C8 7F004598 8E4B000C */  lw    $t3, 0xc($s2)
/* 0390CC 7F00459C 02A02025 */  move  $a0, $s5
/* 0390D0 7F0045A0 02402825 */  move  $a1, $s2
/* 0390D4 7F0045A4 01746824 */  and   $t5, $t3, $s4
/* 0390D8 7F0045A8 15A00154 */  bnez  $t5, other_obj_expand
/* 0390DC 7F0045AC 00000000 */   nop
/* 0390E0 7F0045B0 0FC00C34 */  jal   setupMultiMonitor
/* 0390E4 7F0045B4 02603025 */   move  $a2, $s3
/* 0390E8 7F0045B8 10000150 */  b     other_obj_expand
/* 0390EC 7F0045BC 00000000 */   nop
body_armor_expand:
/* 0390F0 7F0045C0 13C0014E */  beqz  $fp, other_obj_expand
/* 0390F4 7F0045C4 00000000 */   nop
/* 0390F8 7F0045C8 8E4C000C */  lw    $t4, 0xc($s2)
/* 0390FC 7F0045CC 01947824 */  and   $t7, $t4, $s4
/* 039100 7F0045D0 15E0014A */  bnez  $t7, other_obj_expand
/* 039104 7F0045D4 00000000 */   nop
/* 039108 7F0045D8 8E4E0080 */  lw    $t6, 0x80($s2)
/* 03910C 7F0045DC 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 039110 7F0045E0 44813000 */  mtc1  $at, $f6
/* 039114 7F0045E4 448E9000 */  mtc1  $t6, $f18
/* 039118 7F0045E8 02A02025 */  move  $a0, $s5
/* 03911C 7F0045EC 02402825 */  move  $a1, $s2
/* 039120 7F0045F0 46809120 */  cvt.s.w $f4, $f18
/* 039124 7F0045F4 02603025 */  move  $a2, $s3
/* 039128 7F0045F8 46062203 */  div.s $f8, $f4, $f6
/* 03912C 7F0045FC E6480080 */  swc1  $f8, 0x80($s2)
/* 039130 7F004600 C64A0080 */  lwc1  $f10, 0x80($s2)
/* 039134 7F004604 0FC00767 */  jal   domakedefaultobj
/* 039138 7F004608 E64A0084 */   swc1  $f10, 0x84($s2)
/* 03913C 7F00460C 1000013B */  b     other_obj_expand
/* 039140 7F004610 00000000 */   nop
type2F_tinted_glass:
/* 039144 7F004614 13C00139 */  beqz  $fp, other_obj_expand
/* 039148 7F004618 00000000 */   nop
/* 03914C 7F00461C 8E58000C */  lw    $t8, 0xc($s2)
/* 039150 7F004620 03144024 */  and   $t0, $t8, $s4
/* 039154 7F004624 15000135 */  bnez  $t0, other_obj_expand
/* 039158 7F004628 00000000 */   nop
/* 03915C 7F00462C 8E490008 */  lw    $t1, 8($s2)
/* 039160 7F004630 0009C8C0 */  sll   $t9, $t1, 3
/* 039164 7F004634 07230039 */  bgezl $t9, .L7F00471C
/* 039168 7F004638 02A02025 */   move  $a0, $s5
/* 03916C 7F00463C 86420006 */  lh    $v0, 6($s2)
/* 039170 7F004640 3C0B8007 */  lui   $t3, %hi(g_CurrentSetup+0x1C)
/* 039174 7F004644 27A50118 */  addiu $a1, $sp, 0x118
/* 039178 7F004648 28412710 */  slti  $at, $v0, 0x2710
/* 03917C 7F00464C 14200032 */  bnez  $at, .L7F004718
/* 039180 7F004650 00025100 */   sll   $t2, $v0, 4
/* 039184 7F004654 8D6B5D1C */  lw    $t3, %lo(g_CurrentSetup+0x1C)($t3)
/* 039188 7F004658 01425021 */  addu  $t2, $t2, $v0
/* 03918C 7F00465C 000A5080 */  sll   $t2, $t2, 2
/* 039190 7F004660 3C01FFF5 */  lui   $at, (0xFFF59FC0 >> 16) # lui $at, 0xfff5
/* 039194 7F004664 34219FC0 */  ori   $at, (0xFFF59FC0 & 0xFFFF) # ori $at, $at, 0x9fc0
/* 039198 7F004668 014B8021 */  addu  $s0, $t2, $t3
/* 03919C 7F00466C 02018021 */  addu  $s0, $s0, $at
/* 0391A0 7F004670 0FC006F5 */  jal   sub_GAME_7F001BD4
/* 0391A4 7F004674 02002025 */   move  $a0, $s0
/* 0391A8 7F004678 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 0391AC 7F00467C 44810000 */  mtc1  $at, $f0
/* 0391B0 7F004680 C610000C */  lwc1  $f16, 0xc($s0)
/* 0391B4 7F004684 C7A40118 */  lwc1  $f4, 0x118($sp)
/* 0391B8 7F004688 27A40118 */  addiu $a0, $sp, 0x118
/* 0391BC 7F00468C 46008482 */  mul.s $f18, $f16, $f0
/* 0391C0 7F004690 C7B0011C */  lwc1  $f16, 0x11c($sp)
/* 0391C4 7F004694 27A5010C */  addiu $a1, $sp, 0x10c
/* 0391C8 7F004698 46049180 */  add.s $f6, $f18, $f4
/* 0391CC 7F00469C E7A6010C */  swc1  $f6, 0x10c($sp)
/* 0391D0 7F0046A0 C6080010 */  lwc1  $f8, 0x10($s0)
/* 0391D4 7F0046A4 46004282 */  mul.s $f10, $f8, $f0
/* 0391D8 7F0046A8 46105480 */  add.s $f18, $f10, $f16
/* 0391DC 7F0046AC C7AA0120 */  lwc1  $f10, 0x120($sp)
/* 0391E0 7F0046B0 E7B20110 */  swc1  $f18, 0x110($sp)
/* 0391E4 7F0046B4 C6060014 */  lwc1  $f6, 0x14($s0)
/* 0391E8 7F0046B8 46003202 */  mul.s $f8, $f6, $f0
/* 0391EC 7F0046BC 460A4480 */  add.s $f18, $f8, $f10
/* 0391F0 7F0046C0 E7B20114 */  swc1  $f18, 0x114($sp)
/* 0391F4 7F0046C4 C606000C */  lwc1  $f6, 0xc($s0)
/* 0391F8 7F0046C8 46060202 */  mul.s $f8, $f0, $f6
/* 0391FC 7F0046CC 46082481 */  sub.s $f18, $f4, $f8
/* 039200 7F0046D0 E7B20118 */  swc1  $f18, 0x118($sp)
/* 039204 7F0046D4 C6060010 */  lwc1  $f6, 0x10($s0)
/* 039208 7F0046D8 46060102 */  mul.s $f4, $f0, $f6
/* 03920C 7F0046DC 46048201 */  sub.s $f8, $f16, $f4
/* 039210 7F0046E0 E7A8011C */  swc1  $f8, 0x11c($sp)
/* 039214 7F0046E4 C6120014 */  lwc1  $f18, 0x14($s0)
/* 039218 7F0046E8 46120182 */  mul.s $f6, $f0, $f18
/* 03921C 7F0046EC 46065401 */  sub.s $f16, $f10, $f6
/* 039220 7F0046F0 0FC2E781 */  jal   sub_GAME_7F0B9E04
/* 039224 7F0046F4 E7B00120 */   swc1  $f16, 0x120($sp)
/* 039228 7F0046F8 8E4D0090 */  lw    $t5, 0x90($s2)
/* 03922C 7F0046FC 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 039230 7F004700 44819000 */  mtc1  $at, $f18
/* 039234 7F004704 448D2000 */  mtc1  $t5, $f4
/* 039238 7F004708 AE42008C */  sw    $v0, 0x8c($s2)
/* 03923C 7F00470C 46802220 */  cvt.s.w $f8, $f4
/* 039240 7F004710 46124283 */  div.s $f10, $f8, $f18
/* 039244 7F004714 E64A0090 */  swc1  $f10, 0x90($s2)
.L7F004718:
/* 039248 7F004718 02A02025 */  move  $a0, $s5
.L7F00471C:
/* 03924C 7F00471C 02402825 */  move  $a1, $s2
/* 039250 7F004720 0FC00767 */  jal   domakedefaultobj
/* 039254 7F004724 02603025 */   move  $a2, $s3
/* 039258 7F004728 100000F4 */  b     other_obj_expand
/* 03925C 7F00472C 00000000 */   nop
obj_03_expand:
/* 039260 7F004730 13C000F2 */  beqz  $fp, other_obj_expand
/* 039264 7F004734 00000000 */   nop
/* 039268 7F004738 8E4C000C */  lw    $t4, 0xc($s2)
/* 03926C 7F00473C 02A02025 */  move  $a0, $s5
/* 039270 7F004740 02402825 */  move  $a1, $s2
/* 039274 7F004744 01947824 */  and   $t7, $t4, $s4
/* 039278 7F004748 15E000EC */  bnez  $t7, other_obj_expand
/* 03927C 7F00474C 00000000 */   nop
/* 039280 7F004750 0FC00767 */  jal   domakedefaultobj
/* 039284 7F004754 02603025 */   move  $a2, $s3
/* 039288 7F004758 100000E8 */  b     other_obj_expand
/* 03928C 7F00475C 00000000 */   nop
ammo_box_expand:
/* 039290 7F004760 0FC26919 */  jal   getPlayerCount
/* 039294 7F004764 24100001 */   li    $s0, 1
/* 039298 7F004768 28410002 */  slti  $at, $v0, 2
/* 03929C 7F00476C 1420000E */  bnez  $at, .L7F0047A8
/* 0392A0 7F004770 00000000 */   nop
/* 0392A4 7F004774 0FC31985 */  jal   getPtrMPWeaponSetData
/* 0392A8 7F004778 00000000 */   nop
/* 0392AC 7F00477C 3C0E8007 */  lui   $t6, %hi(lastmpweaponnum)
/* 0392B0 7F004780 8DCE9540 */  lw    $t6, %lo(lastmpweaponnum)($t6)
/* 0392B4 7F004784 000EC080 */  sll   $t8, $t6, 2
/* 0392B8 7F004788 030EC023 */  subu  $t8, $t8, $t6
/* 0392BC 7F00478C 0018C0C0 */  sll   $t8, $t8, 3
/* 0392C0 7F004790 00581821 */  addu  $v1, $v0, $t8
/* 0392C4 7F004794 8C64000C */  lw    $a0, 0xc($v1)
/* 0392C8 7F004798 8C700010 */  lw    $s0, 0x10($v1)
/* 0392CC 7F00479C 00044080 */  sll   $t0, $a0, 2
/* 0392D0 7F0047A0 02484821 */  addu  $t1, $s2, $t0
/* 0392D4 7F0047A4 A530007E */  sh    $s0, 0x7e($t1)
.L7F0047A8:
/* 0392D8 7F0047A8 1A0000D4 */  blez  $s0, other_obj_expand
/* 0392DC 7F0047AC 00000000 */   nop
/* 0392E0 7F0047B0 13C000D2 */  beqz  $fp, other_obj_expand
/* 0392E4 7F0047B4 00000000 */   nop
/* 0392E8 7F0047B8 8E59000C */  lw    $t9, 0xc($s2)
/* 0392EC 7F0047BC 00008825 */  move  $s1, $zero
/* 0392F0 7F0047C0 02408025 */  move  $s0, $s2
/* 0392F4 7F0047C4 03345024 */  and   $t2, $t9, $s4
/* 0392F8 7F0047C8 154000CC */  bnez  $t2, other_obj_expand
/* 0392FC 7F0047CC 00000000 */   nop
.L7F0047D0:
/* 039300 7F0047D0 960B0082 */  lhu   $t3, 0x82($s0)
/* 039304 7F0047D4 59600007 */  blezl $t3, .L7F0047F4
/* 039308 7F0047D8 26310004 */   addiu $s1, $s1, 4
/* 03930C 7F0047DC 96040080 */  lhu   $a0, 0x80($s0)
/* 039310 7F0047E0 52C40004 */  beql  $s6, $a0, .L7F0047F4
/* 039314 7F0047E4 26310004 */   addiu $s1, $s1, 4
/* 039318 7F0047E8 0FC15B0E */  jal   modelLoad
/* 03931C 7F0047EC 00000000 */   nop
/* 039320 7F0047F0 26310004 */  addiu $s1, $s1, 4
.L7F0047F4:
/* 039324 7F0047F4 1637FFF6 */  bne   $s1, $s7, .L7F0047D0
/* 039328 7F0047F8 26100004 */   addiu $s0, $s0, 4
/* 03932C 7F0047FC 02A02025 */  move  $a0, $s5
/* 039330 7F004800 02402825 */  move  $a1, $s2
/* 039334 7F004804 0FC00767 */  jal   domakedefaultobj
/* 039338 7F004808 02603025 */   move  $a2, $s3
/* 03933C 7F00480C 100000BB */  b     other_obj_expand
/* 039340 7F004810 00000000 */   nop
type2D_tank:
/* 039344 7F004814 13C000B9 */  beqz  $fp, other_obj_expand
/* 039348 7F004818 00000000 */   nop
/* 03934C 7F00481C 8E4D000C */  lw    $t5, 0xc($s2)
/* 039350 7F004820 24040020 */  li    $a0, 32
/* 039354 7F004824 01B46024 */  and   $t4, $t5, $s4
/* 039358 7F004828 158000B4 */  bnez  $t4, other_obj_expand
/* 03935C 7F00482C 00000000 */   nop
/* 039360 7F004830 0FC015C4 */  jal   weaponLoadProjectileModels
/* 039364 7F004834 E7B400E8 */   swc1  $f20, 0xe8($sp)
/* 039368 7F004838 02A02025 */  move  $a0, $s5
/* 03936C 7F00483C 02402825 */  move  $a1, $s2
/* 039370 7F004840 0FC00767 */  jal   domakedefaultobj
/* 039374 7F004844 02603025 */   move  $a2, $s3
/* 039378 7F004848 E65400C8 */  swc1  $f20, 0xc8($s2)
/* 03937C 7F00484C E65400CC */  swc1  $f20, 0xcc($s2)
/* 039380 7F004850 C64C0038 */  lwc1  $f12, 0x38($s2)
/* 039384 7F004854 0FC16A8C */  jal   atan2f
/* 039388 7F004858 C64E0040 */   lwc1  $f14, 0x40($s2)
/* 03938C 7F00485C 3C018005 */  lui   $at, %hi(D_8004F024)
/* 039390 7F004860 C426F024 */  lwc1  $f6, %lo(D_8004F024)($at)
/* 039394 7F004864 8E420010 */  lw    $v0, 0x10($s2)
/* 039398 7F004868 46003401 */  sub.s $f16, $f6, $f0
/* 03939C 7F00486C 10400006 */  beqz  $v0, .L7F004888
/* 0393A0 7F004870 E65000DC */   swc1  $f16, 0xdc($s2)
/* 0393A4 7F004874 8C440014 */  lw    $a0, 0x14($v0)
/* 0393A8 7F004878 8C450008 */  lw    $a1, 8($v0)
/* 0393AC 7F00487C 0FC2CA5C */  jal   stanGetPositionYValue
/* 0393B0 7F004880 8C460010 */   lw    $a2, 0x10($v0)
/* 0393B4 7F004884 E7A000E8 */  swc1  $f0, 0xe8($sp)
.L7F004888:
/* 0393B8 7F004888 C7A200E8 */  lwc1  $f2, 0xe8($sp)
/* 0393BC 7F00488C 3C018005 */  lui   $at, %hi(D_8004F028)
/* 0393C0 7F004890 E64200D4 */  swc1  $f2, 0xd4($s2)
/* 0393C4 7F004894 C424F028 */  lwc1  $f4, %lo(D_8004F028)($at)
/* 0393C8 7F004898 46041203 */  div.s $f8, $f2, $f4
/* 0393CC 7F00489C 10000097 */  b     other_obj_expand
/* 0393D0 7F0048A0 E64800D0 */   swc1  $f8, 0xd0($s2)
type27_wheeled_vehicle:
/* 0393D4 7F0048A4 13C00095 */  beqz  $fp, other_obj_expand
/* 0393D8 7F0048A8 00000000 */   nop
/* 0393DC 7F0048AC 8E4F000C */  lw    $t7, 0xc($s2)
/* 0393E0 7F0048B0 02A02025 */  move  $a0, $s5
/* 0393E4 7F0048B4 02402825 */  move  $a1, $s2
/* 0393E8 7F0048B8 01F47024 */  and   $t6, $t7, $s4
/* 0393EC 7F0048BC 15C0008F */  bnez  $t6, other_obj_expand
/* 0393F0 7F0048C0 00000000 */   nop
/* 0393F4 7F0048C4 0FC00767 */  jal   domakedefaultobj
/* 0393F8 7F0048C8 02603025 */   move  $a2, $s3
/* 0393FC 7F0048CC 8E440014 */  lw    $a0, 0x14($s2)
/* 039400 7F0048D0 5080000E */  beql  $a0, $zero, .L7F00490C
/* 039404 7F0048D4 3C01BF80 */   lui   $at, 0xbf80
/* 039408 7F0048D8 8C980008 */  lw    $t8, 8($a0)
/* 03940C 7F0048DC 8F080008 */  lw    $t0, 8($t8)
/* 039410 7F0048E0 8D050014 */  lw    $a1, 0x14($t0)
/* 039414 7F0048E4 50A00009 */  beql  $a1, $zero, .L7F00490C
/* 039418 7F0048E8 3C01BF80 */   lui   $at, 0xbf80
/* 03941C 7F0048EC 0FC1B1E7 */  jal   modelGetNodeRwData
/* 039420 7F0048F0 00000000 */   nop
/* 039424 7F0048F4 8E490008 */  lw    $t1, 8($s2)
/* 039428 7F0048F8 3C011000 */  li    $at, 0x10000000 # 0.000000
/* 03942C 7F0048FC 0121C824 */  and   $t9, $t1, $at
/* 039430 7F004900 2F2A0001 */  sltiu $t2, $t9, 1
/* 039434 7F004904 AC4A0000 */  sw    $t2, ($v0)
/* 039438 7F004908 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
.L7F00490C:
/* 03943C 7F00490C 44819000 */  mtc1  $at, $f18
/* 039440 7F004910 E6540088 */  swc1  $f20, 0x88($s2)
/* 039444 7F004914 E654008C */  swc1  $f20, 0x8c($s2)
/* 039448 7F004918 E6540090 */  swc1  $f20, 0x90($s2)
/* 03944C 7F00491C E6540094 */  swc1  $f20, 0x94($s2)
/* 039450 7F004920 E654009C */  swc1  $f20, 0x9c($s2)
/* 039454 7F004924 E65400A0 */  swc1  $f20, 0xa0($s2)
/* 039458 7F004928 8E440080 */  lw    $a0, 0x80($s2)
/* 03945C 7F00492C 0FC0D4E6 */  jal   ailistFindById
/* 039460 7F004930 E6520098 */   swc1  $f18, 0x98($s2)
/* 039464 7F004934 240BFFFF */  li    $t3, -1
/* 039468 7F004938 AE420080 */  sw    $v0, 0x80($s2)
/* 03946C 7F00493C A6400084 */  sh    $zero, 0x84($s2)
/* 039470 7F004940 A64B0086 */  sh    $t3, 0x86($s2)
/* 039474 7F004944 AE4000A4 */  sw    $zero, 0xa4($s2)
/* 039478 7F004948 AE4000A8 */  sw    $zero, 0xa8($s2)
/* 03947C 7F00494C 1000006B */  b     other_obj_expand
/* 039480 7F004950 AE4000AC */   sw    $zero, 0xac($s2)
type28_aircraft:
/* 039484 7F004954 13C00069 */  beqz  $fp, other_obj_expand
/* 039488 7F004958 00000000 */   nop
/* 03948C 7F00495C 8E4D000C */  lw    $t5, 0xc($s2)
/* 039490 7F004960 02A02025 */  move  $a0, $s5
/* 039494 7F004964 02402825 */  move  $a1, $s2
/* 039498 7F004968 01B46024 */  and   $t4, $t5, $s4
/* 03949C 7F00496C 15800063 */  bnez  $t4, other_obj_expand
/* 0394A0 7F004970 00000000 */   nop
/* 0394A4 7F004974 0FC00767 */  jal   domakedefaultobj
/* 0394A8 7F004978 02603025 */   move  $a2, $s3
/* 0394AC 7F00497C 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
/* 0394B0 7F004980 44810000 */  mtc1  $at, $f0
/* 0394B4 7F004984 E6540098 */  swc1  $f20, 0x98($s2)
/* 0394B8 7F004988 E654009C */  swc1  $f20, 0x9c($s2)
/* 0394BC 7F00498C E6540088 */  swc1  $f20, 0x88($s2)
/* 0394C0 7F004990 E654008C */  swc1  $f20, 0x8c($s2)
/* 0394C4 7F004994 E6540090 */  swc1  $f20, 0x90($s2)
/* 0394C8 7F004998 E65400A4 */  swc1  $f20, 0xa4($s2)
/* 0394CC 7F00499C 8E440080 */  lw    $a0, 0x80($s2)
/* 0394D0 7F0049A0 E64000A0 */  swc1  $f0, 0xa0($s2)
/* 0394D4 7F0049A4 0FC0D4E6 */  jal   ailistFindById
/* 0394D8 7F0049A8 E6400094 */   swc1  $f0, 0x94($s2)
/* 0394DC 7F0049AC 240FFFFF */  li    $t7, -1
/* 0394E0 7F0049B0 AE420080 */  sw    $v0, 0x80($s2)
/* 0394E4 7F0049B4 A6400084 */  sh    $zero, 0x84($s2)
/* 0394E8 7F0049B8 A64F0086 */  sh    $t7, 0x86($s2)
/* 0394EC 7F0049BC AE4000A8 */  sw    $zero, 0xa8($s2)
/* 0394F0 7F0049C0 AE4000AC */  sw    $zero, 0xac($s2)
/* 0394F4 7F0049C4 1000004D */  b     other_obj_expand
/* 0394F8 7F0049C8 AE4000B0 */   sw    $zero, 0xb0($s2)
tag_expand:
/* 0394FC 7F0049CC 864E0006 */  lh    $t6, 6($s2)
/* 039500 7F0049D0 02A02025 */  move  $a0, $s5
/* 039504 7F0049D4 0FC15BA8 */  jal   setupCommandGetObject
/* 039508 7F0049D8 01D32821 */   addu  $a1, $t6, $s3
/* 03950C 7F0049DC 10400004 */  beqz  $v0, .L7F0049F0
/* 039510 7F0049E0 AE42000C */   sw    $v0, 0xc($s2)
/* 039514 7F0049E4 8C580064 */  lw    $t8, 0x64($v0)
/* 039518 7F0049E8 37080010 */  ori   $t0, $t8, 0x10
/* 03951C 7F0049EC AC480064 */  sw    $t0, 0x64($v0)
.L7F0049F0:
/* 039520 7F0049F0 0FC013E2 */  jal   set_parent_cur_tag_entry
/* 039524 7F0049F4 02402025 */   move  $a0, $s2
/* 039528 7F0049F8 10000040 */  b     other_obj_expand
/* 03952C 7F0049FC 00000000 */   nop
type25_rename:
/* 039530 7F004A00 8E490004 */  lw    $t1, 4($s2)
/* 039534 7F004A04 02A02025 */  move  $a0, $s5
/* 039538 7F004A08 0FC15BA8 */  jal   setupCommandGetObject
/* 03953C 7F004A0C 01332821 */   addu  $a1, $t1, $s3
/* 039540 7F004A10 10400004 */  beqz  $v0, .L7F004A24
/* 039544 7F004A14 AE420024 */   sw    $v0, 0x24($s2)
/* 039548 7F004A18 8C590064 */  lw    $t9, 0x64($v0)
/* 03954C 7F004A1C 372A0400 */  ori   $t2, $t9, 0x400
/* 039550 7F004A20 AC4A0064 */  sw    $t2, 0x64($v0)
.L7F004A24:
/* 039554 7F004A24 0FC23628 */  jal   bondinvAddTextOverride
/* 039558 7F004A28 02402025 */   move  $a0, $s2
/* 03955C 7F004A2C 10000033 */  b     other_obj_expand
/* 039560 7F004A30 00000000 */   nop
type23_solo_brief_text:
/* 039564 7F004A34 0FC013E8 */  jal   setup_briefing_text_entry_parent
/* 039568 7F004A38 02402025 */   move  $a0, $s2
/* 03956C 7F004A3C 1000002F */  b     other_obj_expand
/* 039570 7F004A40 00000000 */   nop
type2E_viewport_pos:
/* 039574 7F004A44 8E4B0004 */  lw    $t3, 4($s2)
/* 039578 7F004A48 3C0142C8 */  li    $at, 0x42C80000 # 100.000000
/* 03957C 7F004A4C 44810000 */  mtc1  $at, $f0
/* 039580 7F004A50 448B5000 */  mtc1  $t3, $f10
/* 039584 7F004A54 8E4C000C */  lw    $t4, 0xc($s2)
/* 039588 7F004A58 8E4D0008 */  lw    $t5, 8($s2)
/* 03958C 7F004A5C 468051A0 */  cvt.s.w $f6, $f10
/* 039590 7F004A60 448C5000 */  mtc1  $t4, $f10
/* 039594 7F004A64 448D2000 */  mtc1  $t5, $f4
/* 039598 7F004A68 8E4F0010 */  lw    $t7, 0x10($s2)
/* 03959C 7F004A6C 8E4E0014 */  lw    $t6, 0x14($s2)
/* 0395A0 7F004A70 46802220 */  cvt.s.w $f8, $f4
/* 0395A4 7F004A74 448F2000 */  mtc1  $t7, $f4
/* 0395A8 7F004A78 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 0395AC 7F004A7C 46003403 */  div.s $f16, $f6, $f0
/* 0395B0 7F004A80 468051A0 */  cvt.s.w $f6, $f10
/* 0395B4 7F004A84 46004483 */  div.s $f18, $f8, $f0
/* 0395B8 7F004A88 E6500004 */  swc1  $f16, 4($s2)
/* 0395BC 7F004A8C 46802220 */  cvt.s.w $f8, $f4
/* 0395C0 7F004A90 44812000 */  mtc1  $at, $f4
/* 0395C4 7F004A94 46003403 */  div.s $f16, $f6, $f0
/* 0395C8 7F004A98 448E3000 */  mtc1  $t6, $f6
/* 0395CC 7F004A9C E6520008 */  swc1  $f18, 8($s2)
/* 0395D0 7F004AA0 44819000 */  mtc1  $at, $f18
/* 0395D4 7F004AA4 00000000 */  nop
/* 0395D8 7F004AA8 46124283 */  div.s $f10, $f8, $f18
/* 0395DC 7F004AAC E650000C */  swc1  $f16, 0xc($s2)
/* 0395E0 7F004AB0 46803420 */  cvt.s.w $f16, $f6
/* 0395E4 7F004AB4 46048203 */  div.s $f8, $f16, $f4
/* 0395E8 7F004AB8 E64A0010 */  swc1  $f10, 0x10($s2)
/* 0395EC 7F004ABC 1000000F */  b     other_obj_expand
/* 0395F0 7F004AC0 E6480014 */   swc1  $f8, 0x14($s2)
type17_objective_expand:
/* 0395F4 7F004AC4 0FC013EE */  jal   add_ptr_to_objective
/* 0395F8 7F004AC8 02402025 */   move  $a0, $s2
/* 0395FC 7F004ACC 1000000B */  b     other_obj_expand
/* 039600 7F004AD0 00000000 */   nop
type20_subobjective_expand:
/* 039604 7F004AD4 0FC013FD */  jal   set_parent_cur_obj_enter_room
/* 039608 7F004AD8 02402025 */   move  $a0, $s2
/* 03960C 7F004ADC 10000007 */  b     other_obj_expand
/* 039610 7F004AE0 00000000 */   nop
type21_subobjective_expand:
/* 039614 7F004AE4 0FC01403 */  jal   set_parent_cur_obj_deposited_in_room
/* 039618 7F004AE8 02402025 */   move  $a0, $s2
/* 03961C 7F004AEC 10000003 */  b     other_obj_expand
/* 039620 7F004AF0 00000000 */   nop
type1E_subobjective_expand:
/* 039624 7F004AF4 0FC01409 */  jal   set_parent_cur_obj_photograph
/* 039628 7F004AF8 02402025 */   move  $a0, $s2
other_obj_expand:
/* 03962C 7F004AFC 0FC15A3D */  jal   sizepropdef
/* 039630 7F004B00 02402025 */   move  $a0, $s2
/* 039634 7F004B04 0002C080 */  sll   $t8, $v0, 2
/* 039638 7F004B08 03129021 */  addu  $s2, $t8, $s2
/* 03963C 7F004B0C 92430003 */  lbu   $v1, 3($s2)
/* 039640 7F004B10 24010030 */  li    $at, 48
/* 039644 7F004B14 26730001 */  addiu $s3, $s3, 1
/* 039648 7F004B18 5461FE13 */  bnel  $v1, $at, .L7F004368
/* 03964C 7F004B1C 2468FFFF */   addiu $t0, $v1, -1
/* 039650 7F004B20 3C128007 */  lui   $s2, %hi(g_CurrentSetup+0x0c)
/* 039654 7F004B24 8E525D0C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 039658 7F004B28 00009825 */  move  $s3, $zero
/* 03965C 7F004B2C 92430003 */  lbu   $v1, 3($s2)
.L7F004B30:
/* 039660 7F004B30 24010030 */  li    $at, 48
/* 039664 7F004B34 3C170008 */  lui   $s7, 8
/* 039668 7F004B38 106100CF */  beq   $v1, $at, .L7F004E78
/* 03966C 7F004B3C 24160008 */   li    $s6, 8
/* 039670 7F004B40 241E002B */  li    $fp, 43
/* 039674 7F004B44 24140001 */  li    $s4, 1
/* 039678 7F004B48 2468FFFD */  addiu $t0, $v1, -3
.L7F004B4C:
/* 03967C 7F004B4C 2D01002D */  sltiu $at, $t0, 0x2d
/* 039680 7F004B50 102000A4 */  beqz  $at, .L7F004DE4
/* 039684 7F004B54 00084080 */   sll   $t0, $t0, 2
/* 039688 7F004B58 3C018005 */  lui   $at, %hi(jpt_8004F02C)
/* 03968C 7F004B5C 00280821 */  addu  $at, $at, $t0
/* 039690 7F004B60 8C28F02C */  lw    $t0, %lo(jpt_8004F02C)($at)
/* 039694 7F004B64 01000008 */  jr    $t0
/* 039698 7F004B68 00000000 */   nop
.L7F004B6C:
/* 03969C 7F004B6C 8E490010 */  lw    $t1, 0x10($s2)
/* 0396A0 7F004B70 1120009C */  beqz  $t1, .L7F004DE4
/* 0396A4 7F004B74 00000000 */   nop
/* 0396A8 7F004B78 8E590008 */  lw    $t9, 8($s2)
/* 0396AC 7F004B7C 332A8000 */  andi  $t2, $t9, 0x8000
/* 0396B0 7F004B80 11400098 */  beqz  $t2, .L7F004DE4
/* 0396B4 7F004B84 00000000 */   nop
/* 0396B8 7F004B88 86420006 */  lh    $v0, 6($s2)
/* 0396BC 7F004B8C 02A02025 */  move  $a0, $s5
/* 0396C0 7F004B90 0FC15BA8 */  jal   setupCommandGetObject
/* 0396C4 7F004B94 02622821 */   addu  $a1, $s3, $v0
/* 0396C8 7F004B98 10400092 */  beqz  $v0, .L7F004DE4
/* 0396CC 7F004B9C 00408025 */   move  $s0, $v0
/* 0396D0 7F004BA0 8C4B0010 */  lw    $t3, 0x10($v0)
/* 0396D4 7F004BA4 1160008F */  beqz  $t3, .L7F004DE4
/* 0396D8 7F004BA8 00000000 */   nop
/* 0396DC 7F004BAC 8E4D0064 */  lw    $t5, 0x64($s2)
/* 0396E0 7F004BB0 8E440014 */  lw    $a0, 0x14($s2)
/* 0396E4 7F004BB4 01B76025 */  or    $t4, $t5, $s7
/* 0396E8 7F004BB8 AE4C0064 */  sw    $t4, 0x64($s2)
/* 0396EC 7F004BBC 0FC1B39E */  jal   modelSetScale
/* 0396F0 7F004BC0 8C850014 */   lw    $a1, 0x14($a0)
/* 0396F4 7F004BC4 8E440010 */  lw    $a0, 0x10($s2)
/* 0396F8 7F004BC8 0FC0E969 */  jal   chrpropReparent
/* 0396FC 7F004BCC 8E050010 */   lw    $a1, 0x10($s0)
/* 039700 7F004BD0 10000084 */  b     .L7F004DE4
/* 039704 7F004BD4 00000000 */   nop
.L7F004BD8:
/* 039708 7F004BD8 8E4F0004 */  lw    $t7, 4($s2)
/* 03970C 7F004BDC 0FC15AA2 */  jal   setupGetPtrToCommandByIndex
/* 039710 7F004BE0 01F32021 */   addu  $a0, $t7, $s3
/* 039714 7F004BE4 8E4E0008 */  lw    $t6, 8($s2)
/* 039718 7F004BE8 00408025 */  move  $s0, $v0
/* 03971C 7F004BEC 0FC15AA2 */  jal   setupGetPtrToCommandByIndex
/* 039720 7F004BF0 01D32021 */   addu  $a0, $t6, $s3
/* 039724 7F004BF4 1200007B */  beqz  $s0, .L7F004DE4
/* 039728 7F004BF8 00402825 */   move  $a1, $v0
/* 03972C 7F004BFC 10400079 */  beqz  $v0, .L7F004DE4
/* 039730 7F004C00 00000000 */   nop
/* 039734 7F004C04 92180003 */  lbu   $t8, 3($s0)
/* 039738 7F004C08 16D80076 */  bne   $s6, $t8, .L7F004DE4
/* 03973C 7F004C0C 00000000 */   nop
/* 039740 7F004C10 90480003 */  lbu   $t0, 3($v0)
/* 039744 7F004C14 16C80073 */  bne   $s6, $t0, .L7F004DE4
/* 039748 7F004C18 00000000 */   nop
/* 03974C 7F004C1C 0FC1475D */  jal   propweaponSetDual
/* 039750 7F004C20 02002025 */   move  $a0, $s0
/* 039754 7F004C24 1000006F */  b     .L7F004DE4
/* 039758 7F004C28 00000000 */   nop
.L7F004C2C:
/* 03975C 7F004C2C 8E420004 */  lw    $v0, 4($s2)
/* 039760 7F004C30 8E510008 */  lw    $s1, 8($s2)
/* 039764 7F004C34 02A02025 */  move  $a0, $s5
/* 039768 7F004C38 0FC15BA8 */  jal   setupCommandGetObject
/* 03976C 7F004C3C 02622821 */   addu  $a1, $s3, $v0
/* 039770 7F004C40 00408025 */  move  $s0, $v0
/* 039774 7F004C44 0FC15AA2 */  jal   setupGetPtrToCommandByIndex
/* 039778 7F004C48 02712021 */   addu  $a0, $s3, $s1
/* 03977C 7F004C4C 12000065 */  beqz  $s0, .L7F004DE4
/* 039780 7F004C50 00000000 */   nop
/* 039784 7F004C54 8E030010 */  lw    $v1, 0x10($s0)
/* 039788 7F004C58 10600062 */  beqz  $v1, .L7F004DE4
/* 03978C 7F004C5C 00000000 */   nop
/* 039790 7F004C60 10400060 */  beqz  $v0, .L7F004DE4
/* 039794 7F004C64 00000000 */   nop
/* 039798 7F004C68 90490003 */  lbu   $t1, 3($v0)
/* 03979C 7F004C6C 1689005D */  bne   $s4, $t1, .L7F004DE4
/* 0397A0 7F004C70 00000000 */   nop
/* 0397A4 7F004C74 8C590010 */  lw    $t9, 0x10($v0)
/* 0397A8 7F004C78 1320005A */  beqz  $t9, .L7F004DE4
/* 0397AC 7F004C7C 00000000 */   nop
/* 0397B0 7F004C80 AE430004 */  sw    $v1, 4($s2)
/* 0397B4 7F004C84 8C4A0010 */  lw    $t2, 0x10($v0)
/* 0397B8 7F004C88 02402025 */  move  $a0, $s2
/* 0397BC 7F004C8C 0FC00644 */  jal   initSetLevelLoadPropSwitch
/* 0397C0 7F004C90 AE4A0008 */   sw    $t2, 8($s2)
/* 0397C4 7F004C94 8E0B0064 */  lw    $t3, 0x64($s0)
/* 0397C8 7F004C98 356D0001 */  ori   $t5, $t3, 1
/* 0397CC 7F004C9C 10000051 */  b     .L7F004DE4
/* 0397D0 7F004CA0 AE0D0064 */   sw    $t5, 0x64($s0)
.L7F004CA4:
/* 0397D4 7F004CA4 8E420004 */  lw    $v0, 4($s2)
/* 0397D8 7F004CA8 8E4C000C */  lw    $t4, 0xc($s2)
/* 0397DC 7F004CAC 8E510008 */  lw    $s1, 8($s2)
/* 0397E0 7F004CB0 02A02025 */  move  $a0, $s5
/* 0397E4 7F004CB4 02622821 */  addu  $a1, $s3, $v0
/* 0397E8 7F004CB8 0FC15BA8 */  jal   setupCommandGetObject
/* 0397EC 7F004CBC AFAC0080 */   sw    $t4, 0x80($sp)
/* 0397F0 7F004CC0 00408025 */  move  $s0, $v0
/* 0397F4 7F004CC4 02A02025 */  move  $a0, $s5
/* 0397F8 7F004CC8 0FC15BA8 */  jal   setupCommandGetObject
/* 0397FC 7F004CCC 02712821 */   addu  $a1, $s3, $s1
/* 039800 7F004CD0 8FAF0080 */  lw    $t7, 0x80($sp)
/* 039804 7F004CD4 00408825 */  move  $s1, $v0
/* 039808 7F004CD8 02A02025 */  move  $a0, $s5
/* 03980C 7F004CDC 0FC15BA8 */  jal   setupCommandGetObject
/* 039810 7F004CE0 026F2821 */   addu  $a1, $s3, $t7
/* 039814 7F004CE4 1200003F */  beqz  $s0, .L7F004DE4
/* 039818 7F004CE8 00000000 */   nop
/* 03981C 7F004CEC 8E0E0010 */  lw    $t6, 0x10($s0)
/* 039820 7F004CF0 11C0003C */  beqz  $t6, .L7F004DE4
/* 039824 7F004CF4 00000000 */   nop
/* 039828 7F004CF8 1220003A */  beqz  $s1, .L7F004DE4
/* 03982C 7F004CFC 00000000 */   nop
/* 039830 7F004D00 8E380010 */  lw    $t8, 0x10($s1)
/* 039834 7F004D04 13000037 */  beqz  $t8, .L7F004DE4
/* 039838 7F004D08 00000000 */   nop
/* 03983C 7F004D0C 92280003 */  lbu   $t0, 3($s1)
/* 039840 7F004D10 17C80034 */  bne   $fp, $t0, .L7F004DE4
/* 039844 7F004D14 00000000 */   nop
/* 039848 7F004D18 10400032 */  beqz  $v0, .L7F004DE4
/* 03984C 7F004D1C 00000000 */   nop
/* 039850 7F004D20 8C490010 */  lw    $t1, 0x10($v0)
/* 039854 7F004D24 1120002F */  beqz  $t1, .L7F004DE4
/* 039858 7F004D28 00000000 */   nop
/* 03985C 7F004D2C 90590003 */  lbu   $t9, 3($v0)
/* 039860 7F004D30 02402025 */  move  $a0, $s2
/* 039864 7F004D34 1699002B */  bne   $s4, $t9, .L7F004DE4
/* 039868 7F004D38 00000000 */   nop
/* 03986C 7F004D3C AE500004 */  sw    $s0, 4($s2)
/* 039870 7F004D40 AE510008 */  sw    $s1, 8($s2)
/* 039874 7F004D44 AE42000C */  sw    $v0, 0xc($s2)
/* 039878 7F004D48 0FC00650 */  jal   initSetLevelLoadPropSafeItem
/* 03987C 7F004D4C AFA20074 */   sw    $v0, 0x74($sp)
/* 039880 7F004D50 8E0A000C */  lw    $t2, 0xc($s0)
/* 039884 7F004D54 8FA30074 */  lw    $v1, 0x74($sp)
/* 039888 7F004D58 354B0400 */  ori   $t3, $t2, 0x400
/* 03988C 7F004D5C AE0B000C */  sw    $t3, 0xc($s0)
/* 039890 7F004D60 8C6D000C */  lw    $t5, 0xc($v1)
/* 039894 7F004D64 35AC0400 */  ori   $t4, $t5, 0x400
/* 039898 7F004D68 1000001E */  b     .L7F004DE4
/* 03989C 7F004D6C AC6C000C */   sw    $t4, 0xc($v1)
.L7F004D70:
/* 0398A0 7F004D70 8E420004 */  lw    $v0, 4($s2)
/* 0398A4 7F004D74 8E510008 */  lw    $s1, 8($s2)
/* 0398A8 7F004D78 02A02025 */  move  $a0, $s5
/* 0398AC 7F004D7C 0FC15BA8 */  jal   setupCommandGetObject
/* 0398B0 7F004D80 02622821 */   addu  $a1, $s3, $v0
/* 0398B4 7F004D84 00408025 */  move  $s0, $v0
/* 0398B8 7F004D88 02A02025 */  move  $a0, $s5
/* 0398BC 7F004D8C 0FC15BA8 */  jal   setupCommandGetObject
/* 0398C0 7F004D90 02712821 */   addu  $a1, $s3, $s1
/* 0398C4 7F004D94 12000013 */  beqz  $s0, .L7F004DE4
/* 0398C8 7F004D98 00000000 */   nop
/* 0398CC 7F004D9C 8E0F0010 */  lw    $t7, 0x10($s0)
/* 0398D0 7F004DA0 11E00010 */  beqz  $t7, .L7F004DE4
/* 0398D4 7F004DA4 00000000 */   nop
/* 0398D8 7F004DA8 1040000E */  beqz  $v0, .L7F004DE4
/* 0398DC 7F004DAC 00000000 */   nop
/* 0398E0 7F004DB0 8C4E0010 */  lw    $t6, 0x10($v0)
/* 0398E4 7F004DB4 11C0000B */  beqz  $t6, .L7F004DE4
/* 0398E8 7F004DB8 00000000 */   nop
/* 0398EC 7F004DBC 92180003 */  lbu   $t8, 3($s0)
/* 0398F0 7F004DC0 02402025 */  move  $a0, $s2
/* 0398F4 7F004DC4 16980007 */  bne   $s4, $t8, .L7F004DE4
/* 0398F8 7F004DC8 00000000 */   nop
/* 0398FC 7F004DCC AE500004 */  sw    $s0, 4($s2)
/* 039900 7F004DD0 0FC0064A */  jal   initSetLevelLoadPropLockDoor
/* 039904 7F004DD4 AE420008 */   sw    $v0, 8($s2)
/* 039908 7F004DD8 8E080064 */  lw    $t0, 0x64($s0)
/* 03990C 7F004DDC 35092000 */  ori   $t1, $t0, 0x2000
/* 039910 7F004DE0 AE090064 */  sw    $t1, 0x64($s0)
.L7F004DE4:
/* 039914 7F004DE4 0FC15A3D */  jal   sizepropdef
/* 039918 7F004DE8 02402025 */   move  $a0, $s2
/* 03991C 7F004DEC 0002C880 */  sll   $t9, $v0, 2
/* 039920 7F004DF0 03329021 */  addu  $s2, $t9, $s2
/* 039924 7F004DF4 92430003 */  lbu   $v1, 3($s2)
/* 039928 7F004DF8 24010030 */  li    $at, 48
/* 03992C 7F004DFC 26730001 */  addiu $s3, $s3, 1
/* 039930 7F004E00 5461FF52 */  bnel  $v1, $at, .L7F004B4C
/* 039934 7F004E04 2468FFFD */   addiu $t0, $v1, -3
/* 039938 7F004E08 1000001B */  b     .L7F004E78
/* 03993C 7F004E0C 00000000 */   nop
.L7F004E10:
/* 039940 7F004E10 3C018007 */  lui   $at, %hi(g_CurrentSetup+0)
/* 039944 7F004E14 AC205D00 */  sw    $zero, %lo(g_CurrentSetup+0)($at)
/* 039948 7F004E18 3C018007 */  lui   $at, %hi(g_CurrentSetup+4)
/* 03994C 7F004E1C AC205D04 */  sw    $zero, %lo(g_CurrentSetup+4)($at)
/* 039950 7F004E20 3C018007 */  lui   $at, %hi(g_CurrentSetup+8)
/* 039954 7F004E24 AC205D08 */  sw    $zero, %lo(g_CurrentSetup+8)($at)
/* 039958 7F004E28 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x0c)
/* 03995C 7F004E2C AC205D0C */  sw    $zero, %lo(g_CurrentSetup+0x0c)($at)
/* 039960 7F004E30 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x10)
/* 039964 7F004E34 AC205D10 */  sw    $zero, %lo(g_CurrentSetup+0x10)($at)
/* 039968 7F004E38 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x14)
/* 03996C 7F004E3C AC205D14 */  sw    $zero, %lo(g_CurrentSetup+0x14)($at)
/* 039970 7F004E40 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x18)
/* 039974 7F004E44 AC205D18 */  sw    $zero, %lo(g_CurrentSetup+0x18)($at)
/* 039978 7F004E48 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x1C)
/* 03997C 7F004E4C AC205D1C */  sw    $zero, %lo(g_CurrentSetup+0x1C)($at)
/* 039980 7F004E50 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x20)
/* 039984 7F004E54 AC205D20 */  sw    $zero, %lo(g_CurrentSetup+0x20)($at)
/* 039988 7F004E58 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x24)
/* 03998C 7F004E5C AC205D24 */  sw    $zero, %lo(g_CurrentSetup+0x24)($at)
/* 039990 7F004E60 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 039994 7F004E64 00002025 */   move  $a0, $zero
/* 039998 7F004E68 0FC01550 */  jal   sub_GAME_7F005540
/* 03999C 7F004E6C 00002025 */   move  $a0, $zero
/* 0399A0 7F004E70 0FC01589 */  jal   sub_GAME_7F005624
/* 0399A4 7F004E74 00002025 */   move  $a0, $zero
.L7F004E78:
/* 0399A8 7F004E78 0FC00417 */  jal   alloc_false_GUARDdata_to_exec_global_action
/* 0399AC 7F004E7C 00000000 */   nop
/* 0399B0 7F004E80 8FBF0044 */  lw    $ra, 0x44($sp)
/* 0399B4 7F004E84 D7B40018 */  ldc1  $f20, 0x18($sp)
/* 0399B8 7F004E88 8FB00020 */  lw    $s0, 0x20($sp)
/* 0399BC 7F004E8C 8FB10024 */  lw    $s1, 0x24($sp)
/* 0399C0 7F004E90 8FB20028 */  lw    $s2, 0x28($sp)
/* 0399C4 7F004E94 8FB3002C */  lw    $s3, 0x2c($sp)
/* 0399C8 7F004E98 8FB40030 */  lw    $s4, 0x30($sp)
/* 0399CC 7F004E9C 8FB50034 */  lw    $s5, 0x34($sp)
/* 0399D0 7F004EA0 8FB60038 */  lw    $s6, 0x38($sp)
/* 0399D4 7F004EA4 8FB7003C */  lw    $s7, 0x3c($sp)
/* 0399D8 7F004EA8 8FBE0040 */  lw    $fp, 0x40($sp)
/* 0399DC 7F004EAC 03E00008 */  jr    $ra
/* 0399E0 7F004EB0 27BD0280 */   addiu $sp, $sp, 0x280
)
#endif

#ifdef VERSION_JP
 GLOBAL_ASM(
.late_rodata
glabel jpt_object_type_expansion
.word door_expand
.word door_scale_expand
.word obj_03_expand
.word key_expand
.word obj_03_expand
.word surv_camera_expand
.word obj_03_expand
.word item_expand
.word actor_expand
.word single_screen_display_expand
.word multi_screen_display_expand
.word rack_expand
.word autogun_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word hat_expand
.word actor_attr_expand
.word other_obj_expand
.word ammo_box_expand
.word body_armor_expand
.word tag_expand
.word type17_objective_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word type1E_subobjective_expand
.word other_obj_expand
.word type20_subobjective_expand
.word type21_subobjective_expand
.word other_obj_expand
.word type23_solo_brief_text
.word obj_03_expand
.word type25_rename
.word other_obj_expand
.word type27_wheeled_vehicle
.word type28_aircraft
.word obj_03_expand
.word obj_03_expand
.word obj_03_expand
.word other_obj_expand
.word type2D_tank
.word type2E_viewport_pos
.word type2F_tinted_glass

glabel D_8004F024
.word 0x40c90fdb
glabel D_8004F028
.word 0x3e2e147c

glabel jpt_8004F02C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004BE8
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004C3C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004D80
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004CB4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C


  .text
  glabel proplvreset2
/* 038760 7F003BF0 27BDFD80 */  addiu $sp, $sp, -0x280
/* 038764 7F003BF4 AFB50034 */  sw    $s5, 0x34($sp)
/* 038768 7F003BF8 0080A825 */  move  $s5, $a0
/* 03876C 7F003BFC AFBF0044 */  sw    $ra, 0x44($sp)
/* 038770 7F003C00 3C058005 */  lui   $a1, %hi(aNochr) # $a1, 0x8005
/* 038774 7F003C04 AFBE0040 */  sw    $fp, 0x40($sp)
/* 038778 7F003C08 AFB7003C */  sw    $s7, 0x3c($sp)
/* 03877C 7F003C0C AFB60038 */  sw    $s6, 0x38($sp)
/* 038780 7F003C10 AFB40030 */  sw    $s4, 0x30($sp)
/* 038784 7F003C14 AFB3002C */  sw    $s3, 0x2c($sp)
/* 038788 7F003C18 AFB20028 */  sw    $s2, 0x28($sp)
/* 03878C 7F003C1C AFB10024 */  sw    $s1, 0x24($sp)
/* 038790 7F003C20 AFB00020 */  sw    $s0, 0x20($sp)
/* 038794 7F003C24 F7B40018 */  sdc1  $f20, 0x18($sp)
/* 038798 7F003C28 24A5EDC0 */  addiu $a1, %lo(aNochr) # addiu $a1, $a1, -0x1240
/* 03879C 7F003C2C 0C0029AC */  jal   tokenFind
/* 0387A0 7F003C30 24040001 */   li    $a0, 1
/* 0387A4 7F003C34 2C460001 */  sltiu $a2, $v0, 1
/* 0387A8 7F003C38 10C00005 */  beqz  $a2, .L7F003C50
/* 0387AC 7F003C3C 24040001 */   li    $a0, 1
/* 0387B0 7F003C40 3C058005 */  lui   $a1, %hi(aNoprop) # $a1, 0x8005
/* 0387B4 7F003C44 0C0029AC */  jal   tokenFind
/* 0387B8 7F003C48 24A5EDC8 */   addiu $a1, %lo(aNoprop) # addiu $a1, $a1, -0x1238
/* 0387BC 7F003C4C 2C460001 */  sltiu $a2, $v0, 1
.L7F003C50:
/* 0387C0 7F003C50 3C058005 */  lui   $a1, %hi(aNoobj) # $a1, 0x8005
/* 0387C4 7F003C54 AFA60278 */  sw    $a2, 0x278($sp)
/* 0387C8 7F003C58 24A5EDD0 */  addiu $a1, %lo(aNoobj) # addiu $a1, $a1, -0x1230
/* 0387CC 7F003C5C 0C0029AC */  jal   tokenFind
/* 0387D0 7F003C60 24040001 */   li    $a0, 1
/* 0387D4 7F003C64 2C430001 */  sltiu $v1, $v0, 1
/* 0387D8 7F003C68 10600005 */  beqz  $v1, .L7F003C80
/* 0387DC 7F003C6C 24040001 */   li    $a0, 1
/* 0387E0 7F003C70 3C058005 */  lui   $a1, %hi(aNoprop_0) # $a1, 0x8005
/* 0387E4 7F003C74 0C0029AC */  jal   tokenFind
/* 0387E8 7F003C78 24A5EDD8 */   addiu $a1, %lo(aNoprop_0) # addiu $a1, $a1, -0x1228
/* 0387EC 7F003C7C 2C430001 */  sltiu $v1, $v0, 1
.L7F003C80:
/* 0387F0 7F003C80 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 0387F4 7F003C84 44812000 */  mtc1  $at, $f4
/* 0387F8 7F003C88 0060F025 */  move  $fp, $v1
/* 0387FC 7F003C8C 3C018003 */  lui   $at, %hi(g_DoorScale) # $at, 0x8003
/* 038800 7F003C90 3C038004 */  lui   $v1, %hi(PitemZ_entries+0xFF0) # $v1, 0x8004
/* 038804 7F003C94 3C028004 */  lui   $v0, %hi(PitemZ_entries) # $v0, 0x8004
/* 038808 7F003C98 2442A258 */  addiu $v0, %lo(PitemZ_entries) # addiu $v0, $v0, -0x5da8
/* 03880C 7F003C9C 2463B248 */  addiu $v1, %lo(PitemZ_entries+0xFF0) # addiu $v1, $v1, -0x4db8
/* 038810 7F003CA0 E424A400 */  swc1  $f4, %lo(g_DoorScale)($at)
.L7F003CA4:
/* 038814 7F003CA4 8C4E0000 */  lw    $t6, ($v0)
/* 038818 7F003CA8 2442000C */  addiu $v0, $v0, 0xc
/* 03881C 7F003CAC 0043082B */  sltu  $at, $v0, $v1
/* 038820 7F003CB0 1420FFFC */  bnez  $at, .L7F003CA4
/* 038824 7F003CB4 ADC00000 */   sw    $zero, ($t6)
/* 038828 7F003CB8 2AA1003B */  slti  $at, $s5, 0x3b
/* 03882C 7F003CBC 10200458 */  beqz  $at, .L7F004E20
/* 038830 7F003CC0 00157880 */   sll   $t7, $s5, 2
/* 038834 7F003CC4 3C188003 */  lui   $t8, %hi(setup_text_pointers) # $t8, 0x8003
/* 038838 7F003CC8 27187514 */  addiu $t8, %lo(setup_text_pointers) # addiu $t8, $t8, 0x7514
/* 03883C 7F003CCC 01F88021 */  addu  $s0, $t7, $t8
/* 038840 7F003CD0 8E020000 */  lw    $v0, ($s0)
/* 038844 7F003CD4 3C198005 */  lui   $t9, %hi(only_read_by_stageload) # $t9, 0x8005
/* 038848 7F003CD8 27B10174 */  addiu $s1, $sp, 0x174
/* 03884C 7F003CDC 10400450 */  beqz  $v0, .L7F004E20
/* 038850 7F003CE0 2739EDE0 */   addiu $t9, %lo(only_read_by_stageload) # addiu $t9, $t9, -0x1220
/* 038854 7F003CE4 02205025 */  move  $t2, $s1
/* 038858 7F003CE8 272900FC */  addiu $t1, $t9, 0xfc
.L7F003CEC:
/* 03885C 7F003CEC 8F210000 */  lw    $at, ($t9)
/* 038860 7F003CF0 2739000C */  addiu $t9, $t9, 0xc
/* 038864 7F003CF4 254A000C */  addiu $t2, $t2, 0xc
/* 038868 7F003CF8 AD41FFF4 */  sw    $at, -0xc($t2)
/* 03886C 7F003CFC 8F21FFF8 */  lw    $at, -8($t9)
/* 038870 7F003D00 AD41FFF8 */  sw    $at, -8($t2)
/* 038874 7F003D04 8F21FFFC */  lw    $at, -4($t9)
/* 038878 7F003D08 1729FFF8 */  bne   $t9, $t1, .L7F003CEC
/* 03887C 7F003D0C AD41FFFC */   sw    $at, -4($t2)
/* 038880 7F003D10 8F210000 */  lw    $at, ($t9)
/* 038884 7F003D14 0000A025 */  move  $s4, $zero
/* 038888 7F003D18 00009825 */  move  $s3, $zero
/* 03888C 7F003D1C AD410000 */  sw    $at, ($t2)
/* 038890 7F003D20 904B0000 */  lbu   $t3, ($v0)
/* 038894 7F003D24 A3A00175 */  sb    $zero, 0x175($sp)
/* 038898 7F003D28 0FC26C01 */  jal   getPlayerCount
/* 03889C 7F003D2C A3AB0174 */   sb    $t3, 0x174($sp)
/* 0388A0 7F003D30 28410002 */  slti  $at, $v0, 2
/* 0388A4 7F003D34 14200004 */  bnez  $at, .L7F003D48
/* 0388A8 7F003D38 3C058005 */   lui   $a1, %hi(aMp_) # $a1, 0x8005
/* 0388AC 7F003D3C 24A5EEE0 */  addiu $a1, %lo(aMp_) # addiu $a1, $a1, -0x1120
/* 0388B0 7F003D40 0C002A03 */  jal   strcat
/* 0388B4 7F003D44 02202025 */   move  $a0, $s1
.L7F003D48:
/* 0388B8 7F003D48 8E050000 */  lw    $a1, ($s0)
/* 0388BC 7F003D4C 02202025 */  move  $a0, $s1
/* 0388C0 7F003D50 0C002A03 */  jal   strcat
/* 0388C4 7F003D54 24A50001 */   addiu $a1, $a1, 1
/* 0388C8 7F003D58 02202025 */  move  $a0, $s1
/* 0388CC 7F003D5C 24050001 */  li    $a1, 1
/* 0388D0 7F003D60 24060100 */  li    $a2, 256
/* 0388D4 7F003D64 0FC2F62D */  jal   _fileNameLoadToBank
/* 0388D8 7F003D68 24070004 */   li    $a3, 4
/* 0388DC 7F003D6C 3C038007 */  lui   $v1, %hi(g_ptrStageSetupFile) # $v1, 0x8007
/* 0388E0 7F003D70 24635D68 */  addiu $v1, %lo(g_ptrStageSetupFile) # addiu $v1, $v1, 0x5d68
/* 0388E4 7F003D74 AC620000 */  sw    $v0, ($v1)
/* 0388E8 7F003D78 00408825 */  move  $s1, $v0
/* 0388EC 7F003D7C 0FC308A4 */  jal   langGetLangBankIndexFromStagenum
/* 0388F0 7F003D80 02A02025 */   move  $a0, $s5
/* 0388F4 7F003D84 0FC30A6E */  jal   langLoadToAddr
/* 0388F8 7F003D88 00402025 */   move  $a0, $v0
/* 0388FC 7F003D8C 8E2C0000 */  lw    $t4, ($s1)
/* 038900 7F003D90 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8007
/* 038904 7F003D94 24E75D40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d40
/* 038908 7F003D98 022C6821 */  addu  $t5, $s1, $t4
/* 03890C 7F003D9C ACED0000 */  sw    $t5, ($a3)
/* 038910 7F003DA0 8E2E0004 */  lw    $t6, 4($s1)
/* 038914 7F003DA4 022E7821 */  addu  $t7, $s1, $t6
/* 038918 7F003DA8 ACEF0004 */  sw    $t7, 4($a3)
/* 03891C 7F003DAC 8E380008 */  lw    $t8, 8($s1)
/* 038920 7F003DB0 02384021 */  addu  $t0, $s1, $t8
/* 038924 7F003DB4 ACE80008 */  sw    $t0, 8($a3)
/* 038928 7F003DB8 8E29000C */  lw    $t1, 0xc($s1)
/* 03892C 7F003DBC 0229C821 */  addu  $t9, $s1, $t1
/* 038930 7F003DC0 ACF9000C */  sw    $t9, 0xc($a3)
/* 038934 7F003DC4 8E2A0010 */  lw    $t2, 0x10($s1)
/* 038938 7F003DC8 022A5821 */  addu  $t3, $s1, $t2
/* 03893C 7F003DCC ACEB0010 */  sw    $t3, 0x10($a3)
/* 038940 7F003DD0 8E2C0014 */  lw    $t4, 0x14($s1)
/* 038944 7F003DD4 022C6821 */  addu  $t5, $s1, $t4
/* 038948 7F003DD8 ACED0014 */  sw    $t5, 0x14($a3)
/* 03894C 7F003DDC 8E2E0018 */  lw    $t6, 0x18($s1)
/* 038950 7F003DE0 022E7821 */  addu  $t7, $s1, $t6
/* 038954 7F003DE4 ACEF0018 */  sw    $t7, 0x18($a3)
/* 038958 7F003DE8 8E38001C */  lw    $t8, 0x1c($s1)
/* 03895C 7F003DEC 02384021 */  addu  $t0, $s1, $t8
/* 038960 7F003DF0 ACE8001C */  sw    $t0, 0x1c($a3)
/* 038964 7F003DF4 8E220020 */  lw    $v0, 0x20($s1)
/* 038968 7F003DF8 10400003 */  beqz  $v0, .L7F003E08
/* 03896C 7F003DFC 02224821 */   addu  $t1, $s1, $v0
/* 038970 7F003E00 10000002 */  b     .L7F003E0C
/* 038974 7F003E04 ACE90020 */   sw    $t1, 0x20($a3)
.L7F003E08:
/* 038978 7F003E08 ACE00020 */  sw    $zero, 0x20($a3)
.L7F003E0C:
/* 03897C 7F003E0C 8E220024 */  lw    $v0, 0x24($s1)
/* 038980 7F003E10 10400003 */  beqz  $v0, .L7F003E20
/* 038984 7F003E14 0222C821 */   addu  $t9, $s1, $v0
/* 038988 7F003E18 10000002 */  b     .L7F003E24
/* 03898C 7F003E1C ACF90024 */   sw    $t9, 0x24($a3)
.L7F003E20:
/* 038990 7F003E20 ACE00024 */  sw    $zero, 0x24($a3)
.L7F003E24:
/* 038994 7F003E24 8CE50000 */  lw    $a1, ($a3)
/* 038998 7F003E28 50A00010 */  beql  $a1, $zero, .L7F003E6C
/* 03899C 7F003E2C 8CE50004 */   lw    $a1, 4($a3)
/* 0389A0 7F003E30 8CAA0000 */  lw    $t2, ($a1)
/* 0389A4 7F003E34 00001825 */  move  $v1, $zero
/* 0389A8 7F003E38 00A01025 */  move  $v0, $a1
/* 0389AC 7F003E3C 0542000B */  bltzl $t2, .L7F003E6C
/* 0389B0 7F003E40 8CE50004 */   lw    $a1, 4($a3)
/* 0389B4 7F003E44 8C4B0004 */  lw    $t3, 4($v0)
.L7F003E48:
/* 0389B8 7F003E48 24630010 */  addiu $v1, $v1, 0x10
/* 0389BC 7F003E4C 01716021 */  addu  $t4, $t3, $s1
/* 0389C0 7F003E50 AC4C0004 */  sw    $t4, 4($v0)
/* 0389C4 7F003E54 8CED0000 */  lw    $t5, ($a3)
/* 0389C8 7F003E58 01A31021 */  addu  $v0, $t5, $v1
/* 0389CC 7F003E5C 8C4E0000 */  lw    $t6, ($v0)
/* 0389D0 7F003E60 05C3FFF9 */  bgezl $t6, .L7F003E48
/* 0389D4 7F003E64 8C4B0004 */   lw    $t3, 4($v0)
/* 0389D8 7F003E68 8CE50004 */  lw    $a1, 4($a3)
.L7F003E6C:
/* 0389DC 7F003E6C 50A00015 */  beql  $a1, $zero, .L7F003EC4
/* 0389E0 7F003E70 8CE60014 */   lw    $a2, 0x14($a3)
/* 0389E4 7F003E74 8CAF0000 */  lw    $t7, ($a1)
/* 0389E8 7F003E78 00001825 */  move  $v1, $zero
/* 0389EC 7F003E7C 00A01025 */  move  $v0, $a1
/* 0389F0 7F003E80 51E00010 */  beql  $t7, $zero, .L7F003EC4
/* 0389F4 7F003E84 8CE60014 */   lw    $a2, 0x14($a3)
/* 0389F8 7F003E88 8CA40000 */  lw    $a0, ($a1)
/* 0389FC 7F003E8C 0091C021 */  addu  $t8, $a0, $s1
.L7F003E90:
/* 038A00 7F003E90 AC580000 */  sw    $t8, ($v0)
/* 038A04 7F003E94 8CE80004 */  lw    $t0, 4($a3)
/* 038A08 7F003E98 01031021 */  addu  $v0, $t0, $v1
/* 038A0C 7F003E9C 8C490004 */  lw    $t1, 4($v0)
/* 038A10 7F003EA0 2463000C */  addiu $v1, $v1, 0xc
/* 038A14 7F003EA4 0131C821 */  addu  $t9, $t1, $s1
/* 038A18 7F003EA8 AC590004 */  sw    $t9, 4($v0)
/* 038A1C 7F003EAC 8CEA0004 */  lw    $t2, 4($a3)
/* 038A20 7F003EB0 01431021 */  addu  $v0, $t2, $v1
/* 038A24 7F003EB4 8C440000 */  lw    $a0, ($v0)
/* 038A28 7F003EB8 5480FFF5 */  bnezl $a0, .L7F003E90
/* 038A2C 7F003EBC 0091C021 */   addu  $t8, $a0, $s1
/* 038A30 7F003EC0 8CE60014 */  lw    $a2, 0x14($a3)
.L7F003EC4:
/* 038A34 7F003EC4 50C00010 */  beql  $a2, $zero, .L7F003F08
/* 038A38 7F003EC8 8CE20010 */   lw    $v0, 0x10($a3)
/* 038A3C 7F003ECC 8CCB0000 */  lw    $t3, ($a2)
/* 038A40 7F003ED0 00002825 */  move  $a1, $zero
/* 038A44 7F003ED4 00C01025 */  move  $v0, $a2
/* 038A48 7F003ED8 5160000B */  beql  $t3, $zero, .L7F003F08
/* 038A4C 7F003EDC 8CE20010 */   lw    $v0, 0x10($a3)
/* 038A50 7F003EE0 8CC30000 */  lw    $v1, ($a2)
/* 038A54 7F003EE4 00716021 */  addu  $t4, $v1, $s1
.L7F003EE8:
/* 038A58 7F003EE8 AC4C0000 */  sw    $t4, ($v0)
/* 038A5C 7F003EEC 8CED0014 */  lw    $t5, 0x14($a3)
/* 038A60 7F003EF0 24A50008 */  addiu $a1, $a1, 8
/* 038A64 7F003EF4 01A51021 */  addu  $v0, $t5, $a1
/* 038A68 7F003EF8 8C430000 */  lw    $v1, ($v0)
/* 038A6C 7F003EFC 5460FFFA */  bnezl $v1, .L7F003EE8
/* 038A70 7F003F00 00716021 */   addu  $t4, $v1, $s1
/* 038A74 7F003F04 8CE20010 */  lw    $v0, 0x10($a3)
.L7F003F08:
/* 038A78 7F003F08 50400020 */  beql  $v0, $zero, .L7F003F8C
/* 038A7C 7F003F0C 8CEA0018 */   lw    $t2, 0x18($a3)
/* 038A80 7F003F10 8C4E0000 */  lw    $t6, ($v0)
/* 038A84 7F003F14 00002825 */  move  $a1, $zero
/* 038A88 7F003F18 00403025 */  move  $a2, $v0
/* 038A8C 7F003F1C 51C0001B */  beql  $t6, $zero, .L7F003F8C
/* 038A90 7F003F20 8CEA0018 */   lw    $t2, 0x18($a3)
/* 038A94 7F003F24 8C440000 */  lw    $a0, ($v0)
/* 038A98 7F003F28 00917821 */  addu  $t7, $a0, $s1
.L7F003F2C:
/* 038A9C 7F003F2C ACCF0000 */  sw    $t7, ($a2)
/* 038AA0 7F003F30 8CF80010 */  lw    $t8, 0x10($a3)
/* 038AA4 7F003F34 00001825 */  move  $v1, $zero
/* 038AA8 7F003F38 03053021 */  addu  $a2, $t8, $a1
/* 038AAC 7F003F3C 8CC40000 */  lw    $a0, ($a2)
/* 038AB0 7F003F40 8C880000 */  lw    $t0, ($a0)
/* 038AB4 7F003F44 00801025 */  move  $v0, $a0
/* 038AB8 7F003F48 05020009 */  bltzl $t0, .L7F003F70
/* 038ABC 7F003F4C A4C30006 */   sh    $v1, 6($a2)
/* 038AC0 7F003F50 8C490004 */  lw    $t1, 4($v0)
.L7F003F54:
/* 038AC4 7F003F54 24630001 */  addiu $v1, $v1, 1
/* 038AC8 7F003F58 24420004 */  addiu $v0, $v0, 4
/* 038ACC 7F003F5C 0523FFFD */  bgezl $t1, .L7F003F54
/* 038AD0 7F003F60 8C490004 */   lw    $t1, 4($v0)
/* 038AD4 7F003F64 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8007
/* 038AD8 7F003F68 24E75D40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d40
/* 038ADC 7F003F6C A4C30006 */  sh    $v1, 6($a2)
.L7F003F70:
/* 038AE0 7F003F70 8CF90010 */  lw    $t9, 0x10($a3)
/* 038AE4 7F003F74 24A50008 */  addiu $a1, $a1, 8
/* 038AE8 7F003F78 03253021 */  addu  $a2, $t9, $a1
/* 038AEC 7F003F7C 8CC40000 */  lw    $a0, ($a2)
/* 038AF0 7F003F80 5480FFEA */  bnezl $a0, .L7F003F2C
/* 038AF4 7F003F84 00917821 */   addu  $t7, $a0, $s1
/* 038AF8 7F003F88 8CEA0018 */  lw    $t2, 0x18($a3)
.L7F003F8C:
/* 038AFC 7F003F8C 1140001D */  beqz  $t2, .L7F004004
/* 038B00 7F003F90 00000000 */   nop
/* 038B04 7F003F94 0FC2D4F8 */  jal   get_room_data_float2
/* 038B08 7F003F98 00000000 */   nop
/* 038B0C 7F003F9C 3C108007 */  lui   $s0, %hi(g_CurrentSetup+0x18) # $s0, 0x8007
/* 038B10 7F003FA0 8E105D58 */  lw    $s0, %lo(g_CurrentSetup+0x18)($s0)
/* 038B14 7F003FA4 46000086 */  mov.s $f2, $f0
/* 038B18 7F003FA8 8E020024 */  lw    $v0, 0x24($s0)
/* 038B1C 7F003FAC 10400015 */  beqz  $v0, .L7F004004
/* 038B20 7F003FB0 00000000 */   nop
/* 038B24 7F003FB4 C6060000 */  lwc1  $f6, ($s0)
.L7F003FB8:
/* 038B28 7F003FB8 C60A0004 */  lwc1  $f10, 4($s0)
/* 038B2C 7F003FBC C6120008 */  lwc1  $f18, 8($s0)
/* 038B30 7F003FC0 46023202 */  mul.s $f8, $f6, $f2
/* 038B34 7F003FC4 02222821 */  addu  $a1, $s1, $v0
/* 038B38 7F003FC8 AE050024 */  sw    $a1, 0x24($s0)
/* 038B3C 7F003FCC 46025402 */  mul.s $f16, $f10, $f2
/* 038B40 7F003FD0 02002025 */  move  $a0, $s0
/* 038B44 7F003FD4 26060028 */  addiu $a2, $s0, 0x28
/* 038B48 7F003FD8 46029102 */  mul.s $f4, $f18, $f2
/* 038B4C 7F003FDC E6080000 */  swc1  $f8, ($s0)
/* 038B50 7F003FE0 E6100004 */  swc1  $f16, 4($s0)
/* 038B54 7F003FE4 E6040008 */  swc1  $f4, 8($s0)
/* 038B58 7F003FE8 0FC019F8 */  jal   init_pathtable_something
/* 038B5C 7F003FEC E7A2015C */   swc1  $f2, 0x15c($sp)
/* 038B60 7F003FF0 8E020050 */  lw    $v0, 0x50($s0)
/* 038B64 7F003FF4 2610002C */  addiu $s0, $s0, 0x2c
/* 038B68 7F003FF8 C7A2015C */  lwc1  $f2, 0x15c($sp)
/* 038B6C 7F003FFC 5440FFEE */  bnezl $v0, .L7F003FB8
/* 038B70 7F004000 C6060000 */   lwc1  $f6, ($s0)
.L7F004004:
/* 038B74 7F004004 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8007
/* 038B78 7F004008 24E75D40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d40
/* 038B7C 7F00400C 8CEC001C */  lw    $t4, 0x1c($a3)
/* 038B80 7F004010 51800033 */  beql  $t4, $zero, .L7F0040E0
/* 038B84 7F004014 8CE50020 */   lw    $a1, 0x20($a3)
/* 038B88 7F004018 0FC2D4F8 */  jal   get_room_data_float2
/* 038B8C 7F00401C 00000000 */   nop
/* 038B90 7F004020 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8007
/* 038B94 7F004024 24E75D40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d40
/* 038B98 7F004028 8CF0001C */  lw    $s0, 0x1c($a3)
/* 038B9C 7F00402C 46000086 */  mov.s $f2, $f0
/* 038BA0 7F004030 8E020024 */  lw    $v0, 0x24($s0)
/* 038BA4 7F004034 5040002A */  beql  $v0, $zero, .L7F0040E0
/* 038BA8 7F004038 8CE50020 */   lw    $a1, 0x20($a3)
/* 038BAC 7F00403C C6060000 */  lwc1  $f6, ($s0)
.L7F004040:
/* 038BB0 7F004040 C60A0004 */  lwc1  $f10, 4($s0)
/* 038BB4 7F004044 C6120008 */  lwc1  $f18, 8($s0)
/* 038BB8 7F004048 46023202 */  mul.s $f8, $f6, $f2
/* 038BBC 7F00404C C606002C */  lwc1  $f6, 0x2c($s0)
/* 038BC0 7F004050 02222821 */  addu  $a1, $s1, $v0
/* 038BC4 7F004054 46025402 */  mul.s $f16, $f10, $f2
/* 038BC8 7F004058 C60A0030 */  lwc1  $f10, 0x30($s0)
/* 038BCC 7F00405C AE050024 */  sw    $a1, 0x24($s0)
/* 038BD0 7F004060 46029102 */  mul.s $f4, $f18, $f2
/* 038BD4 7F004064 E6080000 */  swc1  $f8, ($s0)
/* 038BD8 7F004068 C6120034 */  lwc1  $f18, 0x34($s0)
/* 038BDC 7F00406C 46023202 */  mul.s $f8, $f6, $f2
/* 038BE0 7F004070 E6100004 */  swc1  $f16, 4($s0)
/* 038BE4 7F004074 C6060038 */  lwc1  $f6, 0x38($s0)
/* 038BE8 7F004078 46025402 */  mul.s $f16, $f10, $f2
/* 038BEC 7F00407C E6040008 */  swc1  $f4, 8($s0)
/* 038BF0 7F004080 C60A003C */  lwc1  $f10, 0x3c($s0)
/* 038BF4 7F004084 46029102 */  mul.s $f4, $f18, $f2
/* 038BF8 7F004088 E608002C */  swc1  $f8, 0x2c($s0)
/* 038BFC 7F00408C C6120040 */  lwc1  $f18, 0x40($s0)
/* 038C00 7F004090 46023202 */  mul.s $f8, $f6, $f2
/* 038C04 7F004094 E6100030 */  swc1  $f16, 0x30($s0)
/* 038C08 7F004098 02002025 */  move  $a0, $s0
/* 038C0C 7F00409C 46025402 */  mul.s $f16, $f10, $f2
/* 038C10 7F0040A0 E6040034 */  swc1  $f4, 0x34($s0)
/* 038C14 7F0040A4 26060028 */  addiu $a2, $s0, 0x28
/* 038C18 7F0040A8 46029102 */  mul.s $f4, $f18, $f2
/* 038C1C 7F0040AC E6080038 */  swc1  $f8, 0x38($s0)
/* 038C20 7F0040B0 E610003C */  swc1  $f16, 0x3c($s0)
/* 038C24 7F0040B4 E6040040 */  swc1  $f4, 0x40($s0)
/* 038C28 7F0040B8 0FC019F8 */  jal   init_pathtable_something
/* 038C2C 7F0040BC E7A20150 */   swc1  $f2, 0x150($sp)
/* 038C30 7F0040C0 8E020068 */  lw    $v0, 0x68($s0)
/* 038C34 7F0040C4 26100044 */  addiu $s0, $s0, 0x44
/* 038C38 7F0040C8 C7A20150 */  lwc1  $f2, 0x150($sp)
/* 038C3C 7F0040CC 5440FFDC */  bnezl $v0, .L7F004040
/* 038C40 7F0040D0 C6060000 */   lwc1  $f6, ($s0)
/* 038C44 7F0040D4 3C078007 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8007
/* 038C48 7F0040D8 24E75D40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x5d40
/* 038C4C 7F0040DC 8CE50020 */  lw    $a1, 0x20($a3)
.L7F0040E0:
/* 038C50 7F0040E0 50A00010 */  beql  $a1, $zero, .L7F004124
/* 038C54 7F0040E4 8CE50024 */   lw    $a1, 0x24($a3)
/* 038C58 7F0040E8 8CAE0000 */  lw    $t6, ($a1)
/* 038C5C 7F0040EC 00001025 */  move  $v0, $zero
/* 038C60 7F0040F0 00A01825 */  move  $v1, $a1
/* 038C64 7F0040F4 51C0000B */  beql  $t6, $zero, .L7F004124
/* 038C68 7F0040F8 8CE50024 */   lw    $a1, 0x24($a3)
/* 038C6C 7F0040FC 8CA40000 */  lw    $a0, ($a1)
/* 038C70 7F004100 00917821 */  addu  $t7, $a0, $s1
.L7F004104:
/* 038C74 7F004104 AC6F0000 */  sw    $t7, ($v1)
/* 038C78 7F004108 8CF80020 */  lw    $t8, 0x20($a3)
/* 038C7C 7F00410C 24420004 */  addiu $v0, $v0, 4
/* 038C80 7F004110 03021821 */  addu  $v1, $t8, $v0
/* 038C84 7F004114 8C640000 */  lw    $a0, ($v1)
/* 038C88 7F004118 5480FFFA */  bnezl $a0, .L7F004104
/* 038C8C 7F00411C 00917821 */   addu  $t7, $a0, $s1
/* 038C90 7F004120 8CE50024 */  lw    $a1, 0x24($a3)
.L7F004124:
/* 038C94 7F004124 50A0000F */  beql  $a1, $zero, .L7F004164
/* 038C98 7F004128 8FAA0278 */   lw    $t2, 0x278($sp)
/* 038C9C 7F00412C 8CA80000 */  lw    $t0, ($a1)
/* 038CA0 7F004130 00001025 */  move  $v0, $zero
/* 038CA4 7F004134 00A01825 */  move  $v1, $a1
/* 038CA8 7F004138 11000009 */  beqz  $t0, .L7F004160
/* 038CAC 7F00413C 01002025 */   move  $a0, $t0
/* 038CB0 7F004140 00914821 */  addu  $t1, $a0, $s1
.L7F004144:
/* 038CB4 7F004144 AC690000 */  sw    $t1, ($v1)
/* 038CB8 7F004148 8CF90024 */  lw    $t9, 0x24($a3)
/* 038CBC 7F00414C 24420004 */  addiu $v0, $v0, 4
/* 038CC0 7F004150 03221821 */  addu  $v1, $t9, $v0
/* 038CC4 7F004154 8C640000 */  lw    $a0, ($v1)
/* 038CC8 7F004158 5480FFFA */  bnezl $a0, .L7F004144
/* 038CCC 7F00415C 00914821 */   addu  $t1, $a0, $s1
.L7F004160:
/* 038CD0 7F004160 8FAA0278 */  lw    $t2, 0x278($sp)
.L7F004164:
/* 038CD4 7F004164 11400012 */  beqz  $t2, .L7F0041B0
/* 038CD8 7F004168 00000000 */   nop
/* 038CDC 7F00416C 0FC006D4 */  jal   load_proptype
/* 038CE0 7F004170 24040009 */   li    $a0, 9
/* 038CE4 7F004174 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 038CE8 7F004178 00402025 */   move  $a0, $v0
/* 038CEC 7F00417C 0FC006D4 */  jal   load_proptype
/* 038CF0 7F004180 24040009 */   li    $a0, 9
/* 038CF4 7F004184 0040A025 */  move  $s4, $v0
/* 038CF8 7F004188 0FC006D4 */  jal   load_proptype
/* 038CFC 7F00418C 24040008 */   li    $a0, 8
/* 038D00 7F004190 00409825 */  move  $s3, $v0
/* 038D04 7F004194 0FC006D4 */  jal   load_proptype
/* 038D08 7F004198 24040004 */   li    $a0, 4
/* 038D0C 7F00419C 02629821 */  addu  $s3, $s3, $v0
/* 038D10 7F0041A0 0FC006D4 */  jal   load_proptype
/* 038D14 7F0041A4 24040011 */   li    $a0, 17
/* 038D18 7F0041A8 10000003 */  b     .L7F0041B8
/* 038D1C 7F0041AC 02629821 */   addu  $s3, $s3, $v0
.L7F0041B0:
/* 038D20 7F0041B0 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 038D24 7F0041B4 00002025 */   move  $a0, $zero
.L7F0041B8:
/* 038D28 7F0041B8 13C0003A */  beqz  $fp, .L7F0042A4
/* 038D2C 7F0041BC 00000000 */   nop
/* 038D30 7F0041C0 0FC006D4 */  jal   load_proptype
/* 038D34 7F0041C4 24040001 */   li    $a0, 1
/* 038D38 7F0041C8 02629821 */  addu  $s3, $s3, $v0
/* 038D3C 7F0041CC 0FC006D4 */  jal   load_proptype
/* 038D40 7F0041D0 24040006 */   li    $a0, 6
/* 038D44 7F0041D4 02629821 */  addu  $s3, $s3, $v0
/* 038D48 7F0041D8 0FC006D4 */  jal   load_proptype
/* 038D4C 7F0041DC 2404000D */   li    $a0, 13
/* 038D50 7F0041E0 02629821 */  addu  $s3, $s3, $v0
/* 038D54 7F0041E4 0FC006D4 */  jal   load_proptype
/* 038D58 7F0041E8 2404000C */   li    $a0, 12
/* 038D5C 7F0041EC 02629821 */  addu  $s3, $s3, $v0
/* 038D60 7F0041F0 0FC006D4 */  jal   load_proptype
/* 038D64 7F0041F4 2404000A */   li    $a0, 10
/* 038D68 7F0041F8 02629821 */  addu  $s3, $s3, $v0
/* 038D6C 7F0041FC 0FC006D4 */  jal   load_proptype
/* 038D70 7F004200 2404000B */   li    $a0, 11
/* 038D74 7F004204 02629821 */  addu  $s3, $s3, $v0
/* 038D78 7F004208 0FC006D4 */  jal   load_proptype
/* 038D7C 7F00420C 24040015 */   li    $a0, 21
/* 038D80 7F004210 02629821 */  addu  $s3, $s3, $v0
/* 038D84 7F004214 0FC006D4 */  jal   load_proptype
/* 038D88 7F004218 24040003 */   li    $a0, 3
/* 038D8C 7F00421C 02629821 */  addu  $s3, $s3, $v0
/* 038D90 7F004220 0FC006D4 */  jal   load_proptype
/* 038D94 7F004224 2404002A */   li    $a0, 42
/* 038D98 7F004228 02629821 */  addu  $s3, $s3, $v0
/* 038D9C 7F00422C 0FC006D4 */  jal   load_proptype
/* 038DA0 7F004230 2404002F */   li    $a0, 47
/* 038DA4 7F004234 02629821 */  addu  $s3, $s3, $v0
/* 038DA8 7F004238 0FC006D4 */  jal   load_proptype
/* 038DAC 7F00423C 2404002B */   li    $a0, 43
/* 038DB0 7F004240 02629821 */  addu  $s3, $s3, $v0
/* 038DB4 7F004244 0FC006D4 */  jal   load_proptype
/* 038DB8 7F004248 24040029 */   li    $a0, 41
/* 038DBC 7F00424C 02629821 */  addu  $s3, $s3, $v0
/* 038DC0 7F004250 0FC006D4 */  jal   load_proptype
/* 038DC4 7F004254 24040024 */   li    $a0, 36
/* 038DC8 7F004258 02629821 */  addu  $s3, $s3, $v0
/* 038DCC 7F00425C 0FC006D4 */  jal   load_proptype
/* 038DD0 7F004260 24040005 */   li    $a0, 5
/* 038DD4 7F004264 02629821 */  addu  $s3, $s3, $v0
/* 038DD8 7F004268 0FC006D4 */  jal   load_proptype
/* 038DDC 7F00426C 24040007 */   li    $a0, 7
/* 038DE0 7F004270 02629821 */  addu  $s3, $s3, $v0
/* 038DE4 7F004274 0FC006D4 */  jal   load_proptype
/* 038DE8 7F004278 24040014 */   li    $a0, 20
/* 038DEC 7F00427C 02629821 */  addu  $s3, $s3, $v0
/* 038DF0 7F004280 0FC006D4 */  jal   load_proptype
/* 038DF4 7F004284 24040027 */   li    $a0, 39
/* 038DF8 7F004288 02629821 */  addu  $s3, $s3, $v0
/* 038DFC 7F00428C 0FC006D4 */  jal   load_proptype
/* 038E00 7F004290 2404002D */   li    $a0, 45
/* 038E04 7F004294 02629821 */  addu  $s3, $s3, $v0
/* 038E08 7F004298 0FC006D4 */  jal   load_proptype
/* 038E0C 7F00429C 24040028 */   li    $a0, 40
/* 038E10 7F0042A0 0282A021 */  addu  $s4, $s4, $v0
.L7F0042A4:
/* 038E14 7F0042A4 0FC01558 */  jal   sub_GAME_7F005540
/* 038E18 7F0042A8 02602025 */   move  $a0, $s3
/* 038E1C 7F0042AC 0FC01591 */  jal   sub_GAME_7F005624
/* 038E20 7F0042B0 02802025 */   move  $a0, $s4
/* 038E24 7F0042B4 0FC26C01 */  jal   getPlayerCount
/* 038E28 7F0042B8 00008025 */   move  $s0, $zero
/* 038E2C 7F0042BC 1840000C */  blez  $v0, .L7F0042F0
/* 038E30 7F0042C0 00000000 */   nop
.L7F0042C4:
/* 038E34 7F0042C4 0FC26F2B */  jal   set_cur_player
/* 038E38 7F0042C8 02002025 */   move  $a0, $s0
/* 038E3C 7F0042CC 0FC006D4 */  jal   load_proptype
/* 038E40 7F0042D0 2404000E */   li    $a0, 14
/* 038E44 7F0042D4 0FC01897 */  jal   alloc_additional_item_slots
/* 038E48 7F0042D8 00402025 */   move  $a0, $v0
/* 038E4C 7F0042DC 0FC26C01 */  jal   getPlayerCount
/* 038E50 7F0042E0 26100001 */   addiu $s0, $s0, 1
/* 038E54 7F0042E4 0202082A */  slt   $at, $s0, $v0
/* 038E58 7F0042E8 1420FFF6 */  bnez  $at, .L7F0042C4
/* 038E5C 7F0042EC 00000000 */   nop
.L7F0042F0:
/* 038E60 7F0042F0 3C0B8007 */  lui   $t3, %hi(g_CurrentSetup+0x0c) # $t3, 0x8007
/* 038E64 7F0042F4 8D6B5D4C */  lw    $t3, %lo(g_CurrentSetup+0x0c)($t3)
/* 038E68 7F0042F8 116002E3 */  beqz  $t3, .L7F004E88
/* 038E6C 7F0042FC 00000000 */   nop
/* 038E70 7F004300 0FC3021E */  jal   lvlGetSelectedDifficulty
/* 038E74 7F004304 00000000 */   nop
/* 038E78 7F004308 244C0004 */  addiu $t4, $v0, 4
/* 038E7C 7F00430C 240D0001 */  li    $t5, 1
/* 038E80 7F004310 0FC26C01 */  jal   getPlayerCount
/* 038E84 7F004314 018DA004 */   sllv  $s4, $t5, $t4
/* 038E88 7F004318 28410002 */  slti  $at, $v0, 2
/* 038E8C 7F00431C 14200007 */  bnez  $at, .L7F00433C
/* 038E90 7F004320 00000000 */   nop
/* 038E94 7F004324 0FC26C01 */  jal   getPlayerCount
/* 038E98 7F004328 00000000 */   nop
/* 038E9C 7F00432C 244E0014 */  addiu $t6, $v0, 0x14
/* 038EA0 7F004330 240F0001 */  li    $t7, 1
/* 038EA4 7F004334 01CFC004 */  sllv  $t8, $t7, $t6
/* 038EA8 7F004338 0298A025 */  or    $s4, $s4, $t8
.L7F00433C:
/* 038EAC 7F00433C 3C128007 */  lui   $s2, %hi(g_CurrentSetup+0x0c) # $s2, 0x8007
/* 038EB0 7F004340 8E525D4C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 038EB4 7F004344 24010030 */  li    $at, 48
/* 038EB8 7F004348 00009825 */  move  $s3, $zero
/* 038EBC 7F00434C 92430003 */  lbu   $v1, 3($s2)
/* 038EC0 7F004350 24170034 */  li    $s7, 52
/* 038EC4 7F004354 506101FA */  beql  $v1, $at, .L7F004B40
/* 038EC8 7F004358 92430003 */   lbu   $v1, 3($s2)
/* 038ECC 7F00435C 4480A000 */  mtc1  $zero, $f20
/* 038ED0 7F004360 3416FFFF */  li    $s6, 65535
/* 038ED4 7F004364 2468FFFF */  addiu $t0, $v1, -1
.L7F004368:
/* 038ED8 7F004368 2D01002F */  sltiu $at, $t0, 0x2f
/* 038EDC 7F00436C 102001E7 */  beqz  $at, other_obj_expand
/* 038EE0 7F004370 00084080 */   sll   $t0, $t0, 2
/* 038EE4 7F004374 3C018005 */  lui   $at, %hi(jpt_object_type_expansion)
/* 038EE8 7F004378 00280821 */  addu  $at, $at, $t0
/* 038EEC 7F00437C 8C28EF98 */  lw    $t0, %lo(jpt_object_type_expansion)($at)
/* 038EF0 7F004380 01000008 */  jr    $t0
/* 038EF4 7F004384 00000000 */   nop
actor_attr_expand:
/* 038EF8 7F004388 9250000B */  lbu   $s0, 0xb($s2)
/* 038EFC 7F00438C 0FC08CAC */  jal   chrFindByLiteralId
/* 038F00 7F004390 8E440004 */   lw    $a0, 4($s2)
/* 038F04 7F004394 104001DD */  beqz  $v0, other_obj_expand
/* 038F08 7F004398 00000000 */   nop
/* 038F0C 7F00439C 8C490018 */  lw    $t1, 0x18($v0)
/* 038F10 7F0043A0 112001DA */  beqz  $t1, other_obj_expand
/* 038F14 7F0043A4 00000000 */   nop
/* 038F18 7F0043A8 8C59001C */  lw    $t9, 0x1c($v0)
/* 038F1C 7F0043AC 132001D7 */  beqz  $t9, other_obj_expand
/* 038F20 7F0043B0 00000000 */   nop
/* 038F24 7F0043B4 100001D5 */  b     other_obj_expand
/* 038F28 7F0043B8 A0500010 */   sb    $s0, 0x10($v0)
actor_expand:
/* 038F2C 7F0043BC 8FAA0278 */  lw    $t2, 0x278($sp)
/* 038F30 7F0043C0 02A02025 */  move  $a0, $s5
/* 038F34 7F0043C4 02402825 */  move  $a1, $s2
/* 038F38 7F0043C8 114001D0 */  beqz  $t2, other_obj_expand
/* 038F3C 7F0043CC 00000000 */   nop
/* 038F40 7F0043D0 0FC08E83 */  jal   expand_09_characters
/* 038F44 7F0043D4 02603025 */   move  $a2, $s3
/* 038F48 7F0043D8 100001CC */  b     other_obj_expand
/* 038F4C 7F0043DC 00000000 */   nop
door_expand:
/* 038F50 7F0043E0 13C001CA */  beqz  $fp, other_obj_expand
/* 038F54 7F0043E4 00000000 */   nop
/* 038F58 7F0043E8 8E4B000C */  lw    $t3, 0xc($s2)
/* 038F5C 7F0043EC 02A02025 */  move  $a0, $s5
/* 038F60 7F0043F0 02402825 */  move  $a1, $s2
/* 038F64 7F0043F4 01746824 */  and   $t5, $t3, $s4
/* 038F68 7F0043F8 15A001C4 */  bnez  $t5, other_obj_expand
/* 038F6C 7F0043FC 00000000 */   nop
/* 038F70 7F004400 0FC00D20 */  jal   setupDoor
/* 038F74 7F004404 02603025 */   move  $a2, $s3
/* 038F78 7F004408 100001C0 */  b     other_obj_expand
/* 038F7C 7F00440C 00000000 */   nop
door_scale_expand:
/* 038F80 7F004410 8E4C0004 */  lw    $t4, 4($s2)
/* 038F84 7F004414 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 038F88 7F004418 44815000 */  mtc1  $at, $f10
/* 038F8C 7F00441C 448C3000 */  mtc1  $t4, $f6
/* 038F90 7F004420 3C018003 */  lui   $at, %hi(g_DoorScale) # $at, 0x8003
/* 038F94 7F004424 46803220 */  cvt.s.w $f8, $f6
/* 038F98 7F004428 460A4403 */  div.s $f16, $f8, $f10
/* 038F9C 7F00442C 100001B7 */  b     other_obj_expand
/* 038FA0 7F004430 E430A400 */   swc1  $f16, %lo(g_DoorScale)($at)
item_expand:
/* 038FA4 7F004434 8FAF0278 */  lw    $t7, 0x278($sp)
/* 038FA8 7F004438 11E001B4 */  beqz  $t7, other_obj_expand
/* 038FAC 7F00443C 00000000 */   nop
/* 038FB0 7F004440 8E4E000C */  lw    $t6, 0xc($s2)
/* 038FB4 7F004444 02A02025 */  move  $a0, $s5
/* 038FB8 7F004448 02402825 */  move  $a1, $s2
/* 038FBC 7F00444C 01D4C024 */  and   $t8, $t6, $s4
/* 038FC0 7F004450 170001AE */  bnez  $t8, other_obj_expand
/* 038FC4 7F004454 00000000 */   nop
/* 038FC8 7F004458 0FC009CE */  jal   weaponAssignToHome
/* 038FCC 7F00445C 02603025 */   move  $a2, $s3
/* 038FD0 7F004460 100001AA */  b     other_obj_expand
/* 038FD4 7F004464 00000000 */   nop
key_expand:
/* 038FD8 7F004468 8FA80278 */  lw    $t0, 0x278($sp)
/* 038FDC 7F00446C 110001A7 */  beqz  $t0, other_obj_expand
/* 038FE0 7F004470 00000000 */   nop
/* 038FE4 7F004474 8E49000C */  lw    $t1, 0xc($s2)
/* 038FE8 7F004478 02A02025 */  move  $a0, $s5
/* 038FEC 7F00447C 02402825 */  move  $a1, $s2
/* 038FF0 7F004480 0134C824 */  and   $t9, $t1, $s4
/* 038FF4 7F004484 172001A1 */  bnez  $t9, other_obj_expand
/* 038FF8 7F004488 00000000 */   nop
/* 038FFC 7F00448C 0FC00A87 */  jal   setupKey
/* 039000 7F004490 02603025 */   move  $a2, $s3
/* 039004 7F004494 1000019D */  b     other_obj_expand
/* 039008 7F004498 00000000 */   nop
hat_expand:
/* 03900C 7F00449C 8FAA0278 */  lw    $t2, 0x278($sp)
/* 039010 7F0044A0 1140019A */  beqz  $t2, other_obj_expand
/* 039014 7F0044A4 00000000 */   nop
/* 039018 7F0044A8 8E4B000C */  lw    $t3, 0xc($s2)
/* 03901C 7F0044AC 02A02025 */  move  $a0, $s5
/* 039020 7F0044B0 02402825 */  move  $a1, $s2
/* 039024 7F0044B4 01746824 */  and   $t5, $t3, $s4
/* 039028 7F0044B8 15A00194 */  bnez  $t5, other_obj_expand
/* 03902C 7F0044BC 00000000 */   nop
/* 039030 7F0044C0 0FC00A6A */  jal   setupHat
/* 039034 7F0044C4 02603025 */   move  $a2, $s3
/* 039038 7F0044C8 10000190 */  b     other_obj_expand
/* 03903C 7F0044CC 00000000 */   nop
surv_camera_expand:
/* 039040 7F0044D0 13C0018E */  beqz  $fp, other_obj_expand
/* 039044 7F0044D4 00000000 */   nop
/* 039048 7F0044D8 8E4C000C */  lw    $t4, 0xc($s2)
/* 03904C 7F0044DC 02A02025 */  move  $a0, $s5
/* 039050 7F0044E0 02402825 */  move  $a1, $s2
/* 039054 7F0044E4 01947824 */  and   $t7, $t4, $s4
/* 039058 7F0044E8 15E00188 */  bnez  $t7, other_obj_expand
/* 03905C 7F0044EC 00000000 */   nop
/* 039060 7F0044F0 0FC00A8F */  jal   setupCctv
/* 039064 7F0044F4 02603025 */   move  $a2, $s3
/* 039068 7F0044F8 10000184 */  b     other_obj_expand
/* 03906C 7F0044FC 00000000 */   nop
autogun_expand:
/* 039070 7F004500 13C00182 */  beqz  $fp, other_obj_expand
/* 039074 7F004504 00000000 */   nop
/* 039078 7F004508 8E4E000C */  lw    $t6, 0xc($s2)
/* 03907C 7F00450C 02A02025 */  move  $a0, $s5
/* 039080 7F004510 02402825 */  move  $a1, $s2
/* 039084 7F004514 01D4C024 */  and   $t8, $t6, $s4
/* 039088 7F004518 1700017C */  bnez  $t8, other_obj_expand
/* 03908C 7F00451C 00000000 */   nop
/* 039090 7F004520 0FC00B16 */  jal   setupAutogun
/* 039094 7F004524 02603025 */   move  $a2, $s3
/* 039098 7F004528 10000178 */  b     other_obj_expand
/* 03909C 7F00452C 00000000 */   nop
rack_expand:
/* 0390A0 7F004530 13C00176 */  beqz  $fp, other_obj_expand
/* 0390A4 7F004534 00000000 */   nop
/* 0390A8 7F004538 8E48000C */  lw    $t0, 0xc($s2)
/* 0390AC 7F00453C 02A02025 */  move  $a0, $s5
/* 0390B0 7F004540 02402825 */  move  $a1, $s2
/* 0390B4 7F004544 01144824 */  and   $t1, $t0, $s4
/* 0390B8 7F004548 15200170 */  bnez  $t1, other_obj_expand
/* 0390BC 7F00454C 00000000 */   nop
/* 0390C0 7F004550 0FC00B87 */  jal   setupHangingMonitors
/* 0390C4 7F004554 02603025 */   move  $a2, $s3
/* 0390C8 7F004558 1000016C */  b     other_obj_expand
/* 0390CC 7F00455C 00000000 */   nop
single_screen_display_expand:
/* 0390D0 7F004560 13C0016A */  beqz  $fp, other_obj_expand
/* 0390D4 7F004564 00000000 */   nop
/* 0390D8 7F004568 8E59000C */  lw    $t9, 0xc($s2)
/* 0390DC 7F00456C 02A02025 */  move  $a0, $s5
/* 0390E0 7F004570 02402825 */  move  $a1, $s2
/* 0390E4 7F004574 03345024 */  and   $t2, $t9, $s4
/* 0390E8 7F004578 15400164 */  bnez  $t2, other_obj_expand
/* 0390EC 7F00457C 00000000 */   nop
/* 0390F0 7F004580 0FC00B8F */  jal   setupSingleMonitor
/* 0390F4 7F004584 02603025 */   move  $a2, $s3
/* 0390F8 7F004588 10000160 */  b     other_obj_expand
/* 0390FC 7F00458C 00000000 */   nop
multi_screen_display_expand:
/* 039100 7F004590 13C0015E */  beqz  $fp, other_obj_expand
/* 039104 7F004594 00000000 */   nop
/* 039108 7F004598 8E4B000C */  lw    $t3, 0xc($s2)
/* 03910C 7F00459C 02A02025 */  move  $a0, $s5
/* 039110 7F0045A0 02402825 */  move  $a1, $s2
/* 039114 7F0045A4 01746824 */  and   $t5, $t3, $s4
/* 039118 7F0045A8 15A00158 */  bnez  $t5, other_obj_expand
/* 03911C 7F0045AC 00000000 */   nop
/* 039120 7F0045B0 0FC00C34 */  jal   setupMultiMonitor
/* 039124 7F0045B4 02603025 */   move  $a2, $s3
/* 039128 7F0045B8 10000154 */  b     other_obj_expand
/* 03912C 7F0045BC 00000000 */   nop
body_armor_expand:
/* 039130 7F0045C0 13C00152 */  beqz  $fp, other_obj_expand
/* 039134 7F0045C4 00000000 */   nop
/* 039138 7F0045C8 8E4C000C */  lw    $t4, 0xc($s2)
/* 03913C 7F0045CC 3C0E8005 */  lui   $t6, %hi(j_text_trigger) # $t6, 0x8005
/* 039140 7F0045D0 01947824 */  and   $t7, $t4, $s4
/* 039144 7F0045D4 51E00005 */  beql  $t7, $zero, .L7F0045EC
/* 039148 7F0045D8 8E580080 */   lw    $t8, 0x80($s2)
/* 03914C 7F0045DC 8DCE8500 */  lw    $t6, %lo(j_text_trigger)($t6)
/* 039150 7F0045E0 11C0014A */  beqz  $t6, other_obj_expand
/* 039154 7F0045E4 00000000 */   nop
/* 039158 7F0045E8 8E580080 */  lw    $t8, 0x80($s2)
.L7F0045EC:
/* 03915C 7F0045EC 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 039160 7F0045F0 44813000 */  mtc1  $at, $f6
/* 039164 7F0045F4 44989000 */  mtc1  $t8, $f18
/* 039168 7F0045F8 02A02025 */  move  $a0, $s5
/* 03916C 7F0045FC 02402825 */  move  $a1, $s2
/* 039170 7F004600 46809120 */  cvt.s.w $f4, $f18
/* 039174 7F004604 02603025 */  move  $a2, $s3
/* 039178 7F004608 46062203 */  div.s $f8, $f4, $f6
/* 03917C 7F00460C E6480080 */  swc1  $f8, 0x80($s2)
/* 039180 7F004610 C64A0080 */  lwc1  $f10, 0x80($s2)
/* 039184 7F004614 0FC00767 */  jal   domakedefaultobj
/* 039188 7F004618 E64A0084 */   swc1  $f10, 0x84($s2)
/* 03918C 7F00461C 1000013B */  b     other_obj_expand
/* 039190 7F004620 00000000 */   nop
type2F_tinted_glass:
/* 039194 7F004624 13C00139 */  beqz  $fp, other_obj_expand
/* 039198 7F004628 00000000 */   nop
/* 03919C 7F00462C 8E48000C */  lw    $t0, 0xc($s2)
/* 0391A0 7F004630 01144824 */  and   $t1, $t0, $s4
/* 0391A4 7F004634 15200135 */  bnez  $t1, other_obj_expand
/* 0391A8 7F004638 00000000 */   nop
/* 0391AC 7F00463C 8E590008 */  lw    $t9, 8($s2)
/* 0391B0 7F004640 001950C0 */  sll   $t2, $t9, 3
/* 0391B4 7F004644 05430039 */  bgezl $t2, .L7F00472C
/* 0391B8 7F004648 02A02025 */   move  $a0, $s5
/* 0391BC 7F00464C 86420006 */  lh    $v0, 6($s2)
/* 0391C0 7F004650 3C0D8007 */  lui   $t5, %hi(g_CurrentSetup+0x1C) # $t5, 0x8007
/* 0391C4 7F004654 27A50118 */  addiu $a1, $sp, 0x118
/* 0391C8 7F004658 28412710 */  slti  $at, $v0, 0x2710
/* 0391CC 7F00465C 14200032 */  bnez  $at, .L7F004728
/* 0391D0 7F004660 00025900 */   sll   $t3, $v0, 4
/* 0391D4 7F004664 8DAD5D5C */  lw    $t5, %lo(g_CurrentSetup+0x1C)($t5)
/* 0391D8 7F004668 01625821 */  addu  $t3, $t3, $v0
/* 0391DC 7F00466C 000B5880 */  sll   $t3, $t3, 2
/* 0391E0 7F004670 3C01FFF5 */  lui   $at, (0xFFF59FC0 >> 16) # lui $at, 0xfff5
/* 0391E4 7F004674 34219FC0 */  ori   $at, (0xFFF59FC0 & 0xFFFF) # ori $at, $at, 0x9fc0
/* 0391E8 7F004678 016D8021 */  addu  $s0, $t3, $t5
/* 0391EC 7F00467C 02018021 */  addu  $s0, $s0, $at
/* 0391F0 7F004680 0FC006F5 */  jal   sub_GAME_7F001BD4
/* 0391F4 7F004684 02002025 */   move  $a0, $s0
/* 0391F8 7F004688 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 0391FC 7F00468C 44810000 */  mtc1  $at, $f0
/* 039200 7F004690 C610000C */  lwc1  $f16, 0xc($s0)
/* 039204 7F004694 C7A40118 */  lwc1  $f4, 0x118($sp)
/* 039208 7F004698 27A40118 */  addiu $a0, $sp, 0x118
/* 03920C 7F00469C 46008482 */  mul.s $f18, $f16, $f0
/* 039210 7F0046A0 C7B0011C */  lwc1  $f16, 0x11c($sp)
/* 039214 7F0046A4 27A5010C */  addiu $a1, $sp, 0x10c
/* 039218 7F0046A8 46049180 */  add.s $f6, $f18, $f4
/* 03921C 7F0046AC E7A6010C */  swc1  $f6, 0x10c($sp)
/* 039220 7F0046B0 C6080010 */  lwc1  $f8, 0x10($s0)
/* 039224 7F0046B4 46004282 */  mul.s $f10, $f8, $f0
/* 039228 7F0046B8 46105480 */  add.s $f18, $f10, $f16
/* 03922C 7F0046BC C7AA0120 */  lwc1  $f10, 0x120($sp)
/* 039230 7F0046C0 E7B20110 */  swc1  $f18, 0x110($sp)
/* 039234 7F0046C4 C6060014 */  lwc1  $f6, 0x14($s0)
/* 039238 7F0046C8 46003202 */  mul.s $f8, $f6, $f0
/* 03923C 7F0046CC 460A4480 */  add.s $f18, $f8, $f10
/* 039240 7F0046D0 E7B20114 */  swc1  $f18, 0x114($sp)
/* 039244 7F0046D4 C606000C */  lwc1  $f6, 0xc($s0)
/* 039248 7F0046D8 46060202 */  mul.s $f8, $f0, $f6
/* 03924C 7F0046DC 46082481 */  sub.s $f18, $f4, $f8
/* 039250 7F0046E0 E7B20118 */  swc1  $f18, 0x118($sp)
/* 039254 7F0046E4 C6060010 */  lwc1  $f6, 0x10($s0)
/* 039258 7F0046E8 46060102 */  mul.s $f4, $f0, $f6
/* 03925C 7F0046EC 46048201 */  sub.s $f8, $f16, $f4
/* 039260 7F0046F0 E7A8011C */  swc1  $f8, 0x11c($sp)
/* 039264 7F0046F4 C6120014 */  lwc1  $f18, 0x14($s0)
/* 039268 7F0046F8 46120182 */  mul.s $f6, $f0, $f18
/* 03926C 7F0046FC 46065401 */  sub.s $f16, $f10, $f6
/* 039270 7F004700 0FC2EA6D */  jal   sub_GAME_7F0B9E04
/* 039274 7F004704 E7B00120 */   swc1  $f16, 0x120($sp)
/* 039278 7F004708 8E4C0090 */  lw    $t4, 0x90($s2)
/* 03927C 7F00470C 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 039280 7F004710 44819000 */  mtc1  $at, $f18
/* 039284 7F004714 448C2000 */  mtc1  $t4, $f4
/* 039288 7F004718 AE42008C */  sw    $v0, 0x8c($s2)
/* 03928C 7F00471C 46802220 */  cvt.s.w $f8, $f4
/* 039290 7F004720 46124283 */  div.s $f10, $f8, $f18
/* 039294 7F004724 E64A0090 */  swc1  $f10, 0x90($s2)
.L7F004728:
/* 039298 7F004728 02A02025 */  move  $a0, $s5
.L7F00472C:
/* 03929C 7F00472C 02402825 */  move  $a1, $s2
/* 0392A0 7F004730 0FC00767 */  jal   domakedefaultobj
/* 0392A4 7F004734 02603025 */   move  $a2, $s3
/* 0392A8 7F004738 100000F4 */  b     other_obj_expand
/* 0392AC 7F00473C 00000000 */   nop
obj_03_expand:
/* 0392B0 7F004740 13C000F2 */  beqz  $fp, other_obj_expand
/* 0392B4 7F004744 00000000 */   nop
/* 0392B8 7F004748 8E4F000C */  lw    $t7, 0xc($s2)
/* 0392BC 7F00474C 02A02025 */  move  $a0, $s5
/* 0392C0 7F004750 02402825 */  move  $a1, $s2
/* 0392C4 7F004754 01F47024 */  and   $t6, $t7, $s4
/* 0392C8 7F004758 15C000EC */  bnez  $t6, other_obj_expand
/* 0392CC 7F00475C 00000000 */   nop
/* 0392D0 7F004760 0FC00767 */  jal   domakedefaultobj
/* 0392D4 7F004764 02603025 */   move  $a2, $s3
/* 0392D8 7F004768 100000E8 */  b     other_obj_expand
/* 0392DC 7F00476C 00000000 */   nop
ammo_box_expand:
/* 0392E0 7F004770 0FC26C01 */  jal   getPlayerCount
/* 0392E4 7F004774 24100001 */   li    $s0, 1
/* 0392E8 7F004778 28410002 */  slti  $at, $v0, 2
/* 0392EC 7F00477C 1420000E */  bnez  $at, .L7F0047B8
/* 0392F0 7F004780 00000000 */   nop
/* 0392F4 7F004784 0FC31CBD */  jal   getPtrMPWeaponSetData
/* 0392F8 7F004788 00000000 */   nop
/* 0392FC 7F00478C 3C188007 */  lui   $t8, %hi(lastmpweaponnum) # $t8, 0x8007
/* 039300 7F004790 8F189580 */  lw    $t8, %lo(lastmpweaponnum)($t8)
/* 039304 7F004794 00184080 */  sll   $t0, $t8, 2
/* 039308 7F004798 01184023 */  subu  $t0, $t0, $t8
/* 03930C 7F00479C 000840C0 */  sll   $t0, $t0, 3
/* 039310 7F0047A0 00481821 */  addu  $v1, $v0, $t0
/* 039314 7F0047A4 8C64000C */  lw    $a0, 0xc($v1)
/* 039318 7F0047A8 8C700010 */  lw    $s0, 0x10($v1)
/* 03931C 7F0047AC 00044880 */  sll   $t1, $a0, 2
/* 039320 7F0047B0 0249C821 */  addu  $t9, $s2, $t1
/* 039324 7F0047B4 A730007E */  sh    $s0, 0x7e($t9)
.L7F0047B8:
/* 039328 7F0047B8 1A0000D4 */  blez  $s0, other_obj_expand
/* 03932C 7F0047BC 00000000 */   nop
/* 039330 7F0047C0 13C000D2 */  beqz  $fp, other_obj_expand
/* 039334 7F0047C4 00000000 */   nop
/* 039338 7F0047C8 8E4A000C */  lw    $t2, 0xc($s2)
/* 03933C 7F0047CC 00008825 */  move  $s1, $zero
/* 039340 7F0047D0 02408025 */  move  $s0, $s2
/* 039344 7F0047D4 01545824 */  and   $t3, $t2, $s4
/* 039348 7F0047D8 156000CC */  bnez  $t3, other_obj_expand
/* 03934C 7F0047DC 00000000 */   nop
.L7F0047E0:
/* 039350 7F0047E0 960D0082 */  lhu   $t5, 0x82($s0)
/* 039354 7F0047E4 59A00007 */  blezl $t5, .L7F004804
/* 039358 7F0047E8 26310004 */   addiu $s1, $s1, 4
/* 03935C 7F0047EC 96040080 */  lhu   $a0, 0x80($s0)
/* 039360 7F0047F0 52C40004 */  beql  $s6, $a0, .L7F004804
/* 039364 7F0047F4 26310004 */   addiu $s1, $s1, 4
/* 039368 7F0047F8 0FC15C4E */  jal   modelLoad
/* 03936C 7F0047FC 00000000 */   nop
/* 039370 7F004800 26310004 */  addiu $s1, $s1, 4
.L7F004804:
/* 039374 7F004804 1637FFF6 */  bne   $s1, $s7, .L7F0047E0
/* 039378 7F004808 26100004 */   addiu $s0, $s0, 4
/* 03937C 7F00480C 02A02025 */  move  $a0, $s5
/* 039380 7F004810 02402825 */  move  $a1, $s2
/* 039384 7F004814 0FC00767 */  jal   domakedefaultobj
/* 039388 7F004818 02603025 */   move  $a2, $s3
/* 03938C 7F00481C 100000BB */  b     other_obj_expand
/* 039390 7F004820 00000000 */   nop
type2D_tank:
/* 039394 7F004824 13C000B9 */  beqz  $fp, other_obj_expand
/* 039398 7F004828 00000000 */   nop
/* 03939C 7F00482C 8E4C000C */  lw    $t4, 0xc($s2)
/* 0393A0 7F004830 24040020 */  li    $a0, 32
/* 0393A4 7F004834 01947824 */  and   $t7, $t4, $s4
/* 0393A8 7F004838 15E000B4 */  bnez  $t7, other_obj_expand
/* 0393AC 7F00483C 00000000 */   nop
/* 0393B0 7F004840 0FC015CC */  jal   weaponLoadProjectileModels
/* 0393B4 7F004844 E7B400E8 */   swc1  $f20, 0xe8($sp)
/* 0393B8 7F004848 02A02025 */  move  $a0, $s5
/* 0393BC 7F00484C 02402825 */  move  $a1, $s2
/* 0393C0 7F004850 0FC00767 */  jal   domakedefaultobj
/* 0393C4 7F004854 02603025 */   move  $a2, $s3
/* 0393C8 7F004858 E65400C8 */  swc1  $f20, 0xc8($s2)
/* 0393CC 7F00485C E65400CC */  swc1  $f20, 0xcc($s2)
/* 0393D0 7F004860 C64C0038 */  lwc1  $f12, 0x38($s2)
/* 0393D4 7F004864 0FC16BD4 */  jal   atan2f
/* 0393D8 7F004868 C64E0040 */   lwc1  $f14, 0x40($s2)
/* 0393DC 7F00486C 3C018005 */  lui   $at, %hi(D_8004F024) # $at, 0x8005
/* 0393E0 7F004870 C426F054 */  lwc1  $f6, %lo(D_8004F024)($at)
/* 0393E4 7F004874 8E420010 */  lw    $v0, 0x10($s2)
/* 0393E8 7F004878 46003401 */  sub.s $f16, $f6, $f0
/* 0393EC 7F00487C 10400006 */  beqz  $v0, .L7F004898
/* 0393F0 7F004880 E65000DC */   swc1  $f16, 0xdc($s2)
/* 0393F4 7F004884 8C440014 */  lw    $a0, 0x14($v0)
/* 0393F8 7F004888 8C450008 */  lw    $a1, 8($v0)
/* 0393FC 7F00488C 0FC2CD48 */  jal   stanGetPositionYValue
/* 039400 7F004890 8C460010 */   lw    $a2, 0x10($v0)
/* 039404 7F004894 E7A000E8 */  swc1  $f0, 0xe8($sp)
.L7F004898:
/* 039408 7F004898 C7A200E8 */  lwc1  $f2, 0xe8($sp)
/* 03940C 7F00489C 3C018005 */  lui   $at, %hi(D_8004F028) # $at, 0x8005
/* 039410 7F0048A0 E64200D4 */  swc1  $f2, 0xd4($s2)
/* 039414 7F0048A4 C424F058 */  lwc1  $f4, %lo(D_8004F028)($at)
/* 039418 7F0048A8 46041203 */  div.s $f8, $f2, $f4
/* 03941C 7F0048AC 10000097 */  b     other_obj_expand
/* 039420 7F0048B0 E64800D0 */   swc1  $f8, 0xd0($s2)
type27_wheeled_vehicle:
/* 039424 7F0048B4 13C00095 */  beqz  $fp, other_obj_expand
/* 039428 7F0048B8 00000000 */   nop
/* 03942C 7F0048BC 8E4E000C */  lw    $t6, 0xc($s2)
/* 039430 7F0048C0 02A02025 */  move  $a0, $s5
/* 039434 7F0048C4 02402825 */  move  $a1, $s2
/* 039438 7F0048C8 01D4C024 */  and   $t8, $t6, $s4
/* 03943C 7F0048CC 1700008F */  bnez  $t8, other_obj_expand
/* 039440 7F0048D0 00000000 */   nop
/* 039444 7F0048D4 0FC00767 */  jal   domakedefaultobj
/* 039448 7F0048D8 02603025 */   move  $a2, $s3
/* 03944C 7F0048DC 8E440014 */  lw    $a0, 0x14($s2)
/* 039450 7F0048E0 5080000E */  beql  $a0, $zero, .L7F00491C
/* 039454 7F0048E4 3C01BF80 */   lui   $at, 0xbf80
/* 039458 7F0048E8 8C880008 */  lw    $t0, 8($a0)
/* 03945C 7F0048EC 8D090008 */  lw    $t1, 8($t0)
/* 039460 7F0048F0 8D250014 */  lw    $a1, 0x14($t1)
/* 039464 7F0048F4 50A00009 */  beql  $a1, $zero, .L7F00491C
/* 039468 7F0048F8 3C01BF80 */   lui   $at, 0xbf80
/* 03946C 7F0048FC 0FC1B363 */  jal   modelGetNodeRwData
/* 039470 7F004900 00000000 */   nop
/* 039474 7F004904 8E590008 */  lw    $t9, 8($s2)
/* 039478 7F004908 3C011000 */  li    $at, 0x10000000 # 0.000000
/* 03947C 7F00490C 03215024 */  and   $t2, $t9, $at
/* 039480 7F004910 2D4B0001 */  sltiu $t3, $t2, 1
/* 039484 7F004914 AC4B0000 */  sw    $t3, ($v0)
/* 039488 7F004918 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
.L7F00491C:
/* 03948C 7F00491C 44819000 */  mtc1  $at, $f18
/* 039490 7F004920 E6540088 */  swc1  $f20, 0x88($s2)
/* 039494 7F004924 E654008C */  swc1  $f20, 0x8c($s2)
/* 039498 7F004928 E6540090 */  swc1  $f20, 0x90($s2)
/* 03949C 7F00492C E6540094 */  swc1  $f20, 0x94($s2)
/* 0394A0 7F004930 E654009C */  swc1  $f20, 0x9c($s2)
/* 0394A4 7F004934 E65400A0 */  swc1  $f20, 0xa0($s2)
/* 0394A8 7F004938 8E440080 */  lw    $a0, 0x80($s2)
/* 0394AC 7F00493C 0FC0D5A6 */  jal   ailistFindById
/* 0394B0 7F004940 E6520098 */   swc1  $f18, 0x98($s2)
/* 0394B4 7F004944 240DFFFF */  li    $t5, -1
/* 0394B8 7F004948 AE420080 */  sw    $v0, 0x80($s2)
/* 0394BC 7F00494C A6400084 */  sh    $zero, 0x84($s2)
/* 0394C0 7F004950 A64D0086 */  sh    $t5, 0x86($s2)
/* 0394C4 7F004954 AE4000A4 */  sw    $zero, 0xa4($s2)
/* 0394C8 7F004958 AE4000A8 */  sw    $zero, 0xa8($s2)
/* 0394CC 7F00495C 1000006B */  b     other_obj_expand
/* 0394D0 7F004960 AE4000AC */   sw    $zero, 0xac($s2)
type28_aircraft:
/* 0394D4 7F004964 13C00069 */  beqz  $fp, other_obj_expand
/* 0394D8 7F004968 00000000 */   nop
/* 0394DC 7F00496C 8E4C000C */  lw    $t4, 0xc($s2)
/* 0394E0 7F004970 02A02025 */  move  $a0, $s5
/* 0394E4 7F004974 02402825 */  move  $a1, $s2
/* 0394E8 7F004978 01947824 */  and   $t7, $t4, $s4
/* 0394EC 7F00497C 15E00063 */  bnez  $t7, other_obj_expand
/* 0394F0 7F004980 00000000 */   nop
/* 0394F4 7F004984 0FC00767 */  jal   domakedefaultobj
/* 0394F8 7F004988 02603025 */   move  $a2, $s3
/* 0394FC 7F00498C 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
/* 039500 7F004990 44810000 */  mtc1  $at, $f0
/* 039504 7F004994 E6540098 */  swc1  $f20, 0x98($s2)
/* 039508 7F004998 E654009C */  swc1  $f20, 0x9c($s2)
/* 03950C 7F00499C E6540088 */  swc1  $f20, 0x88($s2)
/* 039510 7F0049A0 E654008C */  swc1  $f20, 0x8c($s2)
/* 039514 7F0049A4 E6540090 */  swc1  $f20, 0x90($s2)
/* 039518 7F0049A8 E65400A4 */  swc1  $f20, 0xa4($s2)
/* 03951C 7F0049AC 8E440080 */  lw    $a0, 0x80($s2)
/* 039520 7F0049B0 E64000A0 */  swc1  $f0, 0xa0($s2)
/* 039524 7F0049B4 0FC0D5A6 */  jal   ailistFindById
/* 039528 7F0049B8 E6400094 */   swc1  $f0, 0x94($s2)
/* 03952C 7F0049BC 240EFFFF */  li    $t6, -1
/* 039530 7F0049C0 AE420080 */  sw    $v0, 0x80($s2)
/* 039534 7F0049C4 A6400084 */  sh    $zero, 0x84($s2)
/* 039538 7F0049C8 A64E0086 */  sh    $t6, 0x86($s2)
/* 03953C 7F0049CC AE4000A8 */  sw    $zero, 0xa8($s2)
/* 039540 7F0049D0 AE4000AC */  sw    $zero, 0xac($s2)
/* 039544 7F0049D4 1000004D */  b     other_obj_expand
/* 039548 7F0049D8 AE4000B0 */   sw    $zero, 0xb0($s2)
tag_expand:
/* 03954C 7F0049DC 86580006 */  lh    $t8, 6($s2)
/* 039550 7F0049E0 02A02025 */  move  $a0, $s5
/* 039554 7F0049E4 0FC15CE8 */  jal   setupCommandGetObject
/* 039558 7F0049E8 03132821 */   addu  $a1, $t8, $s3
/* 03955C 7F0049EC 10400004 */  beqz  $v0, .L7F004A00
/* 039560 7F0049F0 AE42000C */   sw    $v0, 0xc($s2)
/* 039564 7F0049F4 8C480064 */  lw    $t0, 0x64($v0)
/* 039568 7F0049F8 35090010 */  ori   $t1, $t0, 0x10
/* 03956C 7F0049FC AC490064 */  sw    $t1, 0x64($v0)
.L7F004A00:
/* 039570 7F004A00 0FC013E8 */  jal   set_parent_cur_tag_entry
/* 039574 7F004A04 02402025 */   move  $a0, $s2
/* 039578 7F004A08 10000040 */  b     other_obj_expand
/* 03957C 7F004A0C 00000000 */   nop
type25_rename:
/* 039580 7F004A10 8E590004 */  lw    $t9, 4($s2)
/* 039584 7F004A14 02A02025 */  move  $a0, $s5
/* 039588 7F004A18 0FC15CE8 */  jal   setupCommandGetObject
/* 03958C 7F004A1C 03332821 */   addu  $a1, $t9, $s3
/* 039590 7F004A20 10400004 */  beqz  $v0, .L7F004A34
/* 039594 7F004A24 AE420024 */   sw    $v0, 0x24($s2)
/* 039598 7F004A28 8C4A0064 */  lw    $t2, 0x64($v0)
/* 03959C 7F004A2C 354B0400 */  ori   $t3, $t2, 0x400
/* 0395A0 7F004A30 AC4B0064 */  sw    $t3, 0x64($v0)
.L7F004A34:
/* 0395A4 7F004A34 0FC238E2 */  jal   bondinvAddTextOverride
/* 0395A8 7F004A38 02402025 */   move  $a0, $s2
/* 0395AC 7F004A3C 10000033 */  b     other_obj_expand
/* 0395B0 7F004A40 00000000 */   nop
type23_solo_brief_text:
/* 0395B4 7F004A44 0FC013EE */  jal   setup_briefing_text_entry_parent
/* 0395B8 7F004A48 02402025 */   move  $a0, $s2
/* 0395BC 7F004A4C 1000002F */  b     other_obj_expand
/* 0395C0 7F004A50 00000000 */   nop
type2E_viewport_pos:
/* 0395C4 7F004A54 8E4D0004 */  lw    $t5, 4($s2)
/* 0395C8 7F004A58 3C0142C8 */  li    $at, 0x42C80000 # 100.000000
/* 0395CC 7F004A5C 44810000 */  mtc1  $at, $f0
/* 0395D0 7F004A60 448D5000 */  mtc1  $t5, $f10
/* 0395D4 7F004A64 8E4F000C */  lw    $t7, 0xc($s2)
/* 0395D8 7F004A68 8E4C0008 */  lw    $t4, 8($s2)
/* 0395DC 7F004A6C 468051A0 */  cvt.s.w $f6, $f10
/* 0395E0 7F004A70 448F5000 */  mtc1  $t7, $f10
/* 0395E4 7F004A74 448C2000 */  mtc1  $t4, $f4
/* 0395E8 7F004A78 8E4E0010 */  lw    $t6, 0x10($s2)
/* 0395EC 7F004A7C 8E580014 */  lw    $t8, 0x14($s2)
/* 0395F0 7F004A80 46802220 */  cvt.s.w $f8, $f4
/* 0395F4 7F004A84 448E2000 */  mtc1  $t6, $f4
/* 0395F8 7F004A88 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 0395FC 7F004A8C 46003403 */  div.s $f16, $f6, $f0
/* 039600 7F004A90 468051A0 */  cvt.s.w $f6, $f10
/* 039604 7F004A94 46004483 */  div.s $f18, $f8, $f0
/* 039608 7F004A98 E6500004 */  swc1  $f16, 4($s2)
/* 03960C 7F004A9C 46802220 */  cvt.s.w $f8, $f4
/* 039610 7F004AA0 44812000 */  mtc1  $at, $f4
/* 039614 7F004AA4 46003403 */  div.s $f16, $f6, $f0
/* 039618 7F004AA8 44983000 */  mtc1  $t8, $f6
/* 03961C 7F004AAC E6520008 */  swc1  $f18, 8($s2)
/* 039620 7F004AB0 44819000 */  mtc1  $at, $f18
/* 039624 7F004AB4 00000000 */  nop
/* 039628 7F004AB8 46124283 */  div.s $f10, $f8, $f18
/* 03962C 7F004ABC E650000C */  swc1  $f16, 0xc($s2)
/* 039630 7F004AC0 46803420 */  cvt.s.w $f16, $f6
/* 039634 7F004AC4 46048203 */  div.s $f8, $f16, $f4
/* 039638 7F004AC8 E64A0010 */  swc1  $f10, 0x10($s2)
/* 03963C 7F004ACC 1000000F */  b     other_obj_expand
/* 039640 7F004AD0 E6480014 */   swc1  $f8, 0x14($s2)
type17_objective_expand:
/* 039644 7F004AD4 0FC013F4 */  jal   add_ptr_to_objective
/* 039648 7F004AD8 02402025 */   move  $a0, $s2
/* 03964C 7F004ADC 1000000B */  b     other_obj_expand
/* 039650 7F004AE0 00000000 */   nop
type20_subobjective_expand:
/* 039654 7F004AE4 0FC01403 */  jal   set_parent_cur_obj_enter_room
/* 039658 7F004AE8 02402025 */   move  $a0, $s2
/* 03965C 7F004AEC 10000007 */  b     other_obj_expand
/* 039660 7F004AF0 00000000 */   nop
type21_subobjective_expand:
/* 039664 7F004AF4 0FC01409 */  jal   set_parent_cur_obj_deposited_in_room
/* 039668 7F004AF8 02402025 */   move  $a0, $s2
/* 03966C 7F004AFC 10000003 */  b     other_obj_expand
/* 039670 7F004B00 00000000 */   nop
type1E_subobjective_expand:
/* 039674 7F004B04 0FC0140F */  jal   set_parent_cur_obj_photograph
/* 039678 7F004B08 02402025 */   move  $a0, $s2
other_obj_expand:
/* 03967C 7F004B0C 0FC15B7D */  jal   sizepropdef
/* 039680 7F004B10 02402025 */   move  $a0, $s2
/* 039684 7F004B14 00024080 */  sll   $t0, $v0, 2
/* 039688 7F004B18 01129021 */  addu  $s2, $t0, $s2
/* 03968C 7F004B1C 92430003 */  lbu   $v1, 3($s2)
/* 039690 7F004B20 24010030 */  li    $at, 48
/* 039694 7F004B24 26730001 */  addiu $s3, $s3, 1
/* 039698 7F004B28 5461FE0F */  bnel  $v1, $at, .L7F004368
/* 03969C 7F004B2C 2468FFFF */   addiu $t0, $v1, -1
/* 0396A0 7F004B30 3C128007 */  lui   $s2, %hi(g_CurrentSetup+0x0c) # $s2, 0x8007
/* 0396A4 7F004B34 8E525D4C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 0396A8 7F004B38 00009825 */  move  $s3, $zero
/* 0396AC 7F004B3C 92430003 */  lbu   $v1, 3($s2)
.L7F004B40:
/* 0396B0 7F004B40 24010030 */  li    $at, 48
/* 0396B4 7F004B44 3C170008 */  lui   $s7, 8
/* 0396B8 7F004B48 106100CF */  beq   $v1, $at, .L7F004E88
/* 0396BC 7F004B4C 24160008 */   li    $s6, 8
/* 0396C0 7F004B50 241E002B */  li    $fp, 43
/* 0396C4 7F004B54 24140001 */  li    $s4, 1
/* 0396C8 7F004B58 2469FFFD */  addiu $t1, $v1, -3
.L7F004B5C:
/* 0396CC 7F004B5C 2D21002D */  sltiu $at, $t1, 0x2d
/* 0396D0 7F004B60 102000A4 */  beqz  $at, .L7F004DF4
/* 0396D4 7F004B64 00094880 */   sll   $t1, $t1, 2
/* 0396D8 7F004B68 3C018005 */  lui   $at, %hi(jpt_8004F02C)
/* 0396DC 7F004B6C 00290821 */  addu  $at, $at, $t1
/* 0396E0 7F004B70 8C29F05C */  lw    $t1, %lo(jpt_8004F02C)($at)
/* 0396E4 7F004B74 01200008 */  jr    $t1
/* 0396E8 7F004B78 00000000 */   nop
.L7F004B7C:
/* 0396EC 7F004B7C 8E590010 */  lw    $t9, 0x10($s2)
/* 0396F0 7F004B80 1320009C */  beqz  $t9, .L7F004DF4
/* 0396F4 7F004B84 00000000 */   nop
/* 0396F8 7F004B88 8E4A0008 */  lw    $t2, 8($s2)
/* 0396FC 7F004B8C 314B8000 */  andi  $t3, $t2, 0x8000
/* 039700 7F004B90 11600098 */  beqz  $t3, .L7F004DF4
/* 039704 7F004B94 00000000 */   nop
/* 039708 7F004B98 86420006 */  lh    $v0, 6($s2)
/* 03970C 7F004B9C 02A02025 */  move  $a0, $s5
/* 039710 7F004BA0 0FC15CE8 */  jal   setupCommandGetObject
/* 039714 7F004BA4 02622821 */   addu  $a1, $s3, $v0
/* 039718 7F004BA8 10400092 */  beqz  $v0, .L7F004DF4
/* 03971C 7F004BAC 00408025 */   move  $s0, $v0
/* 039720 7F004BB0 8C4D0010 */  lw    $t5, 0x10($v0)
/* 039724 7F004BB4 11A0008F */  beqz  $t5, .L7F004DF4
/* 039728 7F004BB8 00000000 */   nop
/* 03972C 7F004BBC 8E4C0064 */  lw    $t4, 0x64($s2)
/* 039730 7F004BC0 8E440014 */  lw    $a0, 0x14($s2)
/* 039734 7F004BC4 01977825 */  or    $t7, $t4, $s7
/* 039738 7F004BC8 AE4F0064 */  sw    $t7, 0x64($s2)
/* 03973C 7F004BCC 0FC1B51A */  jal   modelSetScale
/* 039740 7F004BD0 8C850014 */   lw    $a1, 0x14($a0)
/* 039744 7F004BD4 8E440010 */  lw    $a0, 0x10($s2)
/* 039748 7F004BD8 0FC0EA29 */  jal   chrpropReparent
/* 03974C 7F004BDC 8E050010 */   lw    $a1, 0x10($s0)
/* 039750 7F004BE0 10000084 */  b     .L7F004DF4
/* 039754 7F004BE4 00000000 */   nop
.L7F004BE8:
/* 039758 7F004BE8 8E4E0004 */  lw    $t6, 4($s2)
/* 03975C 7F004BEC 0FC15BE2 */  jal   setupGetPtrToCommandByIndex
/* 039760 7F004BF0 01D32021 */   addu  $a0, $t6, $s3
/* 039764 7F004BF4 8E580008 */  lw    $t8, 8($s2)
/* 039768 7F004BF8 00408025 */  move  $s0, $v0
/* 03976C 7F004BFC 0FC15BE2 */  jal   setupGetPtrToCommandByIndex
/* 039770 7F004C00 03132021 */   addu  $a0, $t8, $s3
/* 039774 7F004C04 1200007B */  beqz  $s0, .L7F004DF4
/* 039778 7F004C08 00402825 */   move  $a1, $v0
/* 03977C 7F004C0C 10400079 */  beqz  $v0, .L7F004DF4
/* 039780 7F004C10 00000000 */   nop
/* 039784 7F004C14 92080003 */  lbu   $t0, 3($s0)
/* 039788 7F004C18 16C80076 */  bne   $s6, $t0, .L7F004DF4
/* 03978C 7F004C1C 00000000 */   nop
/* 039790 7F004C20 90490003 */  lbu   $t1, 3($v0)
/* 039794 7F004C24 16C90073 */  bne   $s6, $t1, .L7F004DF4
/* 039798 7F004C28 00000000 */   nop
/* 03979C 7F004C2C 0FC1489C */  jal   propweaponSetDual
/* 0397A0 7F004C30 02002025 */   move  $a0, $s0
/* 0397A4 7F004C34 1000006F */  b     .L7F004DF4
/* 0397A8 7F004C38 00000000 */   nop
.L7F004C3C:
/* 0397AC 7F004C3C 8E420004 */  lw    $v0, 4($s2)
/* 0397B0 7F004C40 8E510008 */  lw    $s1, 8($s2)
/* 0397B4 7F004C44 02A02025 */  move  $a0, $s5
/* 0397B8 7F004C48 0FC15CE8 */  jal   setupCommandGetObject
/* 0397BC 7F004C4C 02622821 */   addu  $a1, $s3, $v0
/* 0397C0 7F004C50 00408025 */  move  $s0, $v0
/* 0397C4 7F004C54 0FC15BE2 */  jal   setupGetPtrToCommandByIndex
/* 0397C8 7F004C58 02712021 */   addu  $a0, $s3, $s1
/* 0397CC 7F004C5C 12000065 */  beqz  $s0, .L7F004DF4
/* 0397D0 7F004C60 00000000 */   nop
/* 0397D4 7F004C64 8E030010 */  lw    $v1, 0x10($s0)
/* 0397D8 7F004C68 10600062 */  beqz  $v1, .L7F004DF4
/* 0397DC 7F004C6C 00000000 */   nop
/* 0397E0 7F004C70 10400060 */  beqz  $v0, .L7F004DF4
/* 0397E4 7F004C74 00000000 */   nop
/* 0397E8 7F004C78 90590003 */  lbu   $t9, 3($v0)
/* 0397EC 7F004C7C 1699005D */  bne   $s4, $t9, .L7F004DF4
/* 0397F0 7F004C80 00000000 */   nop
/* 0397F4 7F004C84 8C4A0010 */  lw    $t2, 0x10($v0)
/* 0397F8 7F004C88 1140005A */  beqz  $t2, .L7F004DF4
/* 0397FC 7F004C8C 00000000 */   nop
/* 039800 7F004C90 AE430004 */  sw    $v1, 4($s2)
/* 039804 7F004C94 8C4B0010 */  lw    $t3, 0x10($v0)
/* 039808 7F004C98 02402025 */  move  $a0, $s2
/* 03980C 7F004C9C 0FC00644 */  jal   initSetLevelLoadPropSwitch
/* 039810 7F004CA0 AE4B0008 */   sw    $t3, 8($s2)
/* 039814 7F004CA4 8E0D0064 */  lw    $t5, 0x64($s0)
/* 039818 7F004CA8 35AC0001 */  ori   $t4, $t5, 1
/* 03981C 7F004CAC 10000051 */  b     .L7F004DF4
/* 039820 7F004CB0 AE0C0064 */   sw    $t4, 0x64($s0)
.L7F004CB4:
/* 039824 7F004CB4 8E420004 */  lw    $v0, 4($s2)
/* 039828 7F004CB8 8E4F000C */  lw    $t7, 0xc($s2)
/* 03982C 7F004CBC 8E510008 */  lw    $s1, 8($s2)
/* 039830 7F004CC0 02A02025 */  move  $a0, $s5
/* 039834 7F004CC4 02622821 */  addu  $a1, $s3, $v0
/* 039838 7F004CC8 0FC15CE8 */  jal   setupCommandGetObject
/* 03983C 7F004CCC AFAF0080 */   sw    $t7, 0x80($sp)
/* 039840 7F004CD0 00408025 */  move  $s0, $v0
/* 039844 7F004CD4 02A02025 */  move  $a0, $s5
/* 039848 7F004CD8 0FC15CE8 */  jal   setupCommandGetObject
/* 03984C 7F004CDC 02712821 */   addu  $a1, $s3, $s1
/* 039850 7F004CE0 8FAE0080 */  lw    $t6, 0x80($sp)
/* 039854 7F004CE4 00408825 */  move  $s1, $v0
/* 039858 7F004CE8 02A02025 */  move  $a0, $s5
/* 03985C 7F004CEC 0FC15CE8 */  jal   setupCommandGetObject
/* 039860 7F004CF0 026E2821 */   addu  $a1, $s3, $t6
/* 039864 7F004CF4 1200003F */  beqz  $s0, .L7F004DF4
/* 039868 7F004CF8 00000000 */   nop
/* 03986C 7F004CFC 8E180010 */  lw    $t8, 0x10($s0)
/* 039870 7F004D00 1300003C */  beqz  $t8, .L7F004DF4
/* 039874 7F004D04 00000000 */   nop
/* 039878 7F004D08 1220003A */  beqz  $s1, .L7F004DF4
/* 03987C 7F004D0C 00000000 */   nop
/* 039880 7F004D10 8E280010 */  lw    $t0, 0x10($s1)
/* 039884 7F004D14 11000037 */  beqz  $t0, .L7F004DF4
/* 039888 7F004D18 00000000 */   nop
/* 03988C 7F004D1C 92290003 */  lbu   $t1, 3($s1)
/* 039890 7F004D20 17C90034 */  bne   $fp, $t1, .L7F004DF4
/* 039894 7F004D24 00000000 */   nop
/* 039898 7F004D28 10400032 */  beqz  $v0, .L7F004DF4
/* 03989C 7F004D2C 00000000 */   nop
/* 0398A0 7F004D30 8C590010 */  lw    $t9, 0x10($v0)
/* 0398A4 7F004D34 1320002F */  beqz  $t9, .L7F004DF4
/* 0398A8 7F004D38 00000000 */   nop
/* 0398AC 7F004D3C 904A0003 */  lbu   $t2, 3($v0)
/* 0398B0 7F004D40 02402025 */  move  $a0, $s2
/* 0398B4 7F004D44 168A002B */  bne   $s4, $t2, .L7F004DF4
/* 0398B8 7F004D48 00000000 */   nop
/* 0398BC 7F004D4C AE500004 */  sw    $s0, 4($s2)
/* 0398C0 7F004D50 AE510008 */  sw    $s1, 8($s2)
/* 0398C4 7F004D54 AE42000C */  sw    $v0, 0xc($s2)
/* 0398C8 7F004D58 0FC00650 */  jal   initSetLevelLoadPropSafeItem
/* 0398CC 7F004D5C AFA20074 */   sw    $v0, 0x74($sp)
/* 0398D0 7F004D60 8E0B000C */  lw    $t3, 0xc($s0)
/* 0398D4 7F004D64 8FA30074 */  lw    $v1, 0x74($sp)
/* 0398D8 7F004D68 356D0400 */  ori   $t5, $t3, 0x400
/* 0398DC 7F004D6C AE0D000C */  sw    $t5, 0xc($s0)
/* 0398E0 7F004D70 8C6C000C */  lw    $t4, 0xc($v1)
/* 0398E4 7F004D74 358F0400 */  ori   $t7, $t4, 0x400
/* 0398E8 7F004D78 1000001E */  b     .L7F004DF4
/* 0398EC 7F004D7C AC6F000C */   sw    $t7, 0xc($v1)
.L7F004D80:
/* 0398F0 7F004D80 8E420004 */  lw    $v0, 4($s2)
/* 0398F4 7F004D84 8E510008 */  lw    $s1, 8($s2)
/* 0398F8 7F004D88 02A02025 */  move  $a0, $s5
/* 0398FC 7F004D8C 0FC15CE8 */  jal   setupCommandGetObject
/* 039900 7F004D90 02622821 */   addu  $a1, $s3, $v0
/* 039904 7F004D94 00408025 */  move  $s0, $v0
/* 039908 7F004D98 02A02025 */  move  $a0, $s5
/* 03990C 7F004D9C 0FC15CE8 */  jal   setupCommandGetObject
/* 039910 7F004DA0 02712821 */   addu  $a1, $s3, $s1
/* 039914 7F004DA4 12000013 */  beqz  $s0, .L7F004DF4
/* 039918 7F004DA8 00000000 */   nop
/* 03991C 7F004DAC 8E0E0010 */  lw    $t6, 0x10($s0)
/* 039920 7F004DB0 11C00010 */  beqz  $t6, .L7F004DF4
/* 039924 7F004DB4 00000000 */   nop
/* 039928 7F004DB8 1040000E */  beqz  $v0, .L7F004DF4
/* 03992C 7F004DBC 00000000 */   nop
/* 039930 7F004DC0 8C580010 */  lw    $t8, 0x10($v0)
/* 039934 7F004DC4 1300000B */  beqz  $t8, .L7F004DF4
/* 039938 7F004DC8 00000000 */   nop
/* 03993C 7F004DCC 92080003 */  lbu   $t0, 3($s0)
/* 039940 7F004DD0 02402025 */  move  $a0, $s2
/* 039944 7F004DD4 16880007 */  bne   $s4, $t0, .L7F004DF4
/* 039948 7F004DD8 00000000 */   nop
/* 03994C 7F004DDC AE500004 */  sw    $s0, 4($s2)
/* 039950 7F004DE0 0FC0064A */  jal   initSetLevelLoadPropLockDoor
/* 039954 7F004DE4 AE420008 */   sw    $v0, 8($s2)
/* 039958 7F004DE8 8E090064 */  lw    $t1, 0x64($s0)
/* 03995C 7F004DEC 35392000 */  ori   $t9, $t1, 0x2000
/* 039960 7F004DF0 AE190064 */  sw    $t9, 0x64($s0)
.L7F004DF4:
/* 039964 7F004DF4 0FC15B7D */  jal   sizepropdef
/* 039968 7F004DF8 02402025 */   move  $a0, $s2
/* 03996C 7F004DFC 00025080 */  sll   $t2, $v0, 2
/* 039970 7F004E00 01529021 */  addu  $s2, $t2, $s2
/* 039974 7F004E04 92430003 */  lbu   $v1, 3($s2)
/* 039978 7F004E08 24010030 */  li    $at, 48
/* 03997C 7F004E0C 26730001 */  addiu $s3, $s3, 1
/* 039980 7F004E10 5461FF52 */  bnel  $v1, $at, .L7F004B5C
/* 039984 7F004E14 2469FFFD */   addiu $t1, $v1, -3
/* 039988 7F004E18 1000001B */  b     .L7F004E88
/* 03998C 7F004E1C 00000000 */   nop
.L7F004E20:
/* 039990 7F004E20 3C018007 */  lui   $at, %hi(g_CurrentSetup+0) # $at, 0x8007
/* 039994 7F004E24 AC205D40 */  sw    $zero, %lo(g_CurrentSetup+0)($at)
/* 039998 7F004E28 3C018007 */  lui   $at, %hi(g_CurrentSetup+4) # $at, 0x8007
/* 03999C 7F004E2C AC205D44 */  sw    $zero, %lo(g_CurrentSetup+4)($at)
/* 0399A0 7F004E30 3C018007 */  lui   $at, %hi(g_CurrentSetup+8) # $at, 0x8007
/* 0399A4 7F004E34 AC205D48 */  sw    $zero, %lo(g_CurrentSetup+8)($at)
/* 0399A8 7F004E38 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x0c) # $at, 0x8007
/* 0399AC 7F004E3C AC205D4C */  sw    $zero, %lo(g_CurrentSetup+0x0c)($at)
/* 0399B0 7F004E40 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x10) # $at, 0x8007
/* 0399B4 7F004E44 AC205D50 */  sw    $zero, %lo(g_CurrentSetup+0x10)($at)
/* 0399B8 7F004E48 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x14) # $at, 0x8007
/* 0399BC 7F004E4C AC205D54 */  sw    $zero, %lo(g_CurrentSetup+0x14)($at)
/* 0399C0 7F004E50 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x18) # $at, 0x8007
/* 0399C4 7F004E54 AC205D58 */  sw    $zero, %lo(g_CurrentSetup+0x18)($at)
/* 0399C8 7F004E58 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x1C) # $at, 0x8007
/* 0399CC 7F004E5C AC205D5C */  sw    $zero, %lo(g_CurrentSetup+0x1C)($at)
/* 0399D0 7F004E60 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x20) # $at, 0x8007
/* 0399D4 7F004E64 AC205D60 */  sw    $zero, %lo(g_CurrentSetup+0x20)($at)
/* 0399D8 7F004E68 3C018007 */  lui   $at, %hi(g_CurrentSetup+0x24) # $at, 0x8007
/* 0399DC 7F004E6C AC205D64 */  sw    $zero, %lo(g_CurrentSetup+0x24)($at)
/* 0399E0 7F004E70 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 0399E4 7F004E74 00002025 */   move  $a0, $zero
/* 0399E8 7F004E78 0FC01558 */  jal   sub_GAME_7F005540
/* 0399EC 7F004E7C 00002025 */   move  $a0, $zero
/* 0399F0 7F004E80 0FC01591 */  jal   sub_GAME_7F005624
/* 0399F4 7F004E84 00002025 */   move  $a0, $zero
.L7F004E88:
/* 0399F8 7F004E88 0FC00417 */  jal   alloc_false_GUARDdata_to_exec_global_action
/* 0399FC 7F004E8C 00000000 */   nop
/* 039A00 7F004E90 8FBF0044 */  lw    $ra, 0x44($sp)
/* 039A04 7F004E94 D7B40018 */  ldc1  $f20, 0x18($sp)
/* 039A08 7F004E98 8FB00020 */  lw    $s0, 0x20($sp)
/* 039A0C 7F004E9C 8FB10024 */  lw    $s1, 0x24($sp)
/* 039A10 7F004EA0 8FB20028 */  lw    $s2, 0x28($sp)
/* 039A14 7F004EA4 8FB3002C */  lw    $s3, 0x2c($sp)
/* 039A18 7F004EA8 8FB40030 */  lw    $s4, 0x30($sp)
/* 039A1C 7F004EAC 8FB50034 */  lw    $s5, 0x34($sp)
/* 039A20 7F004EB0 8FB60038 */  lw    $s6, 0x38($sp)
/* 039A24 7F004EB4 8FB7003C */  lw    $s7, 0x3c($sp)
/* 039A28 7F004EB8 8FBE0040 */  lw    $fp, 0x40($sp)
/* 039A2C 7F004EBC 03E00008 */  jr    $ra
/* 039A30 7F004EC0 27BD0280 */   addiu $sp, $sp, 0x280
 )
#endif

#ifdef VERSION_EU
 GLOBAL_ASM(
.late_rodata
glabel jpt_object_type_expansion
.word door_expand
.word door_scale_expand
.word obj_03_expand
.word key_expand
.word obj_03_expand
.word surv_camera_expand
.word obj_03_expand
.word item_expand
.word actor_expand
.word single_screen_display_expand
.word multi_screen_display_expand
.word rack_expand
.word autogun_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word hat_expand
.word actor_attr_expand
.word other_obj_expand
.word ammo_box_expand
.word .L7F00454C
.word tag_expand
.word type17_objective_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word other_obj_expand
.word type1E_subobjective_expand
.word other_obj_expand
.word type20_subobjective_expand
.word type21_subobjective_expand
.word other_obj_expand
.word type23_solo_brief_text
.word obj_03_expand
.word type25_rename
.word other_obj_expand
.word type27_wheeled_vehicle
.word type28_aircraft
.word obj_03_expand
.word obj_03_expand
.word obj_03_expand
.word other_obj_expand
.word type2D_tank
.word type2E_viewport_pos
.word type2F_tinted_glass

glabel D_8004F024
.word 0x40c90fdb
glabel D_8004F028
.word 0x3e4d35a8

glabel jpt_8004F02C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004BE8
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004C3C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004DF4
.word .L7F004D80
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004B7C
.word .L7F004CB4
.word .L7F004DF4
.word .L7F004DF4
.word .L7F004B7C


  .text
glabel proplvreset2
/* 03656C 7F003B7C 27BDFD80 */  addiu $sp, $sp, -0x280
/* 036570 7F003B80 AFB50034 */  sw    $s5, 0x34($sp)
/* 036574 7F003B84 0080A825 */  move  $s5, $a0
/* 036578 7F003B88 AFBF0044 */  sw    $ra, 0x44($sp)
/* 03657C 7F003B8C 3C058004 */  lui   $a1, %hi(aNochr) # $a1, 0x8004
/* 036580 7F003B90 AFBE0040 */  sw    $fp, 0x40($sp)
/* 036584 7F003B94 AFB7003C */  sw    $s7, 0x3c($sp)
/* 036588 7F003B98 AFB60038 */  sw    $s6, 0x38($sp)
/* 03658C 7F003B9C AFB40030 */  sw    $s4, 0x30($sp)
/* 036590 7F003BA0 AFB3002C */  sw    $s3, 0x2c($sp)
/* 036594 7F003BA4 AFB20028 */  sw    $s2, 0x28($sp)
/* 036598 7F003BA8 AFB10024 */  sw    $s1, 0x24($sp)
/* 03659C 7F003BAC AFB00020 */  sw    $s0, 0x20($sp)
/* 0365A0 7F003BB0 F7B40018 */  sdc1  $f20, 0x18($sp)
/* 0365A4 7F003BB4 24A571D0 */  addiu $a1, %lo(aNochr) # addiu $a1, $a1, 0x71d0
/* 0365A8 7F003BB8 0C0026C0 */  jal   tokenFind
/* 0365AC 7F003BBC 24040001 */   li    $a0, 1
/* 0365B0 7F003BC0 2C460001 */  sltiu $a2, $v0, 1
/* 0365B4 7F003BC4 10C00005 */  beqz  $a2, .L7F003BDC
/* 0365B8 7F003BC8 24040001 */   li    $a0, 1
/* 0365BC 7F003BCC 3C058004 */  lui   $a1, %hi(aNoprop) # $a1, 0x8004
/* 0365C0 7F003BD0 0C0026C0 */  jal   tokenFind
/* 0365C4 7F003BD4 24A571D8 */   addiu $a1, %lo(aNoprop) # addiu $a1, $a1, 0x71d8
/* 0365C8 7F003BD8 2C460001 */  sltiu $a2, $v0, 1
.L7F003BDC:
/* 0365CC 7F003BDC 3C058004 */  lui   $a1, %hi(aNoobj) # $a1, 0x8004
/* 0365D0 7F003BE0 AFA60278 */  sw    $a2, 0x278($sp)
/* 0365D4 7F003BE4 24A571E0 */  addiu $a1, %lo(aNoobj) # addiu $a1, $a1, 0x71e0
/* 0365D8 7F003BE8 0C0026C0 */  jal   tokenFind
/* 0365DC 7F003BEC 24040001 */   li    $a0, 1
/* 0365E0 7F003BF0 2C430001 */  sltiu $v1, $v0, 1
/* 0365E4 7F003BF4 10600005 */  beqz  $v1, .L7F003C0C
/* 0365E8 7F003BF8 24040001 */   li    $a0, 1
/* 0365EC 7F003BFC 3C058004 */  lui   $a1, %hi(aNoprop_0) # $a1, 0x8004
/* 0365F0 7F003C00 0C0026C0 */  jal   tokenFind
/* 0365F4 7F003C04 24A571E8 */   addiu $a1, %lo(aNoprop_0) # addiu $a1, $a1, 0x71e8
/* 0365F8 7F003C08 2C430001 */  sltiu $v1, $v0, 1
.L7F003C0C:
/* 0365FC 7F003C0C 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 036600 7F003C10 44812000 */  mtc1  $at, $f4
/* 036604 7F003C14 0060F025 */  move  $fp, $v1
/* 036608 7F003C18 3C018002 */  lui   $at, %hi(g_DoorScale) # $at, 0x8002
/* 03660C 7F003C1C 3C038003 */  lui   $v1, %hi(PitemZ_entries+0xFF0) # $v1, 0x8003
/* 036610 7F003C20 3C028003 */  lui   $v0, %hi(PitemZ_entries) # $v0, 0x8003
/* 036614 7F003C24 24424D88 */  addiu $v0, %lo(PitemZ_entries) # addiu $v0, $v0, 0x4d88
/* 036618 7F003C28 24635D78 */  addiu $v1, %lo(PitemZ_entries+0xFF0) # addiu $v1, $v1, 0x5d78
/* 03661C 7F003C2C E4245910 */  swc1  $f4, %lo(g_DoorScale)($at)
.L7F003C30:
/* 036620 7F003C30 8C4E0000 */  lw    $t6, ($v0)
/* 036624 7F003C34 2442000C */  addiu $v0, $v0, 0xc
/* 036628 7F003C38 0043082B */  sltu  $at, $v0, $v1
/* 03662C 7F003C3C 1420FFFC */  bnez  $at, .L7F003C30
/* 036630 7F003C40 ADC00000 */   sw    $zero, ($t6)
/* 036634 7F003C44 2AA1003B */  slti  $at, $s5, 0x3b
/* 036638 7F003C48 10200456 */  beqz  $at, .L7F004E20
/* 03663C 7F003C4C 00157880 */   sll   $t7, $s5, 2
/* 036640 7F003C50 3C188003 */  lui   $t8, %hi(setup_text_pointers) # $t8, 0x8003
/* 036644 7F003C54 27182594 */  addiu $t8, %lo(setup_text_pointers) # addiu $t8, $t8, 0x2594
/* 036648 7F003C58 01F88021 */  addu  $s0, $t7, $t8
/* 03664C 7F003C5C 8E020000 */  lw    $v0, ($s0)
/* 036650 7F003C60 3C198004 */  lui   $t9, %hi(only_read_by_stageload) # $t9, 0x8004
/* 036654 7F003C64 27B10174 */  addiu $s1, $sp, 0x174
/* 036658 7F003C68 1040044E */  beqz  $v0, .L7F004E20
/* 03665C 7F003C6C 273971F0 */   addiu $t9, %lo(only_read_by_stageload) # addiu $t9, $t9, 0x71f0
/* 036660 7F003C70 02205025 */  move  $t2, $s1
/* 036664 7F003C74 272900FC */  addiu $t1, $t9, 0xfc
.L7F003C78:
/* 036668 7F003C78 8F210000 */  lw    $at, ($t9)
/* 03666C 7F003C7C 2739000C */  addiu $t9, $t9, 0xc
/* 036670 7F003C80 254A000C */  addiu $t2, $t2, 0xc
/* 036674 7F003C84 AD41FFF4 */  sw    $at, -0xc($t2)
/* 036678 7F003C88 8F21FFF8 */  lw    $at, -8($t9)
/* 03667C 7F003C8C AD41FFF8 */  sw    $at, -8($t2)
/* 036680 7F003C90 8F21FFFC */  lw    $at, -4($t9)
/* 036684 7F003C94 1729FFF8 */  bne   $t9, $t1, .L7F003C78
/* 036688 7F003C98 AD41FFFC */   sw    $at, -4($t2)
/* 03668C 7F003C9C 8F210000 */  lw    $at, ($t9)
/* 036690 7F003CA0 0000A025 */  move  $s4, $zero
/* 036694 7F003CA4 00009825 */  move  $s3, $zero
/* 036698 7F003CA8 AD410000 */  sw    $at, ($t2)
/* 03669C 7F003CAC 904B0000 */  lbu   $t3, ($v0)
/* 0366A0 7F003CB0 A3A00175 */  sb    $zero, 0x175($sp)
/* 0366A4 7F003CB4 0FC26669 */  jal   getPlayerCount
/* 0366A8 7F003CB8 A3AB0174 */   sb    $t3, 0x174($sp)
/* 0366AC 7F003CBC 28410002 */  slti  $at, $v0, 2
/* 0366B0 7F003CC0 14200004 */  bnez  $at, .L7F003CD4
/* 0366B4 7F003CC4 3C058004 */   lui   $a1, %hi(aMp_) # $a1, 0x8004
/* 0366B8 7F003CC8 24A572F0 */  addiu $a1, %lo(aMp_) # addiu $a1, $a1, 0x72f0
/* 0366BC 7F003CCC 0C002717 */  jal   strcat
/* 0366C0 7F003CD0 02202025 */   move  $a0, $s1
.L7F003CD4:
/* 0366C4 7F003CD4 8E050000 */  lw    $a1, ($s0)
/* 0366C8 7F003CD8 02202025 */  move  $a0, $s1
/* 0366CC 7F003CDC 0C002717 */  jal   strcat
/* 0366D0 7F003CE0 24A50001 */   addiu $a1, $a1, 1
/* 0366D4 7F003CE4 02202025 */  move  $a0, $s1
/* 0366D8 7F003CE8 24050001 */  li    $a1, 1
/* 0366DC 7F003CEC 24060100 */  li    $a2, 256
/* 0366E0 7F003CF0 0FC2F053 */  jal   _fileNameLoadToBank
/* 0366E4 7F003CF4 24070004 */   li    $a3, 4
/* 0366E8 7F003CF8 3C038006 */  lui   $v1, %hi(g_ptrStageSetupFile) # $v1, 0x8006
/* 0366EC 7F003CFC 24634C68 */  addiu $v1, %lo(g_ptrStageSetupFile) # addiu $v1, $v1, 0x4c68
/* 0366F0 7F003D00 AC620000 */  sw    $v0, ($v1)
/* 0366F4 7F003D04 00408825 */  move  $s1, $v0
/* 0366F8 7F003D08 0FC302B0 */  jal   langGetLangBankIndexFromStagenum
/* 0366FC 7F003D0C 02A02025 */   move  $a0, $s5
/* 036700 7F003D10 0FC3047A */  jal   langLoadToAddr
/* 036704 7F003D14 00402025 */   move  $a0, $v0
/* 036708 7F003D18 8E2C0000 */  lw    $t4, ($s1)
/* 03670C 7F003D1C 3C078006 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8006
/* 036710 7F003D20 24E74C40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x4c40
/* 036714 7F003D24 022C6821 */  addu  $t5, $s1, $t4
/* 036718 7F003D28 ACED0000 */  sw    $t5, ($a3)
/* 03671C 7F003D2C 8E2E0004 */  lw    $t6, 4($s1)
/* 036720 7F003D30 022E7821 */  addu  $t7, $s1, $t6
/* 036724 7F003D34 ACEF0004 */  sw    $t7, 4($a3)
/* 036728 7F003D38 8E380008 */  lw    $t8, 8($s1)
/* 03672C 7F003D3C 02384021 */  addu  $t0, $s1, $t8
/* 036730 7F003D40 ACE80008 */  sw    $t0, 8($a3)
/* 036734 7F003D44 8E29000C */  lw    $t1, 0xc($s1)
/* 036738 7F003D48 0229C821 */  addu  $t9, $s1, $t1
/* 03673C 7F003D4C ACF9000C */  sw    $t9, 0xc($a3)
/* 036740 7F003D50 8E2A0010 */  lw    $t2, 0x10($s1)
/* 036744 7F003D54 022A5821 */  addu  $t3, $s1, $t2
/* 036748 7F003D58 ACEB0010 */  sw    $t3, 0x10($a3)
/* 03674C 7F003D5C 8E2C0014 */  lw    $t4, 0x14($s1)
/* 036750 7F003D60 022C6821 */  addu  $t5, $s1, $t4
/* 036754 7F003D64 ACED0014 */  sw    $t5, 0x14($a3)
/* 036758 7F003D68 8E2E0018 */  lw    $t6, 0x18($s1)
/* 03675C 7F003D6C 022E7821 */  addu  $t7, $s1, $t6
/* 036760 7F003D70 ACEF0018 */  sw    $t7, 0x18($a3)
/* 036764 7F003D74 8E38001C */  lw    $t8, 0x1c($s1)
/* 036768 7F003D78 02384021 */  addu  $t0, $s1, $t8
/* 03676C 7F003D7C ACE8001C */  sw    $t0, 0x1c($a3)
/* 036770 7F003D80 8E220020 */  lw    $v0, 0x20($s1)
/* 036774 7F003D84 10400003 */  beqz  $v0, .L7F003D94
/* 036778 7F003D88 02224821 */   addu  $t1, $s1, $v0
/* 03677C 7F003D8C 10000002 */  b     .L7F003D98
/* 036780 7F003D90 ACE90020 */   sw    $t1, 0x20($a3)
.L7F003D94:
/* 036784 7F003D94 ACE00020 */  sw    $zero, 0x20($a3)
.L7F003D98:
/* 036788 7F003D98 8E220024 */  lw    $v0, 0x24($s1)
/* 03678C 7F003D9C 10400003 */  beqz  $v0, .L7F003DAC
/* 036790 7F003DA0 0222C821 */   addu  $t9, $s1, $v0
/* 036794 7F003DA4 10000002 */  b     .L7F003DB0
/* 036798 7F003DA8 ACF90024 */   sw    $t9, 0x24($a3)
.L7F003DAC:
/* 03679C 7F003DAC ACE00024 */  sw    $zero, 0x24($a3)
.L7F003DB0:
/* 0367A0 7F003DB0 8CE50000 */  lw    $a1, ($a3)
/* 0367A4 7F003DB4 50A00010 */  beql  $a1, $zero, .L7F003DF8
/* 0367A8 7F003DB8 8CE50004 */   lw    $a1, 4($a3)
/* 0367AC 7F003DBC 8CAA0000 */  lw    $t2, ($a1)
/* 0367B0 7F003DC0 00001825 */  move  $v1, $zero
/* 0367B4 7F003DC4 00A01025 */  move  $v0, $a1
/* 0367B8 7F003DC8 0542000B */  bltzl $t2, .L7F003DF8
/* 0367BC 7F003DCC 8CE50004 */   lw    $a1, 4($a3)
/* 0367C0 7F003DD0 8C4B0004 */  lw    $t3, 4($v0)
.L7F003DD4:
/* 0367C4 7F003DD4 24630010 */  addiu $v1, $v1, 0x10
/* 0367C8 7F003DD8 01716021 */  addu  $t4, $t3, $s1
/* 0367CC 7F003DDC AC4C0004 */  sw    $t4, 4($v0)
/* 0367D0 7F003DE0 8CED0000 */  lw    $t5, ($a3)
/* 0367D4 7F003DE4 01A31021 */  addu  $v0, $t5, $v1
/* 0367D8 7F003DE8 8C4E0000 */  lw    $t6, ($v0)
/* 0367DC 7F003DEC 05C3FFF9 */  bgezl $t6, .L7F003DD4
/* 0367E0 7F003DF0 8C4B0004 */   lw    $t3, 4($v0)
/* 0367E4 7F003DF4 8CE50004 */  lw    $a1, 4($a3)
.L7F003DF8:
/* 0367E8 7F003DF8 50A00015 */  beql  $a1, $zero, .L7F003E50
/* 0367EC 7F003DFC 8CE60014 */   lw    $a2, 0x14($a3)
/* 0367F0 7F003E00 8CAF0000 */  lw    $t7, ($a1)
/* 0367F4 7F003E04 00001825 */  move  $v1, $zero
/* 0367F8 7F003E08 00A01025 */  move  $v0, $a1
/* 0367FC 7F003E0C 51E00010 */  beql  $t7, $zero, .L7F003E50
/* 036800 7F003E10 8CE60014 */   lw    $a2, 0x14($a3)
/* 036804 7F003E14 8CA40000 */  lw    $a0, ($a1)
/* 036808 7F003E18 0091C021 */  addu  $t8, $a0, $s1
.L7F003E1C:
/* 03680C 7F003E1C AC580000 */  sw    $t8, ($v0)
/* 036810 7F003E20 8CE80004 */  lw    $t0, 4($a3)
/* 036814 7F003E24 01031021 */  addu  $v0, $t0, $v1
/* 036818 7F003E28 8C490004 */  lw    $t1, 4($v0)
/* 03681C 7F003E2C 2463000C */  addiu $v1, $v1, 0xc
/* 036820 7F003E30 0131C821 */  addu  $t9, $t1, $s1
/* 036824 7F003E34 AC590004 */  sw    $t9, 4($v0)
/* 036828 7F003E38 8CEA0004 */  lw    $t2, 4($a3)
/* 03682C 7F003E3C 01431021 */  addu  $v0, $t2, $v1
/* 036830 7F003E40 8C440000 */  lw    $a0, ($v0)
/* 036834 7F003E44 5480FFF5 */  bnezl $a0, .L7F003E1C
/* 036838 7F003E48 0091C021 */   addu  $t8, $a0, $s1
/* 03683C 7F003E4C 8CE60014 */  lw    $a2, 0x14($a3)
.L7F003E50:
/* 036840 7F003E50 50C00010 */  beql  $a2, $zero, .L7F003E94
/* 036844 7F003E54 8CE20010 */   lw    $v0, 0x10($a3)
/* 036848 7F003E58 8CCB0000 */  lw    $t3, ($a2)
/* 03684C 7F003E5C 00002825 */  move  $a1, $zero
/* 036850 7F003E60 00C01025 */  move  $v0, $a2
/* 036854 7F003E64 5160000B */  beql  $t3, $zero, .L7F003E94
/* 036858 7F003E68 8CE20010 */   lw    $v0, 0x10($a3)
/* 03685C 7F003E6C 8CC30000 */  lw    $v1, ($a2)
/* 036860 7F003E70 00716021 */  addu  $t4, $v1, $s1
.L7F003E74:
/* 036864 7F003E74 AC4C0000 */  sw    $t4, ($v0)
/* 036868 7F003E78 8CED0014 */  lw    $t5, 0x14($a3)
/* 03686C 7F003E7C 24A50008 */  addiu $a1, $a1, 8
/* 036870 7F003E80 01A51021 */  addu  $v0, $t5, $a1
/* 036874 7F003E84 8C430000 */  lw    $v1, ($v0)
/* 036878 7F003E88 5460FFFA */  bnezl $v1, .L7F003E74
/* 03687C 7F003E8C 00716021 */   addu  $t4, $v1, $s1
/* 036880 7F003E90 8CE20010 */  lw    $v0, 0x10($a3)
.L7F003E94:
/* 036884 7F003E94 50400020 */  beql  $v0, $zero, .L7F003F18
/* 036888 7F003E98 8CEA0018 */   lw    $t2, 0x18($a3)
/* 03688C 7F003E9C 8C4E0000 */  lw    $t6, ($v0)
/* 036890 7F003EA0 00002825 */  move  $a1, $zero
/* 036894 7F003EA4 00403025 */  move  $a2, $v0
/* 036898 7F003EA8 51C0001B */  beql  $t6, $zero, .L7F003F18
/* 03689C 7F003EAC 8CEA0018 */   lw    $t2, 0x18($a3)
/* 0368A0 7F003EB0 8C440000 */  lw    $a0, ($v0)
/* 0368A4 7F003EB4 00917821 */  addu  $t7, $a0, $s1
.L7F003EB8:
/* 0368A8 7F003EB8 ACCF0000 */  sw    $t7, ($a2)
/* 0368AC 7F003EBC 8CF80010 */  lw    $t8, 0x10($a3)
/* 0368B0 7F003EC0 00001825 */  move  $v1, $zero
/* 0368B4 7F003EC4 03053021 */  addu  $a2, $t8, $a1
/* 0368B8 7F003EC8 8CC40000 */  lw    $a0, ($a2)
/* 0368BC 7F003ECC 8C880000 */  lw    $t0, ($a0)
/* 0368C0 7F003ED0 00801025 */  move  $v0, $a0
/* 0368C4 7F003ED4 05020009 */  bltzl $t0, .L7F003EFC
/* 0368C8 7F003ED8 A4C30006 */   sh    $v1, 6($a2)
/* 0368CC 7F003EDC 8C490004 */  lw    $t1, 4($v0)
.L7F003EE0:
/* 0368D0 7F003EE0 24630001 */  addiu $v1, $v1, 1
/* 0368D4 7F003EE4 24420004 */  addiu $v0, $v0, 4
/* 0368D8 7F003EE8 0523FFFD */  bgezl $t1, .L7F003EE0
/* 0368DC 7F003EEC 8C490004 */   lw    $t1, 4($v0)
/* 0368E0 7F003EF0 3C078006 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8006
/* 0368E4 7F003EF4 24E74C40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x4c40
/* 0368E8 7F003EF8 A4C30006 */  sh    $v1, 6($a2)
.L7F003EFC:
/* 0368EC 7F003EFC 8CF90010 */  lw    $t9, 0x10($a3)
/* 0368F0 7F003F00 24A50008 */  addiu $a1, $a1, 8
/* 0368F4 7F003F04 03253021 */  addu  $a2, $t9, $a1
/* 0368F8 7F003F08 8CC40000 */  lw    $a0, ($a2)
/* 0368FC 7F003F0C 5480FFEA */  bnezl $a0, .L7F003EB8
/* 036900 7F003F10 00917821 */   addu  $t7, $a0, $s1
/* 036904 7F003F14 8CEA0018 */  lw    $t2, 0x18($a3)
.L7F003F18:
/* 036908 7F003F18 1140001D */  beqz  $t2, .L7F003F90
/* 03690C 7F003F1C 00000000 */   nop
/* 036910 7F003F20 0FC2CEDB */  jal   get_room_data_float2
/* 036914 7F003F24 00000000 */   nop
/* 036918 7F003F28 3C108006 */  lui   $s0, %hi(g_CurrentSetup+0x18) # $s0, 0x8006
/* 03691C 7F003F2C 8E104C58 */  lw    $s0, %lo(g_CurrentSetup+0x18)($s0)
/* 036920 7F003F30 46000086 */  mov.s $f2, $f0
/* 036924 7F003F34 8E020024 */  lw    $v0, 0x24($s0)
/* 036928 7F003F38 10400015 */  beqz  $v0, .L7F003F90
/* 03692C 7F003F3C 00000000 */   nop
/* 036930 7F003F40 C6060000 */  lwc1  $f6, ($s0)
.L7F003F44:
/* 036934 7F003F44 C60A0004 */  lwc1  $f10, 4($s0)
/* 036938 7F003F48 C6120008 */  lwc1  $f18, 8($s0)
/* 03693C 7F003F4C 46023202 */  mul.s $f8, $f6, $f2
/* 036940 7F003F50 02222821 */  addu  $a1, $s1, $v0
/* 036944 7F003F54 AE050024 */  sw    $a1, 0x24($s0)
/* 036948 7F003F58 46025402 */  mul.s $f16, $f10, $f2
/* 03694C 7F003F5C 02002025 */  move  $a0, $s0
/* 036950 7F003F60 26060028 */  addiu $a2, $s0, 0x28
/* 036954 7F003F64 46029102 */  mul.s $f4, $f18, $f2
/* 036958 7F003F68 E6080000 */  swc1  $f8, ($s0)
/* 03695C 7F003F6C E6100004 */  swc1  $f16, 4($s0)
/* 036960 7F003F70 E6040008 */  swc1  $f4, 8($s0)
/* 036964 7F003F74 0FC019D0 */  jal   init_pathtable_something
/* 036968 7F003F78 E7A2015C */   swc1  $f2, 0x15c($sp)
/* 03696C 7F003F7C 8E020050 */  lw    $v0, 0x50($s0)
/* 036970 7F003F80 2610002C */  addiu $s0, $s0, 0x2c
/* 036974 7F003F84 C7A2015C */  lwc1  $f2, 0x15c($sp)
/* 036978 7F003F88 5440FFEE */  bnezl $v0, .L7F003F44
/* 03697C 7F003F8C C6060000 */   lwc1  $f6, ($s0)
.L7F003F90:
/* 036980 7F003F90 3C078006 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8006
/* 036984 7F003F94 24E74C40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x4c40
/* 036988 7F003F98 8CEC001C */  lw    $t4, 0x1c($a3)
/* 03698C 7F003F9C 51800033 */  beql  $t4, $zero, .L7F00406C
/* 036990 7F003FA0 8CE50020 */   lw    $a1, 0x20($a3)
/* 036994 7F003FA4 0FC2CEDB */  jal   get_room_data_float2
/* 036998 7F003FA8 00000000 */   nop
/* 03699C 7F003FAC 3C078006 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8006
/* 0369A0 7F003FB0 24E74C40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x4c40
/* 0369A4 7F003FB4 8CF0001C */  lw    $s0, 0x1c($a3)
/* 0369A8 7F003FB8 46000086 */  mov.s $f2, $f0
/* 0369AC 7F003FBC 8E020024 */  lw    $v0, 0x24($s0)
/* 0369B0 7F003FC0 5040002A */  beql  $v0, $zero, .L7F00406C
/* 0369B4 7F003FC4 8CE50020 */   lw    $a1, 0x20($a3)
/* 0369B8 7F003FC8 C6060000 */  lwc1  $f6, ($s0)
.L7F003FCC:
/* 0369BC 7F003FCC C60A0004 */  lwc1  $f10, 4($s0)
/* 0369C0 7F003FD0 C6120008 */  lwc1  $f18, 8($s0)
/* 0369C4 7F003FD4 46023202 */  mul.s $f8, $f6, $f2
/* 0369C8 7F003FD8 C606002C */  lwc1  $f6, 0x2c($s0)
/* 0369CC 7F003FDC 02222821 */  addu  $a1, $s1, $v0
/* 0369D0 7F003FE0 46025402 */  mul.s $f16, $f10, $f2
/* 0369D4 7F003FE4 C60A0030 */  lwc1  $f10, 0x30($s0)
/* 0369D8 7F003FE8 AE050024 */  sw    $a1, 0x24($s0)
/* 0369DC 7F003FEC 46029102 */  mul.s $f4, $f18, $f2
/* 0369E0 7F003FF0 E6080000 */  swc1  $f8, ($s0)
/* 0369E4 7F003FF4 C6120034 */  lwc1  $f18, 0x34($s0)
/* 0369E8 7F003FF8 46023202 */  mul.s $f8, $f6, $f2
/* 0369EC 7F003FFC E6100004 */  swc1  $f16, 4($s0)
/* 0369F0 7F004000 C6060038 */  lwc1  $f6, 0x38($s0)
/* 0369F4 7F004004 46025402 */  mul.s $f16, $f10, $f2
/* 0369F8 7F004008 E6040008 */  swc1  $f4, 8($s0)
/* 0369FC 7F00400C C60A003C */  lwc1  $f10, 0x3c($s0)
/* 036A00 7F004010 46029102 */  mul.s $f4, $f18, $f2
/* 036A04 7F004014 E608002C */  swc1  $f8, 0x2c($s0)
/* 036A08 7F004018 C6120040 */  lwc1  $f18, 0x40($s0)
/* 036A0C 7F00401C 46023202 */  mul.s $f8, $f6, $f2
/* 036A10 7F004020 E6100030 */  swc1  $f16, 0x30($s0)
/* 036A14 7F004024 02002025 */  move  $a0, $s0
/* 036A18 7F004028 46025402 */  mul.s $f16, $f10, $f2
/* 036A1C 7F00402C E6040034 */  swc1  $f4, 0x34($s0)
/* 036A20 7F004030 26060028 */  addiu $a2, $s0, 0x28
/* 036A24 7F004034 46029102 */  mul.s $f4, $f18, $f2
/* 036A28 7F004038 E6080038 */  swc1  $f8, 0x38($s0)
/* 036A2C 7F00403C E610003C */  swc1  $f16, 0x3c($s0)
/* 036A30 7F004040 E6040040 */  swc1  $f4, 0x40($s0)
/* 036A34 7F004044 0FC019D0 */  jal   init_pathtable_something
/* 036A38 7F004048 E7A20150 */   swc1  $f2, 0x150($sp)
/* 036A3C 7F00404C 8E020068 */  lw    $v0, 0x68($s0)
/* 036A40 7F004050 26100044 */  addiu $s0, $s0, 0x44
/* 036A44 7F004054 C7A20150 */  lwc1  $f2, 0x150($sp)
/* 036A48 7F004058 5440FFDC */  bnezl $v0, .L7F003FCC
/* 036A4C 7F00405C C6060000 */   lwc1  $f6, ($s0)
/* 036A50 7F004060 3C078006 */  lui   $a3, %hi(g_CurrentSetup+0) # $a3, 0x8006
/* 036A54 7F004064 24E74C40 */  addiu $a3, %lo(g_CurrentSetup+0) # addiu $a3, $a3, 0x4c40
/* 036A58 7F004068 8CE50020 */  lw    $a1, 0x20($a3)
.L7F00406C:
/* 036A5C 7F00406C 50A00010 */  beql  $a1, $zero, .L7F0040B0
/* 036A60 7F004070 8CE50024 */   lw    $a1, 0x24($a3)
/* 036A64 7F004074 8CAE0000 */  lw    $t6, ($a1)
/* 036A68 7F004078 00001025 */  move  $v0, $zero
/* 036A6C 7F00407C 00A01825 */  move  $v1, $a1
/* 036A70 7F004080 51C0000B */  beql  $t6, $zero, .L7F0040B0
/* 036A74 7F004084 8CE50024 */   lw    $a1, 0x24($a3)
/* 036A78 7F004088 8CA40000 */  lw    $a0, ($a1)
/* 036A7C 7F00408C 00917821 */  addu  $t7, $a0, $s1
.L7F004090:
/* 036A80 7F004090 AC6F0000 */  sw    $t7, ($v1)
/* 036A84 7F004094 8CF80020 */  lw    $t8, 0x20($a3)
/* 036A88 7F004098 24420004 */  addiu $v0, $v0, 4
/* 036A8C 7F00409C 03021821 */  addu  $v1, $t8, $v0
/* 036A90 7F0040A0 8C640000 */  lw    $a0, ($v1)
/* 036A94 7F0040A4 5480FFFA */  bnezl $a0, .L7F004090
/* 036A98 7F0040A8 00917821 */   addu  $t7, $a0, $s1
/* 036A9C 7F0040AC 8CE50024 */  lw    $a1, 0x24($a3)
.L7F0040B0:
/* 036AA0 7F0040B0 50A0000F */  beql  $a1, $zero, .L7F0040F0
/* 036AA4 7F0040B4 8FAA0278 */   lw    $t2, 0x278($sp)
/* 036AA8 7F0040B8 8CA80000 */  lw    $t0, ($a1)
/* 036AAC 7F0040BC 00001025 */  move  $v0, $zero
/* 036AB0 7F0040C0 00A01825 */  move  $v1, $a1
/* 036AB4 7F0040C4 11000009 */  beqz  $t0, .L7F0040EC
/* 036AB8 7F0040C8 01002025 */   move  $a0, $t0
/* 036ABC 7F0040CC 00914821 */  addu  $t1, $a0, $s1
.L7F0040D0:
/* 036AC0 7F0040D0 AC690000 */  sw    $t1, ($v1)
/* 036AC4 7F0040D4 8CF90024 */  lw    $t9, 0x24($a3)
/* 036AC8 7F0040D8 24420004 */  addiu $v0, $v0, 4
/* 036ACC 7F0040DC 03221821 */  addu  $v1, $t9, $v0
/* 036AD0 7F0040E0 8C640000 */  lw    $a0, ($v1)
/* 036AD4 7F0040E4 5480FFFA */  bnezl $a0, .L7F0040D0
/* 036AD8 7F0040E8 00914821 */   addu  $t1, $a0, $s1
.L7F0040EC:
/* 036ADC 7F0040EC 8FAA0278 */  lw    $t2, 0x278($sp)
.L7F0040F0:
/* 036AE0 7F0040F0 11400012 */  beqz  $t2, .L7F00413C
/* 036AE4 7F0040F4 00000000 */   nop
/* 036AE8 7F0040F8 0FC006D4 */  jal   load_proptype
/* 036AEC 7F0040FC 24040009 */   li    $a0, 9
/* 036AF0 7F004100 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 036AF4 7F004104 00402025 */   move  $a0, $v0
/* 036AF8 7F004108 0FC006D4 */  jal   load_proptype
/* 036AFC 7F00410C 24040009 */   li    $a0, 9
/* 036B00 7F004110 0040A025 */  move  $s4, $v0
/* 036B04 7F004114 0FC006D4 */  jal   load_proptype
/* 036B08 7F004118 24040008 */   li    $a0, 8
/* 036B0C 7F00411C 00409825 */  move  $s3, $v0
/* 036B10 7F004120 0FC006D4 */  jal   load_proptype
/* 036B14 7F004124 24040004 */   li    $a0, 4
/* 036B18 7F004128 02629821 */  addu  $s3, $s3, $v0
/* 036B1C 7F00412C 0FC006D4 */  jal   load_proptype
/* 036B20 7F004130 24040011 */   li    $a0, 17
/* 036B24 7F004134 10000003 */  b     .L7F004144
/* 036B28 7F004138 02629821 */   addu  $s3, $s3, $v0
.L7F00413C:
/* 036B2C 7F00413C 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 036B30 7F004140 00002025 */   move  $a0, $zero
.L7F004144:
/* 036B34 7F004144 13C0003A */  beqz  $fp, .L7F004230
/* 036B38 7F004148 00000000 */   nop
/* 036B3C 7F00414C 0FC006D4 */  jal   load_proptype
/* 036B40 7F004150 24040001 */   li    $a0, 1
/* 036B44 7F004154 02629821 */  addu  $s3, $s3, $v0
/* 036B48 7F004158 0FC006D4 */  jal   load_proptype
/* 036B4C 7F00415C 24040006 */   li    $a0, 6
/* 036B50 7F004160 02629821 */  addu  $s3, $s3, $v0
/* 036B54 7F004164 0FC006D4 */  jal   load_proptype
/* 036B58 7F004168 2404000D */   li    $a0, 13
/* 036B5C 7F00416C 02629821 */  addu  $s3, $s3, $v0
/* 036B60 7F004170 0FC006D4 */  jal   load_proptype
/* 036B64 7F004174 2404000C */   li    $a0, 12
/* 036B68 7F004178 02629821 */  addu  $s3, $s3, $v0
/* 036B6C 7F00417C 0FC006D4 */  jal   load_proptype
/* 036B70 7F004180 2404000A */   li    $a0, 10
/* 036B74 7F004184 02629821 */  addu  $s3, $s3, $v0
/* 036B78 7F004188 0FC006D4 */  jal   load_proptype
/* 036B7C 7F00418C 2404000B */   li    $a0, 11
/* 036B80 7F004190 02629821 */  addu  $s3, $s3, $v0
/* 036B84 7F004194 0FC006D4 */  jal   load_proptype
/* 036B88 7F004198 24040015 */   li    $a0, 21
/* 036B8C 7F00419C 02629821 */  addu  $s3, $s3, $v0
/* 036B90 7F0041A0 0FC006D4 */  jal   load_proptype
/* 036B94 7F0041A4 24040003 */   li    $a0, 3
/* 036B98 7F0041A8 02629821 */  addu  $s3, $s3, $v0
/* 036B9C 7F0041AC 0FC006D4 */  jal   load_proptype
/* 036BA0 7F0041B0 2404002A */   li    $a0, 42
/* 036BA4 7F0041B4 02629821 */  addu  $s3, $s3, $v0
/* 036BA8 7F0041B8 0FC006D4 */  jal   load_proptype
/* 036BAC 7F0041BC 2404002F */   li    $a0, 47
/* 036BB0 7F0041C0 02629821 */  addu  $s3, $s3, $v0
/* 036BB4 7F0041C4 0FC006D4 */  jal   load_proptype
/* 036BB8 7F0041C8 2404002B */   li    $a0, 43
/* 036BBC 7F0041CC 02629821 */  addu  $s3, $s3, $v0
/* 036BC0 7F0041D0 0FC006D4 */  jal   load_proptype
/* 036BC4 7F0041D4 24040029 */   li    $a0, 41
/* 036BC8 7F0041D8 02629821 */  addu  $s3, $s3, $v0
/* 036BCC 7F0041DC 0FC006D4 */  jal   load_proptype
/* 036BD0 7F0041E0 24040024 */   li    $a0, 36
/* 036BD4 7F0041E4 02629821 */  addu  $s3, $s3, $v0
/* 036BD8 7F0041E8 0FC006D4 */  jal   load_proptype
/* 036BDC 7F0041EC 24040005 */   li    $a0, 5
/* 036BE0 7F0041F0 02629821 */  addu  $s3, $s3, $v0
/* 036BE4 7F0041F4 0FC006D4 */  jal   load_proptype
/* 036BE8 7F0041F8 24040007 */   li    $a0, 7
/* 036BEC 7F0041FC 02629821 */  addu  $s3, $s3, $v0
/* 036BF0 7F004200 0FC006D4 */  jal   load_proptype
/* 036BF4 7F004204 24040014 */   li    $a0, 20
/* 036BF8 7F004208 02629821 */  addu  $s3, $s3, $v0
/* 036BFC 7F00420C 0FC006D4 */  jal   load_proptype
/* 036C00 7F004210 24040027 */   li    $a0, 39
/* 036C04 7F004214 02629821 */  addu  $s3, $s3, $v0
/* 036C08 7F004218 0FC006D4 */  jal   load_proptype
/* 036C0C 7F00421C 2404002D */   li    $a0, 45
/* 036C10 7F004220 02629821 */  addu  $s3, $s3, $v0
/* 036C14 7F004224 0FC006D4 */  jal   load_proptype
/* 036C18 7F004228 24040028 */   li    $a0, 40
/* 036C1C 7F00422C 0282A021 */  addu  $s4, $s4, $v0
.L7F004230:
/* 036C20 7F004230 0FC01538 */  jal   sub_GAME_7F005540
/* 036C24 7F004234 02602025 */   move  $a0, $s3
/* 036C28 7F004238 0FC01571 */  jal   sub_GAME_7F005624
/* 036C2C 7F00423C 02802025 */   move  $a0, $s4
/* 036C30 7F004240 0FC26669 */  jal   getPlayerCount
/* 036C34 7F004244 00008025 */   move  $s0, $zero
/* 036C38 7F004248 1840000C */  blez  $v0, .L7F00427C
/* 036C3C 7F00424C 00000000 */   nop
.L7F004250:
/* 036C40 7F004250 0FC26993 */  jal   set_cur_player
/* 036C44 7F004254 02002025 */   move  $a0, $s0
/* 036C48 7F004258 0FC006D4 */  jal   load_proptype
/* 036C4C 7F00425C 2404000E */   li    $a0, 14
/* 036C50 7F004260 0FC01877 */  jal   alloc_additional_item_slots
/* 036C54 7F004264 00402025 */   move  $a0, $v0
/* 036C58 7F004268 0FC26669 */  jal   getPlayerCount
/* 036C5C 7F00426C 26100001 */   addiu $s0, $s0, 1
/* 036C60 7F004270 0202082A */  slt   $at, $s0, $v0
/* 036C64 7F004274 1420FFF6 */  bnez  $at, .L7F004250
/* 036C68 7F004278 00000000 */   nop
.L7F00427C:
/* 036C6C 7F00427C 3C0B8006 */  lui   $t3, %hi(g_CurrentSetup+0x0c) # $t3, 0x8006
/* 036C70 7F004280 8D6B4C4C */  lw    $t3, %lo(g_CurrentSetup+0x0c)($t3)
/* 036C74 7F004284 116002E1 */  beqz  $t3, .L7F004E88
/* 036C78 7F004288 00000000 */   nop
/* 036C7C 7F00428C 0FC2FC21 */  jal   lvlGetSelectedDifficulty
/* 036C80 7F004290 00000000 */   nop
/* 036C84 7F004294 244C0004 */  addiu $t4, $v0, 4
/* 036C88 7F004298 240D0001 */  li    $t5, 1
/* 036C8C 7F00429C 0FC26669 */  jal   getPlayerCount
/* 036C90 7F0042A0 018DA004 */   sllv  $s4, $t5, $t4
/* 036C94 7F0042A4 28410002 */  slti  $at, $v0, 2
/* 036C98 7F0042A8 14200007 */  bnez  $at, .L7F0042C8
/* 036C9C 7F0042AC 00000000 */   nop
/* 036CA0 7F0042B0 0FC26669 */  jal   getPlayerCount
/* 036CA4 7F0042B4 00000000 */   nop
/* 036CA8 7F0042B8 244E0014 */  addiu $t6, $v0, 0x14
/* 036CAC 7F0042BC 240F0001 */  li    $t7, 1
/* 036CB0 7F0042C0 01CFC004 */  sllv  $t8, $t7, $t6
/* 036CB4 7F0042C4 0298A025 */  or    $s4, $s4, $t8
.L7F0042C8:
/* 036CB8 7F0042C8 3C128006 */  lui   $s2, %hi(g_CurrentSetup+0x0c) # $s2, 0x8006
/* 036CBC 7F0042CC 8E524C4C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 036CC0 7F0042D0 24010030 */  li    $at, 48
/* 036CC4 7F0042D4 00009825 */  move  $s3, $zero
/* 036CC8 7F0042D8 92430003 */  lbu   $v1, 3($s2)
/* 036CCC 7F0042DC 24170034 */  li    $s7, 52
/* 036CD0 7F0042E0 506101F8 */  beql  $v1, $at, .L7F004B40
/* 036CD4 7F0042E4 92430003 */   lbu   $v1, 3($s2)
/* 036CD8 7F0042E8 4480A000 */  mtc1  $zero, $f20
/* 036CDC 7F0042EC 3416FFFF */  li    $s6, 65535
/* 036CE0 7F0042F0 2468FFFF */  addiu $t0, $v1, -1
.L7F0042F4:
/* 036CE4 7F0042F4 2D01002F */  sltiu $at, $t0, 0x2f
/* 036CE8 7F0042F8 102001E5 */  beqz  $at, other_obj_expand
/* 036CEC 7F0042FC 00084080 */   sll   $t0, $t0, 2
/* 036CF0 7F004300 3C018004 */  lui   $at, %hi(jpt_object_type_expansion)
/* 036CF4 7F004304 00280821 */  addu  $at, $at, $t0
/* 036CF8 7F004308 8C2873B0 */  lw    $t0, %lo(jpt_object_type_expansion)($at)
/* 036CFC 7F00430C 01000008 */  jr    $t0
/* 036D00 7F004310 00000000 */   nop
actor_attr_expand:
/* 036D04 7F004314 9250000B */  lbu   $s0, 0xb($s2)
/* 036D08 7F004318 0FC08BE4 */  jal   chrFindByLiteralId
/* 036D0C 7F00431C 8E440004 */   lw    $a0, 4($s2)
/* 036D10 7F004320 104001DB */  beqz  $v0, other_obj_expand
/* 036D14 7F004324 00000000 */   nop
/* 036D18 7F004328 8C490018 */  lw    $t1, 0x18($v0)
/* 036D1C 7F00432C 112001D8 */  beqz  $t1, other_obj_expand
/* 036D20 7F004330 00000000 */   nop
/* 036D24 7F004334 8C59001C */  lw    $t9, 0x1c($v0)
/* 036D28 7F004338 132001D5 */  beqz  $t9, other_obj_expand
/* 036D2C 7F00433C 00000000 */   nop
/* 036D30 7F004340 100001D3 */  b     other_obj_expand
/* 036D34 7F004344 A0500010 */   sb    $s0, 0x10($v0)
actor_expand:
/* 036D38 7F004348 8FAA0278 */  lw    $t2, 0x278($sp)
/* 036D3C 7F00434C 02A02025 */  move  $a0, $s5
/* 036D40 7F004350 02402825 */  move  $a1, $s2
/* 036D44 7F004354 114001CE */  beqz  $t2, other_obj_expand
/* 036D48 7F004358 00000000 */   nop
/* 036D4C 7F00435C 0FC08DBB */  jal   expand_09_characters
/* 036D50 7F004360 02603025 */   move  $a2, $s3
/* 036D54 7F004364 100001CA */  b     other_obj_expand
/* 036D58 7F004368 00000000 */   nop
door_expand:
/* 036D5C 7F00436C 13C001C8 */  beqz  $fp, other_obj_expand
/* 036D60 7F004370 00000000 */   nop
/* 036D64 7F004374 8E4B000C */  lw    $t3, 0xc($s2)
/* 036D68 7F004378 02A02025 */  move  $a0, $s5
/* 036D6C 7F00437C 02402825 */  move  $a1, $s2
/* 036D70 7F004380 01746824 */  and   $t5, $t3, $s4
/* 036D74 7F004384 15A001C2 */  bnez  $t5, other_obj_expand
/* 036D78 7F004388 00000000 */   nop
/* 036D7C 7F00438C 0FC00CFC */  jal   setupDoor
/* 036D80 7F004390 02603025 */   move  $a2, $s3
/* 036D84 7F004394 100001BE */  b     other_obj_expand
/* 036D88 7F004398 00000000 */   nop
door_scale_expand:
/* 036D8C 7F00439C 8E4C0004 */  lw    $t4, 4($s2)
/* 036D90 7F0043A0 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 036D94 7F0043A4 44815000 */  mtc1  $at, $f10
/* 036D98 7F0043A8 448C3000 */  mtc1  $t4, $f6
/* 036D9C 7F0043AC 3C018002 */  lui   $at, %hi(g_DoorScale) # $at, 0x8002
/* 036DA0 7F0043B0 46803220 */  cvt.s.w $f8, $f6
/* 036DA4 7F0043B4 460A4403 */  div.s $f16, $f8, $f10
/* 036DA8 7F0043B8 100001B5 */  b     other_obj_expand
/* 036DAC 7F0043BC E4305910 */   swc1  $f16, %lo(g_DoorScale)($at)
item_expand:
/* 036DB0 7F0043C0 8FAF0278 */  lw    $t7, 0x278($sp)
/* 036DB4 7F0043C4 11E001B2 */  beqz  $t7, other_obj_expand
/* 036DB8 7F0043C8 00000000 */   nop
/* 036DBC 7F0043CC 8E4E000C */  lw    $t6, 0xc($s2)
/* 036DC0 7F0043D0 02A02025 */  move  $a0, $s5
/* 036DC4 7F0043D4 02402825 */  move  $a1, $s2
/* 036DC8 7F0043D8 01D4C024 */  and   $t8, $t6, $s4
/* 036DCC 7F0043DC 170001AC */  bnez  $t8, other_obj_expand
/* 036DD0 7F0043E0 00000000 */   nop
/* 036DD4 7F0043E4 0FC009CE */  jal   weaponAssignToHome
/* 036DD8 7F0043E8 02603025 */   move  $a2, $s3
/* 036DDC 7F0043EC 100001A8 */  b     other_obj_expand
/* 036DE0 7F0043F0 00000000 */   nop
key_expand:
/* 036DE4 7F0043F4 8FA80278 */  lw    $t0, 0x278($sp)
/* 036DE8 7F0043F8 110001A5 */  beqz  $t0, other_obj_expand
/* 036DEC 7F0043FC 00000000 */   nop
/* 036DF0 7F004400 8E49000C */  lw    $t1, 0xc($s2)
/* 036DF4 7F004404 02A02025 */  move  $a0, $s5
/* 036DF8 7F004408 02402825 */  move  $a1, $s2
/* 036DFC 7F00440C 0134C824 */  and   $t9, $t1, $s4
/* 036E00 7F004410 1720019F */  bnez  $t9, other_obj_expand
/* 036E04 7F004414 00000000 */   nop
/* 036E08 7F004418 0FC00A61 */  jal   setupKey
/* 036E0C 7F00441C 02603025 */   move  $a2, $s3
/* 036E10 7F004420 1000019B */  b     other_obj_expand
/* 036E14 7F004424 00000000 */   nop
hat_expand:
/* 036E18 7F004428 8FAA0278 */  lw    $t2, 0x278($sp)
/* 036E1C 7F00442C 11400198 */  beqz  $t2, other_obj_expand
/* 036E20 7F004430 00000000 */   nop
/* 036E24 7F004434 8E4B000C */  lw    $t3, 0xc($s2)
/* 036E28 7F004438 02A02025 */  move  $a0, $s5
/* 036E2C 7F00443C 02402825 */  move  $a1, $s2
/* 036E30 7F004440 01746824 */  and   $t5, $t3, $s4
/* 036E34 7F004444 15A00192 */  bnez  $t5, other_obj_expand
/* 036E38 7F004448 00000000 */   nop
/* 036E3C 7F00444C 0FC00A44 */  jal   setupHat
/* 036E40 7F004450 02603025 */   move  $a2, $s3
/* 036E44 7F004454 1000018E */  b     other_obj_expand
/* 036E48 7F004458 00000000 */   nop
surv_camera_expand:
/* 036E4C 7F00445C 13C0018C */  beqz  $fp, other_obj_expand
/* 036E50 7F004460 00000000 */   nop
/* 036E54 7F004464 8E4C000C */  lw    $t4, 0xc($s2)
/* 036E58 7F004468 02A02025 */  move  $a0, $s5
/* 036E5C 7F00446C 02402825 */  move  $a1, $s2
/* 036E60 7F004470 01947824 */  and   $t7, $t4, $s4
/* 036E64 7F004474 15E00186 */  bnez  $t7, other_obj_expand
/* 036E68 7F004478 00000000 */   nop
/* 036E6C 7F00447C 0FC00A69 */  jal   setupCctv
/* 036E70 7F004480 02603025 */   move  $a2, $s3
/* 036E74 7F004484 10000182 */  b     other_obj_expand
/* 036E78 7F004488 00000000 */   nop
autogun_expand:
/* 036E7C 7F00448C 13C00180 */  beqz  $fp, other_obj_expand
/* 036E80 7F004490 00000000 */   nop
/* 036E84 7F004494 8E4E000C */  lw    $t6, 0xc($s2)
/* 036E88 7F004498 02A02025 */  move  $a0, $s5
/* 036E8C 7F00449C 02402825 */  move  $a1, $s2
/* 036E90 7F0044A0 01D4C024 */  and   $t8, $t6, $s4
/* 036E94 7F0044A4 1700017A */  bnez  $t8, other_obj_expand
/* 036E98 7F0044A8 00000000 */   nop
/* 036E9C 7F0044AC 0FC00AF0 */  jal   setupAutogun
/* 036EA0 7F0044B0 02603025 */   move  $a2, $s3
/* 036EA4 7F0044B4 10000176 */  b     other_obj_expand
/* 036EA8 7F0044B8 00000000 */   nop
rack_expand:
/* 036EAC 7F0044BC 13C00174 */  beqz  $fp, other_obj_expand
/* 036EB0 7F0044C0 00000000 */   nop
/* 036EB4 7F0044C4 8E48000C */  lw    $t0, 0xc($s2)
/* 036EB8 7F0044C8 02A02025 */  move  $a0, $s5
/* 036EBC 7F0044CC 02402825 */  move  $a1, $s2
/* 036EC0 7F0044D0 01144824 */  and   $t1, $t0, $s4
/* 036EC4 7F0044D4 1520016E */  bnez  $t1, other_obj_expand
/* 036EC8 7F0044D8 00000000 */   nop
/* 036ECC 7F0044DC 0FC00B63 */  jal   setupHangingMonitors
/* 036ED0 7F0044E0 02603025 */   move  $a2, $s3
/* 036ED4 7F0044E4 1000016A */  b     other_obj_expand
/* 036ED8 7F0044E8 00000000 */   nop
single_screen_display_expand:
/* 036EDC 7F0044EC 13C00168 */  beqz  $fp, other_obj_expand
/* 036EE0 7F0044F0 00000000 */   nop
/* 036EE4 7F0044F4 8E59000C */  lw    $t9, 0xc($s2)
/* 036EE8 7F0044F8 02A02025 */  move  $a0, $s5
/* 036EEC 7F0044FC 02402825 */  move  $a1, $s2
/* 036EF0 7F004500 03345024 */  and   $t2, $t9, $s4
/* 036EF4 7F004504 15400162 */  bnez  $t2, other_obj_expand
/* 036EF8 7F004508 00000000 */   nop
/* 036EFC 7F00450C 0FC00B6B */  jal   setupSingleMonitor
/* 036F00 7F004510 02603025 */   move  $a2, $s3
/* 036F04 7F004514 1000015E */  b     other_obj_expand
/* 036F08 7F004518 00000000 */   nop
multi_screen_display_expand:
/* 036F0C 7F00451C 13C0015C */  beqz  $fp, other_obj_expand
/* 036F10 7F004520 00000000 */   nop
/* 036F14 7F004524 8E4B000C */  lw    $t3, 0xc($s2)
/* 036F18 7F004528 02A02025 */  move  $a0, $s5
/* 036F1C 7F00452C 02402825 */  move  $a1, $s2
/* 036F20 7F004530 01746824 */  and   $t5, $t3, $s4
/* 036F24 7F004534 15A00156 */  bnez  $t5, other_obj_expand
/* 036F28 7F004538 00000000 */   nop
/* 036F2C 7F00453C 0FC00C10 */  jal   setupMultiMonitor
/* 036F30 7F004540 02603025 */   move  $a2, $s3
/* 036F34 7F004544 10000152 */  b     other_obj_expand
/* 036F38 7F004548 00000000 */   nop
.L7F00454C:
/* 036F3C 7F00454C 13C00150 */  beqz  $fp, other_obj_expand
/* 036F40 7F004550 00000000 */   nop
body_armor_expand:
/* 036F44 7F004554 8E4C000C */  lw    $t4, 0xc($s2)
/* 036F48 7F004558 3C0E8004 */  lui   $t6, %hi(j_text_trigger) # $t6, 0x8004
/* 036F4C 7F00455C 01947824 */  and   $t7, $t4, $s4
/* 036F50 7F004560 51E00005 */  beql  $t7, $zero, .L7F0045EC
/* 036F54 7F004564 8E580080 */   lw    $t8, 0x80($s2)
/* 036F58 7F004568 8DCE1150 */  lw    $t6, %lo(j_text_trigger)($t6)
/* 036F5C 7F00456C 11C00148 */  beqz  $t6, other_obj_expand
/* 036F60 7F004570 00000000 */   nop
/* 036F64 7F004574 8E580080 */  lw    $t8, 0x80($s2)
.L7F0045EC:
/* 036F68 7F004578 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 036F6C 7F00457C 44813000 */  mtc1  $at, $f6
/* 036F70 7F004580 44989000 */  mtc1  $t8, $f18
/* 036F74 7F004584 02A02025 */  move  $a0, $s5
/* 036F78 7F004588 02402825 */  move  $a1, $s2
/* 036F7C 7F00458C 46809120 */  cvt.s.w $f4, $f18
/* 036F80 7F004590 02603025 */  move  $a2, $s3
/* 036F84 7F004594 46062203 */  div.s $f8, $f4, $f6
/* 036F88 7F004598 E6480080 */  swc1  $f8, 0x80($s2)
/* 036F8C 7F00459C C64A0080 */  lwc1  $f10, 0x80($s2)
/* 036F90 7F0045A0 0FC00767 */  jal   domakedefaultobj
/* 036F94 7F0045A4 E64A0084 */   swc1  $f10, 0x84($s2)
/* 036F98 7F0045A8 10000139 */  b     other_obj_expand
/* 036F9C 7F0045AC 00000000 */   nop
type2F_tinted_glass:
/* 036FA0 7F0045B0 13C00137 */  beqz  $fp, other_obj_expand
/* 036FA4 7F0045B4 00000000 */   nop
/* 036FA8 7F0045B8 8E48000C */  lw    $t0, 0xc($s2)
/* 036FAC 7F0045BC 01144824 */  and   $t1, $t0, $s4
/* 036FB0 7F0045C0 15200133 */  bnez  $t1, other_obj_expand
/* 036FB4 7F0045C4 00000000 */   nop
/* 036FB8 7F0045C8 8E590008 */  lw    $t9, 8($s2)
/* 036FBC 7F0045CC 001950C0 */  sll   $t2, $t9, 3
/* 036FC0 7F0045D0 05430039 */  bgezl $t2, .L7F0046B8
/* 036FC4 7F0045D4 02A02025 */   move  $a0, $s5
/* 036FC8 7F0045D8 86420006 */  lh    $v0, 6($s2)
/* 036FCC 7F0045DC 3C0D8006 */  lui   $t5, %hi(g_CurrentSetup+0x1C) # $t5, 0x8006
/* 036FD0 7F0045E0 27A50118 */  addiu $a1, $sp, 0x118
/* 036FD4 7F0045E4 28412710 */  slti  $at, $v0, 0x2710
/* 036FD8 7F0045E8 14200032 */  bnez  $at, .L7F0046B4
/* 036FDC 7F0045EC 00025900 */   sll   $t3, $v0, 4
/* 036FE0 7F0045F0 8DAD4C5C */  lw    $t5, %lo(g_CurrentSetup+0x1C)($t5)
/* 036FE4 7F0045F4 01625821 */  addu  $t3, $t3, $v0
/* 036FE8 7F0045F8 000B5880 */  sll   $t3, $t3, 2
/* 036FEC 7F0045FC 3C01FFF5 */  lui   $at, (0xFFF59FC0 >> 16) # lui $at, 0xfff5
/* 036FF0 7F004600 34219FC0 */  ori   $at, (0xFFF59FC0 & 0xFFFF) # ori $at, $at, 0x9fc0
/* 036FF4 7F004604 016D8021 */  addu  $s0, $t3, $t5
/* 036FF8 7F004608 02018021 */  addu  $s0, $s0, $at
/* 036FFC 7F00460C 0FC006F5 */  jal   sub_GAME_7F001BD4
/* 037000 7F004610 02002025 */   move  $a0, $s0
/* 037004 7F004614 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 037008 7F004618 44810000 */  mtc1  $at, $f0
/* 03700C 7F00461C C610000C */  lwc1  $f16, 0xc($s0)
/* 037010 7F004620 C7A40118 */  lwc1  $f4, 0x118($sp)
/* 037014 7F004624 27A40118 */  addiu $a0, $sp, 0x118
/* 037018 7F004628 46008482 */  mul.s $f18, $f16, $f0
/* 03701C 7F00462C C7B0011C */  lwc1  $f16, 0x11c($sp)
/* 037020 7F004630 27A5010C */  addiu $a1, $sp, 0x10c
/* 037024 7F004634 46049180 */  add.s $f6, $f18, $f4
/* 037028 7F004638 E7A6010C */  swc1  $f6, 0x10c($sp)
/* 03702C 7F00463C C6080010 */  lwc1  $f8, 0x10($s0)
/* 037030 7F004640 46004282 */  mul.s $f10, $f8, $f0
/* 037034 7F004644 46105480 */  add.s $f18, $f10, $f16
/* 037038 7F004648 C7AA0120 */  lwc1  $f10, 0x120($sp)
/* 03703C 7F00464C E7B20110 */  swc1  $f18, 0x110($sp)
/* 037040 7F004650 C6060014 */  lwc1  $f6, 0x14($s0)
/* 037044 7F004654 46003202 */  mul.s $f8, $f6, $f0
/* 037048 7F004658 460A4480 */  add.s $f18, $f8, $f10
/* 03704C 7F00465C E7B20114 */  swc1  $f18, 0x114($sp)
/* 037050 7F004660 C606000C */  lwc1  $f6, 0xc($s0)
/* 037054 7F004664 46060202 */  mul.s $f8, $f0, $f6
/* 037058 7F004668 46082481 */  sub.s $f18, $f4, $f8
/* 03705C 7F00466C E7B20118 */  swc1  $f18, 0x118($sp)
/* 037060 7F004670 C6060010 */  lwc1  $f6, 0x10($s0)
/* 037064 7F004674 46060102 */  mul.s $f4, $f0, $f6
/* 037068 7F004678 46048201 */  sub.s $f8, $f16, $f4
/* 03706C 7F00467C E7A8011C */  swc1  $f8, 0x11c($sp)
/* 037070 7F004680 C6120014 */  lwc1  $f18, 0x14($s0)
/* 037074 7F004684 46120182 */  mul.s $f6, $f0, $f18
/* 037078 7F004688 46065401 */  sub.s $f16, $f10, $f6
/* 03707C 7F00468C 0FC2E428 */  jal   sub_GAME_7F0B9E04
/* 037080 7F004690 E7B00120 */   swc1  $f16, 0x120($sp)
/* 037084 7F004694 8E4C0090 */  lw    $t4, 0x90($s2)
/* 037088 7F004698 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 03708C 7F00469C 44819000 */  mtc1  $at, $f18
/* 037090 7F0046A0 448C2000 */  mtc1  $t4, $f4
/* 037094 7F0046A4 AE42008C */  sw    $v0, 0x8c($s2)
/* 037098 7F0046A8 46802220 */  cvt.s.w $f8, $f4
/* 03709C 7F0046AC 46124283 */  div.s $f10, $f8, $f18
/* 0370A0 7F0046B0 E64A0090 */  swc1  $f10, 0x90($s2)
.L7F0046B4:
/* 0370A4 7F0046B4 02A02025 */  move  $a0, $s5
.L7F0046B8:
/* 0370A8 7F0046B8 02402825 */  move  $a1, $s2
/* 0370AC 7F0046BC 0FC00767 */  jal   domakedefaultobj
/* 0370B0 7F0046C0 02603025 */   move  $a2, $s3
/* 0370B4 7F0046C4 100000F2 */  b     other_obj_expand
/* 0370B8 7F0046C8 00000000 */   nop
obj_03_expand:
/* 0370BC 7F0046CC 13C000F0 */  beqz  $fp, other_obj_expand
/* 0370C0 7F0046D0 00000000 */   nop
/* 0370C4 7F0046D4 8E4F000C */  lw    $t7, 0xc($s2)
/* 0370C8 7F0046D8 02A02025 */  move  $a0, $s5
/* 0370CC 7F0046DC 02402825 */  move  $a1, $s2
/* 0370D0 7F0046E0 01F47024 */  and   $t6, $t7, $s4
/* 0370D4 7F0046E4 15C000EA */  bnez  $t6, other_obj_expand
/* 0370D8 7F0046E8 00000000 */   nop
/* 0370DC 7F0046EC 0FC00767 */  jal   domakedefaultobj
/* 0370E0 7F0046F0 02603025 */   move  $a2, $s3
/* 0370E4 7F0046F4 100000E6 */  b     other_obj_expand
/* 0370E8 7F0046F8 00000000 */   nop
ammo_box_expand:
/* 0370EC 7F0046FC 0FC26669 */  jal   getPlayerCount
/* 0370F0 7F004700 24100001 */   li    $s0, 1
/* 0370F4 7F004704 28410002 */  slti  $at, $v0, 2
/* 0370F8 7F004708 1420000C */  bnez  $at, .L7F0047B8
/* 0370FC 7F00470C 00000000 */   nop
/* 037100 7F004710 0FC316CD */  jal   getPtrMPWeaponSetData
/* 037104 7F004714 00000000 */   nop
/* 037108 7F004718 3C188006 */  lui   $t8, %hi(lastmpweaponnum) # $t8, 0x8006
/* 03710C 7F00471C 8F188480 */  lw    $t8, %lo(lastmpweaponnum)($t8)
/* 037110 7F004720 001840C0 */  sll   $t0, $t8, 3
/* 037114 7F004724 00481821 */  addu  $v1, $v0, $t0
/* 037118 7F004728 80640001 */  lb    $a0, 1($v1)
/* 03711C 7F00472C 80700002 */  lb    $s0, 2($v1)
/* 037120 7F004730 00044880 */  sll   $t1, $a0, 2
/* 037124 7F004734 0249C821 */  addu  $t9, $s2, $t1
/* 037128 7F004738 A730007E */  sh    $s0, 0x7e($t9)
.L7F0047B8:
/* 03712C 7F00473C 1A0000D4 */  blez  $s0, other_obj_expand
/* 037130 7F004740 00000000 */   nop
/* 037134 7F004744 13C000D2 */  beqz  $fp, other_obj_expand
/* 037138 7F004748 00000000 */   nop
/* 03713C 7F00474C 8E4A000C */  lw    $t2, 0xc($s2)
/* 037140 7F004750 00008825 */  move  $s1, $zero
/* 037144 7F004754 02408025 */  move  $s0, $s2
/* 037148 7F004758 01545824 */  and   $t3, $t2, $s4
/* 03714C 7F00475C 156000CC */  bnez  $t3, other_obj_expand
/* 037150 7F004760 00000000 */   nop
.L7F0047E0:
/* 037154 7F004764 960D0082 */  lhu   $t5, 0x82($s0)
/* 037158 7F004768 59A00007 */  blezl $t5, .L7F004804
/* 03715C 7F00476C 26310004 */   addiu $s1, $s1, 4
/* 037160 7F004770 96040080 */  lhu   $a0, 0x80($s0)
/* 037164 7F004774 52C40004 */  beql  $s6, $a0, .L7F004804
/* 037168 7F004778 26310004 */   addiu $s1, $s1, 4
/* 03716C 7F00477C 0FC15BC6 */  jal   modelLoad
/* 037170 7F004780 00000000 */   nop
/* 037174 7F004784 26310004 */  addiu $s1, $s1, 4
.L7F004804:
/* 037178 7F004788 1637FFF6 */  bne   $s1, $s7, .L7F0047E0
/* 03717C 7F00478C 26100004 */   addiu $s0, $s0, 4
/* 037180 7F004790 02A02025 */  move  $a0, $s5
/* 037184 7F004794 02402825 */  move  $a1, $s2
/* 037188 7F004798 0FC00767 */  jal   domakedefaultobj
/* 03718C 7F00479C 02603025 */   move  $a2, $s3
/* 037190 7F0047A0 100000BB */  b     other_obj_expand
/* 037194 7F0047A4 00000000 */   nop
type2D_tank:
/* 037198 7F0047A8 13C000B9 */  beqz  $fp, other_obj_expand
/* 03719C 7F0047AC 00000000 */   nop
/* 0371A0 7F0047B0 8E4C000C */  lw    $t4, 0xc($s2)
/* 0371A4 7F0047B4 24040020 */  li    $a0, 32
/* 0371A8 7F0047B8 01947824 */  and   $t7, $t4, $s4
/* 0371AC 7F0047BC 15E000B4 */  bnez  $t7, other_obj_expand
/* 0371B0 7F0047C0 00000000 */   nop
/* 0371B4 7F0047C4 0FC015AC */  jal   weaponLoadProjectileModels
/* 0371B8 7F0047C8 E7B400E8 */   swc1  $f20, 0xe8($sp)
/* 0371BC 7F0047CC 02A02025 */  move  $a0, $s5
/* 0371C0 7F0047D0 02402825 */  move  $a1, $s2
/* 0371C4 7F0047D4 0FC00767 */  jal   domakedefaultobj
/* 0371C8 7F0047D8 02603025 */   move  $a2, $s3
/* 0371CC 7F0047DC E65400C8 */  swc1  $f20, 0xc8($s2)
/* 0371D0 7F0047E0 E65400CC */  swc1  $f20, 0xcc($s2)
/* 0371D4 7F0047E4 C64C0038 */  lwc1  $f12, 0x38($s2)
/* 0371D8 7F0047E8 0FC16BB8 */  jal   atan2f
/* 0371DC 7F0047EC C64E0040 */   lwc1  $f14, 0x40($s2)
/* 0371E0 7F0047F0 3C018004 */  lui   $at, %hi(D_8004F024) # $at, 0x8004
/* 0371E4 7F0047F4 C426746C */  lwc1  $f6, %lo(D_8004F024)($at)
/* 0371E8 7F0047F8 8E420010 */  lw    $v0, 0x10($s2)
/* 0371EC 7F0047FC 46003401 */  sub.s $f16, $f6, $f0
/* 0371F0 7F004800 10400006 */  beqz  $v0, .L7F004898
/* 0371F4 7F004804 E65000DC */   swc1  $f16, 0xdc($s2)
/* 0371F8 7F004808 8C440014 */  lw    $a0, 0x14($v0)
/* 0371FC 7F00480C 8C450008 */  lw    $a1, 8($v0)
/* 037200 7F004810 0FC2C70C */  jal   stanGetPositionYValue
/* 037204 7F004814 8C460010 */   lw    $a2, 0x10($v0)
/* 037208 7F004818 E7A000E8 */  swc1  $f0, 0xe8($sp)
.L7F004898:
/* 03720C 7F00481C C7A200E8 */  lwc1  $f2, 0xe8($sp)
/* 037210 7F004820 3C018004 */  lui   $at, %hi(D_8004F028) # $at, 0x8004
/* 037214 7F004824 E64200D4 */  swc1  $f2, 0xd4($s2)
/* 037218 7F004828 C4247470 */  lwc1  $f4, %lo(D_8004F028)($at)
/* 03721C 7F00482C 46041203 */  div.s $f8, $f2, $f4
/* 037220 7F004830 10000097 */  b     other_obj_expand
/* 037224 7F004834 E64800D0 */   swc1  $f8, 0xd0($s2)
type27_wheeled_vehicle:
/* 037228 7F004838 13C00095 */  beqz  $fp, other_obj_expand
/* 03722C 7F00483C 00000000 */   nop
/* 037230 7F004840 8E4E000C */  lw    $t6, 0xc($s2)
/* 037234 7F004844 02A02025 */  move  $a0, $s5
/* 037238 7F004848 02402825 */  move  $a1, $s2
/* 03723C 7F00484C 01D4C024 */  and   $t8, $t6, $s4
/* 037240 7F004850 1700008F */  bnez  $t8, other_obj_expand
/* 037244 7F004854 00000000 */   nop
/* 037248 7F004858 0FC00767 */  jal   domakedefaultobj
/* 03724C 7F00485C 02603025 */   move  $a2, $s3
/* 037250 7F004860 8E440014 */  lw    $a0, 0x14($s2)
/* 037254 7F004864 5080000E */  beql  $a0, $zero, .L7F00491C
/* 037258 7F004868 3C01BF80 */   lui   $at, 0xbf80
/* 03725C 7F00486C 8C880008 */  lw    $t0, 8($a0)
/* 037260 7F004870 8D090008 */  lw    $t1, 8($t0)
/* 037264 7F004874 8D250014 */  lw    $a1, 0x14($t1)
/* 037268 7F004878 50A00009 */  beql  $a1, $zero, .L7F00491C
/* 03726C 7F00487C 3C01BF80 */   lui   $at, 0xbf80
/* 037270 7F004880 0FC1B3A3 */  jal   modelGetNodeRwData
/* 037274 7F004884 00000000 */   nop
/* 037278 7F004888 8E590008 */  lw    $t9, 8($s2)
/* 03727C 7F00488C 3C011000 */  li    $at, 0x10000000 # 0.000000
/* 037280 7F004890 03215024 */  and   $t2, $t9, $at
/* 037284 7F004894 2D4B0001 */  sltiu $t3, $t2, 1
/* 037288 7F004898 AC4B0000 */  sw    $t3, ($v0)
/* 03728C 7F00489C 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
.L7F00491C:
/* 037290 7F0048A0 44819000 */  mtc1  $at, $f18
/* 037294 7F0048A4 E6540088 */  swc1  $f20, 0x88($s2)
/* 037298 7F0048A8 E654008C */  swc1  $f20, 0x8c($s2)
/* 03729C 7F0048AC E6540090 */  swc1  $f20, 0x90($s2)
/* 0372A0 7F0048B0 E6540094 */  swc1  $f20, 0x94($s2)
/* 0372A4 7F0048B4 E654009C */  swc1  $f20, 0x9c($s2)
/* 0372A8 7F0048B8 E65400A0 */  swc1  $f20, 0xa0($s2)
/* 0372AC 7F0048BC 8E440080 */  lw    $a0, 0x80($s2)
/* 0372B0 7F0048C0 0FC0D4F6 */  jal   ailistFindById
/* 0372B4 7F0048C4 E6520098 */   swc1  $f18, 0x98($s2)
/* 0372B8 7F0048C8 240DFFFF */  li    $t5, -1
/* 0372BC 7F0048CC AE420080 */  sw    $v0, 0x80($s2)
/* 0372C0 7F0048D0 A6400084 */  sh    $zero, 0x84($s2)
/* 0372C4 7F0048D4 A64D0086 */  sh    $t5, 0x86($s2)
/* 0372C8 7F0048D8 AE4000A4 */  sw    $zero, 0xa4($s2)
/* 0372CC 7F0048DC AE4000A8 */  sw    $zero, 0xa8($s2)
/* 0372D0 7F0048E0 1000006B */  b     other_obj_expand
/* 0372D4 7F0048E4 AE4000AC */   sw    $zero, 0xac($s2)
type28_aircraft:
/* 0372D8 7F0048E8 13C00069 */  beqz  $fp, other_obj_expand
/* 0372DC 7F0048EC 00000000 */   nop
/* 0372E0 7F0048F0 8E4C000C */  lw    $t4, 0xc($s2)
/* 0372E4 7F0048F4 02A02025 */  move  $a0, $s5
/* 0372E8 7F0048F8 02402825 */  move  $a1, $s2
/* 0372EC 7F0048FC 01947824 */  and   $t7, $t4, $s4
/* 0372F0 7F004900 15E00063 */  bnez  $t7, other_obj_expand
/* 0372F4 7F004904 00000000 */   nop
/* 0372F8 7F004908 0FC00767 */  jal   domakedefaultobj
/* 0372FC 7F00490C 02603025 */   move  $a2, $s3
/* 037300 7F004910 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
/* 037304 7F004914 44810000 */  mtc1  $at, $f0
/* 037308 7F004918 E6540098 */  swc1  $f20, 0x98($s2)
/* 03730C 7F00491C E654009C */  swc1  $f20, 0x9c($s2)
/* 037310 7F004920 E6540088 */  swc1  $f20, 0x88($s2)
/* 037314 7F004924 E654008C */  swc1  $f20, 0x8c($s2)
/* 037318 7F004928 E6540090 */  swc1  $f20, 0x90($s2)
/* 03731C 7F00492C E65400A4 */  swc1  $f20, 0xa4($s2)
/* 037320 7F004930 8E440080 */  lw    $a0, 0x80($s2)
/* 037324 7F004934 E64000A0 */  swc1  $f0, 0xa0($s2)
/* 037328 7F004938 0FC0D4F6 */  jal   ailistFindById
/* 03732C 7F00493C E6400094 */   swc1  $f0, 0x94($s2)
/* 037330 7F004940 240EFFFF */  li    $t6, -1
/* 037334 7F004944 AE420080 */  sw    $v0, 0x80($s2)
/* 037338 7F004948 A6400084 */  sh    $zero, 0x84($s2)
/* 03733C 7F00494C A64E0086 */  sh    $t6, 0x86($s2)
/* 037340 7F004950 AE4000A8 */  sw    $zero, 0xa8($s2)
/* 037344 7F004954 AE4000AC */  sw    $zero, 0xac($s2)
/* 037348 7F004958 1000004D */  b     other_obj_expand
/* 03734C 7F00495C AE4000B0 */   sw    $zero, 0xb0($s2)
tag_expand:
/* 037350 7F004960 86580006 */  lh    $t8, 6($s2)
/* 037354 7F004964 02A02025 */  move  $a0, $s5
/* 037358 7F004968 0FC15C60 */  jal   setupCommandGetObject
/* 03735C 7F00496C 03132821 */   addu  $a1, $t8, $s3
/* 037360 7F004970 10400004 */  beqz  $v0, .L7F004A00
/* 037364 7F004974 AE42000C */   sw    $v0, 0xc($s2)
/* 037368 7F004978 8C480064 */  lw    $t0, 0x64($v0)
/* 03736C 7F00497C 35090010 */  ori   $t1, $t0, 0x10
/* 037370 7F004980 AC490064 */  sw    $t1, 0x64($v0)
.L7F004A00:
/* 037374 7F004984 0FC013C8 */  jal   set_parent_cur_tag_entry
/* 037378 7F004988 02402025 */   move  $a0, $s2
/* 03737C 7F00498C 10000040 */  b     other_obj_expand
/* 037380 7F004990 00000000 */   nop
type25_rename:
/* 037384 7F004994 8E590004 */  lw    $t9, 4($s2)
/* 037388 7F004998 02A02025 */  move  $a0, $s5
/* 03738C 7F00499C 0FC15C60 */  jal   setupCommandGetObject
/* 037390 7F0049A0 03332821 */   addu  $a1, $t9, $s3
/* 037394 7F0049A4 10400004 */  beqz  $v0, .L7F004A34
/* 037398 7F0049A8 AE420024 */   sw    $v0, 0x24($s2)
/* 03739C 7F0049AC 8C4A0064 */  lw    $t2, 0x64($v0)
/* 0373A0 7F0049B0 354B0400 */  ori   $t3, $t2, 0x400
/* 0373A4 7F0049B4 AC4B0064 */  sw    $t3, 0x64($v0)
.L7F004A34:
/* 0373A8 7F0049B8 0FC23782 */  jal   bondinvAddTextOverride
/* 0373AC 7F0049BC 02402025 */   move  $a0, $s2
/* 0373B0 7F0049C0 10000033 */  b     other_obj_expand
/* 0373B4 7F0049C4 00000000 */   nop
type23_solo_brief_text:
/* 0373B8 7F0049C8 0FC013CE */  jal   setup_briefing_text_entry_parent
/* 0373BC 7F0049CC 02402025 */   move  $a0, $s2
/* 0373C0 7F0049D0 1000002F */  b     other_obj_expand
/* 0373C4 7F0049D4 00000000 */   nop
type2E_viewport_pos:
/* 0373C8 7F0049D8 8E4D0004 */  lw    $t5, 4($s2)
/* 0373CC 7F0049DC 3C0142C8 */  li    $at, 0x42C80000 # 100.000000
/* 0373D0 7F0049E0 44810000 */  mtc1  $at, $f0
/* 0373D4 7F0049E4 448D5000 */  mtc1  $t5, $f10
/* 0373D8 7F0049E8 8E4F000C */  lw    $t7, 0xc($s2)
/* 0373DC 7F0049EC 8E4C0008 */  lw    $t4, 8($s2)
/* 0373E0 7F0049F0 468051A0 */  cvt.s.w $f6, $f10
/* 0373E4 7F0049F4 448F5000 */  mtc1  $t7, $f10
/* 0373E8 7F0049F8 448C2000 */  mtc1  $t4, $f4
/* 0373EC 7F0049FC 8E4E0010 */  lw    $t6, 0x10($s2)
/* 0373F0 7F004A00 8E580014 */  lw    $t8, 0x14($s2)
/* 0373F4 7F004A04 46802220 */  cvt.s.w $f8, $f4
/* 0373F8 7F004A08 448E2000 */  mtc1  $t6, $f4
/* 0373FC 7F004A0C 3C014780 */  li    $at, 0x47800000 # 65536.000000
/* 037400 7F004A10 46003403 */  div.s $f16, $f6, $f0
/* 037404 7F004A14 468051A0 */  cvt.s.w $f6, $f10
/* 037408 7F004A18 46004483 */  div.s $f18, $f8, $f0
/* 03740C 7F004A1C E6500004 */  swc1  $f16, 4($s2)
/* 037410 7F004A20 46802220 */  cvt.s.w $f8, $f4
/* 037414 7F004A24 44812000 */  mtc1  $at, $f4
/* 037418 7F004A28 46003403 */  div.s $f16, $f6, $f0
/* 03741C 7F004A2C 44983000 */  mtc1  $t8, $f6
/* 037420 7F004A30 E6520008 */  swc1  $f18, 8($s2)
/* 037424 7F004A34 44819000 */  mtc1  $at, $f18
/* 037428 7F004A38 00000000 */  nop
/* 03742C 7F004A3C 46124283 */  div.s $f10, $f8, $f18
/* 037430 7F004A40 E650000C */  swc1  $f16, 0xc($s2)
/* 037434 7F004A44 46803420 */  cvt.s.w $f16, $f6
/* 037438 7F004A48 46048203 */  div.s $f8, $f16, $f4
/* 03743C 7F004A4C E64A0010 */  swc1  $f10, 0x10($s2)
/* 037440 7F004A50 1000000F */  b     other_obj_expand
/* 037444 7F004A54 E6480014 */   swc1  $f8, 0x14($s2)
type17_objective_expand:
/* 037448 7F004A58 0FC013D4 */  jal   add_ptr_to_objective
/* 03744C 7F004A5C 02402025 */   move  $a0, $s2
/* 037450 7F004A60 1000000B */  b     other_obj_expand
/* 037454 7F004A64 00000000 */   nop
type20_subobjective_expand:
/* 037458 7F004A68 0FC013E3 */  jal   set_parent_cur_obj_enter_room
/* 03745C 7F004A6C 02402025 */   move  $a0, $s2
/* 037460 7F004A70 10000007 */  b     other_obj_expand
/* 037464 7F004A74 00000000 */   nop
type21_subobjective_expand:
/* 037468 7F004A78 0FC013E9 */  jal   set_parent_cur_obj_deposited_in_room
/* 03746C 7F004A7C 02402025 */   move  $a0, $s2
/* 037470 7F004A80 10000003 */  b     other_obj_expand
/* 037474 7F004A84 00000000 */   nop
type1E_subobjective_expand:
/* 037478 7F004A88 0FC013EF */  jal   set_parent_cur_obj_photograph
/* 03747C 7F004A8C 02402025 */   move  $a0, $s2
other_obj_expand:
/* 037480 7F004A90 0FC15AF5 */  jal   sizepropdef
/* 037484 7F004A94 02402025 */   move  $a0, $s2
/* 037488 7F004A98 00024080 */  sll   $t0, $v0, 2
/* 03748C 7F004A9C 01129021 */  addu  $s2, $t0, $s2
/* 037490 7F004AA0 92430003 */  lbu   $v1, 3($s2)
/* 037494 7F004AA4 24010030 */  li    $at, 48
/* 037498 7F004AA8 26730001 */  addiu $s3, $s3, 1
/* 03749C 7F004AAC 5461FE11 */  bnel  $v1, $at, .L7F0042F4
/* 0374A0 7F004AB0 2468FFFF */   addiu $t0, $v1, -1
/* 0374A4 7F004AB4 3C128006 */  lui   $s2, %hi(g_CurrentSetup+0x0c) # $s2, 0x8006
/* 0374A8 7F004AB8 8E524C4C */  lw    $s2, %lo(g_CurrentSetup+0x0c)($s2)
/* 0374AC 7F004ABC 00009825 */  move  $s3, $zero
/* 0374B0 7F004AC0 92430003 */  lbu   $v1, 3($s2)
.L7F004B40:
/* 0374B4 7F004AC4 24010030 */  li    $at, 48
/* 0374B8 7F004AC8 3C170008 */  lui   $s7, 8
/* 0374BC 7F004ACC 106100CF */  beq   $v1, $at, .L7F004E88
/* 0374C0 7F004AD0 24160008 */   li    $s6, 8
/* 0374C4 7F004AD4 241E002B */  li    $fp, 43
/* 0374C8 7F004AD8 24140001 */  li    $s4, 1
/* 0374CC 7F004ADC 2469FFFD */  addiu $t1, $v1, -3
.L7F004B5C:
/* 0374D0 7F004AE0 2D21002D */  sltiu $at, $t1, 0x2d
/* 0374D4 7F004AE4 102000A4 */  beqz  $at, .L7F004DF4
/* 0374D8 7F004AE8 00094880 */   sll   $t1, $t1, 2
/* 0374DC 7F004AEC 3C018004 */  lui   $at, %hi(jpt_8004F02C)
/* 0374E0 7F004AF0 00290821 */  addu  $at, $at, $t1
/* 0374E4 7F004AF4 8C297474 */  lw    $t1, %lo(jpt_8004F02C)($at)
/* 0374E8 7F004AF8 01200008 */  jr    $t1
/* 0374EC 7F004AFC 00000000 */   nop
.L7F004B7C:
/* 0374F0 7F004B00 8E590010 */  lw    $t9, 0x10($s2)
/* 0374F4 7F004B04 1320009C */  beqz  $t9, .L7F004DF4
/* 0374F8 7F004B08 00000000 */   nop
/* 0374FC 7F004B0C 8E4A0008 */  lw    $t2, 8($s2)
/* 037500 7F004B10 314B8000 */  andi  $t3, $t2, 0x8000
/* 037504 7F004B14 11600098 */  beqz  $t3, .L7F004DF4
/* 037508 7F004B18 00000000 */   nop
/* 03750C 7F004B1C 86420006 */  lh    $v0, 6($s2)
/* 037510 7F004B20 02A02025 */  move  $a0, $s5
/* 037514 7F004B24 0FC15C60 */  jal   setupCommandGetObject
/* 037518 7F004B28 02622821 */   addu  $a1, $s3, $v0
/* 03751C 7F004B2C 10400092 */  beqz  $v0, .L7F004DF4
/* 037520 7F004B30 00408025 */   move  $s0, $v0
/* 037524 7F004B34 8C4D0010 */  lw    $t5, 0x10($v0)
/* 037528 7F004B38 11A0008F */  beqz  $t5, .L7F004DF4
/* 03752C 7F004B3C 00000000 */   nop
/* 037530 7F004B40 8E4C0064 */  lw    $t4, 0x64($s2)
/* 037534 7F004B44 8E440014 */  lw    $a0, 0x14($s2)
/* 037538 7F004B48 01977825 */  or    $t7, $t4, $s7
/* 03753C 7F004B4C AE4F0064 */  sw    $t7, 0x64($s2)
/* 037540 7F004B50 0FC1B4CF */  jal   modelSetScale
/* 037544 7F004B54 8C850014 */   lw    $a1, 0x14($a0)
/* 037548 7F004B58 8E440010 */  lw    $a0, 0x10($s2)
/* 03754C 7F004B5C 0FC0E999 */  jal   chrpropReparent
/* 037550 7F004B60 8E050010 */   lw    $a1, 0x10($s0)
/* 037554 7F004B64 10000084 */  b     .L7F004DF4
/* 037558 7F004B68 00000000 */   nop
.L7F004BE8:
/* 03755C 7F004B6C 8E4E0004 */  lw    $t6, 4($s2)
/* 037560 7F004B70 0FC15B5A */  jal   setupGetPtrToCommandByIndex
/* 037564 7F004B74 01D32021 */   addu  $a0, $t6, $s3
/* 037568 7F004B78 8E580008 */  lw    $t8, 8($s2)
/* 03756C 7F004B7C 00408025 */  move  $s0, $v0
/* 037570 7F004B80 0FC15B5A */  jal   setupGetPtrToCommandByIndex
/* 037574 7F004B84 03132021 */   addu  $a0, $t8, $s3
/* 037578 7F004B88 1200007B */  beqz  $s0, .L7F004DF4
/* 03757C 7F004B8C 00402825 */   move  $a1, $v0
/* 037580 7F004B90 10400079 */  beqz  $v0, .L7F004DF4
/* 037584 7F004B94 00000000 */   nop
/* 037588 7F004B98 92080003 */  lbu   $t0, 3($s0)
/* 03758C 7F004B9C 16C80076 */  bne   $s6, $t0, .L7F004DF4
/* 037590 7F004BA0 00000000 */   nop
/* 037594 7F004BA4 90490003 */  lbu   $t1, 3($v0)
/* 037598 7F004BA8 16C90073 */  bne   $s6, $t1, .L7F004DF4
/* 03759C 7F004BAC 00000000 */   nop
/* 0375A0 7F004BB0 0FC14815 */  jal   propweaponSetDual
/* 0375A4 7F004BB4 02002025 */   move  $a0, $s0
/* 0375A8 7F004BB8 1000006F */  b     .L7F004DF4
/* 0375AC 7F004BBC 00000000 */   nop
.L7F004C3C:
/* 0375B0 7F004BC0 8E420004 */  lw    $v0, 4($s2)
/* 0375B4 7F004BC4 8E510008 */  lw    $s1, 8($s2)
/* 0375B8 7F004BC8 02A02025 */  move  $a0, $s5
/* 0375BC 7F004BCC 0FC15C60 */  jal   setupCommandGetObject
/* 0375C0 7F004BD0 02622821 */   addu  $a1, $s3, $v0
/* 0375C4 7F004BD4 00408025 */  move  $s0, $v0
/* 0375C8 7F004BD8 0FC15B5A */  jal   setupGetPtrToCommandByIndex
/* 0375CC 7F004BDC 02712021 */   addu  $a0, $s3, $s1
/* 0375D0 7F004BE0 12000065 */  beqz  $s0, .L7F004DF4
/* 0375D4 7F004BE4 00000000 */   nop
/* 0375D8 7F004BE8 8E030010 */  lw    $v1, 0x10($s0)
/* 0375DC 7F004BEC 10600062 */  beqz  $v1, .L7F004DF4
/* 0375E0 7F004BF0 00000000 */   nop
/* 0375E4 7F004BF4 10400060 */  beqz  $v0, .L7F004DF4
/* 0375E8 7F004BF8 00000000 */   nop
/* 0375EC 7F004BFC 90590003 */  lbu   $t9, 3($v0)
/* 0375F0 7F004C00 1699005D */  bne   $s4, $t9, .L7F004DF4
/* 0375F4 7F004C04 00000000 */   nop
/* 0375F8 7F004C08 8C4A0010 */  lw    $t2, 0x10($v0)
/* 0375FC 7F004C0C 1140005A */  beqz  $t2, .L7F004DF4
/* 037600 7F004C10 00000000 */   nop
/* 037604 7F004C14 AE430004 */  sw    $v1, 4($s2)
/* 037608 7F004C18 8C4B0010 */  lw    $t3, 0x10($v0)
/* 03760C 7F004C1C 02402025 */  move  $a0, $s2
/* 037610 7F004C20 0FC00644 */  jal   initSetLevelLoadPropSwitch
/* 037614 7F004C24 AE4B0008 */   sw    $t3, 8($s2)
/* 037618 7F004C28 8E0D0064 */  lw    $t5, 0x64($s0)
/* 03761C 7F004C2C 35AC0001 */  ori   $t4, $t5, 1
/* 037620 7F004C30 10000051 */  b     .L7F004DF4
/* 037624 7F004C34 AE0C0064 */   sw    $t4, 0x64($s0)
.L7F004CB4:
/* 037628 7F004C38 8E420004 */  lw    $v0, 4($s2)
/* 03762C 7F004C3C 8E4F000C */  lw    $t7, 0xc($s2)
/* 037630 7F004C40 8E510008 */  lw    $s1, 8($s2)
/* 037634 7F004C44 02A02025 */  move  $a0, $s5
/* 037638 7F004C48 02622821 */  addu  $a1, $s3, $v0
/* 03763C 7F004C4C 0FC15C60 */  jal   setupCommandGetObject
/* 037640 7F004C50 AFAF0080 */   sw    $t7, 0x80($sp)
/* 037644 7F004C54 00408025 */  move  $s0, $v0
/* 037648 7F004C58 02A02025 */  move  $a0, $s5
/* 03764C 7F004C5C 0FC15C60 */  jal   setupCommandGetObject
/* 037650 7F004C60 02712821 */   addu  $a1, $s3, $s1
/* 037654 7F004C64 8FAE0080 */  lw    $t6, 0x80($sp)
/* 037658 7F004C68 00408825 */  move  $s1, $v0
/* 03765C 7F004C6C 02A02025 */  move  $a0, $s5
/* 037660 7F004C70 0FC15C60 */  jal   setupCommandGetObject
/* 037664 7F004C74 026E2821 */   addu  $a1, $s3, $t6
/* 037668 7F004C78 1200003F */  beqz  $s0, .L7F004DF4
/* 03766C 7F004C7C 00000000 */   nop
/* 037670 7F004C80 8E180010 */  lw    $t8, 0x10($s0)
/* 037674 7F004C84 1300003C */  beqz  $t8, .L7F004DF4
/* 037678 7F004C88 00000000 */   nop
/* 03767C 7F004C8C 1220003A */  beqz  $s1, .L7F004DF4
/* 037680 7F004C90 00000000 */   nop
/* 037684 7F004C94 8E280010 */  lw    $t0, 0x10($s1)
/* 037688 7F004C98 11000037 */  beqz  $t0, .L7F004DF4
/* 03768C 7F004C9C 00000000 */   nop
/* 037690 7F004CA0 92290003 */  lbu   $t1, 3($s1)
/* 037694 7F004CA4 17C90034 */  bne   $fp, $t1, .L7F004DF4
/* 037698 7F004CA8 00000000 */   nop
/* 03769C 7F004CAC 10400032 */  beqz  $v0, .L7F004DF4
/* 0376A0 7F004CB0 00000000 */   nop
/* 0376A4 7F004CB4 8C590010 */  lw    $t9, 0x10($v0)
/* 0376A8 7F004CB8 1320002F */  beqz  $t9, .L7F004DF4
/* 0376AC 7F004CBC 00000000 */   nop
/* 0376B0 7F004CC0 904A0003 */  lbu   $t2, 3($v0)
/* 0376B4 7F004CC4 02402025 */  move  $a0, $s2
/* 0376B8 7F004CC8 168A002B */  bne   $s4, $t2, .L7F004DF4
/* 0376BC 7F004CCC 00000000 */   nop
/* 0376C0 7F004CD0 AE500004 */  sw    $s0, 4($s2)
/* 0376C4 7F004CD4 AE510008 */  sw    $s1, 8($s2)
/* 0376C8 7F004CD8 AE42000C */  sw    $v0, 0xc($s2)
/* 0376CC 7F004CDC 0FC00650 */  jal   initSetLevelLoadPropSafeItem
/* 0376D0 7F004CE0 AFA20074 */   sw    $v0, 0x74($sp)
/* 0376D4 7F004CE4 8E0B000C */  lw    $t3, 0xc($s0)
/* 0376D8 7F004CE8 8FA30074 */  lw    $v1, 0x74($sp)
/* 0376DC 7F004CEC 356D0400 */  ori   $t5, $t3, 0x400
/* 0376E0 7F004CF0 AE0D000C */  sw    $t5, 0xc($s0)
/* 0376E4 7F004CF4 8C6C000C */  lw    $t4, 0xc($v1)
/* 0376E8 7F004CF8 358F0400 */  ori   $t7, $t4, 0x400
/* 0376EC 7F004CFC 1000001E */  b     .L7F004DF4
/* 0376F0 7F004D00 AC6F000C */   sw    $t7, 0xc($v1)
.L7F004D80:
/* 0376F4 7F004D04 8E420004 */  lw    $v0, 4($s2)
/* 0376F8 7F004D08 8E510008 */  lw    $s1, 8($s2)
/* 0376FC 7F004D0C 02A02025 */  move  $a0, $s5
/* 037700 7F004D10 0FC15C60 */  jal   setupCommandGetObject
/* 037704 7F004D14 02622821 */   addu  $a1, $s3, $v0
/* 037708 7F004D18 00408025 */  move  $s0, $v0
/* 03770C 7F004D1C 02A02025 */  move  $a0, $s5
/* 037710 7F004D20 0FC15C60 */  jal   setupCommandGetObject
/* 037714 7F004D24 02712821 */   addu  $a1, $s3, $s1
/* 037718 7F004D28 12000013 */  beqz  $s0, .L7F004DF4
/* 03771C 7F004D2C 00000000 */   nop
/* 037720 7F004D30 8E0E0010 */  lw    $t6, 0x10($s0)
/* 037724 7F004D34 11C00010 */  beqz  $t6, .L7F004DF4
/* 037728 7F004D38 00000000 */   nop
/* 03772C 7F004D3C 1040000E */  beqz  $v0, .L7F004DF4
/* 037730 7F004D40 00000000 */   nop
/* 037734 7F004D44 8C580010 */  lw    $t8, 0x10($v0)
/* 037738 7F004D48 1300000B */  beqz  $t8, .L7F004DF4
/* 03773C 7F004D4C 00000000 */   nop
/* 037740 7F004D50 92080003 */  lbu   $t0, 3($s0)
/* 037744 7F004D54 02402025 */  move  $a0, $s2
/* 037748 7F004D58 16880007 */  bne   $s4, $t0, .L7F004DF4
/* 03774C 7F004D5C 00000000 */   nop
/* 037750 7F004D60 AE500004 */  sw    $s0, 4($s2)
/* 037754 7F004D64 0FC0064A */  jal   initSetLevelLoadPropLockDoor
/* 037758 7F004D68 AE420008 */   sw    $v0, 8($s2)
/* 03775C 7F004D6C 8E090064 */  lw    $t1, 0x64($s0)
/* 037760 7F004D70 35392000 */  ori   $t9, $t1, 0x2000
/* 037764 7F004D74 AE190064 */  sw    $t9, 0x64($s0)
.L7F004DF4:
/* 037768 7F004D78 0FC15AF5 */  jal   sizepropdef
/* 03776C 7F004D7C 02402025 */   move  $a0, $s2
/* 037770 7F004D80 00025080 */  sll   $t2, $v0, 2
/* 037774 7F004D84 01529021 */  addu  $s2, $t2, $s2
/* 037778 7F004D88 92430003 */  lbu   $v1, 3($s2)
/* 03777C 7F004D8C 24010030 */  li    $at, 48
/* 037780 7F004D90 26730001 */  addiu $s3, $s3, 1
/* 037784 7F004D94 5461FF52 */  bnel  $v1, $at, .L7F004B5C
/* 037788 7F004D98 2469FFFD */   addiu $t1, $v1, -3
/* 03778C 7F004D9C 1000001B */  b     .L7F004E88
/* 037790 7F004DA0 00000000 */   nop
.L7F004E20:
/* 037794 7F004DA4 3C018006 */  lui   $at, %hi(g_CurrentSetup+0) # $at, 0x8006
/* 037798 7F004DA8 AC204C40 */  sw    $zero, %lo(g_CurrentSetup+0)($at)
/* 03779C 7F004DAC 3C018006 */  lui   $at, %hi(g_CurrentSetup+4) # $at, 0x8006
/* 0377A0 7F004DB0 AC204C44 */  sw    $zero, %lo(g_CurrentSetup+4)($at)
/* 0377A4 7F004DB4 3C018006 */  lui   $at, %hi(g_CurrentSetup+8) # $at, 0x8006
/* 0377A8 7F004DB8 AC204C48 */  sw    $zero, %lo(g_CurrentSetup+8)($at)
/* 0377AC 7F004DBC 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x0c) # $at, 0x8006
/* 0377B0 7F004DC0 AC204C4C */  sw    $zero, %lo(g_CurrentSetup+0x0c)($at)
/* 0377B4 7F004DC4 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x10) # $at, 0x8006
/* 0377B8 7F004DC8 AC204C50 */  sw    $zero, %lo(g_CurrentSetup+0x10)($at)
/* 0377BC 7F004DCC 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x14) # $at, 0x8006
/* 0377C0 7F004DD0 AC204C54 */  sw    $zero, %lo(g_CurrentSetup+0x14)($at)
/* 0377C4 7F004DD4 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x18) # $at, 0x8006
/* 0377C8 7F004DD8 AC204C58 */  sw    $zero, %lo(g_CurrentSetup+0x18)($at)
/* 0377CC 7F004DDC 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x1C) # $at, 0x8006
/* 0377D0 7F004DE0 AC204C5C */  sw    $zero, %lo(g_CurrentSetup+0x1C)($at)
/* 0377D4 7F004DE4 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x20) # $at, 0x8006
/* 0377D8 7F004DE8 AC204C60 */  sw    $zero, %lo(g_CurrentSetup+0x20)($at)
/* 0377DC 7F004DEC 3C018006 */  lui   $at, %hi(g_CurrentSetup+0x24) # $at, 0x8006
/* 0377E0 7F004DF0 AC204C64 */  sw    $zero, %lo(g_CurrentSetup+0x24)($at)
/* 0377E4 7F004DF4 0FC003AE */  jal   alloc_init_GUARDdata_entries
/* 0377E8 7F004DF8 00002025 */   move  $a0, $zero
/* 0377EC 7F004DFC 0FC01538 */  jal   sub_GAME_7F005540
/* 0377F0 7F004E00 00002025 */   move  $a0, $zero
/* 0377F4 7F004E04 0FC01571 */  jal   sub_GAME_7F005624
/* 0377F8 7F004E08 00002025 */   move  $a0, $zero
.L7F004E88:
/* 0377FC 7F004E0C 0FC00417 */  jal   alloc_false_GUARDdata_to_exec_global_action
/* 037800 7F004E10 00000000 */   nop
/* 037804 7F004E14 8FBF0044 */  lw    $ra, 0x44($sp)
/* 037808 7F004E18 D7B40018 */  ldc1  $f20, 0x18($sp)
/* 03780C 7F004E1C 8FB00020 */  lw    $s0, 0x20($sp)
/* 037810 7F004E20 8FB10024 */  lw    $s1, 0x24($sp)
/* 037814 7F004E24 8FB20028 */  lw    $s2, 0x28($sp)
/* 037818 7F004E28 8FB3002C */  lw    $s3, 0x2c($sp)
/* 03781C 7F004E2C 8FB40030 */  lw    $s4, 0x30($sp)
/* 037820 7F004E30 8FB50034 */  lw    $s5, 0x34($sp)
/* 037824 7F004E34 8FB60038 */  lw    $s6, 0x38($sp)
/* 037828 7F004E38 8FB7003C */  lw    $s7, 0x3c($sp)
/* 03782C 7F004E3C 8FBE0040 */  lw    $fp, 0x40($sp)
/* 037830 7F004E40 03E00008 */  jr    $ra
/* 037834 7F004E44 27BD0280 */   addiu $sp, $sp, 0x280
 )
#endif


#endif
