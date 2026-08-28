#ifndef PROJECT_TITAN_APP_SDK_H
#define PROJECT_TITAN_APP_SDK_H

#include <stdint.h>
#include <stddef.h>

struct TitanGameSessionConfig;
struct TitanGameApiV1;

#ifdef __cplusplus
extern "C" {
#endif

#define TITAN_APP_API_VERSION 1u
#define TITAN_APP_LOGICAL_WIDTH 854u
#define TITAN_APP_LOGICAL_HEIGHT 448u
#define TITAN_APP_MEDIA_WIDTH 512u
#define TITAN_APP_MEDIA_HEIGHT 256u
#define TITAN_APP_MEDIA_SAMPLE_RATE 32000u
#define TITAN_APP_MEDIA_CHANNELS 2u

/* ABI v1 is append-only. These flags/types were added after the initial
 * Project Nes consumer; older v1 modules keep working because every original
 * field retains the same offset and they only require their smaller struct_size. */
#define TITAN_APP_STORAGE_ENTRY_DIRECTORY 0x00000001u
#define TITAN_APP_STORAGE_ENTRY_READ_ONLY 0x00000002u
#define TITAN_APP_STORAGE_WRITE_CREATE    0x00000001u
#define TITAN_APP_STORAGE_WRITE_TRUNCATE  0x00000002u
#define TITAN_APP_STORAGE_NAME_CAPACITY 128u


#define TITAN_APP_STORAGE_VOLUME_ROOT_CAPACITY 32u
#define TITAN_APP_STORAGE_VOLUME_ONLINE    0x00000001u
#define TITAN_APP_STORAGE_VOLUME_READ      0x00000002u
#define TITAN_APP_STORAGE_VOLUME_WRITE     0x00000004u
#define TITAN_APP_STORAGE_VOLUME_READ_ONLY 0x00000008u

typedef struct TitanAppStorageVolume
{
    char root[TITAN_APP_STORAGE_VOLUME_ROOT_CAPACITY];
    uint32_t flags;
    uint32_t reserved;
} TitanAppStorageVolume;

#define TITAN_APP_PFS_CAP_READ  0x00000001u
#define TITAN_APP_PFS_CAP_WRITE 0x00000002u
#define TITAN_APP_PFS_STATE_OFFLINE 0u
#define TITAN_APP_PFS_STATE_MOUNTING 1u
#define TITAN_APP_PFS_STATE_ONLINE 2u
#define TITAN_APP_PFS_STATE_UNMOUNTING 3u
#define TITAN_APP_PFS_STATE_ERROR 4u

typedef struct TitanAppPfsMountStatus
{
    uint32_t slot;
    uint32_t generation;
    uint32_t capabilities;
    uint32_t state;
    char root[TITAN_APP_STORAGE_VOLUME_ROOT_CAPACITY];
} TitanAppPfsMountStatus;

#define TITAN_APP_TEXT_INPUT_PENDING   0
#define TITAN_APP_TEXT_INPUT_CONFIRMED 1
#define TITAN_APP_TEXT_INPUT_CANCELLED (-1)

typedef struct TitanAppStorageEntry
{
    char name[TITAN_APP_STORAGE_NAME_CAPACITY];
    uint64_t size;
    uint32_t flags;
    uint32_t reserved;
} TitanAppStorageEntry;

/* Button values deliberately match TitanInputButton. Apps never include the
 * kernel-private input header, so this public ABI remains self-contained. */
#define TITAN_APP_BUTTON_SELECT   0x0001u
#define TITAN_APP_BUTTON_L3       0x0002u
#define TITAN_APP_BUTTON_R3       0x0004u /* reserved by Titan: window/fullscreen toggle */
#define TITAN_APP_BUTTON_START    0x0008u
#define TITAN_APP_BUTTON_UP       0x0010u
#define TITAN_APP_BUTTON_RIGHT    0x0020u
#define TITAN_APP_BUTTON_DOWN     0x0040u
#define TITAN_APP_BUTTON_LEFT     0x0080u
#define TITAN_APP_BUTTON_L2       0x0100u
#define TITAN_APP_BUTTON_R2       0x0200u
#define TITAN_APP_BUTTON_L1       0x0400u
#define TITAN_APP_BUTTON_R1       0x0800u
#define TITAN_APP_BUTTON_TRIANGLE 0x1000u
#define TITAN_APP_BUTTON_CIRCLE   0x2000u
#define TITAN_APP_BUTTON_CROSS    0x4000u
#define TITAN_APP_BUTTON_SQUARE   0x8000u

typedef enum TitanAppNotificationKind
{
    TITAN_APP_NOTIFICATION_INFO = 0,
    TITAN_APP_NOTIFICATION_ACTIVITY,
    TITAN_APP_NOTIFICATION_SUCCESS,
    TITAN_APP_NOTIFICATION_WARNING,
    TITAN_APP_NOTIFICATION_ERROR
} TitanAppNotificationKind;

typedef struct TitanAppInput
{
    uint16_t held;
    uint16_t pressed;
    uint16_t released;
    uint8_t connected;
    uint8_t reserved0;
    uint8_t left_stick_x;
    uint8_t left_stick_y;
    uint8_t right_stick_x;
    uint8_t right_stick_y;
} TitanAppInput;

/* BuildFix29 append-only normalized input/device inspection. System/Home is a
 * system-owned control: TITAN_APP_NBTN_SYSTEM documents the physical capability
 * but input_get_state() masks it while Titan Session/Overlay owns the session. */
#define TITAN_APP_NBTN_SELECT   (1u << 0)
#define TITAN_APP_NBTN_L3       (1u << 1)
#define TITAN_APP_NBTN_R3       (1u << 2)
#define TITAN_APP_NBTN_START    (1u << 3)
#define TITAN_APP_NBTN_UP       (1u << 4)
#define TITAN_APP_NBTN_RIGHT    (1u << 5)
#define TITAN_APP_NBTN_DOWN     (1u << 6)
#define TITAN_APP_NBTN_LEFT     (1u << 7)
#define TITAN_APP_NBTN_L2       (1u << 8)
#define TITAN_APP_NBTN_R2       (1u << 9)
#define TITAN_APP_NBTN_L1       (1u << 10)
#define TITAN_APP_NBTN_R1       (1u << 11)
#define TITAN_APP_NBTN_TRIANGLE (1u << 12)
#define TITAN_APP_NBTN_CIRCLE   (1u << 13)
#define TITAN_APP_NBTN_CROSS    (1u << 14)
#define TITAN_APP_NBTN_SQUARE   (1u << 15)
#define TITAN_APP_NBTN_SYSTEM   (1u << 16) /* reserved to Titan */
#define TITAN_APP_NBTN_TOUCHPAD (1u << 17)
#define TITAN_APP_NBTN_MIC      (1u << 18)

#define TITAN_APP_INPUT_PLAYER_UNASSIGNED 0xffu
#define TITAN_APP_INPUT_CAP_BUTTONS         (1u << 0)
#define TITAN_APP_INPUT_CAP_DPAD            (1u << 1)
#define TITAN_APP_INPUT_CAP_ANALOG_STICKS   (1u << 2)
#define TITAN_APP_INPUT_CAP_ANALOG_TRIGGERS (1u << 3)
#define TITAN_APP_INPUT_CAP_RUMBLE          (1u << 4)
#define TITAN_APP_INPUT_CAP_LIGHT           (1u << 5)
#define TITAN_APP_INPUT_CAP_BATTERY         (1u << 6)
#define TITAN_APP_INPUT_CAP_KEYBOARD        (1u << 7)
#define TITAN_APP_INPUT_CAP_POINTER         (1u << 8)
#define TITAN_APP_INPUT_CAP_WIRELESS        (1u << 9)
#define TITAN_APP_INPUT_CAP_WIRED           (1u << 10)
#define TITAN_APP_INPUT_CAP_SYSTEM_BUTTON   (1u << 11)

typedef enum TitanAppInputDeviceType {
    TITAN_APP_INPUT_DEVICE_NONE = 0,
    TITAN_APP_INPUT_DEVICE_DUALSHOCK2,
    TITAN_APP_INPUT_DEVICE_DUALSHOCK3,
    TITAN_APP_INPUT_DEVICE_DUALSHOCK4,
    TITAN_APP_INPUT_DEVICE_DUALSENSE,
    TITAN_APP_INPUT_DEVICE_XBOX_ONE,
    TITAN_APP_INPUT_DEVICE_XBOX_SERIES,
    TITAN_APP_INPUT_DEVICE_KEYBOARD,
    TITAN_APP_INPUT_DEVICE_MOUSE,
    TITAN_APP_INPUT_DEVICE_GENERIC_HID
} TitanAppInputDeviceType;

typedef enum TitanAppInputTransport {
    TITAN_APP_INPUT_TRANSPORT_NONE = 0,
    TITAN_APP_INPUT_TRANSPORT_SIO2,
    TITAN_APP_INPUT_TRANSPORT_USB,
    TITAN_APP_INPUT_TRANSPORT_BLUETOOTH
} TitanAppInputTransport;

typedef enum TitanAppInputBatteryState {
    TITAN_APP_INPUT_BATTERY_UNSUPPORTED = 0,
    TITAN_APP_INPUT_BATTERY_UNKNOWN,
    TITAN_APP_INPUT_BATTERY_LEVEL,
    TITAN_APP_INPUT_BATTERY_CHARGING,
    TITAN_APP_INPUT_BATTERY_FULL
} TitanAppInputBatteryState;

typedef struct TitanAppNormalizedInput {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
    uint8_t left_stick_x, left_stick_y, right_stick_x, right_stick_y;
    uint8_t trigger_l, trigger_r;
    int16_t pointer_dx, pointer_dy;
    int8_t wheel;
    uint8_t reserved[3];
} TitanAppNormalizedInput;

typedef struct TitanAppInputDeviceInfo {
    uint32_t device_id;
    uint32_t capabilities;
    uint16_t vendor_id, product_id;
    uint8_t device_type;
    uint8_t transport;
    uint8_t player;
    uint8_t connected;
    uint8_t battery_state;
    uint8_t battery_percent;
    uint8_t battery_percent_valid;
    uint8_t battery_raw;
    uint32_t packet_rate;
    uint32_t reconnect_count;
    uint32_t errors;
    uint32_t latency_us;
} TitanAppInputDeviceInfo;

typedef enum TitanAppNetworkState
{
    TITAN_APP_NETWORK_IDLE = 0,
    TITAN_APP_NETWORK_PENDING,
    TITAN_APP_NETWORK_COMPLETE,
    TITAN_APP_NETWORK_FAILED,
    TITAN_APP_NETWORK_CANCELLED
} TitanAppNetworkState;

typedef enum TitanAppNetworkFailure
{
    TITAN_APP_NETWORK_FAILURE_NONE = 0,
    TITAN_APP_NETWORK_FAILURE_CANCELLED,
    TITAN_APP_NETWORK_FAILURE_TIMEOUT,
    TITAN_APP_NETWORK_FAILURE_DNS,
    TITAN_APP_NETWORK_FAILURE_CONNECT,
    TITAN_APP_NETWORK_FAILURE_TLS,
    TITAN_APP_NETWORK_FAILURE_PROTOCOL,
    TITAN_APP_NETWORK_FAILURE_TOO_LARGE,
    TITAN_APP_NETWORK_FAILURE_TRUNCATED
} TitanAppNetworkFailure;

#define TITAN_APP_VMC_NAME_CAPACITY 32u
#define TITAN_APP_VMC_ENTRY_DIRECTORY 0x00000001u
#define TITAN_APP_VMC_ENTRY_READABLE  0x00000002u
#define TITAN_APP_VMC_ENTRY_WRITABLE  0x00000004u
#define TITAN_APP_VMC_ENTRY_HIDDEN    0x00000008u
#define TITAN_APP_VMC_READ_MAX_BYTES (16u * 1024u)

typedef struct TitanAppVmcEntry
{
    char name[TITAN_APP_VMC_NAME_CAPACITY];
    uint32_t size;
    uint32_t flags;
} TitanAppVmcEntry;

typedef struct TitanAppFtpStatus
{
    uint32_t state;
    uint32_t sessions_accepted;
    uint32_t commands_processed;
    uint32_t commands_rejected;
    uint16_t control_port;
    uint16_t reserved;
} TitanAppFtpStatus;

typedef struct TitanAppNetworkResult
{
    TitanAppNetworkState state;
    int32_t http_status;
    uint32_t bytes_available;
    uint32_t content_length;
    TitanAppNetworkFailure failure_reason;
} TitanAppNetworkResult;

typedef struct TitanAppApiV1
{
    uint32_t abi_version;
    uint32_t struct_size;

    /* Lifecycle and time. */
    void (*request_exit)(void);
    uint64_t (*uptime_ms)(void);

    /* Heap owned and accounted by the Titan process. Apps should release
     * allocations normally; Titan also reclaims any outstanding blocks when
     * the session closes. Bulk file/media buffers should come from this heap
     * or from writable storage inside the module image. */
    void *(*alloc)(uint32_t bytes);
    void (*release)(void *pointer);

    /* UI primitives use the same logical 854x448 coordinate system, font,
     * alpha rules and renderer as the Project Titan shell. The kernel validates
     * the canvas bounds and maps them into the shell-owned app window; R3 is
     * reserved by Titan to toggle windowed/fullscreen presentation. */
    void (*draw_text)(
        float x, float y, float scale, const char *text,
        uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
    );
    void (*draw_text_shadow)(
        float x, float y, float scale, const char *text,
        uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
    );
    void (*draw_rect)(
        float x, float y, float width, float height,
        uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
    );
    void (*draw_panel)(float x, float y, float width, float height, uint8_t alpha);
    void (*draw_progress)(
        float x, float y, float width, float height,
        uint32_t percent, uint8_t alpha
    );
    int (*notify)(TitanAppNotificationKind kind, const char *title, const char *message);

    /* Reads are sandboxed to the installed application root. Writes are
     * restricted to the data/ subtree so an app cannot accidentally replace
     * its own module/manifest. Return values are byte counts or -1. */
    int (*file_read)(
        const char *relative_path,
        uint32_t offset,
        void *buffer,
        uint32_t capacity
    );
    int (*file_write_data)(
        const char *relative_path,
        const void *buffer,
        uint32_t size
    );
    int (*file_exists)(const char *relative_path);

    /* Media foundation. Audio is decoded PCM S16LE, stereo, 32 kHz. Apps that
     * port a codec can decode/resample themselves and stream into this queue.
     * Video/background frames are conventional RGB565 512x256. The kernel
     * converts them to the GS native PSMCT16 layout before upload, keeps the
     * frame in EE RAM and presents it through Titan's renderer; in native 720p it reuses
     * the existing PSM16 scratch in tiles, so no extra GS surface/VRAM is owned
     * by the app. Apps never own framebuffer or AUDSRV state directly. */
    int (*media_begin_audio)(void);
    uint32_t (*media_submit_pcm)(const int16_t *samples, uint32_t frames);
    uint32_t (*media_audio_free_frames)(void);
    void (*media_end_audio)(void);
    int (*media_present_rgb565)(
        const uint16_t *pixels,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    );
    void (*media_draw)(
        float x, float y, float width, float height, uint8_t alpha
    );

    /* Append-only ABI v1 storage/launch extension used by trusted utility
     * applications such as the Titan-native wLaunchELF port. Paths are
     * absolute PS2 device paths, but the kernel applies an allowlist and keeps
     * IOP/FileXio ownership inside Project Titan. Directory listing is paged:
     * start_index is the first logical entry and the return value is the number
     * of entries written, 0 at end, or -1 on error. */
    int (*storage_list)(
        const char *absolute_path,
        uint32_t start_index,
        TitanAppStorageEntry *entries,
        uint32_t capacity
    );
    int (*storage_stat)(
        const char *absolute_path,
        TitanAppStorageEntry *entry
    );
    int (*storage_read)(
        const char *absolute_path,
        uint64_t offset,
        void *buffer,
        uint32_t capacity
    );
    int (*storage_write)(
        const char *absolute_path,
        uint64_t offset,
        const void *buffer,
        uint32_t size,
        uint32_t flags
    );
    int (*storage_mkdir)(const char *absolute_path);
    int (*storage_remove)(const char *absolute_path);
    int (*storage_rename)(const char *source, const char *destination);

    /* Successful ELF handoff does not return: Titan validates the executable
     * and transfers through its own stage2, exactly like an external ELF tile. */
    int (*launch_elf)(const char *absolute_path);

    /* Trusted in-process game handoff. These entries are present for every
     * ABI-v1 consumer to preserve the append-only contract, but return failure
     * unless the installed package was declared as type=titan-game. The game
     * keeps Titan resident and borrows the foreground TGR services until
     * game_end() restores shell audio, video, input and storage ownership. */
    int (*game_begin)(
        const struct TitanGameSessionConfig *config,
        const struct TitanGameApiV1 **game_api
    );
    void (*game_end)(void);

    /* BuildFix27 append-only VFS discovery. Only truthfully detected roots are
     * returned. Capacity flags describe what Titan can actually provide. */
    int (*storage_list_volumes)(
        uint32_t start_index,
        TitanAppStorageVolume *volumes,
        uint32_t capacity
    );

    /* Atomically publish a temporary file through Titan Storage/VFS. Both
     * paths remain subject to the same trusted-path allowlist. */
    int (*storage_commit_replace)(
        const char *temporary_path,
        const char *target_path
    );

    /* Shared shell text-input service. Open is asynchronous; poll returns
     * PENDING, CONFIRMED (and copies text) or CANCELLED. */
    int (*text_input_open)(
        const char *title,
        const char *initial_text,
        uint32_t maximum_codepoints
    );
    int (*text_input_poll)(char *text, uint32_t capacity);

    /* BuildFix27 append-only asynchronous network client. The app never owns
     * PS2IP, DNS sockets, TLS state or a network thread. One bounded GET may
     * be active for the current app session. Results are retained by Titan and
     * copied to app memory only through network_get_read(). */
    int (*network_get_begin)(
        const char *url,
        uint32_t maximum_bytes,
        uint32_t deadline_ms
    );
    int (*network_get_status)(TitanAppNetworkResult *result);
    int (*network_get_read)(
        uint32_t offset,
        void *buffer,
        uint32_t capacity
    );
    void (*network_cancel)(void);

    /* Titan-owned PNG/JPEG presentation. The kernel reads and decodes the
     * allowlisted path, preserves the full image with FIT/letterbox semantics
     * and places it in the existing RGB565 media surface. Apps never receive
     * libjpeg/libpng, GS, VRAM or FileXio ownership. */
    int (*media_present_image_file)(const char *absolute_path);

    /* Titan-owned FTP service. Apps may control lifecycle/status only; TCP
     * sockets, passive data channels and Storage/VFS transfers remain inside
     * the kernel service. */
    int (*ftp_start)(const char *absolute_root, uint16_t port);
    void (*ftp_stop)(void);
    int (*ftp_status)(TitanAppFtpStatus *status);

    /* Read-only view of the active profile's VMC root. Titan parses the raw
     * memory-card image and retains filesystem ownership; no vmc: mount is
     * exposed to the module. Returns 1 entry, 0 end, -1 unavailable/corrupt. */
    int (*vmc_list_active_root)(uint32_t index, TitanAppVmcEntry *entry);

    /* Read-only traversal inside the active profile VMC. relative_path uses
     * normalized VMC-internal components (for example "BASLUS-12345"). */
    int (*vmc_list_active_path)(
        const char *relative_path,
        uint32_t index,
        TitanAppVmcEntry *entry
    );

    /* Bounded read-only bytes from one active-profile VMC regular file. */
    int (*vmc_read_active_file)(
        const char *relative_path,
        uint32_t offset,
        void *buffer,
        uint32_t capacity
    );


    /* Kernel-mediated PFS lifecycle. No FileXio descriptor or mount primitive
     * is exposed to the module. Calls fail closed while no target PFS provider
     * is registered. Mounts created by an app session are reclaimed on exit. */
    int (*pfs_available)(void);
    int (*pfs_mount)(
        const char *hdd_source,
        uint32_t requested_slot,
        uint32_t capabilities,
        TitanAppPfsMountStatus *status
    );
    int (*pfs_unmount)(uint32_t slot);
    int (*pfs_status)(uint32_t slot, TitanAppPfsMountStatus *status);

    /* BuildFix29 append-only input discovery. These queries expose normalized
     * state and device metadata without exposing PADMAN/PADEMU/HID internals.
     * System/Home is reserved to Titan and is masked from normalized state. */
    int (*input_get_state)(uint32_t player, TitanAppNormalizedInput *state);
    int (*input_get_device)(uint32_t player, TitanAppInputDeviceInfo *info);
} TitanAppApiV1;

/* A .tmod is a MIPS ET_REL object loaded into the Titan process. These four
 * symbols form ABI v1. init/update/draw are required; shutdown is optional.
 * titan_app_init() is a pure launch handshake: it may validate/store the API
 * pointer and initialize module-local state, but must not call heap, storage,
 * notification, media or drawing APIs. Resource work begins in the first
 * titan_app_update() after Titan commits the session ACTIVE. */
typedef int (*TitanAppInitFn)(const TitanAppApiV1 *api, const char *app_root);
typedef void (*TitanAppUpdateFn)(const TitanAppInput *input, uint32_t delta_ms);
typedef void (*TitanAppDrawFn)(void);
typedef void (*TitanAppShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif
