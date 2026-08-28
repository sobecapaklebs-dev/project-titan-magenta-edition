#include "titan_video_output.h"

#include "titan_video.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <string.h>

#define TITAN_VIDEO_SETTINGS_DIRECTORY TITAN_STORAGE_DEVICE_ROOT "/settings"
#define TITAN_VIDEO_SETTINGS_PATH TITAN_VIDEO_SETTINGS_DIRECTORY "/shell_video.cfg"
#define TITAN_VIDEO_SETTINGS_TEMP_PATH TITAN_VIDEO_SETTINGS_DIRECTORY "/shell_video.tmp"
#define TITAN_VIDEO_SETTINGS_BACKUP_PATH TITAN_VIDEO_SETTINGS_DIRECTORY "/shell_video.bak"

#if defined(__EE__) || defined(_EE)
#include <graph.h>
#include <kernel.h>
#include <osd_config.h>
#endif

typedef struct TitanVideoOutputState
{
    int initialized;
    int component_configured;
    int pal_region;
    int shell_progressive;
    TitanGameGsmMode game_gsm_mode;
} TitanVideoOutputState;

static TitanVideoOutputState g_video_output;

static int TitanVideoOutput_ReadPersistentSetting(const char *path, int *enabled)
{
    int descriptor;
    char value;
    TitanStorageIoSize size;

    if (path == 0 || enabled == 0 ||
        !TitanStorageIo_PathInfo64(path, 0, &size) || size != 1u)
    {
        return 0;
    }

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanStorageIo_ReadExact(descriptor, &value, 1u) ||
        TitanStorageIo_Close(descriptor) < 0 ||
        (value != '0' && value != '1'))
    {
        return 0;
    }

    *enabled = value == '1';
    return 1;
}

static int TitanVideoOutput_RecoverPersistentSetting(int *enabled)
{
    int value;

    if (TitanVideoOutput_ReadPersistentSetting(TITAN_VIDEO_SETTINGS_PATH, &value))
    {
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_BACKUP_PATH);
        *enabled = value;
        return 1;
    }

    if (TitanVideoOutput_ReadPersistentSetting(TITAN_VIDEO_SETTINGS_TEMP_PATH, &value))
    {
        if (TitanStorageIo_AtomicReplace(
                TITAN_VIDEO_SETTINGS_TEMP_PATH,
                TITAN_VIDEO_SETTINGS_PATH,
                TITAN_VIDEO_SETTINGS_BACKUP_PATH) &&
            TitanVideoOutput_ReadPersistentSetting(TITAN_VIDEO_SETTINGS_PATH, enabled))
        {
            TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_BACKUP_PATH);
            return 1;
        }
    }

    if (TitanVideoOutput_ReadPersistentSetting(TITAN_VIDEO_SETTINGS_BACKUP_PATH, &value))
    {
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);
        if (TitanStorageIo_PathInfo64(TITAN_VIDEO_SETTINGS_PATH, 0, 0))
        {
            TitanStorageIo_Rename(
                TITAN_VIDEO_SETTINGS_PATH,
                TITAN_VIDEO_SETTINGS_TEMP_PATH
            );
        }
        if (TitanStorageIo_Rename(
                TITAN_VIDEO_SETTINGS_BACKUP_PATH,
                TITAN_VIDEO_SETTINGS_PATH) == 0 &&
            TitanVideoOutput_ReadPersistentSetting(TITAN_VIDEO_SETTINGS_PATH, enabled))
        {
            TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);
            return 1;
        }
    }

    return 0;
}

void TitanVideoOutput_Initialize(void)
{
    memset(&g_video_output, 0, sizeof(g_video_output));
    g_video_output.game_gsm_mode = TITAN_GAME_GSM_OFF;

#if defined(__EE__) || defined(_EE)
    {
        ConfigParam config;
        int region;

        memset(&config, 0, sizeof(config));
        GetOsdConfigParam(&config);
        g_video_output.component_configured =
            config.videoOutput == VIDEO_OUTPUT_COMPONENT;

        region = graph_get_region();
        g_video_output.pal_region = region == GRAPH_MODE_PAL;
    }
#else
    g_video_output.component_configured = 1;
    g_video_output.pal_region = 0;
#endif

    g_video_output.shell_progressive = 0;
    g_video_output.initialized = 1;
}

int TitanVideoOutput_IsComponentConfigured(void)
{
    return g_video_output.component_configured;
}

const char *TitanVideoOutput_GetConfiguredOutputName(void)
{
    return g_video_output.component_configured
        ? "Component (YPbPr)"
        : "RGB";
}

const char *TitanVideoOutput_GetRegionName(void)
{
    return g_video_output.pal_region ? "PAL" : "NTSC";
}

