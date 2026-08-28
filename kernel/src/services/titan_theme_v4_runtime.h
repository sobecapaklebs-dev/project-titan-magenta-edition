#ifndef PROJECT_TITAN_THEME_V4_RUNTIME_H
#define PROJECT_TITAN_THEME_V4_RUNTIME_H

#include <stdint.h>

#define TITAN_THEME_V4_RUNTIME_API_VERSION 1u

#define TITAN_THEME_V4_MAX_LAYERS 32u
#define TITAN_THEME_V4_ASSET_PATH 192u
#define TITAN_THEME_V4_MAX_EVENTS 8u

typedef enum TitanThemeV4Priority {
    TITAN_THEME_V4_ESSENTIAL = 0,
    TITAN_THEME_V4_ENHANCED = 1,
    TITAN_THEME_V4_DECORATIVE = 2
} TitanThemeV4Priority;

typedef enum TitanThemeV4Context {
    TITAN_THEME_V4_CONTEXT_HOME = 1,
    TITAN_THEME_V4_CONTEXT_TOP = 2,
    TITAN_THEME_V4_CONTEXT_BOTH = 3
} TitanThemeV4Context;

typedef enum TitanThemeV4Mode {
    TITAN_THEME_V4_STATIC = 0,
    TITAN_THEME_V4_LOOP = 1,
    TITAN_THEME_V4_PINGPONG = 2,
    TITAN_THEME_V4_ONCE = 3
} TitanThemeV4Mode;

typedef enum TitanThemeV4Degradation {
    TITAN_THEME_V4_DEGRADE_NONE = 0,
    TITAN_THEME_V4_DEGRADE_DECORATIVE = 1,
    TITAN_THEME_V4_DEGRADE_ENHANCED = 2,
    TITAN_THEME_V4_DEGRADE_SAFE = 3
} TitanThemeV4Degradation;

typedef struct TitanThemeV4Layer {
    char asset[TITAN_THEME_V4_ASSET_PATH];
    uint16_t width;
    uint16_t height;
    int16_t z;
    uint8_t priority;
    uint8_t context;
    uint8_t mode;
    uint8_t alpha;
    int32_t x_milli;
    int32_t y_milli;
    int32_t scale_x_milli;
    int32_t scale_y_milli;
    int32_t rotation_mdeg;
    int32_t parallax_x_milli;
    int32_t parallax_y_milli;
    uint32_t phase_ms;
    uint32_t duration_ms;
    int32_t anim_x_milli;
    int32_t anim_y_milli;
    int32_t anim_scale_x_milli;
    int32_t anim_scale_y_milli;
    int32_t anim_rotation_mdeg;
    int16_t anim_alpha;
    int16_t top_alpha_delta;
    int32_t top_x_milli;
    int32_t top_y_milli;
    int32_t top_scale_x_milli;
    int32_t top_scale_y_milli;
    int32_t top_rotation_mdeg;
    uint32_t declared_bytes;
    uint32_t declaration_order;
} TitanThemeV4Layer;

typedef struct TitanThemeV4ResolvedLayer {
    const TitanThemeV4Layer *source;
    float x;
    float y;
    float scale_x;
    float scale_y;
    float rotation_degrees;
    unsigned char alpha;
    uint8_t visible;
    uint8_t static_fast_path;
    uint16_t reserved;
} TitanThemeV4ResolvedLayer;

typedef struct TitanThemeV4RareEvent {
    uint8_t layer_declaration_order;
    uint8_t reserved[3];
    uint32_t interval_ms;
    uint32_t duration_ms;
    uint32_t phase_ms;
} TitanThemeV4RareEvent;

