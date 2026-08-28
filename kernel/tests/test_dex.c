#include "titan_dex.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

unsigned int TitanVideo_GetLastFrameGsLoadPercent(void)
{
    return 9u;
}

int main(void)
{
    system("rm -rf /tmp/project_titan_dex_test");
    assert(mkdir("/tmp/project_titan_dex_test", 0700) == 0);

    TitanDex_Initialize();
    assert(TitanDex_IsEnabled() == 0);
    assert(TitanDex_IsOverlayEnabled() == 1);
    assert(TitanDex_SetEnabled(1) == 1);
    assert(TitanDex_IsEnabled() == 1);
    assert(TitanDex_SetOverlayEnabled(0) == 1);
    assert(TitanDex_IsOverlayEnabled() == 0);

    TitanDex_Initialize();
    assert(TitanDex_IsEnabled() == 1);
    assert(TitanDex_IsOverlayEnabled() == 0);
    assert(TitanDex_SetOverlayEnabled(1) == 1);
    assert(TitanDex_SetEnabled(0) == 1);

    TitanDex_Initialize();
    assert(TitanDex_IsEnabled() == 0);
    assert(TitanDex_IsOverlayEnabled() == 1);
    puts("DEX persistence tests passed");
    return 0;
}
