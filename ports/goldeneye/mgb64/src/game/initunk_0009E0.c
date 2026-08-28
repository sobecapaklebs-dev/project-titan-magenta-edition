#include <ultra64.h>
#include "objecthandler.h"

void sub_GAME_7F0009E0(struct bondstruct_unk_animation_related* animBuffer, OSMesgQueue* mq, uintptr_t unused) {
    D_80036414 = animBuffer; // Data structure with offset and two tables
    D_80036418 = (uintptr_t)mq;   // Unused message queue
    D_8003641C = unused; // Some unused array
}
