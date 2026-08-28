#ifndef PROJECT_TITAN_GAME_SDK_H
#define PROJECT_TITAN_GAME_SDK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Titan Game Runtime (TGR) is intentionally separate from Titan App/.tmod.
 * A game owns the foreground EE frame loop while Titan keeps lifecycle,
 * storage arbitration, audio ownership and recovery under kernel control.
 * ABI v2 is append-only relative to v1. */
#define TITAN_GAME_API_VERSION 2u
#define TITAN_GAME_MAX_PADS 4u
#define TITAN_GAME_DEFAULT_WIDTH 640u
#define TITAN_GAME_DEFAULT_HEIGHT 448u
#define TITAN_GAME_AUDIO_SAMPLE_RATE 32000u
#define TITAN_GAME_AUDIO_CHANNELS 2u
#define TITAN_GAME_STORAGE_NAME_CAPACITY 128u

#define TITAN_GAME_FEATURE_Z16                 0x00000001u
#define TITAN_GAME_FEATURE_COLORED_TRIANGLES   0x00000002u
#define TITAN_GAME_FEATURE_TEXTURE_UPLOAD_P16  0x00000004u
#define TITAN_GAME_FEATURE_TEXTURED_TRIANGLES  0x00000008u
#define TITAN_GAME_FEATURE_FOUR_PAD_ABI         0x00000010u
#define TITAN_GAME_FEATURE_CLIPSPACE_TRIANGLES  0x00000020u
#define TITAN_GAME_FEATURE_RENDER_STATE         0x00000040u
#define TITAN_GAME_FEATURE_PERSPECTIVE_STQ       0x00000080u
#define TITAN_GAME_FEATURE_TWO_TEXTURE_MATERIAL  0x00000100u
#define TITAN_GAME_FEATURE_VIEWPORT_SCISSOR      0x00000200u
#define TITAN_GAME_FEATURE_HAPTICS                0x00000400u
#define TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32 0x00000800u
#define TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8     0x00001000u

#define TITAN_GAME_BUTTON_SELECT   0x0001u
#define TITAN_GAME_BUTTON_L3       0x0002u
#define TITAN_GAME_BUTTON_R3       0x0004u
#define TITAN_GAME_BUTTON_START    0x0008u
#define TITAN_GAME_BUTTON_UP       0x0010u
#define TITAN_GAME_BUTTON_RIGHT    0x0020u
#define TITAN_GAME_BUTTON_DOWN     0x0040u
#define TITAN_GAME_BUTTON_LEFT     0x0080u
#define TITAN_GAME_BUTTON_L2       0x0100u
#define TITAN_GAME_BUTTON_R2       0x0200u
#define TITAN_GAME_BUTTON_L1       0x0400u
#define TITAN_GAME_BUTTON_R1       0x0800u
#define TITAN_GAME_BUTTON_TRIANGLE 0x1000u
#define TITAN_GAME_BUTTON_CIRCLE   0x2000u
#define TITAN_GAME_BUTTON_CROSS    0x4000u
#define TITAN_GAME_BUTTON_SQUARE   0x8000u

#define TITAN_GAME_STORAGE_READ      0x01u
#define TITAN_GAME_STORAGE_WRITE     0x02u
#define TITAN_GAME_STORAGE_CREATE    0x04u
#define TITAN_GAME_STORAGE_TRUNCATE  0x08u
#define TITAN_GAME_STORAGE_EXCLUSIVE 0x10u
#define TITAN_GAME_STORAGE_SEEK_SET     0
#define TITAN_GAME_STORAGE_SEEK_CURRENT 1
#define TITAN_GAME_STORAGE_SEEK_END     2
#define TITAN_GAME_STORAGE_ENTRY_DIRECTORY 0x00000001u

typedef struct TitanGamePadState
{
    uint16_t held;
    uint16_t pressed;
    uint16_t released;
    uint8_t connected;
    uint8_t port;
    uint8_t slot;
    uint8_t reserved0;
    uint8_t left_stick_x;
    uint8_t left_stick_y;
    uint8_t right_stick_x;
    uint8_t right_stick_y;
} TitanGamePadState;

typedef struct TitanGameStorageEntry
{
    char name[TITAN_GAME_STORAGE_NAME_CAPACITY];
    uint64_t size;
    uint32_t flags;
    uint32_t reserved;
} TitanGameStorageEntry;

