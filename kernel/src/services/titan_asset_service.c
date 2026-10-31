#include "titan_asset_service.h"

#include "titan_app_runtime.h"
#include "titan_asset_image.h"
#include "titan_network_client.h"
#include "titan_diagnostics.h"
#include "titan_network.h"
#include "titan_neutrino.h"
#include "titan_package_installer.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_storage_scheduler.h"
#include "titan_system_update.h"
#include "titan_time.h"
#include "titan_video.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
extern void *_gp;
extern int DeleteThread(int thread_id);
#endif

#define TITAN_ASSET_INVALID_GAME 0xffffffffu
#define TITAN_ASSET_ICON_SLOT_CAPACITY 24u
#define TITAN_ASSET_NET_STACK_SIZE (48u * 1024u)
#define TITAN_ASSET_NET_STACK_GUARD_WORDS 16u
#define TITAN_ASSET_NET_STACK_GUARD_VALUE 0x5449544eu /* TITN */
#define TITAN_ASSET_NET_STACK_FILL 0xa5u
#define TITAN_ASSET_NET_PRIORITY 110
#define TITAN_ASSET_NET_IDLE_DELAY_US 12000u
#define TITAN_ASSET_NET_YIELD_US 2500u
#define TITAN_ASSET_NETWORK_STABLE_FRAMES 1800u /* 30 s @ 60 Hz, automatic mode only */
#define TITAN_ASSET_AUTOMATIC_IDLE_FRAMES 120u /* 2 s of no user input on Home */
#define TITAN_ASSET_IO_QUANTUM (8u * 1024u)
/* OPL ART reads reopen/seek each quantum. A larger but still bounded local-file
 * slice cuts FileXio RPC churn without changing cache-write fairness. */
#define TITAN_ASSET_EXTERNAL_IO_QUANTUM (32u * 1024u)
#define TITAN_ASSET_SOURCE_MAX_SIZE (1536u * 1024u)
#define TITAN_ASSET_PROVIDER_URL_CAPACITY 768u
#define TITAN_ASSET_BACKGROUND_DEBOUNCE_FRAMES 10u
#define TITAN_ASSET_IDLE_PREFETCH_FRAMES 300u
#define TITAN_ASSET_RETRY_FRAMES 1800u
#define TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES 120u
#define TITAN_ASSET_MANUAL_MAX_ATTEMPTS 4u
#define TITAN_ASSET_STORAGE_STALL_FRAMES 900u /* 15 s without storage progress; network owns its own deadline */
#define TITAN_ASSET_CACHE_ROOT TITAN_STORAGE_DEVICE_ROOT "/cache"
#define TITAN_ASSET_CACHE_DIR TITAN_ASSET_CACHE_ROOT "/assets"
#define TITAN_ASSET_EXTERNAL_ART_DIR "mass:/ART"
#define TITAN_ASSET_EXTERNAL_PATH_CAPACITY 256u
#define TITAN_ASSET_TIMG_MAX_PAYLOAD \
    (TITAN_ASSET_BACKGROUND_WIDTH * TITAN_ASSET_BACKGROUND_HEIGHT * 2u)
#define TITAN_ASSET_TIMG_MAX_SIZE \
    (TITAN_ASSET_TIMG_HEADER_SIZE + TITAN_ASSET_TIMG_MAX_PAYLOAD)
#define TITAN_ASSET_ICON_PAYLOAD \
    (TITAN_ASSET_ICON_SOURCE_WIDTH * TITAN_ASSET_ICON_SOURCE_HEIGHT * 2u)
#define TITAN_ASSET_BACKGROUND_PAYLOAD \
    (TITAN_ASSET_BACKGROUND_WIDTH * TITAN_ASSET_BACKGROUND_HEIGHT * 2u)
#define TITAN_ASSET_TRANSIENT_ATLAS_WIDTH 256u
#define TITAN_ASSET_TRANSIENT_ATLAS_HEIGHT 64u
#define TITAN_ASSET_TRANSIENT_CELL_SIZE TITAN_ASSET_ICON_THUMBNAIL_SIZE
#define TITAN_ASSET_TRANSIENT_COLUMNS 8u

/* TIMG v2 is deliberately tiny and decode-free on PS2. Version 2 also
 * invalidates the old BF12 cache whose ICON entries could contain covers.
 * 0..3  "TIMG"
 * 4..5  version LE (2)
 * 6..7  kind LE
 * 8..9  width LE
 * 10..11 height LE
 * 12..15 flags LE (1 = GS PSM16, converted by the low-priority asset worker)
 * 16..19 payload size LE
 * 20..23 payload CRC32 LE
 * 24..31 reserved
 */

typedef enum TitanAssetPresence
{
    TITAN_ASSET_PRESENCE_UNKNOWN = 0,
    TITAN_ASSET_PRESENCE_PRESENT,
    TITAN_ASSET_PRESENCE_MISSING,
    TITAN_ASSET_PRESENCE_UNAVAILABLE
} TitanAssetPresence;

typedef struct TitanAssetGameState
{
    uint32_t startup_hash;
    TitanAssetPresence icon_presence;
    TitanAssetPresence background_presence;
    unsigned int icon_retry_frame;
    unsigned int background_retry_frame;
    unsigned char icon_probe_pending;
    unsigned char background_probe_pending;
    unsigned char icon_download_pending;
    unsigned char background_download_pending;
    unsigned char icon_attempts;
    unsigned char background_attempts;
    unsigned char icon_external_candidate;
    unsigned char background_external_candidate;
} TitanAssetGameState;

typedef struct TitanAssetIconSlot
{
    unsigned int game_index;
    unsigned int last_touch_frame;
    int valid;
    unsigned short pixels[TITAN_ASSET_ICON_RUNTIME_SIZE * TITAN_ASSET_ICON_RUNTIME_SIZE];
} TitanAssetIconSlot;

typedef enum TitanAssetStorageOp
{
    TITAN_ASSET_STORAGE_NONE = 0,
    TITAN_ASSET_STORAGE_PROBE_ICON,
    TITAN_ASSET_STORAGE_PROBE_BACKGROUND,
    TITAN_ASSET_STORAGE_LOAD_ICON,
    TITAN_ASSET_STORAGE_LOAD_BACKGROUND,
    TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON,
    TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND,
    TITAN_ASSET_STORAGE_WRITE_DOWNLOAD
} TitanAssetStorageOp;

typedef struct TitanAssetStorageJob
{
    TitanAssetStorageOp op;
    TitanAssetKind kind;
    unsigned int game_index;
    unsigned int offset;
    unsigned int total;
    uint32_t startup_hash;
    uint64_t stage_started_ms;
    uint64_t external_io_elapsed_us;
    unsigned int session_generation;
    unsigned int external_candidate;
    unsigned int catalog_generation;
    unsigned char external_prefetch;
    unsigned char external_buffer_allocated;
    unsigned char external_path_checked;
    unsigned char manual_owner;
    char serial[24];
    char path[256];
    char temporary_path[256];
} TitanAssetStorageJob;

typedef struct TitanAssetServiceState
{
    int initialized;
    volatile unsigned int frame_index;
    volatile unsigned int selected_game_index;
    unsigned int visible_games[TITAN_ASSET_VISIBLE_GAME_CAPACITY];
    unsigned int visible_count;
    unsigned int strip_generation;
    unsigned int strip_built_generation;
    unsigned int selected_since_frame;
    unsigned int last_visible_change_frame;
    unsigned int idle_icon_cursor;
    unsigned int idle_background_cursor;
    volatile unsigned int last_user_activity_frame;

    volatile int automatic_download_enabled;
    volatile int automatic_idle_eligible;
    volatile int automatic_paused_for_activity;
    volatile int manual_session_active;
    volatile int manual_start_queued;
    int manual_start_refresh;
    int manual_start_clear_cache;
    int manual_start_preserve_completed;
    volatile int manual_recovering_previous_state;
    unsigned int manual_recovery_count;
    int manual_refresh_existing;
    volatile int manual_clear_cache;
    volatile int artwork_wizard_active;
    TitanAssetKind artwork_wizard_kind;
    unsigned int artwork_wizard_cursor;
    unsigned int artwork_wizard_asset_count;
    volatile int manual_cancel_requested;
    volatile int watchdog_abort_requested;
    volatile int storage_abort_requested;
    TitanAssetManualPhase manual_phase;
    unsigned int manual_probe_cursor;
    unsigned int manual_icon_download_cursor;
    unsigned int manual_background_download_cursor;
    unsigned int manual_order[TITAN_NEUTRINO_GAME_CAPACITY];
    uint32_t manual_completed_icon_hash[TITAN_NEUTRINO_GAME_CAPACITY];
    uint32_t manual_completed_background_hash[TITAN_NEUTRINO_GAME_CAPACITY];
    unsigned int manual_completed_icon_count;
    unsigned int manual_completed_background_count;
    unsigned int manual_clear_cursor;
    unsigned int manual_clear_kind;
    unsigned int session_games;
    unsigned int session_total_assets;
    unsigned int session_completed_assets;
    unsigned int session_failed_assets;
    unsigned int session_unavailable_assets;
    unsigned int manual_session_generation;
    unsigned int pipeline_last_progress_frame;
    unsigned int pipeline_last_storage_offset;

    TitanAssetGameState games[TITAN_NEUTRINO_GAME_CAPACITY];
    TitanAssetIconSlot icon_slots[TITAN_ASSET_ICON_SLOT_CAPACITY];
    unsigned short transient_strip[
        TITAN_ASSET_TRANSIENT_ATLAS_WIDTH * TITAN_ASSET_TRANSIENT_ATLAS_HEIGHT
    ] __attribute__((aligned(64)));

    unsigned short active_background[
        TITAN_ASSET_BACKGROUND_WIDTH * TITAN_ASSET_BACKGROUND_HEIGHT
    ] __attribute__((aligned(64)));
    unsigned int active_background_game;
    int active_background_ready;

    unsigned char storage_buffer[TITAN_ASSET_TIMG_MAX_SIZE] __attribute__((aligned(64)));
    unsigned int storage_buffer_size;
    volatile int storage_stage_ready;
    TitanAssetKind storage_stage_kind;
    unsigned int storage_stage_game;
    uint32_t storage_stage_startup_hash;
    uint64_t storage_stage_ready_ms;
    uint64_t external_decode_queued_ms;

    unsigned char download_buffer[TITAN_ASSET_TIMG_MAX_SIZE] __attribute__((aligned(64)));
    unsigned int download_size;
    TitanAssetKind download_kind;
    unsigned int download_game;
    uint32_t download_startup_hash;
    unsigned int download_session_generation;
    unsigned char download_manual_owner;
    char download_serial[24];
    volatile int download_ready_for_storage;
    unsigned char *source_buffer;

    /* Raw OPL/PS2 Launcher artwork is read only by the explicit Asset Wizard.
     * It is converted to TIMG and published through the normal atomic cache
     * writer; ordinary navigation never decodes PNG/JPEG from mass:/ART. */
    unsigned char *external_source_buffer;
    unsigned int external_source_size;
    unsigned int external_source_capacity;
    volatile int external_decode_ready;
    volatile int external_decode_busy;
    TitanAssetKind external_decode_kind;
    unsigned int external_decode_game;
    unsigned int external_decode_candidate;
    uint32_t external_decode_startup_hash;
    unsigned int external_decode_catalog_generation;
    unsigned int external_decode_session_generation;
    int external_decode_wizard_import;
    char external_decode_path[TITAN_ASSET_EXTERNAL_PATH_CAPACITY];

    /* One-slot look-ahead: while the decode worker reads external_source_buffer,
     * the storage owner may fill this separate buffer. It is promoted only after
     * the decoder releases the current slot. */
    unsigned char *external_prefetch_buffer;
    unsigned int external_prefetch_size;
    unsigned int external_prefetch_capacity;
    volatile int external_prefetch_ready;
    TitanAssetKind external_prefetch_kind;
    unsigned int external_prefetch_game;
    unsigned int external_prefetch_candidate;
    uint32_t external_prefetch_startup_hash;
    unsigned int external_prefetch_catalog_generation;
    char external_prefetch_path[TITAN_ASSET_EXTERNAL_PATH_CAPACITY];

    TitanAssetStorageJob storage_job;
    char last_provider[96];
    unsigned int current_game_index;
    unsigned int current_game_ordinal;
    TitanAssetKind current_kind;
    volatile unsigned int catalog_generation;
    volatile unsigned int transfer_bytes_received;
    volatile unsigned int transfer_bytes_total;
    volatile unsigned int transfer_bytes_per_second;
    volatile unsigned int transfer_progress_percent;
    uint64_t transfer_started_ms;

    volatile int net_busy;
    volatile int net_shutdown;
    volatile int net_faulted;
#if defined(__EE__) || defined(_EE)
    int lock_sema_id;
    int net_thread_id;
#endif
    TitanAssetServiceStatus status;
} TitanAssetServiceState;

static TitanAssetServiceState g_assets;

#if defined(__EE__) || defined(_EE)
typedef struct TitanAssetNetStackArena
{
    uint32_t low_guard[TITAN_ASSET_NET_STACK_GUARD_WORDS];
    unsigned char stack[TITAN_ASSET_NET_STACK_SIZE] __attribute__((aligned(64)));
    uint32_t high_guard[TITAN_ASSET_NET_STACK_GUARD_WORDS];
} TitanAssetNetStackArena;

static TitanAssetNetStackArena g_asset_net_stack_arena __attribute__((aligned(64)));
static int TitanAssetService_EnsureNetworkWorker(void);
#endif

static void TitanAssetService_CopyText(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0u;
    if (dst == 0 || cap == 0u) return;
    while (src != 0 && src[i] != '\0' && i + 1u < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int TitanAssetService_IsValidKind(TitanAssetKind kind)
{
    return kind == TITAN_ASSET_KIND_ICON || kind == TITAN_ASSET_KIND_BACKGROUND;
}

static unsigned int TitanAssetService_ExpectedTimgSize(TitanAssetKind kind)
{
    if (kind == TITAN_ASSET_KIND_ICON)
        return TITAN_ASSET_TIMG_HEADER_SIZE + TITAN_ASSET_ICON_PAYLOAD;
    if (kind == TITAN_ASSET_KIND_BACKGROUND)
        return TITAN_ASSET_TIMG_HEADER_SIZE + TITAN_ASSET_BACKGROUND_PAYLOAD;
    return 0u;
}

static void TitanAssetService_MarkStorageFailureLocked(
    const TitanAssetStorageJob *job, int count_manual_failure, int allow_retry);
static void TitanAssetService_SetManualPhaseLocked(TitanAssetManualPhase phase);
static int TitanAssetService_BuildOplSerial(
    const char *normalized, char *destination, unsigned int capacity);
#if defined(__EE__) || defined(_EE)
static int TitanAssetService_ProcessExternalDecode(void);
#endif

#if defined(__EE__) || defined(_EE)
static int TitanAssetService_EnsureLock(void)
{
    ee_sema_t sema;
    int sema_id;
    if (g_assets.lock_sema_id >= 0) return 1;
    memset(&sema, 0, sizeof(sema));
    sema.init_count = 1;
    sema.max_count = 1;
    sema_id = CreateSema(&sema);
    if (sema_id < 0) return 0;
    g_assets.lock_sema_id = sema_id;
    return 1;
}
#endif

static void TitanAssetService_Lock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_assets.lock_sema_id >= 0) WaitSema(g_assets.lock_sema_id);
#endif
}

static void TitanAssetService_Unlock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_assets.lock_sema_id >= 0) SignalSema(g_assets.lock_sema_id);
#endif
}

static uint16_t TitanAssetService_ReadLe16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t TitanAssetService_ReadLe32(const unsigned char *p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8u) |
        ((uint32_t)p[2] << 16u) |
        ((uint32_t)p[3] << 24u);
}

