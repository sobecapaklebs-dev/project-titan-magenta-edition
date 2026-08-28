#include <ultra64.h>
#include <bondconstants.h>
#include <memp.h>
#include "alloc_window_pieces.h"
#include "unk_0A1DA0.h"
#include "lvl.h"
#include "player_2.h"



void alloc_shattered_window_pieces(void)
{
    s32 i;
    s32 level = lvlGetCurrentStageToLoad();

    SHATTERED_WINDOW_PIECES_BUFFER_LEN = (200 / getPlayerCount());
    if ((level == LEVELID_STREETS) || (level == LEVELID_DEPOT))
    {
        SHATTERED_WINDOW_PIECES_BUFFER_LEN = (SHATTERED_WINDOW_PIECES_BUFFER_LEN >> 1);
    }
    ptr_shattered_window_pieces = mempAllocBytesInBank(((SHATTERED_WINDOW_PIECES_BUFFER_LEN * 0x68) + 0xF) & ~0xF, MEMPOOL_STAGE);
#ifdef NATIVE_PORT
    /* The glass-shard render (sub_GAME_7F0A2C44) emits gSPVertex pointing at
     * piece+0x38. These Vtx records are written by native C at runtime, so
     * register them as PC-native vertex data; treating the buffer as N64 binary
     * byte-swaps small shard coordinates into screen-sized triangles. */
    {
        extern void gfx_register_pc_vertex_region(void *addr, size_t size);
        gfx_register_pc_vertex_region(
            ptr_shattered_window_pieces,
            (size_t)(((SHATTERED_WINDOW_PIECES_BUFFER_LEN * 0x68) + 0xF) & ~0xF));
    }
#endif
    for(i=0; i<SHATTERED_WINDOW_PIECES_BUFFER_LEN; i++)
    {
        ptr_shattered_window_pieces[i].piece = 0;
    }

    g_NextShardNum = 0;
}