int TitanVideoOutput_IsShellProgressive(void)
{
    return g_video_output.shell_progressive;
}

int TitanVideoOutput_CanTestProgressive(void)
{
    return g_video_output.component_configured;
}

TitanResult TitanVideoOutput_SetShellProgressive(int enabled)
{
    TitanResult result;

    if (!g_video_output.initialized)
    {
        TitanVideoOutput_Initialize();
    }

    if (enabled && !TitanVideoOutput_CanTestProgressive())
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    result = TitanVideo_SetProgressiveOutput(enabled != 0);
    if (result == TITAN_RESULT_OK)
    {
        g_video_output.shell_progressive = enabled != 0;
    }

    return result;
}


int TitanVideoOutput_LoadPersistentSetting(void)
{
    int enabled;

    if (!g_video_output.initialized)
    {
        TitanVideoOutput_Initialize();
    }

    enabled = 0;
    if (!TitanVideoOutput_RecoverPersistentSetting(&enabled))
    {
        return 1;
    }

    if (enabled && !TitanVideoOutput_CanTestProgressive())
    {
        return 0;
    }

    return TitanVideoOutput_SetShellProgressive(enabled) == TITAN_RESULT_OK;
}

int TitanVideoOutput_SavePersistentSetting(void)
{
    int descriptor;
    int verified;
    const char value = TitanVideoOutput_IsShellProgressive() ? '1' : '0';

    TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    TitanStorageIo_CreateDirectory(TITAN_VIDEO_SETTINGS_DIRECTORY);
    TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);

    descriptor = TitanStorageIo_Open(
        TITAN_VIDEO_SETTINGS_TEMP_PATH,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanStorageIo_WriteExact(descriptor, &value, 1u) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0)
    {
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);
        return 0;
    }

    if (!TitanVideoOutput_ReadPersistentSetting(
            TITAN_VIDEO_SETTINGS_TEMP_PATH,
            &verified) ||
        verified != (value == '1') ||
        !TitanStorageIo_AtomicReplace(
            TITAN_VIDEO_SETTINGS_TEMP_PATH,
            TITAN_VIDEO_SETTINGS_PATH,
            TITAN_VIDEO_SETTINGS_BACKUP_PATH))
    {
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_TEMP_PATH);
        return 0;
    }

    if (!TitanVideoOutput_ReadPersistentSetting(
            TITAN_VIDEO_SETTINGS_PATH,
            &verified) ||
        verified != (value == '1'))
    {
        TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_PATH);
        TitanStorageIo_Rename(
            TITAN_VIDEO_SETTINGS_BACKUP_PATH,
            TITAN_VIDEO_SETTINGS_PATH
        );
        return 0;
    }

    TitanStorageIo_Remove(TITAN_VIDEO_SETTINGS_BACKUP_PATH);
    return 1;
}

TitanGameGsmMode TitanVideoOutput_GetGameGsmMode(void)
{
    return g_video_output.game_gsm_mode;
}

void TitanVideoOutput_SetGameGsmMode(TitanGameGsmMode mode)
{
    if (mode < TITAN_GAME_GSM_OFF || mode >= TITAN_GAME_GSM_MODE_COUNT)
    {
        mode = TITAN_GAME_GSM_OFF;
    }

    g_video_output.game_gsm_mode = mode;
}

void TitanVideoOutput_CycleGameGsmMode(int direction)
{
    int mode;

    mode = (int)g_video_output.game_gsm_mode;
    mode += direction < 0 ? -1 : 1;

    if (mode < 0)
    {
        mode = TITAN_GAME_GSM_MODE_COUNT - 1;
    }
    else if (mode >= TITAN_GAME_GSM_MODE_COUNT)
    {
        mode = 0;
    }

    g_video_output.game_gsm_mode = (TitanGameGsmMode)mode;
}

const char *TitanVideoOutput_GetGameGsmName(void)
{
    switch (g_video_output.game_gsm_mode)
    {
        case TITAN_GAME_GSM_PROGRESSIVE:
            return "Progressive Auto (fp2)";
        case TITAN_GAME_GSM_PROGRESSIVE_COMPAT_1:
            return "Progressive Compat 1";
        case TITAN_GAME_GSM_OFF:
        default:
            return "Disabled";
    }
}

const char *TitanVideoOutput_GetGameGsmArgument(void)
{
    switch (g_video_output.game_gsm_mode)
    {
        case TITAN_GAME_GSM_PROGRESSIVE:
            return "-gsm=fp2";
        case TITAN_GAME_GSM_PROGRESSIVE_COMPAT_1:
            return "-gsm=fp2:1";
        case TITAN_GAME_GSM_OFF:
        default:
            return 0;
    }
}