static uint32_t TitanAssetService_Crc32(const unsigned char *data, unsigned int size)
{
    uint32_t crc = 0xffffffffu;
    unsigned int i;
    for (i = 0u; i < size; i++)
    {
        unsigned int bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++)
        {
            uint32_t mask = (uint32_t)-(int)(crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static int TitanAssetService_ValidateTimg(
    const unsigned char *data,
    unsigned int size,
    TitanAssetKind expected_kind,
    unsigned int *payload_offset,
    unsigned int *payload_size
)
{
    unsigned int width;
    unsigned int height;
    unsigned int expected_payload;
    unsigned int declared_payload;
    uint32_t declared_crc;

    if (data == 0 || size < TITAN_ASSET_TIMG_HEADER_SIZE ||
        !TitanAssetService_IsValidKind(expected_kind)) return 0;
    if (memcmp(data, "TIMG", 4u) != 0) return 0;
    if (TitanAssetService_ReadLe16(&data[4]) != 2u) return 0;
    if (TitanAssetService_ReadLe16(&data[6]) != (uint16_t)expected_kind) return 0;
    width = TitanAssetService_ReadLe16(&data[8]);
    height = TitanAssetService_ReadLe16(&data[10]);
    if (TitanAssetService_ReadLe32(&data[12]) != 1u) return 0;
    declared_payload = TitanAssetService_ReadLe32(&data[16]);
    declared_crc = TitanAssetService_ReadLe32(&data[20]);

    if (expected_kind == TITAN_ASSET_KIND_ICON)
    {
        if (width != TITAN_ASSET_ICON_SOURCE_WIDTH ||
            height != TITAN_ASSET_ICON_SOURCE_HEIGHT) return 0;
        expected_payload = TITAN_ASSET_ICON_PAYLOAD;
    }
    else
    {
        if (width != TITAN_ASSET_BACKGROUND_WIDTH ||
            height != TITAN_ASSET_BACKGROUND_HEIGHT) return 0;
        expected_payload = TITAN_ASSET_BACKGROUND_PAYLOAD;
    }

    if (declared_payload != expected_payload ||
        size != TITAN_ASSET_TIMG_HEADER_SIZE + declared_payload) return 0;
    if (TitanAssetService_Crc32(
            &data[TITAN_ASSET_TIMG_HEADER_SIZE], declared_payload) != declared_crc)
        return 0;

    if (payload_offset != 0) *payload_offset = TITAN_ASSET_TIMG_HEADER_SIZE;
    if (payload_size != 0) *payload_size = declared_payload;
    return 1;
}

static int TitanAssetService_NormalizeSerial(
    unsigned int game_index,
    char *serial,
    unsigned int capacity
)
{
    const TitanNeutrinoGame *game;
    const char *base;
    const char *candidate;
    if (serial == 0 || capacity < 11u) return 0;
    game = TitanNeutrino_GetGame(game_index);
    if (game == 0 || game->startup[0] == '\0') return 0;
    base = game->startup;

    /* Accept plain IDs (SLUS_209.46), normalized IDs (SLUS-20946), and full
     * SYSTEM.CNF-style paths (cdrom0:\\SLUS_209.46;1). Exactly five title
     * digits are consumed, so the ISO9660 ;1 version suffix can never leak into
     * provider/cache keys. */
    for (candidate = base; *candidate != '\0'; candidate++)
    {
        const char *p = candidate;
        char prefix[5];
        char digits[6];
        unsigned int i;
        unsigned int d = 0u;
        for (i = 0u; i < 4u; i++)
        {
            if (!isalpha((unsigned char)p[i])) break;
            prefix[i] = (char)toupper((unsigned char)p[i]);
        }
        if (i != 4u || isalpha((unsigned char)p[4])) continue;
        prefix[4] = '\0';
        p += 4;
        if (*p == '_' || *p == '-') p++;
        while (*p != '\0' && d < 5u)
        {
            if (isdigit((unsigned char)*p)) digits[d++] = *p;
            else if (*p != '.' && *p != '_' && *p != '-') break;
            p++;
        }
        if (d != 5u) continue;
        digits[5] = '\0';
        if (snprintf(serial, capacity, "%s-%s", prefix, digits) >= (int)capacity)
            return 0;
        return 1;
    }
    return 0;
}

static uint32_t TitanAssetService_HashStartup(const char *startup)
{
    uint32_t hash = 2166136261u;
    const unsigned char *p = (const unsigned char *)startup;
    if (startup == 0) return 0u;
    while (*p != 0u)
    {
        hash ^= (uint32_t)*p++;
        hash *= 16777619u;
    }
    return hash == 0u ? 1u : hash;
}

static void TitanAssetService_SyncGameIdentityLocked(unsigned int game_index)
{
    const TitanNeutrinoGame *game;
    TitanAssetGameState *state;
    uint32_t hash;
    unsigned int i;
    if (game_index >= TITAN_NEUTRINO_GAME_CAPACITY) return;
    game = TitanNeutrino_GetGame(game_index);
    if (game == 0 || game->startup[0] == '\0') return;
    hash = TitanAssetService_HashStartup(game->startup);
    state = &g_assets.games[game_index];
    if (state->startup_hash == hash) return;

    /* Library scan may reorder indices. Never let an icon/background cached in
     * RAM for the old occupant of this index flash on the newly indexed game. */
    state->startup_hash = hash;
    state->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
    state->background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
    state->icon_retry_frame = 0u;
    state->background_retry_frame = 0u;
    state->icon_probe_pending = 0u;
    state->background_probe_pending = 0u;
    state->icon_download_pending = 0u;
    state->background_download_pending = 0u;
    state->icon_external_candidate = 0u;
    state->background_external_candidate = 0u;
    for (i = 0u; i < TITAN_ASSET_ICON_SLOT_CAPACITY; i++)
    {
        if (g_assets.icon_slots[i].valid && g_assets.icon_slots[i].game_index == game_index)
            g_assets.icon_slots[i].valid = 0;
    }
    if (g_assets.active_background_ready && g_assets.active_background_game == game_index)
    {
        g_assets.active_background_ready = 0;
        g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;
    }
    g_assets.strip_generation++;
}

static int TitanAssetService_BuildCachePathForSerial(
    const char *serial,
    TitanAssetKind kind,
    char *path,
    unsigned int capacity
)
{
    int n;
    if (serial == 0 || serial[0] == '\0' || path == 0 || capacity == 0u ||
        !TitanAssetService_IsValidKind(kind)) return 0;
    n = snprintf(
        path,
        capacity,
        "%s/%s.%s.timg",
        TITAN_ASSET_CACHE_DIR,
        serial,
        kind == TITAN_ASSET_KIND_ICON ? "icon" : "bg"
    );
    return n >= 0 && (unsigned int)n < capacity;
}

static int TitanAssetService_BuildOplSerial(
    const char *normalized,
    char *destination,
    unsigned int capacity
)
{
    const char *dash;
    unsigned int prefix_len;
    unsigned int digit_count;
    int n;
    if (normalized == 0 || destination == 0 || capacity == 0u) return 0;
    dash = strchr(normalized, '-');
    if (dash == 0) return 0;
    prefix_len = (unsigned int)(dash - normalized);
    digit_count = (unsigned int)strlen(dash + 1);
    if (prefix_len != 4u || digit_count != 5u) return 0;
    n = snprintf(
        destination,
        capacity,
        "%.*s_%.*s.%s",
        (int)prefix_len,
        normalized,
        (int)(digit_count - 2u),
        dash + 1,
        dash + 1 + digit_count - 2u
    );
    return n >= 0 && (unsigned int)n < capacity;
}

static int TitanAssetService_BuildExternalArtPath(
    const char *serial,
    TitanAssetKind kind,
    unsigned int candidate,
    char *path,
    unsigned int capacity
)
{
    char opl_serial[24];
    const char *suffix = 0;
    int n;
    if (serial == 0 || path == 0 || capacity == 0u ||
        !TitanAssetService_IsValidKind(kind) ||
        !TitanAssetService_BuildOplSerial(serial, opl_serial, sizeof(opl_serial)))
        return 0;

    /* OPL's canonical ART naming is the primary compatibility contract. PS2
     * Launcher shares the same ART ecosystem and its square images can also
     * appear as a bare Game ID. Background screenshots are bounded fallbacks. */
    if (kind == TITAN_ASSET_KIND_ICON)
    {
        switch (candidate)
        {
            case 0u: suffix = "_ICO.png"; break;
            case 1u: suffix = "_ICO.jpg"; break;
            case 2u: suffix = ".png"; break;
            case 3u: suffix = ".jpg"; break;
            case 4u: suffix = "_COV2.png"; break;
            case 5u: suffix = "_COV2.jpg"; break;
            /* OPL cover art is the final icon fallback; the decoder center-crops it. */
            case 6u: suffix = "_COV.png"; break;
            case 7u: suffix = "_COV.jpg"; break;
            default: return 0;
        }
    }
    else
    {
        switch (candidate)
        {
            case 0u: suffix = "_BG_00.png"; break;
            case 1u: suffix = "_BG_00.jpg"; break;
            case 2u: suffix = "_BG.png"; break;
            case 3u: suffix = "_BG.jpg"; break;
            case 4u: suffix = "_SCR_00.png"; break;
            case 5u: suffix = "_SCR_00.jpg"; break;
            case 6u: suffix = "_SCR.png"; break;
            case 7u: suffix = "_SCR.jpg"; break;
            default: return 0;
        }
    }
    n = snprintf(path, capacity, "%s/%s%s", TITAN_ASSET_EXTERNAL_ART_DIR,
        opl_serial, suffix);
    return n >= 0 && (unsigned int)n < capacity;
}

static int TitanAssetService_FindExternalArt(
    const char *serial,
    TitanAssetKind kind,
    unsigned int first_candidate,
    char *path,
    unsigned int capacity,
    unsigned int *size_out,
    unsigned int *candidate_out
)
{
    unsigned int candidate;
    unsigned int candidate_count = 8u;
    TitanStorageIoSize size = 0;
    int is_directory = 0;
    if (path == 0 || capacity == 0u || size_out == 0 || candidate_out == 0) return 0;
    *size_out = 0u;
    path[0] = '\0';
    if (first_candidate >= candidate_count) return 0;
    for (candidate = first_candidate; candidate < candidate_count; candidate++)
    {
        if (!TitanAssetService_BuildExternalArtPath(serial, kind, candidate,
                path, capacity))
            continue;
        is_directory = 0;
        size = 0;
        if (!TitanStorageIo_PathInfo64(path, &is_directory, &size) ||
            is_directory || size == 0u || size > TITAN_ASSET_SOURCE_MAX_SIZE)
            continue;
        *size_out = (unsigned int)size;
        *candidate_out = candidate;
        return 1;
    }
    path[0] = '\0';
    return 0;
}

static void TitanAssetService_FreeExternalSource(void)
{
    unsigned char *buffer = g_assets.external_source_buffer;
    g_assets.external_source_buffer = 0;
    g_assets.external_source_size = 0u;
    g_assets.external_source_capacity = 0u;
    if (buffer != 0) free(buffer);
}

static void TitanAssetService_FreeExternalPrefetchLocked(void)
{
    unsigned char *buffer = g_assets.external_prefetch_buffer;
    g_assets.external_prefetch_buffer = 0;
    g_assets.external_prefetch_size = 0u;
    g_assets.external_prefetch_capacity = 0u;
    g_assets.external_prefetch_ready = 0;
    g_assets.external_prefetch_path[0] = '\0';
    if (buffer != 0) free(buffer);
}

/* Caller holds the asset lock. A prefetched source becomes the decode source
 * only after the current decoder has released its buffer. */
static void TitanAssetService_PromoteExternalPrefetchLocked(void)
{
    if (!g_assets.external_prefetch_ready || g_assets.external_decode_busy ||
        g_assets.external_decode_ready || g_assets.external_source_buffer != 0)
        return;
    if (g_assets.external_prefetch_catalog_generation != g_assets.catalog_generation ||
        g_assets.external_prefetch_game >= TITAN_NEUTRINO_GAME_CAPACITY ||
        g_assets.games[g_assets.external_prefetch_game].startup_hash !=
            g_assets.external_prefetch_startup_hash)
    {
        TitanAssetService_FreeExternalPrefetchLocked();
        return;
    }
    g_assets.external_source_buffer = g_assets.external_prefetch_buffer;
    g_assets.external_source_size = g_assets.external_prefetch_size;
    g_assets.external_source_capacity = g_assets.external_prefetch_capacity;
    g_assets.external_decode_kind = g_assets.external_prefetch_kind;
    g_assets.external_decode_game = g_assets.external_prefetch_game;
    g_assets.external_decode_candidate = g_assets.external_prefetch_candidate;
    g_assets.external_decode_startup_hash = g_assets.external_prefetch_startup_hash;
    g_assets.external_decode_catalog_generation = g_assets.external_prefetch_catalog_generation;
    g_assets.external_decode_session_generation = g_assets.manual_session_generation;
    g_assets.external_decode_wizard_import = g_assets.artwork_wizard_active;
    TitanAssetService_CopyText(g_assets.external_decode_path,
        sizeof(g_assets.external_decode_path), g_assets.external_prefetch_path);
    g_assets.external_prefetch_buffer = 0;
    g_assets.external_prefetch_size = 0u;
    g_assets.external_prefetch_capacity = 0u;
    g_assets.external_prefetch_ready = 0;
    g_assets.external_prefetch_path[0] = '\0';
    g_assets.external_decode_queued_ms = TitanTime_NowMilliseconds();
    g_assets.external_decode_ready = 1;
}

static void TitanAssetService_FreeExternalReadBufferLocked(TitanAssetStorageJob *job)
{
    if (job == 0 || !job->external_buffer_allocated) return;
    if (job->external_prefetch) TitanAssetService_FreeExternalPrefetchLocked();
    else TitanAssetService_FreeExternalSource();
    job->external_buffer_allocated = 0u;
}

static void TitanAssetService_FreeExternalReadBuffer(TitanAssetStorageJob *job)
{
    if (job == 0 || !job->external_buffer_allocated) return;
    TitanAssetService_Lock();
    TitanAssetService_FreeExternalReadBufferLocked(job);
    TitanAssetService_Unlock();
}

static TitanAssetIconSlot *TitanAssetService_FindIconSlot(unsigned int game_index)
{
    unsigned int i;
    for (i = 0u; i < TITAN_ASSET_ICON_SLOT_CAPACITY; i++)
    {
        if (g_assets.icon_slots[i].valid && g_assets.icon_slots[i].game_index == game_index)
            return &g_assets.icon_slots[i];
    }
    return 0;
}

static TitanAssetIconSlot *TitanAssetService_AllocateIconSlot(unsigned int game_index)
{
    unsigned int i;
    unsigned int victim = 0u;
    unsigned int oldest = 0xffffffffu;
    TitanAssetIconSlot *slot = TitanAssetService_FindIconSlot(game_index);
    if (slot != 0) return slot;
    for (i = 0u; i < TITAN_ASSET_ICON_SLOT_CAPACITY; i++)
    {
        unsigned int visible_index;
        int visible = 0;
        if (!g_assets.icon_slots[i].valid)
        {
            victim = i;
            oldest = 0u;
            break;
        }
        for (visible_index = 0u; visible_index < g_assets.visible_count; visible_index++)
        {
            if (g_assets.visible_games[visible_index] == g_assets.icon_slots[i].game_index)
            {
                visible = 1;
                break;
            }
        }
        /* Keep currently displayed game art resident ahead of idle catalog
         * prefetch. With 24 slots and at most 16 visible games, an invisible
         * victim should normally always be available. */
        if (!visible && g_assets.icon_slots[i].last_touch_frame < oldest)
        {
            oldest = g_assets.icon_slots[i].last_touch_frame;
            victim = i;
        }
    }
    slot = &g_assets.icon_slots[victim];
    if (slot->valid && slot->game_index < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        unsigned int visible_index;
        TitanAssetGameState *evicted = &g_assets.games[slot->game_index];
        evicted->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        evicted->icon_probe_pending = 0u;
        for (visible_index = 0u; visible_index < g_assets.visible_count; visible_index++)
        {
            if (g_assets.visible_games[visible_index] == slot->game_index)
            {
                evicted->icon_probe_pending = 1u;
                break;
            }
        }
        g_assets.strip_generation++;
    }
    memset(slot, 0, sizeof(*slot));
    slot->game_index = game_index;
    return slot;
}

static void TitanAssetService_CommitIcon(
    unsigned int game_index,
    const unsigned short *source
)
{
    TitanAssetIconSlot *slot = TitanAssetService_AllocateIconSlot(game_index);
    if (slot == 0) return;
    memcpy(slot->pixels, source, sizeof(slot->pixels));
    slot->valid = 1;
    slot->last_touch_frame = g_assets.frame_index;
    g_assets.games[game_index].icon_presence = TITAN_ASSET_PRESENCE_PRESENT;
    g_assets.games[game_index].icon_probe_pending = 0u;
    g_assets.strip_generation++;
}

static unsigned short TitanAssetService_DownsampleIconPixel(
    const unsigned short *pixels, unsigned int thumb_x, unsigned int thumb_y)
{
    unsigned int sx0 = (thumb_x * TITAN_ASSET_ICON_RUNTIME_SIZE) / TITAN_ASSET_ICON_THUMBNAIL_SIZE;
    unsigned int sx1 = ((thumb_x + 1u) * TITAN_ASSET_ICON_RUNTIME_SIZE) / TITAN_ASSET_ICON_THUMBNAIL_SIZE;
    unsigned int sy0 = (thumb_y * TITAN_ASSET_ICON_RUNTIME_SIZE) / TITAN_ASSET_ICON_THUMBNAIL_SIZE;
    unsigned int sy1 = ((thumb_y + 1u) * TITAN_ASSET_ICON_RUNTIME_SIZE) / TITAN_ASSET_ICON_THUMBNAIL_SIZE;
    unsigned int x, y, count = 0u, r = 0u, g = 0u, b = 0u, a = 0u;
    if (sx1 <= sx0) sx1 = sx0 + 1u;
    if (sy1 <= sy0) sy1 = sy0 + 1u;
    for (y = sy0; y < sy1; y++)
    {
        for (x = sx0; x < sx1; x++)
        {
            unsigned short v = pixels[y * TITAN_ASSET_ICON_RUNTIME_SIZE + x];
            r += v & 0x1fu; g += (v >> 5u) & 0x1fu; b += (v >> 10u) & 0x1fu;
            a += (v & 0x8000u) != 0u ? 1u : 0u; count++;
        }
    }
    if (count == 0u) return 0u;
    return (unsigned short)((r / count) | ((g / count) << 5u) | ((b / count) << 10u) |
        (a * 2u >= count ? 0x8000u : 0u));
}

static void TitanAssetService_RebuildStrip(void)
{
    unsigned int i;
    if (g_assets.strip_built_generation == g_assets.strip_generation) return;
    memset(g_assets.transient_strip, 0, sizeof(g_assets.transient_strip));
    for (i = 0u; i < g_assets.visible_count && i < 16u; i++)
    {
        unsigned int game_index = g_assets.visible_games[i];
        TitanAssetIconSlot *slot = TitanAssetService_FindIconSlot(game_index);
        unsigned int cell_x, cell_y, row, column;
        if (slot == 0 || !slot->valid) continue;
        slot->last_touch_frame = g_assets.frame_index;
        cell_x = (i % TITAN_ASSET_TRANSIENT_COLUMNS) * TITAN_ASSET_TRANSIENT_CELL_SIZE;
        cell_y = (i / TITAN_ASSET_TRANSIENT_COLUMNS) * TITAN_ASSET_TRANSIENT_CELL_SIZE;
        for (row = 0u; row < TITAN_ASSET_ICON_THUMBNAIL_SIZE; row++)
            for (column = 0u; column < TITAN_ASSET_ICON_THUMBNAIL_SIZE; column++)
                g_assets.transient_strip[(cell_y + row) * TITAN_ASSET_TRANSIENT_ATLAS_WIDTH + cell_x + column] =
                    TitanAssetService_DownsampleIconPixel(slot->pixels, column, row);
    }
    g_assets.strip_built_generation = g_assets.strip_generation;
}

static int TitanAssetService_FindVisibleCell(unsigned int game_index, unsigned int *cell)
{
    unsigned int i;
    for (i = 0u; i < g_assets.visible_count && i < 16u; i++)
    {
        if (g_assets.visible_games[i] == game_index)
        {
            if (cell != 0) *cell = i;
            return 1;
        }
    }
    return 0;
}

static void TitanAssetService_RequestCacheProbeLocked(unsigned int game_index, TitanAssetKind kind)
{
    TitanAssetGameState *state;
    if (game_index >= TITAN_NEUTRINO_GAME_CAPACITY) return;
    TitanAssetService_SyncGameIdentityLocked(game_index);
    state = &g_assets.games[game_index];
    if (kind == TITAN_ASSET_KIND_ICON)
    {
        if (state->icon_presence == TITAN_ASSET_PRESENCE_UNKNOWN)
            state->icon_probe_pending = 1u;
    }
    else
    {
        if (state->background_presence == TITAN_ASSET_PRESENCE_UNKNOWN)
            state->background_probe_pending = 1u;
    }
}

void TitanAssetService_SetVisibleGames(
    unsigned int selected_game_index,
    const unsigned int *game_indices,
    unsigned int count,
    unsigned int frame_index
)
{
    unsigned int i;
    int changed = 0;
    if (!g_assets.initialized) return;
    if (count > TITAN_ASSET_VISIBLE_GAME_CAPACITY) count = TITAN_ASSET_VISIBLE_GAME_CAPACITY;

    TitanAssetService_Lock();
    g_assets.frame_index = frame_index;
    if (g_assets.visible_count != count) changed = 1;
    for (i = 0u; i < count; i++)
    {
        if (g_assets.visible_games[i] != game_indices[i]) changed = 1;
        g_assets.visible_games[i] = game_indices[i];
        TitanAssetService_RequestCacheProbeLocked(game_indices[i], TITAN_ASSET_KIND_ICON);
    }
    g_assets.visible_count = count;

    if (g_assets.selected_game_index != selected_game_index)
    {
        unsigned int previous_game = g_assets.selected_game_index;
        /* Only one background surface is resident. Forget the old RAM
         * residency so selecting that game again reloads its still-cached art. */
        if (g_assets.active_background_ready &&
            g_assets.active_background_game != selected_game_index)
        {
            g_assets.active_background_ready = 0;
            g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;
            if (previous_game < TITAN_NEUTRINO_GAME_CAPACITY)
                g_assets.games[previous_game].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        }
        g_assets.selected_game_index = selected_game_index;
        g_assets.selected_since_frame = frame_index;
        changed = 1;
        if (selected_game_index < TITAN_NEUTRINO_GAME_CAPACITY)
            TitanAssetService_RequestCacheProbeLocked(selected_game_index, TITAN_ASSET_KIND_ICON);
    }
    /* Re-arm a background probe whenever selected-game art is not resident,
     * including after catalog/cache invalidation when selection itself did not
     * change. This is what reloads the one-slot background RAM cache reliably. */
    if (selected_game_index < TITAN_NEUTRINO_GAME_CAPACITY &&
        (!g_assets.active_background_ready ||
         g_assets.active_background_game != selected_game_index))
    {
        if (g_assets.games[selected_game_index].background_presence == TITAN_ASSET_PRESENCE_PRESENT)
            g_assets.games[selected_game_index].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        TitanAssetService_RequestCacheProbeLocked(selected_game_index, TITAN_ASSET_KIND_BACKGROUND);
    }
    if (changed)
    {
        g_assets.strip_generation++;
        g_assets.last_visible_change_frame = frame_index;
    }
    g_assets.status.visible_game_count = count;
    g_assets.status.selected_game_index = selected_game_index;
    TitanAssetService_Unlock();
}

enum
{
    TITAN_ASSET_DIAG_SESSION_BEGIN = 0x320,
    TITAN_ASSET_DIAG_DNS_BEGIN = 0x321,
    TITAN_ASSET_DIAG_DNS_END = 0x322,
    TITAN_ASSET_DIAG_TCP_BEGIN = 0x323,
    TITAN_ASSET_DIAG_TCP_END = 0x324,
    TITAN_ASSET_DIAG_TLS_BEGIN = 0x325,
    TITAN_ASSET_DIAG_TLS_END = 0x326,
    TITAN_ASSET_DIAG_DOWNLOAD_BEGIN = 0x327,
    TITAN_ASSET_DIAG_DOWNLOAD_END = 0x328,
    TITAN_ASSET_DIAG_DECODE_BEGIN = 0x329,
    TITAN_ASSET_DIAG_DECODE_END = 0x32a,
    TITAN_ASSET_DIAG_CACHE_BEGIN = 0x32b,
    TITAN_ASSET_DIAG_CACHE_END = 0x32c,
    TITAN_ASSET_DIAG_SESSION_END = 0x32d,
    TITAN_ASSET_DIAG_AUTO_PAUSE = 0x32e,
    TITAN_ASSET_DIAG_AUTO_RESUME = 0x32f,
    TITAN_ASSET_DIAG_STACK_CANARY_FAIL = 0x330,
    TITAN_ASSET_DIAG_STACK_WATERMARK = 0x331,
    TITAN_ASSET_DIAG_STORAGE_JOB_REJECT = 0x332,
    TITAN_ASSET_DIAG_TLS_ENGINE_ERROR = 0x333,
    TITAN_ASSET_DIAG_HTTP_PROTOCOL_ERROR = 0x334,
    TITAN_ASSET_DIAG_CACHE_WRITE_FAIL = 0x335,
    TITAN_ASSET_DIAG_CACHE_LOAD_FAIL = 0x336,
    TITAN_ASSET_DIAG_CATALOG_RACE_DROP = 0x337,
    TITAN_ASSET_DIAG_SOURCE_ALLOC_FAIL = 0x338,
    TITAN_ASSET_DIAG_SOCKET_ERROR = 0x339,
    TITAN_ASSET_DIAG_CONNECT_ERROR = 0x33a,
    TITAN_ASSET_DIAG_CONNECTION_REUSE = 0x33b,
    TITAN_ASSET_DIAG_CONNECTION_CLOSE = 0x33c,
    TITAN_ASSET_DIAG_EXTERNAL_FOUND = 0x33d,
    TITAN_ASSET_DIAG_EXTERNAL_READ_END = 0x33e,
    TITAN_ASSET_DIAG_EXTERNAL_DECODE_BEGIN = 0x33f,
    TITAN_ASSET_DIAG_EXTERNAL_DECODE_END = 0x340,
    TITAN_ASSET_DIAG_EXTERNAL_DECODE_TIMING = 0x341,
    TITAN_ASSET_DIAG_EXTERNAL_READ_TIMING = 0x342,
    TITAN_ASSET_DIAG_EXTERNAL_READ_IO_TIMING = 0x343,
    TITAN_ASSET_DIAG_EXTERNAL_PROBE_TIMING = 0x344,
    TITAN_ASSET_DIAG_EXTERNAL_DISPATCH_TIMING = 0x345,
    TITAN_ASSET_DIAG_STAGE_READY_TIMING = 0x346
};

#if defined(__EE__) || defined(_EE)
static void TitanAssetService_PrepareNetworkStack(void)
{
    unsigned int i;
    for (i = 0u; i < TITAN_ASSET_NET_STACK_GUARD_WORDS; i++)
    {
        g_asset_net_stack_arena.low_guard[i] = TITAN_ASSET_NET_STACK_GUARD_VALUE;
        g_asset_net_stack_arena.high_guard[i] = TITAN_ASSET_NET_STACK_GUARD_VALUE;
    }
    memset(g_asset_net_stack_arena.stack, TITAN_ASSET_NET_STACK_FILL,
        sizeof(g_asset_net_stack_arena.stack));
}

static int TitanAssetService_CheckNetworkStack(unsigned int *low_pristine,
    unsigned int *high_pristine)
{
    unsigned int i;
    unsigned int low = 0u;
    unsigned int high = 0u;
    int guards_ok = 1;
    for (i = 0u; i < TITAN_ASSET_NET_STACK_GUARD_WORDS; i++)
    {
        if (g_asset_net_stack_arena.low_guard[i] != TITAN_ASSET_NET_STACK_GUARD_VALUE ||
            g_asset_net_stack_arena.high_guard[i] != TITAN_ASSET_NET_STACK_GUARD_VALUE)
            guards_ok = 0;
    }
    while (low < sizeof(g_asset_net_stack_arena.stack) &&
        g_asset_net_stack_arena.stack[low] == TITAN_ASSET_NET_STACK_FILL) low++;
    while (high < sizeof(g_asset_net_stack_arena.stack) &&
        g_asset_net_stack_arena.stack[sizeof(g_asset_net_stack_arena.stack) - 1u - high] ==
            TITAN_ASSET_NET_STACK_FILL) high++;
    if (low_pristine != 0) *low_pristine = low;
    if (high_pristine != 0) *high_pristine = high;
    return guards_ok;
}

static void TitanAssetService_LogNetworkStack(void)
{
    unsigned int low = 0u;
    unsigned int high = 0u;
    int ok = TitanAssetService_CheckNetworkStack(&low, &high);
    TitanDiagnostics_Event(ok ? TITAN_DIAG_DEBUG : TITAN_DIAG_ERROR, TITAN_DIAG_ASSET,
        ok ? TITAN_ASSET_DIAG_STACK_WATERMARK : TITAN_ASSET_DIAG_STACK_CANARY_FAIL,
        ok ? 0 : -1, low, high, TITAN_ASSET_NET_STACK_SIZE, 0u);
}
#endif

static int TitanAssetService_ManualOrderContainsLocked(
    unsigned int used,
    unsigned int game_index
)
{
    unsigned int i;
    for (i = 0u; i < used; i++)
        if (g_assets.manual_order[i] == game_index) return 1;
    return 0;
}

static unsigned int TitanAssetService_BuildManualOrderLocked(unsigned int count)
{
    unsigned int used = 0u;
    unsigned int i;
    unsigned int game;

    /* The current catalog has no persistent favorite bit yet. Preserve the
     * intended priority contract with the strongest data we do have: selected
     * game first, then the rest of the visible strip, then the catalog. A future
     * favorites source can be inserted between visible and catalog without
     * changing the downloader. */
    game = g_assets.selected_game_index;
    if (game < count) g_assets.manual_order[used++] = game;

    for (i = 0u; i < g_assets.visible_count; i++)
    {
        game = g_assets.visible_games[i];
        if (game < count && !TitanAssetService_ManualOrderContainsLocked(used, game))
            g_assets.manual_order[used++] = game;
    }
    for (game = 0u; game < count; game++)
    {
        if (!TitanAssetService_ManualOrderContainsLocked(used, game))
            g_assets.manual_order[used++] = game;
    }
    return used;
}

static int TitanAssetService_ManualCompletedContains(const uint32_t *items, unsigned int count, uint32_t hash)
{
    unsigned int i;
    if (hash == 0u) return 0;
    for (i = 0u; i < count; i++) if (items[i] == hash) return 1;
    return 0;
}

static void TitanAssetService_RecordManualCompletionLocked(TitanAssetKind kind, uint32_t hash)
{
    uint32_t *items;
    unsigned int *count;
    if (hash == 0u) return;
    if (kind == TITAN_ASSET_KIND_ICON)
    { items = g_assets.manual_completed_icon_hash; count = &g_assets.manual_completed_icon_count; }
    else
    { items = g_assets.manual_completed_background_hash; count = &g_assets.manual_completed_background_count; }
    if (TitanAssetService_ManualCompletedContains(items, *count, hash)) return;
    if (*count < TITAN_NEUTRINO_GAME_CAPACITY) items[(*count)++] = hash;
}

static void TitanAssetService_PrepareManualCatalogLocked(unsigned int count, int preserve_completed)
{
    unsigned int i;
    g_assets.manual_probe_cursor = 0u;
    g_assets.manual_icon_download_cursor = 0u;
    g_assets.manual_background_download_cursor = 0u;
    g_assets.session_games = TitanAssetService_BuildManualOrderLocked(count);
    g_assets.session_total_assets = g_assets.session_games * 2u;
    g_assets.session_completed_assets = 0u;
    g_assets.session_failed_assets = 0u;
    g_assets.session_unavailable_assets = 0u;
    g_assets.current_game_ordinal = 0u;
    for (i = 0u; i < count; i++)
    {
        uint32_t hash;
        TitanAssetService_SyncGameIdentityLocked(i);
        hash = g_assets.games[i].startup_hash;
        g_assets.games[i].icon_retry_frame = 0u;
        g_assets.games[i].background_retry_frame = 0u;
        g_assets.games[i].icon_probe_pending = 0u;
        g_assets.games[i].background_probe_pending = 0u;
        g_assets.games[i].icon_attempts = 0u;
        g_assets.games[i].background_attempts = 0u;
        if (preserve_completed && TitanAssetService_ManualCompletedContains(
                g_assets.manual_completed_icon_hash, g_assets.manual_completed_icon_count, hash))
        {
            g_assets.games[i].icon_presence = TITAN_ASSET_PRESENCE_PRESENT;
            g_assets.games[i].icon_download_pending = 0u;
            g_assets.session_completed_assets++;
        }
        else
            g_assets.games[i].icon_download_pending = g_assets.manual_refresh_existing ? 1u : 0u;
        if (preserve_completed && TitanAssetService_ManualCompletedContains(
                g_assets.manual_completed_background_hash, g_assets.manual_completed_background_count, hash))
        {
            g_assets.games[i].background_presence = TITAN_ASSET_PRESENCE_PRESENT;
            g_assets.games[i].background_download_pending = 0u;
            g_assets.session_completed_assets++;
        }
        else
            g_assets.games[i].background_download_pending = 0u;
    }
    if (g_assets.manual_refresh_existing)
    {
        g_assets.manual_probe_cursor = count;
        TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_DOWNLOAD_ICONS);
    }
    else TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_PROBE_ICONS);
}

static void TitanAssetService_BeginClearCacheLocked(unsigned int count)
{
    g_assets.manual_session_generation++;
    if (g_assets.manual_session_generation == 0u) g_assets.manual_session_generation = 1u;
    g_assets.manual_session_active = 1;
    g_assets.manual_start_queued = 0;
    g_assets.manual_start_clear_cache = 0;
    g_assets.manual_start_preserve_completed = 0;
    g_assets.manual_refresh_existing = 0;
    g_assets.manual_clear_cache = 1;
    g_assets.manual_cancel_requested = 0;
    g_assets.watchdog_abort_requested = 0;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    g_assets.manual_clear_cursor = 0u;
    g_assets.manual_clear_kind = TITAN_ASSET_KIND_ICON;
    g_assets.session_games = count;
    g_assets.session_total_assets = count * 2u;
    g_assets.session_completed_assets = 0u;
    g_assets.session_failed_assets = 0u;
    g_assets.session_unavailable_assets = 0u;
    TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_CLEAR_CACHE);
}

