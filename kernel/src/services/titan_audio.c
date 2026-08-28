#include "titan_audio.h"

#if defined(__EE__) || defined(_EE)
#include "generated/titan_audsrv_irx.h"
#else
static unsigned char titan_audsrv_irx[1];
static unsigned int size_titan_audsrv_irx = 1u;
#endif

#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_theme.h"
#include "titan_theme_lifecycle.h"

#include <audsrv.h>
#include <delaythread.h>
#include <loadfile.h>
#include <tamtypes.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_AUDIO_SAMPLE_RATE 32000u
#define TITAN_AUDIO_CHANNELS 2u
#define TITAN_AUDIO_BITS 16u
#define TITAN_AUDIO_BYTES_PER_FRAME 4u
#define TITAN_AUDIO_BYTE_RATE \
    (TITAN_AUDIO_SAMPLE_RATE * TITAN_AUDIO_BYTES_PER_FRAME)

#define TITAN_AUDIO_OUTPUT_FRAMES 1024u
#define TITAN_AUDIO_OUTPUT_BYTES \
    (TITAN_AUDIO_OUTPUT_FRAMES * TITAN_AUDIO_BYTES_PER_FRAME)
/* Decoded PCM cushion for USB/HDD metadata bursts. At 32 kHz stereo 16-bit,
 * 1 MiB is a little over eight seconds of music. The old 256 KiB ring only
 * covered about two seconds and could drain during a single slow FAT/ISO
 * operation even though the render thread itself stayed perfectly smooth. */
#define TITAN_AUDIO_MUSIC_RING_BYTES (1024u * 1024u)
#define TITAN_AUDIO_MUSIC_RING_REFILL_LOW_WATER (768u * 1024u)
#define TITAN_AUDIO_MUSIC_RING_REFILL_HIGH_WATER (896u * 1024u)
#define TITAN_AUDIO_EXTERNAL_RING_BYTES (64u * 1024u)
#define TITAN_AUDIO_MUSIC_READ_BUDGET 8192u
#define TITAN_AUDIO_MAX_PUSHES_PER_UPDATE 2u
#define TITAN_AUDIO_MAX_VOICES 8u
#define TITAN_AUDIO_MAX_SINGLE_SFX_BYTES (1024u * 1024u)
#define TITAN_AUDIO_MAX_TOTAL_SFX_BYTES (1024u * 1024u)
#define TITAN_AUDIO_MAX_WAV_CHUNKS 256u
#define TITAN_AUDIO_ASSET_RETRY_FRAMES 120u
#define TITAN_AUDIO_ASSET_INITIAL_DELAY_FRAMES 0u
#define TITAN_AUDIO_SFX_LOAD_BUDGET (4u * 1024u)
#define TITAN_AUDIO_STARTUP_CRITICAL_SFX_COUNT 3u
#define TITAN_AUDIO_ASSET_RETRY_LIMIT 12u
#define TITAN_AUDIO_WAV_FORMAT_PCM 0x0001u
#define TITAN_AUDIO_WAV_FORMAT_IMA_ADPCM 0x0011u
#define TITAN_AUDIO_MAX_ADPCM_BLOCK_BYTES 4096u
#define TITAN_AUDIO_MAX_ADPCM_DECODE_BYTES \
    (TITAN_AUDIO_MAX_ADPCM_BLOCK_BYTES * 4u)

#define TITAN_AUDIO_MUSIC_TARGET_VOLUME 24
#define TITAN_AUDIO_MUSIC_RAMP_STEP 1
#define TITAN_AUDIO_MASTER_VOLUME MAX_VOLUME

#define TITAN_AUDIO_ROOT TITAN_STORAGE_DEVICE_ROOT "/audio"
#define TITAN_AUDIO_MUSIC_PATH TITAN_AUDIO_ROOT "/magenta_melody.wav"
#define TITAN_AUDIO_SFX_ROOT TITAN_AUDIO_ROOT "/sfx"

#define TITAN_AUDIO_SHUTDOWN_TIMEOUT_US 4200000u
#define TITAN_AUDIO_SHUTDOWN_PUMP_DELAY_US 8000u

typedef enum TitanAudioStatus
{
    TITAN_AUDIO_STATUS_NOT_INITIALIZED = 0,
    TITAN_AUDIO_STATUS_READY,
    TITAN_AUDIO_STATUS_READY_NO_ASSETS,
    TITAN_AUDIO_STATUS_LIBSD_FAILED,
    TITAN_AUDIO_STATUS_IRX_FAILED,
    TITAN_AUDIO_STATUS_RPC_FAILED,
    TITAN_AUDIO_STATUS_FORMAT_FAILED,
    TITAN_AUDIO_STATUS_STREAM_IO_FAILED
} TitanAudioStatus;

typedef struct TitanAudioWavInfo
{
    TitanStorageIoOffset data_offset;
    TitanStorageIoSize data_size;
    TitanStorageIoSize sample_frames;
    unsigned int format_tag;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int bits;
    unsigned int block_align;
    unsigned int samples_per_block;
} TitanAudioWavInfo;

typedef struct TitanAudioClip
{
    s16 *samples;
    unsigned int frame_count;
    size_t byte_count;
    int volume;
} TitanAudioClip;

typedef struct TitanAudioVoice
{
    int active;
    TitanAudioSfx effect;
    const TitanAudioClip *clip;
    unsigned int frame_position;
    unsigned int serial;
} TitanAudioVoice;

typedef struct TitanAudioState
{
    int ready;
    int audsrv_started;
    int handoff_paused;
    int exclusive_playback;
    int external_stream_active;
    TitanAudioStatus status;

    int music_descriptor;
    TitanAudioWavInfo music_info;
    TitanStorageIoSize music_byte_position;
    TitanStorageIoSize music_frames_remaining;
    size_t music_adpcm_block_size;
    int music_requested;
    int music_volume;
    int music_target_volume;
    int music_eof_pending;
    unsigned int music_read_calls;

    volatile unsigned int music_ring_read_total;
    volatile unsigned int music_ring_write_total;

    size_t external_ring_read;
    size_t external_ring_write;
    size_t external_ring_size;

    size_t pending_offset;
    size_t pending_size;

    size_t total_sfx_bytes;
    unsigned int loaded_sfx_count;
    unsigned int voice_serial;
    unsigned int asset_retry_frames;
    unsigned int asset_retry_attempts;
    int deferred_asset_io_allowed;
    int sfx_load_descriptor;
    int sfx_load_effect;
    size_t sfx_load_size;
    size_t sfx_load_offset;
    unsigned char sfx_invalid[TITAN_AUDIO_SFX_COUNT];
    TitanAudioClip clips[TITAN_AUDIO_SFX_COUNT];
    TitanAudioVoice voices[TITAN_AUDIO_MAX_VOICES];
} TitanAudioState;

static TitanAudioState g_audio =
{
    .music_descriptor = -1,
    .sfx_load_descriptor = -1,
    .sfx_load_effect = -1,
    .status = TITAN_AUDIO_STATUS_NOT_INITIALIZED
};
static unsigned char g_music_ring[TITAN_AUDIO_MUSIC_RING_BYTES]
    __attribute__((aligned(64)));
static unsigned char g_music_io_buffer[TITAN_AUDIO_MUSIC_READ_BUDGET]
    __attribute__((aligned(64)));
static unsigned char g_external_ring[TITAN_AUDIO_EXTERNAL_RING_BYTES]
    __attribute__((aligned(64)));
static unsigned char g_music_adpcm_block[TITAN_AUDIO_MAX_ADPCM_BLOCK_BYTES]
    __attribute__((aligned(64)));
static s16 g_music_adpcm_decoded[
    TITAN_AUDIO_MAX_ADPCM_DECODE_BYTES / sizeof(s16)
] __attribute__((aligned(64)));
static s16 g_mix_buffer[TITAN_AUDIO_OUTPUT_BYTES / sizeof(s16)]
    __attribute__((aligned(64)));
static unsigned char g_pending_buffer[TITAN_AUDIO_OUTPUT_BYTES]
    __attribute__((aligned(64)));

static const char *g_titan_audio_sfx_paths[TITAN_AUDIO_SFX_COUNT] =
{
    TITAN_AUDIO_SFX_ROOT "/titan_notification.wav",
    TITAN_AUDIO_SFX_ROOT "/titan_profile_entry.wav",
    TITAN_AUDIO_SFX_ROOT "/titan_home_grid_nav.wav",
    TITAN_AUDIO_SFX_ROOT "/titan_top_menu_nav.wav",
    TITAN_AUDIO_SFX_ROOT "/titan_interface_menu_nav.wav",
    TITAN_AUDIO_SFX_ROOT "/titan_shutdown.wav",
    0
};

