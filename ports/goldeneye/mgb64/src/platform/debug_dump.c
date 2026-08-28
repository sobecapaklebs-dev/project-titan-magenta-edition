/**
 * debug_dump.c — Guard state frame dump tool.
 *
 * Press ` (backtick) during gameplay to write a comprehensive snapshot
 * of all guard/chr state to ge007_dump_NNNN.txt in the save directory
 * (see savedirPath(), AUDIT-0066 — no longer a hardcoded /tmp path).
 * An on-screen overlay confirms the dump was captured.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include "savedir.h"     /* savedirPath — write dumps in the save dir, not /tmp */
#include "../bondtypes.h"
#include "../bondconstants.h"
#include "../game/chrai.h"
#include "../game/player.h"
#include "../game/gun.h"

extern union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node);

/* Externals from the game */
extern int g_frame_count_diag;
extern int g_MaxNumRooms;
extern int g_BgCurrentRoom;
extern f32 level_scale, inv_level_scale;
extern u8 *g_VtxBuffers[3];
extern u8 g_GfxActiveBufferIndex;

/* Camera */
extern Mtxf *camGetWorldToScreenMtxf(void);

/* Fog state */
struct CurrentEnvironmentRecord;
extern struct CurrentEnvironmentRecord *fogGetCurrentEnvironmentp(void);

/* Player access helpers — struct player is incomplete here, use bondview.c helpers */
extern coord3d *bondviewGetCurrentPlayersPosition(void);
extern u8 bondviewGetCurrentPlayersRoom(void);

/* Dump state */
static int s_dumpRequested = 0;
static int s_dumpCount = 0;
static char s_dumpPath[1024] = "";  /* sized to the savedir contract (AUDIT-0066) */
static int s_overlayTimer = 0;

static int debugDumpEnvFlag(const char *name) {
    const char *env = getenv(name);
    return env != NULL && env[0] != '\0' && env[0] != '0';
}

static void debugDumpMaybeAutoRequest(void) {
    static int initialized = 0;
    static int frame = -1;
    static int done = 0;
    const char *env;

    if (!initialized) {
        env = getenv("GE007_AUTO_DEBUG_DUMP_FRAME");
        if (env != NULL && env[0] != '\0') {
            frame = (int)strtol(env, NULL, 10);
        }
        initialized = 1;
    }

    if (!done && frame >= 0 && g_frame_count_diag >= frame) {
        s_dumpRequested = 1;
        done = 1;
    }
}

static int debugDumpFiniteF32(f32 value) {
    return value == value &&
           value > -3.402823466e+38f &&
           value < 3.402823466e+38f;
}

static int debugDumpPropPointerLooksValid(const PropRecord *prop) {
    uintptr_t ptr = (uintptr_t)prop;
    uintptr_t first = (uintptr_t)&pos_data_entry[0];
    uintptr_t last = (uintptr_t)&pos_data_entry[POS_DATA_ENTRY_LEN];

    return ptr >= first &&
           ptr < last &&
           ((ptr - first) % sizeof(pos_data_entry[0])) == 0;
}

static int debugDumpPtrInActiveDynVtxRange(const void *ptr, size_t bytes) {
    uintptr_t addr;
    uintptr_t endaddr;
    uintptr_t start;
    uintptr_t end;
    u8 index = g_GfxActiveBufferIndex;

    if (ptr == NULL || bytes == 0 || index > 1 ||
        g_VtxBuffers[index] == NULL || g_VtxBuffers[index + 1] == NULL) {
        return 0;
    }

    addr = (uintptr_t)ptr;
    if (addr > (~(uintptr_t)0) - bytes) {
        return 0;
    }

    endaddr = addr + bytes;
    start = (uintptr_t)g_VtxBuffers[index];
    end = (uintptr_t)g_VtxBuffers[index + 1];

    return addr >= start && endaddr <= end;
}

static int debugDumpModelHeaderLooksSane(const ModelFileHeader *header) {
    if (header == NULL ||
        header->RootNode == NULL ||
        header->numRecords < 0 ||
        header->numRecords > 4096 ||
        header->numMatrices < 0 ||
        header->numMatrices > 512 ||
        header->numSwitches < 0 ||
        header->numSwitches > 1024) {
        return 0;
    }

    if (header->Skeleton != NULL &&
        (header->Skeleton->numjoints <= 0 ||
         header->Skeleton->numjoints > 512)) {
        return 0;
    }

    return 1;
}

