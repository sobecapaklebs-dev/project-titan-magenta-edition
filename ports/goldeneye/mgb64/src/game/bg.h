#ifndef _BG_H_
#define _BG_H_
#include <ultra64.h>
#include <bondgame.h>
#include <bondtypes.h>
#include <bondconstants.h>

struct levelentry
{
    s32 levelID;
    void *bg_seg_filename;
    void *bg_stan_filename;
    f32 levelscale;
    f32 visibility;
    f32 unknownfloat;
};

typedef struct s_room_info {
    // is room being rendered? boolean
    u8 room_rendered;

    // is the room a neighbor to a room being rendered? boolean
    u8 room_neighbor_to_rendered;

    u8 model_bin_loaded;

    // number of portals in between the player's room and this room.
    // is 0 if the room is not visible or if the player is standing in this room.
    u8 portals_to_room_count;

    Vtx * ptr_point_index;
    void * ptr_expanded_mapping_info;
    void * ptr_secondary_expanded_mapping_info;
    s32 csize_point_index_binary;
    s32 csize_primary_DL_binary;
    s32 csize_secondary_DL_binary;
    s32 usize_point_index_binary;
    s32 usize_primary_DL_binary;
    s32 usize_secondary_DL_binary;
    s32 cur_room_totalsize;
    void * ptr_unique_collision_points;
    s32 bitflags3;
    u8 room_loaded_mask;
    u8 field_35;
    s16 field_36;
    coord3d minbounds;
    coord3d maxbounds;
} s_room_info;

typedef struct s_bound_info 
{
    #if defined(VERSION_EU)
    //eu is 0x18 total len
    u8 roomid;
    u8 pad1;
    // could be draw order?
    s16 unk1;
    void* next;
    struct bbox2d bbox;
    
    #else
    //us is 0x1C total len
    s32 roomid;
    // could be draw order?
    s32 unk1;
    struct bbox2d bbox;
    void* next;
    #endif

    
} s_bound_info;

typedef struct bg_portal_entry
{
    u8 numPoints;
    u8 padding[3];
    coord3d point;
} bg_portal_entry;

#ifdef NATIVE_PORT
/* PC: offset_portal is a 32-bit ROM offset, not a pointer */
typedef struct bg_portal_data_entry
{
    u32 offset_portal;
    u8 connectedRoom1;
    u8 connectedRoom2;
    u8 controlbytes1;
    u8 controlbytes2;
} bg_portal_data_entry;
#else
typedef struct bg_portal_data_entry
{
    bg_portal_entry *offset_portal;
    u8 connectedRoom1;
    u8 connectedRoom2;
    u8 controlbytes1;
    u8 controlbytes2;
} bg_portal_data_entry;
#endif

#ifdef NATIVE_PORT
/* PC: ROM data uses 32-bit fields, not pointers.
 * Must match N64 struct layout (24 bytes) for ROM data parsing. */
typedef struct bg_room_data
{
    u32 pPointTableBin;
    u32 pPriMappingBin;
    u32 pSecMappingBin;
    coord3d pos;
} bg_room_data;
#else
typedef struct bg_room_data
{
    void* pPointTableBin;
    void* pPriMappingBin;
    void* pSecMappingBin;
    coord3d pos;
} bg_room_data;
#endif

#ifdef NATIVE_PORT
typedef struct BgHitRecord
{
    coord3d pos;
    coord3d normal;
    Vertex *vtx0;
    Vertex *vtx1;
    Vertex *vtx2;
    Gfx *tricmd;
    s16 tri_index;
    s16 texturenum;
    s16 texture_s;
    s16 texture_t;
} BgHitRecord;
#endif

typedef struct s_specialportal
{
    u8 levelid;
#ifdef NATIVE_PORT
    u8 portallist[16];
#else
    u8 portallist[];
#endif
} s_specialportal;

typedef struct unk_portalstruct 
{
    s32 unk0;
    s32 unk4;
    s32 unk8;
    s32 unkC;
    s32 unk10;
} unk_portalstruct;

extern struct unk_portalstruct table_for_portals[200];
extern s32 g_MaxNumRooms;
extern f32 room_data_float2;

extern bg_room_data * ptr_bgdata_room_fileposition_list;
extern s_room_info g_BgRoomInfo[];
extern Gfx *ptrDynamic_CC_RM_LUT[];
extern Gfx DL_LUT_PRIMARY_ADDFOG[];


void bgInit(void);

// sub_GAME_7F033B38 requres arg be s32
s32 sub_GAME_7F0B8FD0(s32 roomA, u8 roomB);

//f32 sub_GAME_7F0B4F9C(s32 room); // u8 not s32 for sub_GAME_7F0B2FE0
u32 bgDecompress(u8* source, u8 *target);
void delete_room_data(s32 roomID);
void load_bg_file(LEVEL_INDEX stagenum);

Gfx *bgDebugRemoved7F0B9DE4(Gfx *arg0, s32 arg1, s32 arg2);
void bgRemoved7F0B9DF4(s32 arg0);
void bgSwapConnectedRooms(s32 index);
s32 bgGetDataPortalsControlBytes1Bit1(s32 index);
void bgToggleDataPortalsContrlBytes1Bit1(s32 index, s32 toggle);
s32 bgGetDataPortalsControlBytes1Bit2(s32 arg0);
void bgClearDataPortalsControlBytes1Low2Bits(s32 index);
void bgSetDataPortalsControlBytes1Bit2(s32 index);
void sub_GAME_7F0B9A7C(s32 arg0);
void sub_GAME_7F0B9A2C(s32 arg0);
void bgRoomVisibilityRelated(void);
s32 getMaxNumRooms(void);
Gfx* bgLevelRender(Gfx *arg0);
Gfx *bgScissorCurrentPlayerView(Gfx *arg0, s32 left, s32 top, s32 width, s32 height);
Gfx* bgScissorCurrentPlayerViewDefault(Gfx* arg0);
Gfx* bgScissorCurrentPlayerViewF(Gfx* arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4);
f32 get_room_data_float1(void);
u8 getROOMID_isRendered(int roomID);
u8 getROOMID_isNeighborToRendered(s32 roomID);
#ifdef NATIVE_PORT
s32 bgMarkRoomAndPortalNeighborsRendered(s32 room);
#endif
s32 bgGet2dBboxByRoomId(s32 room_id, struct bbox2d *result);
f32 bgGetLevelVisibilityScale(void);
void sub_GAME_7F0B5CC0(struct bbox2d *a, struct bbox2d *b);
f32 get_room_data_float2(void);
/* W1.E2.T1: audited accessor for the world-space sun direction. Render code reads
 * this to keep sim/render separation to a single audited read (rail R1). */
void bgGetGlobalLightDir(f32 out[3]);
s32 bgResolveRoomByPortalPosition(s32 seed_room, const coord3d *position);
s32 sub_GAME_7F0B9CC8(s32 arg0, s32 arg1, struct coord3d *arg2, struct coord3d *arg3);
void sub_GAME_7F0B96CC(s32 arg0, struct PortalMetric *arg1);
void bgLoadFromDynamicCCRMLUT(Gfx *arg0, Gfx *arg1, enum CCRMLUT arg2);
#ifdef NATIVE_PORT
s32 bgTestLineIntersectionInRoom(coord3d *start, coord3d *dest, coord3d *delta, void *collision_entry, s32 room, BgHitRecord *output);
s32 bgTestBulletHitBackground(coord3d *start, coord3d *dest, s32 room, BgHitRecord *output);
#endif

#endif