static const int g_titan_audio_sfx_volumes[TITAN_AUDIO_SFX_COUNT] =
{
    58, 54, 44, 42, 40, 64, 60
};

/* Navigation feedback is loaded first so the shell becomes audible after only
 * a few bounded USB reads. Larger notification/profile/shutdown effects follow
 * in the background. */
static const TitanAudioSfx g_titan_audio_sfx_load_order[TITAN_AUDIO_SFX_COUNT] =
{
    /* These three are the only effects needed before the Home screen.  Their
     * bounded incremental preload fits inside the initial black boot phase,
     * so the visible health/welcome animation does not compete with large
     * notification/interface assets for USB bandwidth. */
    TITAN_AUDIO_SFX_HOME_GRID_NAV,
    TITAN_AUDIO_SFX_PROFILE_ENTRY,
    TITAN_AUDIO_SFX_TOP_MENU_NAV,
    TITAN_AUDIO_SFX_INTERFACE_MENU_NAV,
    TITAN_AUDIO_SFX_NOTIFICATION,
    TITAN_AUDIO_SFX_SHUTDOWN,
    TITAN_AUDIO_SFX_THEME_TOP_ENTER
};

static const char *TitanAudio_ThemeRelativeSfx(TitanAudioSfx effect)
{
    const TitanThemeAudioStyle *audio = TitanTheme_GetAudioStyle();
    if (audio == 0)
        return 0;
    switch (effect)
    {
        case TITAN_AUDIO_SFX_NOTIFICATION: return audio->notification;
        case TITAN_AUDIO_SFX_PROFILE_ENTRY: return audio->profile_entry;
        case TITAN_AUDIO_SFX_HOME_GRID_NAV: return audio->home_nav;
        case TITAN_AUDIO_SFX_TOP_MENU_NAV: return audio->top_nav;
        case TITAN_AUDIO_SFX_INTERFACE_MENU_NAV: return audio->interface_nav;
        case TITAN_AUDIO_SFX_SHUTDOWN: return audio->shutdown;
        case TITAN_AUDIO_SFX_THEME_TOP_ENTER: return audio->top_enter;
        default: return 0;
    }
}

static int TitanAudio_ResolveSfxPath(TitanAudioSfx effect, char *path, unsigned int capacity)
{
    const char *relative;
    const char *fallback;

    if (path == 0 || capacity == 0u || effect < 0 || effect >= TITAN_AUDIO_SFX_COUNT)
        return 0;
    relative = TitanAudio_ThemeRelativeSfx(effect);
    if (relative != 0 && relative[0] != '\0' && TitanTheme_BuildAssetPath(relative, path, capacity))
        return 1;
    fallback = g_titan_audio_sfx_paths[effect];
    if (fallback == 0 || fallback[0] == '\0')
        return 0;
    if (strlen(fallback) + 1u > capacity)
        return 0;
    strcpy(path, fallback);
    return 1;
}

static int TitanAudio_ResolveMusicPath(char *path, unsigned int capacity)
{
    const TitanThemeAudioStyle *audio = TitanTheme_GetAudioStyle();
    if (audio != 0 && audio->music[0] != '\0' &&
        TitanTheme_BuildAssetPath(audio->music, path, capacity))
        return 1;
    if (strlen(TITAN_AUDIO_MUSIC_PATH) + 1u > capacity)
        return 0;
    strcpy(path, TITAN_AUDIO_MUSIC_PATH);
    return 1;
}

static unsigned int TitanAudio_ReadU16Le(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u);
}

static unsigned int TitanAudio_ReadU32Le(const unsigned char *bytes)
{
    return
        (unsigned int)bytes[0] |
        ((unsigned int)bytes[1] << 8u) |
        ((unsigned int)bytes[2] << 16u) |
        ((unsigned int)bytes[3] << 24u);
}

