#include <ultra64.h>

#ifdef NATIVE_PORT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minimap.h"
#include "chr.h"
#include "fr.h"
#include "loadobjectmodel.h"
#include "lvl.h"
#include "objective_status.h"
#include "player.h"
#include "player_2.h"
#include "stan.h"

#define MINIMAP_REVEAL_TTL60 180
#define MINIMAP_REVEAL_STALE_FADE_TTL60 30
#define MINIMAP_BBOX_MAX 3.402823466e+38f
#define MINIMAP_OBJECTIVE_DEDUPE_DIST_SQ 90000.0f
#define MINIMAP_OBJECTIVE_MAX_RECORDS_PER_ENTRY 64

typedef struct MinimapEnemyReveal {
    s32 chrnum;
    f32 x;
    f32 y;
    f32 z;
    s16 room;
    s16 ttl60;
    s16 ttl60_max;
    u8 active;
    u8 suppressed;
} MinimapEnemyReveal;

static MinimapPoly s_minimap_polys[MINIMAP_MAX_POLYS];
static MinimapLevelCache s_minimap_cache;
static MinimapFrameQueue s_minimap_queue;
static MinimapEnemyReveal s_minimap_reveals[MINIMAP_MAX_ENEMY_PINS];
static u32 s_minimap_build_serial;
static u8 s_minimap_setup_ready;

static s32 minimap_tile_point_count(const StandTile *tile)
{
    return ((tile->tail.half >> 12) & 0xf);
}

static void minimap_reset_room_info(MinimapRoomInfo *room)
{
    room->first_poly = 0;
    room->poly_count = 0;
    room->x_min = MINIMAP_BBOX_MAX;
    room->z_min = MINIMAP_BBOX_MAX;
    room->x_max = -MINIMAP_BBOX_MAX;
    room->z_max = -MINIMAP_BBOX_MAX;
    room->y_min = MINIMAP_BBOX_MAX;
    room->y_max = -MINIMAP_BBOX_MAX;
}

static void minimap_reset_cache_bbox(MinimapLevelCache *cache)
{
    cache->x_min = MINIMAP_BBOX_MAX;
    cache->z_min = MINIMAP_BBOX_MAX;
    cache->x_max = -MINIMAP_BBOX_MAX;
    cache->z_max = -MINIMAP_BBOX_MAX;
    cache->y_min = MINIMAP_BBOX_MAX;
    cache->y_max = -MINIMAP_BBOX_MAX;
}

static s32 minimap_finite3(f32 x, f32 y, f32 z)
{
    return __builtin_isfinite(x) && __builtin_isfinite(y) && __builtin_isfinite(z);
}

static s32 minimap_pin_pos_valid(const coord3d *pos)
{
    return pos != NULL && minimap_finite3(pos->x, pos->y, pos->z);
}

static void minimap_expand_cache_bbox(MinimapLevelCache *cache, f32 x, f32 y, f32 z)
{
    if (x < cache->x_min) cache->x_min = x;
    if (z < cache->z_min) cache->z_min = z;
    if (x > cache->x_max) cache->x_max = x;
    if (z > cache->z_max) cache->z_max = z;
    if (y < cache->y_min) cache->y_min = y;
    if (y > cache->y_max) cache->y_max = y;
}

static void minimap_expand_room_bbox(MinimapRoomInfo *room, f32 x, f32 y, f32 z)
{
    if (x < room->x_min) room->x_min = x;
    if (z < room->z_min) room->z_min = z;
    if (x > room->x_max) room->x_max = x;
    if (z > room->z_max) room->z_max = z;
    if (y < room->y_min) room->y_min = y;
    if (y > room->y_max) room->y_max = y;
}

static void minimap_clear_runtime_state(void)
{
    memset(&s_minimap_queue, 0, sizeof(s_minimap_queue));
    memset(s_minimap_reveals, 0, sizeof(s_minimap_reveals));
    s_minimap_setup_ready = 0;
}

static const char *minimap_dump_path(void)
{
    const char *path = getenv("GE007_MINIMAP_DUMP");

    if (path == NULL || path[0] == '\0' || (path[0] == '0' && path[1] == '\0')) {
        return NULL;
    }

    return path;
}