static void TitanAssetService_BeginManualSessionLocked(unsigned int count, int refresh_existing,
    int preserve_completed)
{
#if defined(__EE__) || defined(_EE)
    /* New manual session, drained boundary: discard stale keep-alive sockets and
     * DNS answers from any previous automatic/manual activity before warming the
     * two-host pool again. */
    TitanHttpClient_ResetSession();
#endif
    g_assets.manual_session_generation++;
    if (g_assets.manual_session_generation == 0u) g_assets.manual_session_generation = 1u;
    g_assets.manual_session_active = 1;
    g_assets.manual_start_queued = 0;
    g_assets.manual_start_clear_cache = 0;
    g_assets.manual_start_preserve_completed = 0;
    g_assets.manual_refresh_existing = refresh_existing ? 1 : 0;
    g_assets.manual_clear_cache = 0;
    g_assets.manual_cancel_requested = 0;
    g_assets.watchdog_abort_requested = 0;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    if (!preserve_completed)
    {
        g_assets.manual_completed_icon_count = 0u;
        g_assets.manual_completed_background_count = 0u;
    }
    TitanAssetService_PrepareManualCatalogLocked(count, preserve_completed);
}

static void TitanAssetService_LogSessionEnd(int result)
{
    TitanDiagnostics_Event(
        result == 0 ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
        TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_SESSION_END, result,
        g_assets.session_completed_assets, g_assets.session_failed_assets,
        g_assets.session_total_assets, (uint32_t)g_assets.manual_phase
    );
}

static void TitanAssetService_SetManualPhaseLocked(TitanAssetManualPhase phase)
{
    if (g_assets.manual_phase == phase) return;
    g_assets.manual_phase = phase;
    TitanDiagnostics_Event(
        TITAN_DIAG_INFO,
        TITAN_DIAG_ASSET,
        0x311,
        0,
        (uint32_t)phase,
        g_assets.session_completed_assets,
        g_assets.session_failed_assets,
        g_assets.session_total_assets
    );
}

const char *TitanAssetService_GetManualPhaseName(TitanAssetManualPhase phase)
{
    switch (phase)
    {
        case TITAN_ASSET_MANUAL_PROBE_ICONS: return "Verificando icones";
        case TITAN_ASSET_MANUAL_DOWNLOAD_ICONS: return "Baixando icones";
        case TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS: return "Verificando backgrounds";
        case TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS: return "Baixando backgrounds";
        case TITAN_ASSET_MANUAL_CLEAR_CACHE: return "Limpando cache";
        case TITAN_ASSET_MANUAL_RECOVERING: return "Recuperando operacao anterior";
        case TITAN_ASSET_MANUAL_COMPLETE: return "Concluido";
        case TITAN_ASSET_MANUAL_CANCELLED: return "Cancelado";
        case TITAN_ASSET_MANUAL_IMPORT_ARTWORK: return "Convertendo artwork para TIMG";
        case TITAN_ASSET_MANUAL_IDLE:
        default: return "Aguardando";
    }
}

static void TitanAssetService_InvalidateGameRamLocked(unsigned int game_index)
{
    unsigned int i;
    for (i = 0u; i < TITAN_ASSET_ICON_SLOT_CAPACITY; i++)
    {
        if (g_assets.icon_slots[i].valid && g_assets.icon_slots[i].game_index == game_index)
            g_assets.icon_slots[i].valid = 0;
    }
    if (g_assets.active_background_ready && g_assets.active_background_game == game_index)
    {
        g_assets.active_background_ready = 0;
        g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;
    }
    if (game_index < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        g_assets.games[game_index].icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        g_assets.games[game_index].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        g_assets.games[game_index].icon_probe_pending = 0u;
        g_assets.games[game_index].background_probe_pending = 0u;
        g_assets.games[game_index].icon_download_pending = 0u;
        g_assets.games[game_index].background_download_pending = 0u;
        g_assets.games[game_index].icon_external_candidate = 0u;
        g_assets.games[game_index].background_external_candidate = 0u;
    }
    g_assets.strip_generation++;
}

static int TitanAssetService_ManualIconWorkRemainingLocked(void)
{
    unsigned int i;
    unsigned int count = g_assets.session_games;
    if (g_assets.manual_probe_cursor < count) return 1;
    for (i = 0u; i < count; i++)
    {
        TitanAssetGameState *s = &g_assets.games[g_assets.manual_order[i]];
        if (s->icon_probe_pending || s->icon_download_pending) return 1;
    }
    if (g_assets.net_busy && g_assets.current_kind == TITAN_ASSET_KIND_ICON) return 1;
    if (g_assets.download_ready_for_storage && g_assets.download_kind == TITAN_ASSET_KIND_ICON) return 1;
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE &&
        g_assets.storage_job.kind == TITAN_ASSET_KIND_ICON) return 1;
    if (g_assets.storage_stage_ready && g_assets.storage_stage_kind == TITAN_ASSET_KIND_ICON) return 1;
    return 0;
}

static int TitanAssetService_ManualBackgroundWorkRemainingLocked(void)
{
    unsigned int i;
    unsigned int count = g_assets.session_games;
    if (g_assets.manual_probe_cursor < count) return 1;
    for (i = 0u; i < count; i++)
    {
        TitanAssetGameState *s = &g_assets.games[g_assets.manual_order[i]];
        if (s->background_probe_pending || s->background_download_pending) return 1;
    }
    if (g_assets.net_busy && g_assets.current_kind == TITAN_ASSET_KIND_BACKGROUND) return 1;
    if (g_assets.download_ready_for_storage && g_assets.download_kind == TITAN_ASSET_KIND_BACKGROUND) return 1;
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE &&
        g_assets.storage_job.kind == TITAN_ASSET_KIND_BACKGROUND) return 1;
    if (g_assets.storage_stage_ready && g_assets.storage_stage_kind == TITAN_ASSET_KIND_BACKGROUND) return 1;
    return 0;
}

static int TitanAssetService_ManualPipelineDrainedLocked(void)
{
    if (g_assets.net_busy) return 0;
    if (g_assets.download_ready_for_storage) return 0;
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE) return 0;
    if (g_assets.storage_stage_ready || g_assets.external_decode_ready ||
        g_assets.external_decode_busy || g_assets.external_prefetch_ready ||
        g_assets.external_prefetch_buffer != 0) return 0;
    return 1;
}

static void TitanAssetService_UpdateManualPhaseLocked(void)
{
    if (!g_assets.manual_session_active || g_assets.manual_clear_cache) return;
    if (g_assets.manual_cancel_requested)
    {
        /* Cancellation is a drain barrier, not an immediate session teardown.
         * Keep the session alive until the network/storage owners have observed
         * the cancel token and released their live jobs. This guarantees that
         * ASSET_SESSION_END is always the final asset event for the session. */
        if (TitanAssetService_ManualPipelineDrainedLocked())
        {
            g_assets.manual_session_active = 0;
            g_assets.manual_cancel_requested = 0;
            g_assets.manual_clear_cache = 0;
            g_assets.artwork_wizard_active = 0;
#if defined(__EE__) || defined(_EE)
            TitanHttpClient_ResetSession();
#endif
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_CANCELLED);
            TitanAssetService_LogSessionEnd(-1);
        }
        return;
    }
    if (g_assets.artwork_wizard_active)
    {
        if (g_assets.artwork_wizard_cursor >= g_assets.artwork_wizard_asset_count &&
            TitanAssetService_ManualPipelineDrainedLocked())
        {
            g_assets.manual_session_active = 0;
            g_assets.artwork_wizard_active = 0;
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_COMPLETE);
            TitanAssetService_LogSessionEnd(g_assets.session_failed_assets == 0u ? 0 : -1);
        }
        return;
    }
    if (g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_ICONS ||
        g_assets.manual_phase == TITAN_ASSET_MANUAL_DOWNLOAD_ICONS)
    {
        if (!TitanAssetService_ManualIconWorkRemainingLocked())
        {
            g_assets.manual_probe_cursor = 0u;
            if (g_assets.manual_refresh_existing)
            {
                unsigned int i;
                for (i = 0u; i < g_assets.session_games; i++)
                {
                    unsigned int game = g_assets.manual_order[i];
                    uint32_t hash = g_assets.games[game].startup_hash;
                    g_assets.games[game].background_download_pending =
                        TitanAssetService_ManualCompletedContains(
                            g_assets.manual_completed_background_hash,
                            g_assets.manual_completed_background_count, hash) ? 0u : 1u;
                }
                g_assets.manual_probe_cursor = g_assets.session_games;
                TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS);
            }
            else
            {
                TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS);
            }
        }
    }
    else if (g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS ||
             g_assets.manual_phase == TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS)
    {
        if (!TitanAssetService_ManualBackgroundWorkRemainingLocked() &&
            TitanAssetService_ManualPipelineDrainedLocked())
        {
            g_assets.manual_session_active = 0;
#if defined(__EE__) || defined(_EE)
            TitanHttpClient_ResetSession();
#endif
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_COMPLETE);
            TitanDiagnostics_Event(
                TITAN_DIAG_INFO, TITAN_DIAG_ASSET, 0x312, 0,
                g_assets.session_completed_assets,
                g_assets.session_failed_assets,
                g_assets.session_total_assets,
                g_assets.status.downloads_completed
            );
            TitanAssetService_LogSessionEnd(g_assets.session_failed_assets == 0u ? 0 : -1);
        }
    }
}

int TitanAssetService_StartManualDownload(int refresh_existing)
{
    const TitanNetworkStatus *network = TitanNetwork_GetStatus();
    unsigned int count = TitanNeutrino_GetGameCount();
    int must_queue;
    if (!g_assets.initialized || network == 0 || network->state != TITAN_NETWORK_ONLINE) return 0;
    if (count > TITAN_NEUTRINO_GAME_CAPACITY) count = TITAN_NEUTRINO_GAME_CAPACITY;
    if (count == 0u) return 0;
#if defined(__EE__) || defined(_EE)
    if (!TitanAssetService_EnsureNetworkWorker()) return 0;
#endif
    TitanAssetService_Lock();
    if (g_assets.manual_session_active || g_assets.manual_start_queued)
    { TitanAssetService_Unlock(); return 1; }
    must_queue = g_assets.net_busy || g_assets.download_ready_for_storage || g_assets.storage_stage_ready ||
        g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE;
    if (must_queue)
    {
        g_assets.manual_start_queued = 1;
        g_assets.manual_start_refresh = refresh_existing ? 1 : 0;
        g_assets.manual_start_clear_cache = 0;
        g_assets.manual_start_preserve_completed = 0;
        g_assets.manual_recovering_previous_state = 1;
        g_assets.manual_recovery_count++;
        g_assets.session_games = count;
        g_assets.session_total_assets = count * 2u;
        g_assets.session_completed_assets = 0u;
        g_assets.session_failed_assets = 0u;
        g_assets.session_unavailable_assets = 0u;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_RECOVERING);
        TitanAssetService_Unlock();
        return 1;
    }
    TitanAssetService_BeginManualSessionLocked(count, refresh_existing, 0);
    g_assets.manual_recovering_previous_state = 0;
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET, 0x310, 0,
        g_assets.session_games, refresh_existing ? 1u : 0u, g_assets.session_total_assets, 0u);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_SESSION_BEGIN, 0,
        g_assets.session_games, refresh_existing ? 1u : 0u,
        g_assets.session_total_assets, g_assets.selected_game_index);
    return 1;
}

int TitanAssetService_StartArtworkWizard(int all_games, unsigned int selected_game,
    TitanAssetKind kind)
{
    unsigned int count;
    unsigned int built;
    unsigned int i;
    unsigned int valid_count = 0u;
    if (!g_assets.initialized ||
        (kind != 0 && !TitanAssetService_IsValidKind(kind))) return 0;
    count = TitanNeutrino_GetGameCount();
    if (count > TITAN_NEUTRINO_GAME_CAPACITY) count = TITAN_NEUTRINO_GAME_CAPACITY;
    if (!all_games)
    {
        const TitanNeutrinoGame *game = TitanNeutrino_GetGame(selected_game);
        if (selected_game >= count || game == 0 || game->startup[0] == '\0') return 0;
    }
#if defined(__EE__) || defined(_EE)
    /* The low-priority worker owns cooperative PNG/JPEG decoding. Conversion
     * is local and does not require an online network connection. */
    if (!TitanAssetService_EnsureNetworkWorker()) return 0;
#endif
    TitanAssetService_Lock();
    if (g_assets.manual_session_active || g_assets.manual_start_queued ||
        g_assets.net_busy || g_assets.download_ready_for_storage ||
        g_assets.storage_stage_ready || g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE ||
        g_assets.external_decode_ready || g_assets.external_decode_busy)
    {
        TitanAssetService_Unlock();
        return 0;
    }
    g_assets.manual_session_generation++;
    if (g_assets.manual_session_generation == 0u) g_assets.manual_session_generation = 1u;
    g_assets.manual_session_active = 1;
    g_assets.manual_start_queued = 0;
    g_assets.manual_refresh_existing = 0;
    g_assets.manual_clear_cache = 0;
    g_assets.manual_cancel_requested = 0;
    g_assets.watchdog_abort_requested = 0;
    g_assets.artwork_wizard_active = 1;
    g_assets.artwork_wizard_kind = kind;
    g_assets.artwork_wizard_cursor = 0u;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    g_assets.current_game_index = TITAN_ASSET_INVALID_GAME;
    g_assets.current_game_ordinal = 0u;
    g_assets.current_kind = kind == TITAN_ASSET_KIND_BACKGROUND
        ? TITAN_ASSET_KIND_BACKGROUND : TITAN_ASSET_KIND_ICON;
    if (all_games)
    {
        built = TitanAssetService_BuildManualOrderLocked(count);
        for (i = 0u; i < built; i++)
        {
            unsigned int game_index = g_assets.manual_order[i];
            const TitanNeutrinoGame *game = TitanNeutrino_GetGame(game_index);
            if (game == 0 || game->startup[0] == '\0') continue;
            TitanAssetService_SyncGameIdentityLocked(game_index);
            g_assets.manual_order[valid_count++] = game_index;
        }
    }
    else
    {
        TitanAssetService_SyncGameIdentityLocked(selected_game);
        g_assets.manual_order[0] = selected_game;
        valid_count = 1u;
    }
    if (valid_count == 0u)
    {
        g_assets.manual_session_active = 0;
        g_assets.artwork_wizard_active = 0;
        TitanAssetService_Unlock();
        return 0;
    }
    g_assets.session_games = valid_count;
    g_assets.artwork_wizard_asset_count = valid_count * (kind == 0 ? 2u : 1u);
    g_assets.manual_probe_cursor = 0u;
    g_assets.manual_icon_download_cursor = 0u;
    g_assets.manual_background_download_cursor = 0u;
    g_assets.session_total_assets = g_assets.artwork_wizard_asset_count;
    g_assets.session_completed_assets = 0u;
    g_assets.session_failed_assets = 0u;
    g_assets.session_unavailable_assets = 0u;
    g_assets.manual_completed_icon_count = 0u;
    g_assets.manual_completed_background_count = 0u;
    TitanAssetService_CopyText(g_assets.last_provider, sizeof(g_assets.last_provider),
        "Asset Wizard");
    TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_IMPORT_ARTWORK);
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_SESSION_BEGIN, 0, valid_count,
        (uint32_t)kind, valid_count * (kind == 0 ? 2u : 1u), all_games ? 1u : selected_game);
    return valid_count != 0u;
}

int TitanAssetService_StartClearCache(void)
{
    unsigned int count = TitanNeutrino_GetGameCount();
    int must_queue;
    if (!g_assets.initialized) return 0;
    if (count > TITAN_NEUTRINO_GAME_CAPACITY) count = TITAN_NEUTRINO_GAME_CAPACITY;
    TitanAssetService_Lock();
    if (g_assets.manual_session_active || g_assets.manual_start_queued)
    { TitanAssetService_Unlock(); return 1; }
    must_queue = g_assets.net_busy || g_assets.download_ready_for_storage || g_assets.storage_stage_ready ||
        g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE;
    if (must_queue)
    {
        g_assets.manual_start_queued = 1;
        g_assets.manual_start_clear_cache = 1;
        g_assets.manual_start_refresh = 0;
        g_assets.manual_start_preserve_completed = 0;
        g_assets.manual_recovering_previous_state = 1;
        g_assets.manual_recovery_count++;
        g_assets.session_games = count;
        g_assets.session_total_assets = count * 2u;
        g_assets.session_completed_assets = 0u;
        g_assets.session_failed_assets = 0u;
        g_assets.session_unavailable_assets = 0u;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_RECOVERING);
        TitanAssetService_Unlock();
        return 1;
    }
    TitanAssetService_BeginClearCacheLocked(count);
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,0x314,0,count,0,0,0);
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,TITAN_ASSET_DIAG_SESSION_BEGIN,0,
        count,2u,count*2u,0u);
    return 1;
}