typedef struct TitanThemeV4Telemetry {
    uint32_t active_layers;
    uint32_t visible_layers;
    uint32_t animated_layers;
    uint32_t static_layers;
    uint32_t draw_count;
    uint32_t skipped_layers;
    uint32_t skipped_invisible;
    uint32_t skipped_offscreen;
    uint32_t transform_updates;
    uint32_t transform_skips;
    uint32_t declared_ee_ram;
    uint32_t vram_budget;
    uint32_t ee_ram_budget;
    uint32_t reserved_headroom;
    uint32_t degradation_state;
    uint32_t rare_events_active;
    uint32_t texture_uploads;
    uint32_t upload_bytes;
    uint32_t theme_vram;
    uint32_t theme_vram_peak;
    uint32_t clut_usage;
    uint32_t pikavr_hits;
    uint32_t pikavr_misses;
    uint32_t evictions;
    uint32_t animation_update_us;
    uint32_t theme_render_us;
} TitanThemeV4Telemetry;

typedef struct TitanThemeV4Runtime {
    TitanThemeV4Layer layers[TITAN_THEME_V4_MAX_LAYERS];
    TitanThemeV4ResolvedLayer resolved[TITAN_THEME_V4_MAX_LAYERS];
    uint32_t layer_count;
    uint32_t elapsed_ms;
    uint32_t vram_budget;
    uint32_t ee_ram_budget;
    uint32_t reserved_headroom;
    TitanThemeV4RareEvent events[TITAN_THEME_V4_MAX_EVENTS];
    uint32_t event_count;
    TitanThemeV4Degradation degradation;
    TitanThemeV4Telemetry telemetry;
} TitanThemeV4Runtime;


typedef struct TitanThemeV4SceneLayer {
    TitanThemeV4Layer compiled;
    float x;
    float y;
    float scale_x;
    float scale_y;
    float rotation;
    uint32_t declaration_order;
    int16_t z;
} TitanThemeV4SceneLayer;

typedef struct TitanThemeV4Scene {
    TitanThemeV4SceneLayer layers[TITAN_THEME_V4_MAX_LAYERS];
    uint32_t layer_count;
    uint32_t declared_bytes;
    uint32_t vram_budget;
    uint32_t ee_ram_budget;
    uint32_t reserved_headroom;
    TitanThemeV4RareEvent events[TITAN_THEME_V4_MAX_EVENTS];
    uint32_t event_count;
} TitanThemeV4Scene;

typedef struct TitanThemeV4EvaluatedLayer {
    float x;
    float y;
    float scale_x;
    float scale_y;
    float rotation;
    unsigned char alpha;
    uint8_t visible;
    uint8_t static_fast_path;
    uint8_t reserved;
} TitanThemeV4EvaluatedLayer;

typedef struct TitanThemeV4FrameInput {
    uint32_t delta_ms;
    uint8_t context;
    uint8_t pressure_percent;
    uint16_t reserved;
    float navigation_x;
    float navigation_y;
    float navigation;
    uint32_t elapsed_ms;
    float top_transition;
    float idle_drift;
    float viewport_width;
    float viewport_height;
} TitanThemeV4FrameInput;

int TitanThemeV4_ParseCompiled(const char *text, TitanThemeV4Scene *scene);
int TitanThemeV4_EvaluateLayer(const TitanThemeV4SceneLayer *layer, const TitanThemeV4FrameInput *input, TitanThemeV4EvaluatedLayer *out);
void TitanThemeV4_EvaluateScene(const TitanThemeV4Scene *scene, const TitanThemeV4FrameInput *input, TitanThemeV4Telemetry *telemetry);
void TitanThemeV4_SetDegradation(TitanThemeV4Degradation degradation);

void TitanThemeV4_Init(TitanThemeV4Runtime *runtime);
int TitanThemeV4_ParseManifest(TitanThemeV4Runtime *runtime, const char *text);
void TitanThemeV4_Update(TitanThemeV4Runtime *runtime, const TitanThemeV4FrameInput *input);
const TitanThemeV4ResolvedLayer *TitanThemeV4_GetResolved(const TitanThemeV4Runtime *runtime, uint32_t index);
void TitanThemeV4_GetTelemetry(const TitanThemeV4Runtime *runtime, TitanThemeV4Telemetry *out);

#endif