typedef struct TitanGameVertex3D
{
    float x;
    float y;
    float z;
    float u;
    float v;
    uint32_t rgba;
} TitanGameVertex3D;

typedef struct TitanGameClipVertex
{
    float x;
    float y;
    float z;
    float w;
    float u;
    float v;
    uint32_t rgba;
} TitanGameClipVertex;

/* Fixed-function material lane for N64-era renderers.  The GS has one texture
 * unit, therefore TEX0_TEX1_LOD is executed as two ordered passes by Titan.
 * s/t are normalized pre-division coordinates and q0/q1 are reciprocal clip-W;
 * Titan emits GS ST corrected for Q and TEX0's power-of-two declaration, so a
 * logical normalized coordinate remains exact for NPOT textures too.  Use q=1
 * for affine mapping. */
#define TITAN_GAME_COMBINE_SHADE             0u
#define TITAN_GAME_COMBINE_TEX0              1u
#define TITAN_GAME_COMBINE_TEX0_MODULATE     2u
#define TITAN_GAME_COMBINE_TEX0_DECAL        3u
#define TITAN_GAME_COMBINE_TEX0_TEX1_LOD     4u

#define TITAN_GAME_DEPTH_ALWAYS  0u
#define TITAN_GAME_DEPTH_GEQUAL  1u
#define TITAN_GAME_DEPTH_GREATER 2u

#define TITAN_GAME_BLEND_OPAQUE   0u
#define TITAN_GAME_BLEND_ALPHA    1u
#define TITAN_GAME_BLEND_MODULATE 2u
#define TITAN_GAME_BLEND_ADDITIVE 3u

#define TITAN_GAME_WRAP_REPEAT 0u
#define TITAN_GAME_WRAP_CLAMP  1u
#define TITAN_GAME_WRAP_MIRROR 2u

typedef struct TitanGameSamplerState
{
    uint8_t wrap_s;
    uint8_t wrap_t;
    uint8_t linear_filter;
    uint8_t reserved0;
} TitanGameSamplerState;

typedef struct TitanGameRenderState
{
    uint32_t struct_size;
    int32_t viewport_x;
    int32_t viewport_y;
    uint32_t viewport_width;
    uint32_t viewport_height;
    int32_t scissor_x;
    int32_t scissor_y;
    uint32_t scissor_width;
    uint32_t scissor_height;
    uint8_t depth_test;
    uint8_t depth_write;
    uint8_t depth_compare;
    uint8_t blend_mode;
    uint8_t alpha_compare;
    uint8_t alpha_threshold; /* Public 0..255; Titan maps it to GS 0..128. */
    uint8_t fog_enabled;
    uint8_t combine_mode;
    uint32_t fog_rgba;
    TitanGameSamplerState sampler[2];
    uint32_t reserved[4];
} TitanGameRenderState;

typedef struct TitanGameMaterialVertex
{
    float x;
    float y;
    float z;
    float w;
    float s0;
    float t0;
    float q0;
    float s1;
    float t1;
    float q1;
    uint32_t rgba;
    uint8_t fog;
    uint8_t lod_fraction;
    uint8_t reserved0;
    uint8_t reserved1;
} TitanGameMaterialVertex;

typedef struct TitanGameVideoInfo
{
    uint32_t width;
    uint32_t height;
    uint32_t framebuffer_count;
    uint32_t zbuffer_enabled;
    uint32_t texture_bytes_free;
    uint32_t feature_flags;
} TitanGameVideoInfo;

typedef struct TitanGameSessionConfig
{
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t heap_limit_bytes; /* 0 = Titan default. Kernel clamps this. */
    uint32_t reserved[4];
} TitanGameSessionConfig;

