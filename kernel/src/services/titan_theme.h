#ifndef PROJECT_TITAN_THEME_H
#define PROJECT_TITAN_THEME_H

#include <stdint.h>

#define TITAN_THEME_CAPACITY 16u
#define TITAN_THEME_ID_CAPACITY 16u
#define TITAN_THEME_NAME_CAPACITY 64u
#define TITAN_THEME_AUTHOR_CAPACITY 48u
#define TITAN_THEME_VERSION_CAPACITY 16u
#define TITAN_THEME_PATH_CAPACITY 256u
#define TITAN_THEME_SHELL_API_VERSION 4u

#define TITAN_THEME_BACKGROUND_DEFAULT 0u
#define TITAN_THEME_BACKGROUND_FLOW3D 1u
/* Ribbon3D supersedes the old head/tail Flow motion while intentionally
 * retaining ABI value 1 so installed themes and persisted IDs keep working. */
#define TITAN_THEME_BACKGROUND_RIBBON3D TITAN_THEME_BACKGROUND_FLOW3D
#define TITAN_THEME_BACKGROUND_SCENE_V2 2u
#define TITAN_THEME_BACKGROUND_SCENE_V4 3u
#define TITAN_THEME_MAX_PRIMARY_WAVES 3u
#define TITAN_THEME_ASSET_PATH_CAPACITY 96u
#define TITAN_THEME_SCENE_TRANSITION_MAX 6u
#define TITAN_THEME_SCENE_IDLE_MAX 8u
#define TITAN_THEME_SCENE_IDLE_TOTAL_MAX 8u
#define TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN 40u
#define TITAN_THEME_SCENE_IDLE_FRAME_MS_MAX 2000u

#define TITAN_THEME_SCENE_IDLE_LOOP 0u
#define TITAN_THEME_SCENE_IDLE_PINGPONG 1u

#define TITAN_THEME_ICON_DECORATOR_NONE 0u
#define TITAN_THEME_ICON_DECORATOR_RING 1u
#define TITAN_THEME_ICON_DECORATOR_CORNERS 2u

typedef enum TitanThemeContext
{
    TITAN_THEME_CONTEXT_HOME = 0,
    TITAN_THEME_CONTEXT_SYSTEM = 1
} TitanThemeContext;

typedef struct TitanThemeContextPolicy
{
    int full_scene;
    int system_waves;
    int functional_ui;
} TitanThemeContextPolicy;

void TitanTheme_GetContextPolicy(TitanThemeContext context, TitanThemeContextPolicy *policy);

typedef struct TitanThemeColor
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} TitanThemeColor;

typedef struct TitanThemeShellStyle
{
    TitanThemeColor accent;
    float home_selected_scale;
    float home_tile_spacing;
    float title_offset_y;
    float header_alpha;
    float help_alpha;
    float chrome_alpha;
    float horizontal_motion_scale;
    float vertical_motion_scale;
} TitanThemeShellStyle;