static int debugDumpModelLooksSane(const Model *model) {
    if (model == NULL ||
        !debugDumpModelHeaderLooksSane(model->obj) ||
        !debugDumpFiniteF32(model->scale) ||
        model->scale <= 0.0f ||
        model->scale > 1000.0f) {
        return 0;
    }

    if (model->obj->numRecords > 0 && model->datas == NULL) {
        return 0;
    }

    return 1;
}

static int debugDumpMatrixCountForModel(const Model *model) {
    s32 count;

    if (!debugDumpModelLooksSane(model)) {
        return 0;
    }

    count = model->obj->numMatrices;

#ifdef NATIVE_PORT
    if (model->obj->requiredRenderPosCount > count) {
        count = model->obj->requiredRenderPosCount;
    }
#endif

    if (model->obj->Skeleton != NULL &&
        model->obj->Skeleton->numjoints > count) {
        count = model->obj->Skeleton->numjoints;
    }

    if (count < 0 || count > 512) {
        return 0;
    }

    return count;
}

void debugDumpRequest(void) {
    s_dumpRequested = 1;
}

int debugDumpOverlayActive(void) {
    return s_overlayTimer > 0;
}

const char *debugDumpOverlayText(void) {
    return s_dumpPath;
}

/* debugDumpOverlayTick is defined at the bottom of this file */

static const char *actionName(int act) {
    static const char *names[] = {
        "INIT","STAND","KNEEL","ANIM","DIE","DEAD","ARGH","PREARGH",
        "ATTACK","ATTACKWALK","ATTACKROLL","SIDESTEP","JUMPOUT","RUNPOS",
        "PATROL","GOPOS","SURRENDER","STARTLARM","SURPRISED","TEST"
    };
    if (act >= 0 && act < 20) return names[act];
    return "???";
}

static const char *propTypeName(int t) {
    switch (t) {
        case 1: return "DOOR";
        case 2: return "OBJ";
        case 3: return "CHR";
        case 4: return "WEAPON";
        case 5: return "VIEWER";
        default: return "???";
    }
}

static void debugDumpViewmodelTrace(FILE *fp, const char *label, GUNHAND hand) {
    PortViewmodelTrace trace;

    memset(&trace, 0, sizeof(trace));
    portGetViewmodelTrace(hand, &trace);

    fprintf(fp, "  %s trace: valid=%d frame=%d item=%d visible=%d firing=%d flash=%d state=%d hold=%d\n",
            label,
            trace.valid,
            trace.frame,
            trace.item,
            trace.visible,
            trace.firing,
            trace.flash,
            trace.state,
            trace.hold_time);
    fprintf(fp, "    switch1=%d switches=%d s060=%d s078=%d sxflash=%d shell_l=0x%X shell_r=0x%X depth=%.2f\n",
            trace.switch1,
            trace.switch_count,
            trace.suppress_0x60,
            trace.suppress_0x78,
            trace.suppress_extra_flash,
            trace.shell_left_mask,
            trace.shell_right_mask,
            trace.depth);
    fprintf(fp, "    raw: item=%d pending=%d invis=%d lock=%d mag=%d weaponnum=%d watchmenu=%d anim=%d ammo_type=%d reserve=%d mag_size=%d flags=0x%X\n",
            trace.raw_hand_item,
            trace.raw_pending,
            trace.raw_invis,
            trace.raw_lock,
            trace.raw_mag,
            trace.raw_weaponnum,
            trace.raw_watchmenu,
            trace.raw_animation,
            trace.raw_ammo_type,
            trace.raw_ammo_reserve,
            trace.raw_mag_size,
            trace.raw_flags);
    fprintf(fp, "    root=(%.2f, %.2f, %.2f) world=(%.2f, %.2f, %.2f) muzzle=(%.2f, %.2f, %.2f) recoil=(%.5f, %.5f)\n",
            trace.root[0], trace.root[1], trace.root[2],
            trace.world[0], trace.world[1], trace.world[2],
            trace.muzzle[0], trace.muzzle[1], trace.muzzle[2],
            trace.recoil_angle, trace.bolt_recoil);
}