static void minimap_dump_pin_json(FILE *f, const MinimapPin *pin)
{
    fprintf(f,
            "{\"kind\":%d,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
            "\"room\":%d,\"status\":%u,\"alpha\":%u,\"icon\":%u,\"flags\":%u}",
            (s32)pin->kind,
            pin->x,
            pin->y,
            pin->z,
            (s32)pin->room,
            (u32)pin->status,
            (u32)pin->alpha,
            (u32)pin->icon,
            (u32)pin->flags);
}

static void minimap_dump_json(const MinimapFrame *frame)
{
    const char *path = minimap_dump_path();
    FILE *f;
    u32 room;
    u32 pin_index;
    u32 printed_room = 0;

    if (path == NULL) {
        return;
    }

    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"stage\": %d,\n", (s32)s_minimap_cache.build_stage);
    fprintf(f, "  \"ready\": %u,\n", (u32)s_minimap_cache.ready);
    fprintf(f, "  \"setup_ready\": %u,\n", (u32)s_minimap_setup_ready);
    fprintf(f, "  \"build_serial\": %u,\n", s_minimap_cache.build_serial);
    fprintf(f, "  \"poly_count\": %u,\n", s_minimap_cache.poly_count);
    fprintf(f, "  \"poly_capacity\": %u,\n", s_minimap_cache.poly_capacity);
    fprintf(f, "  \"overflow_count\": %u,\n", s_minimap_cache.overflow_count);
    fprintf(f, "  \"bbox\": {\"x_min\": %.3f, \"z_min\": %.3f, \"x_max\": %.3f, \"z_max\": %.3f, \"y_min\": %.3f, \"y_max\": %.3f},\n",
            s_minimap_cache.x_min, s_minimap_cache.z_min,
            s_minimap_cache.x_max, s_minimap_cache.z_max,
            s_minimap_cache.y_min, s_minimap_cache.y_max);
    fprintf(f, "  \"rooms\": [\n");
    for (room = 0; room < MINIMAP_MAX_ROOMS; room++) {
        const MinimapRoomInfo *info = &s_minimap_cache.rooms[room];
        if (info->poly_count == 0) {
            continue;
        }
        if (printed_room) {
            fprintf(f, ",\n");
        }
        fprintf(f,
                "    {\"room\": %u, \"first_poly\": %u, \"poly_count\": %u, "
                "\"x_min\": %.3f, \"z_min\": %.3f, \"x_max\": %.3f, \"z_max\": %.3f, "
                "\"y_min\": %.3f, \"y_max\": %.3f}",
                room, (u32)info->first_poly, (u32)info->poly_count,
                info->x_min, info->z_min, info->x_max, info->z_max,
                info->y_min, info->y_max);
        printed_room = 1;
    }
    if (printed_room) {
        fprintf(f, "\n");
    }
    fprintf(f, "  ],\n");

    if (frame != NULL) {
        fprintf(f, "  \"player\": {\"num\": %d, \"room\": %d, \"tile_id\": %u, \"tile_valid\": %u, \"x\": %.3f, \"y\": %.3f, \"z\": %.3f, \"theta_deg\": %.3f},\n",
                frame->player_num, frame->player_room,
                (u32)frame->player_tile_id,
                (u32)frame->player_tile_valid,
                frame->player_x, frame->player_y, frame->player_z,
                frame->player_theta_deg);
        fprintf(f, "  \"snapshot_count\": %u,\n", (u32)s_minimap_queue.count);
        fprintf(f, "  \"objective_count\": %u,\n", (u32)frame->objective_count);
        fprintf(f, "  \"enemy_count\": %u,\n", (u32)frame->enemy_count);
        fprintf(f, "  \"objectives\": [");
        for (pin_index = 0; pin_index < frame->objective_count; pin_index++) {
            if (pin_index != 0) {
                fprintf(f, ", ");
            }
            minimap_dump_pin_json(f, &frame->objectives[pin_index]);
        }
        fprintf(f, "],\n");
        fprintf(f, "  \"enemies\": [");
        for (pin_index = 0; pin_index < frame->enemy_count; pin_index++) {
            if (pin_index != 0) {
                fprintf(f, ", ");
            }
            minimap_dump_pin_json(f, &frame->enemies[pin_index]);
        }
        fprintf(f, "]\n");
    } else {
        fprintf(f, "  \"player\": null,\n");
        fprintf(f, "  \"snapshot_count\": %u,\n", (u32)s_minimap_queue.count);
        fprintf(f, "  \"objective_count\": 0,\n");
        fprintf(f, "  \"enemy_count\": 0,\n");
        fprintf(f, "  \"objectives\": [],\n");
        fprintf(f, "  \"enemies\": []\n");
    }

    fprintf(f, "}\n");
    fclose(f);
}