void TitanAssetService_CancelManualOperation(void)
{
    unsigned int i;
    TitanAssetService_Lock();
    if (g_assets.manual_start_queued && !g_assets.manual_session_active)
    {
        g_assets.manual_start_queued = 0;
        g_assets.manual_start_clear_cache = 0;
        g_assets.manual_start_preserve_completed = 0;
        g_assets.manual_recovering_previous_state = 0;
        TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_CANCELLED);
        TitanAssetService_Unlock();
        TitanAssetService_LogSessionEnd(-1);
        return;
    }
    if (!g_assets.manual_session_active)
    {
        TitanAssetService_Unlock();
        return;
    }
    /* Cancellation is cooperative and session-scoped. Even when no worker is
     * currently inside I/O, keep the cancel token set until the main-thread
     * phase machine proves that network + staging + storage are drained. */
    g_assets.manual_cancel_requested = 1;
    if (g_assets.download_ready_for_storage &&
        g_assets.storage_job.op != TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
    {
        g_assets.download_ready_for_storage = 0;
        g_assets.download_size = 0u;
        g_assets.download_game = TITAN_ASSET_INVALID_GAME;
        g_assets.download_kind = 0;
        g_assets.download_startup_hash = 0u;
        g_assets.download_session_generation = 0u;
        g_assets.download_manual_owner = 0u;
        g_assets.download_serial[0] = '\0';
    }
    for (i = 0u; i < g_assets.session_games; i++)
    {
        unsigned int game = g_assets.manual_order[i];
        if (game >= TITAN_NEUTRINO_GAME_CAPACITY) continue;
        g_assets.games[game].icon_download_pending = 0u;
        g_assets.games[game].background_download_pending = 0u;
    }
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_WARN,TITAN_DIAG_ASSET,0x313,0,
        g_assets.session_completed_assets,g_assets.session_failed_assets,g_assets.session_total_assets,0u);
}

int TitanAssetService_IsManualOperationActive(void)
{
    return g_assets.manual_session_active || g_assets.manual_start_queued;
}

int TitanAssetService_IsAutomaticDownloadEnabled(void)
{
    return g_assets.automatic_download_enabled;
}

void TitanAssetService_SetAutomaticDownloadEnabled(int enabled)
{
#if defined(__EE__) || defined(_EE)
    if (enabled && !TitanAssetService_EnsureNetworkWorker()) enabled = 0;
#endif
    g_assets.automatic_download_enabled = enabled ? 1 : 0;
    if (!g_assets.automatic_download_enabled) g_assets.automatic_paused_for_activity = 0;
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,0x315,0,
        (uint32_t)g_assets.automatic_download_enabled,0,0,0);
}

void TitanAssetService_SetAutomaticIdleEligible(int eligible)
{
    int value = eligible ? 1 : 0;
    TitanAssetService_Lock();
    if (value != g_assets.automatic_idle_eligible)
    {
        g_assets.automatic_idle_eligible = value;
        g_assets.last_user_activity_frame = g_assets.frame_index;
    }
    TitanAssetService_Unlock();
}

void TitanAssetService_NotifyUserActivity(void)
{
    TitanAssetService_Lock();
    g_assets.last_user_activity_frame = g_assets.frame_index;
    TitanAssetService_Unlock();
}

static void TitanAssetService_RefreshStatus(void)
{
    unsigned int i;
    unsigned int ready = 0u;
    TitanAssetService_Lock();
    for (i = 0u; i < g_assets.visible_count; i++)
    {
        if (TitanAssetService_FindIconSlot(g_assets.visible_games[i]) != 0) ready++;
    }
    g_assets.status.initialized = g_assets.initialized;
    g_assets.status.network_busy = g_assets.net_busy;
    g_assets.status.storage_busy =
        g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE ||
        g_assets.download_ready_for_storage ||
        g_assets.external_decode_ready || g_assets.external_decode_busy ||
        g_assets.external_prefetch_ready || g_assets.external_prefetch_buffer != 0;
    g_assets.status.automatic_download_enabled = g_assets.automatic_download_enabled;
    g_assets.status.manual_session_active = g_assets.manual_session_active || g_assets.manual_start_queued;
    g_assets.status.manual_start_queued = g_assets.manual_start_queued;
    g_assets.status.recovering_previous_state = g_assets.manual_recovering_previous_state;
    g_assets.status.recovery_count = g_assets.manual_recovery_count;
    g_assets.status.manual_refresh_existing = g_assets.manual_refresh_existing;
    g_assets.status.manual_clear_cache = g_assets.manual_clear_cache;
    g_assets.status.artwork_wizard_active = g_assets.artwork_wizard_active;
    g_assets.status.manual_phase = g_assets.manual_phase;
    g_assets.status.current_game_index = g_assets.current_game_index;
    g_assets.status.current_game_ordinal = g_assets.current_game_ordinal;
    g_assets.status.current_kind = g_assets.current_kind;
    g_assets.status.automatic_paused_for_activity = g_assets.automatic_paused_for_activity;
    g_assets.status.session_games = g_assets.session_games;
    g_assets.status.session_total_assets = g_assets.session_total_assets;
    g_assets.status.session_completed_assets = g_assets.session_completed_assets;
    g_assets.status.session_failed_assets = g_assets.session_failed_assets;
    g_assets.status.session_unavailable_assets = g_assets.session_unavailable_assets;
    g_assets.status.icons_ready = ready;
    g_assets.status.direct_internet = 1;
    g_assets.status.transfer_bytes_received = g_assets.transfer_bytes_received;
    g_assets.status.transfer_bytes_total = g_assets.transfer_bytes_total;
    g_assets.status.transfer_bytes_per_second = g_assets.transfer_bytes_per_second;
    g_assets.status.transfer_progress_percent = g_assets.transfer_progress_percent;
    TitanAssetService_CopyText(
        g_assets.status.provider, sizeof(g_assets.status.provider), g_assets.last_provider
    );
    if (g_assets.net_faulted)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail),
            "Asset Service pausado: recurso do worker indisponivel");
    else if (g_assets.manual_cancel_requested && g_assets.manual_session_active)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail),
            "Cancelando e drenando I/O de assets");
    else if (g_assets.manual_start_queued)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail),
            "Aguardando finalizacao segura do I/O anterior");
    else if (g_assets.manual_session_active)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail),
            TitanAssetService_GetManualPhaseName(g_assets.manual_phase));
    else if (g_assets.net_busy)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail), "Baixando direto da Internet");
    else if (g_assets.automatic_download_enabled)
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail), "Download automatico habilitado");
    else
        TitanAssetService_CopyText(g_assets.status.detail, sizeof(g_assets.status.detail), "Internet direto pronto; downloads aguardam comando manual");
    TitanAssetService_Unlock();
}

static void TitanAssetService_RecoverStalledPipelineLocked(void)
{
    unsigned int stalled;
    if (!g_assets.manual_start_queued && !g_assets.manual_session_active) return;
    if (g_assets.pipeline_last_progress_frame == 0u)
    {
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        return;
    }
    stalled = g_assets.frame_index - g_assets.pipeline_last_progress_frame;
    if (stalled < TITAN_ASSET_STORAGE_STALL_FRAMES) return;

    /* Never infer a network stall from absence of BODY bytes. DNS/TCP/TLS and
     * response headers can legitimately make no payload progress. The HTTP
     * client owns a stage-aware whole-request deadline and cooperative cancel. */
    if (g_assets.net_busy) return;

    if (g_assets.storage_job.op == TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
    {
        /* Main thread must not memset/remove a job while the serialized storage
         * worker owns it. Request an abort; the owner performs cleanup safely. */
        g_assets.storage_abort_requested = 1;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
            TITAN_ASSET_DIAG_STORAGE_JOB_REJECT, -81, stalled,
            g_assets.storage_job.game_index, (uint32_t)g_assets.storage_job.kind, 1u);
        return;
    }
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE || g_assets.download_ready_for_storage)
    {
        /* Probe/load jobs are bounded one-quantum operations. Leave ownership
         * untouched and retry the recovery check later instead of racing it. */
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        return;
    }

    /* Only ownerless staged RAM may be discarded synchronously. */
    if (g_assets.storage_stage_ready)
    {
        g_assets.storage_stage_ready = 0;
        g_assets.storage_stage_startup_hash = 0u;
        g_assets.manual_recovering_previous_state = 1;
        g_assets.manual_recovery_count++;
    }
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
}

void TitanAssetService_UpdateMainThread(unsigned int frame_index)
{
    unsigned int payload_offset = 0u;
    unsigned int payload_size = 0u;
    unsigned int stage_size = 0u;
    TitanAssetKind kind = 0;
    unsigned int game = TITAN_ASSET_INVALID_GAME;
    uint32_t startup_hash = 0u;
    int stage_ready = 0;
    int identity_matches = 0;
    int valid = 0;
    uint64_t stage_ready_timestamp_ms = 0u;
    uint64_t ready_handoff_ms = 0u;
    char ready_stage[32];
    if (!g_assets.initialized) return;
    g_assets.frame_index = frame_index;
#if defined(__EE__) || defined(_EE)
    if (g_assets.external_decode_ready && !g_assets.external_decode_busy)
        (void)TitanAssetService_EnsureNetworkWorker();
#endif
    TitanAssetService_Lock();
    TitanAssetService_RecoverStalledPipelineLocked();
    TitanAssetService_Unlock();
    if (g_assets.manual_start_queued && !g_assets.net_busy && !g_assets.download_ready_for_storage &&
        !g_assets.storage_stage_ready && g_assets.storage_job.op == TITAN_ASSET_STORAGE_NONE)
    {
        unsigned int count = TitanNeutrino_GetGameCount();
        int refresh = g_assets.manual_start_refresh;
        int clear_cache = g_assets.manual_start_clear_cache;
        int preserve_completed = g_assets.manual_start_preserve_completed;
        if (count > TITAN_NEUTRINO_GAME_CAPACITY) count = TITAN_NEUTRINO_GAME_CAPACITY;
#if defined(__EE__) || defined(_EE)
        if (!clear_cache && !TitanAssetService_EnsureNetworkWorker())
        {
            TitanAssetService_RefreshStatus();
            return;
        }
#endif
        TitanAssetService_Lock();
        if (clear_cache) TitanAssetService_BeginClearCacheLocked(count);
        else TitanAssetService_BeginManualSessionLocked(count, refresh, preserve_completed);
        g_assets.manual_recovering_previous_state = 0;
        TitanAssetService_Unlock();
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_SESSION_BEGIN, 0,
            g_assets.session_games, clear_cache ? 2u : (refresh ? 1u : 0u),
            g_assets.session_total_assets, g_assets.selected_game_index);
    }
    TitanAssetService_Lock();
    if (g_assets.storage_stage_ready)
    {
        kind = g_assets.storage_stage_kind; game = g_assets.storage_stage_game;
        startup_hash = g_assets.storage_stage_startup_hash; stage_size = g_assets.storage_buffer_size;
        stage_ready_timestamp_ms = g_assets.storage_stage_ready_ms;
        identity_matches = game < TITAN_NEUTRINO_GAME_CAPACITY && startup_hash != 0u &&
            g_assets.games[game].startup_hash == startup_hash;
        stage_ready = 1;
    }
    TitanAssetService_Unlock();
    if (stage_ready)
    {
        uint64_t now = TitanTime_NowMilliseconds();
        ready_handoff_ms = now >= stage_ready_timestamp_ms
            ? now - stage_ready_timestamp_ms : 0u;
        if (ready_handoff_ms > 0xffffffffu) ready_handoff_ms = 0xffffffffu;
        (void)snprintf(ready_stage, sizeof(ready_stage), "%s WAIT %ums",
            kind == TITAN_ASSET_KIND_ICON ? "READY" : "READY BG",
            (unsigned int)ready_handoff_ms);
        valid = identity_matches && TitanAssetService_IsValidKind(kind) &&
            TitanAssetService_ValidateTimg(g_assets.storage_buffer, stage_size, kind, &payload_offset, &payload_size);
        if (valid)
        {
            if (kind == TITAN_ASSET_KIND_ICON)
            {
                const TitanNeutrinoGame *ready_game = TitanNeutrino_GetGame(game);
                TitanAssetService_CommitIcon(game,
                    (const unsigned short *)&g_assets.storage_buffer[payload_offset]);
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
                    ready_stage, ready_game != 0 ? ready_game->title : "Icon ready", 100);
            }
            else if (payload_size == TITAN_ASSET_BACKGROUND_PAYLOAD)
            {
                const TitanNeutrinoGame *ready_game = TitanNeutrino_GetGame(game);
                memcpy(g_assets.active_background, &g_assets.storage_buffer[payload_offset], TITAN_ASSET_BACKGROUND_PAYLOAD);
                g_assets.active_background_game = game; g_assets.active_background_ready = 1;
                g_assets.games[game].background_presence = TITAN_ASSET_PRESENCE_PRESENT;
                g_assets.games[game].background_probe_pending = 0u;
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
                    ready_stage, ready_game != 0 ? ready_game->title : "Background ready", 100);
            }
        }
        else
        {
            TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
                identity_matches ? TITAN_ASSET_DIAG_CACHE_LOAD_FAIL : TITAN_ASSET_DIAG_CATALOG_RACE_DROP,
                -4, game, (uint32_t)kind, stage_size, startup_hash);
            TitanAssetService_Lock();
            if (identity_matches && game < TITAN_NEUTRINO_GAME_CAPACITY && TitanAssetService_IsValidKind(kind))
            {
                TitanAssetStorageJob recovery;
                memset(&recovery, 0, sizeof(recovery));
                recovery.game_index = game; recovery.kind = kind; recovery.startup_hash = startup_hash;
                TitanAssetService_MarkStorageFailureLocked(&recovery, 0, 1);
            }
            TitanAssetService_Unlock();
        }
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
            TITAN_ASSET_DIAG_STAGE_READY_TIMING, 0, game, (uint32_t)kind,
            (unsigned int)ready_handoff_ms, stage_size);
        TitanAssetService_Lock();
        g_assets.storage_stage_ready = 0;
        g_assets.storage_stage_ready_ms = 0u;
        g_assets.storage_stage_startup_hash = 0u;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanAssetService_Unlock();
    }
    TitanAssetService_Lock();
    TitanAssetService_UpdateManualPhaseLocked();
    TitanAssetService_Unlock();
    TitanAssetService_RebuildStrip();
    TitanAssetService_RefreshStatus();
    if (g_assets.manual_recovering_previous_state && !g_assets.manual_start_queued)
        g_assets.manual_recovering_previous_state = 0;
}

int TitanAssetService_DrawIcon(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    unsigned int cell;
    unsigned int source_x;
    unsigned int source_y;
    TitanAssetIconSlot *slot;
    if (!g_assets.initialized || alpha == 0u ||
        !TitanAssetService_FindVisibleCell(game_index, &cell))
        return 0;
    slot = TitanAssetService_FindIconSlot(game_index);
    if (slot == 0 || !slot->valid) return 0;
    /* Library rendering uses the shared thumbnail atlas rather than the
     * selected-tile path; still refresh LRU or idle prefetch can evict visible
     * artwork while the library is sitting on screen. */
    slot->last_touch_frame = g_assets.frame_index;

    if (TitanVideo_UploadTransientAtlasPsm16(
            g_assets.transient_strip,
            TITAN_ASSET_TRANSIENT_ATLAS_WIDTH,
            TITAN_ASSET_TRANSIENT_ATLAS_HEIGHT,
            TITAN_ASSET_TRANSIENT_ATLAS_WIDTH,
            g_assets.strip_built_generation) != TITAN_RESULT_OK)
        return 0;

    source_x = (cell % TITAN_ASSET_TRANSIENT_COLUMNS) * TITAN_ASSET_TRANSIENT_CELL_SIZE;
    source_y = (cell / TITAN_ASSET_TRANSIENT_COLUMNS) * TITAN_ASSET_TRANSIENT_CELL_SIZE;
    TitanVideo_DrawTransientAtlasRegionPsm16(
        x, y, width, height,
        source_x, source_y,
        TITAN_ASSET_ICON_THUMBNAIL_SIZE,
        TITAN_ASSET_ICON_THUMBNAIL_SIZE,
        alpha
    );
    return 1;
}


int TitanAssetService_DrawIconHighQuality(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    TitanAssetIconSlot *slot;
    if (!g_assets.initialized || alpha == 0u) return 0;
    slot = TitanAssetService_FindIconSlot(game_index);
    if (slot == 0 || !slot->valid) return 0;
    slot->last_touch_frame = g_assets.frame_index;
    return TitanVideo_DrawAdaptiveImagePsm16(
        slot->pixels,
        TITAN_ASSET_ICON_RUNTIME_SIZE,
        TITAN_ASSET_ICON_RUNTIME_SIZE,
        TITAN_ASSET_ICON_RUNTIME_SIZE,
        x, y, width, height, alpha
    ) == TITAN_RESULT_OK;
}

int TitanAssetService_DrawBackground(
    unsigned int game_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    if (!g_assets.initialized || !g_assets.active_background_ready ||
        g_assets.active_background_game != game_index || alpha == 0u)
        return 0;
    return TitanVideo_DrawAdaptiveImagePsm16(
        g_assets.active_background,
        TITAN_ASSET_BACKGROUND_WIDTH,
        TITAN_ASSET_BACKGROUND_HEIGHT,
        TITAN_ASSET_BACKGROUND_WIDTH,
        x, y, width, height, alpha
    ) == TITAN_RESULT_OK;
}

static int TitanAssetService_SelectProbeLocked(
    unsigned int *game_index,
    TitanAssetKind *kind
)
{
    unsigned int i;

    if (g_assets.artwork_wizard_active && !g_assets.manual_cancel_requested)
    {
        while (g_assets.artwork_wizard_cursor < g_assets.artwork_wizard_asset_count)
        {
            unsigned int item = g_assets.artwork_wizard_cursor++;
            unsigned int ordinal;
            unsigned int game;
            TitanAssetKind selected_kind;
            if (g_assets.artwork_wizard_kind == 0)
            {
                ordinal = item / 2u;
                selected_kind = (item & 1u) == 0u
                    ? TITAN_ASSET_KIND_ICON : TITAN_ASSET_KIND_BACKGROUND;
            }
            else
            {
                ordinal = item;
                selected_kind = g_assets.artwork_wizard_kind;
            }
            if (ordinal >= g_assets.session_games) continue;
            game = g_assets.manual_order[ordinal];
            TitanAssetService_SyncGameIdentityLocked(game);
            g_assets.current_game_index = game;
            g_assets.current_game_ordinal = ordinal + 1u;
            g_assets.current_kind = selected_kind;
            *game_index = game;
            *kind = selected_kind;
            return 1;
        }
        return 0;
    }

    if (g_assets.manual_session_active && !g_assets.manual_refresh_existing &&
        !g_assets.manual_clear_cache)
    {
        unsigned int count = g_assets.session_games;
        TitanAssetKind manual_kind =
            (g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_ICONS ||
             g_assets.manual_phase == TITAN_ASSET_MANUAL_DOWNLOAD_ICONS)
                ? TITAN_ASSET_KIND_ICON : TITAN_ASSET_KIND_BACKGROUND;
        while (g_assets.manual_probe_cursor < count)
        {
            unsigned int game = g_assets.manual_order[g_assets.manual_probe_cursor++];
            TitanAssetGameState *state;
            TitanAssetService_SyncGameIdentityLocked(game);
            state = &g_assets.games[game];
            if (manual_kind == TITAN_ASSET_KIND_ICON)
            {
                if (state->icon_presence == TITAN_ASSET_PRESENCE_UNKNOWN ||
                    state->icon_presence == TITAN_ASSET_PRESENCE_MISSING ||
                    state->icon_presence == TITAN_ASSET_PRESENCE_UNAVAILABLE)
                {
                    *game_index = game; *kind = manual_kind; return 1;
                }
                g_assets.session_completed_assets++;
            }
            else
            {
                if (state->background_presence == TITAN_ASSET_PRESENCE_UNKNOWN ||
                    state->background_presence == TITAN_ASSET_PRESENCE_MISSING ||
                    state->background_presence == TITAN_ASSET_PRESENCE_UNAVAILABLE)
                {
                    *game_index = game; *kind = manual_kind; return 1;
                }
                g_assets.session_completed_assets++;
            }
        }
        return 0;
    }

    if (g_assets.selected_game_index < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        TitanAssetGameState *s = &g_assets.games[g_assets.selected_game_index];
        if (s->icon_probe_pending)
        {
            s->icon_probe_pending = 0u;
            *game_index = g_assets.selected_game_index;
            *kind = TITAN_ASSET_KIND_ICON;
            return 1;
        }
    }
    /* The whole visible tile window outranks decorative backgrounds. A selected
     * game's background used to jump ahead of neighboring tile icons after the
     * debounce, making visible artwork look serialized behind the slowest item. */
    for (i = 0u; i < g_assets.visible_count; i++)
    {
        unsigned int game = g_assets.visible_games[i];
        if (game < TITAN_NEUTRINO_GAME_CAPACITY && g_assets.games[game].icon_probe_pending)
        {
            g_assets.games[game].icon_probe_pending = 0u;
            *game_index = game;
            *kind = TITAN_ASSET_KIND_ICON;
            return 1;
        }
    }
    if (g_assets.selected_game_index < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        TitanAssetGameState *s = &g_assets.games[g_assets.selected_game_index];
        if (s->background_probe_pending &&
            g_assets.frame_index - g_assets.selected_since_frame >= TITAN_ASSET_BACKGROUND_DEBOUNCE_FRAMES)
        {
            s->background_probe_pending = 0u;
            *game_index = g_assets.selected_game_index;
            *kind = TITAN_ASSET_KIND_BACKGROUND;
            return 1;
        }
    }

    /* P4 enrichment only begins after five seconds with no visible-window
     * change. It never competes with navigation priorities above. Icons for
     * the catalog are discovered first; backgrounds follow on later quanta. */
    if (g_assets.frame_index - g_assets.last_visible_change_frame >=
        TITAN_ASSET_IDLE_PREFETCH_FRAMES)
    {
        unsigned int count = TitanNeutrino_GetGameCount();
        while (g_assets.idle_icon_cursor < count)
        {
            unsigned int game = g_assets.idle_icon_cursor++;
            TitanAssetService_SyncGameIdentityLocked(game);
            if (g_assets.games[game].icon_presence == TITAN_ASSET_PRESENCE_UNKNOWN)
            {
                *game_index = game;
                *kind = TITAN_ASSET_KIND_ICON;
                return 1;
            }
        }
        while (g_assets.idle_background_cursor < count)
        {
            unsigned int game = g_assets.idle_background_cursor++;
            TitanAssetService_SyncGameIdentityLocked(game);
            if (g_assets.games[game].background_presence == TITAN_ASSET_PRESENCE_UNKNOWN)
            {
                *game_index = game;
                *kind = TITAN_ASSET_KIND_BACKGROUND;
                return 1;
            }
        }
    }
    return 0;
}

