#include "titan_dex.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_video.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#if defined(__EE__) || defined(_EE)
#include <timer.h>
#endif

#define TITAN_DEX_SETTINGS_DIRECTORY TITAN_STORAGE_DEVICE_ROOT "/settings"
#define TITAN_DEX_SETTINGS_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_mode.cfg"
#define TITAN_DEX_SETTINGS_TEMP_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_mode.tmp"
#define TITAN_DEX_SETTINGS_BACKUP_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_mode.bak"
#define TITAN_DEX_OVERLAY_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_overlay.cfg"
#define TITAN_DEX_OVERLAY_TEMP_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_overlay.tmp"
#define TITAN_DEX_OVERLAY_BACKUP_PATH TITAN_DEX_SETTINGS_DIRECTORY "/dex_overlay.bak"
#define TITAN_DEX_REFRESH_FRAMES 30u
#define TITAN_DEX_EE_RAM_BASE 0x00100000u
#define TITAN_DEX_EE_RAM_LIMIT 0x02000000u

typedef struct TitanDexState
{
    int initialized;
    int enabled;
    int overlay_enabled;
    unsigned int ee_load_percent;
    unsigned int gs_load_percent;
    unsigned int ram_used_kilobytes;
    unsigned int sample_frames;
    unsigned long long ee_ticks_accumulated;
    unsigned long long frame_ticks_accumulated;
    unsigned long long frame_start;
    unsigned long long ee_complete;
    char ip_address[16];
} TitanDexState;

static TitanDexState g_dex;

static void TitanDex_CopyText(char *destination, unsigned int capacity, const char *source)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static unsigned long long TitanDex_GetTicks(void)
{
#if defined(__EE__) || defined(_EE)
    return (unsigned long long)GetTimerSystemTime();
#else
    static unsigned long long host_ticks;
    host_ticks += 1000u;
    return host_ticks;
#endif
}

static void TitanDex_UpdateRamUsage(void)
{
#if defined(__EE__) || defined(_EE)
    uintptr_t current_break;
    unsigned int used;

    {
        void *break_pointer;
        break_pointer = sbrk(0);
        if (break_pointer == (void *)-1)
        {
            g_dex.ram_used_kilobytes = 0u;
            return;
        }
        current_break = (uintptr_t)break_pointer;
    }

    if (current_break < TITAN_DEX_EE_RAM_BASE)
    {
        used = 0u;
    }
    else if (current_break > TITAN_DEX_EE_RAM_LIMIT)
    {
        used = TITAN_DEX_EE_RAM_LIMIT - TITAN_DEX_EE_RAM_BASE;
    }
    else
    {
        used = (unsigned int)(current_break - TITAN_DEX_EE_RAM_BASE);
    }

    g_dex.ram_used_kilobytes = (used + 1023u) / 1024u;
#else
    g_dex.ram_used_kilobytes = 0u;
#endif
}

static int TitanDex_ReadSetting(const char *path, int *enabled)
{
    int descriptor;
    char value;
    TitanStorageIoSize size;

    if (enabled == 0 || !TitanStorageIo_PathInfo64(path, 0, &size) || size != 1u)
        return 0;

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
        return 0;
    if (!TitanStorageIo_ReadExact(descriptor, &value, 1u) ||
        TitanStorageIo_Close(descriptor) < 0 ||
        (value != '0' && value != '1'))
        return 0;

    *enabled = value == '1';
    return 1;
}