void minimap_stage_reset(LEVEL_INDEX stage)
{
    u32 i;

    memset(&s_minimap_cache, 0, sizeof(s_minimap_cache));
    s_minimap_cache.polys = s_minimap_polys;
    s_minimap_cache.poly_capacity = MINIMAP_MAX_POLYS;
    s_minimap_cache.build_stage = stage;
    minimap_reset_cache_bbox(&s_minimap_cache);

    for (i = 0; i < MINIMAP_MAX_ROOMS; i++) {
        minimap_reset_room_info(&s_minimap_cache.rooms[i]);
    }

    minimap_clear_runtime_state();
}

void minimap_build_level_cache(LEVEL_INDEX stage)
{
    s32 room;

    s_minimap_cache.build_stage = stage;
    s_minimap_cache.poly_count = 0;
    s_minimap_cache.overflow_count = 0;
    s_minimap_cache.ready = 0;
    minimap_reset_cache_bbox(&s_minimap_cache);

    for (room = 0; room < MINIMAP_MAX_ROOMS; room++) {
        minimap_reset_room_info(&s_minimap_cache.rooms[room]);
    }

    if (standTileStart == NULL || dword_CODE_bss_8007B9DC <= 0) {
        minimap_dump_json(NULL);
        return;
    }

    for (room = 0; room < dword_CODE_bss_8007B9DC && room < MINIMAP_MAX_ROOMS; room++) {
        StandTile *tile = firststaninroom[room];

        while (tile != NULL && *(s32 *)tile != 0 && (tile->room & 0xff) == room) {
            s32 point_count = minimap_tile_point_count(tile);
            s32 i;

            if (point_count <= 0 || point_count > 10) {
                s_minimap_cache.overflow_count++;
                break;
            }

            if (s_minimap_cache.poly_count < MINIMAP_MAX_POLYS
                && point_count <= MINIMAP_MAX_POLY_POINTS) {
                MinimapPoly *poly = &s_minimap_polys[s_minimap_cache.poly_count];
                MinimapRoomInfo *room_info = &s_minimap_cache.rooms[room];
                f32 xs[MINIMAP_MAX_POLY_POINTS];
                f32 ys[MINIMAP_MAX_POLY_POINTS];
                f32 zs[MINIMAP_MAX_POLY_POINTS];
                f32 y_sum = 0.0f;
                s32 valid = 1;

                for (i = 0; i < point_count; i++) {
                    f32 x = (f32)tile->points[i].x * inv_level_scale;
                    f32 y = (f32)tile->points[i].y * inv_level_scale;
                    f32 z = (f32)tile->points[i].z * inv_level_scale;

                    if (!minimap_finite3(x, y, z)) {
                        valid = 0;
                        break;
                    }

                    xs[i] = x;
                    ys[i] = y;
                    zs[i] = z;
                    y_sum += y;
                }

                if (!valid) {
                    s_minimap_cache.overflow_count++;
                    tile = (StandTile *)((u8 *)tile + list_of_tilesizes[point_count]);
                    continue;
                }

                if (room_info->poly_count == 0) {
                    room_info->first_poly = (u16)s_minimap_cache.poly_count;
                }

                memset(poly, 0, sizeof(*poly));
                poly->tile_id = (u16)(tile->id & 0xffff);
                poly->room = (u8)room;
                poly->point_count = (u8)point_count;
                poly->x_min = MINIMAP_BBOX_MAX;
                poly->z_min = MINIMAP_BBOX_MAX;
                poly->x_max = -MINIMAP_BBOX_MAX;
                poly->z_max = -MINIMAP_BBOX_MAX;

                for (i = 0; i < point_count; i++) {
                    f32 x = xs[i];
                    f32 y = ys[i];
                    f32 z = zs[i];

                    poly->x[i] = x;
                    poly->z[i] = z;

                    if (x < poly->x_min) poly->x_min = x;
                    if (z < poly->z_min) poly->z_min = z;
                    if (x > poly->x_max) poly->x_max = x;
                    if (z > poly->z_max) poly->z_max = z;

                    minimap_expand_cache_bbox(&s_minimap_cache, x, y, z);
                    minimap_expand_room_bbox(room_info, x, y, z);
                }

                poly->y_avg = y_sum / (f32)point_count;
                room_info->poly_count++;
                s_minimap_cache.poly_count++;
            } else {
                s_minimap_cache.overflow_count++;
            }

            tile = (StandTile *)((u8 *)tile + list_of_tilesizes[point_count]);
        }
    }

    if (s_minimap_cache.poly_count > 0
        && s_minimap_cache.x_max > s_minimap_cache.x_min
        && s_minimap_cache.z_max > s_minimap_cache.z_min) {
        s_minimap_cache.ready = 1;
    }

    s_minimap_cache.build_serial = ++s_minimap_build_serial;
    minimap_dump_json(NULL);
}