static int TitanAssetService_EnsureCacheDirs(void)
{
    int is_dir = 0;
    if (!TitanStorageIo_PathInfo64(TITAN_STORAGE_DEVICE_ROOT, &is_dir, 0) || !is_dir) return 0;

    is_dir = 0;
    if (TitanStorageIo_PathInfo64(TITAN_ASSET_CACHE_ROOT, &is_dir, 0))
    {
        if (!is_dir) return 0;
    }
    else
    {
        if (TitanStorageIo_CreateDirectory(TITAN_ASSET_CACHE_ROOT) < 0) return 0;
        is_dir = 0;
        if (!TitanStorageIo_PathInfo64(TITAN_ASSET_CACHE_ROOT, &is_dir, 0) || !is_dir) return 0;
    }

    is_dir = 0;
    if (TitanStorageIo_PathInfo64(TITAN_ASSET_CACHE_DIR, &is_dir, 0))
    {
        if (!is_dir) return 0;
    }
    else
    {
        if (TitanStorageIo_CreateDirectory(TITAN_ASSET_CACHE_DIR) < 0) return 0;
        is_dir = 0;
        if (!TitanStorageIo_PathInfo64(TITAN_ASSET_CACHE_DIR, &is_dir, 0) || !is_dir) return 0;
    }
    return 1;
}


static int TitanAssetService_StorageJobIdentityMatchesLocked(const TitanAssetStorageJob *job)
{
    return job != 0 && job->game_index < TITAN_NEUTRINO_GAME_CAPACITY &&
        job->startup_hash != 0u &&
        g_assets.games[job->game_index].startup_hash == job->startup_hash;
}

static void TitanAssetService_LogStorageJobReject(const TitanAssetStorageJob *job, int reason)
{
    TitanDiagnostics_Event(
        TITAN_DIAG_ERROR, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_STORAGE_JOB_REJECT, reason,
        job != 0 ? job->game_index : TITAN_ASSET_INVALID_GAME,
        job != 0 ? (uint32_t)job->kind : 0u,
        job != 0 ? job->total : 0u,
        job != 0 ? job->offset : 0u
    );
}

static int TitanAssetService_StorageJobIsValid(const TitanAssetStorageJob *job)
{
    unsigned int expected;
    if (job == 0 || job->op <= TITAN_ASSET_STORAGE_NONE ||
        job->op > TITAN_ASSET_STORAGE_WRITE_DOWNLOAD ||
        !TitanAssetService_IsValidKind(job->kind) ||
        job->game_index >= TITAN_NEUTRINO_GAME_CAPACITY ||
        job->startup_hash == 0u || job->path[0] == '\0') return 0;
    expected = TitanAssetService_ExpectedTimgSize(job->kind);
    if (expected == 0u || job->offset > job->total) return 0;
    if (job->op == TITAN_ASSET_STORAGE_PROBE_ICON ||
        job->op == TITAN_ASSET_STORAGE_PROBE_BACKGROUND)
        return job->offset == 0u && job->total == 0u;
    if (job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON ||
        job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)
        return job->total > 0u && job->total <= TITAN_ASSET_SOURCE_MAX_SIZE &&
            job->offset <= job->total;
    if (job->total != expected || job->total > sizeof(g_assets.storage_buffer)) return 0;
    if (job->op == TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
    {
        if (job->serial[0] == '\0' || job->temporary_path[0] == '\0' ||
            job->total > sizeof(g_assets.download_buffer) ||
            !TitanAssetService_ValidateTimg(g_assets.download_buffer, job->total, job->kind, 0, 0))
            return 0;
    }
    return 1;
}

static int TitanAssetService_StorageJobBelongsToManualSessionLocked(const TitanAssetStorageJob *job)
{
    return job != 0 && job->manual_owner && g_assets.manual_session_active &&
        !g_assets.manual_clear_cache && job->session_generation != 0u &&
        job->session_generation == g_assets.manual_session_generation;
}

static void TitanAssetService_MarkStorageFailureLocked(
    const TitanAssetStorageJob *job, int count_manual_failure, int allow_retry)
{
    TitanAssetGameState *state;
    if (!TitanAssetService_StorageJobIdentityMatchesLocked(job))
    {
        if (count_manual_failure && TitanAssetService_StorageJobBelongsToManualSessionLocked(job))
            g_assets.session_failed_assets++;
        return;
    }
    state = &g_assets.games[job->game_index];
    {
        int manual = TitanAssetService_StorageJobBelongsToManualSessionLocked(job);
        if (manual && g_assets.artwork_wizard_active)
        {
            /* Import mode never falls through to Internet download retries. */
            if (job->kind == TITAN_ASSET_KIND_ICON)
            {
                state->icon_presence = TITAN_ASSET_PRESENCE_MISSING;
                state->icon_download_pending = 0u;
            }
            else
            {
                state->background_presence = TITAN_ASSET_PRESENCE_MISSING;
                state->background_download_pending = 0u;
            }
            g_assets.session_unavailable_assets++;
            return;
        }
        unsigned int retry_frame = allow_retry
            ? (manual ? g_assets.frame_index : g_assets.frame_index + TITAN_ASSET_RETRY_FRAMES)
            : 0u;
        if (job->kind == TITAN_ASSET_KIND_ICON)
        {
            state->icon_presence = TITAN_ASSET_PRESENCE_MISSING;
            state->icon_download_pending = allow_retry ? 1u : 0u;
            state->icon_retry_frame = retry_frame;
        }
        else
        {
            state->background_presence = TITAN_ASSET_PRESENCE_MISSING;
            state->background_download_pending = allow_retry ? 1u : 0u;
            state->background_retry_frame = retry_frame;
        }
    }
    if (count_manual_failure && TitanAssetService_StorageJobBelongsToManualSessionLocked(job))
        g_assets.session_failed_assets++;
}

static void TitanAssetService_FailStorageWrite(TitanAssetStorageJob *job, int cancelled)
{
    if (job == 0) return;
    if (job->temporary_path[0] != '\0') (void)TitanStorageIo_Remove(job->temporary_path);
    TitanAssetService_Lock();
    if (g_assets.download_ready_for_storage &&
        g_assets.download_game == job->game_index && g_assets.download_kind == job->kind &&
        g_assets.download_startup_hash == job->startup_hash)
        g_assets.download_ready_for_storage = 0;
    if (!cancelled)
    {
        int manual = TitanAssetService_StorageJobBelongsToManualSessionLocked(job);
        if (manual && g_assets.artwork_wizard_active)
        {
            TitanAssetGameState *state = job->game_index < TITAN_NEUTRINO_GAME_CAPACITY
                ? &g_assets.games[job->game_index] : 0;
            if (state != 0)
            {
                if (job->kind == TITAN_ASSET_KIND_ICON)
                {
                    state->icon_presence = TITAN_ASSET_PRESENCE_MISSING;
                    state->icon_download_pending = 0u;
                }
                else
                {
                    state->background_presence = TITAN_ASSET_PRESENCE_MISSING;
                    state->background_download_pending = 0u;
                }
            }
            g_assets.session_failed_assets++;
        }
        else if (manual)
        {
            TitanAssetGameState *state = job->game_index < TITAN_NEUTRINO_GAME_CAPACITY ? &g_assets.games[job->game_index] : 0;
            unsigned int attempts = state == 0 ? TITAN_ASSET_MANUAL_MAX_ATTEMPTS :
                (job->kind == TITAN_ASSET_KIND_ICON ? state->icon_attempts : state->background_attempts);
            int retry = attempts < TITAN_ASSET_MANUAL_MAX_ATTEMPTS;
            TitanAssetService_MarkStorageFailureLocked(job, !retry, retry);
            if (retry)
            {
                if (job->kind == TITAN_ASSET_KIND_ICON) state->icon_retry_frame = g_assets.frame_index + TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES;
                else state->background_retry_frame = g_assets.frame_index + TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES;
            }
        }
        else TitanAssetService_MarkStorageFailureLocked(job, 0, 1);
    }
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_ERROR, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_CACHE_WRITE_FAIL, -1, job->game_index, (uint32_t)job->kind,
        job->offset, job->total);
}

static void TitanAssetService_FailStorageLoad(TitanAssetStorageJob *job, int invalid_cache)
{
    if (job == 0) return;
    if (invalid_cache && job->path[0] != '\0') (void)TitanStorageIo_Remove(job->path);
    TitanAssetService_Lock();
    TitanAssetService_MarkStorageFailureLocked(job, 0, 1);
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_CACHE_LOAD_FAIL, invalid_cache ? -2 : -1,
        job->game_index, (uint32_t)job->kind, job->offset, job->total);
}

static int TitanAssetService_StartStorageJob(void)
{
    unsigned int game = TITAN_ASSET_INVALID_GAME;
    TitanAssetKind kind = 0;

    if (g_assets.net_busy && !g_assets.download_ready_for_storage) return 0;
    if (g_assets.download_ready_for_storage)
    {
        TitanAssetStorageJob *job = &g_assets.storage_job;
        int path_ok;
        int temp_n;
        memset(job, 0, sizeof(*job));
        TitanAssetService_Lock();
        job->op = TITAN_ASSET_STORAGE_WRITE_DOWNLOAD;
        job->kind = g_assets.download_kind;
        job->game_index = g_assets.download_game;
        job->total = g_assets.download_size;
        job->startup_hash = g_assets.download_startup_hash;
        job->session_generation = g_assets.download_session_generation;
        job->manual_owner = g_assets.download_manual_owner;
        TitanAssetService_CopyText(job->serial, sizeof(job->serial), g_assets.download_serial);
        TitanAssetService_Unlock();
        path_ok = TitanAssetService_BuildCachePathForSerial(
            job->serial, job->kind, job->path, sizeof(job->path));
        temp_n = path_ok ? snprintf(job->temporary_path, sizeof(job->temporary_path),
            "%s.part", job->path) : -1;
        if (!path_ok || temp_n < 0 || (unsigned int)temp_n >= sizeof(job->temporary_path) ||
            !TitanAssetService_StorageJobIsValid(job))
        {
            TitanAssetService_LogStorageJobReject(job, -10);
            TitanAssetService_FailStorageWrite(job, 0);
            memset(job, 0, sizeof(*job));
            return 1;
        }
        TitanAssetService_Lock();
        if (!TitanAssetService_StorageJobIdentityMatchesLocked(job))
        {
            TitanAssetService_Unlock();
            TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_CATALOG_RACE_DROP, -1,
                job->game_index, (uint32_t)job->kind, job->startup_hash, 0u);
            TitanAssetService_FailStorageWrite(job, 1);
            memset(job, 0, sizeof(*job));
            return 1;
        }
        TitanAssetService_Unlock();
        return 1;
    }

    TitanAssetService_Lock();
    if (TitanAssetService_SelectProbeLocked(&game, &kind))
    {
        TitanAssetStorageJob *job = &g_assets.storage_job;
        char serial[24];
        int ok;
        memset(job, 0, sizeof(*job));
        TitanAssetService_SyncGameIdentityLocked(game);
        job->op = kind == TITAN_ASSET_KIND_ICON
            ? TITAN_ASSET_STORAGE_PROBE_ICON : TITAN_ASSET_STORAGE_PROBE_BACKGROUND;
        job->kind = kind;
        job->game_index = game;
        job->startup_hash = g_assets.games[game].startup_hash;
        job->catalog_generation = g_assets.catalog_generation;
        job->external_candidate = kind == TITAN_ASSET_KIND_ICON
            ? g_assets.games[game].icon_external_candidate
            : g_assets.games[game].background_external_candidate;
        job->session_generation = g_assets.manual_session_generation;
        job->manual_owner = (g_assets.manual_session_active && !g_assets.manual_clear_cache) ? 1u : 0u;
        ok = TitanAssetService_NormalizeSerial(game, serial, sizeof(serial));
        if (ok)
        {
            TitanAssetService_CopyText(job->serial, sizeof(job->serial), serial);
            ok = TitanAssetService_BuildCachePathForSerial(
                serial, kind, job->path, sizeof(job->path));
        }
        if (!ok || !TitanAssetService_StorageJobIsValid(job))
        {
            TitanAssetGameState *state = &g_assets.games[game];
            if (kind == TITAN_ASSET_KIND_ICON) state->icon_presence = TITAN_ASSET_PRESENCE_UNAVAILABLE;
            else state->background_presence = TITAN_ASSET_PRESENCE_UNAVAILABLE;
            if (g_assets.manual_session_active && !g_assets.manual_clear_cache)
                g_assets.session_failed_assets++;
            TitanAssetService_Unlock();
            TitanAssetService_LogStorageJobReject(job, -11);
            memset(job, 0, sizeof(*job));
            return 1;
        }
        TitanAssetService_Unlock();
        TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
            "PROBE", job->serial, -1);
        return 1;
    }
    TitanAssetService_Unlock();
    return 0;
}

int TitanAssetService_HasStorageWork(void)
{
    unsigned int i;
    if (!g_assets.initialized) return 0;
    /* Ready slots are immutable until consumed. While decoding, storage may
     * read ahead only when the active job is an external ART read (or no job
     * exists yet); cached TIMG reads share the decoder output buffer and wait. */
    if (g_assets.external_decode_ready || g_assets.external_prefetch_ready) return 0;
    if (g_assets.external_decode_busy &&
        (g_assets.manual_clear_cache || g_assets.download_ready_for_storage ||
         (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_PROBE_ICON &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_PROBE_BACKGROUND &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)))
        return 0;
    if (g_assets.manual_session_active && g_assets.manual_clear_cache) return 1;
    /* Missing-only manual sessions drive their cache probes through the
     * serialized Storage Scheduler. Do not depend on the visible/idle probe
     * flags: the manual ordered catalog is its own explicit work queue. */
    if (g_assets.artwork_wizard_active && !g_assets.manual_cancel_requested &&
        g_assets.artwork_wizard_cursor < g_assets.artwork_wizard_asset_count)
        return 1;
    if (g_assets.manual_session_active && !g_assets.manual_refresh_existing &&
        !g_assets.artwork_wizard_active &&
        g_assets.manual_probe_cursor < g_assets.session_games)
        return 1;
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE ||
        g_assets.download_ready_for_storage)
        return 1;
    if (g_assets.selected_game_index < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        TitanAssetGameState *s = &g_assets.games[g_assets.selected_game_index];
        if (s->icon_probe_pending || s->background_probe_pending) return 1;
    }
    for (i = 0u; i < g_assets.visible_count; i++)
    {
        unsigned int game = g_assets.visible_games[i];
        if (game < TITAN_NEUTRINO_GAME_CAPACITY && g_assets.games[game].icon_probe_pending) return 1;
    }
    if (g_assets.frame_index - g_assets.last_visible_change_frame >=
        TITAN_ASSET_IDLE_PREFETCH_FRAMES)
    {
        unsigned int count = TitanNeutrino_GetGameCount();
        if (g_assets.idle_icon_cursor < count || g_assets.idle_background_cursor < count) return 1;
    }
    return 0;
}

static int TitanAssetService_ReadStorageChunk(TitanAssetStorageJob *job)
{
    TitanStorageIoSize file_size = 0;
    unsigned int expected;
    int fd;
    unsigned int remaining;
    unsigned int quantum;
    if (!TitanAssetService_StorageJobIsValid(job))
    {
        TitanAssetService_LogStorageJobReject(job, -20);
        return 0;
    }
    expected = TitanAssetService_ExpectedTimgSize(job->kind);
    if (!TitanStorageIo_PathInfo64(job->path, 0, &file_size) ||
        file_size != (TitanStorageIoSize)expected) return 0;
    if (job->offset == 0u) { job->total = expected; g_assets.storage_buffer_size = 0u; }
    if (job->total != expected || job->offset > job->total) return 0;
    remaining = job->total - job->offset;
    if (remaining == 0u) return 2;
    quantum = remaining > TITAN_ASSET_IO_QUANTUM ? TITAN_ASSET_IO_QUANTUM : remaining;
    fd = TitanStorageIo_Open(job->path, TITAN_STORAGE_IO_READ);
    if (fd < 0) return 0;
    if (job->offset > 0u &&
        TitanStorageIo_Seek64(fd, job->offset, TITAN_STORAGE_IO_SEEK_SET) != (TitanStorageIoOffset)job->offset)
    { TitanStorageIo_Close(fd); return 0; }
    if (!TitanStorageIo_ReadExact(fd, &g_assets.storage_buffer[job->offset], quantum))
    { TitanStorageIo_Close(fd); return 0; }
    TitanStorageIo_Close(fd);
    job->offset += quantum;
    g_assets.storage_buffer_size = job->offset;
    return job->offset >= job->total ? 2 : 1;
}

static int TitanAssetService_ReadExternalStorageChunk(TitanAssetStorageJob *job)
{
    TitanStorageIoSize file_size = 0;
    unsigned char **buffer;
    unsigned int *capacity;
    unsigned int remaining;
    unsigned int quantum;
    int fd;
    if (!TitanAssetService_StorageJobIsValid(job))
    {
        TitanAssetService_LogStorageJobReject(job, -25);
        return 0;
    }
    /* The expected size was discovered by FindExternalArt immediately before
     * the job was queued. Revalidate once, not on every 32 KiB quantum: each
     * PathInfo call is another serialized FileXio RPC and can dominate a large
     * ART read on USB media. */
    if (!job->external_path_checked)
    {
        uint64_t io_started_us = TitanTime_NowMicroseconds();
        job->stage_started_ms = TitanTime_NowMilliseconds();
        if (!TitanStorageIo_PathInfo64(job->path, 0, &file_size) ||
            file_size != (TitanStorageIoSize)job->total ||
            file_size == 0u || file_size > TITAN_ASSET_SOURCE_MAX_SIZE)
        {
            job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
            return 0;
        }
        job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
        job->external_path_checked = 1u;
    }
    if (job->offset == 0u)
    {
        unsigned char *allocated = (unsigned char *)malloc(job->total);
        int defer = 0;
        if (allocated == 0)
        {
            int decoder_owns_stage;
            TitanAssetService_Lock();
            decoder_owns_stage = g_assets.external_decode_busy ||
                g_assets.external_decode_ready || g_assets.storage_stage_ready;
            TitanAssetService_Unlock();
            /* Memory pressure must degrade to the old serial path, not turn a
             * valid local image into an asset failure. Retry after publication
             * releases the current source/TIMG working set. */
            if (decoder_owns_stage) return 1;
            TitanDiagnostics_Event(TITAN_DIAG_ERROR, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_SOURCE_ALLOC_FAIL, -2,
                job->total, job->game_index, (uint32_t)job->kind, 0u);
            return 0;
        }
        TitanAssetService_Lock();
        /* Re-evaluate ownership after allocation: the decoder may have started
         * while malloc ran. Never attach a read to a slot it now owns. */
        job->external_prefetch = g_assets.external_decode_busy ? 1u : 0u;
        if (job->external_prefetch)
        {
            if (g_assets.external_prefetch_ready || g_assets.external_prefetch_buffer != 0)
                defer = 1;
            else
            {
                g_assets.external_prefetch_buffer = allocated;
                g_assets.external_prefetch_capacity = job->total;
                g_assets.external_prefetch_size = 0u;
            }
        }
        else if (g_assets.external_decode_ready || g_assets.external_source_buffer != 0)
            defer = 1;
        else
        {
            g_assets.external_source_buffer = allocated;
            g_assets.external_source_capacity = job->total;
            g_assets.external_source_size = 0u;
        }
        TitanAssetService_Unlock();
        if (defer)
        {
            free(allocated);
            return 1; /* another owner has the available slot; retry next turn */
        }
        job->external_buffer_allocated = 1u;
    }
    buffer = job->external_prefetch
        ? &g_assets.external_prefetch_buffer : &g_assets.external_source_buffer;
    capacity = job->external_prefetch
        ? &g_assets.external_prefetch_capacity : &g_assets.external_source_capacity;
    if (*buffer == 0 || *capacity < job->total || job->offset > job->total)
        return 0;
    remaining = job->total - job->offset;
    if (remaining == 0u) return 2;
    quantum = remaining > TITAN_ASSET_EXTERNAL_IO_QUANTUM
        ? TITAN_ASSET_EXTERNAL_IO_QUANTUM : remaining;
    {
        uint64_t io_started_us = TitanTime_NowMicroseconds();
        fd = TitanStorageIo_Open(job->path, TITAN_STORAGE_IO_READ);
        job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
    }
    if (fd < 0) return 0;
    if (job->offset > 0u)
    {
        uint64_t io_started_us = TitanTime_NowMicroseconds();
        TitanStorageIoOffset seek_result = TitanStorageIo_Seek64(
            fd, job->offset, TITAN_STORAGE_IO_SEEK_SET);
        job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
        if (seek_result != (TitanStorageIoOffset)job->offset)
        {
            io_started_us = TitanTime_NowMicroseconds();
            TitanStorageIo_Close(fd);
            job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
            return 0;
        }
    }
    {
        uint64_t io_started_us = TitanTime_NowMicroseconds();
        int read_ok = TitanStorageIo_ReadExact(fd, &(*buffer)[job->offset], quantum);
        job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
        io_started_us = TitanTime_NowMicroseconds();
        TitanStorageIo_Close(fd);
        job->external_io_elapsed_us += TitanTime_NowMicroseconds() - io_started_us;
        if (!read_ok) return 0;
    }
    job->offset += quantum;
    if (job->external_prefetch) g_assets.external_prefetch_size = job->offset;
    else g_assets.external_source_size = job->offset;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    g_assets.pipeline_last_storage_offset = job->offset;
    TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
        job->external_prefetch ? "PREFETCH" : "READ", job->path,
        job->total != 0u ? (int)((job->offset * 100u) / job->total) : -1);
    return job->offset >= job->total ? 2 : 1;
}

