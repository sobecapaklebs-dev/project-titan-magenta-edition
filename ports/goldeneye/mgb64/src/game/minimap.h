#ifndef _MINIMAP_H_
#define _MINIMAP_H_

#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>

#define MINIMAP_MAX_POLY_POINTS 12
#define MINIMAP_MAX_ROOMS 256
#define MINIMAP_MAX_POLYS 8192
#define MINIMAP_MAX_OBJECTIVE_PINS 16
#define MINIMAP_MAX_ENEMY_PINS 64
#define MINIMAP_MAX_SNAPSHOTS 4

#define MINIMAP_PIN_FLAG_OBJECT_TARGET 0x01
#define MINIMAP_PIN_FLAG_ROOM_TARGET 0x02
#define MINIMAP_PIN_FLAG_DEPOSIT_TARGET 0x04
#define MINIMAP_PIN_FLAG_FAILED 0x80

typedef enum MinimapPinKind {
    MINIMAP_PIN_OBJECTIVE = 0,
    MINIMAP_PIN_ENEMY_FIRE = 1,
    MINIMAP_PIN_PLAYER = 2
} MinimapPinKind;

typedef struct MinimapPoly {
    u16 tile_id;
    u8 room;
    u8 point_count;
    f32 y_avg;
    f32 x_min;
    f32 z_min;
    f32 x_max;
    f32 z_max;
    f32 x[MINIMAP_MAX_POLY_POINTS];
    f32 z[MINIMAP_MAX_POLY_POINTS];
} MinimapPoly;

typedef struct MinimapRoomInfo {
    u16 first_poly;
    u16 poly_count;
    f32 x_min;
    f32 z_min;
    f32 x_max;
    f32 z_max;
    f32 y_min;
    f32 y_max;
} MinimapRoomInfo;

typedef struct MinimapLevelCache {
    const MinimapPoly *polys;
    u32 poly_count;
    u32 poly_capacity;
    u32 overflow_count;
    MinimapRoomInfo rooms[MINIMAP_MAX_ROOMS];
    f32 x_min;
    f32 z_min;
    f32 x_max;
    f32 z_max;
    f32 y_min;
    f32 y_max;
    LEVEL_INDEX build_stage;
    u32 build_serial;
    u8 ready;
} MinimapLevelCache;

typedef struct MinimapPin {
    MinimapPinKind kind;
    f32 x;
    f32 y;
    f32 z;
    s16 room;
    u8 status;
    u8 alpha;
    u8 icon;
    u8 flags;
} MinimapPin;

typedef struct MinimapFrame {
    s32 player_num;
    s32 view_left;
    s32 view_top;
    s32 view_width;
    s32 view_height;
    f32 player_x;
    f32 player_y;
    f32 player_z;
    f32 player_theta_deg;
    s16 player_room;
    u16 player_tile_id;
    u8 enabled;
    u8 mode;
    u8 player_tile_valid;
    u8 objective_count;
    u8 enemy_count;
    MinimapPin objectives[MINIMAP_MAX_OBJECTIVE_PINS];
    MinimapPin enemies[MINIMAP_MAX_ENEMY_PINS];
} MinimapFrame;

typedef struct MinimapFrameQueue {
    u8 count;
    MinimapFrame frames[MINIMAP_MAX_SNAPSHOTS];
} MinimapFrameQueue;

extern s32 g_pcMinimapEnabled;
extern s32 g_pcMinimapMode;
extern s32 g_pcMinimapObjectives;
extern s32 g_pcMinimapEnemyFireReveal;
extern s32 g_pcMinimapShowAllEnemies;
extern f32 g_pcMinimapOpacity;
extern f32 g_pcMinimapSize;
extern s32 g_pcMinimapSharpOverlay;

void minimap_stage_reset(LEVEL_INDEX stage);
void minimap_build_level_cache(LEVEL_INDEX stage);
void minimap_setup_ready(void);
void minimap_tick(void);
void minimap_note_guard_fired(ChrRecord *chr, s32 hand, s32 itemid, s32 audible);
void minimap_queue_current_player_snapshot(void);

const MinimapLevelCache *minimap_get_level_cache(void);
const MinimapFrameQueue *minimap_get_frame_queue(void);
void minimap_clear_frame_queue(void);
s32 minimap_is_enabled(void);

#endif