typedef struct TitanThemePrimaryWave
{
    /* Logical-canvas Y positions where this primary ribbon enters at the left
     * edge and leaves at the right edge. Different values produce the diagonal
     * / sweeping geometry used by Flow-style themes instead of forcing every
     * theme through a horizontal center line. */
    float start_y;
    float end_y;
    /* Cubic-Bezier vertical controls. They let a theme describe the broad
     * crossing/S-shaped ribbons used by the PS4 Flow background instead of
     * reducing every primary wave to a straight left-to-right baseline. */
    float control1_y;
    float control2_y;
    /* Slow whole-ribbon vertical breathing and independent sheet thickness. */
    float drift_y;
    float thickness_scale;
    float amplitude_scale;
    float alpha_scale;
    unsigned int phase_offset;

    /* Optional PS4-Flow-style long-cycle sweep.  When enabled, this ribbon is
     * treated as one composite sheet that orbits the logical canvas and slowly
     * rotates through vertical/diagonal/horizontal poses.  This lets the same
     * continuous ribbon enter and leave different screen edges instead of
     * requiring several independent waves to cross each other. */
    unsigned int sweep_enabled;
    float sweep_speed;       /* LUT phase steps per rendered frame. */
    float sweep_orbit_x;     /* logical pixels */
    float sweep_orbit_y;     /* logical pixels */
    float sweep_span_scale;  /* local ribbon length before rotation */
    unsigned int sweep_phase;

    /* Flow-style stochastic wander. Unlike sweep/orbit this has no closed
     * trajectory: the renderer chooses deterministic pseudo-random target
     * states and eases between them, so a ribbon can emerge from different
     * screen regions, change direction/curvature, fade and leave the canvas
     * without behaving like an arrow travelling around an ellipse. */
    unsigned int wander_enabled;
    float wander_speed;          /* target-state transitions per frame */
    float wander_extent_x;       /* center range around logical canvas center */
    float wander_extent_y;
    float wander_curve;          /* max Bezier control-point displacement */
    float wander_visibility_min; /* 0..1 dynamic alpha floor */
    unsigned int wander_seed;

    /* Head-driven trailing ribbon. The head follows a smooth Catmull-Rom path
     * through deterministic pseudo-random waypoints. Each body sample reads an
     * older point on that same path, so turns propagate from head to tail like
     * a snake instead of rotating/translating the whole sheet at once. */
    unsigned int snake_enabled;
    float snake_speed;           /* waypoint transitions per rendered frame */
    float snake_extent_x;
    float snake_extent_y;
    float snake_trail_frames;    /* history delay between adjacent spine samples */
    float snake_visibility_min;
    unsigned int snake_seed;

    /* Ribbon reference model (BuildFix18). The ribbon is a broad continuous
     * sheet spanning the viewport. Local cross-section rotation, width
     * breathing and slow travelling folds create the fabric-like motion from
     * the reference video without a head/tail trajectory. */
    unsigned int reference_ribbon_enabled;
    float ribbon_speed;            /* LUT phase steps per rendered frame */
    float ribbon_center_y;         /* logical canvas Y */
    float ribbon_thickness;        /* logical full face-on thickness */
    float ribbon_fold_amplitude;   /* broad centre-line fold amplitude */
    float ribbon_fold_frequency;   /* LUT phase across full ribbon span */
    float ribbon_twist_amplitude;  /* local X displacement at edge-on twist */
    float ribbon_span_scale;       /* 1.0 = logical canvas width */
    float ribbon_drift_x;          /* slow whole-sheet horizontal breathing */
    float ribbon_drift_y;          /* slow whole-sheet vertical breathing */
    unsigned int ribbon_seed;
} TitanThemePrimaryWave;

typedef struct TitanThemeFlowStyle
{
    TitanThemeColor background_top;
    TitanThemeColor background_bottom;
    TitanThemeColor wave_base;
    TitanThemeColor wave_highlight;
    TitanThemeColor particles;
    float speed;
    float frequency;
    float amplitude;
    float noise;
    float fresnel;
    float horizontal_boost;
    float vertical_shift;
    float day_frequency;
    float night_frequency;
    float day_speed;
    float night_speed;

    /* VU1 procedural deformation carrier. These values are theme-owned and
     * only affect the VU1 path; the EE/fallback mesh remains valid when VU1
     * is unavailable. Phase/frequency use Titan's 128-unit full-cycle scale. */
    float vu1_deform_speed;
    float vu1_deform_frequency;
    float vu1_deform_amplitude;

    unsigned int primary_wave_count;
    TitanThemePrimaryWave primary_waves[TITAN_THEME_MAX_PRIMARY_WAVES];
    unsigned char atmosphere_alpha;
} TitanThemeFlowStyle;


typedef struct TitanThemeSceneStyle
{
    /* scene-v2 uses 512x256 RGB565 keyframes.  The HOME and TOP frames are
     * mandatory; optional transition frames are selected from the shell's
     * existing top-menu transition value so animation runs without a second
     * timing/state machine. */
    char home_frame[TITAN_THEME_ASSET_PATH_CAPACITY];
    char top_frame[TITAN_THEME_ASSET_PATH_CAPACITY];
    char transition_frames[TITAN_THEME_SCENE_TRANSITION_MAX][TITAN_THEME_ASSET_PATH_CAPACITY];
    unsigned int transition_frame_count;

    /* Theme Shell API v3 idle animation. HOME/TOP base frames remain the
     * first frame in each loop; these arrays contain optional additional
     * frames. Both states share an eight-frame additional-frame budget so the
     * complete scene cache stays bounded to 4 MiB of EE RAM even when the six
     * transition keyframes are also used. */
    char home_idle_frames[TITAN_THEME_SCENE_IDLE_MAX][TITAN_THEME_ASSET_PATH_CAPACITY];
    char top_idle_frames[TITAN_THEME_SCENE_IDLE_MAX][TITAN_THEME_ASSET_PATH_CAPACITY];
    unsigned int home_idle_frame_count;
    unsigned int top_idle_frame_count;
    unsigned int home_idle_frame_ms;
    unsigned int top_idle_frame_ms;
    unsigned int home_idle_mode;
    unsigned int top_idle_mode;

    unsigned int system_flow_enabled;
    unsigned int home_title_visible;
} TitanThemeSceneStyle;