void debugDumpExecute(void) {
    debugDumpMaybeAutoRequest();

    if (!s_dumpRequested) return;
    s_dumpRequested = 0;

    /* AUDIT-0066: write dumps into the resolved save directory (portable across
     * Windows/consoles), not a hardcoded POSIX /tmp. savedirPath() returns a
     * static, non-thread-safe buffer, so copy it into s_dumpPath immediately
     * (mirrors stall_watchdog.c). */
    {
        char fn[64];
        snprintf(fn, sizeof(fn), "ge007_dump_%04d.txt", s_dumpCount++);
        snprintf(s_dumpPath, sizeof(s_dumpPath), "%s", savedirPath(fn));
    }
    FILE *fp = fopen(s_dumpPath, "w");
    if (!fp) {
        snprintf(s_dumpPath, sizeof(s_dumpPath), "DUMP FAILED (fopen)");
        s_overlayTimer = 180;
        return;
    }

    fprintf(fp, "=== GE007 Debug Dump — frame %d ===\n\n", g_frame_count_diag);

    /* --- Camera --- */
    fprintf(fp, "== CAMERA ==\n");
    {
        coord3d *ppos = bondviewGetCurrentPlayersPosition();
        s32 proom = bondviewGetCurrentPlayersRoom();
        if (ppos) {
            fprintf(fp, "  player_pos=(%.1f, %.1f, %.1f)\n", ppos->x, ppos->y, ppos->z);
        }
        fprintf(fp, "  player_room=%d g_BgCurrentRoom=%d\n", proom, g_BgCurrentRoom);
    }
    fprintf(fp, "  level_scale=%.6f inv_level_scale=%.6f\n", level_scale, inv_level_scale);

    Mtxf *cam = camGetWorldToScreenMtxf();
    if (cam) {
        fprintf(fp, "  cam_w2s:\n");
        for (int r = 0; r < 4; r++) {
            fprintf(fp, "    [%.4f, %.4f, %.4f, %.4f]\n",
                    cam->m[r][0], cam->m[r][1], cam->m[r][2], cam->m[r][3]);
        }
    }

    /* --- Fog --- */
    fprintf(fp, "\n== FOG ==\n");
    {
        /* CurrentEnvironmentRecord layout: s32 DiffFromFar, s32 FarIntensity,
         * u8 Red, u8 Green, u8 Blue, u8 Clouds, ... */
        u8 *ep = (u8 *)fogGetCurrentEnvironmentp();
        if (ep) {
            s32 diffFromFar, farIntensity;
            memcpy(&diffFromFar, ep + 0, 4);
            memcpy(&farIntensity, ep + 4, 4);
            fprintf(fp, "  DiffFromFar=%d FarIntensity=%d\n", diffFromFar, farIntensity);
            fprintf(fp, "  RGB=(%d,%d,%d) Clouds=%d\n", ep[8], ep[9], ep[10], ep[11]);
        }
    }

    /* --- Player weapon/HUD state --- */
    fprintf(fp, "\n== PLAYER WEAPON/HUD ==\n");
    if (g_CurrentPlayer) {
        struct player *p = g_CurrentPlayer;

        fprintf(fp, "  watch_state=%d watch_pause=%d watch_open_req=%d outside_watch=%d mpmenuon=%d\n",
                p->watch_animation_state,
                p->watch_pause_time,
                p->open_close_solo_watch_menu,
                p->outside_watch_menu,
                p->mpmenuon);
        fprintf(fp, "  gunsightmode=%d insightaimmode=%d gunammooff=%d force_disarm=%d\n",
                p->gunsightmode,
                p->insightaimmode,
                p->gunammooff,
                g_bondviewForceDisarm);
        fprintf(fp, "  crosshair=(%.2f, %.2f) crosshair_pos=(%.4f, %.4f) gunsight=(%.2f, %.2f)\n",
                p->crosshair_angle.f[0],
                p->crosshair_angle.f[1],
                p->crosshair_x_pos,
                p->crosshair_y_pos,
                p->field_FFC.x,
                p->field_FFC.y);
        fprintf(fp, "  health: bond=%.4f armour=%.4f actual_h=%.4f actual_a=%.4f damage_show=%d health_show=%d\n",
                p->bondhealth,
                p->bondarmour,
                p->actual_health,
                p->actual_armor,
                p->damageshowtime,
                p->healthshowtime);

        for (int i = GUNRIGHT; i <= GUNLEFT; i++) {
            struct hand *hand = &p->hands[i];
            const char *label = (i == GUNRIGHT) ? "RIGHT" : "LEFT";
            ITEM_IDS active = get_item_in_hand_or_watch_menu((GUNHAND)i);

            fprintf(fp, "  %s hand: ready=%d hand_item=%d active_item=%d pending=%d invis=%d lock=%d\n",
                    label,
                    Gun_hand_without_item((GUNHAND)i),
                    p->hand_item[i],
                    active,
                    p->field_2A44[i],
                    p->hand_invisible[i],
                    p->lock_hand_model[i]);
            fprintf(fp, "    weaponnum=%d watchmenu=%d prev=%d next=%d anim=%d trig=%d state=%d hold=%d fire=%d flash=%d visible=%d\n",
                    hand->weaponnum,
                    hand->weaponnum_watchmenu,
                    hand->previous_weapon,
                    hand->weapon_next_weapon,
                    hand->weapon_current_animation,
                    hand->weapon_animation_trigger,
                    hand->when_detonating_mines_is_0,
                    hand->weapon_hold_time,
                    hand->weapon_firing_status,
                    hand->field_87D,
                    hand->field_87F);
            fprintf(fp, "    ammo_mag=%d field_8A4=%d model=%p model_header=%p render_pos=%p muzzle=(%.2f, %.2f, %.2f) depth=%.2f\n",
                    hand->weapon_ammo_in_magazine,
                    hand->field_8A4,
                    (void *)hand->field_B68,
                    (void *)hand->field_B70,
                    (void *)hand->field_B74,
                    hand->field_B58.x,
                    hand->field_B58.y,
                    hand->field_B58.z,
                    hand->field_B64);
            debugDumpViewmodelTrace(fp, label, (GUNHAND)i);
        }
    } else {
        fprintf(fp, "  g_CurrentPlayer=NULL\n");
    }

    /* --- All CHR props --- */
    fprintf(fp, "\n== GUARDS/CHRS ==\n");
    {
        /* Iterate chr records via the setup propDefs.
         * On NATIVE_PORT, chrTickBeams iterates props; we can access
         * chr data through the prop->chr pointer for chr-type props. */
        extern s32 g_NumChrSlots;
        extern ChrRecord *g_ChrSlots;

        if (g_ChrSlots) {
            for (int i = 0; i < g_NumChrSlots && i < 50; i++) {
                ChrRecord *chr = &g_ChrSlots[i];
                if (chr->prop == NULL) continue;
                PropRecord *prop = chr->prop;
                Model *model = chr->model;

                fprintf(fp, "\n  --- chr[%d] chrnum=%d ---\n", i, chr->chrnum);

                if (!debugDumpPropPointerLooksValid(prop)) {
                    fprintf(fp, "  skipped: invalid prop pointer %p\n", (void *)prop);
                    continue;
                }

                if (prop->type != PROP_TYPE_CHR || prop->chr != chr) {
                    fprintf(fp, "  skipped: prop mismatch type=%d prop_chr=%p expected_chr=%p\n",
                            prop->type, (void *)prop->chr, (void *)chr);
                    continue;
                }

                fprintf(fp, "  action=%d(%s) hidden=0x%04X chrflags=0x%X sleep=%d\n",
                        chr->actiontype, actionName(chr->actiontype),
                        chr->hidden, chr->chrflags, chr->sleep);
                fprintf(fp, "  pos=(%.1f, %.1f, %.1f) ground=%.1f manground=%.1f\n",
                        prop->pos.x, prop->pos.y, prop->pos.z,
                        chr->ground, chr->manground);
                fprintf(fp, "  prop_type=%d(%s) flags=0x%X rooms=[%d,%d,%d,%d] stan=%p\n",
                        prop->type, propTypeName(prop->type),
                        prop->flags,
                        prop->rooms[0], prop->rooms[1], prop->rooms[2], prop->rooms[3],
                        (void*)prop->stan);

                if (model) {
                    if (!debugDumpModelLooksSane(model)) {
                        fprintf(fp, "  model=%p skipped: unsafe model/header state obj=%p datas=%p render_pos=%p\n",
                                (void *)model,
                                model != NULL ? (void *)model->obj : NULL,
                                model != NULL ? (void *)model->datas : NULL,
                                model != NULL ? (void *)model->render_pos : NULL);
                        continue;
                    }

                    fprintf(fp, "  model=%p anim=%p animlooping=%d speed=%.2f playspeed=%.2f\n",
                            (void*)model, (void*)model->anim,
                            model->animlooping, model->speed, model->playspeed);
                    fprintf(fp, "  scale=%.4f render_pos=%p field_20=%p\n",
                            model->scale, (void*)model->render_pos, (void*)chr->field_20);

                    /* Dump bone matrices only when explicitly requested. This
                     * path reads per-frame dyn memory and must stay non-fatal
                     * when a model slot is stale or mid-transition. */
                    if (model->render_pos) {
                        s32 nmat = debugDumpMatrixCountForModel(model);
                        size_t nbytes = (size_t)nmat * sizeof(RenderPosView);

                        fprintf(fp, "  render_pos=%p matrices=%d dyn_live=%d\n",
                                (void*)model->render_pos,
                                nmat,
                                debugDumpPtrInActiveDynVtxRange(model->render_pos, nbytes));

                        if (debugDumpEnvFlag("GE007_DEBUG_DUMP_BONES") &&
                            nmat > 0 &&
                            debugDumpPtrInActiveDynVtxRange(model->render_pos, nbytes)) {
                            if (nmat > 20) nmat = 20; /* safety cap */
                            for (int bi = 0; bi < nmat; bi++) {
                                float (*rp)[4] = (float (*)[4])&model->render_pos[bi];
                                float diag_sum = rp[0][0]*rp[0][0] + rp[1][1]*rp[1][1] + rp[2][2]*rp[2][2];
                                int valid = (diag_sum > 0.001f);
                                fprintf(fp, "  bone[%d] trans=(%8.1f,%8.1f,%8.1f) diag=(%6.3f,%6.3f,%6.3f) %s\n",
                                        bi, rp[3][0], rp[3][1], rp[3][2],
                                        rp[0][0], rp[1][1], rp[2][2],
                                        valid ? "OK" : "*** ZERO ***");
                            }
                        } else {
                            fprintf(fp, "  bone dump skipped (set GE007_DEBUG_DUMP_BONES=1 for live dyn matrices)\n");
                        }
                    } else {
                        fprintf(fp, "  *** render_pos = NULL ***\n");
                    }

                    /* Check rwdata unk00/unk01 (the struct mismatch field) */
                    if (debugDumpEnvFlag("GE007_DEBUG_DUMP_RWDATA") && model->obj && model->obj->RootNode) {
                        u8 *rwbytes = (u8 *)modelGetNodeRwData(model, model->obj->RootNode);
                        if (rwbytes) {
                            fprintf(fp, "  rwdata byte0=%d byte1=%d byte2=%d (unk00/unk01/unk02)\n",
                                    rwbytes[0], rwbytes[1], rwbytes[2]);
                        }
                    }
                } else {
                    fprintf(fp, "  model=NULL\n");
                }

                /* Fog culling info */
                {
                    coord3d *ppos = bondviewGetCurrentPlayersPosition();
                    if (ppos) {
                        f32 dx = prop->pos.x - ppos->x;
                        f32 dz = prop->pos.z - ppos->z;
                        f32 dist = sqrtf(dx*dx + dz*dz);
                        fprintf(fp, "  dist_to_player=%.1f\n", dist);
                    }
                }
            }
        } else {
            fprintf(fp, "  g_ChrSlots=NULL (no chr data)\n");
        }
    }

    fprintf(fp, "\n=== END DUMP ===\n");

    /* AUDIT-0067: never report a dump as saved after a failed write/flush/close.
     * Detect accumulated buffered-write errors (ferror) and the final close,
     * delete the partial file, and surface the failure instead of "DUMP SAVED". */
    {
        int writeErr = ferror(fp);
        int closeErr = (fclose(fp) != 0);
        if (writeErr || closeErr) {
            /* errno is only meaningful for the close failure (ferror does not set
             * it, and a clean fclose leaves it stale). */
            int e = closeErr ? errno : 0;
            remove(s_dumpPath);
            if (e) {
                fprintf(stderr, "[DUMP] FAILED to write %s at frame %d: %s\n",
                        s_dumpPath, g_frame_count_diag, strerror(e));
            } else {
                fprintf(stderr, "[DUMP] FAILED to write %s at frame %d (stream write error)\n",
                        s_dumpPath, g_frame_count_diag);
            }
            snprintf(s_dumpPath, sizeof(s_dumpPath), "DUMP FAILED (write/close)");
            s_overlayTimer = 180;
            {
                extern void platformSetWindowTitle(const char *title);
                platformSetWindowTitle("GE007 - DUMP FAILED (write error)");
            }
            return;
        }
    }

    fprintf(stderr, "[DUMP] Wrote %s at frame %d\n", s_dumpPath, g_frame_count_diag);
    s_overlayTimer = 180; /* Show overlay for ~3 seconds at 60fps */

    /* Update window title to confirm dump */
    {
        extern void platformSetWindowTitle(const char *title);
        char title[300];
        snprintf(title, sizeof(title), "GE007 - DUMP SAVED: %s (frame %d)", s_dumpPath, g_frame_count_diag);
        platformSetWindowTitle(title);
    }
}

/* Called from lvl.c each frame to restore title after overlay timeout */
void debugDumpOverlayTick(void) {
    if (s_overlayTimer > 0) {
        s_overlayTimer--;
        if (s_overlayTimer == 0) {
            extern void platformSetWindowTitle(const char *title);
            platformSetWindowTitle("MGB64");
        }
    }
}
