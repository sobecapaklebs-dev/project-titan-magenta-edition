/**
file bgroomtrans.c
*/

#include <ultra64.h>
#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdlib.h>
#include <gbi_extension.h>
#endif
#include "player.h"
#include "bg.h"
#include "unk_0BC530.h"
#include "matrixmath.h"

#ifdef NATIVE_PORT
extern int g_frame_count_diag;
#endif

#ifdef VERSION_EU
#define AMT300 100
#else
#define AMT300 300
#endif

#ifdef NATIVE_PORT
/* Reserved matrix-slot band for the widescreen widen's DRAW-ONLY rooms
 * (bg.c bgApplyWidescreenDrawOnlyWiden). Draw-only rooms render but must not
 * touch sim-hashed state: the normal allocator records its slot id in
 * g_BgRoomInfo[room].field_36 (inside the FID-0012-hashed region) and shares
 * the AMT300 slot space with faithful rooms (interleaving could shift their
 * slot ids). Band slots live ABOVE AMT300 so faithful allocation is untouched
 * byte-for-byte, and the band setup never writes field_36. roomIndices[] IS
 * kept for band slots so impact attribution (roomMatrixRoomFromAddress) still
 * resolves the room. Sized for the worst per-frame draw-only count; overflow
 * reuses the last slot (visual-only artifact) with a one-time warning. */
#define ROOMMTX_DRAWONLY_BAND 32
#define ROOMMTX_TOTAL (AMT300 + ROOMMTX_DRAWONLY_BAND)
#else
#define ROOMMTX_TOTAL AMT300
#endif

// bss
/**
 * EU .bss 8007DC90
*/
u8 roomStatusFlags[ROOMMTX_TOTAL] = {0};

s32 roomIndices[ROOMMTX_TOTAL] = {0}; //mtxbufferroom
s32 roomOwners[ROOMMTX_TOTAL] = {0};
Mtx roomMatrices[ROOMMTX_TOTAL] = {0};
#ifdef NATIVE_PORT
static Mtxf roomMatricesF[ROOMMTX_TOTAL];
static Mtxf roomMatricesFVisibilityCompensated[ROOMMTX_TOTAL];
static coord3d roomMatrixBases[ROOMMTX_TOTAL];
static s32 roomMatrixBasisValid[ROOMMTX_TOTAL];

int roomMatrixContainsAddress(const void *addr)
{
    uintptr_t p = (uintptr_t)addr;
    uintptr_t fixed_start = (uintptr_t)&roomMatrices[0];
    uintptr_t fixed_end = fixed_start + sizeof(roomMatrices);
    uintptr_t float_start = (uintptr_t)&roomMatricesF[0];
    uintptr_t float_end = float_start + sizeof(roomMatricesF);
    uintptr_t compensated_start = (uintptr_t)&roomMatricesFVisibilityCompensated[0];
    uintptr_t compensated_end = compensated_start + sizeof(roomMatricesFVisibilityCompensated);
    return (p >= fixed_start && p < fixed_end) ||
           (p >= float_start && p < float_end) ||
           (p >= compensated_start && p < compensated_end);
}

int roomMatrixRoomFromAddress(const void *addr)
{
    uintptr_t p = (uintptr_t)addr;
    uintptr_t fixed_start = (uintptr_t)&roomMatrices[0];
    uintptr_t fixed_end = fixed_start + sizeof(roomMatrices);
    uintptr_t float_start = (uintptr_t)&roomMatricesF[0];
    uintptr_t float_end = float_start + sizeof(roomMatricesF);
    uintptr_t compensated_start = (uintptr_t)&roomMatricesFVisibilityCompensated[0];
    uintptr_t compensated_end = compensated_start + sizeof(roomMatricesFVisibilityCompensated);
    size_t index;

    if (p >= fixed_start && p < fixed_end) {
        index = (size_t)(p - fixed_start) / sizeof(roomMatrices[0]);
        return roomIndices[index];
    }
    if (p >= float_start && p < float_end) {
        index = (size_t)(p - float_start) / sizeof(roomMatricesF[0]);
        return roomIndices[index];
    }
    if (p >= compensated_start && p < compensated_end) {
        index = (size_t)(p - compensated_start) / sizeof(roomMatricesFVisibilityCompensated[0]);
        return roomIndices[index];
    }
    return -1;
}
#endif


/**
 * Initialize room and player-related data structures.
 * Sets all rooms and players to an initial state.
 *
 * Address: 0x7F0BC530
 */
