#include "titan_game_gsm.h"
#include "titan_storage_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

int main(void)
{
    const char *startup = "SLUS_999.99";
    FILE *file;
    int encoded;

    assert(system("rm -rf /tmp/project_titan_game_gsm_test") == 0);
    assert(mkdir("/tmp/project_titan_game_gsm_test", 0700) == 0);
    assert(chdir("/tmp/project_titan_game_gsm_test") == 0);
    assert(mkdir("mass:", 0700) == 0);
    assert(mkdir("mass:/TITAN", 0700) == 0);

    assert(TitanGameGsm_IsValidStartup(startup));
    assert(!TitanGameGsm_IsValidStartup("../BAD"));
    assert(!TitanGameGsm_IsValidStartup("mass:/BAD"));

    TitanGameGsm_InvalidateCache();
    assert(TitanGameGsm_GetMode(startup) == TITAN_PER_GAME_GSM_DEFAULT);

    assert(TitanGameGsm_SetMode(startup, TITAN_PER_GAME_GSM_480P));
    assert(file_exists("mass:/TITAN/games/SLUS_999.99.gsm"));
    TitanGameGsm_InvalidateCache();
    assert(TitanGameGsm_GetMode(startup) == TITAN_PER_GAME_GSM_480P);
    assert(strcmp(
        TitanGameGsm_GetNeutrinoArgument(TITAN_PER_GAME_GSM_480P),
        "-gsm=fp2"
    ) == 0);

    assert(TitanGameGsm_SetMode(startup, TITAN_PER_GAME_GSM_720P));
    TitanGameGsm_InvalidateCache();
    assert(TitanGameGsm_GetMode(startup) == TITAN_PER_GAME_GSM_720P);
    assert(TitanGameGsm_GetNeutrinoArgument(TITAN_PER_GAME_GSM_720P) == 0);

    assert(TitanGameGsm_SetMode(startup, TITAN_PER_GAME_GSM_1080I));
    TitanGameGsm_InvalidateCache();
    assert(TitanGameGsm_GetMode(startup) == TITAN_PER_GAME_GSM_1080I);
    assert(strcmp(
        TitanGameGsm_GetNeutrinoArgument(TITAN_PER_GAME_GSM_1080I),
        "-gsm=1080ix2"
    ) == 0);

    file = fopen("mass:/TITAN/games/SLUS_999.99.gsm", "rb");
    assert(file != 0);
    encoded = fgetc(file);
    fclose(file);
    assert(encoded == '3');

    assert(TitanGameGsm_SetMode(startup, TITAN_PER_GAME_GSM_DEFAULT));
    assert(file_exists("mass:/TITAN/games/SLUS_999.99.gsm"));
    file = fopen("mass:/TITAN/games/SLUS_999.99.gsm", "rb");
    assert(file != 0);
    encoded = fgetc(file);
    fclose(file);
    assert(encoded == '0');
    TitanGameGsm_InvalidateCache();
    assert(TitanGameGsm_GetMode(startup) == TITAN_PER_GAME_GSM_DEFAULT);
    assert(TitanGameGsm_GetNeutrinoArgument(TITAN_PER_GAME_GSM_DEFAULT) == 0);


    assert(mkdir("mass:/CFG", 0700) == 0);
    file = fopen("mass:/CFG/SLUS_999.99.cfg", "wb");
    assert(file != 0);
    fputs("$Compatibility=3\r\n$GSMSource=0\r\n$EnableGSM=1\r\n$GSMVMode=2\r\n$GSMXOffset=7\r\n", file);
    fclose(file);

    assert(TitanGameGsm_PrepareUlGoldenConfig(startup, TITAN_PER_GAME_GSM_720P));
    file = fopen("mass:/CFG/SLUS_999.99.cfg", "rb");
    assert(file != 0);
    {
        char cfg[512];
        size_t got = fread(cfg, 1u, sizeof(cfg) - 1u, file);
        cfg[got] = '\0';
        assert(strstr(cfg, "$Compatibility=3") != 0);
        assert(strstr(cfg, "$GSMSource=1") != 0);
        assert(strstr(cfg, "$EnableGSM=1") != 0);
        assert(strstr(cfg, "$GSMVMode=10") != 0);
        assert(strstr(cfg, "$GSMXOffset=") == 0);
    }
    fclose(file);

    assert(TitanGameGsm_PrepareUlGoldenConfig(startup, TITAN_PER_GAME_GSM_DEFAULT));
    file = fopen("mass:/CFG/SLUS_999.99.cfg", "rb");
    assert(file != 0);
    {
        char cfg[512];
        size_t got = fread(cfg, 1u, sizeof(cfg) - 1u, file);
        cfg[got] = '\0';
        assert(strstr(cfg, "$GSMSource=1") != 0);
        assert(strstr(cfg, "$EnableGSM=0") != 0);
        assert(strstr(cfg, "$GSMVMode=") == 0);
    }
    fclose(file);

    puts("Titan per-game GSM persistence tests passed");
    return 0;
}