typedef struct TitanGameApiV1
{
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t feature_flags;
    uint32_t reserved0;

    /* Lifecycle/time. request_exit() is cooperative and never tears a game
     * down from an arbitrary call site. */
    void (*request_exit)(void);
    int (*exit_requested)(void);
    uint64_t (*uptime_ms)(void);
    uint64_t (*uptime_us)(void);

    /* Game heap. Alignment must be a power of two from 16..4096. All live
     * allocations are reclaimed by TitanGameRuntime_End(). */
    void *(*alloc)(uint32_t bytes, uint32_t alignment);
    void (*release)(void *pointer);
    uint32_t (*heap_used)(void);
    uint32_t (*heap_limit)(void);

    /* Stable four-slot input ABI. v1 physically populates PS2 ports 0 and 1;
     * slots 2/3 are already reserved for multitap without an ABI break. */
    void (*poll_input)(void);
    int (*get_pad)(uint32_t index, TitanGamePadState *state);
    uint32_t (*connected_pad_count)(void);

    /* Fixed 32 kHz stereo signed-16 PCM stream. */
    int (*audio_begin)(void);
    uint32_t (*audio_submit_pcm)(const int16_t *samples, uint32_t frames);
    uint32_t (*audio_free_frames)(void);
    void (*audio_end)(void);

    /* Foreground single-owner FileXio lane. Relative paths resolve below the
     * installed package root; absolute paths are accepted only when they point
     * below that same root. Writes are restricted to data/. */
    int (*storage_open)(const char *path, uint32_t flags);
    int (*storage_close)(int descriptor);
    int64_t (*storage_read)(int descriptor, void *buffer, uint32_t bytes);
    int64_t (*storage_write)(int descriptor, const void *buffer, uint32_t bytes);
    int64_t (*storage_seek)(int descriptor, int64_t offset, int origin);
    int (*storage_stat)(const char *path, TitanGameStorageEntry *entry);
    int (*storage_list)(const char *path, uint32_t start_index,
                        TitanGameStorageEntry *entries, uint32_t capacity);
    int (*storage_mkdir)(const char *path);
    int (*storage_remove)(const char *path);
    int (*storage_rename)(const char *source, const char *destination);

    /* PS2 renderer. set_mvp accepts a row-major 4x4 matrix. Texture UVs are
     * normalized 0..1. v2 supplies bounded reusable texture slots so caches
     * can explicitly release entries instead of leaking GS VRAM. */
    int (*video_get_info)(TitanGameVideoInfo *info);
    int (*video_begin_frame)(uint8_t red, uint8_t green, uint8_t blue);
    void (*video_set_mvp)(const float matrix16[16]);
    int (*video_draw_indexed_triangles)(
        const TitanGameVertex3D *vertices, uint32_t vertex_count,
        const uint16_t *indices, uint32_t index_count,
        uint32_t texture_handle
    );
    int (*video_upload_texture_psm16)(
        const uint16_t *pixels, uint32_t width, uint32_t height,
        uint32_t stride, uint32_t *texture_handle
    );
    int (*video_end_frame)(void);

    /* Added in v2. Existing v1 fields above retain their offsets. */
    int (*video_release_texture)(uint32_t texture_handle);

    /* Clip-space path for GBI translators. x/y/z/w are the values produced by
     * Fast3D before perspective division; this avoids reconstructing a matrix
     * or discarding W in a native GoldenEye backend. */
    int (*video_draw_clip_triangles)(
        const TitanGameClipVertex *vertices,
        uint32_t vertex_count,
        uint32_t texture_handle
    );

    /* Added after the original v2 prefix.  Callers must check struct_size and
     * TITAN_GAME_FEATURE_RENDER_STATE before dereferencing these fields. */
    int (*video_set_render_state)(const TitanGameRenderState *state);
    int (*video_draw_material_triangles)(
        const TitanGameMaterialVertex *vertices,
        uint32_t vertex_count,
        uint32_t texture0_handle,
        uint32_t texture1_handle
    );

    /* Output-only DualShock motors. small_motor is 0/1; large_motor is 0..255. */
    int (*set_pad_rumble)(
        uint32_t index,
        uint8_t small_motor,
        uint8_t large_motor
    );

    /* Append-only v2 extension.  RGBA is byte ordered R,G,B,A and alpha is
     * preserved at GS PSM32 precision.  Check both struct_size and
     * TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32 before dereferencing. */
    int (*video_upload_texture_rgba32)(
        const uint8_t *rgba,
        uint32_t width,
        uint32_t height,
        uint32_t stride_bytes,
        uint32_t *texture_handle
    );

    /* P162.4 append-only native indexed lane. I8 intensity is preserved
     * losslessly as GS PSM8 + a grayscale CLUT. Callers must check both the
     * feature bit and struct_size before dereferencing. */
    int (*video_upload_texture_i8)(
        const uint8_t *pixels,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint32_t *texture_handle
    );
} TitanGameApiV1;

#ifdef __cplusplus
}
#endif

#endif