void initializeRoomData(void)
{
    int i;

    for (i=0; i<getPlayerCount(); i++)
    {
        g_playerPointers[i]->curRoomIndex = -1;
    }

    for (i=0; i<ROOMMTX_TOTAL; i++)
    {
      roomIndices[i] = -1;
      roomStatusFlags[i] = 2;

      roomOwners[i] = -1;
#ifdef NATIVE_PORT
      roomMatrixBasisValid[i] = 0;
#endif


    }

    for (i=0; i<getMaxNumRooms(); ++i)
    {
        g_BgRoomInfo[i].field_36 = -1;
    }
}

void invalidateRoomMatrices(void)
{
    int i;

    for (i = 0; i < ROOMMTX_TOTAL; i++)
    {
        roomIndices[i] = -1;
        roomStatusFlags[i] = 2;
        roomOwners[i] = -1;
#ifdef NATIVE_PORT
        roomMatrixBasisValid[i] = 0;
#endif
    }

    for (i = 0; i < getMaxNumRooms(); ++i)
    {
        g_BgRoomInfo[i].field_36 = -1;
    }
}


/**
 * Set the player's room field.
 *
 * Address: 0x7F0BC624
 */
void setPlayerRoomField(s32 roomIndex) {
  g_CurrentPlayer->curRoomIndex = roomIndex;
}


/**
 * Assigns a room index to a specific room ID.
 *
 * Address: 0x7F0BC634
 */
void assignRoomIndexToRoomID(int mtx,int room)
{
#ifdef DEBUG
    //check we are clear first before assignment
    assert(g_BgRoomInfo[room].mtxid == -1);
    assert(mtxbufferroom[mtx] == -1);
#endif

    g_BgRoomInfo[room].field_36 = mtx;
    roomIndices[mtx] = room;
#ifdef NATIVE_PORT
    roomMatrixBasisValid[mtx] = 0;
#endif
}


/**
 * Removes the room index assignment for a specific room ID.
 *
 * Address: 0x7F0BC660
 */
void removeRoomIndexFromRoomID(int mtx,int room)
{
#ifdef DEBUG
    // check the requested mtx is assigned before removing
    assert(g_BgRoomInfo[room].mtxid == mtx);
    assert(mtxbufferroom[mtx] == room);
#endif

    g_BgRoomInfo[room].field_36 = -1;
    roomIndices[mtx] = -1;
#ifdef NATIVE_PORT
    roomMatrixBasisValid[mtx] = 0;
#endif
}


/**
 * Resets a room's state to its initial condition.
 *
 * Address: 0x7F0BC690
 */
void resetRoomState(int roomIndex)
{
    if (roomIndices[roomIndex] != -1) {
        removeRoomIndexFromRoomID(roomIndex,roomIndices[roomIndex]);
    }
    roomStatusFlags[roomIndex] = 2;
    roomOwners[roomIndex] = -1;
}


/**
 * Finds and returns the first available room index.
 * Returns 0 if no available room is found.
 *
 * Address: 0x7F0BC6F0
 */
s32 findAvailableRoomIndex(void)
{
    s32 i;

    for (i = 0; i<AMT300; i++)
    {
        if (((s32) roomStatusFlags[i] >= 2) && (roomOwners[i] == -1))
        {
            return i;
        }
    }
    return 0;
}


/**
 * Updates the status flags for rooms, resetting those that are inactive.
 *
 * NTSC address 0x7F0BC7D4.
 */
void updateRoomStatusFlags(void)
{
    s32 i;

    for(i = 0; i<AMT300; ++i)
    {
        if (roomOwners[i] > -1)
        {
            roomStatusFlags[i]++;

            if (roomStatusFlags[i] >= 2)
            {
                resetRoomState(i);
            }
        }
    }
}




/**
 * Manages room index allocation and matrix setup for a given room.
 *
 * NTSC address 0x7F0BC85C.
 */
static void roomMatrixFillSlot(s32 mtx, s32 room);

#ifdef NATIVE_PORT
/* Draw-only room-matrix band setup (widescreen widen frames only): allocate a
 * per-frame slot from the reserved band ABOVE AMT300 and fill it, WITHOUT
 * touching g_BgRoomInfo[room].field_36 (sim-hashed) or the faithful slot space.
 * roomIndices[] is written so roomMatrixRoomFromAddress still attributes
 * impacts on this room's geometry. The per-frame counter self-resets on the
 * frame counter, so split-screen passes within one frame share the band. */