static int TitanAudio_AddU64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == 0 || UINT64_MAX - left < right)
    {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int TitanAudio_ParseWav(int descriptor, TitanAudioWavInfo *info)
{
    unsigned char header[12];
    TitanStorageIoOffset position;
    TitanStorageIoSize file_size;
    uint64_t riff_end;
    int found_format;
    int found_data;
    int found_fact;
    unsigned int chunk_count;
    unsigned int format_tag;
    unsigned int channels;
    unsigned int sample_rate;
    unsigned int byte_rate;
    unsigned int block_align;
    unsigned int bits;
    unsigned int samples_per_block;
    unsigned int fact_sample_frames;

    if (descriptor < 0 || info == 0)
    {
        return 0;
    }

    position = TitanStorageIo_Seek64(descriptor, 0, TITAN_STORAGE_IO_SEEK_END);
    if (position < 0)
    {
        return 0;
    }
    file_size = (TitanStorageIoSize)position;
    if (file_size < sizeof(header) ||
        TitanStorageIo_Seek64(descriptor, 0, TITAN_STORAGE_IO_SEEK_SET) != 0 ||
        !TitanStorageIo_ReadExact(descriptor, header, sizeof(header)))
    {
        return 0;
    }

    if (memcmp(header, "RIFF", 4u) != 0 ||
        memcmp(header + 8u, "WAVE", 4u) != 0)
    {
        return 0;
    }

    riff_end = (uint64_t)TitanAudio_ReadU32Le(header + 4u) + 8u;
    if (riff_end < sizeof(header) || riff_end > file_size)
    {
        return 0;
    }

    memset(info, 0, sizeof(*info));
    found_format = 0;
    found_data = 0;
    found_fact = 0;
    chunk_count = 0u;
    format_tag = 0u;
    channels = 0u;
    sample_rate = 0u;
    byte_rate = 0u;
    block_align = 0u;
    bits = 0u;
    samples_per_block = 0u;
    fact_sample_frames = 0u;
    position = (TitanStorageIoOffset)sizeof(header);

    while ((uint64_t)position + 8u <= riff_end &&
           chunk_count < TITAN_AUDIO_MAX_WAV_CHUNKS)
    {
        unsigned char chunk_header[8];
        uint64_t chunk_data;
        uint64_t chunk_end;
        uint64_t padded_end;
        unsigned int chunk_size;

        if (TitanStorageIo_Seek64(
                descriptor,
                position,
                TITAN_STORAGE_IO_SEEK_SET
            ) != position ||
            !TitanStorageIo_ReadExact(descriptor, chunk_header, sizeof(chunk_header)))
        {
            return 0;
        }

        chunk_count++;
        chunk_size = TitanAudio_ReadU32Le(chunk_header + 4u);
        chunk_data = (uint64_t)position + sizeof(chunk_header);
        if (!TitanAudio_AddU64(chunk_data, chunk_size, &chunk_end) ||
            !TitanAudio_AddU64(chunk_end, chunk_size & 1u, &padded_end) ||
            chunk_end > riff_end || padded_end > riff_end)
        {
            return 0;
        }

        if (memcmp(chunk_header, "fmt ", 4u) == 0 && !found_format)
        {
            unsigned char format[20];
            size_t format_bytes;

            if (chunk_size < 16u ||
                TitanStorageIo_Seek64(
                    descriptor,
                    (TitanStorageIoOffset)chunk_data,
                    TITAN_STORAGE_IO_SEEK_SET
                ) != (TitanStorageIoOffset)chunk_data)
            {
                return 0;
            }

            memset(format, 0, sizeof(format));
            format_bytes = chunk_size < sizeof(format)
                ? (size_t)chunk_size
                : sizeof(format);
            if (!TitanStorageIo_ReadExact(descriptor, format, format_bytes))
            {
                return 0;
            }

            format_tag = TitanAudio_ReadU16Le(format);
            channels = TitanAudio_ReadU16Le(format + 2u);
            sample_rate = TitanAudio_ReadU32Le(format + 4u);
            byte_rate = TitanAudio_ReadU32Le(format + 8u);
            block_align = TitanAudio_ReadU16Le(format + 12u);
            bits = TitanAudio_ReadU16Le(format + 14u);
            if (format_tag == TITAN_AUDIO_WAV_FORMAT_IMA_ADPCM)
            {
                if (chunk_size < sizeof(format) ||
                    TitanAudio_ReadU16Le(format + 16u) < 2u)
                {
                    return 0;
                }
                samples_per_block = TitanAudio_ReadU16Le(format + 18u);
            }
            found_format = 1;
        }
        else if (memcmp(chunk_header, "fact", 4u) == 0 && !found_fact)
        {
            unsigned char fact[4];

            if (chunk_size < sizeof(fact) ||
                TitanStorageIo_Seek64(
                    descriptor,
                    (TitanStorageIoOffset)chunk_data,
                    TITAN_STORAGE_IO_SEEK_SET
                ) != (TitanStorageIoOffset)chunk_data ||
                !TitanStorageIo_ReadExact(descriptor, fact, sizeof(fact)))
            {
                return 0;
            }
            fact_sample_frames = TitanAudio_ReadU32Le(fact);
            found_fact = 1;
        }
        else if (memcmp(chunk_header, "data", 4u) == 0 && !found_data)
        {
            info->data_offset = (TitanStorageIoOffset)chunk_data;
            info->data_size = (TitanStorageIoSize)chunk_size;
            found_data = 1;
        }

        position = (TitanStorageIoOffset)padded_end;
    }

    if (!found_format || !found_data || info->data_size == 0u ||
        sample_rate != TITAN_AUDIO_SAMPLE_RATE ||
        channels != TITAN_AUDIO_CHANNELS ||
        (uint64_t)info->data_offset + info->data_size > riff_end)
    {
        memset(info, 0, sizeof(*info));
        return 0;
    }

    if (format_tag == TITAN_AUDIO_WAV_FORMAT_PCM)
    {
        if (bits != TITAN_AUDIO_BITS ||
            block_align != TITAN_AUDIO_BYTES_PER_FRAME ||
            byte_rate != TITAN_AUDIO_BYTE_RATE ||
            (info->data_size % TITAN_AUDIO_BYTES_PER_FRAME) != 0u)
        {
            memset(info, 0, sizeof(*info));
            return 0;
        }
        info->sample_frames =
            info->data_size / TITAN_AUDIO_BYTES_PER_FRAME;
    }
    else if (format_tag == TITAN_AUDIO_WAV_FORMAT_IMA_ADPCM)
    {
        uint64_t block_count;
        uint64_t maximum_frames;
        uint64_t minimum_frames;
        unsigned int expected_samples_per_block;

        if (bits != 4u || byte_rate == 0u || !found_fact ||
            block_align < 16u ||
            block_align > TITAN_AUDIO_MAX_ADPCM_BLOCK_BYTES ||
            ((block_align - (4u * TITAN_AUDIO_CHANNELS)) % 8u) != 0u ||
            (info->data_size % block_align) != 0u)
        {
            memset(info, 0, sizeof(*info));
            return 0;
        }

        expected_samples_per_block =
            (((block_align - (4u * TITAN_AUDIO_CHANNELS)) * 8u) /
             (bits * TITAN_AUDIO_CHANNELS)) + 1u;
        block_count = info->data_size / block_align;
        maximum_frames = block_count * expected_samples_per_block;
        minimum_frames = maximum_frames - expected_samples_per_block;
        if (samples_per_block != expected_samples_per_block ||
            fact_sample_frames == 0u ||
            (uint64_t)fact_sample_frames <= minimum_frames ||
            (uint64_t)fact_sample_frames > maximum_frames)
        {
            memset(info, 0, sizeof(*info));
            return 0;
        }
        info->sample_frames = fact_sample_frames;
    }
    else
    {
        memset(info, 0, sizeof(*info));
        return 0;
    }

    info->format_tag = format_tag;
    info->sample_rate = sample_rate;
    info->channels = channels;
    info->bits = bits;
    info->block_align = block_align;
    info->samples_per_block = samples_per_block;
    return 1;
}

static void TitanAudio_ResetStreaming(void)
{
    g_audio.music_ring_read_total = 0u;
    g_audio.music_ring_write_total = 0u;
    g_audio.music_adpcm_block_size = 0u;
    g_audio.pending_offset = 0u;
    g_audio.pending_size = 0u;
    g_audio.music_eof_pending = 0;
}

static void TitanAudio_CloseMusic(void)
{
    if (g_audio.music_descriptor >= 0)
    {
        TitanStorageIo_Close(g_audio.music_descriptor);
    }
    g_audio.music_descriptor = -1;
    memset(&g_audio.music_info, 0, sizeof(g_audio.music_info));
    g_audio.music_byte_position = 0u;
    g_audio.music_frames_remaining = 0u;
    g_audio.music_requested = 0;
    g_audio.music_target_volume = 0;
    g_audio.music_volume = 0;
    TitanAudio_ResetStreaming();
}

static int TitanAudio_OpenMusic(void)
{
    TitanAudioWavInfo info;
    char path[TITAN_THEME_PATH_CAPACITY];
    int descriptor;

    if (!TitanAudio_ResolveMusicPath(path, sizeof(path)))
        return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanAudio_ParseWav(descriptor, &info) ||
        TitanStorageIo_Seek64(
            descriptor,
            info.data_offset,
            TITAN_STORAGE_IO_SEEK_SET
        ) != info.data_offset)
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    g_audio.music_descriptor = descriptor;
    g_audio.music_info = info;
    g_audio.music_byte_position = 0u;
    g_audio.music_frames_remaining = info.sample_frames;
    g_audio.music_adpcm_block_size = 0u;
    g_audio.music_eof_pending = 0;
    return 1;
}

static void TitanAudio_FreeClip(TitanAudioClip *clip)
{
    if (clip != 0)
    {
        free(clip->samples);
        memset(clip, 0, sizeof(*clip));
    }
}

static void TitanAudio_CancelClipLoad(void)
{
    if (g_audio.sfx_load_descriptor >= 0)
    {
        TitanStorageIo_Close(g_audio.sfx_load_descriptor);
    }
    if (g_audio.sfx_load_effect >= 0 &&
        g_audio.sfx_load_effect < TITAN_AUDIO_SFX_COUNT)
    {
        TitanAudio_FreeClip(&g_audio.clips[g_audio.sfx_load_effect]);
    }
    g_audio.sfx_load_descriptor = -1;
    g_audio.sfx_load_effect = -1;
    g_audio.sfx_load_size = 0u;
    g_audio.sfx_load_offset = 0u;
}

static int TitanAudio_BeginClipLoad(TitanAudioSfx effect)
{
    TitanAudioWavInfo info;
    TitanAudioClip *clip;
    char path[TITAN_THEME_PATH_CAPACITY];
    int descriptor;
    size_t data_size;

    if (effect < 0 || effect >= TITAN_AUDIO_SFX_COUNT ||
        g_audio.sfx_load_descriptor >= 0 ||
        g_audio.clips[effect].samples != 0 ||
        g_audio.sfx_invalid[effect])
    {
        return 0;
    }

    if (!TitanAudio_ResolveSfxPath(effect, path, sizeof(path)))
    {
        g_audio.sfx_invalid[effect] = 1u;
        return 0;
    }
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanAudio_ParseWav(descriptor, &info) ||
        info.format_tag != TITAN_AUDIO_WAV_FORMAT_PCM ||
        info.data_size > TITAN_AUDIO_MAX_SINGLE_SFX_BYTES ||
        info.data_size > SIZE_MAX ||
        g_audio.total_sfx_bytes >
            TITAN_AUDIO_MAX_TOTAL_SFX_BYTES - (size_t)info.data_size ||
        TitanStorageIo_Seek64(
            descriptor,
            info.data_offset,
            TITAN_STORAGE_IO_SEEK_SET
        ) != info.data_offset)
    {
        /* The file existed but is not a usable system effect. Do not reopen a
         * malformed/oversized asset every retry cycle. */
        g_audio.sfx_invalid[effect] = 1u;
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    data_size = (size_t)info.data_size;
    clip = &g_audio.clips[effect];
    clip->samples = (s16 *)malloc(data_size);
    if (clip->samples == 0)
    {
        TitanStorageIo_Close(descriptor);
        g_audio.sfx_invalid[effect] = 1u;
        return 0;
    }

    g_audio.sfx_load_descriptor = descriptor;
    g_audio.sfx_load_effect = (int)effect;
    g_audio.sfx_load_size = data_size;
    g_audio.sfx_load_offset = 0u;
    return 1;
}

