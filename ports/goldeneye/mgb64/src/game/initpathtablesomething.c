#include <ultra64.h>
#include <bondtypes.h>
#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include "stan.h"


/**
 * NTSC address 0x7F0067C0.
*/
s32 init_pathtable_something(struct PadRecord *pad, char *tilename, struct StandTile **tile_stack)
{
    struct coord3d coord;
#ifdef NATIVE_PORT
    const char *trace_name = getenv("GE007_TRACE_TILE_NAME");
    int trace_enabled = trace_name && strcmp(trace_name, tilename) == 0;
#endif

    *tile_stack = stanMatchTileName(tilename);

    if ((*tile_stack == NULL) || (isPointInsideTriStandTileUnscaled_Maybe(*tile_stack, pad->pos.f[0], pad->pos.f[2]) == 0))
    {
        coord.f[0] = pad->pos.f[0];
        coord.f[1] = pad->pos.f[1];
        coord.f[2] = pad->pos.f[2];

        *tile_stack = sub_GAME_7F0AFB78(&coord.f[0], &coord.f[1], &coord.f[2], 0);

        if ((*tile_stack != NULL) && (walkTilesBetweenPoints_NoCallback(tile_stack, coord.f[0], coord.f[2], pad->pos.f[0], pad->pos.f[2]) != 0))
        {
#ifdef NATIVE_PORT
            if (trace_enabled) {
                fprintf(stderr,
                        "[INIT_PATH] name=%s mode=fallback pad=(%.1f,%.1f,%.1f) tile=%p room=%d snapped=(%.1f,%.1f,%.1f)\n",
                        tilename,
                        pad->pos.f[0], pad->pos.f[1], pad->pos.f[2],
                        (void *)*tile_stack, *tile_stack ? (*tile_stack)->room : -1,
                        coord.f[0], coord.f[1], coord.f[2]);
            }
#endif
            return 2;
        }

        *tile_stack = NULL;

#ifdef NATIVE_PORT
        if (trace_enabled) {
            fprintf(stderr,
                    "[INIT_PATH] name=%s mode=fallback-miss pad=(%.1f,%.1f,%.1f)\n",
                    tilename, pad->pos.f[0], pad->pos.f[1], pad->pos.f[2]);
        }
#endif
        return 0;
    }

#ifdef NATIVE_PORT
    if (trace_enabled) {
        fprintf(stderr,
                "[INIT_PATH] name=%s mode=direct pad=(%.1f,%.1f,%.1f) tile=%p room=%d\n",
                tilename, pad->pos.f[0], pad->pos.f[1], pad->pos.f[2],
                (void *)*tile_stack, *tile_stack ? (*tile_stack)->room : -1);
    }
#endif
    return 1;
}
 
