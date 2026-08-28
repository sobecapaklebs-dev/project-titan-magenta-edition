/* binding_conflict.c — see binding_conflict.h. Pure integer logic, no SDL, no
 * savedir: the reverse owner map behind the Controls panel's move-or-reject
 * duplicate-binding policy (AUDIT-0050). */
#include "binding_conflict.h"

int bindingOwnerOf(const int *encoded, int count, int enc, int exclude) {
    int j;
    if (enc == GB_NONE || !encoded) return -1;
    for (j = 0; j < count; ++j) {
        if (j == exclude) continue;
        if (encoded[j] == enc) return j;
    }
    return -1;
}
