#include "titan_audio.h"
#include "audsrv.h"
#include "loadfile.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef TITAN_STORAGE_DEVICE_ROOT
#error TITAN_STORAGE_DEVICE_ROOT must be defined for the audio test
#endif

#define AUDIO_ROOT TITAN_STORAGE_DEVICE_ROOT "/audio"
#define SFX_ROOT AUDIO_ROOT "/sfx"

static int g_fail_init_count;
static int g_force_libsd_load_error;
static int g_force_audsrv_load_error;
static int g_format_ok;
static int g_master_volume_ok;
static int g_bytes_accepted;
static int g_nonzero_bytes;
static int g_play_limit;
static int g_force_zero_play;
static int g_quit_calls;

#define TEST_BUILTIN_SFX_COUNT (TITAN_AUDIO_SFX_COUNT - 1u)

static const char *g_sfx_names[TITAN_AUDIO_SFX_COUNT] =
{
    "titan_notification.wav",
    "titan_profile_entry.wav",
    "titan_home_grid_nav.wav",
    "titan_top_menu_nav.wav",
    "titan_interface_menu_nav.wav",
    "titan_shutdown.wav",
    "theme_top_enter.wav"
};

static void require(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "audio-runtime: FAIL: %s\n", message);
        exit(1);
    }
}

static void write_u16_le(FILE *file, unsigned int value)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xFFu);
    require(fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes), "write u16");
}

static void write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16u) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24u) & 0xFFu);
    require(fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes), "write u32");
}

static void write_wav(const char *path, size_t data_size, int malformed)
{
    FILE *file;
    size_t index;

    require(data_size > 0u && (data_size % 4u) == 0u, "valid PCM size");
    file = fopen(path, "wb");
    require(file != 0, "create WAV");

    require(fwrite(malformed ? "NOPE" : "RIFF", 1u, 4u, file) == 4u, "write RIFF");
    write_u32_le(file, (uint32_t)(36u + data_size));
    require(fwrite("WAVEfmt ", 1u, 8u, file) == 8u, "write WAVE/fmt");
    write_u32_le(file, 16u);
    write_u16_le(file, 1u);
    write_u16_le(file, 2u);
    write_u32_le(file, 32000u);
    write_u32_le(file, 128000u);
    write_u16_le(file, 4u);
    write_u16_le(file, 16u);
    require(fwrite("data", 1u, 4u, file) == 4u, "write data tag");
    write_u32_le(file, (uint32_t)data_size);

    for (index = 0u; index < data_size; index++)
    {
        unsigned char value;
        value = (unsigned char)((index * 17u + 11u) & 0xFFu);
        require(fwrite(&value, 1u, 1u, file) == 1u, "write PCM");
    }
    require(fclose(file) == 0, "close WAV");
}

static void write_ima_wav(const char *path)
{
    FILE *file;
    unsigned int block;
    unsigned int encoded;

    file = fopen(path, "wb");
    require(file != 0, "create IMA WAV");

    require(fwrite("RIFF", 1u, 4u, file) == 4u, "write IMA RIFF");
    write_u32_le(file, 84u);
    require(fwrite("WAVEfmt ", 1u, 8u, file) == 8u, "write IMA WAVE/fmt");
    write_u32_le(file, 20u);
    write_u16_le(file, 0x0011u);
    write_u16_le(file, 2u);
    write_u32_le(file, 32000u);
    write_u32_le(file, 16000u);
    write_u16_le(file, 16u);
    write_u16_le(file, 4u);
    write_u16_le(file, 2u);
    write_u16_le(file, 9u);
    require(fwrite("fact", 1u, 4u, file) == 4u, "write fact tag");
    write_u32_le(file, 4u);
    write_u32_le(file, 18u);
    require(fwrite("data", 1u, 4u, file) == 4u, "write IMA data tag");
    write_u32_le(file, 32u);

    for (block = 0u; block < 2u; block++)
    {
        write_u16_le(file, 1000u);
        require(fputc(0, file) == 0, "write left IMA index");
        require(fputc(0, file) == 0, "write left IMA reserved");
        write_u16_le(file, 0xFC18u);
        require(fputc(0, file) == 0, "write right IMA index");
        require(fputc(0, file) == 0, "write right IMA reserved");
        for (encoded = 0u; encoded < 8u; encoded++)
        {
            require(fputc(0, file) == 0, "write IMA nibbles");
        }
    }
    require(fclose(file) == 0, "close IMA WAV");
}