void minimap_setup_ready(void)
{
    s_minimap_setup_ready = 1;
    minimap_dump_json(NULL);
}

static s32 minimap_is_suppressed_item(s32 itemid)
{
    return itemid == ITEM_WPPKSIL || itemid == ITEM_MP5KSIL;
}

static s32 minimap_chr_reveal_should_stale_fade(ChrRecord *chr)
{
    if (chr == NULL || chr->prop == NULL) {
        return 1;
    }

    return chr->actiontype == ACT_DIE
        || chr->actiontype == ACT_DEAD
        || (chr->hidden & CHRHIDDEN_REMOVE) != 0
        || (chr->chrflags & CHRFLAG_HIDDEN) != 0;
}

static ChrRecord *minimap_resolve_reveal_chr(const MinimapEnemyReveal *reveal)
{
    if (reveal == NULL || reveal->chrnum < 0) {
        return NULL;
    }

    return chrFindByLiteralId(reveal->chrnum);
}

static void minimap_stale_fade_reveal(MinimapEnemyReveal *reveal)
{
    if (reveal->ttl60 > MINIMAP_REVEAL_STALE_FADE_TTL60) {
        reveal->ttl60 = MINIMAP_REVEAL_STALE_FADE_TTL60;
    }
}

void minimap_note_guard_fired(ChrRecord *chr, s32 hand, s32 itemid, s32 audible)
{
    s32 i;
    s32 slot = -1;
    s32 suppressed = minimap_is_suppressed_item(itemid);

    (void)hand;

    if (!minimap_is_enabled() || chr == NULL || chr->prop == NULL) {
        return;
    }

    if (!g_pcMinimapShowAllEnemies && (!audible || suppressed)) {
        return;
    }

    for (i = 0; i < MINIMAP_MAX_ENEMY_PINS; i++) {
        if (s_minimap_reveals[i].active
            && s_minimap_reveals[i].chrnum == chr->chrnum) {
            slot = i;
            break;
        }
        if (slot < 0 && !s_minimap_reveals[i].active) {
            slot = i;
        }
    }

    if (slot < 0) {
        return;
    }

    s_minimap_reveals[slot].active = 1;
    s_minimap_reveals[slot].suppressed = suppressed ? 1 : 0;
    s_minimap_reveals[slot].chrnum = chr->chrnum;
    s_minimap_reveals[slot].ttl60 = MINIMAP_REVEAL_TTL60;
    s_minimap_reveals[slot].ttl60_max = MINIMAP_REVEAL_TTL60;
    s_minimap_reveals[slot].x = chr->prop->pos.x;
    s_minimap_reveals[slot].y = chr->prop->pos.y;
    s_minimap_reveals[slot].z = chr->prop->pos.z;
    s_minimap_reveals[slot].room = chr->prop->stan != NULL ? chr->prop->stan->room : -1;
}

void minimap_tick(void)
{
    s32 i;
    s32 delta = g_ClockTimer;

    if (delta <= 0) {
        return;
    }

    for (i = 0; i < MINIMAP_MAX_ENEMY_PINS; i++) {
        MinimapEnemyReveal *reveal = &s_minimap_reveals[i];
        ChrRecord *chr;

        if (!reveal->active) {
            continue;
        }

        reveal->ttl60 -= delta;
        if (reveal->ttl60 <= 0) {
            memset(reveal, 0, sizeof(*reveal));
            continue;
        }

        chr = minimap_resolve_reveal_chr(reveal);
        if (minimap_chr_reveal_should_stale_fade(chr)) {
            minimap_stale_fade_reveal(reveal);
        }

        if (chr != NULL && chr->prop != NULL) {
            reveal->x = chr->prop->pos.x;
            reveal->y = chr->prop->pos.y;
            reveal->z = chr->prop->pos.z;
            reveal->room = chr->prop->stan != NULL ? chr->prop->stan->room : reveal->room;
        }
    }
}