static int TitanAssetService_WriteStorageChunk(TitanAssetStorageJob *job)
{
    int fd;
    unsigned int remaining;
    unsigned int quantum;
    int flags;
    if (!TitanAssetService_StorageJobIsValid(job))
    { TitanAssetService_LogStorageJobReject(job, -30); TitanAssetService_FailStorageWrite(job, 0); return 0; }
    if (g_assets.manual_cancel_requested)
    { TitanAssetService_FailStorageWrite(job, 1); return 0; }
    if (g_assets.storage_abort_requested)
    {
        g_assets.storage_abort_requested = 0;
        TitanAssetService_FailStorageWrite(job, 0);
        return 0;
    }
    if (job->offset == 0u)
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_CACHE_BEGIN, 0,
            job->game_index, (uint32_t)job->kind, job->total, TITAN_ASSET_IO_QUANTUM);
    if (!TitanAssetService_EnsureCacheDirs() || job->offset > job->total) goto fail;
    remaining = job->total - job->offset;
    if (remaining == 0u) goto publish;
    quantum = remaining > TITAN_ASSET_IO_QUANTUM ? TITAN_ASSET_IO_QUANTUM : remaining;
    flags = TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE;
    if (job->offset == 0u) flags |= TITAN_STORAGE_IO_TRUNCATE;
    fd = TitanStorageIo_Open(job->temporary_path, flags);
    if (fd < 0) goto fail;
    if (job->offset > 0u &&
        TitanStorageIo_Seek64(fd, job->offset, TITAN_STORAGE_IO_SEEK_SET) != (TitanStorageIoOffset)job->offset)
    { TitanStorageIo_Close(fd); goto fail; }
    if (!TitanStorageIo_WriteExact(fd, &g_assets.download_buffer[job->offset], quantum))
    { TitanStorageIo_Close(fd); goto fail; }
    (void)TitanStorageIo_Flush(fd);
    TitanStorageIo_Close(fd);
    job->offset += quantum;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    g_assets.pipeline_last_storage_offset = job->offset;
    if (job->offset < job->total) return 1;
publish:
    if (!TitanAssetService_ValidateTimg(g_assets.download_buffer, job->total, job->kind, 0, 0) ||
        !TitanStorageIo_AtomicReplace(job->temporary_path, job->path, 0)) goto fail;
    TitanAssetService_Lock();
    if (TitanAssetService_StorageJobIdentityMatchesLocked(job))
    {
        memcpy(g_assets.storage_buffer, g_assets.download_buffer, job->total);
        g_assets.storage_buffer_size = job->total;
        g_assets.storage_stage_kind = job->kind;
        g_assets.storage_stage_game = job->game_index;
        g_assets.storage_stage_startup_hash = job->startup_hash;
        g_assets.storage_stage_ready = 1;
        g_assets.storage_stage_ready_ms = TitanTime_NowMilliseconds();
    }
    else
        TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_CATALOG_RACE_DROP,
            -2, job->game_index, (uint32_t)job->kind, job->startup_hash, 0u);
    g_assets.status.downloads_completed++;
    if (TitanAssetService_StorageJobBelongsToManualSessionLocked(job))
    {
        g_assets.session_completed_assets++;
        TitanAssetService_RecordManualCompletionLocked(job->kind, job->startup_hash);
    }
    if (g_assets.download_ready_for_storage && g_assets.download_game == job->game_index &&
        g_assets.download_kind == job->kind && g_assets.download_startup_hash == job->startup_hash)
        g_assets.download_ready_for_storage = 0;
    TitanAssetService_Unlock();
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_CACHE_END, 0,
        job->game_index, (uint32_t)job->kind, job->total, 1u);
    return 2;
fail:
    TitanAssetService_FailStorageWrite(job, 0);
    TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_CACHE_END, -1,
        job->game_index, (uint32_t)job->kind, job->offset, job->total);
    return 0;
}

int TitanAssetService_StorageWorkerPump(void)
{
    TitanAssetStorageJob *job;
    int result;
    TitanStorageIoSize size = 0;

    if (!g_assets.initialized) return 0;
    if (g_assets.external_decode_ready || g_assets.external_prefetch_ready) return 0;
    if (g_assets.external_decode_busy &&
        (g_assets.manual_clear_cache || g_assets.download_ready_for_storage ||
         (g_assets.storage_job.op != TITAN_ASSET_STORAGE_NONE &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_PROBE_ICON &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_PROBE_BACKGROUND &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON &&
          g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)))
        return 0;
    if (g_assets.manual_session_active && g_assets.manual_clear_cache)
    {
        TitanStorageIoEntry entry;
        char path[320];
        unsigned int skip;
        int count;
        int n;
        TitanAssetService_Lock();
        if (g_assets.manual_cancel_requested)
        {
            g_assets.manual_session_active = 0;
            g_assets.manual_clear_cache = 0;
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_CANCELLED);
            TitanAssetService_Unlock();
            TitanAssetService_LogSessionEnd(-1);
            return 1;
        }
        skip = g_assets.manual_clear_cursor;
        TitanAssetService_Unlock();
        if (!TitanAssetService_EnsureCacheDirs())
        {
            TitanAssetService_Lock();
            g_assets.session_failed_assets++;
            g_assets.manual_session_active = 0;
            g_assets.manual_clear_cache = 0;
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_COMPLETE);
            TitanAssetService_Unlock();
            TitanAssetService_LogSessionEnd(-1);
            return 1;
        }
        memset(&entry, 0, sizeof(entry));
        count = TitanStorageIo_ListDirectory(TITAN_ASSET_CACHE_DIR, skip, &entry, 1u);
        if (count < 0)
        {
            TitanAssetService_Lock();
            g_assets.session_failed_assets++;
            g_assets.manual_session_active = 0;
            g_assets.manual_clear_cache = 0;
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_COMPLETE);
            TitanAssetService_Unlock();
            TitanAssetService_LogSessionEnd(-1);
            return 1;
        }
        if (count == 0)
        {
            unsigned int i;
            TitanAssetService_Lock();
            for (i = 0u; i < TITAN_NEUTRINO_GAME_CAPACITY; i++)
                TitanAssetService_InvalidateGameRamLocked(i);
            g_assets.manual_session_active = 0;
            g_assets.manual_clear_cache = 0;
            if (g_assets.session_total_assets < g_assets.session_completed_assets + g_assets.session_failed_assets)
                g_assets.session_total_assets = g_assets.session_completed_assets + g_assets.session_failed_assets;
            TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_COMPLETE);
            TitanAssetService_Unlock();
            TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,0x312,0,
                g_assets.session_completed_assets,g_assets.session_failed_assets,
                g_assets.session_total_assets,0u);
            TitanAssetService_LogSessionEnd(g_assets.session_failed_assets == 0u ? 0 : -1);
            return 1;
        }
        if (strchr(entry.name, '/') != 0 || strchr(entry.name, '\\') != 0 || entry.name[0] == '\0')
        {
            TitanAssetService_Lock();
            g_assets.manual_clear_cursor++;
            g_assets.session_failed_assets++;
            TitanAssetService_Unlock();
            return 1;
        }
        n = snprintf(path, sizeof(path), "%s/%s", TITAN_ASSET_CACHE_DIR, entry.name);
        if (n < 0 || (unsigned int)n >= sizeof(path) || TitanStorageIo_RemoveAny(path) < 0)
        {
            TitanAssetService_Lock();
            g_assets.manual_clear_cursor++;
            g_assets.session_failed_assets++;
            TitanAssetService_Unlock();
            return 1;
        }
        TitanAssetService_Lock();
        g_assets.manual_clear_cursor = 0u;
        g_assets.session_completed_assets++;
        if (g_assets.session_total_assets < g_assets.session_completed_assets + g_assets.session_failed_assets)
            g_assets.session_total_assets = g_assets.session_completed_assets + g_assets.session_failed_assets;
        TitanAssetService_Unlock();
        return 1;
    }
    if (g_assets.storage_stage_ready &&
        !(g_assets.storage_job.external_prefetch &&
          (g_assets.storage_job.op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON ||
           g_assets.storage_job.op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)))
        return 0;
    if (g_assets.storage_job.op == TITAN_ASSET_STORAGE_NONE && !TitanAssetService_StartStorageJob()) return 0;
    job = &g_assets.storage_job;
    if (!TitanAssetService_StorageJobIsValid(job))
    {
        TitanAssetService_LogStorageJobReject(job, -40);
        if (job->op == TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
            TitanAssetService_FailStorageWrite(job, g_assets.manual_cancel_requested ? 1 : 0);
        if (job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON ||
            job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)
            TitanAssetService_FreeExternalReadBuffer(job);
        memset(job, 0, sizeof(*job));
        return 1;
    }
    TitanAssetService_Lock();
    if (!TitanAssetService_StorageJobIdentityMatchesLocked(job))
    {
        TitanAssetService_Unlock();
        TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_CATALOG_RACE_DROP,
            -3, job->game_index, (uint32_t)job->kind, job->startup_hash, 0u);
        if (job->op == TITAN_ASSET_STORAGE_WRITE_DOWNLOAD) TitanAssetService_FailStorageWrite(job, 1);
        if (job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON ||
            job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)
            TitanAssetService_FreeExternalReadBuffer(job);
        memset(job, 0, sizeof(*job));
        return 1;
    }
    TitanAssetService_Unlock();

    if (job->op == TITAN_ASSET_STORAGE_PROBE_ICON || job->op == TITAN_ASSET_STORAGE_PROBE_BACKGROUND)
    {
        unsigned int expected = TitanAssetService_ExpectedTimgSize(job->kind);
        int wizard_import = g_assets.artwork_wizard_active;
        /* Raw artwork is deliberately consumed only by the explicit wizard.
         * Normal navigation reads validated TIMG from the persistent cache. */
        if (wizard_import)
        {
            unsigned int external_size = 0u;
            unsigned int external_candidate = 0u;
            uint64_t probe_started_us = TitanTime_NowMicroseconds();
            uint64_t probe_elapsed_us;
            char external_path[TITAN_ASSET_EXTERNAL_PATH_CAPACITY];
            char probe_stage[32];
            int external_found = TitanAssetService_FindExternalArt(job->serial, job->kind,
                job->external_candidate, external_path, sizeof(external_path),
                &external_size, &external_candidate);
            probe_elapsed_us = TitanTime_NowMicroseconds() - probe_started_us;
            (void)snprintf(probe_stage, sizeof(probe_stage), "%s %ums",
                external_found ? "ART FOUND" : "ART MISS",
                (unsigned int)(probe_elapsed_us / 1000u));
            TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
                probe_stage, external_found ? external_path : job->serial, -1);
            TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_EXTERNAL_PROBE_TIMING, 0, job->game_index,
                (uint32_t)job->kind, external_found ? external_size : 0u,
                (unsigned int)(probe_elapsed_us / 1000u));
            if (external_found)
            {
                job->external_candidate = external_candidate;
                TitanAssetService_CopyText(job->path, sizeof(job->path), external_path);
                job->op = job->kind == TITAN_ASSET_KIND_ICON
                    ? TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON
                    : TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND;
                job->offset = 0u;
                job->total = external_size;
                g_assets.status.cache_misses++;
                TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
                    TITAN_ASSET_DIAG_EXTERNAL_FOUND, 0, job->game_index,
                    (uint32_t)job->kind, external_size, 0u);
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
                    "FOUND", job->path, 0);
                return 1;
            }
        }
        {
            int present = TitanStorageIo_PathInfo64(job->path, 0, &size) &&
                size == (TitanStorageIoSize)expected;
            if (present)
            {
                job->op = job->kind == TITAN_ASSET_KIND_ICON
                    ? TITAN_ASSET_STORAGE_LOAD_ICON : TITAN_ASSET_STORAGE_LOAD_BACKGROUND;
                job->offset = 0u;
                job->total = expected;
                g_assets.status.cache_hits++;
                return 1;
            }
        }
        if (TitanStorageIo_PathInfo64(job->path, 0, &size) && size != 0u)
            (void)TitanStorageIo_Remove(job->path);
        TitanAssetService_Lock();
        TitanAssetService_MarkStorageFailureLocked(job, 0, 1);
        g_assets.status.cache_misses++;
        TitanAssetService_Unlock();
        memset(job, 0, sizeof(*job));
        return 1;
    }
    if (job->op == TITAN_ASSET_STORAGE_LOAD_ICON || job->op == TITAN_ASSET_STORAGE_LOAD_BACKGROUND)
    {
        result = TitanAssetService_ReadStorageChunk(job);
        if (result == 2)
        {
            if (TitanAssetService_ValidateTimg(g_assets.storage_buffer, g_assets.storage_buffer_size, job->kind, 0, 0))
            {
                TitanAssetService_Lock();
                if (TitanAssetService_StorageJobIdentityMatchesLocked(job))
                {
                    g_assets.storage_stage_kind = job->kind;
                    g_assets.storage_stage_game = job->game_index;
                    g_assets.storage_stage_startup_hash = job->startup_hash;
                    g_assets.storage_stage_ready = 1;
                    g_assets.storage_stage_ready_ms = TitanTime_NowMilliseconds();
                    if (TitanAssetService_StorageJobBelongsToManualSessionLocked(job) &&
                        !g_assets.manual_refresh_existing)
                        g_assets.session_completed_assets++;
                }
                TitanAssetService_Unlock();
            }
            else TitanAssetService_FailStorageLoad(job, 1);
            memset(job, 0, sizeof(*job));
        }
        else if (result == 0)
        {
            TitanAssetService_FailStorageLoad(job, 0);
            memset(job, 0, sizeof(*job));
        }
        return 1;
    }
    if (job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON ||
        job->op == TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND)
    {
        result = TitanAssetService_ReadExternalStorageChunk(job);
        if (result == 2)
        {
            unsigned char *completed_buffer;
            unsigned int completed_size;
            char completed_path[TITAN_ASSET_EXTERNAL_PATH_CAPACITY];
            unsigned int completed_game_index;
            unsigned int completed_kind;
            unsigned int completed_total;
            unsigned int completed_prefetch;
            uint64_t completed_started_ms;
            uint64_t completed_io_elapsed_us;
            TitanAssetService_Lock();
            completed_buffer = job->external_prefetch
                ? g_assets.external_prefetch_buffer : g_assets.external_source_buffer;
            completed_size = job->external_prefetch
                ? g_assets.external_prefetch_size : g_assets.external_source_size;
            if (TitanAssetService_StorageJobIdentityMatchesLocked(job) &&
                job->catalog_generation == g_assets.catalog_generation &&
                completed_buffer != 0 && completed_size == job->total)
            {
                if (job->external_prefetch)
                {
                    g_assets.external_prefetch_kind = job->kind;
                    g_assets.external_prefetch_game = job->game_index;
                    g_assets.external_prefetch_candidate = job->external_candidate;
                    g_assets.external_prefetch_startup_hash = job->startup_hash;
                    g_assets.external_prefetch_catalog_generation = job->catalog_generation;
                    TitanAssetService_CopyText(g_assets.external_prefetch_path,
                        sizeof(g_assets.external_prefetch_path), job->path);
                    g_assets.external_prefetch_ready = 1;
                    TitanAssetService_PromoteExternalPrefetchLocked();
                }
                else
                {
                    g_assets.external_decode_kind = job->kind;
                    g_assets.external_decode_game = job->game_index;
                    g_assets.external_decode_candidate = job->external_candidate;
                    g_assets.external_decode_startup_hash = job->startup_hash;
                    g_assets.external_decode_catalog_generation = job->catalog_generation;
                    g_assets.external_decode_session_generation = g_assets.manual_session_generation;
                    g_assets.external_decode_wizard_import = g_assets.artwork_wizard_active;
                    TitanAssetService_CopyText(g_assets.external_decode_path,
                        sizeof(g_assets.external_decode_path), job->path);
                    g_assets.external_decode_queued_ms = TitanTime_NowMilliseconds();
                    g_assets.external_decode_ready = 1;
                    g_assets.external_decode_busy = 0;
                }
                job->external_buffer_allocated = 0u;
                g_assets.pipeline_last_progress_frame = g_assets.frame_index;
            }
            else TitanAssetService_FreeExternalReadBufferLocked(job);
            completed_game_index = job->game_index;
            completed_kind = (unsigned int)job->kind;
            completed_total = job->total;
            completed_prefetch = job->external_prefetch;
            completed_started_ms = job->stage_started_ms;
            completed_io_elapsed_us = job->external_io_elapsed_us;
            TitanAssetService_CopyText(completed_path, sizeof(completed_path), job->path);
            memset(job, 0, sizeof(*job));
            TitanAssetService_Unlock();
            {
                uint64_t now = TitanTime_NowMilliseconds();
                uint64_t elapsed = now >= completed_started_ms
                    ? now - completed_started_ms : 0u;
                uint64_t io_ms = completed_io_elapsed_us / 1000u;
                uint64_t wait_ms;
                char stage[40];
                if (elapsed > 0xffffffffu) elapsed = 0xffffffffu;
                if (io_ms > elapsed) io_ms = elapsed;
                wait_ms = elapsed - io_ms;
                (void)snprintf(stage, sizeof(stage), "%s %us I%u W%u",
                    completed_prefetch ? "PREF" : "READ",
                    (unsigned int)(elapsed / 1000u), (unsigned int)io_ms,
                    (unsigned int)wait_ms);
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
                    stage, completed_path, -1);
                TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
                    TITAN_ASSET_DIAG_EXTERNAL_READ_END, 0, completed_game_index,
                    completed_kind, completed_total, completed_prefetch);
                TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
                    TITAN_ASSET_DIAG_EXTERNAL_READ_TIMING, 0, completed_game_index,
                    completed_kind, completed_total, (unsigned int)elapsed);
                TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
                    TITAN_ASSET_DIAG_EXTERNAL_READ_IO_TIMING, 0, completed_game_index,
                    completed_kind, completed_total,
                    (unsigned int)(completed_io_elapsed_us / 1000u));
            }
        }
        else if (result == 0)
        {
            TitanAssetService_FreeExternalReadBuffer(job);
            TitanAssetService_Lock();
            TitanAssetService_MarkStorageFailureLocked(job, 0, 1);
            TitanAssetService_Unlock();
            TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_EXTERNAL_READ_END, -1, job->game_index,
                (uint32_t)job->kind, job->offset, job->total);
            memset(job, 0, sizeof(*job));
        }
        return 1;
    }
    if (job->op == TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
    {
        result = TitanAssetService_WriteStorageChunk(job);
        if (result == 2 || result == 0) memset(job, 0, sizeof(*job));
        return 1;
    }
    TitanAssetService_LogStorageJobReject(job, -41);
    memset(job, 0, sizeof(*job));
    return 1;
}

#if defined(__EE__) || defined(_EE)
typedef struct TitanAssetFetchContext
{
    unsigned int game_index;
    TitanAssetKind kind;
    unsigned int catalog_generation;
    uint32_t startup_hash;
    unsigned int session_generation;
    unsigned char manual_owner;
} TitanAssetFetchContext;

static void TitanAssetService_HttpTrace(
    void *user_data,
    TitanHttpTracePhase phase,
    int result
)
{
    const TitanAssetFetchContext *context = (const TitanAssetFetchContext *)user_data;
    uint16_t event = 0u;
    switch (phase)
    {
        case TITAN_HTTP_TRACE_DNS_BEGIN: event = TITAN_ASSET_DIAG_DNS_BEGIN; break;
        case TITAN_HTTP_TRACE_DNS_END: event = TITAN_ASSET_DIAG_DNS_END; break;
        case TITAN_HTTP_TRACE_TCP_BEGIN: event = TITAN_ASSET_DIAG_TCP_BEGIN; break;
        case TITAN_HTTP_TRACE_TCP_END: event = TITAN_ASSET_DIAG_TCP_END; break;
        case TITAN_HTTP_TRACE_TLS_BEGIN: event = TITAN_ASSET_DIAG_TLS_BEGIN; break;
        case TITAN_HTTP_TRACE_TLS_END: event = TITAN_ASSET_DIAG_TLS_END; break;
        case TITAN_HTTP_TRACE_TLS_ENGINE_ERROR: event = TITAN_ASSET_DIAG_TLS_ENGINE_ERROR; break;
        case TITAN_HTTP_TRACE_PROTOCOL_ERROR: event = TITAN_ASSET_DIAG_HTTP_PROTOCOL_ERROR; break;
        case TITAN_HTTP_TRACE_SOCKET_ERROR: event = TITAN_ASSET_DIAG_SOCKET_ERROR; break;
        case TITAN_HTTP_TRACE_CONNECT_ERROR: event = TITAN_ASSET_DIAG_CONNECT_ERROR; break;
        case TITAN_HTTP_TRACE_CONNECTION_REUSE: event = TITAN_ASSET_DIAG_CONNECTION_REUSE; break;
        case TITAN_HTTP_TRACE_CONNECTION_CLOSE: event = TITAN_ASSET_DIAG_CONNECTION_CLOSE; break;
        default: return;
    }
    TitanDiagnostics_Event(
        result < 0 ? TITAN_DIAG_WARN : TITAN_DIAG_TRACE,
        TITAN_DIAG_ASSET, event, result,
        context != 0 ? context->game_index : TITAN_ASSET_INVALID_GAME,
        context != 0 ? (uint32_t)context->kind : 0u,
        0u, 0u
    );
}

static void TitanAssetService_HttpProgress(
    void *user_data,
    unsigned int received_bytes,
    unsigned int total_bytes
)
{
    uint64_t now = TitanTime_NowMilliseconds();
    uint64_t elapsed;
    uint64_t bps;
    (void)user_data;
    TitanAssetService_Lock();
    g_assets.transfer_bytes_received = received_bytes;
    g_assets.transfer_bytes_total = total_bytes;
    if (g_assets.transfer_started_ms == 0u) g_assets.transfer_started_ms = now;
    elapsed = now > g_assets.transfer_started_ms ? now - g_assets.transfer_started_ms : 0u;
    bps = elapsed > 0u ? ((uint64_t)received_bytes * 1000u) / elapsed : 0u;
    if (bps > 0xffffffffu) bps = 0xffffffffu;
    g_assets.transfer_bytes_per_second = (unsigned int)bps;
    g_assets.transfer_progress_percent = total_bytes > 0u
        ? (received_bytes >= total_bytes ? 100u : (received_bytes * 100u) / total_bytes) : 0u;
    g_assets.pipeline_last_progress_frame = g_assets.frame_index;
    TitanAssetService_Unlock();
}