static s32 roomMatrixDrawOnlySetup(s32 room)
{
    static s32 frame_tag = -1;
    static s32 used = 0;
    s32 mtx;

    if (frame_tag != g_frame_count_diag) {
        frame_tag = g_frame_count_diag;
        used = 0;
    }
    if (used >= ROOMMTX_DRAWONLY_BAND) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            fprintf(stderr,
                    "[ROOM-MTX] draw-only matrix band overflow (>%d slots/frame); reusing last slot\n",
                    ROOMMTX_DRAWONLY_BAND);
        }
        used = ROOMMTX_DRAWONLY_BAND - 1;
    }
    mtx = AMT300 + used;
    used++;
    roomIndices[mtx] = room;
    roomMatrixFillSlot(mtx, room);
    return mtx;
}
#endif

s32 setupRoomTransformationMatrix(s32 room)
{
    s32 mtx;
#ifdef NATIVE_PORT
    s32 basisMatches = FALSE;

    {
        extern u8 g_BgRoomDrawOnly[];
        extern s32 bgWidescreenWidenRanThisFrameGet(void);
        if (g_BgRoomDrawOnly[room] && bgWidescreenWidenRanThisFrameGet()) {
            return roomMatrixDrawOnlySetup(room);
        }
    }
#endif

    mtx = g_BgRoomInfo[room].field_36;//mtxid

#ifdef NATIVE_PORT
    if (mtx != -1 && roomMatrixBasisValid[mtx])
    {
        /* Native authored intros can switch current_model_pos while the owner
         * room stays unchanged. Treat the room basis as part of the cache key. */
        basisMatches =
            roomMatrixBases[mtx].f[0] == g_CurrentPlayer->current_model_pos.f[0] &&
            roomMatrixBases[mtx].f[1] == g_CurrentPlayer->current_model_pos.f[1] &&
            roomMatrixBases[mtx].f[2] == g_CurrentPlayer->current_model_pos.f[2];
    }
#endif

    if ((mtx == -1) || (g_CurrentPlayer->curRoomIndex != roomOwners[mtx])
#ifdef NATIVE_PORT
        || !basisMatches
#endif
        )
    {
        if (mtx != -1)
        {
            removeRoomIndexFromRoomID(mtx, room);
        }

        mtx = findAvailableRoomIndex();
        assignRoomIndexToRoomID(mtx, room);

        roomStatusFlags[mtx] = 0;
#ifdef DEBUG
        assert(g_BgRoomInfo[room].mtxid == mtx);
        assert(mtxbufferroom[mtx] == room);
#endif
    }
    else
    {
        roomStatusFlags[mtx] = 0;
        #ifdef DEBUG
        assert(g_BgRoomInfo[room].mtxid == mtx);
        assert(mtxbufferroom[mtx] == room);
        #endif
        return mtx;
    }

    roomMatrixFillSlot(mtx, room);

    return mtx;
}

/* Build + store the room transform for slot mtx (owner, fixed + float
 * matrices, basis cache). Shared by the faithful allocator above and the
 * draw-only band setup. */