s32 minimap_is_enabled(void)
{
    return g_pcMinimapEnabled != 0 && g_pcMinimapSharpOverlay != 0;
}

static s32 minimap_setup_pad_valid(s32 padid)
{
    s32 i;

    if (padid < 0 || g_CurrentSetup.pads == NULL) {
        return 0;
    }

    for (i = 0; i <= padid && i < 10000; i++) {
        if (g_CurrentSetup.pads[i].plink == NULL) {
            return 0;
        }
    }

    return padid < 10000;
}

static s32 minimap_bound_pad_valid(s32 bound_pad)
{
    s32 i;

    if (bound_pad < 0 || g_CurrentSetup.boundpads == NULL) {
        return 0;
    }

    for (i = 0; i <= bound_pad && i < 10000; i++) {
        if (g_CurrentSetup.boundpads[i].plink == NULL) {
            return 0;
        }
    }

    return bound_pad < 10000;
}

static s32 minimap_resolve_pad_pos(s32 padid, coord3d *out_pos, s16 *out_room)
{
    PadRecord *pad;

    if (out_room != NULL) {
        *out_room = -1;
    }
    if (out_pos != NULL) {
        out_pos->x = 0.0f;
        out_pos->y = 0.0f;
        out_pos->z = 0.0f;
    }
    if (padid < 0) {
        return 0;
    }

    if (isNotBoundPad(padid)) {
        if (!minimap_setup_pad_valid(padid)) {
            return 0;
        }
        pad = &g_CurrentSetup.pads[padid];
    } else {
        s32 bound_pad = getBoundPadNum(padid);

        if (!minimap_bound_pad_valid(bound_pad)) {
            return 0;
        }
        pad = (PadRecord *)&g_CurrentSetup.boundpads[bound_pad];
    }

    if (out_pos != NULL) {
        *out_pos = pad->pos;
    }
    if (out_room != NULL && pad->stan != NULL) {
        *out_room = pad->stan->room;
    }

    return minimap_pin_pos_valid(out_pos);
}

static s32 minimap_resolve_object_pos(ObjectRecord *obj, coord3d *out_pos, s16 *out_room)
{
    if (out_room != NULL) {
        *out_room = -1;
    }
    if (out_pos != NULL) {
        out_pos->x = 0.0f;
        out_pos->y = 0.0f;
        out_pos->z = 0.0f;
    }
    if (obj == NULL) {
        return 0;
    }

    if (obj->prop != NULL && minimap_pin_pos_valid(&obj->prop->pos)) {
        if (out_pos != NULL) {
            *out_pos = obj->prop->pos;
        }
        if (out_room != NULL && obj->prop->stan != NULL) {
            *out_room = obj->prop->stan->room;
        }
        return 1;
    }

    if (obj->type == PROPDEF_DOOR) {
        DoorRecord *door = (DoorRecord *)obj;
        PadRecord *pad;

        if (!minimap_bound_pad_valid(door->pad)) {
            return 0;
        }
        pad = (PadRecord *)&g_CurrentSetup.boundpads[door->pad];
        if (out_pos != NULL) {
            *out_pos = pad->pos;
        }
        if (out_room != NULL && pad->stan != NULL) {
            *out_room = pad->stan->room;
        }
        return minimap_pin_pos_valid(out_pos);
    }

    return minimap_resolve_pad_pos(obj->pad, out_pos, out_room);
}