typedef struct TitanAssetProviderCandidate
{
    const char *name;
    char url[TITAN_ASSET_PROVIDER_URL_CAPACITY];
} TitanAssetProviderCandidate;

static int TitanAssetService_ShouldAbortFetch(void *user_data)
{
    const TitanAssetFetchContext *context = (const TitanAssetFetchContext *)user_data;
    const TitanNetworkStatus *network = TitanNetwork_GetStatus();
    int abort = 0;
    /* Library scan is a background lane, not an exclusive operation. Aborting
     * on each scan quantum repeatedly restarts the same selected download. */
    if (g_assets.net_shutdown || g_assets.manual_cancel_requested || g_assets.watchdog_abort_requested ||
        (context != 0 && context->catalog_generation != g_assets.catalog_generation) ||
        (context != 0 && !context->manual_owner && !g_assets.automatic_download_enabled) ||
        network == 0 || network->state != TITAN_NETWORK_ONLINE ||
        TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive() ||
        TitanAppRuntime_IsActive() || TitanStorageScheduler_IsForegroundBusy())
        return 1;
    if (!g_assets.manual_session_active && g_assets.automatic_download_enabled &&
        (!g_assets.automatic_idle_eligible ||
         g_assets.frame_index - g_assets.last_user_activity_frame < TITAN_ASSET_AUTOMATIC_IDLE_FRAMES))
        return 1;
    if (!g_assets.manual_session_active && context != 0 &&
        context->kind == TITAN_ASSET_KIND_BACKGROUND)
    {
        TitanAssetService_Lock();
        abort = g_assets.selected_game_index != context->game_index;
        TitanAssetService_Unlock();
    }
    return abort;
}

static void TitanAssetService_ImageYield(void *user_data)
{
    (void)user_data;
    DelayThread(TITAN_ASSET_NET_YIELD_US);
}

static unsigned int TitanAssetService_BuildProviders(
    const char *serial,
    TitanAssetKind kind,
    TitanAssetProviderCandidate *providers,
    unsigned int capacity
)
{
    char opl_serial[24];
    unsigned int count = 0u;
    int n;
    if (serial == 0 || providers == 0 || capacity == 0u ||
        !TitanAssetService_BuildOplSerial(serial, opl_serial, sizeof(opl_serial)))
        return 0u;

#define ADD_PROVIDER(label, format, ...) do { \
    if (count < capacity) { \
        providers[count].name = (label); \
        n = snprintf(providers[count].url, sizeof(providers[count].url), (format), __VA_ARGS__); \
        if (n >= 0 && (unsigned int)n < sizeof(providers[count].url)) count++; \
    } \
} while (0)

    if (kind == TITAN_ASSET_KIND_ICON)
    {
        /* ICON is square/icon artwork only; mirrors serve the same semantic
         * object and never introduce a cover fallback. */
        ADD_PROVIDER(
            "PSBBN square CDN",
            "https://cdn.jsdelivr.net/gh/CosmicScale/psbbn-art-database@main/art/%s.png",
            opl_serial
        );
        ADD_PROVIDER(
            "PSBBN square raw",
            "https://raw.githubusercontent.com/CosmicScale/psbbn-art-database/main/art/%s.png",
            opl_serial
        );
        ADD_PROVIDER(
            "OSD-XMB ICON0 CDN",
            "https://cdn.jsdelivr.net/gh/HiroTex/OSD-XMB-ARTDB@main/PS2/%s/ICON0.PNG",
            opl_serial
        );
        ADD_PROVIDER(
            "OSD-XMB ICON0 raw",
            "https://raw.githubusercontent.com/HiroTex/OSD-XMB-ARTDB/main/PS2/%s/ICON0.PNG",
            opl_serial
        );
    }
    else
    {
        /* Titan has only ICON and BACKGROUND. Covers never enter either path. */
        ADD_PROVIDER(
            "Luden02 BG CDN",
            "https://cdn.jsdelivr.net/gh/Luden02/psx-ps2-opl-art-database@main/PS2/%s/%s_BG_00.png",
            opl_serial, opl_serial
        );
        ADD_PROVIDER(
            "Luden02 BG raw",
            "https://raw.githubusercontent.com/Luden02/psx-ps2-opl-art-database/main/PS2/%s/%s_BG_00.png",
            opl_serial, opl_serial
        );
        ADD_PROVIDER(
            "Luden02 screenshot CDN",
            "https://cdn.jsdelivr.net/gh/Luden02/psx-ps2-opl-art-database@main/PS2/%s/%s_SCR_00.png",
            opl_serial, opl_serial
        );
        ADD_PROVIDER(
            "Luden02 screenshot raw",
            "https://raw.githubusercontent.com/Luden02/psx-ps2-opl-art-database/main/PS2/%s/%s_SCR_00.png",
            opl_serial, opl_serial
        );
        ADD_PROVIDER(
            "OSD-XMB PIC2 CDN",
            "https://cdn.jsdelivr.net/gh/HiroTex/OSD-XMB-ARTDB@main/PS2/%s/PIC2.PNG",
            opl_serial
        );
        ADD_PROVIDER(
            "OSD-XMB PIC2 raw",
            "https://raw.githubusercontent.com/HiroTex/OSD-XMB-ARTDB/main/PS2/%s/PIC2.PNG",
            opl_serial
        );
    }
#undef ADD_PROVIDER
    return count;
}

static int TitanAssetService_SelectDownload(
    unsigned int *game_index,
    TitanAssetKind *kind
)
{
    unsigned int i;
    int found = 0;
    TitanAssetService_Lock();
    if (!g_assets.download_ready_for_storage && !g_assets.storage_stage_ready &&
        g_assets.storage_job.op == TITAN_ASSET_STORAGE_NONE)
    {
        if (g_assets.artwork_wizard_active)
        {
            /* Local conversion never enters the HTTP provider pipeline. */
        }
        else if (g_assets.manual_session_active && !g_assets.manual_clear_cache)
        {
            TitanAssetKind wanted =
                (g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_ICONS ||
                 g_assets.manual_phase == TITAN_ASSET_MANUAL_DOWNLOAD_ICONS)
                    ? TITAN_ASSET_KIND_ICON : TITAN_ASSET_KIND_BACKGROUND;
            {
                unsigned int *cursor = wanted == TITAN_ASSET_KIND_ICON
                    ? &g_assets.manual_icon_download_cursor
                    : &g_assets.manual_background_download_cursor;
                unsigned int start = g_assets.session_games > 0u ? (*cursor % g_assets.session_games) : 0u;
                for (i = 0u; i < g_assets.session_games; i++)
                {
                    unsigned int ordinal = (start + i) % g_assets.session_games;
                    unsigned int ordered_game = g_assets.manual_order[ordinal];
                    TitanAssetGameState *state;
                    if (ordered_game >= TITAN_NEUTRINO_GAME_CAPACITY)
                    {
                        TitanDiagnostics_Event(TITAN_DIAG_ERROR, TITAN_DIAG_ASSET,
                            TITAN_ASSET_DIAG_STORAGE_JOB_REJECT, -50, ordered_game, ordinal,
                            g_assets.session_games, 0u);
                        continue;
                    }
                    state = &g_assets.games[ordered_game];
                    if (wanted == TITAN_ASSET_KIND_ICON && state->icon_download_pending &&
                        g_assets.frame_index >= state->icon_retry_frame)
                    {
                        state->icon_download_pending = 0u;
                        if (state->icon_attempts < 255u) state->icon_attempts++;
                        *game_index = ordered_game; *kind = wanted;
                        g_assets.current_game_ordinal = ordinal + 1u;
                        *cursor = ordinal + 1u;
                        found = 1; break;
                    }
                    if (wanted == TITAN_ASSET_KIND_BACKGROUND && state->background_download_pending &&
                        g_assets.frame_index >= state->background_retry_frame)
                    {
                        state->background_download_pending = 0u;
                        if (state->background_attempts < 255u) state->background_attempts++;
                        *game_index = ordered_game; *kind = wanted;
                        g_assets.current_game_ordinal = ordinal + 1u;
                        *cursor = ordinal + 1u;
                        found = 1; break;
                    }
                }
            }
            if (found && wanted == TITAN_ASSET_KIND_ICON &&
                g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_ICONS)
                TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_DOWNLOAD_ICONS);
            if (found && wanted == TITAN_ASSET_KIND_BACKGROUND &&
                g_assets.manual_phase == TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS)
                TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS);
        }
        else if (g_assets.automatic_download_enabled)
        {
            unsigned int selected = g_assets.selected_game_index;
            if (selected < TITAN_NEUTRINO_GAME_CAPACITY)
            {
                TitanAssetGameState *state = &g_assets.games[selected];
                if (state->icon_download_pending && g_assets.frame_index >= state->icon_retry_frame)
                {
                    state->icon_download_pending = 0u; *game_index = selected;
                    *kind = TITAN_ASSET_KIND_ICON; found = 1;
                }
                /* Icons are globally exhausted before any background is selected. */
            }
            if (!found)
            {
                for (i = 0u; i < g_assets.visible_count; i++)
                {
                    unsigned int game = g_assets.visible_games[i];
                    TitanAssetGameState *state;
                    if (game >= TITAN_NEUTRINO_GAME_CAPACITY) continue;
                    state = &g_assets.games[game];
                    if (state->icon_download_pending && g_assets.frame_index >= state->icon_retry_frame)
                    {
                        state->icon_download_pending = 0u; *game_index = game;
                        *kind = TITAN_ASSET_KIND_ICON; found = 1; break;
                    }
                }
            }
            if (!found && g_assets.frame_index - g_assets.last_visible_change_frame >=
                TITAN_ASSET_IDLE_PREFETCH_FRAMES)
            {
                unsigned int count = TitanNeutrino_GetGameCount();
                for (i = 0u; i < count && i < TITAN_NEUTRINO_GAME_CAPACITY; i++)
                {
                    TitanAssetGameState *state = &g_assets.games[i];
                    if (state->icon_download_pending && g_assets.frame_index >= state->icon_retry_frame)
                    {
                        state->icon_download_pending = 0u; *game_index = i;
                        *kind = TITAN_ASSET_KIND_ICON; found = 1; break;
                    }
                }
                if (!found && selected < TITAN_NEUTRINO_GAME_CAPACITY)
                {
                    TitanAssetGameState *state = &g_assets.games[selected];
                    if (state->background_download_pending &&
                        g_assets.frame_index - g_assets.selected_since_frame >= TITAN_ASSET_BACKGROUND_DEBOUNCE_FRAMES &&
                        g_assets.frame_index >= state->background_retry_frame)
                    {
                        state->background_download_pending = 0u; *game_index = selected;
                        *kind = TITAN_ASSET_KIND_BACKGROUND; found = 1;
                    }
                }
                if (!found)
                {
                    for (i = 0u; i < g_assets.visible_count; i++)
                    {
                        unsigned int game = g_assets.visible_games[i];
                        TitanAssetGameState *state;
                        if (game >= TITAN_NEUTRINO_GAME_CAPACITY || game == selected) continue;
                        state = &g_assets.games[game];
                        if (state->background_download_pending && g_assets.frame_index >= state->background_retry_frame)
                        { state->background_download_pending = 0u; *game_index = game; *kind = TITAN_ASSET_KIND_BACKGROUND; found = 1; break; }
                    }
                }
                if (!found)
                {
                    for (i = 0u; i < count && i < TITAN_NEUTRINO_GAME_CAPACITY; i++)
                    {
                        TitanAssetGameState *state = &g_assets.games[i];
                        if (state->background_download_pending && g_assets.frame_index >= state->background_retry_frame)
                        { state->background_download_pending = 0u; *game_index = i; *kind = TITAN_ASSET_KIND_BACKGROUND; found = 1; break; }
                    }
                }
            }
        }
    }
    if (found) { g_assets.net_busy = 1; g_assets.pipeline_last_progress_frame = g_assets.frame_index; }
    TitanAssetService_Unlock();
    return found;
}

static int TitanAssetService_DownloadOne(unsigned int game_index, TitanAssetKind kind)
{
    char serial[24];
    TitanAssetProviderCandidate providers[8];
    unsigned int provider_count;
    unsigned int provider_index;
    unsigned char *source;
    TitanAssetFetchContext fetch_context;
    TitanHttpClientOptions options;
    TitanAssetImageOptions image_options;
    const TitanNeutrinoGame *game;
    int saw_transient_failure = 0;

    if (!TitanAssetService_IsValidKind(kind) || game_index >= TITAN_NEUTRINO_GAME_CAPACITY)
        return -1;
    game = TitanNeutrino_GetGame(game_index);
    if (game == 0 || game->startup[0] == '\0' ||
        !TitanAssetService_NormalizeSerial(game_index, serial, sizeof(serial))) return -1;

    fetch_context.game_index = game_index;
    fetch_context.kind = kind;
    TitanAssetService_Lock();
    fetch_context.catalog_generation = g_assets.catalog_generation;
    fetch_context.session_generation = g_assets.manual_session_generation;
    fetch_context.manual_owner = g_assets.manual_session_active ? 1u : 0u;
    TitanAssetService_Unlock();
    fetch_context.startup_hash = TitanAssetService_HashStartup(game->startup);
    provider_count = TitanAssetService_BuildProviders(
        serial, kind, providers, sizeof(providers) / sizeof(providers[0])
    );
    if (provider_count == 0u) return -1;

    source = g_assets.source_buffer;
    if (source == 0)
    {
        source = (unsigned char *)malloc(TITAN_ASSET_SOURCE_MAX_SIZE);
        if (source == 0)
        {
            TitanDiagnostics_Event(TITAN_DIAG_ERROR, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_SOURCE_ALLOC_FAIL, -1,
                TITAN_ASSET_SOURCE_MAX_SIZE, game_index, (uint32_t)kind, 0u);
            return 0;
        }
        g_assets.source_buffer = source;
    }

    memset(&options, 0, sizeof(options));
    options.should_cancel = TitanAssetService_ShouldAbortFetch;
    options.trace = TitanAssetService_HttpTrace;
    options.progress = TitanAssetService_HttpProgress;
    options.user_data = &fetch_context;
    options.yield_microseconds = TITAN_ASSET_NET_YIELD_US;
    options.maximum_redirects = 3u;
    options.deadline_milliseconds = 60000u;
    memset(&image_options, 0, sizeof(image_options));
    image_options.should_cancel = TitanAssetService_ShouldAbortFetch;
    image_options.yield = TitanAssetService_ImageYield;
    image_options.user_data = &fetch_context;
    image_options.yield_every_rows = 8u;
    image_options.minimum_source_width = kind == TITAN_ASSET_KIND_ICON ? 128u : 256u;
    image_options.minimum_source_height = kind == TITAN_ASSET_KIND_ICON ? 128u : 128u;

    for (provider_index = 0u; provider_index < provider_count; provider_index++)
    {
        unsigned int source_size = 0u;
        unsigned int timg_size = 0u;
        int status = 0;
        int ok;
        TitanHttpResultInfo http_info;
        int source_magic_ok = 0;
        if (TitanAssetService_ShouldAbortFetch(&fetch_context)) return -2;
        TitanAssetService_Lock();
        TitanAssetService_CopyText(g_assets.last_provider, sizeof(g_assets.last_provider),
            providers[provider_index].name);
        g_assets.transfer_bytes_received = 0u;
        g_assets.transfer_bytes_total = 0u;
        g_assets.transfer_bytes_per_second = 0u;
        g_assets.transfer_progress_percent = 0u;
        g_assets.transfer_started_ms = TitanTime_NowMilliseconds();
        TitanAssetService_Unlock();

        TitanDiagnostics_Event(
            TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_DOWNLOAD_BEGIN, 0,
            game_index, (uint32_t)kind, provider_index, 0u
        );
        memset(&http_info, 0, sizeof(http_info));
        ok = TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_ASSET_SERVICE, providers[provider_index].url, source, TITAN_ASSET_SOURCE_MAX_SIZE,
            &source_size, &status, &options, &http_info);
        TitanDiagnostics_Event(
            ok ? TITAN_DIAG_INFO : TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
            TITAN_ASSET_DIAG_DOWNLOAD_END, ok ? status : -1,
            game_index, (uint32_t)kind, provider_index, source_size
        );
        if (!ok)
        {
            if (http_info.failure_reason == TITAN_HTTP_FAILURE_CANCELLED &&
                TitanAssetService_ShouldAbortFetch(&fetch_context)) return -2;
            saw_transient_failure = 1;
            continue;
        }
        if (status == 404 || status == 410) continue;
        if (status != 200) { saw_transient_failure = 1; continue; }
        if (source_size == 0u) { saw_transient_failure = 1; continue; }
        if (http_info.content_length != 0u && http_info.bytes_received != http_info.content_length)
        {
            saw_transient_failure = 1;
            continue;
        }
        source_magic_ok = (source_size >= 8u && memcmp(source, "\x89PNG\r\n\x1a\n", 8u) == 0) ||
            (source_size >= 2u && source[0] == 0xffu && source[1] == 0xd8u);
        if (!source_magic_ok)
        {
            /* A CDN may return a complete HTML/error object with HTTP 200.
             * Reject this provider and try mirrors; if every provider is bad,
             * keep the item retryable instead of misclassifying it as absent. */
            saw_transient_failure = 1;
            continue;
        }

        TitanDiagnostics_Event(
            TITAN_DIAG_INFO, TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_DECODE_BEGIN, 0,
            game_index, (uint32_t)kind, source_size, 0u
        );
        ok = TitanAssetImage_ConvertToTimgEx(
            source, source_size, kind, g_assets.download_buffer,
            sizeof(g_assets.download_buffer), &timg_size, &image_options);
        TitanDiagnostics_Event(
            ok ? TITAN_DIAG_INFO : TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
            TITAN_ASSET_DIAG_DECODE_END, ok ? 0 : -1,
            game_index, (uint32_t)kind, source_size, timg_size
        );
        /* A complete image that this decoder rejects is a provider/source issue,
         * not a network error. Try the next semantic provider immediately. */
        if (!ok)
        {
            if (TitanAssetService_ShouldAbortFetch(&fetch_context)) return -2;
            saw_transient_failure = 1;
            continue;
        }
        if (!TitanAssetService_ValidateTimg(g_assets.download_buffer, timg_size, kind, 0, 0))
            continue;
        if (TitanAssetService_ShouldAbortFetch(&fetch_context)) return -2;

        TitanAssetService_Lock();
        g_assets.download_size = timg_size;
        g_assets.download_kind = kind;
        g_assets.download_game = game_index;
        g_assets.download_startup_hash = fetch_context.startup_hash;
        g_assets.download_session_generation = fetch_context.session_generation;
        g_assets.download_manual_owner = fetch_context.manual_owner;
        TitanAssetService_CopyText(g_assets.download_serial,
            sizeof(g_assets.download_serial), serial);
        g_assets.download_ready_for_storage = 1;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanAssetService_Unlock();
        return 1;
    }
    return saw_transient_failure ? 0 : -1;
}

typedef struct TitanAssetExternalDecodeContext
{
    unsigned int game_index;
    TitanAssetKind kind;
    unsigned int catalog_generation;
    unsigned int candidate;
    unsigned int session_generation;
    uint32_t startup_hash;
    int wizard_import;
} TitanAssetExternalDecodeContext;

static int TitanAssetService_ShouldAbortExternalDecode(void *user_data)
{
    const TitanAssetExternalDecodeContext *context =
        (const TitanAssetExternalDecodeContext *)user_data;
    /* Decode cooperatively yields; a catalog scan is not a reason to discard
     * the source and start the same ART read/decode cycle again. */
    if (g_assets.net_shutdown || context == 0 ||
        context->catalog_generation != g_assets.catalog_generation ||
        context->game_index >= TITAN_NEUTRINO_GAME_CAPACITY ||
        g_assets.games[context->game_index].startup_hash != context->startup_hash ||
        TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive() ||
        TitanAppRuntime_IsActive() || TitanStorageScheduler_IsForegroundBusy())
        return 1;
    if (context->wizard_import &&
        (!g_assets.artwork_wizard_active || !g_assets.manual_session_active ||
         g_assets.manual_cancel_requested ||
         context->session_generation != g_assets.manual_session_generation))
        return 1;
    if (!context->wizard_import && context->kind == TITAN_ASSET_KIND_BACKGROUND &&
        g_assets.selected_game_index != context->game_index)
        return 1;
    return 0;
}