static int TitanDex_RecoverBooleanSetting(
    const char *path,
    const char *temporary_path,
    const char *backup_path,
    int default_value,
    int *enabled
)
{
    int value;

    if (enabled == 0)
        return 0;
    if (TitanDex_ReadSetting(path, &value))
    {
        TitanStorageIo_Remove(temporary_path);
        TitanStorageIo_Remove(backup_path);
        *enabled = value;
        return 1;
    }

    if (TitanDex_ReadSetting(temporary_path, &value))
    {
        if (TitanStorageIo_AtomicReplace(temporary_path, path, backup_path) &&
            TitanDex_ReadSetting(path, enabled))
        {
            TitanStorageIo_Remove(backup_path);
            return 1;
        }
    }

    if (TitanDex_ReadSetting(backup_path, &value))
    {
        TitanStorageIo_Remove(temporary_path);
        if (TitanStorageIo_PathInfo64(path, 0, 0))
            TitanStorageIo_Rename(path, temporary_path);
        if (TitanStorageIo_Rename(backup_path, path) == 0 &&
            TitanDex_ReadSetting(path, enabled))
        {
            TitanStorageIo_Remove(temporary_path);
            return 1;
        }
    }

    *enabled = default_value != 0;
    return 0;
}

static int TitanDex_RecoverSetting(int *enabled)
{
    return TitanDex_RecoverBooleanSetting(
        TITAN_DEX_SETTINGS_PATH,
        TITAN_DEX_SETTINGS_TEMP_PATH,
        TITAN_DEX_SETTINGS_BACKUP_PATH,
        0,
        enabled
    );
}