typedef struct TitanThemeSceneV4Style
{
    char manifest[TITAN_THEME_ASSET_PATH_CAPACITY];
    uint32_t vram_budget;
    uint32_t ee_ram_budget;
    uint32_t reserved_headroom;
} TitanThemeSceneV4Style;

typedef struct TitanThemeAudioStyle
{
    char music[TITAN_THEME_ASSET_PATH_CAPACITY];
    unsigned int music_home_only;
    char notification[TITAN_THEME_ASSET_PATH_CAPACITY];
    char profile_entry[TITAN_THEME_ASSET_PATH_CAPACITY];
    char home_nav[TITAN_THEME_ASSET_PATH_CAPACITY];
    char top_nav[TITAN_THEME_ASSET_PATH_CAPACITY];
    char interface_nav[TITAN_THEME_ASSET_PATH_CAPACITY];
    char top_enter[TITAN_THEME_ASSET_PATH_CAPACITY];
    char shutdown[TITAN_THEME_ASSET_PATH_CAPACITY];
} TitanThemeAudioStyle;

typedef struct TitanThemeIconStyle
{
    unsigned int tint_enabled;
    TitanThemeColor tint;
    unsigned int decorator;
    TitanThemeColor decorator_color;
} TitanThemeIconStyle;

typedef struct TitanThemeInfo
{
    char id[TITAN_THEME_ID_CAPACITY];
    char name[TITAN_THEME_NAME_CAPACITY];
    char author[TITAN_THEME_AUTHOR_CAPACITY];
    char version[TITAN_THEME_VERSION_CAPACITY];
    char path[TITAN_THEME_PATH_CAPACITY];
    unsigned int shell_api;
    unsigned int background_engine;
    int built_in;
    TitanThemeShellStyle shell;
    TitanThemeFlowStyle flow;
    TitanThemeSceneStyle scene;
    TitanThemeSceneV4Style scene_v4;
    TitanThemeAudioStyle audio;
    TitanThemeIconStyle icons;
} TitanThemeInfo;

void TitanTheme_Initialize(void);
unsigned int TitanTheme_Refresh(void);
int TitanTheme_LoadPersistentSetting(void);
unsigned int TitanTheme_GetCount(void);
const TitanThemeInfo *TitanTheme_Get(unsigned int index);
const TitanThemeInfo *TitanTheme_GetActive(void);
unsigned int TitanTheme_GetActiveIndex(void);
const TitanThemeShellStyle *TitanTheme_GetShellStyle(void);
const TitanThemeFlowStyle *TitanTheme_GetFlowStyle(void);
const TitanThemeSceneStyle *TitanTheme_GetSceneStyle(void);
const TitanThemeSceneV4Style *TitanTheme_GetSceneV4Style(void);
const TitanThemeAudioStyle *TitanTheme_GetAudioStyle(void);
const TitanThemeIconStyle *TitanTheme_GetIconStyle(void);
int TitanTheme_BuildAssetPath(const char *relative, char *output, unsigned int capacity);
unsigned int TitanTheme_GetGeneration(void);
int TitanTheme_Apply(unsigned int index);
int TitanTheme_CanDelete(unsigned int index);
int TitanTheme_Delete(unsigned int index, char *error, unsigned int error_capacity);
int TitanTheme_ValidateDirectory(
    const char *root_path,
    char *error,
    unsigned int error_capacity
);
int TitanTheme_ValidateDirectoryForId(
    const char *root_path,
    const char *expected_id,
    char *error,
    unsigned int error_capacity
);

#endif
