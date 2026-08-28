#include "titan_platform.h"

#include "titan_font.h"
#include "titan_video.h"
#include "titan_vram_boot_guard.h"
#include "titan_version.h"

#include <debug.h>
#include <kernel.h>

TitanResult TitanPlatform_Initialize(void)
{
    TitanResult result;

    result = TitanVideo_Initialize();

    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_FONT);
    result = TitanFont_Initialize();

    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    result = TitanFont_UploadAtlas();

    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    return TITAN_RESULT_OK;
}

void TitanPlatform_ShowFatalError(
    const char *stage,
    TitanResult result
)
{
    init_scr();

    scr_printf(
        "\nPROJECT TITAN\n"
        "Kernel %s\n"
        "--------------------------------\n\n",
        TITAN_KERNEL_VERSION_STRING
    );

    scr_printf(
        "KERNEL FAILURE\n"
        "stage: %s\n"
        "error code: %d\n\n",
        stage != 0 ? stage : "unknown",
        (int)result
    );

    scr_printf(
        "The kernel halted safely.\n"
    );

    SleepThread();
}
