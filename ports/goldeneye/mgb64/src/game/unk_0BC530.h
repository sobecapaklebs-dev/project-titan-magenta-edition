#ifndef _UNK_0BC530_H_
#define _UNK_0BC530_H_
#include <ultra64.h>

void setPlayerRoomField(s32 room);
void invalidateRoomMatrices(void);
void updateRoomStatusFlags(void);
void getRoomPositionScaledByIndex(s32 index, coord3d *out);
Gfx * applyRoomMatrixToDisplayList(Gfx *DL,int index);
#ifdef NATIVE_PORT
Gfx *applyRoomMatrixToDisplayListForWorldImpact(Gfx *DL, int index);
#endif
struct coord3d* getRoomPositionByIndex(s32 index);
int roomMatrixRoomFromAddress(const void *addr);

#endif