static int TitanDex_SaveBooleanSetting(
    const char *path,
    const char *temporary_path,
    const char *backup_path,
    int enabled
)
{
    int descriptor;
    int verified;
    const char value = enabled ? '1' : '0';

    TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    TitanStorageIo_CreateDirectory(TITAN_DEX_SETTINGS_DIRECTORY);
    TitanStorageIo_Remove(temporary_path);

    descriptor = TitanStorageIo_Open(
        temporary_path,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
        return 0;

    if (!TitanStorageIo_WriteExact(descriptor, &value, 1u) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0) {
        TitanStorageIo_Remove(temporary_path);
        return 0;
    }

    if (!TitanDex_ReadSetting(temporary_path, &verified) ||
        verified != enabled ||
        !TitanStorageIo_AtomicReplace(temporary_path, path, backup_path)) {
        TitanStorageIo_Remove(temporary_path);
        return 0;
    }

    if (!TitanDex_ReadSetting(path, &verified) || verified != enabled) {
        TitanStorageIo_Remove(path);
        TitanStorageIo_Rename(backup_path, path);
        return 0;
    }

    TitanStorageIo_Remove(backup_path);
    return 1;
}

static int TitanDex_SaveSetting(void)
{
    return TitanDex_SaveBooleanSetting(
        TITAN_DEX_SETTINGS_PATH,
        TITAN_DEX_SETTINGS_TEMP_PATH,
        TITAN_DEX_SETTINGS_BACKUP_PATH,
        g_dex.enabled
    );
}

static int TitanDex_SaveOverlaySetting(void)
{
    return TitanDex_SaveBooleanSetting(
        TITAN_DEX_OVERLAY_PATH,
        TITAN_DEX_OVERLAY_TEMP_PATH,
        TITAN_DEX_OVERLAY_BACKUP_PATH,
        g_dex.overlay_enabled
    );
}

void TitanDex_Initialize(void)
{
    int enabled;

    memset(&g_dex, 0, sizeof(g_dex));
    TitanDex_CopyText(g_dex.ip_address, sizeof(g_dex.ip_address), "Offline");

    enabled = 0;
    TitanDex_RecoverSetting(&enabled);
    g_dex.enabled = enabled;

    /* Overlay visibility is independent from DEX mode. Missing settings keep
     * the historical DEX behavior (overlay visible) for compatibility, while
     * tmp/bak recovery preserves a confirmed choice across interrupted writes. */
    g_dex.overlay_enabled = 1;
    (void)TitanDex_RecoverBooleanSetting(
        TITAN_DEX_OVERLAY_PATH,
        TITAN_DEX_OVERLAY_TEMP_PATH,
        TITAN_DEX_OVERLAY_BACKUP_PATH,
        1,
        &g_dex.overlay_enabled
    );

    TitanDex_UpdateRamUsage();
    g_dex.initialized = 1;
}

int TitanDex_IsEnabled(void)
{
    return g_dex.enabled;
}

int TitanDex_SetEnabled(int enabled)
{
    int previous;

    if (!g_dex.initialized)
    {
        TitanDex_Initialize();
    }

    previous = g_dex.enabled;
    g_dex.enabled = enabled != 0;
    if (!TitanDex_SaveSetting())
    {
        g_dex.enabled = previous;
        return 0;
    }

    return 1;
}

const char *TitanDex_GetStateName(void)
{
    return g_dex.enabled ? "Enabled" : "Disabled";
}

int TitanDex_IsOverlayEnabled(void)
{
    return g_dex.overlay_enabled;
}

int TitanDex_SetOverlayEnabled(int enabled)
{
    int previous;

    if (!g_dex.initialized)
    {
        TitanDex_Initialize();
    }

    previous = g_dex.overlay_enabled;
    g_dex.overlay_enabled = enabled != 0;
    if (!TitanDex_SaveOverlaySetting())
    {
        g_dex.overlay_enabled = previous;
        return 0;
    }
    return 1;
}

const char *TitanDex_GetOverlayStateName(void)
{
    return g_dex.overlay_enabled ? "Enabled" : "Disabled";
}

void TitanDex_BeginFrame(void)
{
    if (!g_dex.enabled)
    {
        return;
    }

    g_dex.frame_start = TitanDex_GetTicks();
    g_dex.ee_complete = g_dex.frame_start;
}

void TitanDex_MarkEeComplete(void)
{
    if (g_dex.enabled)
    {
        g_dex.ee_complete = TitanDex_GetTicks();
    }
}

void TitanDex_EndFrame(void)
{
    unsigned long long frame_end;
    unsigned long long ee_ticks;
    unsigned long long frame_ticks;

    if (!g_dex.enabled)
    {
        return;
    }

    frame_end = TitanDex_GetTicks();
    ee_ticks = g_dex.ee_complete >= g_dex.frame_start
        ? g_dex.ee_complete - g_dex.frame_start
        : 0u;
    frame_ticks = frame_end >= g_dex.frame_start
        ? frame_end - g_dex.frame_start
        : 0u;

    g_dex.ee_ticks_accumulated += ee_ticks;
    g_dex.frame_ticks_accumulated += frame_ticks;
    g_dex.sample_frames++;

    if (g_dex.sample_frames >= TITAN_DEX_REFRESH_FRAMES)
    {
        if (g_dex.frame_ticks_accumulated > 0u)
        {
            unsigned long long percent;
            percent = (g_dex.ee_ticks_accumulated * 100u) /
                g_dex.frame_ticks_accumulated;
            if (percent > 100u)
            {
                percent = 100u;
            }
            g_dex.ee_load_percent = (unsigned int)percent;
        }
        else
        {
            g_dex.ee_load_percent = 0u;
        }

        g_dex.gs_load_percent = TitanVideo_GetLastFrameGsLoadPercent();
        TitanDex_UpdateRamUsage();
        g_dex.sample_frames = 0u;
        g_dex.ee_ticks_accumulated = 0u;
        g_dex.frame_ticks_accumulated = 0u;
    }
}

unsigned int TitanDex_GetEeLoadPercent(void)
{
    return g_dex.ee_load_percent;
}

unsigned int TitanDex_GetGsLoadPercent(void)
{
    return g_dex.gs_load_percent;
}

unsigned int TitanDex_GetRamUsedKilobytes(void)
{
    return g_dex.ram_used_kilobytes;
}

const char *TitanDex_GetIpAddress(void)
{
    return g_dex.ip_address;
}

void TitanDex_SetIpAddress(const char *address)
{
    if (address == 0 || address[0] == '\0')
    {
        TitanDex_CopyText(g_dex.ip_address, sizeof(g_dex.ip_address), "Offline");
    }
    else
    {
        TitanDex_CopyText(g_dex.ip_address, sizeof(g_dex.ip_address), address);
    }
}
