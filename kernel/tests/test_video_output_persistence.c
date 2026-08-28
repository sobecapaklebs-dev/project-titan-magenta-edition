#include "titan_video_output.h"
#include "titan_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_progressive;
static unsigned int g_switch_count;

TitanResult TitanVideo_SetProgressiveOutput(int enabled)
{
    g_progressive = enabled != 0;
    g_switch_count++;
    return TITAN_RESULT_OK;
}

static void require_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "video-persistence: FAIL: %s\n", message);
        exit(1);
    }
}

int main(void)
{
    (void)system("rm -rf /tmp/project_titan_video_persistence_test");
    require_true(
        mkdir("/tmp/project_titan_video_persistence_test", 0777) == 0,
        "cannot create test root"
    );

    TitanVideoOutput_Initialize();
    require_true(!TitanVideoOutput_IsShellProgressive(), "default must be interlaced");

    require_true(
        TitanVideoOutput_SetShellProgressive(1) == TITAN_RESULT_OK,
        "cannot enable 720p"
    );
    require_true(TitanVideoOutput_SavePersistentSetting(), "cannot persist 720p");

    require_true(
        TitanVideoOutput_SetShellProgressive(0) == TITAN_RESULT_OK,
        "cannot return to interlaced before reload"
    );
    require_true(TitanVideoOutput_LoadPersistentSetting(), "cannot reload 720p preference");
    require_true(TitanVideoOutput_IsShellProgressive(), "720p preference was not restored");
    require_true(g_progressive == 1, "renderer did not receive restored 720p mode");

    require_true(
        TitanVideoOutput_SetShellProgressive(0) == TITAN_RESULT_OK,
        "cannot select interlaced"
    );
    require_true(TitanVideoOutput_SavePersistentSetting(), "cannot persist interlaced mode");
    require_true(
        TitanVideoOutput_SetShellProgressive(1) == TITAN_RESULT_OK,
        "cannot perturb mode before second reload"
    );
    require_true(TitanVideoOutput_LoadPersistentSetting(), "cannot reload interlaced preference");
    require_true(!TitanVideoOutput_IsShellProgressive(), "interlaced preference was not restored");
    require_true(g_progressive == 0, "renderer did not receive restored interlaced mode");
    require_true(g_switch_count >= 6u, "video mode transitions were not exercised");

    puts("video-persistence: OK");
    return 0;
}