static s32 minimap_add_objective_pin(MinimapFrame *frame,
                                     s32 objective_index,
                                     OBJECTIVESTATUS status,
                                     u8 flags,
                                     const coord3d *pos,
                                     s16 room)
{
    MinimapPin *pin;
    u32 i;

    if (frame == NULL
        || pos == NULL
        || !minimap_pin_pos_valid(pos)
        || frame->objective_count >= MINIMAP_MAX_OBJECTIVE_PINS) {
        return 0;
    }

    for (i = 0; i < frame->objective_count; i++) {
        MinimapPin *existing = &frame->objectives[i];
        f32 dx;
        f32 dz;

        if (existing->icon != (u8)objective_index) {
            continue;
        }

        dx = existing->x - pos->x;
        dz = existing->z - pos->z;
        if (existing->room == room && (dx * dx + dz * dz) <= MINIMAP_OBJECTIVE_DEDUPE_DIST_SQ) {
            existing->flags |= flags;
            return 1;
        }
    }

    pin = &frame->objectives[frame->objective_count++];
    memset(pin, 0, sizeof(*pin));
    pin->kind = MINIMAP_PIN_OBJECTIVE;
    pin->x = pos->x;
    pin->y = pos->y;
    pin->z = pos->z;
    pin->room = room;
    pin->status = (u8)status;
    pin->alpha = 255;
    pin->icon = (u8)objective_index;
    pin->flags = flags;
    if (status == OBJECTIVESTATUS_FAILED) {
        pin->flags |= MINIMAP_PIN_FLAG_FAILED;
    }

    return 1;
}

static void minimap_add_objective_object_pin(MinimapFrame *frame,
                                             s32 objective_index,
                                             OBJECTIVESTATUS status,
                                             s32 tag_id)
{
    ObjectRecord *obj = objFindByTagId(tag_id);
    coord3d pos;
    s16 room;

    if (minimap_resolve_object_pos(obj, &pos, &room)) {
        minimap_add_objective_pin(frame,
                                  objective_index,
                                  status,
                                  MINIMAP_PIN_FLAG_OBJECT_TARGET,
                                  &pos,
                                  room);
    }
}

static void minimap_add_objective_pad_pin(MinimapFrame *frame,
                                          s32 objective_index,
                                          OBJECTIVESTATUS status,
                                          s32 pad_id,
                                          u8 flags)
{
    coord3d pos;
    s16 room;

    if (minimap_resolve_pad_pos(pad_id, &pos, &room)) {
        minimap_add_objective_pin(frame,
                                  objective_index,
                                  status,
                                  flags,
                                  &pos,
                                  room);
    }
}

static void minimap_copy_objective_pins(MinimapFrame *frame)
{
    s32 objective_count;
    s32 objective_index;
    DIFFICULTY difficulty = lvlGetSelectedDifficulty();

    if (!g_pcMinimapObjectives || frame == NULL) {
        return;
    }

    objective_count = objectiveGetCount();
    if (objective_count < 0) {
        return;
    }
    if (objective_count > OBJECTIVES_MAX) {
        objective_count = OBJECTIVES_MAX;
    }

    for (objective_index = 0;
         objective_index < objective_count && frame->objective_count < MINIMAP_MAX_OBJECTIVE_PINS;
         objective_index++) {
        struct objective_entry *entry = objective_ptrs[objective_index];
        OBJECTIVESTATUS status;
        MissionObjectiveRecord *objective;
        s32 guard;

        if (entry == NULL || get_difficulty_for_objective(objective_index) > difficulty) {
            continue;
        }

        status = get_status_of_objective(objective_index);
        if (status == OBJECTIVESTATUS_COMPLETE) {
            continue;
        }

        objective = (MissionObjectiveRecord *)&entry->id;
        for (guard = 0;
             objective != NULL
             && objective->type != PROPDEF_OBJECTIVE_END
             && guard < MINIMAP_OBJECTIVE_MAX_RECORDS_PER_ENTRY
             && frame->objective_count < MINIMAP_MAX_OBJECTIVE_PINS;
             guard++) {
            PropDefHeaderRecord *header = (PropDefHeaderRecord *)objective;

            switch (objective->type) {
                case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
                case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
                case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
                    minimap_add_objective_object_pin(frame,
                                                     objective_index,
                                                     status,
                                                     objective->ObjRefID);
                    break;
                case PROPDEF_OBJECTIVE_PHOTOGRAPH: {
                    struct criteria_picture *criteria = (struct criteria_picture *)objective;

                    if (criteria->flag == 0) {
                        minimap_add_objective_object_pin(frame,
                                                         objective_index,
                                                         status,
                                                         criteria->tag_id);
                    }
                    break;
                }
                case PROPDEF_OBJECTIVE_ENTER_ROOM: {
                    struct criteria_roomentered *criteria = (struct criteria_roomentered *)objective;

                    if (criteria->status == 0) {
                        minimap_add_objective_pad_pin(frame,
                                                      objective_index,
                                                      status,
                                                      (s32)criteria->pad,
                                                      MINIMAP_PIN_FLAG_ROOM_TARGET);
                    }
                    break;
                }
                case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: {
                    struct criteria_deposit *criteria = (struct criteria_deposit *)objective;

                    if (criteria->flag == 0) {
                        minimap_add_objective_pad_pin(frame,
                                                      objective_index,
                                                      status,
                                                      criteria->padid,
                                                      MINIMAP_PIN_FLAG_ROOM_TARGET | MINIMAP_PIN_FLAG_DEPOSIT_TARGET);
                    }
                    break;
                }
                default:
                    break;
            }

            objective = (MissionObjectiveRecord *)(header + sizepropdef(header));
        }
    }
}