static void prepare_paths(void)
{
    char command[1024];

    snprintf(
        command,
        sizeof(command),
        "rm -rf '%s' && mkdir -p '%s'",
        TITAN_STORAGE_DEVICE_ROOT,
        SFX_ROOT
    );
    require(system(command) == 0, "prepare audio test root");
}

static void prepare_assets(size_t sfx_size, int malformed_effect)
{
    unsigned int index;
    char path[1024];

    prepare_paths();
    snprintf(path, sizeof(path), "%s/magenta_melody.wav", AUDIO_ROOT);
    write_ima_wav(path);

    for (index = 0u; index < TEST_BUILTIN_SFX_COUNT; index++)
    {
        snprintf(path, sizeof(path), "%s/%s", SFX_ROOT, g_sfx_names[index]);
        write_wav(path, sfx_size, (int)index == malformed_effect);
    }
}

int SifLoadModule(const char *path, int argument_length, const char *arguments)
{
    (void)argument_length;
    (void)arguments;
    if (path != 0 && strcmp(path, "rom0:LIBSD") == 0)
    {
        return g_force_libsd_load_error ? -1 : 1;
    }
    return -1;
}

int SifExecModuleBuffer(
    void *module,
    int module_size,
    int argument_length,
    const char *arguments,
    int *module_result
)
{
    (void)module;
    (void)module_size;
    (void)argument_length;
    (void)arguments;
    if (module_result != 0)
    {
        *module_result = g_force_audsrv_load_error ? -1 : 0;
    }
    return g_force_audsrv_load_error ? -1 : 2;
}

int SifLoadElf(const char *path, t_ExecData *data)
{
    (void)path;
    (void)data;
    return 0;
}

int DelayThread(int microseconds)
{
    (void)microseconds;
    return 0;
}

int audsrv_init(void)
{
    if (g_fail_init_count > 0)
    {
        g_fail_init_count--;
        return -1;
    }
    return AUDSRV_ERR_NOERROR;
}

int audsrv_quit(void)
{
    g_quit_calls++;
    return AUDSRV_ERR_NOERROR;
}

int audsrv_set_format(audsrv_fmt_t *format)
{
    g_format_ok =
        format != 0 &&
        format->freq == 32000 &&
        format->bits == 16 &&
        format->channels == 2;
    return g_format_ok ? AUDSRV_ERR_NOERROR : -1;
}

int audsrv_set_volume(int volume)
{
    g_master_volume_ok = volume == MAX_VOLUME;
    return g_master_volume_ok ? AUDSRV_ERR_NOERROR : -1;
}

int audsrv_stop_audio(void)
{
    return AUDSRV_ERR_NOERROR;
}

int audsrv_available(void)
{
    return 65536;
}

int audsrv_play_audio(const char *chunk, int bytes)
{
    int accepted;
    int index;

    if (chunk == 0 || bytes <= 0)
    {
        return -1;
    }
    if (g_force_zero_play)
    {
        return 0;
    }

    accepted = bytes;
    if (g_play_limit > 0 && accepted > g_play_limit)
    {
        accepted = g_play_limit;
    }
    for (index = 0; index < accepted; index++)
    {
        if ((unsigned char)chunk[index] != 0u)
        {
            g_nonzero_bytes++;
        }
    }
    g_bytes_accepted += accepted;
    return accepted;
}

int audsrv_queued(void)
{
    return 0;
}

static void reset_capture(void)
{
    g_bytes_accepted = 0;
    g_nonzero_bytes = 0;
    g_play_limit = 0;
    g_force_zero_play = 0;
}