static void roomMatrixFillSlot(s32 mtx, s32 room)
{
    Mtxf roomTransformMatrix;

    roomOwners[mtx] = g_CurrentPlayer->curRoomIndex;

    matrix_4x4_set_identity(&roomTransformMatrix);

    // set room size according to level scaling
    roomTransformMatrix.m[0][0] = room_data_float2;
    roomTransformMatrix.m[1][1] = room_data_float2;
    roomTransformMatrix.m[2][2] = room_data_float2;

    // room translation to position it relative to the player
    roomTransformMatrix.m[3][0] = (ptr_bgdata_room_fileposition_list[room].pos.f[0] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[0];
    roomTransformMatrix.m[3][1] = (ptr_bgdata_room_fileposition_list[room].pos.f[1] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[1];
    roomTransformMatrix.m[3][2] = (ptr_bgdata_room_fileposition_list[room].pos.f[2] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[2];

#ifdef NATIVE_PORT
    if ((getenv("GE007_TRACE_SHARDS") && g_frame_count_diag <= 1)
        || (getenv("GE007_TRACE_CAMERA") && g_frame_count_diag <= 3)) {
        static int room_mtx_log_count = 0;
        if (room_mtx_log_count < 40 || room == 75) {
            fprintf(stderr,
                    "[ROOM-MTX] frame=%d room=%d curRoom=%d mtx=%d owner=%d scale=%.6f "
                    "roompos=(%.1f,%.1f,%.1f) curModel=(%.1f,%.1f,%.1f) trans=(%.1f,%.1f,%.1f)\n",
                    g_frame_count_diag, room, g_CurrentPlayer->curRoomIndex, mtx, roomOwners[mtx], room_data_float2,
                    ptr_bgdata_room_fileposition_list[room].pos.f[0],
                    ptr_bgdata_room_fileposition_list[room].pos.f[1],
                    ptr_bgdata_room_fileposition_list[room].pos.f[2],
                    g_CurrentPlayer->current_model_pos.f[0],
                    g_CurrentPlayer->current_model_pos.f[1],
                    g_CurrentPlayer->current_model_pos.f[2],
                    roomTransformMatrix.m[3][0],
                    roomTransformMatrix.m[3][1],
                    roomTransformMatrix.m[3][2]);
            room_mtx_log_count++;
        }
    }
#endif

    matrix_4x4_f32_to_s32(roomTransformMatrix.m, roomMatrices[mtx].m);
#ifdef NATIVE_PORT
    roomMatricesF[mtx] = roomTransformMatrix;
    roomMatrixBases[mtx] = g_CurrentPlayer->current_model_pos;
    roomMatrixBasisValid[mtx] = 1;
#endif
}



/**
 * Updates the display list with the room matrix for a specific room roomID.
 *
 * Address: 0x7F0BC9C4
 */
Gfx * applyRoomMatrixToDisplayList(Gfx *gdl,int roomID)
{
    s32 roomIndex;

    roomIndex = setupRoomTransformationMatrix(roomID);
#ifdef NATIVE_PORT
    {
        static int use_float_room_mtx = -1;
        if (use_float_room_mtx < 0) {
            const char *fixed_env = getenv("GE007_FIXED_ROOM_MTX");
            const char *float_env = getenv("GE007_FLOAT_ROOM_MTX");

            if (fixed_env != NULL && fixed_env[0] != '\0' && fixed_env[0] != '0') {
                use_float_room_mtx = 0;
            } else if (float_env != NULL && float_env[0] != '\0' && float_env[0] == '0') {
                use_float_room_mtx = 0;
            } else {
                use_float_room_mtx = 1;
            }
        }
        if (use_float_room_mtx) {
            gSPMatrix(gdl++, &roomMatricesF[roomIndex],
                      G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH | G_MTX_FLOAT_PORT);
            return gdl;
        }
    }
#endif
    gSPMatrix(gdl++, &roomMatrices[roomIndex], G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
    return gdl;
}

#ifdef NATIVE_PORT
static int roomImpactVisibilityCompensationEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *value = getenv("GE007_BULLET_IMPACT_INV_VIS_SCALE");
        enabled = (value != NULL && value[0] != '\0' && value[0] == '0') ? 0 : 1;
    }

    return enabled;
}

Gfx *applyRoomMatrixToDisplayListForWorldImpact(Gfx *gdl, int roomID)
{
    s32 roomIndex;
    f32 vis_scale;

    if (!roomImpactVisibilityCompensationEnabled()) {
        return applyRoomMatrixToDisplayList(gdl, roomID);
    }

    vis_scale = bgGetLevelVisibilityScale();
    if (vis_scale == 0.0f || vis_scale == 1.0f) {
        return applyRoomMatrixToDisplayList(gdl, roomID);
    }

    roomIndex = setupRoomTransformationMatrix(roomID);
    roomMatricesFVisibilityCompensated[roomIndex] = roomMatricesF[roomIndex];
    matrix_scalar_multiply_3(1.0f / vis_scale, &roomMatricesFVisibilityCompensated[roomIndex].m[0][0]);
    gSPMatrix(gdl++,
              &roomMatricesFVisibilityCompensated[roomIndex],
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH | G_MTX_FLOAT_PORT);
    return gdl;
}
#endif


/**
 * Returns the position of a room by its roomID.
 *
 * Address: 0x7f0bca14
 */
struct coord3d* getRoomPositionByIndex(s32 roomID)
{
    return &ptr_bgdata_room_fileposition_list[roomID].pos;
}

/**
 * Retrieves and scales the position of a room by its roomID.
 *
 * Address: 0x7F0BCA34
 */
void getRoomPositionScaledByIndex(s32 roomID, coord3d *scaledPos)
{
    scaledPos->x = ptr_bgdata_room_fileposition_list[roomID].pos.x * room_data_float2;
    scaledPos->y = ptr_bgdata_room_fileposition_list[roomID].pos.y * room_data_float2;
    scaledPos->z = ptr_bgdata_room_fileposition_list[roomID].pos.z * room_data_float2;
}
