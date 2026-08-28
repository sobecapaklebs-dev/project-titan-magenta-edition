/*
 * stan_roomset.c — see stan_roomset.h.
 *
 * Byte-faithful reconstruction of sub_GAME_7F0AF20C's roomset filter loop
 * (retail ASM src/game/stan.c:1063-1086, .L7F0AF3CC-.L7F0AF410). The byte
 * reader mirrors the instruction sequence exactly:
 *   li   $s6, 255              # 0xFF terminator sentinel
 *   li   $a0, 4                # entry cap
 *   .L7F0AF3CC: lbu $t7,($t6)  # byte load, roomset[0]
 *   beq  $s6, $t7, reject      # empty list -> room not listed
 *   .L7F0AF3E4:
 *     bnel $s5, $v1, next      # room ($s5) != roomset[j] -> keep scanning
 *     lbu  $v1, 1($v0)         #   next byte (stride +1)
 *     b    accept              # room == roomset[j] -> in the set
 *   .L7F0AF3F8: addiu $s0,$s0,1 / addiu $v0,$v0,1  # j++, ptr += 1 byte
 *     beq  $s6, $v1, reject    # 0xFF -> stop, not listed
 *     bne  $s0, $a0, .L7F0AF3E4 # j != 4 -> loop
 */
#include "stan_roomset.h"

int stanRoomsetContainsRoom(const unsigned char *roomset, int room)
{
    int j;
    for (j = 0; j < 4 && roomset[j] != 0xFF; j++) {
        if ((int)roomset[j] == room) {
            return 1;
        }
    }
    return 0;
}

int stanRoomsetContainsRoomLegacyS32(const int *roomset, int room)
{
    int j;
    for (j = 0; roomset[j] != -1 && j < 4; j++) {
        if (roomset[j] == room) {
            return 1;
        }
    }
    return 0;
}

int stanRoomsetFilterAdmitsRoom(const void *roomset, int room, int legacy)
{
    if (legacy) {
        return stanRoomsetContainsRoomLegacyS32((const int *)roomset, room);
    }
    return stanRoomsetContainsRoom((const unsigned char *)roomset, room);
}