static void pump_until_sfx_count(unsigned int expected, int maximum_updates)
{
    int update;

    for (update = 0;
         update < maximum_updates && TitanAudio_GetLoadedSfxCount() < expected;
         update++)
    {
        TitanAudio_Update();
    }
    if (TitanAudio_GetLoadedSfxCount() != expected)
    {
        fprintf(stderr, "audio-runtime: expected %u SFX, got %u after %d updates\n",
            expected, TitanAudio_GetLoadedSfxCount(), maximum_updates);
    }
    require(TitanAudio_GetLoadedSfxCount() == expected,
        "incremental SFX loading did not reach expected count");
}

int main(void)
{
    unsigned int before_reads;
    unsigned int after_reads;
    size_t pending;
    int update;
    static short external_pcm[4096u * 2u];

    /* Failed initialization must remain retryable and must clean AUDSRV state. */
    prepare_assets(8192u, -1);
    g_fail_init_count = 1;
    require(!TitanAudio_Initialize(), "first RPC initialization should fail");
    require(!TitanAudio_IsReady(), "failed initialization latched ready");
    require(TitanAudio_Initialize(), "initialization retry failed");
    require(TitanAudio_IsReady(), TitanAudio_GetStatusName());
    require(g_format_ok, "PCM format not configured as 32000/16/stereo");
    require(g_master_volume_ok, "AUDSRV master gain is not MAX_VOLUME");
    require(TitanAudio_GetLoadedSfxCount() == 0u,
        "initialization performed blocking SFX reads");
    pump_until_sfx_count(3u, 100);
    require(TitanAudio_GetLoadedSfxCount() == 3u,
        "startup gate did not preload exactly the critical effects");
    for (update = 0; update < 160; update++)
        TitanAudio_Update();
    require(TitanAudio_GetLoadedSfxCount() == 3u,
        "deferred effects performed USB I/O during startup gate");
    TitanAudio_SetDeferredAssetIoAllowed(1);
    pump_until_sfx_count(TEST_BUILTIN_SFX_COUNT, 140);

    /* One update performs at most one bounded music read. */
    TitanAudio_StartMusic();
    before_reads = TitanAudio_TestGetMusicReadCalls();
    TitanAudio_Update();
    after_reads = TitanAudio_TestGetMusicReadCalls();
    require(after_reads - before_reads <= 1u, "more than one music read in one update");
    require(g_bytes_accepted > 0, "music did not queue PCM");
    TitanAudio_Update();
    require(g_nonzero_bytes > 0, "music PCM is silent after fade-in began");

    /* A zero-progress AUDSRV call retains the authoritative pending tail. */
    TitanAudio_StopMusic();
    reset_capture();
    require(TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION), "notification SFX missing");
    g_force_zero_play = 1;
    TitanAudio_Update();
    pending = TitanAudio_TestGetPendingBytes();
    require(pending > 0u, "zero enqueue discarded pending audio");
    TitanAudio_Update();
    require(TitanAudio_TestGetPendingBytes() == pending, "zero enqueue advanced pending audio");

    /* Positive partial enqueue must drain exactly, never drop the tail. */
    g_force_zero_play = 0;
    g_play_limit = 317;
    for (update = 0; update < 32 && TitanAudio_TestGetPendingBytes() > 0u; update++)
    {
        TitanAudio_Update();
    }
    require(TitanAudio_TestGetPendingBytes() == 0u, "partial enqueue tail did not drain");
    require(g_bytes_accepted >= 4096, "partial enqueue lost PCM bytes");

    /* Tiny looping music reaches EOF without an inner retry/spin loop. */
    TitanAudio_StartMusic();
    for (update = 0; update < 20; update++)
    {
        before_reads = TitanAudio_TestGetMusicReadCalls();
        TitanAudio_Update();
        after_reads = TitanAudio_TestGetMusicReadCalls();
        require(after_reads - before_reads <= 1u, "EOF loop performed repeated reads");
    }

    /* Titan games run on a dedicated EE worker while the kernel ticker stays
     * alive. The worker may only publish PCM; the kernel update remains the
     * sole AUDSRV consumer and releases ring capacity deterministically. */
    reset_capture();
    for (update = 0; update < (int)(sizeof(external_pcm) / sizeof(external_pcm[0]));
         update++)
        external_pcm[update] = (short)((update & 1) ? -12000 : 12000);
    require(TitanAudio_BeginExternalStream(), "external stream did not begin");
    before_reads = TitanAudio_GetExternalFreeFrames();
    require(TitanAudio_SubmitExternalPcm(external_pcm, 4096u) > 0u,
        "external PCM was rejected");
    after_reads = TitanAudio_GetExternalFreeFrames();
    require(after_reads < before_reads, "external PCM was not queued");
    require(g_bytes_accepted == 0,
        "game worker called AUDSRV instead of publishing PCM");
    TitanAudio_Update();
    require(g_bytes_accepted > 0,
        "kernel audio tick did not drain external PCM");
    require(g_nonzero_bytes > 0, "external PCM became silent");
    require(TitanAudio_GetExternalFreeFrames() > after_reads,
        "kernel audio tick did not release external ring capacity");
    TitanAudio_EndExternalStream();

    TitanAudio_PrepareHandoff();
    require(!TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION), "handoff accepted SFX");
    TitanAudio_CancelHandoff();

    reset_capture();
    TitanAudio_PlayShutdownAndWait();
    require(g_bytes_accepted > 0, "shutdown sound did not play");
    require(g_nonzero_bytes > 0, "shutdown sound is silent");
    TitanAudio_Shutdown();
    TitanAudio_Shutdown();
    require(g_quit_calls >= 1, "AUDSRV quit not called");

    /* Resident-module diagnostics must not defeat a working AUDSRV RPC bind. */
    prepare_assets(8192u, -1);
    g_force_libsd_load_error = 1;
    g_force_audsrv_load_error = 1;
    require(TitanAudio_Initialize(), "resident audio modules rejected");
    require(TitanAudio_IsReady(), "resident-module RPC bind did not become ready");
    require(TitanAudio_GetLoadedSfxCount() == 0u,
        "resident-module init performed blocking SFX reads");
    TitanAudio_SetDeferredAssetIoAllowed(1);
    pump_until_sfx_count(TEST_BUILTIN_SFX_COUNT, 140);
    TitanAudio_Shutdown();
    g_force_libsd_load_error = 0;
    g_force_audsrv_load_error = 0;

    /* Assets that appear after initialization must be picked up without reboot. */
    prepare_paths();
    require(TitanAudio_Initialize(), "init without assets failed");
    require(TitanAudio_GetLoadedSfxCount() == 0u, "unexpected assets in empty root");
    prepare_assets(8192u, -1);
    TitanAudio_SetDeferredAssetIoAllowed(1);
    pump_until_sfx_count(TEST_BUILTIN_SFX_COUNT, 180);
    TitanAudio_Shutdown();

    /* Malformed WAV is rejected without taking down valid effects. */
    prepare_assets(8192u, TITAN_AUDIO_SFX_NOTIFICATION);
    require(TitanAudio_Initialize(), "init with one malformed asset failed");
    TitanAudio_SetDeferredAssetIoAllowed(1);
    pump_until_sfx_count(TEST_BUILTIN_SFX_COUNT - 1u, 180);
    require(!TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION),
        "malformed effect became playable");
    TitanAudio_Shutdown();

    /* Aggregate SFX memory is capped even when each file is individually valid. */
    prepare_assets(220000u, -1);
    require(TitanAudio_Initialize(), "init with oversized aggregate failed");
    TitanAudio_SetDeferredAssetIoAllowed(1);
    for (update = 0; update < 240; update++)
    {
        TitanAudio_Update();
    }
    require(TitanAudio_GetLoadedSfxCount() <= 4u, "aggregate SFX budget exceeded");
    TitanAudio_Shutdown();

    puts("audio-runtime: OK");
    return 0;
}