static void minimap_copy_enemy_pins(MinimapFrame *frame)
{
    s32 i;

    if (!g_pcMinimapEnemyFireReveal && !g_pcMinimapShowAllEnemies) {
        return;
    }

    for (i = 0; i < MINIMAP_MAX_ENEMY_PINS && frame->enemy_count < MINIMAP_MAX_ENEMY_PINS; i++) {
        MinimapEnemyReveal *reveal = &s_minimap_reveals[i];
        MinimapPin *pin;
        s32 alpha;

        if (!reveal->active || reveal->ttl60_max <= 0) {
            continue;
        }

        alpha = (reveal->ttl60 * 255) / reveal->ttl60_max;
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;

        pin = &frame->enemies[frame->enemy_count++];
        memset(pin, 0, sizeof(*pin));
        pin->kind = MINIMAP_PIN_ENEMY_FIRE;
        pin->x = reveal->x;
        pin->y = reveal->y;
        pin->z = reveal->z;
        pin->room = reveal->room;
        pin->alpha = (u8)alpha;
        pin->flags = reveal->suppressed;
    }
}

void minimap_queue_current_player_snapshot(void)
{
    MinimapFrame *frame;

    if (!minimap_is_enabled()
        || !s_minimap_cache.ready
        || !s_minimap_setup_ready
        || g_CurrentPlayer == NULL
        || g_CurrentPlayer->prop == NULL
        || getPlayerCount() != 1
        || g_CurrentPlayer->mpmenuon != 0
        || g_CurrentPlayer->bonddead != 0
        || g_CurrentPlayer->watch_animation_state != 0
        || s_minimap_queue.count >= MINIMAP_MAX_SNAPSHOTS) {
        return;
    }

    frame = &s_minimap_queue.frames[s_minimap_queue.count++];
    memset(frame, 0, sizeof(*frame));
    frame->enabled = 1;
    frame->mode = (u8)g_pcMinimapMode;
    frame->player_num = get_cur_playernum();
    frame->view_left = viGetViewLeft();
    frame->view_top = viGetViewTop();
    frame->view_width = viGetViewWidth();
    frame->view_height = viGetViewHeight();
    frame->player_x = g_CurrentPlayer->prop->pos.x;
    frame->player_y = g_CurrentPlayer->prop->pos.y;
    frame->player_z = g_CurrentPlayer->prop->pos.z;
    frame->player_theta_deg = g_CurrentPlayer->vv_theta;
    frame->player_room = g_CurrentPlayer->prop->stan != NULL
        ? g_CurrentPlayer->prop->stan->room
        : g_CurrentPlayer->curRoomIndex;
    if (g_CurrentPlayer->prop->stan != NULL) {
        frame->player_tile_id = (u16)(g_CurrentPlayer->prop->stan->id & 0xffff);
        frame->player_tile_valid = 1;
    }

    minimap_copy_objective_pins(frame);
    minimap_copy_enemy_pins(frame);

    if (minimap_dump_path() != NULL) {
        minimap_dump_json(frame);
    }
}

const MinimapLevelCache *minimap_get_level_cache(void)
{
    return &s_minimap_cache;
}

const MinimapFrameQueue *minimap_get_frame_queue(void)
{
    return &s_minimap_queue;
}

void minimap_clear_frame_queue(void)
{
    memset(&s_minimap_queue, 0, sizeof(s_minimap_queue));
}

#endif