static int TitanAudio_StepClipLoad(void)
{
    TitanAudioClip *clip;
    size_t remaining;
    size_t amount;
    TitanStorageIoResult result;
    int effect;

    if (g_audio.sfx_load_descriptor < 0 ||
        g_audio.sfx_load_effect < 0 ||
        g_audio.sfx_load_effect >= TITAN_AUDIO_SFX_COUNT)
    {
        return 0;
    }

    effect = g_audio.sfx_load_effect;
    clip = &g_audio.clips[effect];
    remaining = g_audio.sfx_load_size - g_audio.sfx_load_offset;
    amount = remaining;
    if (amount > TITAN_AUDIO_SFX_LOAD_BUDGET)
    {
        amount = TITAN_AUDIO_SFX_LOAD_BUDGET;
    }

    if (amount > 0u)
    {
        result = TitanStorageIo_ReadSome(
            g_audio.sfx_load_descriptor,
            ((unsigned char *)clip->samples) + g_audio.sfx_load_offset,
            amount
        );
        if (result <= 0 || (size_t)result > amount)
        {
            TitanAudio_CancelClipLoad();
            return 0;
        }
        g_audio.sfx_load_offset += (size_t)result;
    }

    if (g_audio.sfx_load_offset < g_audio.sfx_load_size)
    {
        return 1;
    }

    TitanStorageIo_Close(g_audio.sfx_load_descriptor);
    g_audio.sfx_load_descriptor = -1;
    /* Publish frame_count last. The realtime thread treats non-zero
     * frame_count as the readiness flag and can never observe a half-filled
     * clip (for example before volume/byte_count are initialized). */
    clip->byte_count = g_audio.sfx_load_size;
    clip->volume = g_titan_audio_sfx_volumes[effect];
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
    clip->frame_count =
        (unsigned int)(g_audio.sfx_load_size / TITAN_AUDIO_BYTES_PER_FRAME);
    g_audio.total_sfx_bytes += g_audio.sfx_load_size;
    g_audio.loaded_sfx_count++;
    g_audio.sfx_load_effect = -1;
    g_audio.sfx_load_size = 0u;
    g_audio.sfx_load_offset = 0u;

    /* Start the next effect on the next frame instead of waiting another full
     * retry interval. */
    g_audio.asset_retry_frames = TITAN_AUDIO_ASSET_RETRY_FRAMES;
    return 1;
}

static int TitanAudio_AllAssetsLoaded(void)
{
    unsigned int index;

    if (g_audio.music_descriptor < 0)
    {
        return 0;
    }
    for (index = 0u; index < TITAN_AUDIO_SFX_COUNT; index++)
    {
        if (g_audio.clips[index].samples == 0 && !g_audio.sfx_invalid[index])
        {
            return 0;
        }
    }
    return 1;
}

static int TitanAudio_BeginNextMissingAsset(void)
{
    unsigned int index;

    for (index = 0u; index < TITAN_AUDIO_SFX_COUNT; index++)
    {
        TitanAudioSfx effect;

        effect = g_titan_audio_sfx_load_order[index];
        if (g_audio.clips[effect].samples == 0 && !g_audio.sfx_invalid[effect])
        {
            if (index >= TITAN_AUDIO_STARTUP_CRITICAL_SFX_COUNT &&
                !g_audio.deferred_asset_io_allowed)
            {
                return -1;
            }
            if (TitanAudio_BeginClipLoad(effect))
            {
                return 1;
            }
            if (g_audio.sfx_invalid[effect])
            {
                continue;
            }
            return 0;
        }
    }
    return 0;
}

static void TitanAudio_UpdateAssetStatus(void)
{
    g_audio.status =
        (g_audio.music_descriptor >= 0 || g_audio.loaded_sfx_count > 0u)
        ? TITAN_AUDIO_STATUS_READY
        : TITAN_AUDIO_STATUS_READY_NO_ASSETS;
}

static void TitanAudio_ClearVoices(void)
{
    memset(g_audio.voices, 0, sizeof(g_audio.voices));
}

static int TitanAudio_HasActiveVoices(void)
{
    unsigned int index;
    for (index = 0u; index < TITAN_AUDIO_MAX_VOICES; index++)
    {
        if (g_audio.voices[index].active)
        {
            return 1;
        }
    }
    return 0;
}

static int TitanAudio_IsEffectActive(TitanAudioSfx effect)
{
    unsigned int index;
    for (index = 0u; index < TITAN_AUDIO_MAX_VOICES; index++)
    {
        if (g_audio.voices[index].active &&
            g_audio.voices[index].effect == effect)
        {
            return 1;
        }
    }
    return 0;
}

static int TitanAudio_PlaySfxInternal(TitanAudioSfx effect, int force)
{
    const TitanAudioClip *clip;
    unsigned int index;
    unsigned int selected;
    unsigned int oldest_serial;

    if (!g_audio.ready || effect < 0 || effect >= TITAN_AUDIO_SFX_COUNT ||
        (!force && g_audio.handoff_paused))
    {
        return 0;
    }

    clip = &g_audio.clips[effect];
    if (clip->samples == 0 || clip->frame_count == 0u)
    {
        return 0;
    }

    if (effect == TITAN_AUDIO_SFX_HOME_GRID_NAV ||
        effect == TITAN_AUDIO_SFX_TOP_MENU_NAV ||
        effect == TITAN_AUDIO_SFX_INTERFACE_MENU_NAV)
    {
        for (index = 0u; index < TITAN_AUDIO_MAX_VOICES; index++)
        {
            if (g_audio.voices[index].active &&
                g_audio.voices[index].effect == effect)
            {
                g_audio.voices[index].frame_position = 0u;
                g_audio.voices[index].serial = ++g_audio.voice_serial;
                return 1;
            }
        }
    }

    selected = TITAN_AUDIO_MAX_VOICES;
    oldest_serial = UINT_MAX;
    for (index = 0u; index < TITAN_AUDIO_MAX_VOICES; index++)
    {
        if (!g_audio.voices[index].active)
        {
            selected = index;
            break;
        }
        if (g_audio.voices[index].serial < oldest_serial)
        {
            oldest_serial = g_audio.voices[index].serial;
            selected = index;
        }
    }

    if (selected >= TITAN_AUDIO_MAX_VOICES)
    {
        return 0;
    }

    g_audio.voices[selected].active = 1;
    g_audio.voices[selected].effect = effect;
    g_audio.voices[selected].clip = clip;
    g_audio.voices[selected].frame_position = 0u;
    g_audio.voices[selected].serial = ++g_audio.voice_serial;
    return 1;
}

static int TitanAudio_ClampSample(int value);

/* Music uses a single-producer/single-consumer ring.  The storage worker is
 * the only producer and the realtime/main thread is the only consumer.  Each
 * side owns one monotonic 32-bit cursor, so neither thread performs a shared
 * read-modify-write while fileXio is active.  Unsigned subtraction remains
 * valid across cursor wrap because the ring is far smaller than 2^31 bytes. */