static int TitanAssetService_ProcessExternalDecode(void)
{
    TitanAssetExternalDecodeContext context;
    TitanAssetImageOptions options;
    unsigned char *source;
    unsigned int source_size;
    unsigned int timg_size = 0u;
    uint64_t decode_started_ms;
    uint64_t decode_elapsed_ms;
    uint64_t decode_queued_ms;
    uint64_t decode_dispatch_ms;
    char path[TITAN_ASSET_EXTERNAL_PATH_CAPACITY];
    char decode_stage[32];
    char dispatch_stage[32];
    char serial[24] = {0};
    int have_serial = 0;
    int ok;

    TitanAssetService_Lock();
    if (!g_assets.external_decode_ready || g_assets.external_decode_busy ||
        g_assets.storage_stage_ready || g_assets.external_source_buffer == 0 ||
        g_assets.external_source_size == 0u)
    {
        TitanAssetService_Unlock();
        return 0;
    }
    g_assets.external_decode_ready = 0;
    g_assets.external_decode_busy = 1;
    context.game_index = g_assets.external_decode_game;
    context.kind = g_assets.external_decode_kind;
    context.catalog_generation = g_assets.external_decode_catalog_generation;
    context.candidate = g_assets.external_decode_candidate;
    context.session_generation = g_assets.external_decode_session_generation;
    context.startup_hash = g_assets.external_decode_startup_hash;
    context.wizard_import = g_assets.external_decode_wizard_import;
    source = g_assets.external_source_buffer;
    source_size = g_assets.external_source_size;
    decode_queued_ms = g_assets.external_decode_queued_ms;
    TitanAssetService_CopyText(path, sizeof(path), g_assets.external_decode_path);
    TitanAssetService_CopyText(g_assets.last_provider, sizeof(g_assets.last_provider),
        context.wizard_import ? "Asset Wizard" : "External OPL/PS2 Launcher ART");
    TitanAssetService_Unlock();
    if (context.wizard_import)
        have_serial = TitanAssetService_NormalizeSerial(context.game_index, serial,
            sizeof(serial));

    memset(&options, 0, sizeof(options));
    options.should_cancel = TitanAssetService_ShouldAbortExternalDecode;
    options.yield = TitanAssetService_ImageYield;
    options.user_data = &context;
    /* Local artwork is streamed concurrently with the next storage read through
     * a second buffer. Yield less often here to avoid millisecond sleep tax per
     * tiny row group, while foreground cancellation still checks every row. */
    options.yield_every_rows = 32u;
    /* OPL ICO art is commonly 64x64, while OPL covers are portrait images.
     * Accept both local conventions; this lower bound applies only to the
     * local ART reader, not to downloaded provider assets. */
    options.minimum_source_width = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 256u;
    options.minimum_source_height = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 128u;

    decode_started_ms = TitanTime_NowMilliseconds();
    decode_dispatch_ms = decode_started_ms >= decode_queued_ms
        ? decode_started_ms - decode_queued_ms : 0u;
    if (decode_dispatch_ms > 0xffffffffu) decode_dispatch_ms = 0xffffffffu;
    (void)snprintf(dispatch_stage, sizeof(dispatch_stage), "DECODE WAIT %ums",
        (unsigned int)decode_dispatch_ms);
    TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
        dispatch_stage, path, -1);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_EXTERNAL_DISPATCH_TIMING, 0, context.game_index,
        (uint32_t)context.kind, (unsigned int)decode_dispatch_ms, source_size);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_EXTERNAL_DECODE_BEGIN, 0, context.game_index,
        (uint32_t)context.kind, source_size, 0u);
    TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
        "DECODE", path, -1);
    ok = TitanAssetImage_ConvertToTimgEx(source, source_size, context.kind,
        g_assets.storage_buffer, sizeof(g_assets.storage_buffer), &timg_size, &options);
    if (ok)
        ok = TitanAssetService_ValidateTimg(g_assets.storage_buffer, timg_size,
            context.kind, 0, 0);
    {
        uint64_t now = TitanTime_NowMilliseconds();
        decode_elapsed_ms = now >= decode_started_ms ? now - decode_started_ms : 0u;
        if (decode_elapsed_ms > 0xffffffffu) decode_elapsed_ms = 0xffffffffu;
        (void)snprintf(decode_stage, sizeof(decode_stage), "DECODE %ums",
            (unsigned int)decode_elapsed_ms);
    }

    {
        int aborted = TitanAssetService_ShouldAbortExternalDecode(&context);
        TitanAssetService_Lock();
        if (ok && !aborted && context.wizard_import &&
            g_assets.artwork_wizard_active &&
            context.session_generation == g_assets.manual_session_generation && have_serial)
        {
            memcpy(g_assets.download_buffer, g_assets.storage_buffer, timg_size);
            g_assets.download_size = timg_size;
            g_assets.download_kind = context.kind;
            g_assets.download_game = context.game_index;
            g_assets.download_startup_hash = context.startup_hash;
            g_assets.download_session_generation = context.session_generation;
            g_assets.download_manual_owner = 1u;
            TitanAssetService_CopyText(g_assets.download_serial,
                sizeof(g_assets.download_serial), serial);
            g_assets.download_ready_for_storage = 1;
        }
        else if (ok && !aborted)
        {
            g_assets.storage_buffer_size = timg_size;
            g_assets.storage_stage_kind = context.kind;
            g_assets.storage_stage_game = context.game_index;
            g_assets.storage_stage_startup_hash = context.startup_hash;
            g_assets.storage_stage_ready = 1;
            g_assets.storage_stage_ready_ms = TitanTime_NowMilliseconds();
        }
        else if (context.game_index < TITAN_NEUTRINO_GAME_CAPACITY &&
            g_assets.games[context.game_index].startup_hash == context.startup_hash)
        {
            TitanAssetGameState *state = &g_assets.games[context.game_index];
            if (context.wizard_import && g_assets.artwork_wizard_active &&
                context.session_generation == g_assets.manual_session_generation)
            {
                if (aborted && !g_assets.manual_cancel_requested &&
                    context.catalog_generation == g_assets.catalog_generation &&
                    g_assets.artwork_wizard_cursor > 0u)
                    g_assets.artwork_wizard_cursor--;
                else if (!aborted)
                    g_assets.session_failed_assets++;
            }
            else if (aborted)
            {
                /* Navigation decode was pre-empted; leave it eligible for a
                 * later visible-window request without treating it as missing. */
                if (context.kind == TITAN_ASSET_KIND_ICON)
                {
                    state->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
                    state->icon_probe_pending = 1u;
                }
                else
                {
                    state->background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
                    state->background_probe_pending = 1u;
                }
            }
            else if (!context.wizard_import && context.candidate < 7u)
            {
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_WARN,
                    "FALLBACK", "Imagem recusada; tentando proxima", -1);
                /* A higher-priority OPL image may exist but be corrupt or use
                 * an unsupported encoding. Try the next supported suffix
                 * before treating this game's artwork as unavailable. */
                if (context.kind == TITAN_ASSET_KIND_ICON)
                {
                    state->icon_external_candidate = (unsigned char)(context.candidate + 1u);
                    state->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
                    state->icon_probe_pending = 1u;
                }
                else
                {
                    state->background_external_candidate = (unsigned char)(context.candidate + 1u);
                    state->background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
                    state->background_probe_pending = 1u;
                }
            }
            else
            {
                TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_ERROR,
                    "DECODE ERROR", path, -1);
                TitanAssetStorageJob recovery;
                memset(&recovery, 0, sizeof(recovery));
                recovery.game_index = context.game_index;
                recovery.kind = context.kind;
                recovery.startup_hash = context.startup_hash;
                TitanAssetService_MarkStorageFailureLocked(&recovery, 0, 1);
            }
        }
        g_assets.external_decode_ready = 0;
        g_assets.external_decode_busy = 0;
        g_assets.external_decode_path[0] = '\0';
        g_assets.external_decode_wizard_import = 0;
        g_assets.pipeline_last_progress_frame = g_assets.frame_index;
        TitanAssetService_FreeExternalSource();
        TitanAssetService_PromoteExternalPrefetchLocked();
        TitanAssetService_Unlock();
    }

    TitanDiagnostics_Event(ok ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
        TITAN_DIAG_ASSET, TITAN_ASSET_DIAG_EXTERNAL_DECODE_END, ok ? 0 : -1,
        context.game_index, (uint32_t)context.kind, source_size, timg_size);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
        TITAN_ASSET_DIAG_EXTERNAL_DECODE_TIMING, 0, context.game_index,
        (uint32_t)context.kind, (unsigned int)decode_elapsed_ms, timg_size);
    TitanDiagnostics_ReportOperation("ASSET", ok ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
        decode_stage, path, -1);
    (void)path;
    return 1;
}

static void TitanAssetService_NetworkWorker(void *argument)
{
    unsigned int online_since_frame = 0u;
    (void)argument;
    for (;;)
    {
        unsigned int game = TITAN_ASSET_INVALID_GAME;
        TitanAssetKind kind = 0;
        int result;
        int stack_ok;
        unsigned int selection_generation;
        const TitanNetworkStatus *network;
        if (g_assets.net_shutdown) break;
        if (TitanAssetService_ProcessExternalDecode())
        {
            stack_ok = TitanAssetService_CheckNetworkStack(0, 0);
            TitanAssetService_LogNetworkStack();
            if (!stack_ok)
            {
                g_assets.net_faulted = 1;
                break;
            }
            DelayThread(TITAN_ASSET_NET_YIELD_US);
            continue;
        }
        /* A completed TIMG owns the single publish buffer until the main thread
         * commits it. Do not let a prefetched decode overwrite that stage, or
         * let an unrelated network fetch start while the handoff is blocked. */
        if (g_assets.external_decode_ready)
        {
            DelayThread(TITAN_ASSET_NET_YIELD_US);
            continue;
        }
        network = TitanNetwork_GetStatus();
        if ((!g_assets.automatic_download_enabled && !g_assets.manual_session_active && !g_assets.manual_start_queued) || g_assets.manual_clear_cache)
        {
            online_since_frame = 0u;
            if (g_assets.source_buffer != 0 && !g_assets.net_busy && !g_assets.download_ready_for_storage)
            { free(g_assets.source_buffer); g_assets.source_buffer = 0; }
            DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue;
        }
        if (network == 0 || network->state != TITAN_NETWORK_ONLINE)
        { online_since_frame = 0u; DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue; }
        if (online_since_frame == 0u) online_since_frame = g_assets.frame_index;
        if (TitanPackageInstaller_IsActive() || TitanSystemUpdate_IsActive() || TitanAppRuntime_IsActive() ||
            TitanStorageScheduler_IsForegroundBusy() || TitanStorageScheduler_IsLibraryScanBusy())
        { DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue; }
        /* A user-requested manual/clear operation owns the next safe slot. Do
         * not let automatic mode race it and create another tail to wait for. */
        if (g_assets.manual_start_queued)
        { DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue; }
        if (!g_assets.manual_session_active)
        {
            int auto_paused = g_assets.frame_index - online_since_frame < TITAN_ASSET_NETWORK_STABLE_FRAMES ||
                !g_assets.automatic_idle_eligible ||
                g_assets.frame_index - g_assets.last_user_activity_frame < TITAN_ASSET_AUTOMATIC_IDLE_FRAMES;
            if (auto_paused)
            {
                if (!g_assets.automatic_paused_for_activity)
                {
                    g_assets.automatic_paused_for_activity = 1;
                    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,TITAN_ASSET_DIAG_AUTO_PAUSE,0,
                        g_assets.frame_index,online_since_frame,g_assets.last_user_activity_frame,
                        (uint32_t)g_assets.automatic_idle_eligible);
                }
                DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue;
            }
            if (g_assets.automatic_paused_for_activity)
            {
                g_assets.automatic_paused_for_activity = 0;
                TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,TITAN_ASSET_DIAG_AUTO_RESUME,0,
                    g_assets.frame_index,online_since_frame,g_assets.last_user_activity_frame,0u);
            }
        }
        if (!TitanAssetService_SelectDownload(&game, &kind))
        { DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US); continue; }
        selection_generation = g_assets.catalog_generation;
        g_assets.net_busy = 1;
        if (!g_assets.manual_session_active) g_assets.current_game_ordinal = 0u;
        g_assets.current_game_index = game; g_assets.current_kind = kind;
        TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,0x316,0,game,(uint32_t)kind,0,0);
        result = TitanAssetService_DownloadOne(game, kind);
        g_assets.net_busy = 0;
        TitanDiagnostics_Event(result > 0 ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
            TITAN_DIAG_ASSET,0x317,result,game,(uint32_t)kind,0,0);
        stack_ok = TitanAssetService_CheckNetworkStack(0, 0);
        TitanAssetService_LogNetworkStack();
        if (!stack_ok)
        {
            int ended_manual = 0;
            TitanAssetService_Lock();
            g_assets.net_faulted = 1;
            g_assets.automatic_download_enabled = 0;
            if (g_assets.manual_session_active)
            {
                ended_manual = 1;
                g_assets.manual_session_active = 0;
                g_assets.manual_start_queued = 1;
                g_assets.manual_start_refresh = g_assets.manual_refresh_existing;
                g_assets.manual_start_clear_cache = 0;
                g_assets.manual_start_preserve_completed = 1;
                g_assets.manual_recovering_previous_state = 1;
                g_assets.manual_recovery_count++;
                TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_RECOVERING);
            }
            g_assets.download_ready_for_storage = 0;
            TitanAssetService_Unlock();
            if (ended_manual)
                TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
                    TITAN_ASSET_DIAG_STACK_CANARY_FAIL, -3, g_assets.manual_recovery_count,
                    g_assets.session_completed_assets, g_assets.session_total_assets, 0u);
            break;
        }
        if (selection_generation != g_assets.catalog_generation)
        {
            TitanDiagnostics_Event(TITAN_DIAG_WARN, TITAN_DIAG_ASSET,
                TITAN_ASSET_DIAG_CATALOG_RACE_DROP, -5, game, (uint32_t)kind,
                selection_generation, g_assets.catalog_generation);
        }
        else if (game < TITAN_NEUTRINO_GAME_CAPACITY && result <= 0 &&
            !(result == -2 && g_assets.manual_cancel_requested))
        {
            TitanAssetService_Lock();
            if (g_assets.manual_session_active && result == -2 && g_assets.watchdog_abort_requested)
            {
                TitanAssetGameState *state = &g_assets.games[game];
                /* The attempt was cut because it made no progress. Put it at
                 * the tail immediately; the round-robin cursor already points
                 * at the following game, so one bad host/title cannot block. */
                if (kind == TITAN_ASSET_KIND_ICON)
                {
                    state->icon_download_pending = 1u;
                    state->icon_retry_frame = g_assets.frame_index;
                }
                else
                {
                    state->background_download_pending = 1u;
                    state->background_retry_frame = g_assets.frame_index;
                }
                g_assets.manual_recovering_previous_state = 1;
                g_assets.manual_recovery_count++;
            }
            else if (g_assets.manual_session_active && result != -2)
            {
                TitanAssetGameState *state = &g_assets.games[game];
                int permanent = result == -1;
                int retry = !permanent && ((kind == TITAN_ASSET_KIND_ICON ? state->icon_attempts : state->background_attempts) <
                    TITAN_ASSET_MANUAL_MAX_ATTEMPTS);
                if (kind == TITAN_ASSET_KIND_ICON)
                {
                    state->icon_presence = permanent ? TITAN_ASSET_PRESENCE_UNAVAILABLE : TITAN_ASSET_PRESENCE_MISSING;
                    state->icon_download_pending = retry ? 1u : 0u;
                    state->icon_retry_frame = retry ? g_assets.frame_index + TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES : 0u;
                }
                else
                {
                    state->background_presence = permanent ? TITAN_ASSET_PRESENCE_UNAVAILABLE : TITAN_ASSET_PRESENCE_MISSING;
                    state->background_download_pending = retry ? 1u : 0u;
                    state->background_retry_frame = retry ? g_assets.frame_index + TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES : 0u;
                }
                if (!retry)
                {
                    if (permanent) g_assets.session_unavailable_assets++;
                    else g_assets.session_failed_assets++;
                }
            }
            else
            {
                int retry = result == -2 || result == 0;
                if (kind == TITAN_ASSET_KIND_ICON)
                {
                    g_assets.games[game].icon_retry_frame = result == -2 ? g_assets.frame_index : g_assets.frame_index + TITAN_ASSET_RETRY_FRAMES;
                    g_assets.games[game].icon_download_pending = retry ? 1u : 0u;
                    if (result == -1) g_assets.games[game].icon_presence = TITAN_ASSET_PRESENCE_UNAVAILABLE;
                }
                else
                {
                    g_assets.games[game].background_retry_frame = result == -2 ? g_assets.frame_index : g_assets.frame_index + TITAN_ASSET_RETRY_FRAMES;
                    g_assets.games[game].background_download_pending = retry ? 1u : 0u;
                    if (result == -1) g_assets.games[game].background_presence = TITAN_ASSET_PRESENCE_UNAVAILABLE;
                }
            }
            TitanAssetService_Unlock();
        }
        while (g_assets.download_ready_for_storage && !g_assets.net_shutdown)
            DelayThread(TITAN_ASSET_NET_IDLE_DELAY_US);
        if (g_assets.watchdog_abort_requested) g_assets.watchdog_abort_requested = 0;
        DelayThread(TITAN_ASSET_NET_YIELD_US);
    }
    if (g_assets.source_buffer != 0) { free(g_assets.source_buffer); g_assets.source_buffer = 0; }
    g_assets.net_thread_id = -1;
    ExitDeleteThread();
}

static int TitanAssetService_EnsureNetworkWorker(void)
{
    ee_thread_t thread;
    /* Never create the asynchronous worker without mutual exclusion. A failed
     * boot-time semaphore allocation is recoverable, but only by successfully
     * allocating the lock before clearing net_faulted. */
    if (!TitanAssetService_EnsureLock())
    {
        g_assets.net_faulted = 1;
        return 0;
    }
    if (g_assets.net_faulted && g_assets.net_thread_id < 0 && !g_assets.net_busy)
    {
        g_assets.net_faulted = 0;
        g_assets.manual_recovering_previous_state = 1;
        g_assets.manual_recovery_count++;
    }
    if (g_assets.net_faulted) return 0;
    if (g_assets.net_thread_id >= 0) return 1;
    TitanAssetService_PrepareNetworkStack();
    memset(&thread, 0, sizeof(thread));
    thread.func = TitanAssetService_NetworkWorker;
    thread.stack = g_asset_net_stack_arena.stack;
    thread.stack_size = sizeof(g_asset_net_stack_arena.stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = TITAN_ASSET_NET_PRIORITY;
    g_assets.net_thread_id = CreateThread(&thread);
    if (g_assets.net_thread_id < 0) return 0;
    if (StartThread(g_assets.net_thread_id, 0) < 0)
    {
        DeleteThread(g_assets.net_thread_id);
        g_assets.net_thread_id = -1;
        return 0;
    }
    TitanDiagnostics_Event(TITAN_DIAG_DEBUG,TITAN_DIAG_ASSET,0x318,0,
        (uint32_t)g_assets.net_thread_id,TITAN_ASSET_NET_PRIORITY,
        (uint32_t)sizeof(g_asset_net_stack_arena.stack),0u);
    return 1;
}
#endif

void TitanAssetService_Initialize(void)
{
    memset(&g_assets, 0, sizeof(g_assets));
    g_assets.selected_game_index = TITAN_ASSET_INVALID_GAME;
    g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;
    g_assets.status.selected_game_index = TITAN_ASSET_INVALID_GAME;
    g_assets.status.current_game_index = TITAN_ASSET_INVALID_GAME;
    g_assets.current_game_index = TITAN_ASSET_INVALID_GAME;
    g_assets.external_decode_game = TITAN_ASSET_INVALID_GAME;
    g_assets.status.direct_internet = 1;
    g_assets.automatic_download_enabled = 0;
    g_assets.manual_phase = TITAN_ASSET_MANUAL_IDLE;
    g_assets.catalog_generation = 1u;
#if defined(__EE__) || defined(_EE)
    g_assets.lock_sema_id = -1;
    g_assets.net_thread_id = -1;
    if (!TitanAssetService_EnsureLock())
        g_assets.net_faulted = 1;
    /* BF9: the shared low-priority asset worker is lazy. Merely booting or
     * connecting the network cannot create it. Direct external ART may wake
     * the same worker later, but it never performs FileXio from that thread. */
#endif
    g_assets.initialized = 1;
    g_assets.status.initialized = 1;
    TitanAssetService_CopyText(
        g_assets.status.detail, sizeof(g_assets.status.detail), "Internet direto pronto; downloads aguardam comando manual"
    );
}

void TitanAssetService_NotifyCatalogChanged(void)
{
    unsigned int i;
    int recovering = 0;
    unsigned int count;
    if (!g_assets.initialized) return;
    count = TitanNeutrino_GetGameCount();
    if (count > TITAN_NEUTRINO_GAME_CAPACITY) count = TITAN_NEUTRINO_GAME_CAPACITY;
    TitanAssetService_Lock();
    g_assets.catalog_generation++;
    if (g_assets.catalog_generation == 0u) g_assets.catalog_generation = 1u;
    if (g_assets.artwork_wizard_active)
        g_assets.manual_cancel_requested = 1;
    g_assets.idle_icon_cursor = 0u;
    g_assets.idle_background_cursor = 0u;
    g_assets.last_visible_change_frame = g_assets.frame_index;
    for (i = 0u; i < TITAN_NEUTRINO_GAME_CAPACITY; i++)
    {
        g_assets.games[i].startup_hash = 0u;
        g_assets.games[i].icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        g_assets.games[i].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN;
        g_assets.games[i].icon_probe_pending = 0u;
        g_assets.games[i].background_probe_pending = 0u;
        g_assets.games[i].icon_download_pending = 0u;
        g_assets.games[i].background_download_pending = 0u;
        g_assets.games[i].icon_attempts = 0u;
        g_assets.games[i].background_attempts = 0u;
        g_assets.games[i].icon_external_candidate = 0u;
        g_assets.games[i].background_external_candidate = 0u;
    }
    for (i = 0u; i < TITAN_ASSET_ICON_SLOT_CAPACITY; i++) g_assets.icon_slots[i].valid = 0;
    g_assets.active_background_ready = 0;
    g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;
    g_assets.storage_stage_ready = 0;
    g_assets.storage_stage_startup_hash = 0u;
    if (g_assets.external_decode_ready && !g_assets.external_decode_busy)
    {
        g_assets.external_decode_ready = 0;
        g_assets.external_decode_path[0] = '\0';
        TitanAssetService_FreeExternalSource();
    }
    if (g_assets.storage_job.op != TITAN_ASSET_STORAGE_WRITE_DOWNLOAD)
        g_assets.download_ready_for_storage = 0;
    g_assets.strip_generation++;

    /* BF12: a Library commit no longer kills a user-requested asset session.
     * Cache files and startup hashes are the durable truth. Rebind the fresh
     * catalog, remember identities that already completed in this session, and
     * continue icons-first without making the user restart the operation. */
    if (g_assets.manual_session_active && !g_assets.manual_clear_cache &&
        !g_assets.artwork_wizard_active)
    {
        recovering = 1;
        g_assets.manual_recovering_previous_state = 1;
        g_assets.manual_recovery_count++;
        TitanAssetService_SetManualPhaseLocked(TITAN_ASSET_MANUAL_RECOVERING);
        TitanAssetService_PrepareManualCatalogLocked(count, 1);
    }
    TitanAssetService_Unlock();
    if (recovering)
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_ASSET,
            TITAN_ASSET_DIAG_CATALOG_RACE_DROP, 1, g_assets.catalog_generation,
            g_assets.session_completed_assets, g_assets.session_total_assets, g_assets.manual_recovery_count);
}

const TitanAssetServiceStatus *TitanAssetService_GetStatus(void)
{
    return &g_assets.status;
}
