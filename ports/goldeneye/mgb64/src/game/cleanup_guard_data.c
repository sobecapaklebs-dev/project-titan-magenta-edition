#include <ultra64.h>
#include "chr.h"

#ifdef NATIVE_PORT
extern void chrpropDelist(PropRecord *prop);
#endif

void cleanupGuardData(void)
{
    int i;

    for (i = 0; i < g_NumChrSlots; i++) {
        if (g_ChrSlots[i].model != 0) {
#ifdef NATIVE_PORT
            if (g_ChrSlots[i].prop == NULL ||
                g_ChrSlots[i].prop->type != PROP_TYPE_CHR ||
                g_ChrSlots[i].prop->chr != &g_ChrSlots[i]) {
                g_ChrSlots[i].model = NULL;
                g_ChrSlots[i].chrnum = -1;
                continue;
            }
#endif
            disable_sounds_attached_to_player_then_something(g_ChrSlots[i].prop);
            chrpropDelist(g_ChrSlots[i].prop);
            chrpropDisable(g_ChrSlots[i].prop);
            chrpropFree(g_ChrSlots[i].prop);
        }
    }
}