static void TitanAudio_CompilerBarrier(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static size_t TitanAudio_MusicBufferedBytes(void)
{
    unsigned int write_total;
    unsigned int read_total;
    unsigned int available;

    write_total = g_audio.music_ring_write_total;
    TitanAudio_CompilerBarrier();
    read_total = g_audio.music_ring_read_total;
    available = write_total - read_total;
    if (available > TITAN_AUDIO_MUSIC_RING_BYTES)
    {
        return 0u;
    }
    return (size_t)available;
}

static size_t TitanAudio_MusicFreeBytes(void)
{
    return TITAN_AUDIO_MUSIC_RING_BYTES - TitanAudio_MusicBufferedBytes();
}

static int TitanAudio_RingWrite(const unsigned char *data, size_t byte_count)
{
    unsigned int write_total;
    size_t write_offset;
    size_t first;

    if (data == 0 || byte_count == 0u ||
        byte_count > TitanAudio_MusicFreeBytes())
    {
        return 0;
    }

    write_total = g_audio.music_ring_write_total;
    write_offset = (size_t)(write_total % TITAN_AUDIO_MUSIC_RING_BYTES);
    first = TITAN_AUDIO_MUSIC_RING_BYTES - write_offset;
    if (first > byte_count)
    {
        first = byte_count;
    }
    memcpy(g_music_ring + write_offset, data, first);
    if (first < byte_count)
    {
        memcpy(g_music_ring, data + first, byte_count - first);
    }

    /* Publish the new cursor only after all PCM bytes are visible. */
    TitanAudio_CompilerBarrier();
    g_audio.music_ring_write_total = write_total + (unsigned int)byte_count;
    return 1;
}

static const int g_titan_audio_ima_index_table[8] =
{
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int g_titan_audio_ima_step_table[89] =
{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static s16 TitanAudio_DecodeImaNibble(
    unsigned int code,
    int *predictor,
    int *step_index
)
{
    int difference;
    int step;

    step = g_titan_audio_ima_step_table[*step_index];
    difference = step >> 3;
    if (code & 1u)
    {
        difference += step >> 2;
    }
    if (code & 2u)
    {
        difference += step >> 1;
    }
    if (code & 4u)
    {
        difference += step;
    }

    if (code & 8u)
    {
        *predictor -= difference;
    }
    else
    {
        *predictor += difference;
    }
    *predictor = TitanAudio_ClampSample(*predictor);

    *step_index += g_titan_audio_ima_index_table[code & 7u];
    if (*step_index < 0)
    {
        *step_index = 0;
    }
    else if (*step_index > 88)
    {
        *step_index = 88;
    }
    return (s16)*predictor;
}

static int TitanAudio_DecodeImaBlock(
    const unsigned char *block,
    unsigned int block_bytes,
    unsigned int samples_per_block,
    unsigned int requested_frames,
    s16 *output,
    size_t output_samples
)
{
    int predictors[TITAN_AUDIO_CHANNELS];
    int step_indices[TITAN_AUDIO_CHANNELS];
    unsigned int expected_frames;
    unsigned int frame;
    unsigned int group_offset;
    unsigned int channel;

    if (block == 0 || output == 0 || block_bytes < 16u ||
        block_bytes > TITAN_AUDIO_MAX_ADPCM_BLOCK_BYTES ||
        ((block_bytes - (4u * TITAN_AUDIO_CHANNELS)) % 8u) != 0u)
    {
        return 0;
    }

    expected_frames =
        (((block_bytes - (4u * TITAN_AUDIO_CHANNELS)) * 8u) /
         (4u * TITAN_AUDIO_CHANNELS)) + 1u;
    if (samples_per_block != expected_frames || requested_frames == 0u ||
        requested_frames > samples_per_block ||
        output_samples < (size_t)requested_frames * TITAN_AUDIO_CHANNELS)
    {
        return 0;
    }

    for (channel = 0u; channel < TITAN_AUDIO_CHANNELS; channel++)
    {
        unsigned int predictor_value;
        unsigned int header_offset;

        header_offset = channel * 4u;
        predictor_value = TitanAudio_ReadU16Le(block + header_offset);
        predictors[channel] = predictor_value >= 0x8000u
            ? (int)predictor_value - 0x10000
            : (int)predictor_value;
        step_indices[channel] = block[header_offset + 2u];
        if (step_indices[channel] > 88 || block[header_offset + 3u] != 0u)
        {
            return 0;
        }
        output[channel] = (s16)predictors[channel];
    }

    frame = 1u;
    group_offset = 4u * TITAN_AUDIO_CHANNELS;
    while (frame < requested_frames && group_offset + 8u <= block_bytes)
    {
        s16 decoded[TITAN_AUDIO_CHANNELS][8];
        unsigned int sample;

        for (channel = 0u; channel < TITAN_AUDIO_CHANNELS; channel++)
        {
            const unsigned char *encoded;
            unsigned int byte_index;

            encoded = block + group_offset + (channel * 4u);
            for (byte_index = 0u; byte_index < 4u; byte_index++)
            {
                decoded[channel][byte_index * 2u] =
                    TitanAudio_DecodeImaNibble(
                        encoded[byte_index] & 0x0Fu,
                        &predictors[channel],
                        &step_indices[channel]
                    );
                decoded[channel][(byte_index * 2u) + 1u] =
                    TitanAudio_DecodeImaNibble(
                        encoded[byte_index] >> 4u,
                        &predictors[channel],
                        &step_indices[channel]
                    );
            }
        }

        for (sample = 0u; sample < 8u && frame < requested_frames; sample++)
        {
            for (channel = 0u; channel < TITAN_AUDIO_CHANNELS; channel++)
            {
                output[(frame * TITAN_AUDIO_CHANNELS) + channel] =
                    decoded[channel][sample];
            }
            frame++;
        }
        group_offset += 8u;
    }

    return frame == requested_frames;
}

static void TitanAudio_FailMusicStream(void)
{
    g_audio.status = TITAN_AUDIO_STATUS_STREAM_IO_FAILED;
    TitanAudio_CloseMusic();
}

static void TitanAudio_FillPcmMusicRing(void)
{
    size_t amount;
    TitanStorageIoResult result;
    TitanStorageIoSize remaining;
    TitanStorageIoSize remaining_pcm;

    remaining = g_audio.music_info.data_size - g_audio.music_byte_position;
    remaining_pcm =
        g_audio.music_frames_remaining * TITAN_AUDIO_BYTES_PER_FRAME;
    if (remaining_pcm < remaining)
    {
        remaining = remaining_pcm;
    }

    amount = TitanAudio_MusicFreeBytes();
    if (amount > TITAN_AUDIO_MUSIC_READ_BUDGET)
    {
        amount = TITAN_AUDIO_MUSIC_READ_BUDGET;
    }
    if ((TitanStorageIoSize)amount > remaining)
    {
        amount = (size_t)remaining;
    }
    amount -= amount % TITAN_AUDIO_BYTES_PER_FRAME;
    if (amount == 0u)
    {
        return;
    }

    /* fileXio never writes directly into the consumer-visible ring.  Read to
     * worker-owned staging RAM first, then publish atomically via RingWrite. */
    g_audio.music_read_calls++;
    result = TitanStorageIo_ReadSome(
        g_audio.music_descriptor,
        g_music_io_buffer,
        amount
    );
    if (result <= 0 || (size_t)result > amount ||
        ((size_t)result % TITAN_AUDIO_BYTES_PER_FRAME) != 0u ||
        !TitanAudio_RingWrite(g_music_io_buffer, (size_t)result))
    {
        TitanAudio_FailMusicStream();
        return;
    }

    g_audio.music_byte_position += (TitanStorageIoSize)result;
    g_audio.music_frames_remaining -=
        (TitanStorageIoSize)result / TITAN_AUDIO_BYTES_PER_FRAME;
}

static void TitanAudio_FillImaMusicRing(void)
{
    unsigned int block_bytes;
    unsigned int frames;
    size_t decoded_bytes;

    block_bytes = g_audio.music_info.block_align;
    if (g_audio.music_adpcm_block_size < block_bytes)
    {
        size_t amount;
        TitanStorageIoResult result;
        TitanStorageIoSize remaining;

        remaining = g_audio.music_info.data_size - g_audio.music_byte_position;
        amount = block_bytes - g_audio.music_adpcm_block_size;
        if (amount > TITAN_AUDIO_MUSIC_READ_BUDGET)
        {
            amount = TITAN_AUDIO_MUSIC_READ_BUDGET;
        }
        if ((TitanStorageIoSize)amount > remaining)
        {
            TitanAudio_FailMusicStream();
            return;
        }

        g_audio.music_read_calls++;
        result = TitanStorageIo_ReadSome(
            g_audio.music_descriptor,
            g_music_adpcm_block + g_audio.music_adpcm_block_size,
            amount
        );
        if (result <= 0 || (size_t)result > amount)
        {
            TitanAudio_FailMusicStream();
            return;
        }
        g_audio.music_adpcm_block_size += (size_t)result;
        g_audio.music_byte_position += (TitanStorageIoSize)result;
        if (g_audio.music_adpcm_block_size < block_bytes)
        {
            return;
        }
    }

    frames = g_audio.music_info.samples_per_block;
    if ((TitanStorageIoSize)frames > g_audio.music_frames_remaining)
    {
        frames = (unsigned int)g_audio.music_frames_remaining;
    }
    decoded_bytes = (size_t)frames * TITAN_AUDIO_BYTES_PER_FRAME;
    if (decoded_bytes > TitanAudio_MusicFreeBytes())
    {
        return;
    }
    if (!TitanAudio_DecodeImaBlock(
            g_music_adpcm_block,
            block_bytes,
            g_audio.music_info.samples_per_block,
            frames,
            g_music_adpcm_decoded,
            sizeof(g_music_adpcm_decoded) / sizeof(g_music_adpcm_decoded[0])
        ) ||
        !TitanAudio_RingWrite(
            (const unsigned char *)g_music_adpcm_decoded,
            decoded_bytes
        ))
    {
        TitanAudio_FailMusicStream();
        return;
    }

    g_audio.music_frames_remaining -= frames;
    g_audio.music_adpcm_block_size = 0u;
}

static void TitanAudio_FillMusicRing(void)
{
    if (g_audio.music_descriptor < 0 || !g_audio.music_requested ||
        g_audio.handoff_paused || g_audio.exclusive_playback)
    {
        return;
    }

    if (g_audio.music_eof_pending)
    {
        if (TitanStorageIo_Seek64(
                g_audio.music_descriptor,
                g_audio.music_info.data_offset,
                TITAN_STORAGE_IO_SEEK_SET
            ) != g_audio.music_info.data_offset)
        {
            TitanAudio_FailMusicStream();
            return;
        }
        g_audio.music_byte_position = 0u;
        g_audio.music_frames_remaining = g_audio.music_info.sample_frames;
        g_audio.music_adpcm_block_size = 0u;
        g_audio.music_eof_pending = 0;
        return; /* At most one storage operation per update. */
    }

    if (g_audio.music_frames_remaining == 0u)
    {
        g_audio.music_eof_pending = 1;
        return;
    }
    if (g_audio.music_byte_position >= g_audio.music_info.data_size)
    {
        TitanAudio_FailMusicStream();
        return;
    }

    if (g_audio.music_info.format_tag == TITAN_AUDIO_WAV_FORMAT_PCM)
    {
        TitanAudio_FillPcmMusicRing();
    }
    else if (g_audio.music_info.format_tag == TITAN_AUDIO_WAV_FORMAT_IMA_ADPCM)
    {
        TitanAudio_FillImaMusicRing();
    }
    else
    {
        TitanAudio_FailMusicStream();
    }
}

static s16 TitanAudio_PopMusicSample(void)
{
    unsigned char bytes[2];
    unsigned int read_total;
    unsigned int write_total;
    size_t read_offset;

    read_total = g_audio.music_ring_read_total;
    write_total = g_audio.music_ring_write_total;
    TitanAudio_CompilerBarrier();
    if ((unsigned int)(write_total - read_total) < sizeof(bytes))
    {
        return 0;
    }

    read_offset = (size_t)(read_total % TITAN_AUDIO_MUSIC_RING_BYTES);
    bytes[0] = g_music_ring[read_offset];
    bytes[1] = g_music_ring[(read_offset + 1u) % TITAN_AUDIO_MUSIC_RING_BYTES];
    TitanAudio_CompilerBarrier();
    g_audio.music_ring_read_total = read_total + (unsigned int)sizeof(bytes);
    return (s16)TitanAudio_ReadU16Le(bytes);
}

static s16 TitanAudio_PopExternalSample(void)
{
    unsigned char bytes[2];

    if (g_audio.external_ring_size < sizeof(bytes))
    {
        return 0;
    }
    bytes[0] = g_external_ring[g_audio.external_ring_read];
    g_audio.external_ring_read =
        (g_audio.external_ring_read + 1u) % TITAN_AUDIO_EXTERNAL_RING_BYTES;
    bytes[1] = g_external_ring[g_audio.external_ring_read];
    g_audio.external_ring_read =
        (g_audio.external_ring_read + 1u) % TITAN_AUDIO_EXTERNAL_RING_BYTES;
    g_audio.external_ring_size -= sizeof(bytes);
    return (s16)TitanAudio_ReadU16Le(bytes);
}

static int TitanAudio_ClampSample(int value)
{
    if (value < -32768)
    {
        return -32768;
    }
    if (value > 32767)
    {
        return 32767;
    }
    return value;
}

static void TitanAudio_MixPendingChunk(void)
{
    int start_music_volume;
    int next_music_volume;
    unsigned int frame;

    start_music_volume = g_audio.external_stream_active
        ? 0
        : g_audio.music_volume;
    next_music_volume = start_music_volume;
    if (next_music_volume < g_audio.music_target_volume)
    {
        next_music_volume += TITAN_AUDIO_MUSIC_RAMP_STEP;
        if (next_music_volume > g_audio.music_target_volume)
        {
            next_music_volume = g_audio.music_target_volume;
        }
    }
    else if (next_music_volume > g_audio.music_target_volume)
    {
        next_music_volume -= TITAN_AUDIO_MUSIC_RAMP_STEP;
        if (next_music_volume < g_audio.music_target_volume)
        {
            next_music_volume = g_audio.music_target_volume;
        }
    }

    for (frame = 0u; frame < TITAN_AUDIO_OUTPUT_FRAMES; frame++)
    {
        unsigned int sample_index;
        unsigned int voice_index;
        int volume;
        int mixed_left;
        int mixed_right;

        sample_index = frame * TITAN_AUDIO_CHANNELS;
        volume = start_music_volume +
            ((next_music_volume - start_music_volume) * (int)frame) /
            (int)TITAN_AUDIO_OUTPUT_FRAMES;
        if (g_audio.external_stream_active)
        {
            mixed_left = (int)TitanAudio_PopExternalSample();
            mixed_right = (int)TitanAudio_PopExternalSample();
        }
        else
        {
            mixed_left = ((int)TitanAudio_PopMusicSample() * volume) / 100;
            mixed_right = ((int)TitanAudio_PopMusicSample() * volume) / 100;
        }

        for (voice_index = 0u; voice_index < TITAN_AUDIO_MAX_VOICES; voice_index++)
        {
            TitanAudioVoice *voice;
            unsigned int clip_sample;

            voice = &g_audio.voices[voice_index];
            if (!voice->active || voice->clip == 0)
            {
                continue;
            }
            if (voice->frame_position >= voice->clip->frame_count)
            {
                voice->active = 0;
                continue;
            }

            clip_sample = voice->frame_position * TITAN_AUDIO_CHANNELS;
            mixed_left +=
                ((int)voice->clip->samples[clip_sample] * voice->clip->volume) / 100;
            mixed_right +=
                ((int)voice->clip->samples[clip_sample + 1u] * voice->clip->volume) / 100;
            voice->frame_position++;
            if (voice->frame_position >= voice->clip->frame_count)
            {
                voice->active = 0;
            }
        }

        g_mix_buffer[sample_index] = (s16)TitanAudio_ClampSample(mixed_left);
        g_mix_buffer[sample_index + 1u] = (s16)TitanAudio_ClampSample(mixed_right);
    }

    g_audio.music_volume = next_music_volume;
    memcpy(g_pending_buffer, g_mix_buffer, sizeof(g_pending_buffer));
    g_audio.pending_offset = 0u;
    g_audio.pending_size = sizeof(g_pending_buffer);
}

static int TitanAudio_HasOutput(void)
{
    return g_audio.pending_size > g_audio.pending_offset ||
        g_audio.external_ring_size > 0u ||
        (g_audio.music_requested && TitanAudio_MusicBufferedBytes() > 0u) ||
        g_audio.music_volume > 0 ||
        g_audio.music_target_volume > 0 ||
        TitanAudio_HasActiveVoices();
}

static void TitanAudio_DrainPending(void)
{
    unsigned int pushes;

    for (pushes = 0u;
         pushes < TITAN_AUDIO_MAX_PUSHES_PER_UPDATE &&
         g_audio.pending_offset < g_audio.pending_size;
         pushes++)
    {
        size_t remaining;
        int available;
        int amount;
        int result;

        available = audsrv_available();
        if (available <= 0)
        {
            break;
        }
        remaining = g_audio.pending_size - g_audio.pending_offset;
        amount = available;
        if ((size_t)amount > remaining)
        {
            amount = (int)remaining;
        }
        if (amount <= 0)
        {
            break;
        }

        result = audsrv_play_audio(
            (const char *)g_pending_buffer + g_audio.pending_offset,
            amount
        );
        if (result <= 0 || result > amount)
        {
            break;
        }
        g_audio.pending_offset += (size_t)result;
    }

    if (g_audio.pending_offset >= g_audio.pending_size)
    {
        g_audio.pending_offset = 0u;
        g_audio.pending_size = 0u;
    }
}

int TitanAudio_Initialize(void)
{
    audsrv_fmt_t format;
    int module_result;
    int result;

    if (g_audio.ready)
    {
        return 1;
    }

    TitanAudio_Shutdown();
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.music_descriptor = -1;
    g_audio.sfx_load_descriptor = -1;
    g_audio.sfx_load_effect = -1;
    g_audio.status = TITAN_AUDIO_STATUS_NOT_INITIALIZED;

    /* A bootloader or previous service may leave LIBSD/AUDSRV resident.
     * Duplicate module-load return codes are therefore only diagnostics; the
     * authoritative readiness check is whether the AUDSRV RPC bind succeeds. */
    result = SifLoadModule("rom0:LIBSD", 0, 0);
    module_result = 0;
    {
        int audsrv_module_result;
        int audsrv_exec_result;

        audsrv_module_result = 0;
        audsrv_exec_result = SifExecModuleBuffer(
            titan_audsrv_irx,
            (int)size_titan_audsrv_irx,
            0,
            "",
            &audsrv_module_result
        );
        module_result =
            (audsrv_exec_result < 0 || audsrv_module_result < 0)
            ? -1
            : 0;
    }

    if (audsrv_init() != AUDSRV_ERR_NOERROR)
    {
        if (result < 0)
        {
            g_audio.status = TITAN_AUDIO_STATUS_LIBSD_FAILED;
        }
        else if (module_result < 0)
        {
            g_audio.status = TITAN_AUDIO_STATUS_IRX_FAILED;
        }
        else
        {
            g_audio.status = TITAN_AUDIO_STATUS_RPC_FAILED;
        }
        return 0;
    }
    g_audio.audsrv_started = 1;

    format.freq = TITAN_AUDIO_SAMPLE_RATE;
    format.bits = TITAN_AUDIO_BITS;
    format.channels = TITAN_AUDIO_CHANNELS;
    if (audsrv_set_format(&format) != AUDSRV_ERR_NOERROR)
    {
        g_audio.status = TITAN_AUDIO_STATUS_FORMAT_FAILED;
        TitanAudio_Shutdown();
        g_audio.status = TITAN_AUDIO_STATUS_FORMAT_FAILED;
        return 0;
    }

    if (audsrv_set_volume(TITAN_AUDIO_MASTER_VOLUME) != AUDSRV_ERR_NOERROR)
    {
        g_audio.status = TITAN_AUDIO_STATUS_FORMAT_FAILED;
        TitanAudio_Shutdown();
        g_audio.status = TITAN_AUDIO_STATUS_FORMAT_FAILED;
        return 0;
    }
    audsrv_stop_audio();

    /* Asset files stay byte-for-byte external, but are loaded incrementally.
     * Never read hundreds of KiB of SFX before the first animated frames. */
    g_audio.asset_retry_frames =
        TITAN_AUDIO_ASSET_RETRY_FRAMES - TITAN_AUDIO_ASSET_INITIAL_DELAY_FRAMES;
    TitanAudio_UpdateAssetStatus();
    TitanAudio_ClearVoices();
    g_audio.ready = 1;
    return 1;
}

void TitanAudio_Shutdown(void)
{
    unsigned int index;
    int had_audsrv;

    had_audsrv = g_audio.audsrv_started;
    if (had_audsrv)
    {
        audsrv_stop_audio();
    }
    if (g_audio.music_descriptor >= 0)
    {
        TitanStorageIo_Close(g_audio.music_descriptor);
    }
    if (g_audio.sfx_load_descriptor >= 0)
    {
        TitanStorageIo_Close(g_audio.sfx_load_descriptor);
        g_audio.sfx_load_descriptor = -1;
    }
    for (index = 0u; index < TITAN_AUDIO_SFX_COUNT; index++)
    {
        TitanAudio_FreeClip(&g_audio.clips[index]);
    }
    if (had_audsrv)
    {
        audsrv_quit();
    }

    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.music_descriptor = -1;
    g_audio.sfx_load_descriptor = -1;
    g_audio.sfx_load_effect = -1;
    g_audio.status = TITAN_AUDIO_STATUS_NOT_INITIALIZED;
}

void TitanAudio_Update(void)
{
    if (!g_audio.ready || g_audio.handoff_paused)
    {
        return;
    }

#if !defined(__EE__) && !defined(_EE)
    /* Host tests have no storage worker. Exercise exactly the same worker-side
     * pumps synchronously so the runtime contract remains testable. */
    (void)TitanAudio_StorageWorkerPumpRealtime();
    (void)TitanAudio_StorageWorkerPumpDeferred();
#endif

    /* PS2 realtime path: AUDSRV + RAM mixing only.  No fileXio/TitanStorageIo
     * call is allowed here; the storage worker owns the serialized I/O lane. */
    TitanAudio_DrainPending();
    if (g_audio.pending_size == 0u && TitanAudio_HasOutput())
    {
        TitanAudio_MixPendingChunk();
        TitanAudio_DrainPending();
    }
}

int TitanAudio_StorageWorkerNeedsRealtime(void)
{
    if (!g_audio.ready || g_audio.handoff_paused ||
        g_audio.exclusive_playback || g_audio.external_stream_active ||
        !g_audio.music_requested)
    {
        return 0;
    }
    return TitanAudio_MusicBufferedBytes() <
        TITAN_AUDIO_MUSIC_RING_REFILL_LOW_WATER;
}

int TitanAudio_StorageWorkerPumpRealtime(void)
{
    unsigned int before_reads;
    size_t before_buffered;

    if (!g_audio.ready || g_audio.handoff_paused ||
        g_audio.exclusive_playback || g_audio.external_stream_active ||
        !g_audio.music_requested)
    {
        return 0;
    }

    if (g_audio.music_descriptor < 0)
    {
        if (!TitanAudio_OpenMusic())
        {
            return 0;
        }
        TitanAudio_UpdateAssetStatus();
        return 1;
    }

    before_buffered = TitanAudio_MusicBufferedBytes();
    if (before_buffered >= TITAN_AUDIO_MUSIC_RING_REFILL_HIGH_WATER)
    {
        return 0;
    }

    before_reads = g_audio.music_read_calls;
    TitanAudio_FillMusicRing();
    return g_audio.music_read_calls != before_reads ||
        TitanAudio_MusicBufferedBytes() != before_buffered;
}

int TitanAudio_StorageWorkerPumpDeferred(void)
{
    int asset_result;

    if (!g_audio.ready || g_audio.handoff_paused)
    {
        return 0;
    }

    if (g_audio.sfx_load_descriptor >= 0)
    {
        (void)TitanAudio_StepClipLoad();
        TitanAudio_UpdateAssetStatus();
        return 1;
    }

    if (TitanAudio_AllAssetsLoaded() ||
        g_audio.asset_retry_attempts >= TITAN_AUDIO_ASSET_RETRY_LIMIT)
    {
        return 0;
    }

    g_audio.asset_retry_frames++;
    if (g_audio.asset_retry_frames < TITAN_AUDIO_ASSET_RETRY_FRAMES)
    {
        return 0;
    }

    g_audio.asset_retry_frames = 0u;
    g_audio.asset_retry_attempts++;
    asset_result = TitanAudio_BeginNextMissingAsset();
    if (asset_result > 0)
    {
        if (g_audio.asset_retry_attempts > 0u)
        {
            g_audio.asset_retry_attempts--;
        }
        g_audio.asset_retry_frames = TITAN_AUDIO_ASSET_RETRY_FRAMES;
        TitanAudio_UpdateAssetStatus();
        return 1;
    }
    if (asset_result < 0)
    {
        if (g_audio.asset_retry_attempts > 0u)
        {
            g_audio.asset_retry_attempts--;
        }
        g_audio.asset_retry_frames = 0u;
    }
    TitanAudio_UpdateAssetStatus();
    return 0;
}

void TitanAudio_SetDeferredAssetIoAllowed(int allowed)
{
    int normalized;

    normalized = allowed != 0;
    if (normalized && !g_audio.deferred_asset_io_allowed)
    {
        /* Home just became available: make deferred effects eligible on the
         * next worker poll without waiting for the late-mount retry timer. */
        g_audio.asset_retry_frames = TITAN_AUDIO_ASSET_RETRY_FRAMES;
    }
    g_audio.deferred_asset_io_allowed = normalized;
}

int TitanAudio_IsReady(void)
{
    return g_audio.ready;
}

const char *TitanAudio_GetStatusName(void)
{
    switch (g_audio.status)
    {
        case TITAN_AUDIO_STATUS_READY:
            return "Ready";
        case TITAN_AUDIO_STATUS_READY_NO_ASSETS:
            return "Ready; no audio assets found";
        case TITAN_AUDIO_STATUS_LIBSD_FAILED:
            return "LIBSD unavailable";
        case TITAN_AUDIO_STATUS_IRX_FAILED:
            return "AUDSRV IRX unavailable";
        case TITAN_AUDIO_STATUS_RPC_FAILED:
            return "AUDSRV RPC failed";
        case TITAN_AUDIO_STATUS_FORMAT_FAILED:
            return "PCM format rejected";
        case TITAN_AUDIO_STATUS_STREAM_IO_FAILED:
            return "Music stream I/O failed";
        case TITAN_AUDIO_STATUS_NOT_INITIALIZED:
        default:
            return "Not initialized";
    }
}

unsigned int TitanAudio_GetLoadedSfxCount(void)
{
    return g_audio.loaded_sfx_count;
}

void TitanAudio_StartMusic(void)
{
    if (!g_audio.ready || g_audio.handoff_paused)
    {
        return;
    }
    if (TitanTheme_GetAudioStyle()->music_home_only &&
        !TitanThemeLifecycle_MusicAllowed())
    {
        g_audio.music_requested = 0;
        g_audio.music_target_volume = 0;
        return;
    }

    /* Request only. Opening/parsing/seeking the 100+ MiB music asset belongs
     * to the storage worker so a busy USB RPC can never park the UI thread. */
    g_audio.music_requested = 1;
    g_audio.music_target_volume = g_audio.external_stream_active
        ? 0
        : TITAN_AUDIO_MUSIC_TARGET_VOLUME;
}

void TitanAudio_StopMusic(void)
{
    g_audio.music_requested = 0;
    g_audio.music_target_volume = 0;
}

void TitanAudio_ReloadThemeAssets(void)
{
    unsigned int index;
    int requested;

    requested = g_audio.music_requested;
    TitanAudio_CloseMusic();
    TitanAudio_CancelClipLoad();
    TitanAudio_ClearVoices();
    for (index = 0u; index < TITAN_AUDIO_SFX_COUNT; index++)
        TitanAudio_FreeClip(&g_audio.clips[index]);
    memset(g_audio.sfx_invalid, 0, sizeof(g_audio.sfx_invalid));
    g_audio.total_sfx_bytes = 0u;
    g_audio.loaded_sfx_count = 0u;
    g_audio.asset_retry_frames = TITAN_AUDIO_ASSET_RETRY_FRAMES;
    g_audio.asset_retry_attempts = 0u;
    g_audio.music_requested = requested;
    if (requested)
        g_audio.music_target_volume = TITAN_AUDIO_MUSIC_TARGET_VOLUME;
    TitanAudio_UpdateAssetStatus();
}

int TitanAudio_BeginExternalStream(void)
{
    if (!g_audio.ready || g_audio.handoff_paused)
    {
        return 0;
    }

    g_audio.external_stream_active = 1;
    g_audio.external_ring_read = 0u;
    g_audio.external_ring_write = 0u;
    g_audio.external_ring_size = 0u;
    g_audio.music_target_volume = 0;
    g_audio.music_volume = 0;
    g_audio.pending_offset = 0u;
    g_audio.pending_size = 0u;
    TitanAudio_ClearVoices();
    audsrv_stop_audio();
    return 1;
}

void TitanAudio_EndExternalStream(void)
{
    if (!g_audio.external_stream_active)
    {
        return;
    }
    g_audio.external_stream_active = 0;
    g_audio.external_ring_read = 0u;
    g_audio.external_ring_write = 0u;
    g_audio.external_ring_size = 0u;
    g_audio.pending_offset = 0u;
    g_audio.pending_size = 0u;
    audsrv_stop_audio();
    if (g_audio.music_requested)
    {
        g_audio.music_target_volume = TITAN_AUDIO_MUSIC_TARGET_VOLUME;
    }
}

unsigned int TitanAudio_SubmitExternalPcm(
    const short *interleaved_stereo,
    unsigned int frames
)
{
    const unsigned char *bytes;
    size_t requested;
    size_t free_bytes;
    size_t accepted;
    size_t first;

    if (!g_audio.external_stream_active || interleaved_stereo == 0 || frames == 0u)
    {
        return 0u;
    }

    requested = (size_t)frames * TITAN_AUDIO_BYTES_PER_FRAME;
    free_bytes = TITAN_AUDIO_EXTERNAL_RING_BYTES - g_audio.external_ring_size;
    accepted = requested < free_bytes ? requested : free_bytes;
    accepted -= accepted % TITAN_AUDIO_BYTES_PER_FRAME;
    if (accepted == 0u)
    {
        return 0u;
    }

    bytes = (const unsigned char *)interleaved_stereo;
    first = TITAN_AUDIO_EXTERNAL_RING_BYTES - g_audio.external_ring_write;
    if (first > accepted)
    {
        first = accepted;
    }
    memcpy(g_external_ring + g_audio.external_ring_write, bytes, first);
    if (accepted > first)
    {
        memcpy(g_external_ring, bytes + first, accepted - first);
    }
    g_audio.external_ring_write =
        (g_audio.external_ring_write + accepted) % TITAN_AUDIO_EXTERNAL_RING_BYTES;
    g_audio.external_ring_size += accepted;
    return (unsigned int)(accepted / TITAN_AUDIO_BYTES_PER_FRAME);
}

unsigned int TitanAudio_GetExternalBufferedFrames(void)
{
    return (unsigned int)(g_audio.external_ring_size / TITAN_AUDIO_BYTES_PER_FRAME);
}

unsigned int TitanAudio_GetExternalFreeFrames(void)
{
    return (unsigned int)(
        (TITAN_AUDIO_EXTERNAL_RING_BYTES - g_audio.external_ring_size) /
        TITAN_AUDIO_BYTES_PER_FRAME
    );
}

int TitanAudio_IsExternalStreamActive(void)
{
    return g_audio.external_stream_active;
}

int TitanAudio_PlaySfx(TitanAudioSfx effect)
{
    return TitanAudio_PlaySfxInternal(effect, 0);
}

void TitanAudio_PrepareHandoff(void)
{
    if (!g_audio.ready)
    {
        return;
    }
    g_audio.handoff_paused = 1;
    g_audio.external_stream_active = 0;
    g_audio.external_ring_read = 0u;
    g_audio.external_ring_write = 0u;
    g_audio.external_ring_size = 0u;
    g_audio.music_requested = 0;
    g_audio.music_target_volume = 0;
    g_audio.music_volume = 0;
    TitanAudio_ClearVoices();
    TitanAudio_ResetStreaming();
    audsrv_stop_audio();
}

void TitanAudio_CancelHandoff(void)
{
    if (!g_audio.ready)
    {
        return;
    }
    g_audio.handoff_paused = 0;
    audsrv_stop_audio();
    TitanAudio_ResetStreaming();
    TitanAudio_StartMusic();
}

void TitanAudio_PlayShutdownAndWait(void)
{
    unsigned int elapsed;

    if (!g_audio.ready)
    {
        return;
    }

    audsrv_stop_audio();
    TitanAudio_ClearVoices();
    TitanAudio_ResetStreaming();
    g_audio.handoff_paused = 0;
    g_audio.exclusive_playback = 1;
    g_audio.music_requested = 0;
    g_audio.music_target_volume = 0;
    g_audio.music_volume = 0;

    if (!TitanAudio_PlaySfxInternal(TITAN_AUDIO_SFX_SHUTDOWN, 1))
    {
        g_audio.exclusive_playback = 0;
        g_audio.handoff_paused = 1;
        return;
    }

    elapsed = 0u;
    while (elapsed < TITAN_AUDIO_SHUTDOWN_TIMEOUT_US)
    {
        TitanAudio_Update();
        if (!TitanAudio_IsEffectActive(TITAN_AUDIO_SFX_SHUTDOWN) &&
            g_audio.pending_size == 0u && audsrv_queued() <= 0)
        {
            break;
        }
        DelayThread(TITAN_AUDIO_SHUTDOWN_PUMP_DELAY_US);
        elapsed += TITAN_AUDIO_SHUTDOWN_PUMP_DELAY_US;
    }

    audsrv_stop_audio();
    TitanAudio_ResetStreaming();
    g_audio.exclusive_playback = 0;
    g_audio.handoff_paused = 1;
}

#if defined(TITAN_AUDIO_TESTING)
size_t TitanAudio_TestGetPendingBytes(void)
{
    return g_audio.pending_size - g_audio.pending_offset;
}

size_t TitanAudio_TestGetMusicBufferedBytes(void)
{
    return TitanAudio_MusicBufferedBytes();
}

unsigned int TitanAudio_TestGetMusicReadCalls(void)
{
    return g_audio.music_read_calls;
}
#endif
