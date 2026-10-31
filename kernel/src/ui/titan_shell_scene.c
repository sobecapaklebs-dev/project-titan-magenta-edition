#include "titan_shell_scene.h"

/* P10.6 — Persistent Controller Session / Player Assignment / Multitap */

#include "titan_animation.h"
#include "titan_audio.h"
#include "titan_browser.h"
#include "titan_download_service.h"
#include "titan_store_service.h"
#include "titan_disc_service.h"
#include "titan_disc_engine.h"
#include "titan_applications.h"
#include "titan_app_runtime.h"
#include "titan_app_assets.h"
#include "titan_asset_service.h"
#include "titan_dex.h"
#include "titan_datetime.h"
#include "titan_localization.h"
#include "titan_diagnostics.h"
#include "titan_health_trace.h"
#include "titan_early_input.h"
#include "titan_vram_boot_guard.h"
#include "titan_vlombiscore.h"
#include "titan_boot_recovery.h"
#include "titan_exec_handoff.h"
#include "titan_home_folders.h"
#include "titan_iop_service_manager.h"
#include "titan_ingame_overlay.h"
#include "titan_avatar_picker.h"
#include "titan_avatars.h"
#include "titan_font.h"
#include "titan_icons.h"
#include "titan_notifications.h"
#include "titan_neutrino.h"
#include "titan_game_gsm.h"
#include "titan_game_patches.h"
#include "titan_network.h"
#include "titan_library_scan.h"
#include "titan_library_cache.h"
#include "titan_package_installer.h"
#include "titan_pademu_input_bridge.h"
#include "titan_controller_assignment.h"
#include <titan_pademu_telemetry.h>
#include "titan_profile_storage.h"
#include "titan_storage_io.h"
#include "titan_storage_inventory.h"
#include "titan_storage_scheduler.h"
#include "titan_wireless_accessories.h"
#include "titan_storage_paths.h"
#include "titan_shell_background.h"
#include "titan_shell_catalog.h"
#include "titan_shell_layout.h"
#include "titan_shell_busy.h"
#include "titan_session_service.h"
#include "titan_unified_input.h"
#include "titan_text_input.h"
#include "titan_theme.h"
#include "titan_theme_lifecycle.h"
#include "titan_ui_transition.h"
#include "titan_ui_layout_guard.h"
#include "titan_time.h"
#include "titan_system_clock.h"
#include "titan_system_info.h"
#include "titan_target_memory.h"
#include "titan_system_update.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"
#include "titan_video.h"
#include "titan_version.h"
#include "titan_video_output.h"
#include "titan_ui_assets.h"
#include "generated/titan_ui_power_ps2slim_data.h"
#include "generated/titan_poweroff_irx.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <delaythread.h>
#include <kernel.h>
#include <libpwroff.h>
#include <loadfile.h>

/* Route legacy shell literals through the active language without rewriting
 * every draw site at release time. Exact runtime/game/user strings pass through
 * unchanged. Macro self-reference is intentionally suppressed by the C
 * preprocessor for the replacement invocation, so these still call the real
 * font/notification APIs. */
#define TitanFont_MeasureText(scale, text) \
    TitanFont_MeasureText((scale), TitanLocalization_Compat((text)))
#define TitanFont_DrawTextAlpha(x, y, scale, text, r, g, b, a) \
    TitanFont_DrawTextAlpha((x), (y), (scale), TitanLocalization_Compat((text)), (r), (g), (b), (a))
#define TitanFont_DrawTextShadow(x, y, scale, text, r, g, b, a) \
    TitanFont_DrawTextShadow((x), (y), (scale), TitanLocalization_Compat((text)), (r), (g), (b), (a))
#define TitanNotifications_Push(type, title, message) \
    TitanNotifications_Push((type), TitanLocalization_Compat((title)), TitanLocalization_Compat((message)))
#define TitanNotifications_OpenInformation(title, line1, line2, line3, line4, line5) \
    TitanNotifications_OpenInformation( \
        TitanLocalization_Compat((title)), TitanLocalization_Compat((line1)), \
        TitanLocalization_Compat((line2)), TitanLocalization_Compat((line3)), \
        TitanLocalization_Compat((line4)), TitanLocalization_Compat((line5)))
#define TitanNotifications_OpenConfirmation(title, line1, line2, cancel, confirm, callback, user_data) \
    TitanNotifications_OpenConfirmation( \
        TitanLocalization_Compat((title)), TitanLocalization_Compat((line1)), \
        TitanLocalization_Compat((line2)), TitanLocalization_Compat((cancel)), \
        TitanLocalization_Compat((confirm)), (callback), (user_data))

#define TITAN_SHELL_LAYOUT_CAPACITY 15
#define TITAN_SHELL_BASE_PREFIX_COUNT 1u
#define TITAN_SHELL_STATIC_SUFFIX_COUNT 3u
#define TITAN_CONTEXT_MENU_MAX_ITEMS 8u
#define TITAN_FOLDER_SHELL_SELECTION_COUNT TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT
#define TITAN_APPLICATION_VISIBLE_ROWS 5u
#define TITAN_SETTINGS_ROW_TOP 76.0f
#define TITAN_SETTINGS_ROW_STEP 69.0f
#define TITAN_SETTINGS_SCROLL_TOP 80.0f
#define TITAN_SETTINGS_SCROLL_HEIGHT 332.0f
#define TITAN_VIDEO_TEST_DURATION_FRAMES 360u
#define TITAN_SHELL_TILE_GAP 4.0f
#define TITAN_SHELL_TILE_SMALL_WIDTH 92.0f
#define TITAN_SHELL_TILE_FOCUSED_WIDTH 142.0f
#define TITAN_SHELL_TILE_SMALL_HEIGHT 80.0f
#define TITAN_SHELL_TILE_FOCUSED_HEIGHT 156.0f
#define TITAN_SHELL_TILE_TOP 90.0f
#define TITAN_SHELL_TILE_FOCUS_RISE 0.0f
#define TITAN_SHELL_TILE_FOCUS_ANCHOR_CENTER_X 202.0f
#define TITAN_SHELL_START_BAR_HEIGHT 24.0f
#define TITAN_SHELL_PAGE_TRANSITION_STEP 0.028f
#define TITAN_SHELL_HOME_EXIT_END 0.32f
#define TITAN_SHELL_WAVE_MORPH_START 0.20f
#define TITAN_SHELL_WAVE_MORPH_END 0.74f
#define TITAN_SHELL_PAGE_APPROACH_START 0.60f
#define TITAN_SHELL_DETAIL_DELAY_FRAMES 55u
#define TITAN_SHELL_FOOTER_DELAY_FRAMES 180u
#define TITAN_SHELL_FOOTER_TOP 424.0f
#define TITAN_SHELL_FOOTER_HEIGHT 24.0f
#define TITAN_SHELL_FOOTER_TEXT_Y 430.0f
#define TITAN_SHELL_SYSTEM_MENU_SHIFT 250.0f
#define TITAN_SHELL_SYSTEM_MENU_Y 270.0f
#define TITAN_SHELL_TOP_MENU_GRID_SHIFT 244.0f
#define TITAN_SHELL_HEADER_USER_X 252.0f
#define TITAN_SHELL_HEADER_USER_Y 26.0f
#define TITAN_SETTINGS_CATEGORY_COUNT 11
#define TITAN_SETTINGS_MAX_ROW_COUNT 17
#define TITAN_SETTINGS_VISIBLE_ROW_COUNT 5
#define TITAN_THEME_GRID_COLUMNS 3
#define TITAN_THEME_GRID_ROWS 2
#define TITAN_THEME_GRID_PAGE_SIZE (TITAN_THEME_GRID_COLUMNS * TITAN_THEME_GRID_ROWS)
#define TITAN_THEME_GRID_CARD_WIDTH 208.0f
#define TITAN_THEME_GRID_CARD_HEIGHT 94.0f
#define TITAN_THEME_GRID_PREVIEW_WIDTH 94.0f
#define TITAN_THEME_GRID_GAP_X 10.0f
#define TITAN_THEME_GRID_GAP_Y 10.0f
#define TITAN_THEME_GRID_ORIGIN_X 92.0f
#define TITAN_THEME_GRID_ORIGIN_Y 104.0f
#define TITAN_DEBUG_SETTINGS_ROW_COUNT 12
#define TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS 4
#define TITAN_PACKAGE_INSTALLER_ROW_TOP 72.0f
#define TITAN_PACKAGE_INSTALLER_ROW_STEP 78.0f
#define TITAN_PACKAGE_INSTALLER_TEXT_WIDTH 470.0f
#define TITAN_SHELL_SYSTEM_MENU_ITEM_STEP 92.0f
#define TITAN_POWER_QUIESCE_MAX_ATTEMPTS 500u
#define TITAN_POWER_QUIESCE_DELAY_US 10000
#define TITAN_STARTUP_BLACK_HOLD_FRAMES 78u
#define TITAN_STARTUP_HEALTH_DURATION_FRAMES 210u
#define TITAN_STARTUP_BACKGROUND_FADE_FRAMES 120u
#define TITAN_STARTUP_TEXT_FADE_FRAMES 30u
#define TITAN_STARTUP_WELCOME_ENTER_FRAMES 24u
#define TITAN_STARTUP_WELCOME_EXIT_FRAMES 20u
#define TITAN_STARTUP_PROFILE_ENTER_FRAMES 24u
#define TITAN_STARTUP_PROFILE_EXIT_FRAMES 30u
#define TITAN_STARTUP_HOME_PAUSE_FRAMES 72u
#define TITAN_STARTUP_HOME_ENTER_FRAMES 108u
#define TITAN_BOOT_PS2LOGO_WIDTH 256u
#define TITAN_BOOT_PS2LOGO_HEIGHT 96u
#define TITAN_BOOT_STAGE_LOGO_DRAW_WIDTH 176.0f
#define TITAN_BOOT_STAGE_LOGO_DRAW_HEIGHT 66.0f
#define TITAN_BOOT_STAGE_LOGO_X ((float)TITAN_UI_WIDTH * 0.5f)
#define TITAN_BOOT_STAGE_LOGO_Y 206.0f
#define TITAN_STARTUP_CORNER_LOGO_X 106.0f
#define TITAN_STARTUP_CORNER_LOGO_Y 46.0f
#define TITAN_STARTUP_CORNER_LOGO_WIDTH 160.0f
#define TITAN_STARTUP_CORNER_LOGO_HEIGHT 60.0f
#define TITAN_GAME_PLACEHOLDER_SIZE 128u
#define TITAN_PROFILE_TILE_SIZE 112.0f
#define TITAN_PROFILE_TILE_GAP 16.0f
/*
 * Orbis-style game launch choreography at the shell's fixed 60 Hz step.
 * Handoff is intentionally delayed until the transition and engine badge
 * have both had time to complete on screen.
 */
#define TITAN_GAME_LAUNCH_TOTAL_FRAMES 204u
#define TITAN_GAME_LAUNCH_LABEL_FADE_START 8u
#define TITAN_GAME_LAUNCH_LABEL_FADE_END 30u
#define TITAN_GAME_LAUNCH_OTHER_FADE_START 12u
#define TITAN_GAME_LAUNCH_OTHER_FADE_END 42u
#define TITAN_GAME_LAUNCH_STAGE_IN_START 28u
#define TITAN_GAME_LAUNCH_STAGE_IN_END 72u
#define TITAN_GAME_LAUNCH_BADGE_IN_FRAME 68u
#define TITAN_GAME_LAUNCH_BADGE_IN_END_FRAME 86u
#define TITAN_GAME_LAUNCH_BADGE_OUT_FRAME 176u
#define TITAN_GAME_LAUNCH_BADGE_OUT_END_FRAME 196u
#define TITAN_GAME_LAUNCH_SELECTED_FADE_START 178u
#define TITAN_GAME_LAUNCH_SELECTED_FADE_END 200u
#define TITAN_GAME_LAUNCH_BLACK_FADE_START 184u
#define TITAN_GAME_LAUNCH_BLACK_FADE_END 202u
#define TITAN_GAME_RETURN_TOTAL_FRAMES 66u
#define TITAN_FOLDER_NAV_PREVIEW_TOP 276.0f
#define TITAN_FOLDER_NAV_ACTIVE_TOP 92.0f
#define TITAN_FOLDER_NAV_PREVIEW_SIZE 62.0f
#define TITAN_FOLDER_NAV_SMALL_WIDTH 76.0f
#define TITAN_FOLDER_NAV_SMALL_HEIGHT 76.0f
#define TITAN_FOLDER_NAV_FOCUSED_WIDTH 142.0f
#define TITAN_FOLDER_NAV_FOCUSED_HEIGHT 156.0f
#define TITAN_FOLDER_NAV_GAP 5.0f
#define TITAN_FOLDER_NAV_ANCHOR_CENTER_X 202.0f
#define TITAN_LIBRARY_VISIBLE_COLUMNS 3
#define TITAN_LIBRARY_VISIBLE_ROWS 4
#define TITAN_LIBRARY_VISIBLE_ITEMS (TITAN_LIBRARY_VISIBLE_COLUMNS * TITAN_LIBRARY_VISIBLE_ROWS)
#define TITAN_LIBRARY_ENTRY_CAPACITY (TITAN_NEUTRINO_GAME_CAPACITY + TITAN_APPLICATION_CAPACITY + TITAN_HOME_FOLDER_CAPACITY)
#define TITAN_LIBRARY_SEARCH_CAPACITY 64u

typedef enum TitanStartupPhase
{
    TITAN_STARTUP_PHASE_INITIAL_BLACK = 0,
    TITAN_STARTUP_PHASE_HEALTH,
    TITAN_STARTUP_PHASE_WELCOME_ENTER,
    TITAN_STARTUP_PHASE_WELCOME_WAIT,
    TITAN_STARTUP_PHASE_WELCOME_EXIT,
    TITAN_STARTUP_PHASE_PROFILE_ENTER,
    TITAN_STARTUP_PHASE_PROFILE_WAIT,
    TITAN_STARTUP_PHASE_PROFILE_EXIT,
    TITAN_STARTUP_PHASE_HOME_PAUSE,
    TITAN_STARTUP_PHASE_HOME_ENTER
} TitanStartupPhase;

typedef enum TitanSettingsCategory
{
    TITAN_SETTINGS_USERS = 0,
    TITAN_SETTINGS_SAVED_DATA,
    TITAN_SETTINGS_CONTENT,
    TITAN_SETTINGS_NETWORK,
    TITAN_SETTINGS_ACCESSORIES,
    TITAN_SETTINGS_STORAGE,
    TITAN_SETTINGS_VIDEO,
    TITAN_SETTINGS_THEMES,
    TITAN_SETTINGS_SYSTEM,
    TITAN_SETTINGS_ABOUT,
    TITAN_SETTINGS_DEBUG
} TitanSettingsCategory;

typedef enum TitanSettingsLeafMode
{
    TITAN_SETTINGS_LEAF_CLOSED = 0,
    TITAN_SETTINGS_LEAF_WIRELESS,
    TITAN_SETTINGS_LEAF_CONTROLLER_ASSIGNMENT,
    TITAN_SETTINGS_LEAF_MULTITAP,
    TITAN_SETTINGS_LEAF_STORAGE_APPLICATIONS,
    TITAN_SETTINGS_LEAF_STORAGE_GAMES,
    TITAN_SETTINGS_LEAF_STORAGE_THEMES
} TitanSettingsLeafMode;

typedef enum TitanWirelessDetailMode
{
    TITAN_WIRELESS_DETAIL_CLOSED = 0,
    TITAN_WIRELESS_DETAIL_SCAN,
    TITAN_WIRELESS_DETAIL_REGISTERED
} TitanWirelessDetailMode;

typedef enum TitanSettingsActionKind
{
    TITAN_SETTINGS_ACTION_NONE = 0,
    TITAN_SETTINGS_ACTION_STORAGE_APPLICATION,
    TITAN_SETTINGS_ACTION_STORAGE_GAME,
    TITAN_SETTINGS_ACTION_STORAGE_THEME,
    TITAN_SETTINGS_ACTION_WIRELESS_DEVICE,
    TITAN_SETTINGS_ACTION_WIRED_DEVICE
} TitanSettingsActionKind;

typedef enum TitanNetworkPageMode
{
    TITAN_NETWORK_PAGE_CLOSED = 0,
    TITAN_NETWORK_PAGE_SETUP_METHOD,
    TITAN_NETWORK_PAGE_WIFI_MODE,
    TITAN_NETWORK_PAGE_WIFI_LIST,
    TITAN_NETWORK_PAGE_LAN_MODE,
    TITAN_NETWORK_PAGE_TEST,
    TITAN_NETWORK_PAGE_STATUS,
    TITAN_NETWORK_PAGE_SERVICES,
    TITAN_NETWORK_PAGE_GAME_DATA,
    TITAN_NETWORK_PAGE_ASSET_WIZARD,
    TITAN_NETWORK_PAGE_ASSET_PROGRESS
} TitanNetworkPageMode;

typedef enum TitanHomeEntryType
{
    TITAN_HOME_ENTRY_SHELL = 0,
    TITAN_HOME_ENTRY_GAME,
    TITAN_HOME_ENTRY_APPLICATION,
    TITAN_HOME_ENTRY_FOLDER,
    TITAN_HOME_ENTRY_DISC
} TitanHomeEntryType;

typedef enum TitanLibraryCategory
{
    TITAN_LIBRARY_CATEGORY_ALL = 0,
    TITAN_LIBRARY_CATEGORY_FOLDERS,
    TITAN_LIBRARY_CATEGORY_GAMES,
    TITAN_LIBRARY_CATEGORY_APPLICATIONS,
    TITAN_LIBRARY_CATEGORY_COUNT
} TitanLibraryCategory;

typedef enum TitanLibraryEntryType
{
    TITAN_LIBRARY_ENTRY_FOLDER = 0,
    TITAN_LIBRARY_ENTRY_GAME,
    TITAN_LIBRARY_ENTRY_APPLICATION
} TitanLibraryEntryType;

typedef struct TitanLibraryEntry
{
    TitanLibraryEntryType type;
    unsigned int index;
    const char *title;
} TitanLibraryEntry;

typedef struct TitanLibraryViewCache
{
    TitanLibraryEntry entries[TITAN_LIBRARY_ENTRY_CAPACITY];
    unsigned int count;
    unsigned int game_generation;
    unsigned int application_generation;
    unsigned int folder_count;
    unsigned int pending_hits;
    int category;
    int valid;
    char query[64];
} TitanLibraryViewCache;

static TitanLibraryViewCache g_library_view_cache;

typedef enum TitanFolderUiMode
{
    TITAN_FOLDER_UI_CLOSED = 0,
    TITAN_FOLDER_UI_CONTEXT,
    TITAN_FOLDER_UI_DESTINATION,
    TITAN_FOLDER_UI_EDITOR,
    TITAN_FOLDER_UI_CONTENT
} TitanFolderUiMode;

typedef enum TitanContextSource
{
    TITAN_CONTEXT_SOURCE_HOME = 0,
    TITAN_CONTEXT_SOURCE_SYSTEM_MENU
} TitanContextSource;

typedef enum TitanContextAction
{
    TITAN_CONTEXT_ACTION_NONE = 0,
    TITAN_CONTEXT_ACTION_OPEN,
    TITAN_CONTEXT_ACTION_LAUNCH_APPLICATION,
    TITAN_CONTEXT_ACTION_UNINSTALL_APPLICATION,
    TITAN_CONTEXT_ACTION_ADD_FOLDER,
    TITAN_CONTEXT_ACTION_MOVE,
    TITAN_CONTEXT_ACTION_INFO,
    TITAN_CONTEXT_ACTION_HIDE,
    TITAN_CONTEXT_ACTION_ENGINE,
    TITAN_CONTEXT_ACTION_CHEATS,
    TITAN_CONTEXT_ACTION_GRAPHICS_ENHANCEMENTS,
    TITAN_CONTEXT_ACTION_RESOLUTION_OPTIONS,
    TITAN_CONTEXT_ACTION_EDIT_FOLDER,
    TITAN_CONTEXT_ACTION_DELETE_FOLDER,
    TITAN_CONTEXT_ACTION_STORAGE_INFO,
    TITAN_CONTEXT_ACTION_REFRESH_DEVICES,
    TITAN_CONTEXT_ACTION_TEST_CONTROLLER,
    TITAN_CONTEXT_ACTION_REASSIGN_CONTROLLER,
    TITAN_CONTEXT_ACTION_CONTROLLER_SETTINGS,
    TITAN_CONTEXT_ACTION_MARK_NOTIFICATIONS_READ,
    TITAN_CONTEXT_ACTION_CLEAR_NOTIFICATIONS,
    TITAN_CONTEXT_ACTION_NOTIFICATION_SETTINGS,
    TITAN_CONTEXT_ACTION_SWITCH_PROFILE,
    TITAN_CONTEXT_ACTION_EDIT_PROFILE,
    TITAN_CONTEXT_ACTION_PROFILE_VMC,
    TITAN_CONTEXT_ACTION_CREATE_VMC,
    TITAN_CONTEXT_ACTION_MANAGE_VMC,
    TITAN_CONTEXT_ACTION_VERIFY_VMC,
    TITAN_CONTEXT_ACTION_RESET_SETTINGS,
    TITAN_CONTEXT_ACTION_EXPORT_SETTINGS,
    TITAN_CONTEXT_ACTION_IMPORT_SETTINGS,
    TITAN_CONTEXT_ACTION_RESTART_TITAN,
    TITAN_CONTEXT_ACTION_RESTART_CONSOLE,
    TITAN_CONTEXT_ACTION_POWER_OFF
} TitanContextAction;

typedef struct TitanContextMenuItem
{
    const char *label;
    TitanContextAction action;
} TitanContextMenuItem;

typedef struct TitanHomeEntry
{
    TitanHomeEntryType type;
    const char *title;
    const char *subtitle;
    const char *status;
    TitanIconId icon;
    TitanShellPage page;
    unsigned int game_index;
    unsigned int application_index;
    unsigned int folder_index;
    unsigned int shell_catalog_index;
    int shell_is_dex;
} TitanHomeEntry;

typedef struct TitanShellSceneState
{
    int initialized;

    int selected_item;
    TitanShellPage page;

    int top_menu_active;
    int top_menu_selected;
    int settings_selected_category;
    int settings_category_open;
    int settings_selected_row;
    int power_selected_row;
    int notifications_selected_row;
    int user_profile_selected_row;
    int saved_data_selected_row;
    int applications_selected_row;
    int input_selected_row;
    int library_selected_category;
    int library_grid_active;
    int library_selected_item;
    char library_search[TITAN_LIBRARY_SEARCH_CAPACITY];
    int package_installer_open;
    int package_installer_theme_only;
    int package_installer_detail_open;
    int package_installer_selected_row;
    int system_update_open;
    int system_update_selected_row;
    int system_update_tracking;
    TitanNetworkPageMode network_page_mode;
    TitanNetworkPageMode asset_wizard_return_mode;
    int asset_wizard_progress_is_clear_cache;
    int network_page_selected_row;
    int network_setup_custom;
    TitanSettingsLeafMode settings_leaf_mode;
    int settings_leaf_open;
    int settings_leaf_selected_row;
    TitanWirelessDetailMode wireless_detail_mode;
    int wireless_detail_open;
    int wireless_detail_selected_row;
    TitanSettingsActionKind settings_action_kind;
    int settings_action_open;
    int settings_action_selected;
    unsigned int settings_action_source_index;
    unsigned int wireless_connect_source_index;
    int package_install_tracking;
    int package_music_suspended;
    int package_scan_deferred;
    TitanFolderUiMode folder_ui_mode;
    TitanFolderUiMode folder_previous_ui_mode;
    int folder_context_selected;
    int resolution_options_open;
    int resolution_options_selected;
    int patch_options_open;
    TitanGamePatchKind patch_options_kind;
    int patch_options_selected;
    int patch_options_scroll;
    int folder_destination_selected;
    int folder_editor_selected;
    int folder_content_cursor;
    int folder_content_side_selected;
    int folder_content_side_active;
    int folder_edit_existing;
    int folder_name_locked;
    TitanContextSource context_source;
    TitanShellPage context_shell_page;
    unsigned int context_shell_catalog_index;
    int shell_move_active;
    int shell_move_from_system_menu;
    unsigned int shell_move_catalog_index;
    int shell_move_original_slot;
    int folder_open;
    unsigned int folder_open_index;
    int folder_open_selected;
    float folder_open_transition;
    float folder_open_selected_position;
    unsigned int folder_origin_game_index;
    int folder_origin_shell_catalog_index;
    unsigned char folder_selected_games[TITAN_NEUTRINO_GAME_CAPACITY];
    unsigned char folder_selected_shell[TITAN_FOLDER_SHELL_SELECTION_COUNT];
    char folder_pending_name[TITAN_HOME_FOLDER_NAME_CAPACITY];
    char pending_uninstall_root[TITAN_APPLICATION_PATH_CAPACITY];
    char pending_uninstall_title_id[TITAN_APPLICATION_TITLE_ID_CAPACITY];
    char pending_uninstall_title[TITAN_APPLICATION_TITLE_CAPACITY];
    float folder_context_transition;
    float resolution_options_transition;
    float patch_options_transition;
    float folder_screen_transition;
    int video_test_active;
    int video_test_previous_progressive;
    int shutdown_screen_active;
    int startup_active;
    TitanStartupPhase startup_phase;
    int startup_profile_selected;
    int profile_creation_from_startup;
    int storage_boot_notice_shown;
    int memory_card_selected_port;
    int memory_card_action_port;
    char pending_profile_name[TITAN_USER_PROFILE_NAME_CAPACITY];
    int game_launch_active;
    unsigned int game_launch_frames;
    unsigned int game_launch_index;
    int game_launch_ul_engine;
    int game_launch_disc;
    int game_launch_application;
    unsigned int game_launch_application_index;
    int game_launch_enhanced;
    unsigned int game_launch_cheat_count;
    char game_launch_cheat_taunt[192];
    int game_return_active;
    unsigned int game_return_frames;

    float selected_position;
    float page_transition;
    float page_transition_target;
    float top_menu_transition;
    float top_menu_selected_position;
    float settings_depth_transition;
    float settings_category_position;
    float settings_row_position;
    float power_row_position;
    float notifications_row_position;
    float user_profile_row_position;
    float saved_data_row_position;
    float applications_row_position;
    float input_row_position;
    float library_category_position;
    float library_item_position;
    float package_installer_row_position;
    float package_installer_transition;
    float system_update_row_position;
    float system_update_transition;
    float network_page_row_position;
    float network_page_transition;
    float settings_leaf_transition;
    float settings_leaf_row_position;
    float wireless_detail_transition;
    float wireless_detail_row_position;
    float settings_action_row_position;
    float shutdown_screen_fade;

    unsigned int local_frame;
    unsigned int selected_hold_frames;
    unsigned int inactivity_frames;
    unsigned int startup_phase_frames;
    unsigned int video_test_frames_remaining;
    unsigned long long last_update_ms;
    unsigned int fixed_step_accumulator;

    float detail_visibility;
    float startup_profile_position;
    float footer_visibility;
} TitanShellSceneState;

static TitanShellSceneState g_scene;
static uint32_t g_welcome_candidate_id;


static void TitanShellScene_BeginGameLaunch(unsigned int game_index);
static void TitanShellScene_BeginDiscLaunch(void);
static void TitanShellScene_BeginApplicationLaunch(unsigned int application_index);
static float TitanShellScene_GameLaunchProgress(unsigned int start, unsigned int end);
static void TitanShellScene_DrawEngineBadge(unsigned char alpha);
static void TitanShellScene_DrawTitanEnhanced(unsigned char alpha);
static void TitanShellScene_DrawCheatTaunt(unsigned char alpha);
static void TitanShellScene_SetFolderUiMode(TitanFolderUiMode mode);
static unsigned int TitanShellScene_BuildResolutionModes(
    const TitanNeutrinoGame *game,
    TitanPerGameGsmMode modes[4]
);

static void TitanShellScene_PlayNavigationAudio(
    const TitanInputState *input
)
{
    unsigned int directional;

    if (input == 0 || !input->connected)
    {
        return;
    }

    directional = input->pressed &
        (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT |
         TITAN_INPUT_BUTTON_UP | TITAN_INPUT_BUTTON_DOWN);
    if (directional == 0u)
    {
        return;
    }

    if (g_scene.startup_active)
    {
        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_WAIT)
        {
            (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_HOME_GRID_NAV);
        }
        return;
    }

    /* Tile moving reports audio only after the layout service confirms that
       an item actually changed slot. This avoids a second generic trigger. */
    if (g_scene.shell_move_active)
    {
        return;
    }

    if (g_scene.top_menu_active)
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_TOP_MENU_NAV);
        return;
    }

    if (g_scene.folder_open)
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_HOME_GRID_NAV);
        return;
    }

    if (g_scene.folder_ui_mode != TITAN_FOLDER_UI_CLOSED ||
        g_scene.package_installer_open ||
        g_scene.network_page_mode != TITAN_NETWORK_PAGE_CLOSED ||
        g_scene.video_test_active ||
        g_scene.page_transition_target > 0.5f)
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_INTERFACE_MENU_NAV);
    }
}

static void TitanShellScene_CopyEllipsizedText(
    char *destination,
    unsigned int capacity,
    const char *source,
    float scale,
    float max_width
);

static void TitanShellScene_CopyFittedTextPrefix(
    char *destination,
    unsigned int capacity,
    const char *source,
    float scale,
    float max_width,
    unsigned int *consumed_bytes
);

static void TitanShellScene_DrawSettingsListRow(
    TitanIconId icon,
    const char *label,
    const char *line1,
    const char *line2,
    float y,
    float offset_x,
    unsigned char alpha,
    float focus
);

static void TitanShellScene_DrawSettingsScrollbar(
    int row_count,
    int selected_row,
    float offset_x,
    unsigned char alpha
);

static int TitanShellScene_CanSelectGameForFolder(unsigned int game_index);

static unsigned int TitanShellScene_GetHomeDexCount(void)
{
    return TitanDex_IsEnabled()
        ? TitanShellCatalog_GetDexCount()
        : 0u;
}

static int TitanShellScene_HomeShellCanFolder(unsigned int catalog_index)
{
    const TitanShellItem *item;

    item = TitanShellCatalog_Get(catalog_index);
    if (item == 0)
    {
        return 0;
    }

    return
        item->page == TITAN_SHELL_PAGE_APPLICATIONS ||
        item->page == TITAN_SHELL_PAGE_INPUT;
}

static int TitanShellScene_HomeShellIsGrouped(unsigned int catalog_index)
{
    const TitanShellItem *item;

    if (!TitanShellScene_HomeShellCanFolder(catalog_index))
    {
        return 0;
    }

    item = TitanShellCatalog_Get(catalog_index);
    return
        item != 0 &&
        TitanHomeFolders_FindForItemKey(
            TitanHomeFolders_MakeShellKey((unsigned int)item->page)
        ) >= 0;
}

static int TitanShellScene_HomeShellSlotVisible(unsigned int slot)
{
    unsigned int catalog_index;

    catalog_index = TitanShellLayout_GetHomeCatalogIndex(slot);
    if (catalog_index >= TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        return 0;
    }

    return
        !TitanShellLayout_IsHomeHidden(catalog_index) &&
        !TitanShellScene_HomeShellIsGrouped(catalog_index);
}

static unsigned int TitanShellScene_CountVisibleHomeShellRange(
    unsigned int first_slot,
    unsigned int slot_count
)
{
    unsigned int slot;
    unsigned int end_slot;
    unsigned int count;

    end_slot = first_slot + slot_count;
    if (end_slot > TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        end_slot = TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;
    }

    count = 0u;
    for (slot = first_slot; slot < end_slot; slot++)
    {
        if (TitanShellScene_HomeShellSlotVisible(slot))
        {
            count++;
        }
    }
    return count;
}

static const TitanShellItem *TitanShellScene_GetVisibleHomeShell(
    unsigned int first_slot,
    unsigned int slot_count,
    unsigned int visible_index,
    unsigned int *catalog_index
)
{
    unsigned int slot;
    unsigned int end_slot;
    unsigned int current;

    end_slot = first_slot + slot_count;
    if (end_slot > TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        end_slot = TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;
    }

    current = 0u;
    for (slot = first_slot; slot < end_slot; slot++)
    {
        unsigned int mapped;

        if (!TitanShellScene_HomeShellSlotVisible(slot))
        {
            continue;
        }

        if (current == visible_index)
        {
            mapped = TitanShellLayout_GetHomeCatalogIndex(slot);
            if (catalog_index != 0)
            {
                *catalog_index = mapped;
            }
            return TitanShellCatalog_Get(mapped);
        }
        current++;
    }
    return 0;
}

static unsigned int TitanShellScene_GetHomeDynamicCapacity(void)
{
    unsigned int fixed_count;

    fixed_count =
        TitanShellScene_GetHomeDexCount() +
        TitanShellScene_CountVisibleHomeShellRange(
            0u,
            TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT
        );

    return fixed_count < TITAN_SHELL_LAYOUT_CAPACITY
        ? TITAN_SHELL_LAYOUT_CAPACITY - fixed_count
        : 0u;
}

static unsigned int TitanShellScene_GetVisibleFolderCount(void)
{
    unsigned int folder_count;
    unsigned int capacity;
    unsigned int application_count;

    folder_count = TitanHomeFolders_GetCount();
    capacity = TitanShellScene_GetHomeDynamicCapacity();
    application_count = TitanApplications_GetHomeTileCount();
    if (application_count >= capacity)
    {
        return 0u;
    }
    capacity -= application_count;
    return folder_count < capacity ? folder_count : capacity;
}

static int TitanShellScene_GameCanUseLegacyFolderKey(
    unsigned int game_index,
    const TitanNeutrinoGame *game
)
{
    TitanHomeGameKey legacy_key;
    unsigned int index;

    if (game == 0 || game->format != TITAN_NEUTRINO_GAME_USBEXTREME)
    {
        return 0;
    }

    legacy_key = TitanHomeFolders_MakeGameKey(game->path);
    for (index = 0u; index < game_index; index++)
    {
        const TitanNeutrinoGame *candidate;

        candidate = TitanNeutrino_GetGame(index);
        if (
            candidate != 0 &&
            candidate->format == TITAN_NEUTRINO_GAME_USBEXTREME &&
            TitanHomeFolders_MakeGameKey(candidate->path) == legacy_key
        )
        {
            return 0;
        }
    }
    return 1;
}

static int TitanShellScene_FindFolderForGame(
    unsigned int game_index,
    const TitanNeutrinoGame *game
)
{
    int owner;

    if (game == 0)
    {
        return -1;
    }

    owner = TitanHomeFolders_FindForItemKey(
        TitanHomeFolders_MakeCatalogGameKey(game)
    );
    if (owner >= 0)
    {
        return owner;
    }

    /* RC13 and earlier stored the ul.cfg path itself. All UL records shared
     * that key, so only the first catalog entry may inherit it. This keeps an
     * existing one-game folder usable without hiding every other UL game. */
    return TitanShellScene_GameCanUseLegacyFolderKey(game_index, game)
        ? TitanHomeFolders_FindForGamePath(game->path)
        : -1;
}

static int TitanShellScene_FolderContainsGame(
    unsigned int folder_index,
    unsigned int game_index,
    const TitanNeutrinoGame *game
)
{
    if (game == 0)
    {
        return 0;
    }

    if (TitanHomeFolders_ContainsItemKey(
        folder_index,
        TitanHomeFolders_MakeCatalogGameKey(game)
    ))
    {
        return 1;
    }

    return
        TitanShellScene_GameCanUseLegacyFolderKey(game_index, game) &&
        TitanHomeFolders_ContainsGamePath(folder_index, game->path);
}

static int TitanShellScene_GameIsGrouped(unsigned int game_index)
{
    const TitanNeutrinoGame *game;

    game = TitanNeutrino_GetGame(game_index);
    return TitanShellScene_FindFolderForGame(game_index, game) >= 0;
}

static unsigned int TitanShellScene_GetUngroupedGameCount(void)
{
    unsigned int game_count;
    unsigned int index;
    unsigned int count;

    game_count = TitanNeutrino_GetGameCount();
    count = 0u;

    for (index = 0u; index < game_count; index++)
    {
        if (!TitanShellScene_GameIsGrouped(index))
        {
            count++;
        }
    }

    return count;
}

static unsigned int TitanShellScene_GetHomeDiscCount(void)
{
    unsigned int capacity = TitanShellScene_GetHomeDynamicCapacity();
    unsigned int used = TitanShellScene_GetVisibleFolderCount() + TitanApplications_GetHomeTileCount();
    return TitanDiscService_IsReady() && used < capacity ? 1u : 0u;
}

static unsigned int TitanShellScene_GetHomeGameCount(void)
{
    unsigned int game_count;
    unsigned int capacity;
    unsigned int folder_count;
    unsigned int application_count;

    game_count = TitanShellScene_GetUngroupedGameCount();
    capacity = TitanShellScene_GetHomeDynamicCapacity();
    folder_count = TitanShellScene_GetVisibleFolderCount();

    if (folder_count >= capacity)
    {
        return 0u;
    }

    capacity -= folder_count;
    application_count = TitanApplications_GetHomeTileCount();
    if (application_count >= capacity)
    {
        return 0u;
    }
    capacity -= application_count;
    if (TitanShellScene_GetHomeDiscCount() != 0u)
    {
        if (capacity == 0u) return 0u;
        capacity--;
    }
    return game_count < capacity ? game_count : capacity;
}

static unsigned int TitanShellScene_GetHomeApplicationCount(void)
{
    unsigned int capacity;
    unsigned int folder_count;
    unsigned int application_count;

    capacity = TitanShellScene_GetHomeDynamicCapacity();
    folder_count = TitanShellScene_GetVisibleFolderCount();
    if (folder_count >= capacity)
    {
        return 0u;
    }

    capacity -= folder_count;
    application_count = TitanApplications_GetHomeTileCount();
    return application_count < capacity ? application_count : capacity;
}

static unsigned int TitanShellScene_GetHomeEntryCount(void)
{
    return
        TitanShellScene_GetHomeDexCount() +
        TitanShellScene_CountVisibleHomeShellRange(0u, TITAN_SHELL_BASE_PREFIX_COUNT) +
        TitanShellScene_GetVisibleFolderCount() +
        TitanShellScene_GetHomeApplicationCount() +
        TitanShellScene_GetHomeDiscCount() +
        TitanShellScene_GetHomeGameCount() +
        TitanShellScene_CountVisibleHomeShellRange(
            TITAN_SHELL_BASE_PREFIX_COUNT,
            TITAN_SHELL_STATIC_SUFFIX_COUNT
        );
}

static int TitanShellScene_GetUngroupedGameByVisibleIndex(
    unsigned int visible_index,
    unsigned int *game_index
)
{
    unsigned int count;
    unsigned int index;
    unsigned int game_count;

    count = 0u;
    game_count = TitanNeutrino_GetGameCount();

    for (index = 0u; index < game_count; index++)
    {
        if (TitanShellScene_GameIsGrouped(index))
        {
            continue;
        }

        if (count == visible_index)
        {
            if (game_index != 0)
            {
                *game_index = index;
            }
            return 1;
        }
        count++;
    }

    return 0;
}

static int TitanShellScene_GetHomeEntry(
    unsigned int index,
    TitanHomeEntry *entry
)
{
    unsigned int dex_count;
    unsigned int prefix_count;
    unsigned int suffix_count;
    unsigned int folder_count;
    unsigned int application_count;
    unsigned int disc_count;
    unsigned int game_count;
    unsigned int logical_index;
    const TitanShellItem *shell_item;

    if (entry == 0 || index >= TitanShellScene_GetHomeEntryCount())
    {
        return 0;
    }

    dex_count = TitanShellScene_GetHomeDexCount();
    prefix_count = TitanShellScene_CountVisibleHomeShellRange(
        0u,
        TITAN_SHELL_BASE_PREFIX_COUNT
    );
    suffix_count = TitanShellScene_CountVisibleHomeShellRange(
        TITAN_SHELL_BASE_PREFIX_COUNT,
        TITAN_SHELL_STATIC_SUFFIX_COUNT
    );
    folder_count = TitanShellScene_GetVisibleFolderCount();
    application_count = TitanShellScene_GetHomeApplicationCount();
    disc_count = TitanShellScene_GetHomeDiscCount();
    game_count = TitanShellScene_GetHomeGameCount();
    shell_item = 0;
    memset(entry, 0, sizeof(*entry));
    entry->shell_catalog_index = TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;

    if (index < dex_count)
    {
        shell_item = TitanShellCatalog_GetDex(index);
        entry->shell_is_dex = 1;
        entry->shell_catalog_index = index;
    }
    else
    {
        logical_index = index - dex_count;

        if (logical_index < prefix_count)
        {
            shell_item = TitanShellScene_GetVisibleHomeShell(
                0u,
                TITAN_SHELL_BASE_PREFIX_COUNT,
                logical_index,
                &entry->shell_catalog_index
            );
        }
        else if (logical_index < prefix_count + folder_count)
        {
            const TitanHomeFolder *folder;
            char *subtitle_buffer;
            static char folder_subtitles[TITAN_HOME_FOLDER_CAPACITY][32];
            unsigned int folder_index;

            folder_index = logical_index - prefix_count;
            folder = TitanHomeFolders_Get(folder_index);
            if (folder == 0)
            {
                return 0;
            }

            subtitle_buffer = folder_subtitles[folder_index];
            snprintf(
                subtitle_buffer,
                32u,
                "%u %s",
                folder->item_count,
                folder->item_count == 1u ? TitanLocalization_Compat("item") : TitanLocalization_Compat("items")
            );

            entry->type = TITAN_HOME_ENTRY_FOLDER;
            entry->title = folder->name;
            entry->subtitle = subtitle_buffer;
            entry->status = TitanLocalization_Get(TITAN_STR_FOLDER_OPEN);
            entry->icon = TITAN_ICON_APPLICATIONS;
            entry->page = TITAN_SHELL_PAGE_HOME;
            entry->folder_index = folder_index;
            return 1;
        }
        else if (logical_index < prefix_count + folder_count + application_count)
        {
            const TitanApplication *application;
            unsigned int visible_application_index;

            visible_application_index =
                logical_index - prefix_count - folder_count;
            application = TitanApplications_GetHomeTile(
                visible_application_index,
                &entry->application_index
            );
            if (application == 0)
            {
                return 0;
            }

            entry->type = TITAN_HOME_ENTRY_APPLICATION;
            entry->title = application->title;
            entry->subtitle = application->description[0] != '\0'
                ? application->description
                : application->path;
            entry->status = application->type == TITAN_APPLICATION_SYSTEM_BROWSER
                ? TitanLocalization_Get(TITAN_STR_SYSTEM_APP_OPEN)
                : (application->type == TITAN_APPLICATION_TITAN_NATIVE
                    ? TitanLocalization_Get(TITAN_STR_TITAN_APP_START)
                    : (application->type == TITAN_APPLICATION_TITAN_GAME
                        ? TitanLocalization_Get(TITAN_STR_TITAN_GAME_START)
                        : TitanLocalization_Get(TITAN_STR_ELF_APP_START)));
            entry->icon = TITAN_ICON_APPLICATIONS;
            entry->page = TITAN_SHELL_PAGE_HOME;
            return 1;
        }
        else if (
            logical_index < prefix_count + folder_count + application_count + disc_count
        )
        {
            const TitanDiscInfo *disc = TitanDiscService_GetInfo();
            static char disc_subtitle[64];
            if (disc == 0 || disc->state != TITAN_DISC_READY) return 0;
            snprintf(disc_subtitle, sizeof(disc_subtitle), "%s / %s",
                disc->title_id[0] != '\0' ? disc->title_id : "PS2",
                disc->media_type == TITAN_DISC_MEDIA_PS2_DVD ? "DVD" : "CD");
            entry->type = TITAN_HOME_ENTRY_DISC;
            entry->title = disc->title[0] != '\0' ? disc->title : "PlayStation 2 Disc";
            entry->subtitle = disc_subtitle;
            entry->status = TitanLocalization_Get(TITAN_STR_DISC_START);
            entry->icon = TITAN_ICON_APPLICATIONS;
            entry->page = TITAN_SHELL_PAGE_HOME;
            return 1;
        }
        else if (
            logical_index <
                prefix_count + folder_count + application_count + disc_count + game_count
        )
        {
            const TitanNeutrinoGame *game;
            unsigned int visible_game_index;

            visible_game_index =
                logical_index - prefix_count - folder_count - application_count - disc_count;

            if (!TitanShellScene_GetUngroupedGameByVisibleIndex(
                visible_game_index,
                &entry->game_index
            ))
            {
                return 0;
            }

            game = TitanNeutrino_GetGame(entry->game_index);
            if (game == 0)
            {
                return 0;
            }

            entry->type = TITAN_HOME_ENTRY_GAME;
            entry->title = game->title;
            entry->subtitle = game->startup[0] != '\0'
                ? game->startup
                : TitanLocalization_Get(TITAN_STR_PS2_GAME);
            entry->status = game->format == TITAN_NEUTRINO_GAME_USBEXTREME
                ? TitanLocalization_Get(TITAN_STR_UL_START)
                : TitanLocalization_Get(TITAN_STR_NEUTRINO_START);
            entry->icon = TITAN_ICON_APPLICATIONS;
            entry->page = TITAN_SHELL_PAGE_HOME;
            return 1;
        }
        else if (
            logical_index <
                prefix_count + folder_count + application_count +
                disc_count + game_count + suffix_count
        )
        {
            unsigned int suffix_index;

            suffix_index =
                logical_index - prefix_count - folder_count -
                application_count - disc_count - game_count;
            shell_item = TitanShellScene_GetVisibleHomeShell(
                TITAN_SHELL_BASE_PREFIX_COUNT,
                TITAN_SHELL_STATIC_SUFFIX_COUNT,
                suffix_index,
                &entry->shell_catalog_index
            );
        }
    }

    if (shell_item == 0)
    {
        return 0;
    }

    entry->type = TITAN_HOME_ENTRY_SHELL;
    entry->title = shell_item->title;
    entry->subtitle = shell_item->subtitle;
    entry->status = shell_item->status;
    entry->icon = shell_item->icon;
    entry->page = shell_item->page;
    entry->game_index = 0u;
    entry->folder_index = 0u;
    return 1;
}

static void TitanShellScene_ClampHomeSelection(void)
{
    unsigned int count;

    count = TitanShellScene_GetHomeEntryCount();
    if (count == 0u)
    {
        g_scene.selected_item = 0;
        g_scene.selected_position = 0.0f;
        return;
    }

    if (g_scene.selected_item < 0)
    {
        g_scene.selected_item = 0;
    }
    else if ((unsigned int)g_scene.selected_item >= count)
    {
        g_scene.selected_item = (int)count - 1;
    }
}

void TitanShellScene_ApplyDexLayout(void)
{
    g_scene.selected_item = (int)TitanShellScene_GetHomeDexCount();
    g_scene.selected_position = (float)g_scene.selected_item;
    TitanShellScene_ClampHomeSelection();
}

static void TitanShellScene_ToggleDexMode(void)
{
    int requested;

    requested = !TitanDex_IsEnabled();
    if (TitanDex_SetEnabled(requested))
    {
        /* Debug Settings is a DEX-only surface. If DEX is disabled while the
         * user is inside that page, leave it immediately so the hidden
         * category can never remain reachable through stale UI state. */
        if (!requested &&
            g_scene.settings_selected_category == TITAN_SETTINGS_DEBUG)
        {
            g_scene.settings_selected_category = TITAN_SETTINGS_ABOUT;
            g_scene.settings_category_position =
                (float)TITAN_SETTINGS_ABOUT;
            g_scene.settings_category_open = 0;
            g_scene.settings_selected_row = 0;
            g_scene.settings_row_position = 0.0f;
        }

        TitanShellScene_ApplyDexLayout();
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "DEX Mode",
            requested
                ? "Development features enabled"
                : "Development features disabled"
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "DEX Mode",
            "Unable to save the system setting"
        );
    }
}

static void TitanShellScene_LaunchApplication(unsigned int application_index)
{
    const TitanApplication *application;
    TitanApplicationResult launch_result;

    application = TitanApplications_Get(application_index);
    if (application == 0)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            TitanLocalization_Compat("Failed to start application"),
            TitanApplications_GetResultName(TITAN_APPLICATION_INVALID_INDEX)
        );
        return;
    }

    if (TitanApplications_IsSystemBrowser(application_index))
    {
        TitanBrowser_GoHome();
        g_scene.page = TITAN_SHELL_PAGE_BROWSER;
        g_scene.page_transition_target = 1.0f;
        g_scene.selected_hold_frames = 0u;
        return;
    }

    if (application->type == TITAN_APPLICATION_TITAN_NATIVE ||
        application->type == TITAN_APPLICATION_TITAN_GAME)
    {
        /* Both Titan Native and Titan Game stay inside the shell process and
         * launch asynchronously through TitanAppRuntime. Never route either
         * type through the external ELF/audio handoff path: Launch() returning
         * TITAN_APPLICATION_OK means the in-process launch was accepted. */
        launch_result = TitanApplications_Launch(application_index);
        if (launch_result == TITAN_APPLICATION_OK)
            return;
    }
    else
    {
        TitanShellBusy_Begin();
        TitanShellBusy_Pulse();
        TitanAudio_PrepareHandoff();
        launch_result = TitanApplications_Launch(application_index);
        TitanAudio_CancelHandoff();
        TitanShellBusy_End();
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        TitanLocalization_Compat("Failed to start application"),
        launch_result == TITAN_APPLICATION_INVALID_TMOD
            ? TitanAppRuntime_GetLastError()
            : TitanApplications_GetResultName(launch_result)
    );
}

static int TitanShellScene_FindHomeDisplayIndexForShell(
    unsigned int catalog_index
)
{
    unsigned int index;
    unsigned int count;

    count = TitanShellScene_GetHomeEntryCount();
    for (index = 0u; index < count; index++)
    {
        TitanHomeEntry entry;

        if (
            TitanShellScene_GetHomeEntry(index, &entry) &&
            entry.type == TITAN_HOME_ENTRY_SHELL &&
            !entry.shell_is_dex &&
            entry.shell_catalog_index == catalog_index
        )
        {
            return (int)index;
        }
    }
    return -1;
}

static void TitanShellScene_OpenShellPage(TitanShellPage page)
{
    g_scene.page = page;

    if (g_scene.page == TITAN_SHELL_PAGE_APP_HOME)
    {
        g_scene.page = TITAN_SHELL_PAGE_APPLICATIONS;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ACTIVITY,
            "APP_HOME",
            "Abrindo o iniciador de aplicativos do sistema."
        );
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_FTP_HOME)
    {
        g_scene.page = TITAN_SHELL_PAGE_NOTIFICATIONS;
        g_scene.notifications_selected_row = 2;
        g_scene.notifications_row_position = 2.0f;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ACTIVITY,
            "FTP_HOME",
            "Opening activity. FTP server is not included in this build."
        );
    }

    if (g_scene.page == TITAN_SHELL_PAGE_LIBRARY)
    {
        g_scene.library_selected_category = TITAN_LIBRARY_CATEGORY_ALL;
        g_scene.library_grid_active = 0;
        g_scene.library_selected_item = 0;
        g_scene.library_category_position = 0.0f;
        g_scene.library_item_position = 0.0f;
        g_scene.library_search[0] = '\0';
        if (
            TitanNeutrino_GetGameCount() == 0u &&
            TitanApplications_GetCount() == 0u &&
            !TitanStorageScheduler_IsLibraryScanBusy()
        )
        {
            (void)TitanStorageScheduler_RequestLibraryScan(0);
        }
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_APPLICATIONS)
    {
        g_scene.applications_selected_row = 0;
    g_scene.input_selected_row = 0;
    g_scene.input_row_position = 0.0f;
        g_scene.applications_row_position = 0.0f;
        if (
            TitanApplications_GetCount() == 0u &&
            !TitanStorageScheduler_IsLibraryScanBusy()
        )
        {
            (void)TitanStorageScheduler_RequestLibraryScan(0);
        }
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_SETTINGS)
    {
        g_scene.settings_category_open = 0;
        g_scene.settings_selected_row = 0;
        g_scene.settings_depth_transition = 0.0f;
        g_scene.package_installer_open = 0;
        g_scene.package_installer_detail_open = 0;
        g_scene.package_installer_selected_row = 0;
        g_scene.package_installer_row_position = 0.0f;
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_POWER)
    {
        g_scene.power_selected_row = 0;
        g_scene.power_row_position = 0.0f;
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_NOTIFICATIONS)
    {
        if (page != TITAN_SHELL_PAGE_FTP_HOME)
        {
            g_scene.notifications_selected_row = 0;
            g_scene.notifications_row_position = 0.0f;
        }
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_USER_PROFILE)
    {
        g_scene.user_profile_selected_row = 0;
        g_scene.user_profile_row_position = 0.0f;
    }
    else if (g_scene.page == TITAN_SHELL_PAGE_SAVED_DATA)
    {
        g_scene.saved_data_selected_row = 0;
        g_scene.saved_data_row_position = 0.0f;
    }

    g_scene.page_transition_target = 1.0f;
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;
}

static int TitanShellScene_IsFolderScreenMode(TitanFolderUiMode mode)
{
    return
        mode == TITAN_FOLDER_UI_DESTINATION ||
        mode == TITAN_FOLDER_UI_EDITOR ||
        mode == TITAN_FOLDER_UI_CONTENT;
}

static void TitanShellScene_SetFolderUiMode(TitanFolderUiMode mode)
{
    if (g_scene.folder_ui_mode == mode)
    {
        return;
    }

    g_scene.folder_previous_ui_mode = g_scene.folder_ui_mode;
    g_scene.folder_ui_mode = mode;
    g_scene.folder_screen_transition = 0.0f;

    if (mode == TITAN_FOLDER_UI_CONTEXT)
    {
        g_scene.folder_context_transition = 0.0f;
    }
    if (mode != TITAN_FOLDER_UI_CONTEXT)
    {
        g_scene.resolution_options_open = 0;
        g_scene.patch_options_open = 0;
    }
}

static void TitanShellScene_OpenHomeContext(const TitanHomeEntry *entry)
{
    if (entry == 0)
    {
        return;
    }

    g_scene.context_source = TITAN_CONTEXT_SOURCE_HOME;
    g_scene.context_shell_page = entry->page;
    g_scene.context_shell_catalog_index = entry->shell_catalog_index;
    g_scene.folder_context_selected = 0;
    g_scene.resolution_options_open = 0;
    g_scene.resolution_options_selected = 0;
    g_scene.resolution_options_transition = 0.0f;
    g_scene.patch_options_open = 0;
    g_scene.patch_options_kind = TITAN_GAME_PATCH_CHEAT;
    g_scene.patch_options_selected = 0;
    g_scene.patch_options_scroll = 0;
    g_scene.patch_options_transition = 0.0f;
    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CONTEXT);
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;
}

static void TitanShellScene_OpenSystemMenuContext(void)
{
    unsigned int catalog_index;
    const TitanShellItem *item;

    catalog_index = TitanShellLayout_GetSystemCatalogIndex(
        (unsigned int)g_scene.top_menu_selected
    );
    item = TitanShellCatalog_GetSystem(catalog_index);
    if (item == 0)
    {
        return;
    }

    g_scene.context_source = TITAN_CONTEXT_SOURCE_SYSTEM_MENU;
    g_scene.context_shell_page = item->page;
    g_scene.context_shell_catalog_index = catalog_index;
    g_scene.folder_context_selected = 0;
    g_scene.resolution_options_open = 0;
    g_scene.patch_options_open = 0;
    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CONTEXT);
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;
}

static void TitanShellScene_ConfirmVmcFormat(
    int confirmed,
    void *user_data
);

static void TitanShellScene_ConfirmVmcRecreate(
    int confirmed,
    void *user_data
);

static void TitanShellScene_ConfirmPhysicalFormatFirst(
    int confirmed,
    void *user_data
);

static void TitanShellScene_ConfirmPhysicalFormatFinal(
    int confirmed,
    void *user_data
);

static const char *TitanShellScene_GetMemoryCardName(int port)
{
    return port == 1 ? "Memory Card 2" : "Memory Card 1";
}

static void TitanShellScene_SetTextInputMessage(
    char *message,
    unsigned int capacity,
    const char *text
)
{
    unsigned int index;

    if (message == 0 || capacity == 0u)
    {
        return;
    }

    index = 0u;

    text = TitanLocalization_Compat(text);

    while (
        index + 1u < capacity &&
        text != 0 &&
        text[index] != '\0'
    )
    {
        message[index] = text[index];
        index++;
    }

    message[index] = '\0';
}

static int TitanShellScene_ValidateNewProfileName(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    TitanUserProfileNameResult result;

    (void)user_data;
    result = TitanUserProfiles_CheckName(text);

    if (result == TITAN_USER_PROFILE_NAME_OK)
    {
        return 1;
    }

    if (result == TITAN_USER_PROFILE_NAME_EMPTY)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Digite um nome para o perfil.");
    }
    else if (result == TITAN_USER_PROFILE_NAME_TOO_LONG)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "O nome e muito longo.");
    }
    else if (result == TITAN_USER_PROFILE_NAME_DUPLICATE)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Ja existe um perfil com esse nome.");
    }
    else if (result == TITAN_USER_PROFILE_NAME_CAPACITY_REACHED)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "O limite de quatro perfis foi atingido.");
    }
    else
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Use um nome valido.");
    }

    return 0;
}

static int TitanShellScene_FindGameByKey(
    TitanHomeGameKey key,
    unsigned int *game_index
)
{
    unsigned int index;
    unsigned int count;

    if (key == 0u)
    {
        return 0;
    }

    count = TitanNeutrino_GetGameCount();
    for (index = 0u; index < count; index++)
    {
        const TitanNeutrinoGame *game;

        game = TitanNeutrino_GetGame(index);
        if (
            game != 0 &&
            TitanHomeFolders_MakeCatalogGameKey(game) == key
        )
        {
            if (game_index != 0)
            {
                *game_index = index;
            }
            return 1;
        }
    }

    /* Read compatibility for HOMEFOLD.DAT created before UL records received
     * a unique startup-based key. */
    for (index = 0u; index < count; index++)
    {
        const TitanNeutrinoGame *game;

        game = TitanNeutrino_GetGame(index);
        if (
            TitanShellScene_GameCanUseLegacyFolderKey(index, game) &&
            TitanHomeFolders_MakeGameKey(game->path) == key
        )
        {
            if (game_index != 0)
            {
                *game_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static int TitanShellScene_FindShellByKey(
    TitanHomeGameKey key,
    unsigned int *catalog_index,
    const TitanShellItem **item
)
{
    unsigned int index;

    if (key == 0u)
    {
        return 0;
    }

    for (index = 0u; index < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; index++)
    {
        const TitanShellItem *candidate;

        candidate = TitanShellCatalog_Get(index);
        if (
            candidate != 0 &&
            TitanShellScene_HomeShellCanFolder(index) &&
            TitanHomeFolders_MakeShellKey((unsigned int)candidate->page) == key
        )
        {
            if (catalog_index != 0)
            {
                *catalog_index = index;
            }
            if (item != 0)
            {
                *item = candidate;
            }
            return 1;
        }
    }
    return 0;
}

static int TitanShellScene_CanSelectShellForFolder(unsigned int catalog_index)
{
    const TitanShellItem *item;
    int owner;

    if (!TitanShellScene_HomeShellCanFolder(catalog_index))
    {
        return 0;
    }

    item = TitanShellCatalog_Get(catalog_index);
    if (item == 0)
    {
        return 0;
    }

    owner = TitanHomeFolders_FindForItemKey(
        TitanHomeFolders_MakeShellKey((unsigned int)item->page)
    );
    return owner < 0 || owner == g_scene.folder_edit_existing;
}

static unsigned int TitanShellScene_GetFolderShellCandidateCount(void)
{
    unsigned int index;
    unsigned int count;

    count = 0u;
    for (index = 0u; index < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; index++)
    {
        if (TitanShellScene_HomeShellCanFolder(index))
        {
            count++;
        }
    }
    return count;
}

static int TitanShellScene_GetFolderShellCandidate(
    unsigned int visible_index,
    unsigned int *catalog_index
)
{
    unsigned int index;
    unsigned int count;

    count = 0u;
    for (index = 0u; index < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; index++)
    {
        if (!TitanShellScene_HomeShellCanFolder(index))
        {
            continue;
        }

        if (count == visible_index)
        {
            if (catalog_index != 0)
            {
                *catalog_index = index;
            }
            return 1;
        }
        count++;
    }
    return 0;
}

static int TitanShellScene_GetFolderCandidateIndexForShell(
    unsigned int catalog_index
)
{
    unsigned int visible;
    unsigned int mapped;
    unsigned int count;

    count = TitanShellScene_GetFolderShellCandidateCount();
    for (visible = 0u; visible < count; visible++)
    {
        if (
            TitanShellScene_GetFolderShellCandidate(visible, &mapped) &&
            mapped == catalog_index
        )
        {
            return (int)visible;
        }
    }
    return -1;
}

static unsigned int TitanShellScene_GetFolderCandidateCount(void)
{
    return
        TitanShellScene_GetFolderShellCandidateCount() +
        TitanNeutrino_GetGameCount();
}

static int TitanShellScene_GetFolderCandidate(
    unsigned int candidate_index,
    int *is_shell,
    unsigned int *item_index
)
{
    unsigned int shell_count;

    shell_count = TitanShellScene_GetFolderShellCandidateCount();
    if (candidate_index < shell_count)
    {
        if (is_shell != 0)
        {
            *is_shell = 1;
        }
        return TitanShellScene_GetFolderShellCandidate(
            candidate_index,
            item_index
        );
    }

    candidate_index -= shell_count;
    if (candidate_index >= TitanNeutrino_GetGameCount())
    {
        return 0;
    }

    if (is_shell != 0)
    {
        *is_shell = 0;
    }
    if (item_index != 0)
    {
        *item_index = candidate_index;
    }
    return 1;
}

static int TitanShellScene_FolderCandidateSelected(unsigned int candidate_index)
{
    int is_shell;
    unsigned int item_index;

    if (!TitanShellScene_GetFolderCandidate(candidate_index, &is_shell, &item_index))
    {
        return 0;
    }

    return is_shell
        ? g_scene.folder_selected_shell[item_index] != 0u
        : g_scene.folder_selected_games[item_index] != 0u;
}

static int TitanShellScene_CanSelectFolderCandidate(unsigned int candidate_index)
{
    int is_shell;
    unsigned int item_index;

    if (!TitanShellScene_GetFolderCandidate(candidate_index, &is_shell, &item_index))
    {
        return 0;
    }

    return is_shell
        ? TitanShellScene_CanSelectShellForFolder(item_index)
        : TitanShellScene_CanSelectGameForFolder(item_index);
}

static void TitanShellScene_SetFolderCandidateSelected(
    unsigned int candidate_index,
    int selected
)
{
    int is_shell;
    unsigned int item_index;

    if (!TitanShellScene_GetFolderCandidate(candidate_index, &is_shell, &item_index))
    {
        return;
    }

    if (is_shell)
    {
        g_scene.folder_selected_shell[item_index] = selected ? 1u : 0u;
    }
    else
    {
        g_scene.folder_selected_games[item_index] = selected ? 1u : 0u;
    }
}

static unsigned int TitanShellScene_CountFolderSelectedItems(void)
{
    unsigned int index;
    unsigned int count;
    unsigned int game_count;

    game_count = TitanNeutrino_GetGameCount();
    count = 0u;
    for (index = 0u; index < game_count; index++)
    {
        if (g_scene.folder_selected_games[index] != 0u)
        {
            count++;
        }
    }

    for (index = 0u; index < TITAN_FOLDER_SHELL_SELECTION_COUNT; index++)
    {
        if (g_scene.folder_selected_shell[index] != 0u)
        {
            count++;
        }
    }

    if (g_scene.folder_edit_existing >= 0)
    {
        const TitanHomeFolder *folder;

        folder = TitanHomeFolders_Get((unsigned int)g_scene.folder_edit_existing);
        if (folder != 0)
        {
            for (index = 0u; index < folder->item_count; index++)
            {
                if (
                    !TitanShellScene_FindGameByKey(folder->item_keys[index], 0) &&
                    !TitanShellScene_FindShellByKey(folder->item_keys[index], 0, 0)
                )
                {
                    count++;
                }
            }
        }
    }

    return count;
}

static void TitanShellScene_ConfirmFolderName(
    const char *text,
    void *user_data
)
{
    (void)user_data;
    TitanShellScene_SetTextInputMessage(
        g_scene.folder_pending_name,
        sizeof(g_scene.folder_pending_name),
        text
    );
}

static int TitanShellScene_ValidateFolderName(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    (void)user_data;

    if (text == 0 || text[0] == '\0')
    {
        TitanShellScene_SetTextInputMessage(
            message,
            message_capacity,
            "Enter a folder name."
        );
        return 0;
    }

    if (strlen(text) >= TITAN_HOME_FOLDER_NAME_CAPACITY)
    {
        TitanShellScene_SetTextInputMessage(
            message,
            message_capacity,
            "The folder name is too long."
        );
        return 0;
    }

    return 1;
}

static int TitanShellScene_OpenFolderNameInput(void)
{
    TitanTextInputRequest request;

    if (g_scene.folder_name_locked)
    {
        return 0;
    }

    request.title = TitanLocalization_Compat("Folder name");
    request.initial_text = g_scene.folder_pending_name;
    request.placeholder = TitanLocalization_Compat("Folder");
    request.maximum_codepoints = 32u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = TitanShellScene_ValidateFolderName;
    request.confirm_callback = TitanShellScene_ConfirmFolderName;
    request.cancel_callback = 0;
    request.user_data = 0;

    return TitanTextInput_Open(&request);
}

static void TitanShellScene_BeginFolderEditor(
    unsigned int origin_game_index,
    int origin_shell_catalog_index,
    int existing_folder,
    int name_locked
)
{
    unsigned int index;
    unsigned int game_count;

    memset(g_scene.folder_selected_games, 0, sizeof(g_scene.folder_selected_games));
    memset(g_scene.folder_selected_shell, 0, sizeof(g_scene.folder_selected_shell));
    g_scene.folder_origin_game_index = origin_game_index;
    g_scene.folder_origin_shell_catalog_index = origin_shell_catalog_index;
    g_scene.folder_edit_existing = existing_folder;
    g_scene.folder_name_locked = name_locked;
    g_scene.folder_editor_selected = name_locked ? 1 : 0;
    g_scene.folder_content_cursor = 0;
    g_scene.folder_content_side_selected = 0;
    g_scene.folder_content_side_active = 0;

    if (existing_folder >= 0)
    {
        const TitanHomeFolder *folder;

        folder = TitanHomeFolders_Get((unsigned int)existing_folder);
        if (folder != 0)
        {
            TitanShellScene_SetTextInputMessage(
                g_scene.folder_pending_name,
                sizeof(g_scene.folder_pending_name),
                folder->name
            );

            game_count = TitanNeutrino_GetGameCount();
            for (index = 0u; index < game_count; index++)
            {
                const TitanNeutrinoGame *game;

                game = TitanNeutrino_GetGame(index);
                if (
                    game != 0 &&
                    TitanShellScene_FolderContainsGame(
                        (unsigned int)existing_folder,
                        index,
                        game
                    )
                )
                {
                    g_scene.folder_selected_games[index] = 1u;
                }
            }

            for (index = 0u; index < TITAN_FOLDER_SHELL_SELECTION_COUNT; index++)
            {
                const TitanShellItem *item;

                item = TitanShellCatalog_Get(index);
                if (
                    item != 0 &&
                    TitanShellScene_HomeShellCanFolder(index) &&
                    TitanHomeFolders_ContainsItemKey(
                        (unsigned int)existing_folder,
                        TitanHomeFolders_MakeShellKey((unsigned int)item->page)
                    )
                )
                {
                    g_scene.folder_selected_shell[index] = 1u;
                }
            }
        }
    }
    else
    {
        TitanShellScene_SetTextInputMessage(
            g_scene.folder_pending_name,
            sizeof(g_scene.folder_pending_name),
            "Folder"
        );
    }

    if (origin_game_index < TitanNeutrino_GetGameCount())
    {
        g_scene.folder_selected_games[origin_game_index] = 1u;
        g_scene.folder_content_cursor =
            (int)TitanShellScene_GetFolderShellCandidateCount() +
            (int)origin_game_index;
    }
    else if (
        origin_shell_catalog_index >= 0 &&
        (unsigned int)origin_shell_catalog_index < TITAN_FOLDER_SHELL_SELECTION_COUNT &&
        TitanShellScene_HomeShellCanFolder((unsigned int)origin_shell_catalog_index)
    )
    {
        int candidate_index;

        g_scene.folder_selected_shell[origin_shell_catalog_index] = 1u;
        candidate_index = TitanShellScene_GetFolderCandidateIndexForShell(
            (unsigned int)origin_shell_catalog_index
        );
        if (candidate_index >= 0)
        {
            g_scene.folder_content_cursor = candidate_index;
        }
    }

    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_EDITOR);
}

static int TitanShellScene_ApplyFolderEditor(void)
{
    TitanHomeGameKey keys[TITAN_HOME_FOLDER_ITEM_CAPACITY];
    unsigned int key_count;
    unsigned int index;
    unsigned int game_count;
    unsigned int folder_index;
    int success;

    key_count = 0u;
    game_count = TitanNeutrino_GetGameCount();

    if (g_scene.folder_edit_existing >= 0)
    {
        const TitanHomeFolder *existing_folder;
        unsigned int existing_index;

        existing_folder = TitanHomeFolders_Get(
            (unsigned int)g_scene.folder_edit_existing
        );
        if (existing_folder != 0)
        {
            for (
                existing_index = 0u;
                existing_index < existing_folder->item_count &&
                key_count < TITAN_HOME_FOLDER_ITEM_CAPACITY;
                existing_index++
            )
            {
                if (
                    !TitanShellScene_FindGameByKey(
                        existing_folder->item_keys[existing_index],
                        0
                    ) &&
                    !TitanShellScene_FindShellByKey(
                        existing_folder->item_keys[existing_index],
                        0,
                        0
                    )
                )
                {
                    keys[key_count++] = existing_folder->item_keys[existing_index];
                }
            }
        }
    }

    for (index = 0u; index < TITAN_FOLDER_SHELL_SELECTION_COUNT; index++)
    {
        const TitanShellItem *item;

        if (g_scene.folder_selected_shell[index] == 0u)
        {
            continue;
        }

        item = TitanShellCatalog_Get(index);
        if (
            item == 0 ||
            !TitanShellScene_HomeShellCanFolder(index) ||
            key_count >= TITAN_HOME_FOLDER_ITEM_CAPACITY
        )
        {
            continue;
        }

        keys[key_count++] = TitanHomeFolders_MakeShellKey((unsigned int)item->page);
    }

    for (index = 0u; index < game_count; index++)
    {
        const TitanNeutrinoGame *game;

        if (g_scene.folder_selected_games[index] == 0u)
        {
            continue;
        }

        game = TitanNeutrino_GetGame(index);
        if (game == 0 || key_count >= TITAN_HOME_FOLDER_ITEM_CAPACITY)
        {
            continue;
        }

        keys[key_count++] = TitanHomeFolders_MakeCatalogGameKey(game);
    }

    if (key_count == 0u)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Folder",
            "Selecione pelo menos um item"
        );
        return 0;
    }

    if (g_scene.folder_edit_existing >= 0)
    {
        folder_index = (unsigned int)g_scene.folder_edit_existing;
        success = TitanHomeFolders_UpdateKeys(
            folder_index,
            g_scene.folder_pending_name,
            keys,
            key_count
        );
    }
    else
    {
        success = TitanHomeFolders_CreateKeys(
            g_scene.folder_pending_name,
            keys,
            key_count,
            &folder_index
        );
    }

    if (!success)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Folder",
            "Could not save the folder"
        );
        return 0;
    }

    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
    g_scene.folder_context_selected = 0;
    g_scene.resolution_options_open = 0;
    g_scene.resolution_options_selected = 0;
    g_scene.patch_options_open = 0;
    g_scene.patch_options_selected = 0;
    g_scene.patch_options_scroll = 0;
    g_scene.folder_destination_selected = 0;
    g_scene.folder_open = 0;
    g_scene.selected_item =
        (int)(
            TitanShellScene_GetHomeDexCount() +
            TitanShellScene_CountVisibleHomeShellRange(
                0u,
                TITAN_SHELL_BASE_PREFIX_COUNT
            ) +
            folder_index
        );
    TitanShellScene_ClampHomeSelection();
    g_scene.selected_position = (float)g_scene.selected_item;
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_SUCCESS,
        "Folder",
        g_scene.folder_pending_name
    );
    return 1;
}

static void TitanShellScene_ConfirmDeleteFolder(
    int confirmed,
    void *user_data
)
{
    unsigned int folder_index;

    folder_index = (unsigned int)(uintptr_t)user_data;
    if (!confirmed)
    {
        return;
    }

    if (TitanHomeFolders_Delete(folder_index))
    {
        TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
        g_scene.folder_open = 0;
        TitanShellScene_ClampHomeSelection();
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Folder deleted",
            "Os itens voltaram para a grade principal"
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Folder",
            "Could not delete the folder"
        );
    }
}

static void TitanShellScene_ConfirmUninstallApplication(
    int confirmed,
    void *user_data
)
{
    (void)user_data;

    if (!confirmed)
    {
        return;
    }
    if (TitanStorageScheduler_IsForegroundBusy())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Armazenamento ocupado",
            "Aguarde a operacao atual antes de desinstalar"
        );
        return;
    }
    if (!TitanStorageScheduler_RequestApplicationUninstall(
            g_scene.pending_uninstall_root,
            g_scene.pending_uninstall_title_id))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Desinstalacao",
            "Could not start application removal"
        );
        return;
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        TitanLocalization_Compat("Uninstalling application"),
        g_scene.pending_uninstall_title[0] != '\0'
            ? g_scene.pending_uninstall_title
            : g_scene.pending_uninstall_title_id
    );
}

static void TitanShellScene_NotifyStorageBootStatus(void)
{
    TitanProfileStorageStatus status;

    if (g_scene.storage_boot_notice_shown)
    {
        return;
    }

    g_scene.storage_boot_notice_shown = 1;
    status = TitanProfileStorage_GetStatus();

    if (status == TITAN_PROFILE_STORAGE_READY_LOADED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Dados do perfil",
            "Perfis carregados"
        );
    }
    else if (status == TITAN_PROFILE_STORAGE_READY_EMPTY)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Armazenamento",
            "Pronto para salvar"
        );
    }
    else if (status == TITAN_PROFILE_STORAGE_NO_STORAGE)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Armazenamento",
            "USB unavailable"
        );
    }
    else if (status == TITAN_PROFILE_STORAGE_CORRUPT)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Dados protegidos",
            "TITAN.DAT inválido"
        );
    }
    else if (status == TITAN_PROFILE_STORAGE_FUTURE_VERSION)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Dados protegidos",
            "Versão futura detectada"
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Armazenamento",
            "Serviço indisponível"
        );
    }
}

static void TitanShellScene_ReportVmcResult(
    TitanVmcResult result,
    int show_ready
)
{
    if (result == TITAN_VMC_RESULT_CREATED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "VMC criada",
            "Sistema de arquivos PS2 criado e verificado"
        );
    }
    else if (result == TITAN_VMC_RESULT_FORMATTED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "VMC formatada",
            "Sistema de arquivos PS2 criado e verificado"
        );
    }
    else if (result == TITAN_VMC_RESULT_OK && show_ready)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "VMC verificada",
            "Pronta para montar"
        );
    }
    else if (result == TITAN_VMC_RESULT_NEEDS_FORMAT)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "VMC disponível",
            "Formatação pendente"
        );
    }
    else if (result == TITAN_VMC_RESULT_INVALID)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "VMC protegida",
            "Arquivo inválido"
        );
    }
    else if (result == TITAN_VMC_RESULT_NO_STORAGE)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "VMC not created",
            "Armazenamento offline"
        );
    }
    else if (result == TITAN_VMC_RESULT_IO_ERROR)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Falha na VMC",
            "Erro de leitura/gravação"
        );
    }
}

static void TitanShellScene_AppendFailureText(
    char *buffer,
    unsigned int capacity,
    unsigned int *length,
    const char *text
)
{
    if (
        buffer == 0 ||
        length == 0 ||
        text == 0 ||
        capacity == 0u
    )
    {
        return;
    }

    while (
        *length + 1u < capacity &&
        *text != '\0'
    )
    {
        buffer[*length] = *text;
        (*length)++;
        text++;
    }

    buffer[*length] = '\0';
}

static void TitanShellScene_AppendFailureNumber(
    char *buffer,
    unsigned int capacity,
    unsigned int *length,
    int value
)
{
    char digits[12];
    unsigned int count;
    unsigned int magnitude;

    if (value < 0)
    {
        TitanShellScene_AppendFailureText(
            buffer,
            capacity,
            length,
            "-"
        );
        magnitude = (unsigned int)(-(value + 1)) + 1u;
    }
    else
    {
        magnitude = (unsigned int)value;
    }

    count = 0u;

    do
    {
        digits[count] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
        count++;
    }
    while (magnitude != 0u && count < sizeof(digits));

    while (count > 0u)
    {
        char character[2];

        count--;
        character[0] = digits[count];
        character[1] = '\0';
        TitanShellScene_AppendFailureText(
            buffer,
            capacity,
            length,
            character
        );
    }
}

static void TitanShellScene_BuildStorageFailureDetail(
    char *buffer,
    unsigned int capacity
)
{
    unsigned int length;

    if (buffer == 0 || capacity == 0u)
    {
        return;
    }

    buffer[0] = '\0';
    length = 0u;
    TitanShellScene_AppendFailureText(
        buffer,
        capacity,
        &length,
        TitanProfileStorage_GetLastFailureStageName()
    );
    TitanShellScene_AppendFailureText(
        buffer,
        capacity,
        &length,
        " r="
    );
    TitanShellScene_AppendFailureNumber(
        buffer,
        capacity,
        &length,
        TitanProfileStorage_GetLastFailureResult()
    );
    TitanShellScene_AppendFailureText(
        buffer,
        capacity,
        &length,
        " c="
    );
    TitanShellScene_AppendFailureNumber(
        buffer,
        capacity,
        &length,
        TitanProfileStorage_GetLastFailureCommand()
    );
}

static void TitanShellScene_ReportSaveResult(
    TitanProfileStorageSaveResult result,
    int allow_format_prompt,
    int show_success,
    int show_failure
)
{
    (void)allow_format_prompt;

    if (result == TITAN_PROFILE_STORAGE_SAVE_OK)
    {
        if (show_success)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Perfil salvo",
                "Dados verificados"
            );
        }

        return;
    }

    if (result == TITAN_PROFILE_STORAGE_SAVE_NOT_NEEDED)
    {
        return;
    }

    if (!show_failure)
    {
        return;
    }

    if (result == TITAN_PROFILE_STORAGE_SAVE_NO_STORAGE)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Perfil temporário",
            "Armazenamento offline"
        );
    }
    else if (result == TITAN_PROFILE_STORAGE_SAVE_PROTECTED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Dados protegidos",
            "Dados preservados"
        );
    }
    else
    {
        char detail[64];

        TitanShellScene_BuildStorageFailureDetail(
            detail,
            sizeof(detail)
        );
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Falha ao salvar",
            detail
        );
    }
}

static void TitanShellScene_PollStorageScheduler(void)
{
    TitanStorageCompletion completion;

    if (!TitanStorageScheduler_PollCompletion(&completion))
    {
        return;
    }

    if (completion.type == TITAN_STORAGE_FOREGROUND_PROFILE_SAVE)
    {
        TitanShellScene_ReportSaveResult(
            (TitanProfileStorageSaveResult)completion.result,
            completion.allow_format_prompt,
            completion.show_success,
            completion.show_failure
        );
    }
    else if (completion.type == TITAN_STORAGE_FOREGROUND_VMC_VERIFY ||
             completion.type == TITAN_STORAGE_FOREGROUND_VMC_FORMAT ||
             completion.type == TITAN_STORAGE_FOREGROUND_VMC_RECREATE)
    {
        TitanShellScene_ReportVmcResult(
            (TitanVmcResult)completion.result,
            1
        );
    }
    else if (completion.type == TITAN_STORAGE_FOREGROUND_APP_UNINSTALL)
    {
        if (completion.result)
        {
            (void)TitanApplications_RemoveInstalledFromCatalog(
                completion.application_title_id
            );
            (void)TitanStorageScheduler_RequestAppAssetsRebuild();
            if (!TitanStorageScheduler_IsLibraryScanBusy())
            {
                (void)TitanStorageScheduler_RequestLibraryScan(1);
            }
            TitanShellScene_ClampHomeSelection();
            TitanStorageInventory_RequestRefresh();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Application uninstalled",
                g_scene.pending_uninstall_title[0] != '\0'
                    ? g_scene.pending_uninstall_title
                    : completion.application_title_id
            );
        }
        else
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ERROR,
                "Desinstalacao",
                "Could not completely remove the application"
            );
        }
        g_scene.pending_uninstall_root[0] = '\0';
        g_scene.pending_uninstall_title_id[0] = '\0';
        g_scene.pending_uninstall_title[0] = '\0';
    }
}

static void TitanShellScene_SaveProfileState(
    int allow_format_prompt,
    int show_success,
    int show_failure
)
{
    if (!TitanStorageScheduler_RequestProfileSave(
            allow_format_prompt,
            show_success,
            show_failure))
    {
        if (show_failure)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_WARNING,
                "Armazenamento ocupado",
                "Aguarde a operação atual e tente novamente"
            );
        }
        return;
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Salvando configuração",
        "Gravando dados do perfil"
    );
}

static void TitanShellScene_CancelVideoTest(const char *message)
{
    if (!g_scene.video_test_active)
    {
        return;
    }

    TitanVideoOutput_SetShellProgressive(
        g_scene.video_test_previous_progressive
    );
    TitanShellBackground_RefreshThemeAssets();
    g_scene.video_test_active = 0;
    g_scene.video_test_frames_remaining = 0u;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_WARNING,
        "Video output restored",
        message != 0 ? message : "Modo anterior reativado"
    );
}

static void TitanShellScene_StartVideoTest(void)
{
    TitanResult result;
    int target_progressive;

    if (!TitanVideoOutput_CanTestProgressive())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Teste progressivo indisponível",
            "Selecione Component no OSD do PS2"
        );
        return;
    }

    g_scene.video_test_previous_progressive =
        TitanVideoOutput_IsShellProgressive();
    target_progressive = !g_scene.video_test_previous_progressive;

    result = TitanVideoOutput_SetShellProgressive(target_progressive);
    if (result != TITAN_RESULT_OK)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Falha no modo de vídeo",
            "O modo anterior foi preservado"
        );
        return;
    }

    /* A successful layout transition may clear a transient VRAM recovery
     * latch (notably legacy bootstrap -> optimized 720p). Re-evaluate theme
     * residency immediately instead of leaving this session visually stuck
     * in MINIMUM until the next reboot. */
    TitanShellBackground_RefreshThemeAssets();

    g_scene.video_test_active = 1;
    g_scene.video_test_frames_remaining =
        TITAN_VIDEO_TEST_DURATION_FRAMES;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Testing video output",
        "Temporary 720p output. Press X to keep or O to restore"
    );
}

static void TitanShellScene_ConfirmVideoTest(void)
{
    if (!g_scene.video_test_active)
    {
        return;
    }

    g_scene.video_test_active = 0;
    g_scene.video_test_frames_remaining = 0u;

    if (TitanVideoOutput_SavePersistentSetting())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Video output confirmed",
            TitanVideoOutput_IsShellProgressive()
                ? "Modo 720p mantido"
                : "Modo interlaçado mantido"
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Video output confirmed",
            "Mode kept for this session; preference was not saved"
        );
    }
}

static void TitanShellScene_VerifyActiveVmc(void)
{
    const TitanUserProfile *profile;

    profile = TitanUserProfiles_GetActive();

    if (profile == 0)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "VMC indisponível",
            "Nenhum perfil ativo"
        );
        return;
    }

    if (!TitanStorageScheduler_RequestVmcVerify(profile))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "VMC ocupada",
            "Aguarde a operação de armazenamento atual"
        );
        return;
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Verificando VMC",
        "Analisando o sistema de arquivos em segundo plano"
    );
}

static void TitanShellScene_BeginVmcFormat(void)
{
    const TitanUserProfile *profile;

    profile = TitanUserProfiles_GetActive();
    if (profile == 0)
    {
        TitanShellScene_VerifyActiveVmc();
        return;
    }

    if (
        !TitanNotifications_OpenConfirmation(
            "Formatar VMC do perfil?",
            "All saves on the active VMC will be erased.",
            "Uma cópia .bak será mantida no armazenamento.",
            "Cancel",
            "Formatar VMC",
            TitanShellScene_ConfirmVmcFormat,
            0
        )
    )
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "VMC",
            "Confirmação indisponível"
        );
    }
}

static void TitanShellScene_ConfirmVmcFormat(
    int confirmed,
    void *user_data
)
{
    const TitanUserProfile *profile;

    (void)user_data;
    if (!confirmed)
    {
        return;
    }

    profile = TitanUserProfiles_GetActive();
    if (profile == 0 || !TitanStorageScheduler_RequestVmcFormat(profile))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "VMC ocupada",
            "Aguarde a operação de armazenamento atual"
        );
        return;
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Formatando VMC",
        "Do not remove storage"
    );
}

static void TitanShellScene_BeginVmcRecreate(void)
{
    const TitanUserProfile *profile;

    profile = TitanUserProfiles_GetActive();

    if (profile == 0)
    {
        TitanShellScene_VerifyActiveVmc();
        return;
    }

    if (
        !TitanNotifications_OpenConfirmation(
            "Recriar VMC do perfil?",
            "All saves on this VMC will be erased.",
            "The profile and TITAN.DAT will be preserved.",
            "Cancel",
            "Recriar VMC",
            TitanShellScene_ConfirmVmcRecreate,
            0
        )
    )
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "VMC",
            "Confirmação indisponível"
        );
    }
}

static void TitanShellScene_ConfirmVmcRecreate(
    int confirmed,
    void *user_data
)
{
    const TitanUserProfile *profile;

    (void)user_data;

    if (!confirmed)
    {
        return;
    }

    profile = TitanUserProfiles_GetActive();
    if (profile == 0 || !TitanStorageScheduler_RequestVmcRecreate(profile))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "VMC ocupada",
            "Aguarde a operação de armazenamento atual"
        );
        return;
    }

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Recriando VMC",
        "Do not remove storage"
    );
}

static void TitanShellScene_BeginPhysicalFormat(void)
{
    const char *title;

    g_scene.memory_card_action_port =
        g_scene.memory_card_selected_port;
    title =
        g_scene.memory_card_action_port == 1
            ? "Formatar Memory Card 2?"
            : "Formatar Memory Card 1?";

    if (
        !TitanNotifications_OpenConfirmation(
            title,
            "All saves and data on the card will be erased.",
            "Escolha continuar para ver a confirmação final.",
            "Cancel",
            "Continuar",
            TitanShellScene_ConfirmPhysicalFormatFirst,
            0
        )
    )
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Formatação",
            "Confirmação indisponível"
        );
    }
}

static void TitanShellScene_ConfirmPhysicalFormatFirst(
    int confirmed,
    void *user_data
)
{
    const char *line_1;

    (void)user_data;

    if (!confirmed)
    {
        return;
    }

    line_1 =
        g_scene.memory_card_action_port == 1
            ? "O Memory Card 2 será completamente apagado."
            : "O Memory Card 1 será completamente apagado.";

    if (
        !TitanNotifications_OpenConfirmation(
            "Confirmação final",
            line_1,
            "This operation cannot be undone.",
            "Cancel",
            "APAGAR TUDO",
            TitanShellScene_ConfirmPhysicalFormatFinal,
            0
        )
    )
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Formatação",
            "Confirmação indisponível"
        );
    }
}

static void TitanShellScene_ConfirmPhysicalFormatFinal(
    int confirmed,
    void *user_data
)
{
    TitanProfileStorageFormatResult result;
    int port;
    const char *slot_message;

    (void)user_data;

    if (!confirmed)
    {
        return;
    }

    port = g_scene.memory_card_action_port;
    slot_message = port == 1 ? "Slot 2 concluído" : "Slot 1 concluído";
    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Formatando Memory Card",
        port == 1 ? "Processando Slot 2" : "Processando Slot 1"
    );
    TitanShellBusy_Begin();
    TitanShellBusy_Pulse();
    result = TitanProfileStorage_FormatPort(port);
    TitanShellBusy_End();

    if (result == TITAN_PROFILE_STORAGE_FORMAT_OK)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Formatação concluída",
            slot_message
        );
    }
    else if (result == TITAN_PROFILE_STORAGE_FORMAT_NO_CARD)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Formatação cancelada",
            "Device not detected"
        );
    }
    else if (result == TITAN_PROFILE_STORAGE_FORMAT_UNSUPPORTED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Falha ao formatar",
            "Dispositivo incompatível"
        );
    }
    else
    {
        char detail[64];

        TitanShellScene_BuildStorageFailureDetail(
            detail,
            sizeof(detail)
        );
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Falha ao formatar",
            detail
        );
    }
}

static void TitanShellScene_ConfirmNewProfileAvatar(
    unsigned int avatar_id,
    void *user_data
)
{
    unsigned int profile_count;
    int has_add_tile;

    (void)user_data;

    if (
        !TitanUserProfiles_AddNamedWithAvatar(
            g_scene.pending_profile_name,
            avatar_id
        )
    )
    {
        return;
    }

    g_scene.pending_profile_name[0] = '\0';

    TitanShellScene_SaveProfileState(1, 1, 1);

    if (!g_scene.profile_creation_from_startup)
    {
        return;
    }

    profile_count = TitanUserProfiles_GetCount();
    has_add_tile = profile_count < TITAN_USER_PROFILE_CAPACITY;
    g_scene.startup_profile_selected =
        has_add_tile
            ? (int)TitanUserProfiles_GetActiveIndex() + 1
            : (int)TitanUserProfiles_GetActiveIndex();
    g_scene.startup_profile_position =
        (float)g_scene.startup_profile_selected;
}

static void TitanShellScene_CancelNewProfileAvatar(
    void *user_data
)
{
    (void)user_data;
    g_scene.pending_profile_name[0] = '\0';
}

static void TitanShellScene_ConfirmNewProfileName(
    const char *text,
    void *user_data
)
{
    TitanAvatarPickerRequest request;

    (void)user_data;

    TitanShellScene_SetTextInputMessage(
        g_scene.pending_profile_name,
        sizeof(g_scene.pending_profile_name),
        text
    );

    request.title = TitanLocalization_Compat("Escolha seu avatar");
    request.profile_name = g_scene.pending_profile_name;
    request.initial_avatar_id = 0u;
    request.confirm_callback = TitanShellScene_ConfirmNewProfileAvatar;
    request.cancel_callback = TitanShellScene_CancelNewProfileAvatar;
    request.user_data = 0;

    if (!TitanAvatarPicker_Open(&request))
    {
        g_scene.pending_profile_name[0] = '\0';
    }
}

static int TitanShellScene_ValidateProfileRename(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    TitanUserProfileNameResult result;

    (void)user_data;
    result = TitanUserProfiles_CheckRenameActive(text);
    if (result == TITAN_USER_PROFILE_NAME_OK)
    {
        return 1;
    }

    if (result == TITAN_USER_PROFILE_NAME_EMPTY)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Digite um nome para o perfil.");
    }
    else if (result == TITAN_USER_PROFILE_NAME_TOO_LONG)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "O nome e muito longo.");
    }
    else if (result == TITAN_USER_PROFILE_NAME_DUPLICATE)
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Ja existe um perfil com esse nome.");
    }
    else
    {
        TitanShellScene_SetTextInputMessage(message, message_capacity, "Use um nome valido.");
    }
    return 0;
}

static void TitanShellScene_ConfirmProfileRename(
    const char *text,
    void *user_data
)
{
    (void)user_data;

    if (TitanUserProfiles_RenameActive(text))
    {
        TitanShellScene_SaveProfileState(0, 0, 0);
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Perfil atualizado",
            text
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Perfil",
            "Could not rename the profile"
        );
    }
}

static int TitanShellScene_OpenProfileRename(void)
{
    const TitanUserProfile *profile;
    TitanTextInputRequest request;

    profile = TitanUserProfiles_GetActive();
    if (profile == 0)
    {
        return 0;
    }

    request.title = TitanLocalization_Compat("Editar perfil");
    request.initial_text = profile->name;
    request.placeholder = TitanLocalization_Compat("Nome do perfil");
    request.maximum_codepoints = 20u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = TitanShellScene_ValidateProfileRename;
    request.confirm_callback = TitanShellScene_ConfirmProfileRename;
    request.cancel_callback = 0;
    request.user_data = 0;
    return TitanTextInput_Open(&request);
}

static int TitanShellScene_OpenProfileCreation(
    int from_startup
)
{
    TitanTextInputRequest request;

    g_scene.pending_profile_name[0] = '\0';

    request.title = TitanLocalization_Compat("Criar perfil");
    request.initial_text = "";
    request.placeholder = TitanLocalization_Compat("Digite o nome do perfil");
    request.maximum_codepoints = 20u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = TitanShellScene_ValidateNewProfileName;
    request.confirm_callback = TitanShellScene_ConfirmNewProfileName;
    request.cancel_callback = 0;
    request.user_data = 0;

    if (!TitanTextInput_Open(&request))
    {
        return 0;
    }

    g_scene.profile_creation_from_startup = from_startup;
    return 1;
}

static void TitanShellScene_FormatUnsigned(
    unsigned int value,
    char *buffer,
    unsigned int capacity
)
{
    char reversed[16];
    unsigned int count;
    unsigned int output;

    if (buffer == 0 || capacity == 0)
    {
        return;
    }

    count = 0;

    do
    {
        reversed[count] =
            (char)('0' + (value % 10u));

        value /= 10u;
        count++;
    }
    while (
        value > 0u &&
        count < sizeof(reversed)
    );

    output = 0;

    while (
        count > 0u &&
        output + 1u < capacity
    )
    {
        count--;
        buffer[output] = reversed[count];
        output++;
    }

    buffer[output] = '\0';
}

static void TitanShellScene_FormatInteger(
    int value,
    char *buffer,
    unsigned int capacity
)
{
    unsigned int magnitude;

    if (buffer == 0 || capacity == 0)
    {
        return;
    }

    if (value >= 0)
    {
        TitanShellScene_FormatUnsigned(
            (unsigned int)value,
            buffer,
            capacity
        );

        return;
    }

    if (capacity < 2)
    {
        buffer[0] = '\0';
        return;
    }

    buffer[0] = '-';

    magnitude =
        (unsigned int)(-(value + 1)) + 1u;

    TitanShellScene_FormatUnsigned(
        magnitude,
        buffer + 1,
        capacity - 1
    );
}

static void TitanShellScene_BuildTickerText(
    const char *message,
    unsigned int offset,
    unsigned int window,
    char *buffer,
    unsigned int capacity
)
{
    unsigned int length;
    unsigned int index;
    unsigned int total;

    if (buffer == 0 || capacity == 0u)
    {
        return;
    }

    buffer[0] = '\0';

    if (message == 0 || window == 0u)
    {
        return;
    }

    length = 0u;

    while (message[length] != '\0' && length < 240u)
    {
        length++;
    }

    if (length == 0u)
    {
        return;
    }

    if (window + 1u > capacity)
    {
        window = capacity - 1u;
    }

    total = length + 6u;

    for (index = 0u; index < window; index++)
    {
        unsigned int source_index;

        source_index = (offset + index) % total;

        if (source_index < length)
        {
            buffer[index] = message[source_index];
        }
        else
        {
            buffer[index] = ' ';
        }
    }

    buffer[window] = '\0';
}

static float TitanShellScene_Clamp(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

static unsigned char TitanShellScene_Alpha(
    float value
)
{
    return (unsigned char)TitanShellScene_Clamp(
        value,
        0.0f,
        255.0f
    );
}

static unsigned char TitanShellScene_ModulateAlpha(
    unsigned char base_alpha,
    unsigned char layer_alpha
)
{
    return TitanShellScene_Alpha(
        (float)base_alpha * ((float)layer_alpha / 255.0f)
    );
}

static float TitanShellScene_FrameProgress(
    unsigned int frame,
    unsigned int duration
)
{
    if (duration == 0u)
    {
        return 1.0f;
    }

    return TitanShellScene_Clamp(
        (float)frame / (float)duration,
        0.0f,
        1.0f
    );
}

static float TitanShellScene_MoveTowards(
    float current,
    float target,
    float maximum_delta
)
{
    if (current < target)
    {
        current += maximum_delta;

        if (current > target)
        {
            current = target;
        }
    }
    else if (current > target)
    {
        current -= maximum_delta;

        if (current < target)
        {
            current = target;
        }
    }

    return current;
}

static void TitanShellScene_DrawOutline(
    float x,
    float y,
    float width,
    float height,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    if (alpha == 0)
    {
        return;
    }

    TitanVideo_DrawRectAlpha(
        x,
        y,
        width,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawRectAlpha(
        x,
        y + height - thickness,
        width,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawRectAlpha(
        x,
        y,
        thickness,
        height,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawRectAlpha(
        x + width - thickness,
        y,
        thickness,
        height,
        red,
        green,
        blue,
        alpha
    );
}



#define TITAN_POWER_PS2SLIM_WIDTH 256u
#define TITAN_POWER_PS2SLIM_HEIGHT 192u
#define TITAN_POWER_PS2SLIM_EXPECTED_BYTES \
    (TITAN_POWER_PS2SLIM_WIDTH * TITAN_POWER_PS2SLIM_HEIGHT)

static unsigned char TitanShellScene_SampleImageA8Bilinear(
    const unsigned char *data,
    unsigned int width,
    unsigned int height,
    unsigned int source_x_fixed,
    unsigned int source_y_fixed
)
{
    unsigned int source_x;
    unsigned int source_y;
    unsigned int next_x;
    unsigned int next_y;
    unsigned int fraction_x;
    unsigned int fraction_y;
    unsigned int row0;
    unsigned int row1;
    unsigned int alpha00;
    unsigned int alpha10;
    unsigned int alpha01;
    unsigned int alpha11;
    unsigned int alpha0;
    unsigned int alpha1;

    source_x = source_x_fixed >> 8;
    source_y = source_y_fixed >> 8;
    fraction_x = source_x_fixed & 255u;
    fraction_y = source_y_fixed & 255u;

    if (source_x >= width)
    {
        source_x = width - 1u;
        fraction_x = 0u;
    }

    if (source_y >= height)
    {
        source_y = height - 1u;
        fraction_y = 0u;
    }

    next_x = source_x + 1u;
    next_y = source_y + 1u;

    if (next_x >= width)
    {
        next_x = source_x;
    }

    if (next_y >= height)
    {
        next_y = source_y;
    }

    row0 = source_y * width;
    row1 = next_y * width;

    alpha00 = data[row0 + source_x];
    alpha10 = data[row0 + next_x];
    alpha01 = data[row1 + source_x];
    alpha11 = data[row1 + next_x];

    alpha0 = ((alpha00 * (256u - fraction_x)) + (alpha10 * fraction_x)) >> 8;
    alpha1 = ((alpha01 * (256u - fraction_x)) + (alpha11 * fraction_x)) >> 8;

    return (unsigned char)(((alpha0 * (256u - fraction_y)) + (alpha1 * fraction_y)) >> 8);
}

static void TitanShellScene_DrawImageA8(
    const unsigned char *data,
    unsigned int data_size,
    unsigned int source_width,
    unsigned int source_height,
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int physical_x0;
    int physical_x1;
    int physical_y0;
    int physical_y1;
    int physical_width;
    int physical_height;
    int destination_x;
    int destination_y;
    unsigned int source_x_fixed;
    unsigned int source_y_fixed;
    unsigned int sample_alpha;
    unsigned int combined_alpha;

    if (
        data == 0 ||
        alpha == 0u ||
        source_width == 0u ||
        source_height == 0u ||
        data_size != source_width * source_height
    )
    {
        return;
    }

    physical_x0 = TitanVideo_MapLogicalX(center_x - (width * 0.5f));
    physical_x1 = TitanVideo_MapLogicalX(center_x + (width * 0.5f));
    physical_y0 = TitanVideo_MapLogicalY(center_y - (height * 0.5f));
    physical_y1 = TitanVideo_MapLogicalY(center_y + (height * 0.5f));

    physical_width = physical_x1 - physical_x0;
    physical_height = physical_y1 - physical_y0;

    if (physical_width < 1)
    {
        physical_width = 1;
    }

    if (physical_height < 1)
    {
        physical_height = 1;
    }

    for (destination_y = 0; destination_y < physical_height; destination_y++)
    {
        if (physical_height > 1)
        {
            source_y_fixed = ((unsigned int)destination_y * ((source_height - 1u) << 8)) / (unsigned int)(physical_height - 1);
        }
        else
        {
            source_y_fixed = ((source_height - 1u) << 7);
        }

        for (destination_x = 0; destination_x < physical_width; destination_x++)
        {
            if (physical_width > 1)
            {
                source_x_fixed = ((unsigned int)destination_x * ((source_width - 1u) << 8)) / (unsigned int)(physical_width - 1);
            }
            else
            {
                source_x_fixed = ((source_width - 1u) << 7);
            }

            sample_alpha = TitanShellScene_SampleImageA8Bilinear(
                data,
                source_width,
                source_height,
                source_x_fixed,
                source_y_fixed
            );

            if (sample_alpha < 2u)
            {
                continue;
            }

            combined_alpha = ((sample_alpha * (unsigned int)alpha) + 127u) / 255u;

            if (combined_alpha >= 250u)
            {
                TitanVideo_DrawPixelPhysical(
                    physical_x0 + destination_x,
                    physical_y0 + destination_y,
                    red,
                    green,
                    blue
                );
            }
            else if (combined_alpha > 1u)
            {
                TitanVideo_DrawPixelPhysicalAlpha(
                    physical_x0 + destination_x,
                    physical_y0 + destination_y,
                    red,
                    green,
                    blue,
                    (unsigned char)combined_alpha
                );
            }
        }
    }
}

static void TitanShellScene_DrawShutdownSymbol(
    int symbol,
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float half;
    float radius;
    float thickness;

    if (alpha == 0u)
    {
        return;
    }

    half = 4.2f * scale;
    radius = 4.2f * scale;
    thickness = 1.4f * scale;

    switch (symbol)
    {
        case 0:
            TitanShellScene_DrawOutline(center_x - half, center_y - half, half * 2.0f, half * 2.0f, thickness, red, green, blue, alpha);
            break;
        case 1:
            TitanVideo_DrawLineAlpha(center_x, center_y - half, center_x - half, center_y + half, thickness, red, green, blue, alpha);
            TitanVideo_DrawLineAlpha(center_x - half, center_y + half, center_x + half, center_y + half, thickness, red, green, blue, alpha);
            TitanVideo_DrawLineAlpha(center_x + half, center_y + half, center_x, center_y - half, thickness, red, green, blue, alpha);
            break;
        case 2:
            TitanVideo_DrawCircleAlpha(center_x, center_y, radius, red, green, blue, alpha);
            TitanVideo_DrawCircleAlpha(center_x, center_y, TitanShellScene_Clamp(radius - thickness, 0.0f, radius), 0, 0, 0, alpha);
            break;
        default:
            TitanVideo_DrawLineAlpha(center_x - half, center_y - half, center_x + half, center_y + half, thickness, red, green, blue, alpha);
            TitanVideo_DrawLineAlpha(center_x + half, center_y - half, center_x - half, center_y + half, thickness, red, green, blue, alpha);
            break;
    }
}

static void TitanShellScene_DrawShutdownSymbolCluster(
    unsigned int frame,
    float x,
    float y,
    unsigned char alpha
)
{
    static const int sequence[4] =
    {
        1, 0, 3, 2
    };
    unsigned int sequence_index;
    unsigned int phase;
    float pulse;
    unsigned char symbol_alpha;

    sequence_index = (frame / 16u) % 4u;
    phase = frame % 16u;

    if (phase < 4u)
    {
        pulse = (float)phase / 4.0f;
    }
    else if (phase > 11u)
    {
        pulse = (float)(15u - phase) / 4.0f;
    }
    else
    {
        pulse = 1.0f;
    }

    symbol_alpha = TitanShellScene_Alpha(
        (float)alpha * (0.36f + (pulse * 0.62f))
    );

    TitanShellScene_DrawShutdownSymbol(
        sequence[sequence_index],
        x,
        y,
        1.26f + (pulse * 0.08f),
        242,
        245,
        250,
        symbol_alpha
    );
}

static void TitanShellScene_DrawShutdownScreen(
    unsigned int frame,
    unsigned char alpha
)
{
    const char *title;
    const char *line1;
    const char *line2;
    float title_scale;
    float body_scale;
    float title_width;
    float title_group_width;
    float title_group_x;
    float card_x;
    float card_y;
    float card_width;
    float card_height;
    float text_x;
    unsigned char title_alpha;
    unsigned char body_alpha;
    unsigned char panel_alpha;

    title = "Preparando para desligar o PS2...";
    line1 = "Do not disconnect the power cable while the";
    line2 = "indicador do PS2 estiver aceso ou piscando.";
    title_scale = 0.82f;
    body_scale = 0.70f;
    title_width = TitanFont_MeasureText(title_scale, title);
    title_group_width = 22.0f + 12.0f + title_width;
    title_group_x = ((float)TITAN_UI_WIDTH - title_group_width) * 0.5f;
    card_x = 112.0f;
    card_y = 188.0f;
    card_width = 126.0f;
    card_height = 112.0f;
    text_x = card_x + card_width + 28.0f;

    title_alpha = TitanShellScene_Alpha((float)alpha * 0.98f);
    body_alpha = TitanShellScene_Alpha((float)alpha * 0.88f);
    panel_alpha = TitanShellScene_Alpha((float)alpha * 0.66f);

    TitanVideo_DrawRectAlpha(0.0f, 0.0f, (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT, 0, 0, 0, panel_alpha);

    TitanShellScene_DrawShutdownSymbolCluster(frame, title_group_x + 8.0f, 133.0f, alpha);

    TitanFont_DrawTextShadow(
        title_group_x + 22.0f + 12.0f,
        126.0f,
        title_scale,
        title,
        244,
        247,
        252,
        title_alpha
    );

    TitanShellScene_DrawImageA8(
        titan_ui_power_ps2slim,
        size_titan_ui_power_ps2slim,
        TITAN_POWER_PS2SLIM_WIDTH,
        TITAN_POWER_PS2SLIM_HEIGHT,
        card_x + (card_width * 0.5f),
        card_y + (card_height * 0.5f),
        150.0f,
        112.0f,
        246,
        248,
        252,
        alpha
    );

    TitanFont_DrawTextShadow(text_x, 224.0f, body_scale, line1, 244, 247, 252, body_alpha);
    TitanFont_DrawTextShadow(text_x, 248.0f, body_scale, line2, 244, 247, 252, body_alpha);
}

static void TitanShellScene_DrawStartupHealthMessage(
    unsigned char alpha
)
{
    const char *line1;
    const char *line2;
    float scale;
    float width1;
    float width2;
    float x1;
    float x2;

    line1 = TitanLocalization_Get(TITAN_STR_HEALTH_LINE1);
    line2 = TitanLocalization_Get(TITAN_STR_HEALTH_LINE2);
    scale = 0.80f;
    width1 = TitanFont_MeasureText(scale, line1);
    width2 = TitanFont_MeasureText(scale, line2);
    x1 = ((float)TITAN_UI_WIDTH - width1) * 0.5f;
    x2 = ((float)TITAN_UI_WIDTH - width2) * 0.5f;

    TitanFont_DrawTextShadow(x1, 206.0f, scale, line1, 240, 244, 248, alpha);
    TitanFont_DrawTextShadow(x2, 236.0f, scale, line2, 240, 244, 248, alpha);
}

static const TitanInputDeviceDescriptor *TitanShellScene_WelcomeCandidate(void)
{
    const TitanInputDeviceDescriptor *d=TitanControllerAssignment_GetWelcomeCandidate();
    if (d != 0) g_welcome_candidate_id=d->device_id;
    else g_welcome_candidate_id=0u;
    return d;
}

static const char *TitanShellScene_WelcomeGlyph(const TitanInputDeviceDescriptor *d)
{
    if(!d || d->type==TITAN_INPUT_DEVICE_DUALSHOCK2) return "X";
    if(d->type==TITAN_INPUT_DEVICE_XBOX_ONE || d->type==TITAN_INPUT_DEVICE_XBOX_SERIES) return "HOME";
    if(d->type==TITAN_INPUT_DEVICE_DUALSHOCK3 || d->type==TITAN_INPUT_DEVICE_DUALSHOCK4 || d->type==TITAN_INPUT_DEVICE_DUALSENSE) return "PS";
    return "X";
}

static int TitanShellScene_WelcomeConfirm(const TitanInputDeviceDescriptor *d)
{
    uint32_t required;
    if(!d) return 0;
    required=(d->type==TITAN_INPUT_DEVICE_DUALSHOCK2)?TITAN_NBTN_CROSS:TITAN_NBTN_SYSTEM;
    if(((d->state.pressed|d->state.buttons) & required)==0u) return 0;
    return TitanUnifiedInput_ClaimPlayerOnFirstInput(d->device_id,0u);
}

static void TitanShellScene_DrawStartupWelcome(
    unsigned char alpha
)
{
    const TitanInputDeviceDescriptor *device = TitanShellScene_WelcomeCandidate();
    const char *title = TitanLocalization_Get(TITAN_STR_WELCOME_TITLE);
    const char *glyph = TitanShellScene_WelcomeGlyph(device);
    const char *prompt =
        (device && device->type != TITAN_INPUT_DEVICE_DUALSHOCK2)
            ? TitanLocalization_Get(TITAN_STR_WELCOME_SYSTEM)
            : TitanLocalization_Get(TITAN_STR_WELCOME_X);
    float title_scale = 0.94f;
    float prompt_scale = 0.62f;
    float title_x =
        ((float)TITAN_UI_WIDTH - TitanFont_MeasureText(title_scale, title)) * 0.5f;
    float prompt_x =
        ((float)TITAN_UI_WIDTH - TitanFont_MeasureText(prompt_scale, prompt)) * 0.5f;
    float pulse = 1.0f +
        0.035f * (float)((g_scene.startup_phase_frames / 8u) & 1u);
    int dualshock_cross =
        device == 0 || device->type == TITAN_INPUT_DEVICE_DUALSHOCK2;

    TitanUiAssets_DrawBootLogo(
        TITAN_STARTUP_CORNER_LOGO_X,
        TITAN_STARTUP_CORNER_LOGO_Y,
        TITAN_STARTUP_CORNER_LOGO_WIDTH,
        TITAN_STARTUP_CORNER_LOGO_HEIGHT,
        244, 247, 252, alpha
    );
    TitanFont_DrawTextShadow(
        title_x, 198.0f, title_scale, title, 242, 246, 251, alpha
    );

    if (dualshock_cross)
    {
        /* DS2 welcome affordance: real Cross symbol inside the circular face
         * button, not a text glyph inside the old 64x44 pill. */
        float radius = 16.5f * pulse;
        float center_x = (float)TITAN_UI_WIDTH * 0.5f;
        float center_y = 252.0f;
        float arm = 5.9f * pulse;
        float line = 1.95f * pulse;

        TitanVideo_DrawCircleAlpha(
            center_x, center_y, radius,
            220, 226, 238, alpha
        );
        TitanVideo_DrawCircleAlpha(
            center_x, center_y, radius - 2.0f,
            32, 35, 44,
            (unsigned char)((alpha * 230u) / 255u)
        );
        TitanVideo_DrawLineAlpha(
            center_x - arm, center_y - arm,
            center_x + arm, center_y + arm,
            line, 250, 250, 252, alpha
        );
        TitanVideo_DrawLineAlpha(
            center_x + arm, center_y - arm,
            center_x - arm, center_y + arm,
            line, 250, 250, 252, alpha
        );
    }
    else
    {
        float bw = glyph[0] == 'H' ? 94.0f : 64.0f;
        float bh = 44.0f;
        float bx = ((float)TITAN_UI_WIDTH - bw * pulse) * 0.5f;
        float by = 240.0f;
        float gw = TitanFont_MeasureText(0.64f, glyph);

        TitanVideo_DrawRoundedRectAlpha(
            bx, by, bw * pulse, bh * pulse, 12.0f,
            220, 226, 238, alpha
        );
        TitanVideo_DrawRoundedRectAlpha(
            bx + 2.0f, by + 2.0f,
            bw * pulse - 4.0f, bh * pulse - 4.0f,
            10.0f, 32, 35, 44,
            (unsigned char)((alpha * 230u) / 255u)
        );
        TitanFont_DrawTextShadow(
            bx + (bw * pulse - gw) * 0.5f,
            by + 28.0f,
            0.64f,
            glyph,
            250, 250, 252, alpha
        );
    }

    TitanFont_DrawTextShadow(
        prompt_x, 286.0f, prompt_scale, prompt, 210, 218, 230, alpha
    );
}

static void TitanShellScene_DrawProfileTile(
    float x,
    float y,
    float focus,
    float motion_scale,
    int add_user,
    const TitanUserProfile *profile,
    unsigned char alpha
)
{
    float eased_focus;
    float scale;
    float size;
    float tile_x;
    float tile_y;
    float center_x;
    float label_width;
    const char *label;
    unsigned char glass_alpha;
    unsigned char border_alpha;

    eased_focus = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(focus, 0.0f, 1.0f)
    );
    scale =
        TitanShellScene_Clamp(motion_scale, 0.0f, 1.0f) *
        (1.0f + (eased_focus * 0.020f));
    size = TITAN_PROFILE_TILE_SIZE * scale;
    tile_x = x - ((size - TITAN_PROFILE_TILE_SIZE) * 0.5f);
    tile_y = y - ((size - TITAN_PROFILE_TILE_SIZE) * 0.5f);
    center_x = tile_x + (size * 0.5f);
    glass_alpha = TitanShellScene_Alpha(
        (float)alpha * (0.25f + (eased_focus * 0.18f))
    );
    border_alpha = TitanShellScene_Alpha(
        (float)alpha * (0.38f + (eased_focus * 0.62f))
    );

    if (eased_focus > 0.01f)
    {
        TitanShellScene_DrawOutline(
            tile_x - 5.0f,
            tile_y - 5.0f,
            size + 10.0f,
            size + 10.0f,
            2.0f,
            231,
            42,
            81,
            TitanShellScene_Alpha((float)alpha * eased_focus * 0.18f)
        );
        TitanShellScene_DrawOutline(
            tile_x - 2.0f,
            tile_y - 2.0f,
            size + 4.0f,
            size + 4.0f,
            2.0f,
            236,
            45,
            88,
            TitanShellScene_Alpha((float)alpha * eased_focus * 0.34f)
        );
    }

    TitanVideo_DrawRoundedRectAlpha(
        tile_x,
        tile_y,
        size,
        size,
        8.0f,
        236,
        241,
        248,
        glass_alpha
    );
    TitanShellScene_DrawOutline(
        tile_x,
        tile_y,
        size,
        size,
        2.0f,
        eased_focus > 0.5f ? 244 : 184,
        eased_focus > 0.5f ? 55 : 192,
        eased_focus > 0.5f ? 101 : 205,
        border_alpha
    );

    if (add_user)
    {
        TitanVideo_DrawCircleAlpha(
            center_x,
            tile_y + (size * 0.43f),
            25.0f * scale,
            18,
            22,
            31,
            TitanShellScene_Alpha((float)alpha * 0.52f)
        );
        TitanVideo_DrawRectAlpha(
            center_x - (17.0f * scale),
            tile_y + (size * 0.43f) - (2.0f * scale),
            34.0f * scale,
            4.0f * scale,
            248,
            250,
            255,
            alpha
        );
        TitanVideo_DrawRectAlpha(
            center_x - (2.0f * scale),
            tile_y + (size * 0.43f) - (17.0f * scale),
            4.0f * scale,
            34.0f * scale,
            248,
            250,
            255,
            alpha
        );
        label = "Adicionar usuario";
    }
    else
    {
        if (profile != 0 && TitanAvatars_IsReady())
        {
            TitanAvatars_Draw(
                profile->avatar_id,
                center_x - (34.0f * scale),
                tile_y + (13.0f * scale),
                68.0f * scale,
                alpha
            );
        }
        else
        {
            TitanVideo_DrawCircleAlpha(
                center_x,
                tile_y + (size * 0.43f),
                30.0f * scale,
                22,
                27,
                38,
                TitanShellScene_Alpha((float)alpha * 0.58f)
            );
            TitanIcons_Draw(
                TITAN_ICON_USER,
                center_x,
                tile_y + (size * 0.43f),
                0.78f * scale,
                247,
                249,
                253,
                alpha
            );
        }
        label = profile != 0 ? profile->name : "Perfil";
    }

    label_width = TitanFont_MeasureText(0.54f, label);
    TitanFont_DrawTextShadow(
        center_x - (label_width * 0.5f),
        tile_y + size - 22.0f,
        0.54f,
        label,
        247,
        249,
        253,
        alpha
    );
}

static void TitanShellScene_DrawStartupProfiles(
    float reveal,
    float exit_progress
)
{
    unsigned int profile_count;
    unsigned int tile_count;
    unsigned int tile_index;
    float total_width;
    float start_x;
    float tile_x;
    float tile_y;
    float focus;
    float tile_reveal;
    float tile_alpha_factor;
    float motion_scale;
    float title_reveal;
    float exit_eased;
    float stagger_start;
    float title_width;
    unsigned char title_alpha;
    unsigned char tile_alpha;
    const char *title;

    profile_count = TitanUserProfiles_GetCount();
    tile_count = profile_count;
    if (profile_count < TITAN_USER_PROFILE_CAPACITY)
    {
        tile_count++;
    }

    total_width =
        ((float)tile_count * TITAN_PROFILE_TILE_SIZE) +
        ((float)(tile_count - 1u) * TITAN_PROFILE_TILE_GAP);
    start_x = ((float)TITAN_UI_WIDTH - total_width) * 0.5f;
    title = "Quem esta usando este PS2?";
    title_width = TitanFont_MeasureText(0.94f, title);
    title_reveal = TitanAnimation_EaseOutCubic(
        TitanShellScene_Clamp(reveal / 0.55f, 0.0f, 1.0f)
    );
    exit_eased = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(exit_progress, 0.0f, 1.0f)
    );
    title_alpha = TitanShellScene_Alpha(
        255.0f * title_reveal * (1.0f - exit_eased)
    );

    if (title_alpha > 4u)
    {
        TitanUiAssets_DrawBootLogo(
            TITAN_STARTUP_CORNER_LOGO_X,
            TITAN_STARTUP_CORNER_LOGO_Y,
            TITAN_STARTUP_CORNER_LOGO_WIDTH,
            TITAN_STARTUP_CORNER_LOGO_HEIGHT,
            244,
            247,
            252,
            title_alpha
        );
        TitanFont_DrawTextShadow(
            ((float)TITAN_UI_WIDTH - title_width) * 0.5f,
            112.0f,
            0.94f,
            title,
            245,
            248,
            253,
            title_alpha
        );
    }

    for (tile_index = 0u; tile_index < tile_count; tile_index++)
    {
        int add_user;
        unsigned int profile_index;

        add_user = profile_count < TITAN_USER_PROFILE_CAPACITY && tile_index == 0u;
        profile_index = add_user ? 0u : tile_index - (profile_count < TITAN_USER_PROFILE_CAPACITY ? 1u : 0u);
        tile_x = start_x + ((float)tile_index * (TITAN_PROFILE_TILE_SIZE + TITAN_PROFILE_TILE_GAP));
        stagger_start = (float)tile_index * 0.020f;
        tile_reveal = TitanAnimation_EaseOutCubic(
            TitanShellScene_Clamp(
                (reveal - stagger_start) / 0.45f,
                0.0f,
                1.0f
            )
        );
        tile_alpha_factor = tile_reveal * (1.0f - exit_eased);
        tile_alpha = TitanShellScene_Alpha(255.0f * tile_alpha_factor);
        tile_y = 166.0f;
        motion_scale = 1.0f;
        focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)tile_index - g_scene.startup_profile_position),
            0.0f,
            1.0f
        );
        if (tile_alpha > 4u)
        {
            TitanShellScene_DrawProfileTile(
                tile_x,
                tile_y,
                focus,
                motion_scale,
                add_user,
                add_user ? 0 : TitanUserProfiles_Get(profile_index),
                tile_alpha
            );
        }
    }

    if (title_alpha > 4u)
    {
        TitanFont_DrawTextAlpha(
            38.0f,
            414.0f,
            0.54f,
            "LEFT/RIGHT Navegar    X Confirmar",
            221,
            226,
            235,
            title_alpha
        );
    }
}


static void TitanShellScene_DrawStartupBootLogo(
    unsigned char alpha
)
{
    TitanUiAssets_DrawBootLogo(
        TITAN_BOOT_STAGE_LOGO_X,
        TITAN_BOOT_STAGE_LOGO_Y,
        TITAN_BOOT_STAGE_LOGO_DRAW_WIDTH,
        TITAN_BOOT_STAGE_LOGO_DRAW_HEIGHT,
        244,
        247,
        252,
        alpha
    );
}


static void TitanShellScene_DrawStartupSequence(
    unsigned int frame
)
{
    unsigned char black_alpha;
    unsigned char content_alpha;
    float phase_alpha;
    float reveal;
    float background_reveal;
    float exit_progress;

    if (g_scene.startup_phase == TITAN_STARTUP_PHASE_INITIAL_BLACK)
    {
        float reveal;
        float exit_progress;
        unsigned char logo_alpha;

        TitanVideo_DrawRectAlpha(
            0.0f,
            0.0f,
            (float)TITAN_UI_WIDTH,
            (float)TITAN_UI_HEIGHT,
            0,
            0,
            0,
            255
        );

        reveal = TitanAnimation_EaseOutCubic(
            TitanShellScene_Clamp(
                (float)g_scene.startup_phase_frames / 18.0f,
                0.0f,
                1.0f
            )
        );
        exit_progress = 0.0f;
        if (g_scene.startup_phase_frames > TITAN_STARTUP_BLACK_HOLD_FRAMES - 20u)
        {
            exit_progress = TitanAnimation_SmoothStep(
                TitanShellScene_Clamp(
                    (float)(g_scene.startup_phase_frames - (TITAN_STARTUP_BLACK_HOLD_FRAMES - 20u)) / 20.0f,
                    0.0f,
                    1.0f
                )
            );
        }
        logo_alpha = TitanShellScene_Alpha(255.0f * reveal * (1.0f - exit_progress));
        if (logo_alpha > 4u)
        {
            TitanShellScene_DrawStartupBootLogo(logo_alpha);
        }
        return;
    }

    /* Startup functional surfaces use Ribbon/palette. HOME_ENTER is the one
     * lifecycle boundary where the background may reveal the dynamic scene;
     * opacity itself comes from TitanThemeLifecycle, not an arbitrary timer. */
    TitanShellBackground_Draw(
        frame, 0.0f, 0.0f, 0.0f, 0.0f,
        g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_ENTER ? 0.0f : 1.0f
    );

    if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HEALTH)
    {
        reveal = TitanAnimation_EaseOutCubic(
            TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames,
                TITAN_STARTUP_TEXT_FADE_FRAMES
            )
        );
        background_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames,
                TITAN_STARTUP_BACKGROUND_FADE_FRAMES
            )
        );

        black_alpha = TitanShellScene_Alpha(
            255.0f * (1.0f - background_reveal)
        );
        if (black_alpha > 0u)
        {
            TitanVideo_DrawRectAlpha(
                0.0f,
                0.0f,
                (float)TITAN_UI_WIDTH,
                (float)TITAN_UI_HEIGHT,
                0,
                0,
                0,
                black_alpha
            );
        }

        exit_progress = 0.0f;
        if (
            g_scene.startup_phase_frames >
                TITAN_STARTUP_HEALTH_DURATION_FRAMES -
                TITAN_STARTUP_TEXT_FADE_FRAMES
        )
        {
            exit_progress = TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames -
                    (
                        TITAN_STARTUP_HEALTH_DURATION_FRAMES -
                        TITAN_STARTUP_TEXT_FADE_FRAMES
                    ),
                TITAN_STARTUP_TEXT_FADE_FRAMES
            );
        }
        phase_alpha = reveal * (1.0f - TitanAnimation_SmoothStep(exit_progress));
        content_alpha = TitanShellScene_Alpha(255.0f * phase_alpha);

        if (content_alpha > 4u)
        {
            TitanShellScene_DrawStartupHealthMessage(content_alpha);
        }

        return;
    }

    if (
        g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_ENTER ||
        g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_WAIT ||
        g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_EXIT
    )
    {
        reveal = 1.0f;
        exit_progress = 0.0f;

        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_ENTER)
        {
            reveal = TitanAnimation_EaseOutCubic(
                TitanShellScene_FrameProgress(
                    g_scene.startup_phase_frames,
                    TITAN_STARTUP_WELCOME_ENTER_FRAMES
                )
            );
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_EXIT)
        {
            exit_progress = TitanAnimation_SmoothStep(
                TitanShellScene_FrameProgress(
                    g_scene.startup_phase_frames,
                    TITAN_STARTUP_WELCOME_EXIT_FRAMES
                )
            );
        }

        phase_alpha = reveal * (1.0f - exit_progress);
        content_alpha = TitanShellScene_Alpha(255.0f * phase_alpha);

        if (content_alpha > 4u)
        {
            TitanShellScene_DrawStartupWelcome(content_alpha);
        }

        return;
    }

    if (
        g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_ENTER ||
        g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_WAIT ||
        g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_EXIT
    )
    {
        reveal = 1.0f;
        exit_progress = 0.0f;

        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_ENTER)
        {
            reveal = TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames,
                TITAN_STARTUP_PROFILE_ENTER_FRAMES
            );
        }
        else
        {
            if (g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_EXIT)
            {
                exit_progress = TitanShellScene_FrameProgress(
                    g_scene.startup_phase_frames,
                    TITAN_STARTUP_PROFILE_EXIT_FRAMES
                );
            }
        }

        TitanShellScene_DrawStartupProfiles(reveal, exit_progress);
    }
}


static void TitanShellScene_DrawDiagnosticsOperationStatus(
    unsigned int frame,
    unsigned char alpha
)
{
    TitanDiagnosticsOperationStatus status;
    char heading[48];
    char message[64];
    float box_x = (float)TITAN_UI_WIDTH - 396.0f;
    float box_y = 12.0f;
    float box_width = 194.0f;
    float box_height = 32.0f;
    unsigned char border_red;
    unsigned char border_green;
    unsigned char border_blue;

    /* DEX mode, not the optional large telemetry panel, owns this live ribbon. */
    if (!TitanDex_IsEnabled() ||
        !TitanDiagnostics_GetOperationStatus(&status) || !status.active)
        return;

    snprintf(heading, sizeof(heading), "%s / %s", status.area, status.stage);
    TitanShellScene_BuildTickerText(status.message, frame / 6u, 26u,
        message, sizeof(message));

    if (status.level == TITAN_DIAG_ERROR)
    {
        border_red = 255u; border_green = 65u; border_blue = 82u;
    }
    else if (status.level == TITAN_DIAG_WARN)
    {
        border_red = 255u; border_green = 194u; border_blue = 72u;
    }
    else
    {
        border_red = 222u; border_green = 43u; border_blue = 120u;
    }

    TitanVideo_DrawRoundedRectAlpha(box_x, box_y, box_width, box_height,
        4.0f, 9u, 10u, 16u, TitanShellScene_ModulateAlpha(232u, alpha));
    TitanShellScene_DrawOutline(box_x, box_y, box_width, box_height, 1.0f,
        border_red, border_green, border_blue,
        TitanShellScene_ModulateAlpha(245u, alpha));
    TitanFont_DrawTextAlpha(box_x + 7.0f, box_y + 3.0f, 0.34f,
        heading, 190u, 198u, 212u, alpha);
    TitanFont_DrawTextAlpha(box_x + 7.0f, box_y + 15.0f, 0.43f,
        message, 248u, 249u, 252u, alpha);
    if (status.progress_percent >= 0)
    {
        float progress_width = (box_width - 2.0f) *
            ((float)status.progress_percent / 100.0f);
        if (progress_width > 0.0f)
            TitanVideo_DrawRectAlpha(box_x + 1.0f, box_y + box_height - 2.0f,
                progress_width, 1.0f, border_red, border_green, border_blue, alpha);
    }
}

static void TitanShellScene_DrawHeader(
    unsigned int frame,
    const TitanInputState *input,
    unsigned char alpha
)
{
    const char *clock_text;
    const char *header_status;
    float clock_width;
    float clock_x;
    int clock_services_ready;
    int shell_busy;
    const TitanUserProfile *active_profile;
    float profile_name_x;
    float profile_name_width;
    char ticker_text[40];
    unsigned int ticker_offset;
    const TitanThemeShellStyle *theme_style;

    theme_style = TitanTheme_GetShellStyle();
    alpha = TitanShellScene_Alpha(
        (float)alpha * TitanShellScene_Clamp(theme_style->header_alpha, 0.0f, 1.0f)
    );

    TitanVideo_DrawRectAlpha(
        0.0f, 0.0f,
        (float)TITAN_UI_WIDTH, 54.0f,
        0, 0, 0,
        TitanShellScene_ModulateAlpha(132u, alpha)
    );
    TitanVideo_DrawRectAlpha(
        0.0f, 52.0f,
        (float)TITAN_UI_WIDTH, 1.0f,
        theme_style->accent.red,
        theme_style->accent.green,
        theme_style->accent.blue,
        TitanShellScene_ModulateAlpha(116u, alpha)
    );

    clock_services_ready = 0;

    if (input != 0)
    {
        clock_services_ready =
            input->bootstrap_stage ==
                TITAN_INPUT_BOOTSTRAP_READY ||
            input->bootstrap_stage ==
                TITAN_INPUT_BOOTSTRAP_FAILED;
    }

    shell_busy =
        TitanShellBusy_IsActive() ||
        TitanStorageScheduler_IsForegroundBusy() ||
        TitanStorageScheduler_IsLibraryScanBusy() ||
        TitanAppAssets_IsSelectionLoading();

    if (!shell_busy)
    {
        TitanSystemClock_Update(
            frame,
            clock_services_ready
        );

        clock_text = TitanSystemClock_GetText();
        clock_width = TitanFont_MeasureText(
            0.98f,
            clock_text
        );

        /* Keep a defensive fallback because a stale or not-yet-ready font metric
           must never push the clock against the physical right-edge crop. */
        if (clock_width < 20.0f || clock_width > 150.0f)
        {
            clock_width = 82.0f;
        }

        clock_x =
            (float)TITAN_UI_WIDTH -
            70.0f -
            clock_width;
    }

    active_profile = TitanUserProfiles_GetActive();

    if (
        active_profile != 0 &&
        g_scene.page_transition_target < 0.5f &&
        !g_scene.top_menu_active
    )
    {
        TitanIcons_Draw(
            TITAN_ICON_RUNE,
            25.0f,
            26.0f,
            0.50f,
            245,
            192,
            39,
            alpha
        );

        TitanIcons_Draw(
            TITAN_ICON_NOTIFICATIONS,
            54.0f,
            26.0f,
            0.44f,
            243,
            244,
            248,
            TitanShellScene_ModulateAlpha(235u, alpha)
        );

        header_status = TitanNotifications_GetHeaderStatus();
        if (header_status != 0 && header_status[0] != '\0')
        {
            ticker_offset = frame / 6u;

            TitanShellScene_BuildTickerText(
                header_status,
                ticker_offset,
                16u,
                ticker_text,
                sizeof(ticker_text)
            );

            TitanFont_DrawTextAlpha(
                70.0f,
                18.0f,
                0.76f,
                ticker_text,
                227,
                229,
                236,
                TitanShellScene_ModulateAlpha(226u, alpha)
            );
        }

        if (TitanAvatars_IsReady())
        {
            TitanAvatars_Draw(
                active_profile->avatar_id,
                TITAN_SHELL_HEADER_USER_X - 9.0f,
                TITAN_SHELL_HEADER_USER_Y - 9.0f,
                18.0f,
                alpha
            );
        }
        else
        {
            TitanIcons_Draw(
                TITAN_ICON_USER,
                TITAN_SHELL_HEADER_USER_X,
                TITAN_SHELL_HEADER_USER_Y,
                0.40f,
                245,
                246,
                250,
                alpha
            );
        }

        profile_name_width = TitanFont_MeasureText(
            0.78f,
            active_profile->name
        );
        if (profile_name_width < 10.0f)
        {
            profile_name_width = 52.0f;
        }
        profile_name_x = TITAN_SHELL_HEADER_USER_X + 18.0f;

        TitanFont_DrawTextShadow(
            profile_name_x,
            17.0f,
            0.78f,
            active_profile->name,
            244,
            245,
            249,
            alpha
        );
    }

    if (shell_busy)
    {
        TitanShellScene_DrawShutdownSymbolCluster(
            frame,
            (float)TITAN_UI_WIDTH - 111.0f,
            26.0f,
            alpha
        );
    }
    else
    {
        TitanFont_DrawTextShadow(
            clock_x,
            13.0f,
            0.98f,
            clock_text,
            TitanSystemClock_IsValid() ? 252 : 213,
            TitanSystemClock_IsValid() ? 252 : 216,
            TitanSystemClock_IsValid() ? 255 : 222,
            alpha
        );
    }

    TitanShellScene_DrawDiagnosticsOperationStatus(frame, alpha);
}

static void TitanShellScene_DrawFolderGlyph(
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float x;
    float y;
    float tab_width;
    float tab_height;

    x = center_x - (width * 0.5f);
    y = center_y - (height * 0.5f);
    tab_width = width * 0.43f;
    tab_height = height * 0.22f;

    TitanVideo_DrawRoundedRectAlpha(
        x,
        y + tab_height * 0.70f,
        width,
        height - (tab_height * 0.70f),
        2.0f,
        red,
        green,
        blue,
        alpha
    );
    TitanVideo_DrawRoundedRectAlpha(
        x + 2.0f,
        y,
        tab_width,
        tab_height,
        2.0f,
        red,
        green,
        blue,
        alpha
    );
}

static int TitanShellScene_DrawFolderKeyVisual(
    TitanHomeGameKey key,
    float center_x,
    float center_y,
    float size,
    unsigned char alpha
)
{
    unsigned int shell_index;
    const TitanShellItem *shell_item;

    if (TitanShellScene_FindShellByKey(key, &shell_index, &shell_item))
    {
        (void)shell_index;
        TitanVideo_DrawRectAlpha(
            center_x - (size * 0.5f),
            center_y - (size * 0.5f),
            size,
            size,
            22,
            23,
            30,
            TitanShellScene_ModulateAlpha(210u, alpha)
        );
        TitanIcons_Draw(
            shell_item->icon,
            center_x,
            center_y,
            size > 60.0f ? 0.66f : 0.42f,
            244,
            247,
            252,
            alpha
        );
        return 1;
    }

    if (TitanShellScene_FindGameByKey(key, 0))
    {
        TitanUiAssets_DrawGamePlaceholder(
            center_x,
            center_y,
            size,
            size,
            236,
            240,
            248,
            alpha
        );
        return 1;
    }

    return 0;
}

static void TitanShellScene_DrawFolderMosaic(
    float center_x,
    float center_y,
    float size,
    unsigned int folder_index,
    unsigned char alpha
)
{
    const TitanHomeFolder *folder;
    unsigned int member_count;
    float half;
    float gap;
    float cell;
    float left;
    float top;

    folder = TitanHomeFolders_Get(folder_index);
    member_count = folder != 0 ? folder->item_count : 0u;
    half = size * 0.5f;
    gap = 2.0f;
    cell = half - gap;
    left = center_x - half;
    top = center_y - half;

    TitanVideo_DrawRectAlpha(
        left,
        top,
        size,
        size,
        13,
        14,
        20,
        TitanShellScene_ModulateAlpha(alpha, 238u)
    );

    if (member_count > 0u)
    {
        (void)TitanShellScene_DrawFolderKeyVisual(
            folder->item_keys[0],
            left + (cell * 0.5f),
            top + (cell * 0.5f),
            cell,
            alpha
        );
    }
    if (member_count > 1u)
    {
        (void)TitanShellScene_DrawFolderKeyVisual(
            folder->item_keys[1],
            left + half + gap + (cell * 0.5f),
            top + (cell * 0.5f),
            cell,
            alpha
        );
    }
    if (member_count > 2u)
    {
        (void)TitanShellScene_DrawFolderKeyVisual(
            folder->item_keys[2],
            left + (cell * 0.5f),
            top + half + gap + (cell * 0.5f),
            cell,
            alpha
        );
    }

    TitanShellScene_DrawFolderGlyph(
        left + half + gap + (cell * 0.5f),
        top + half + gap + (cell * 0.5f),
        cell * 0.62f,
        cell * 0.48f,
        250,
        250,
        252,
        alpha
    );
}

static int TitanShellScene_GetDexMetadataForHomeEntry(
    const TitanHomeEntry *item,
    char *text,
    unsigned int capacity
)
{
    const char *title_id;
    const char *version;

    if (
        item == 0 || text == 0 || capacity == 0u ||
        !TitanDex_IsEnabled() || item->type == TITAN_HOME_ENTRY_FOLDER
    )
    {
        return 0;
    }

    title_id = 0;
    version = 0;

    if (item->type == TITAN_HOME_ENTRY_GAME)
    {
        const TitanNeutrinoGame *game;

        game = TitanNeutrino_GetGame(item->game_index);
        if (game != 0)
        {
            title_id = game->startup;
            version = game->version;
        }
    }
    else if (item->type == TITAN_HOME_ENTRY_SHELL)
    {
        const TitanShellItem *shell_item;

        shell_item = item->shell_is_dex
            ? TitanShellCatalog_GetDex(item->shell_catalog_index)
            : TitanShellCatalog_Get(item->shell_catalog_index);
        if (shell_item != 0)
        {
            title_id = shell_item->title_id;
            version = shell_item->version;
        }
    }

    if (title_id == 0 || title_id[0] == '\0')
    {
        return 0;
    }

    if (version != 0 && version[0] != '\0')
    {
        snprintf(text, capacity, "%s v%s", title_id, version);
    }
    else
    {
        snprintf(text, capacity, "%s v?", title_id);
    }
    text[capacity - 1u] = '\0';
    return 1;
}

static int TitanShellScene_GetDexMetadataForFolderKey(
    TitanHomeGameKey key,
    char *text,
    unsigned int capacity
)
{
    unsigned int shell_index;
    const TitanShellItem *shell_item;
    unsigned int game_index;
    const TitanNeutrinoGame *game;

    if (text == 0 || capacity == 0u || !TitanDex_IsEnabled())
    {
        return 0;
    }

    if (TitanShellScene_FindShellByKey(key, &shell_index, &shell_item))
    {
        (void)shell_index;
        if (shell_item == 0 || shell_item->title_id == 0 ||
            shell_item->title_id[0] == '\0')
        {
            return 0;
        }
        if (shell_item->version != 0 && shell_item->version[0] != '\0')
        {
            snprintf(
                text,
                capacity,
                "%s v%s",
                shell_item->title_id,
                shell_item->version
            );
        }
        else
        {
            snprintf(text, capacity, "%s v?", shell_item->title_id);
        }
        text[capacity - 1u] = '\0';
        return 1;
    }

    if (!TitanShellScene_FindGameByKey(key, &game_index))
    {
        return 0;
    }
    game = TitanNeutrino_GetGame(game_index);
    if (game == 0 || game->startup[0] == '\0')
    {
        return 0;
    }

    if (game->version[0] != '\0')
    {
        snprintf(text, capacity, "%s v%s", game->startup, game->version);
    }
    else
    {
        snprintf(text, capacity, "%s v?", game->startup);
    }
    text[capacity - 1u] = '\0';
    return 1;
}

static void TitanShellScene_DrawDexMetadataStrip(
    float x,
    float y,
    float width,
    float height,
    const char *metadata,
    unsigned char alpha
)
{
    float strip_height;
    float text_scale;
    float text_width;
    float text_x;
    float text_y;

    if (metadata == 0 || metadata[0] == '\0' || width < 38.0f || height < 30.0f)
    {
        return;
    }

    strip_height = height > 110.0f ? 15.0f : 11.0f;
    text_scale = height > 110.0f ? 0.34f : 0.27f;

    TitanVideo_DrawRectAlpha(
        x + 1.0f,
        y + height - strip_height,
        width - 2.0f,
        strip_height - 1.0f,
        5,
        7,
        12,
        TitanShellScene_ModulateAlpha(215u, alpha)
    );

    text_width = TitanFont_MeasureText(text_scale, metadata);
    if (text_width > width - 8.0f)
    {
        text_scale *= (width - 8.0f) / text_width;
        if (text_scale < 0.20f)
        {
            text_scale = 0.20f;
        }
        text_width = TitanFont_MeasureText(text_scale, metadata);
    }
    text_x = x + ((width - text_width) * 0.5f);
    text_y = y + height - strip_height + 1.5f;

    TitanFont_DrawTextAlpha(
        text_x,
        text_y,
        text_scale,
        metadata,
        244,
        246,
        250,
        alpha
    );
}

static void TitanShellScene_DrawTile(
    const TitanHomeEntry *item,
    float x,
    float y,
    float width,
    float height,
    float focus,
    int selected,
    int suppress_focus_frame,
    float layout_scale,
    float action_visibility,
    unsigned char alpha
)
{
    float clamped_focus;
    float icon_scale;
    float icon_size;
    float icon_y;
    float content_height;
    float bar_reveal;
    float bar_text_reveal;
    float bar_height;
    float bar_y;
    float start_label_scale;
    float start_label_width;
    float outline_inset;
    float outline_thickness;
    float glow_phase;
    float glow_pulse;
    float glow_strength;
    unsigned char glow_inner_alpha;
    unsigned char start_label_alpha;
    unsigned char bar_alpha;
    unsigned char outline_alpha;
    unsigned char border_red;
    unsigned char border_green;
    unsigned char border_blue;
    char dex_metadata[64];
    int show_dex_metadata;
    const TitanThemeShellStyle *theme_style;

    if (item == 0)
    {
        return;
    }

    theme_style = TitanTheme_GetShellStyle();

    clamped_focus = TitanShellScene_Clamp(
        focus,
        0.0f,
        1.0f
    );

    border_red = (unsigned char)(
        45.0f + (clamped_focus * ((float)theme_style->accent.red - 45.0f))
    );

    border_green = (unsigned char)(
        50.0f + (clamped_focus * ((float)theme_style->accent.green - 50.0f))
    );

    border_blue = (unsigned char)(
        69.0f + (clamped_focus * ((float)theme_style->accent.blue - 69.0f))
    );

    bar_reveal = 0.0f;
    bar_height = 0.0f;

    if (selected)
    {
        bar_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(
                (clamped_focus - 0.08f) / 0.92f,
                0.0f,
                1.0f
            )
        ) * TitanShellScene_Clamp(
            action_visibility,
            0.0f,
            1.0f
        );

        bar_height =
            TITAN_SHELL_START_BAR_HEIGHT *
            bar_reveal *
            layout_scale;
    }

    content_height = height - bar_height;
    show_dex_metadata = TitanShellScene_GetDexMetadataForHomeEntry(
        item,
        dex_metadata,
        sizeof(dex_metadata)
    );

    /*
     * Visual cleanup:
     * - tiles não selecionadas sem fundo;
     * - tile selecionada sem véu escuro;
     * - foco/outline preservado.
     */
    bar_alpha = TitanShellScene_Alpha(
        (float)alpha *
        (0.52f + (clamped_focus * 0.20f))
    );

    outline_alpha = TitanShellScene_Alpha(
        (float)alpha *
        (0.46f + (clamped_focus * 0.54f))
    );

    glow_phase =
        (float)(g_scene.local_frame % 96u) /
        48.0f;

    if (glow_phase > 1.0f)
    {
        glow_phase = 2.0f - glow_phase;
    }

    glow_pulse = TitanAnimation_SmoothStep(glow_phase);
    glow_strength =
        clamped_focus *
        TitanShellScene_Clamp(action_visibility, 0.0f, 1.0f);

    if (selected && !suppress_focus_frame && glow_strength > 0.04f)
    {
        glow_inner_alpha = TitanShellScene_Alpha(
            (float)alpha *
            glow_strength *
            (0.14f + (glow_pulse * 0.14f))
        );

        TitanShellScene_DrawOutline(
            x - 3.0f,
            y - 3.0f,
            width + 6.0f,
            height + 6.0f,
            2.0f,
            theme_style->accent.red,
            theme_style->accent.green,
            theme_style->accent.blue,
            glow_inner_alpha
        );

        border_red = (unsigned char)(
            (float)theme_style->accent.red +
            ((255.0f - (float)theme_style->accent.red) * glow_pulse * 0.28f)
        );
    }

    if (selected && !suppress_focus_frame)
    {
        TitanVideo_DrawRectAlpha(
            x + 2.0f,
            y + 2.0f,
            width - 4.0f,
            1.0f + (height * 0.10f),
            232,
            235,
            244,
            TitanShellScene_Alpha(
                (float)alpha *
                (0.012f + (clamped_focus * 0.020f))
            )
        );
    }

    if (bar_height > 1.0f)
    {
        bar_y = y + height - bar_height;

        TitanVideo_DrawRectAlpha(
            x + 3.0f,
            bar_y,
            width - 6.0f,
            bar_height,
            19,
            23,
            38,
            bar_alpha
        );

        TitanVideo_DrawRectAlpha(
            x + 3.0f,
            bar_y,
            width - 6.0f,
            2.0f,
            theme_style->accent.red,
            theme_style->accent.green,
            theme_style->accent.blue,
            outline_alpha
        );

        bar_text_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(
                (bar_reveal - 0.42f) / 0.58f,
                0.0f,
                1.0f
            )
        );

        start_label_alpha = TitanShellScene_Alpha(
            (float)alpha * bar_text_reveal
        );

        if (start_label_alpha > 4)
        {
            if (
                item->page == TITAN_SHELL_PAGE_OVERVIEW ||
                item->type == TITAN_HOME_ENTRY_FOLDER
            )
            {
                float arrow_center_x;
                float arrow_center_y;
                float arrow_size;
                unsigned char arrow_shadow_alpha;

                arrow_center_x = x + (width * 0.5f);
                arrow_center_y = bar_y + (bar_height * 0.52f);
                arrow_size = 5.8f * layout_scale;
                arrow_shadow_alpha = TitanShellScene_Alpha((float)start_label_alpha * 0.42f);

                TitanVideo_DrawLineAlpha(
                    arrow_center_x + 0.5f,
                    arrow_center_y - (arrow_size * 0.90f) + 0.5f,
                    arrow_center_x + 0.5f,
                    arrow_center_y + (arrow_size * 0.02f) + 0.5f,
                    1.8f,
                    10,
                    12,
                    18,
                    arrow_shadow_alpha
                );

                TitanVideo_DrawLineAlpha(
                    arrow_center_x - (arrow_size * 0.68f) + 0.5f,
                    arrow_center_y - (arrow_size * 0.12f) + 0.5f,
                    arrow_center_x + 0.5f,
                    arrow_center_y + (arrow_size * 0.60f) + 0.5f,
                    1.8f,
                    10,
                    12,
                    18,
                    arrow_shadow_alpha
                );

                TitanVideo_DrawLineAlpha(
                    arrow_center_x + (arrow_size * 0.68f) + 0.5f,
                    arrow_center_y - (arrow_size * 0.12f) + 0.5f,
                    arrow_center_x + 0.5f,
                    arrow_center_y + (arrow_size * 0.60f) + 0.5f,
                    1.8f,
                    10,
                    12,
                    18,
                    arrow_shadow_alpha
                );

                TitanVideo_DrawLineAlpha(
                    arrow_center_x,
                    arrow_center_y - (arrow_size * 0.95f),
                    arrow_center_x,
                    arrow_center_y,
                    1.4f,
                    241,
                    243,
                    248,
                    start_label_alpha
                );

                TitanVideo_DrawLineAlpha(
                    arrow_center_x - (arrow_size * 0.66f),
                    arrow_center_y - (arrow_size * 0.18f),
                    arrow_center_x,
                    arrow_center_y + (arrow_size * 0.52f),
                    1.4f,
                    241,
                    243,
                    248,
                    start_label_alpha
                );

                TitanVideo_DrawLineAlpha(
                    arrow_center_x + (arrow_size * 0.66f),
                    arrow_center_y - (arrow_size * 0.18f),
                    arrow_center_x,
                    arrow_center_y + (arrow_size * 0.52f),
                    1.4f,
                    241,
                    243,
                    248,
                    start_label_alpha
                );
            }
            else
            {
                const char *start_label = TitanLocalization_Compat("Start");
                start_label_scale = 0.54f * layout_scale;
                start_label_width = TitanFont_MeasureText(
                    start_label_scale,
                    start_label
                );

                TitanFont_DrawTextAlpha(
                    x + ((width - start_label_width) * 0.5f),
                    bar_y + ((bar_height - 13.0f) * 0.5f),
                    start_label_scale,
                    start_label,
                    241,
                    243,
                    248,
                    start_label_alpha
                );
            }
        }
    }

    icon_scale =
        (0.82f +
        (clamped_focus * 0.92f)) *
        layout_scale;

    icon_y =
        y +
        (content_height * 0.50f);

    if (item->type == TITAN_HOME_ENTRY_GAME)
    {
        /* BF12 game-art layer: downloaded artwork fills the visual tile instead
         * of being rendered as a small centered stamp. The selected tile uses
         * the resident 128x128 TIMG for every Home game tile. The fast atlas
         * remains a fallback/Library path and is derived from the same source. */
        float art_x = x + 2.0f;
        float art_y = y + 2.0f;
        float art_w = width - 4.0f;
        float art_h = content_height - 4.0f;
        int art_drawn = TitanAssetService_DrawIconHighQuality(
            item->game_index, art_x, art_y, art_w, art_h, alpha);
        /* Real ICO artwork fills the tile. The normal placeholder deliberately
         * stays at its original compact Home size. */
        icon_size = (46.0f + (clamped_focus * 48.0f)) * layout_scale;
        if (!art_drawn)
        {
            TitanUiAssets_DrawGamePlaceholder(
                x + (width * 0.5f),
                icon_y,
                icon_size,
                icon_size,
                clamped_focus > 0.50f ? 248 : 207,
                clamped_focus > 0.50f ? 249 : 211,
                clamped_focus > 0.50f ? 252 : 220,
                alpha
            );
        }
    }
    else if (item->type == TITAN_HOME_ENTRY_FOLDER)
    {
        icon_size =
            (52.0f + (clamped_focus * 54.0f)) *
            layout_scale;

        TitanShellScene_DrawFolderMosaic(
            x + (width * 0.5f),
            icon_y,
            icon_size,
            item->folder_index,
            alpha
        );
    }
    else if (item->type == TITAN_HOME_ENTRY_APPLICATION)
    {
        int tile_drawn = 0;

        /* The selected app uses its dedicated 256x256 artwork across the full
         * live-area surface. Non-selected apps retain the compact legacy icon
         * atlas, so HOME never allocates 57 large tiles at once. */
        if (clamped_focus > 0.50f)
        {
            tile_drawn = TitanAppAssets_DrawSelectedTile(
                item->application_index,
                x + 2.0f,
                y + 2.0f,
                width - 4.0f,
                content_height - 4.0f,
                alpha
            );
        }

        if (!tile_drawn)
        {
            icon_size =
                (46.0f + (clamped_focus * 48.0f)) *
                layout_scale;
            if (!TitanAppAssets_DrawIcon(
                    item->application_index,
                    x + (width * 0.5f) - (icon_size * 0.5f),
                    icon_y - (icon_size * 0.5f),
                    icon_size,
                    icon_size,
                    alpha))
            {
                TitanIcons_Draw(
                    item->icon,
                    x + (width * 0.5f),
                    icon_y,
                    icon_scale,
                    clamped_focus > 0.50f ? 248 : 207,
                    clamped_focus > 0.50f ? 249 : 211,
                    clamped_focus > 0.50f ? 252 : 220,
                    alpha
                );
            }
        }
    }
    else
    {
        TitanIcons_Draw(
            item->icon,
            x + (width * 0.5f),
            icon_y,
            icon_scale,
            clamped_focus > 0.50f ? 248 : 207,
            clamped_focus > 0.50f ? 249 : 211,
            clamped_focus > 0.50f ? 252 : 220,
            alpha
        );
    }

    /* Z-order contract: game art first, Title ID/DEX strip above the art,
     * focus frame last. The strip remains physically at the tile bottom;
     * "above" here is strictly the render layer requested by the UI design. */
    if (show_dex_metadata)
    {
        TitanShellScene_DrawDexMetadataStrip(
            x,
            y,
            width,
            content_height,
            dex_metadata,
            alpha
        );
    }

    /* Draw the focus frame last and one pixel inside the tile. This keeps
       every edge visible even while the selected tile changes size. */
    outline_inset = 1.0f;
    outline_thickness =
        1.0f +
        (clamped_focus * 2.0f);

    if (!suppress_focus_frame)
    {
        TitanShellScene_DrawOutline(
            x + outline_inset,
            y + outline_inset,
            width - (outline_inset * 2.0f),
            height - (outline_inset * 2.0f),
            outline_thickness,
            border_red,
            border_green,
            border_blue,
            outline_alpha
        );
    }
}

static void TitanShellScene_DrawSystemMenu(
    float offset_x,
    unsigned char alpha
)
{
    unsigned int item_count;
    unsigned int index;
    float transition;
    unsigned char menu_alpha;
    float total_width;
    float start_x;
    float base_y;

    transition = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(
            g_scene.top_menu_transition,
            0.0f,
            1.0f
        )
    );

    menu_alpha = TitanShellScene_Alpha(
        (float)alpha * transition
    );

    if (menu_alpha <= 4)
    {
        return;
    }

    item_count = TitanShellCatalog_GetSystemCount();

    if (item_count == 0u)
    {
        return;
    }

    total_width =
        (float)(item_count - 1u) *
        TITAN_SHELL_SYSTEM_MENU_ITEM_STEP;

    start_x =
        (((float)TITAN_UI_WIDTH - total_width) * 0.5f) +
        offset_x;

    base_y =
        TITAN_SHELL_SYSTEM_MENU_Y -
        ((1.0f - transition) * 54.0f);

    for (index = 0u; index < item_count; index++)
    {
        const TitanShellItem *item;
        float item_x;
        float item_y;
        float focus_distance;
        float focus;
        float icon_scale;
        float label_width;
        unsigned char icon_alpha;
        unsigned char label_alpha;

        item = TitanShellCatalog_GetSystem(
            TitanShellLayout_GetSystemCatalogIndex(index)
        );

        if (item == 0)
        {
            continue;
        }

        focus_distance = TitanAnimation_Absolute(
            (float)index -
            g_scene.top_menu_selected_position
        );

        focus = 1.0f - TitanShellScene_Clamp(
            focus_distance,
            0.0f,
            1.0f
        );

        focus = TitanAnimation_SmoothStep(focus);

        item_x =
            start_x +
            ((float)index * TITAN_SHELL_SYSTEM_MENU_ITEM_STEP);

        item_y = base_y - (focus * 7.0f);
        icon_scale = 0.50f + (focus * 0.24f);
        icon_alpha = TitanShellScene_Alpha(
            (float)menu_alpha *
            (0.68f + (focus * 0.32f))
        );

        if (focus > 0.01f)
        {
            TitanVideo_DrawCircleAlpha(
                item_x,
                item_y,
                23.0f + (focus * 5.0f),
                202,
                32,
                56,
                TitanShellScene_Alpha(
                    (float)menu_alpha *
                    focus *
                    0.13f
                )
            );
        }

        TitanIcons_Draw(
            item->icon,
            item_x,
            item_y,
            icon_scale,
            focus > 0.50f ? 255 : 224,
            focus > 0.50f ? 255 : 227,
            focus > 0.50f ? 255 : 234,
            icon_alpha
        );

        label_alpha = TitanShellScene_Alpha(
            (float)menu_alpha * focus
        );

        if (label_alpha > 4)
        {
            label_width = TitanFont_MeasureText(
                0.70f,
                item->title
            );

            TitanFont_DrawTextShadow(
                item_x - (label_width * 0.5f),
                base_y + 38.0f,
                0.70f,
                item->title,
                250,
                251,
                254,
                label_alpha
            );
        }
    }
}

static void TitanShellScene_DrawOrbisButton(
    float x,
    float y,
    float width,
    float height,
    const char *label,
    int selected,
    unsigned char alpha
)
{
    float text_width;
    unsigned char border_alpha;

    /* Folder child screens sit directly on the animated shell wave.  Do not
     * paint opaque/black option cards over it: focus is expressed only by the
     * Orbis-style outline and magenta accent. */
    if (selected)
    {
        TitanVideo_DrawRectAlpha(
            x,
            y,
            width,
            height,
            238,
            45,
            88,
            TitanShellScene_ModulateAlpha(22u, alpha)
        );
    }

    border_alpha = TitanShellScene_ModulateAlpha(selected ? 255u : 150u, alpha);
    TitanShellScene_DrawOutline(
        x,
        y,
        width,
        height,
        selected ? 2.4f : 1.2f,
        selected ? 238 : 170,
        selected ? 45 : 174,
        selected ? 88 : 186,
        border_alpha
    );

    text_width = TitanFont_MeasureText(0.64f, label);
    TitanFont_DrawTextAlpha(
        x + ((width - text_width) * 0.5f),
        y + ((height - 14.0f) * 0.5f),
        0.64f,
        label,
        246,
        248,
        252,
        alpha
    );
}

static int TitanShellScene_BuildFolderItemEntry(
    TitanHomeGameKey key,
    TitanHomeEntry *entry
)
{
    unsigned int shell_index;
    const TitanShellItem *shell_item;
    unsigned int game_index;
    const TitanNeutrinoGame *game;

    if (entry == 0)
    {
        return 0;
    }
    memset(entry, 0, sizeof(*entry));
    entry->shell_catalog_index = TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;

    if (TitanShellScene_FindShellByKey(key, &shell_index, &shell_item))
    {
        if (shell_item == 0)
        {
            return 0;
        }
        entry->type = TITAN_HOME_ENTRY_SHELL;
        entry->title = shell_item->title;
        entry->subtitle = shell_item->subtitle;
        entry->status = shell_item->status;
        entry->icon = shell_item->icon;
        entry->page = shell_item->page;
        entry->shell_catalog_index = shell_index;
        entry->shell_is_dex = 0;
        return 1;
    }

    if (!TitanShellScene_FindGameByKey(key, &game_index))
    {
        return 0;
    }
    game = TitanNeutrino_GetGame(game_index);
    if (game == 0)
    {
        return 0;
    }

    entry->type = TITAN_HOME_ENTRY_GAME;
    entry->title = game->title;
    entry->subtitle = game->startup[0] != '\0' ? game->startup : "Jogo PS2";
    entry->status = game->format == TITAN_NEUTRINO_GAME_USBEXTREME
        ? "Titan UL Engine"
        : "Titan-Neutrino Engine";
    entry->icon = TITAN_ICON_APPLICATIONS;
    entry->page = TITAN_SHELL_PAGE_HOME;
    entry->game_index = game_index;
    return 1;
}

static void TitanShellScene_DrawFolderOpenRow(
    unsigned int folder_index,
    unsigned char alpha
)
{
    const TitanHomeFolder *folder;
    unsigned int visible_count;
    unsigned int start_index;
    unsigned int index;
    float transition;
    float active_transition;
    float preview_size;
    float preview_start_x;
    float preview_y;
    float widths[7];
    float heights[7];
    float centers[7];
    float active_x[7];
    float focus[7];
    float track_focus_center;
    float track_offset_x;
    float local_selected_position;
    unsigned int local_selected_index;
    float local_fraction;
    TitanHomeEntry selected_entry;
    float selected_draw_x;
    float selected_draw_y;
    float selected_draw_width;
    float selected_draw_height;
    int selected_entry_valid;
    float launch_other_visibility;
    float launch_selected_visibility;
    float launch_label_visibility;
    float launch_stage_progress;

    folder = TitanHomeFolders_Get(folder_index);
    if (folder == 0 || folder->item_count == 0u)
    {
        return;
    }

    transition = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(g_scene.folder_open_transition, 0.0f, 1.0f)
    );
    active_transition = transition;
    launch_other_visibility = 1.0f;
    launch_selected_visibility = 1.0f;
    launch_label_visibility = 1.0f;
    launch_stage_progress = 0.0f;
    if (g_scene.game_launch_active)
    {
        launch_other_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_OTHER_FADE_START,
            TITAN_GAME_LAUNCH_OTHER_FADE_END
        );
        launch_selected_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_SELECTED_FADE_START,
            TITAN_GAME_LAUNCH_SELECTED_FADE_END
        );
        launch_label_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_LABEL_FADE_START,
            TITAN_GAME_LAUNCH_LABEL_FADE_END
        );
        launch_stage_progress = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_STAGE_IN_START,
            TITAN_GAME_LAUNCH_STAGE_IN_END
        );
    }

    visible_count = folder->item_count < 7u ? folder->item_count : 7u;
    start_index = 0u;
    if (
        folder->item_count > visible_count &&
        g_scene.folder_open_selected >= (int)visible_count
    )
    {
        start_index =
            (unsigned int)g_scene.folder_open_selected - visible_count + 1u;
    }

    preview_size = TITAN_FOLDER_NAV_PREVIEW_SIZE;
    preview_start_x = 145.0f;
    preview_y = TITAN_FOLDER_NAV_PREVIEW_TOP;

    local_selected_position =
        g_scene.folder_open_selected_position - (float)start_index;
    if (local_selected_position < 0.0f)
    {
        local_selected_position = 0.0f;
    }
    if (local_selected_position > (float)(visible_count - 1u))
    {
        local_selected_position = (float)(visible_count - 1u);
    }

    for (index = 0u; index < visible_count; index++)
    {
        float distance;

        distance = TitanAnimation_Absolute(
            (float)index - local_selected_position
        );
        focus[index] = TitanAnimation_SmoothStep(
            1.0f - TitanShellScene_Clamp(distance, 0.0f, 1.0f)
        );
        widths[index] =
            TITAN_FOLDER_NAV_SMALL_WIDTH +
            (focus[index] *
                (TITAN_FOLDER_NAV_FOCUSED_WIDTH - TITAN_FOLDER_NAV_SMALL_WIDTH));
        heights[index] =
            TITAN_FOLDER_NAV_SMALL_HEIGHT +
            (focus[index] *
                (TITAN_FOLDER_NAV_FOCUSED_HEIGHT - TITAN_FOLDER_NAV_SMALL_HEIGHT));
    }

    centers[0] = widths[0] * 0.5f;
    for (index = 1u; index < visible_count; index++)
    {
        centers[index] =
            centers[index - 1u] +
            (widths[index - 1u] * 0.5f) +
            TITAN_FOLDER_NAV_GAP +
            (widths[index] * 0.5f);
    }

    local_selected_index = (unsigned int)local_selected_position;
    if (local_selected_index >= visible_count)
    {
        local_selected_index = visible_count - 1u;
    }
    local_fraction = local_selected_position - (float)local_selected_index;
    if (local_selected_index + 1u < visible_count)
    {
        track_focus_center =
            centers[local_selected_index] +
            ((centers[local_selected_index + 1u] - centers[local_selected_index]) *
                local_fraction);
    }
    else
    {
        track_focus_center = centers[local_selected_index];
    }

    track_offset_x = TITAN_FOLDER_NAV_ANCHOR_CENTER_X - track_focus_center;
    for (index = 0u; index < visible_count; index++)
    {
        active_x[index] =
            track_offset_x + centers[index] - (widths[index] * 0.5f);
    }

    /* Orbis-like breadcrumb: it only becomes visible while entering the
       folder contents. The top system strip itself remains untouched. */
    if (active_transition > 0.03f)
    {
        unsigned char header_alpha;
        float header_width;

        header_alpha = TitanShellScene_Alpha(
            (float)alpha * active_transition * launch_label_visibility
        );
        header_width = TitanFont_MeasureText(0.46f, folder->name) + 44.0f;
        if (header_width < 124.0f)
        {
            header_width = 124.0f;
        }
        if (header_width > 280.0f)
        {
            header_width = 280.0f;
        }

        TitanVideo_DrawRectAlpha(
            112.0f,
            57.0f,
            header_width,
            20.0f,
            8,
            10,
            18,
            TitanShellScene_ModulateAlpha(172u, header_alpha)
        );
        TitanShellScene_DrawFolderGlyph(
            125.0f,
            67.0f,
            15.0f,
            11.0f,
            246,
            248,
            252,
            header_alpha
        );
        TitanFont_DrawTextAlpha(
            139.0f,
            59.0f,
            0.46f,
            folder->name,
            246,
            248,
            252,
            header_alpha
        );
    }

    selected_entry_valid = 0;
    selected_draw_x = 0.0f;
    selected_draw_y = 0.0f;
    selected_draw_width = 0.0f;
    selected_draw_height = 0.0f;

    for (index = 0u; index < visible_count; index++)
    {
        unsigned int item_index;
        TitanHomeGameKey key;
        TitanHomeEntry item_entry;
        float preview_x;
        float draw_x;
        float draw_y;
        float draw_width;
        float draw_height;
        float draw_focus;
        int is_selected;
        unsigned char item_alpha;

        item_index = start_index + index;
        key = folder->item_keys[item_index];
        preview_x =
            preview_start_x + ((float)index * (preview_size + TITAN_FOLDER_NAV_GAP));

        draw_x = preview_x + ((active_x[index] - preview_x) * active_transition);
        draw_y =
            preview_y +
            ((TITAN_FOLDER_NAV_ACTIVE_TOP - preview_y) * active_transition);
        draw_width =
            preview_size + ((widths[index] - preview_size) * active_transition);
        draw_height =
            preview_size + ((heights[index] - preview_size) * active_transition);
        draw_focus = focus[index] * active_transition;
        is_selected =
            g_scene.folder_open &&
            item_index == (unsigned int)g_scene.folder_open_selected;
        item_alpha = TitanShellScene_Alpha(
            (float)alpha *
            (is_selected ? launch_selected_visibility : launch_other_visibility)
        );

        if (g_scene.game_launch_active && is_selected)
        {
            const float target_width = 128.0f;
            const float target_height = 170.0f;
            const float target_x = 76.0f;
            const float target_y = 112.0f;

            draw_x += (target_x - draw_x) * launch_stage_progress;
            draw_y += (target_y - draw_y) * launch_stage_progress;
            draw_width += (target_width - draw_width) * launch_stage_progress;
            draw_height += (target_height - draw_height) * launch_stage_progress;
            draw_focus *= launch_label_visibility;
        }

        if (TitanShellScene_BuildFolderItemEntry(key, &item_entry))
        {
            TitanShellScene_DrawTile(
                &item_entry,
                draw_x,
                draw_y,
                draw_width,
                draw_height,
                draw_focus,
                is_selected,
                g_scene.game_launch_active && is_selected,
                1.0f,
                active_transition * launch_label_visibility,
                item_alpha
            );

            if (is_selected)
            {
                selected_entry = item_entry;
                selected_entry_valid = 1;
                selected_draw_x = draw_x;
                selected_draw_y = draw_y;
                selected_draw_width = draw_width;
                selected_draw_height = draw_height;
            }
        }
        else
        {
            (void)TitanShellScene_DrawFolderKeyVisual(
                key,
                draw_x + (draw_width * 0.5f),
                draw_y + (draw_height * 0.5f),
                draw_width < draw_height ? draw_width : draw_height,
                TitanShellScene_ModulateAlpha(110u, item_alpha)
            );
        }
    }

    if (selected_entry_valid && active_transition > 0.08f)
    {
        unsigned char label_alpha;
        float label_x;
        float label_y;
        float label_width;

        label_alpha = TitanShellScene_Alpha(
            (float)alpha * active_transition * launch_label_visibility
        );
        label_x = selected_draw_x + selected_draw_width + 10.0f;
        label_y = selected_draw_y + selected_draw_height - 31.0f;
        label_width = TitanFont_MeasureText(1.02f, selected_entry.title);
        if (label_x + label_width > (float)TITAN_UI_WIDTH - 18.0f)
        {
            label_x = (float)TITAN_UI_WIDTH - 18.0f - label_width;
        }

        TitanFont_DrawTextShadow(
            label_x,
            label_y,
            1.02f,
            selected_entry.title,
            248,
            249,
            252,
            label_alpha
        );
    }

    if (active_transition < 0.96f)
    {
        unsigned char preview_hint_alpha;
        char count_text[32];

        preview_hint_alpha = TitanShellScene_Alpha(
            (float)alpha * (1.0f - active_transition)
        );
        snprintf(
            count_text,
            sizeof(count_text),
            "%u %s",
            folder->item_count,
            folder->item_count == 1u ? TitanLocalization_Compat("item") : TitanLocalization_Compat("items")
        );
        TitanFont_DrawTextAlpha(
            145.0f,
            357.0f,
            0.48f,
            count_text,
            236,
            240,
            248,
            preview_hint_alpha
        );
        TitanFont_DrawTextAlpha(
            145.0f,
            379.0f,
            0.46f,
            TitanLocalization_Get(TITAN_STR_FOLDER_OPEN),
            236,
            45,
            88,
            preview_hint_alpha
        );
    }

    if (active_transition > 0.10f)
    {
        unsigned char footer_alpha;
        footer_alpha = TitanShellScene_Alpha(
            (float)alpha * active_transition * launch_label_visibility
        );
        TitanFont_DrawTextAlpha(
            145.0f,
            407.0f,
            0.45f,
            "LEFT/RIGHT Navegar    X Abrir    SELECT Opcoes    O Voltar",
            214,
            220,
            234,
            footer_alpha
        );
    }
}

static unsigned int TitanShellScene_BuildContextMenu(
    TitanContextMenuItem items[TITAN_CONTEXT_MENU_MAX_ITEMS]
)
{
    TitanHomeEntry selected_home;
    unsigned int count;

    if (items == 0)
    {
        return 0u;
    }

    count = 0u;
#define TITAN_ADD_CONTEXT_ITEM(text_value, action_value) \
    do \
    { \
        if (count < TITAN_CONTEXT_MENU_MAX_ITEMS) \
        { \
            items[count].label = (text_value); \
            items[count].action = (action_value); \
            count++; \
        } \
    } while (0)

    if (g_scene.context_source == TITAN_CONTEXT_SOURCE_SYSTEM_MENU)
    {
        switch (g_scene.context_shell_page)
        {
            case TITAN_SHELL_PAGE_NOTIFICATIONS:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Marcar todas como lidas", TITAN_CONTEXT_ACTION_MARK_NOTIFICATIONS_READ);
                TITAN_ADD_CONTEXT_ITEM("Limpar notificacoes", TITAN_CONTEXT_ACTION_CLEAR_NOTIFICATIONS);
                TITAN_ADD_CONTEXT_ITEM("Config. de notificacoes", TITAN_CONTEXT_ACTION_NOTIFICATION_SETTINGS);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_USER_PROFILE:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Trocar perfil", TITAN_CONTEXT_ACTION_SWITCH_PROFILE);
                TITAN_ADD_CONTEXT_ITEM("Editar perfil", TITAN_CONTEXT_ACTION_EDIT_PROFILE);
                TITAN_ADD_CONTEXT_ITEM("Gerenciar saves/VMC", TITAN_CONTEXT_ACTION_PROFILE_VMC);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_SAVED_DATA:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Criar VMC", TITAN_CONTEXT_ACTION_CREATE_VMC);
                TITAN_ADD_CONTEXT_ITEM("Gerenciar VMCs", TITAN_CONTEXT_ACTION_MANAGE_VMC);
                TITAN_ADD_CONTEXT_ITEM("Verificar VMC", TITAN_CONTEXT_ACTION_VERIFY_VMC);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_SETTINGS:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Restaurar padroes", TITAN_CONTEXT_ACTION_RESET_SETTINGS);
                TITAN_ADD_CONTEXT_ITEM("Exportar configuracoes", TITAN_CONTEXT_ACTION_EXPORT_SETTINGS);
                TITAN_ADD_CONTEXT_ITEM("Importar configuracoes", TITAN_CONTEXT_ACTION_IMPORT_SETTINGS);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_POWER:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Reiniciar Project Titan", TITAN_CONTEXT_ACTION_RESTART_TITAN);
                TITAN_ADD_CONTEXT_ITEM("Reiniciar console", TITAN_CONTEXT_ACTION_RESTART_CONSOLE);
                TITAN_ADD_CONTEXT_ITEM("Desligar console", TITAN_CONTEXT_ACTION_POWER_OFF);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            default:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;
        }

        return count;
    }

    if (!TitanShellScene_GetHomeEntry(
        (unsigned int)g_scene.selected_item,
        &selected_home
    ))
    {
        return 0u;
    }

    if (selected_home.type == TITAN_HOME_ENTRY_FOLDER)
    {
        TITAN_ADD_CONTEXT_ITEM("Edit folder", TITAN_CONTEXT_ACTION_EDIT_FOLDER);
        TITAN_ADD_CONTEXT_ITEM("Delete folder", TITAN_CONTEXT_ACTION_DELETE_FOLDER);
        TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
    }
    else if (selected_home.type == TITAN_HOME_ENTRY_GAME)
    {
        TITAN_ADD_CONTEXT_ITEM("Add to folder", TITAN_CONTEXT_ACTION_ADD_FOLDER);
        TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
        TITAN_ADD_CONTEXT_ITEM("Cheats", TITAN_CONTEXT_ACTION_CHEATS);
        TITAN_ADD_CONTEXT_ITEM(
            "Graphics Enhancements",
            TITAN_CONTEXT_ACTION_GRAPHICS_ENHANCEMENTS
        );
        {
            const TitanNeutrinoGame *game;

            game = TitanNeutrino_GetGame(selected_home.game_index);
            TITAN_ADD_CONTEXT_ITEM(
                "Resolution options",
                TITAN_CONTEXT_ACTION_RESOLUTION_OPTIONS
            );
            TITAN_ADD_CONTEXT_ITEM(
                game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME
                    ? "Opcoes do Titan UL Engine"
                    : "Opcoes do Titan-Neutrino Engine",
                TITAN_CONTEXT_ACTION_ENGINE
            );
        }
    }
    else if (selected_home.type == TITAN_HOME_ENTRY_APPLICATION)
    {
        TITAN_ADD_CONTEXT_ITEM("Iniciar", TITAN_CONTEXT_ACTION_LAUNCH_APPLICATION);
        TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
        if (TitanApplications_IsInstalled(selected_home.application_index))
        {
            TITAN_ADD_CONTEXT_ITEM("Desinstalar", TITAN_CONTEXT_ACTION_UNINSTALL_APPLICATION);
        }
    }
    else
    {
        switch (selected_home.page)
        {
            case TITAN_SHELL_PAGE_APPLICATIONS:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Add to folder", TITAN_CONTEXT_ACTION_ADD_FOLDER);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                TITAN_ADD_CONTEXT_ITEM("Ocultar da grade", TITAN_CONTEXT_ACTION_HIDE);
                break;

            case TITAN_SHELL_PAGE_STORAGE:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Ver armazenamento", TITAN_CONTEXT_ACTION_STORAGE_INFO);
                TITAN_ADD_CONTEXT_ITEM("Atualizar dispositivos", TITAN_CONTEXT_ACTION_REFRESH_DEVICES);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_INPUT:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Add to folder", TITAN_CONTEXT_ACTION_ADD_FOLDER);
                TITAN_ADD_CONTEXT_ITEM("Testar controle", TITAN_CONTEXT_ACTION_TEST_CONTROLLER);
                TITAN_ADD_CONTEXT_ITEM("Reatribuir porta", TITAN_CONTEXT_ACTION_REASSIGN_CONTROLLER);
                TITAN_ADD_CONTEXT_ITEM("Config. do controle", TITAN_CONTEXT_ACTION_CONTROLLER_SETTINGS);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_OVERVIEW:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Mover", TITAN_CONTEXT_ACTION_MOVE);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;

            case TITAN_SHELL_PAGE_APP_HOME:
            case TITAN_SHELL_PAGE_FTP_HOME:
            default:
                TITAN_ADD_CONTEXT_ITEM("Abrir", TITAN_CONTEXT_ACTION_OPEN);
                TITAN_ADD_CONTEXT_ITEM("Information", TITAN_CONTEXT_ACTION_INFO);
                break;
        }
    }

#undef TITAN_ADD_CONTEXT_ITEM
    return count;
}

static void TitanShellScene_DrawFolderContextMenu(void)
{
    TitanContextMenuItem items[TITAN_CONTEXT_MENU_MAX_ITEMS];
    TitanHomeEntry selected_home;
    const TitanNeutrinoGame *game = 0;
    float transition;
    float panel_width;
    float panel_x;
    float first_row_y;
    float row_step;
    unsigned char alpha;
    unsigned int index;
    unsigned int menu_count;
    int is_game_context;

    transition = TitanAnimation_SmoothStep(TitanShellScene_Clamp(g_scene.folder_context_transition, 0.0f, 1.0f));
    if (transition <= 0.01f) return;
    menu_count = TitanShellScene_BuildContextMenu(items);
    if (menu_count == 0u) return;

    memset(&selected_home, 0, sizeof(selected_home));
    is_game_context =
        g_scene.context_source == TITAN_CONTEXT_SOURCE_HOME &&
        TitanShellScene_GetHomeEntry((unsigned int)g_scene.selected_item, &selected_home) &&
        selected_home.type == TITAN_HOME_ENTRY_GAME;
    if (is_game_context) game = TitanNeutrino_GetGame(selected_home.game_index);

    if (is_game_context)
    {
        const float main_width = 286.0f;
        const float resolution_width = 184.0f;
        const float patch_width = 300.0f;
        const float compact_step = 47.0f;
        float resolution_transition = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(g_scene.resolution_options_transition, 0.0f, 1.0f));
        float patch_transition = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(g_scene.patch_options_transition, 0.0f, 1.0f));
        float submenu_transition = resolution_transition > patch_transition ?
            resolution_transition : patch_transition;
        float option_width = 184.0f;
        if (patch_transition > resolution_transition)
            option_width = patch_width;
        float main_target_x =
            (float)TITAN_UI_WIDTH - main_width - (option_width * submenu_transition);
        float main_closed_x = (float)TITAN_UI_WIDTH;
        TitanPerGameGsmMode modes[4];
        unsigned int mode_count = TitanShellScene_BuildResolutionModes(game, modes);
        float option_x;
        float option_visible_width;
        TitanPerGameGsmMode current = game != 0 ?
            TitanGameGsm_GetMode(game->startup) : TITAN_PER_GAME_GSM_DEFAULT;

        panel_x = main_closed_x + (main_target_x - main_closed_x) * transition;
        option_x = panel_x + main_width;
        option_visible_width = option_width * submenu_transition;
        alpha = TitanShellScene_Alpha(255.0f * transition);
        if (game != 0 && game->format != TITAN_NEUTRINO_GAME_USBEXTREME &&
            current == TITAN_PER_GAME_GSM_720P)
            current = TITAN_PER_GAME_GSM_DEFAULT;

        /* SELECT submenus are one continuous edge-attached surface. */
        TitanVideo_DrawRectAlpha(
            0.0f, 0.0f, panel_x, (float)TITAN_UI_HEIGHT,
            0, 0, 0,
            TitanShellScene_Alpha((124.0f + 18.0f * submenu_transition) * transition)
        );
        TitanVideo_DrawRectAlpha(
            panel_x, 0.0f, main_width, (float)TITAN_UI_HEIGHT,
            4, 5, 10, TitanShellScene_Alpha(226.0f * transition)
        );
        if (submenu_transition > 0.01f)
        {
            TitanVideo_DrawRectAlpha(
                option_x, 0.0f, option_visible_width, (float)TITAN_UI_HEIGHT,
                4, 5, 10,
                TitanShellScene_Alpha(226.0f * transition * submenu_transition)
            );
            TitanVideo_DrawRectAlpha(
                option_x, 0.0f, 1.0f, (float)TITAN_UI_HEIGHT,
                58, 61, 72,
                TitanShellScene_Alpha(118.0f * transition * submenu_transition)
            );
        }

        first_row_y =
            ((float)TITAN_UI_HEIGHT - ((float)menu_count * compact_step)) * 0.5f + 13.0f;
        if (first_row_y < 54.0f) first_row_y = 54.0f;
        for (index = 0u; index < menu_count; ++index)
        {
            float row_y = first_row_y + (float)index * compact_step;
            int selected = (int)index == g_scene.folder_context_selected;
            int resolution_row = items[index].action == TITAN_CONTEXT_ACTION_RESOLUTION_OPTIONS;
            int patch_row =
                (g_scene.patch_options_kind == TITAN_GAME_PATCH_CHEAT &&
                 items[index].action == TITAN_CONTEXT_ACTION_CHEATS) ||
                (g_scene.patch_options_kind == TITAN_GAME_PATCH_ENHANCEMENT &&
                 items[index].action == TITAN_CONTEXT_ACTION_GRAPHICS_ENHANCEMENTS);
            int active_submenu_row =
                (g_scene.resolution_options_open && resolution_row) ||
                (g_scene.patch_options_open && patch_row);
            float de_emphasis =
                (g_scene.resolution_options_open || g_scene.patch_options_open) &&
                !active_submenu_row ? 0.48f : 1.0f;
            unsigned char row_alpha = TitanShellScene_Alpha((float)alpha * de_emphasis);

            if (selected || active_submenu_row)
                TitanShellScene_DrawOutline(
                    panel_x + 15.0f, row_y - 8.0f, main_width - 31.0f, 35.0f,
                    2.0f, 244, 247, 252, alpha
                );
            TitanFont_DrawTextAlpha(
                panel_x + 26.0f, row_y, 0.56f,
                TitanLocalization_Compat(items[index].label),
                selected || active_submenu_row ? 253 : 206,
                selected || active_submenu_row ? 253 : 211,
                selected || active_submenu_row ? 255 : 222,
                row_alpha
            );
        }

        if (resolution_transition > 0.55f)
        {
            float text_reveal = TitanShellScene_Clamp(
                (resolution_transition - 0.55f) / 0.45f, 0.0f, 1.0f);
            unsigned char sub_alpha = TitanShellScene_Alpha(
                255.0f * transition * text_reveal);
            float option_first_row_y =
                ((float)TITAN_UI_HEIGHT - ((float)mode_count * compact_step)) * 0.5f + 13.0f;
            if (option_first_row_y < 54.0f) option_first_row_y = 54.0f;
            for (index = 0u; index < mode_count; ++index)
            {
                float row_y = option_first_row_y + (float)index * compact_step;
                int selected = (int)index == g_scene.resolution_options_selected;
                int active = modes[index] == current;
                if (selected)
                    TitanShellScene_DrawOutline(
                        option_x + 15.0f, row_y - 8.0f, resolution_width - 31.0f, 35.0f,
                        2.0f, 244, 247, 252, sub_alpha
                    );
                if (active)
                    TitanVideo_DrawRectAlpha(
                        option_x + 19.0f, row_y - 1.0f, 3.0f, 15.0f,
                        236, 45, 88, sub_alpha
                    );
                TitanFont_DrawTextAlpha(
                    option_x + 26.0f, row_y, 0.60f,
                    TitanLocalization_Compat(TitanGameGsm_GetModeName(modes[index])),
                    selected ? 253 : 218, selected ? 253 : 223,
                    selected ? 255 : 232, sub_alpha
                );
            }
        }
        else if (patch_transition > 0.55f)
        {
            const unsigned int visible_rows = 8u;
            unsigned int entry_count = TitanGamePatches_GetEntryCount(g_scene.patch_options_kind);
            unsigned int first = g_scene.patch_options_scroll < 0 ?
                0u : (unsigned int)g_scene.patch_options_scroll;
            unsigned int last = first + visible_rows;
            float text_reveal = TitanShellScene_Clamp(
                (patch_transition - 0.55f) / 0.45f, 0.0f, 1.0f);
            unsigned char sub_alpha = TitanShellScene_Alpha(
                255.0f * transition * text_reveal);
            float patch_first_row_y;

            if (last > entry_count) last = entry_count;
            if (entry_count == 0u)
            {
                const char *empty_text = g_scene.patch_options_kind == TITAN_GAME_PATCH_CHEAT ?
                    "No cheats found" : "No graphics enhancements found";
                const char *folder_text = g_scene.patch_options_kind == TITAN_GAME_PATCH_CHEAT ?
                    "CHT/<GAME ID>.cht" : "ENH/<GAME ID>.enh";
                TitanFont_DrawTextAlpha(
                    option_x + 24.0f, (float)TITAN_UI_HEIGHT * 0.5f - 10.0f,
                    0.52f, TitanLocalization_Compat(empty_text), 232, 235, 241, sub_alpha
                );
                TitanFont_DrawTextAlpha(
                    option_x + 24.0f, (float)TITAN_UI_HEIGHT * 0.5f + 22.0f,
                    0.42f, folder_text, 166, 172, 185, sub_alpha
                );
            }
            else
            {
                unsigned int visible_count = last - first;
                patch_first_row_y =
                    ((float)TITAN_UI_HEIGHT - ((float)visible_count * compact_step)) * 0.5f + 13.0f;
                if (patch_first_row_y < 54.0f) patch_first_row_y = 54.0f;

                for (index = first; index < last; ++index)
                {
                    TitanGamePatchEntryInfo entry;
                    float row_y = patch_first_row_y + (float)(index - first) * compact_step;
                    int selected = (int)index == g_scene.patch_options_selected;
                    char name[34];
                    const char *status;
                    float status_width;

                    if (!TitanGamePatches_GetEntry(g_scene.patch_options_kind, index, &entry))
                        continue;
                    if (strlen(entry.name) >= sizeof(name))
                    {
                        memcpy(name, entry.name, sizeof(name) - 4u);
                        memcpy(name + sizeof(name) - 4u, "...", 4u);
                    }
                    else
                    {
                        strcpy(name, entry.name);
                    }
                    status = TitanLocalization_Compat(
                        entry.enabled ? "Enabled" : "Disabled"
                    );
                    status_width = TitanFont_MeasureText(0.43f, status);

                    if (selected)
                        TitanShellScene_DrawOutline(
                            option_x + 13.0f, row_y - 8.0f, patch_width - 27.0f, 35.0f,
                            2.0f, 244, 247, 252, sub_alpha
                        );
                    if (entry.enabled)
                        TitanVideo_DrawRectAlpha(
                            option_x + 18.0f, row_y - 1.0f, 3.0f, 15.0f,
                            236, 45, 88, sub_alpha
                        );
                    TitanFont_DrawTextAlpha(
                        option_x + 26.0f, row_y, 0.48f, name,
                        selected ? 253 : 218, selected ? 253 : 223,
                        selected ? 255 : 232, sub_alpha
                    );
                    TitanFont_DrawTextAlpha(
                        option_x + patch_width - 18.0f - status_width,
                        row_y + 1.0f, 0.43f, status,
                        entry.enabled ? 238 : 166,
                        entry.enabled ? 224 : 172,
                        entry.enabled ? 234 : 185,
                        sub_alpha
                    );
                    if (entry.experimental)
                        TitanFont_DrawTextAlpha(
                            option_x + patch_width - 82.0f, row_y + 17.0f,
                            0.32f, TitanLocalization_Compat("Experimental"),
                            190, 194, 205, sub_alpha
                        );
                }

                if (entry_count > visible_rows)
                {
                    char counter[32];
                    int written = snprintf(
                        counter, sizeof(counter), "%u/%u",
                        (unsigned int)g_scene.patch_options_selected + 1u,
                        entry_count
                    );
                    if (written > 0 && (unsigned int)written < sizeof(counter))
                        TitanFont_DrawTextAlpha(
                            option_x + patch_width - 68.0f,
                            (float)TITAN_UI_HEIGHT - 25.0f,
                            0.37f, counter, 170, 175, 188, sub_alpha
                        );
                }
            }
        }
        return;
    }

    panel_width = 286.0f;
    panel_x = (float)TITAN_UI_WIDTH - (panel_width * transition);
    alpha = TitanShellScene_Alpha(255.0f * transition);
    row_step = menu_count > 6u ? 42.0f : 47.0f;
    first_row_y = ((float)TITAN_UI_HEIGHT - ((float)menu_count * row_step)) * 0.5f + 13.0f;
    if (first_row_y < 54.0f) first_row_y = 54.0f;
    TitanVideo_DrawRectAlpha(0.0f, 0.0f, panel_x, (float)TITAN_UI_HEIGHT, 0, 0, 0,
        TitanShellScene_Alpha(124.0f * transition));
    TitanVideo_DrawRectAlpha(panel_x, 0.0f, panel_width, (float)TITAN_UI_HEIGHT, 4, 5, 10,
        TitanShellScene_Alpha(226.0f * transition));
    for (index = 0u; index < menu_count; index++)
    {
        float row_y = first_row_y + ((float)index * row_step);
        int selected = (int)index == g_scene.folder_context_selected;
        if (selected)
            TitanShellScene_DrawOutline(panel_x + 15.0f, row_y - 8.0f, panel_width - 31.0f, 35.0f,
                2.0f, 244, 247, 252, alpha);
        TitanFont_DrawTextAlpha(panel_x + 26.0f, row_y, menu_count > 6u ? 0.56f : 0.60f,
            TitanLocalization_Compat(items[index].label), selected ? 253 : 215,
            selected ? 253 : 219, selected ? 255 : 229, alpha);
    }
}

static void TitanShellScene_DrawFolderScreenBackdrop(unsigned char alpha)
{
    if (alpha <= 4u)
    {
        return;
    }

    /* Full-screen folder UI owns the content plane, but the shell wave remains
     * visible exactly like Library.  A former near-opaque black fullscreen
     * rectangle made Home show through as a dark ghost and destroyed the
     * Magenta visual language. */
    TitanVideo_DrawRectAlpha(
        0.0f,
        66.0f,
        (float)TITAN_UI_WIDTH,
        1.0f,
        238,
        45,
        88,
        TitanShellScene_ModulateAlpha(205u, alpha)
    );
}

static void TitanShellScene_DrawFolderDestinationScreen(unsigned char alpha)
{
    unsigned int folder_count;
    unsigned int index;

    TitanFont_DrawTextAlpha(
        46.0f,
        25.0f,
        1.04f,
        "Add to folder",
        249,
        250,
        253,
        alpha
    );

    TitanShellScene_DrawOrbisButton(
        238.0f,
        96.0f,
        385.0f,
        40.0f,
        "Add to a new folder",
        g_scene.folder_destination_selected == 0,
        alpha
    );

    folder_count = TitanHomeFolders_GetCount();
    for (index = 0u; index < folder_count && index < 5u; index++)
    {
        const TitanHomeFolder *folder;
        float y;
        char item_text[32];

        folder = TitanHomeFolders_Get(index);
        if (folder == 0)
        {
            continue;
        }
        y = 154.0f + ((float)index * 49.0f);
        snprintf(item_text, sizeof(item_text), TitanLocalization_Compat("%u items"), folder->item_count);
        TitanShellScene_DrawOrbisButton(
            238.0f,
            y,
            385.0f,
            42.0f,
            folder->name,
            g_scene.folder_destination_selected == (int)index + 1,
            alpha
        );
        TitanFont_DrawTextAlpha(
            635.0f,
            y + 11.0f,
            0.48f,
            item_text,
            206,
            220,
            245,
            alpha
        );
    }

    TitanFont_DrawTextAlpha(
        38.0f,
        420.0f,
        0.48f,
        "X Confirmar     O Cancelar",
        222,
        231,
        247,
        alpha
    );
}

static void TitanShellScene_DrawFolderEditorScreen(unsigned char alpha)
{
    unsigned int selected_count;
    unsigned int index;
    unsigned int preview_count;
    unsigned int game_count;
    float preview_x;
    const char *title;
    char count_text[24];

    title = g_scene.folder_edit_existing >= 0
        ? (g_scene.folder_name_locked ? "Add to existing folder" : "Edit folder")
        : "Add to new folder";

    TitanFont_DrawTextAlpha(46.0f, 25.0f, 1.04f, title, 249, 250, 253, alpha);

    selected_count = TitanShellScene_CountFolderSelectedItems();
    snprintf(count_text, sizeof(count_text), "%u", selected_count);

    TitanFont_DrawTextAlpha(188.0f, 93.0f, 0.56f, "Nome", 207, 210, 219, alpha);
    if (g_scene.folder_editor_selected == 0 && !g_scene.folder_name_locked)
    {
        TitanVideo_DrawRectAlpha(260.0f, 84.0f, 470.0f, 38.0f, 238, 45, 88,
            TitanShellScene_ModulateAlpha(18u, alpha));
    }
    TitanShellScene_DrawOutline(
        260.0f,
        84.0f,
        470.0f,
        38.0f,
        g_scene.folder_editor_selected == 0 ? 2.2f : 1.0f,
        g_scene.folder_editor_selected == 0 ? 238 : 176,
        g_scene.folder_editor_selected == 0 ? 45 : 180,
        g_scene.folder_editor_selected == 0 ? 88 : 190,
        g_scene.folder_name_locked ? 110u : alpha
    );
    TitanFont_DrawTextAlpha(272.0f, 94.0f, 0.62f, g_scene.folder_pending_name, 247, 249, 253, alpha);

    TitanFont_DrawTextAlpha(149.0f, 140.0f, 0.56f, "Numero de itens", 207, 210, 219, alpha);
    TitanShellScene_DrawOutline(260.0f, 132.0f, 38.0f, 32.0f, 1.0f,
        176, 180, 190, TitanShellScene_ModulateAlpha(150u, alpha));
    TitanFont_DrawTextAlpha(274.0f, 140.0f, 0.58f, count_text, 247, 249, 253, alpha);

    TitanFont_DrawTextAlpha(177.0f, 187.0f, 0.56f, "Conteudo", 207, 210, 219, alpha);
    TitanShellScene_DrawOrbisButton(
        260.0f,
        171.0f,
        230.0f,
        38.0f,
        "Selecionar",
        g_scene.folder_editor_selected == 1,
        alpha
    );

    TitanFont_DrawTextAlpha(199.0f, 227.0f, 0.56f, "Ordenar", 207, 210, 219, alpha);

    preview_x = 260.0f;
    preview_count = 0u;
    for (
        index = 0u;
        index < TITAN_FOLDER_SHELL_SELECTION_COUNT && preview_count < 5u;
        index++
    )
    {
        const TitanShellItem *item;

        if (g_scene.folder_selected_shell[index] == 0u)
        {
            continue;
        }
        item = TitanShellCatalog_Get(index);
        if (item == 0)
        {
            continue;
        }

        TitanIcons_Draw(
            item->icon,
            preview_x + 32.0f,
            274.0f,
            0.78f,
            238,
            241,
            248,
            alpha
        );
        preview_x += 68.0f;
        preview_count++;
    }

    game_count = TitanNeutrino_GetGameCount();
    for (index = 0u; index < game_count && preview_count < 5u; index++)
    {
        if (g_scene.folder_selected_games[index] == 0u)
        {
            continue;
        }

        TitanUiAssets_DrawGamePlaceholder(
            preview_x + 32.0f,
            274.0f,
            62.0f,
            62.0f,
            238,
            241,
            248,
            alpha
        );
        preview_x += 68.0f;
        preview_count++;
    }

    TitanShellScene_DrawOrbisButton(
        260.0f,
        376.0f,
        210.0f,
        38.0f,
        "Cancel",
        g_scene.folder_editor_selected == 2,
        alpha
    );
    TitanShellScene_DrawOrbisButton(
        500.0f,
        376.0f,
        210.0f,
        38.0f,
        "OK",
        g_scene.folder_editor_selected == 3,
        alpha
    );

    TitanFont_DrawTextAlpha(
        38.0f,
        423.0f,
        0.48f,
        "X Confirmar     O Cancelar",
        222,
        231,
        247,
        alpha
    );
}

static void TitanShellScene_DrawFolderContentScreen(unsigned char alpha)
{
    unsigned int candidate_count;
    unsigned int index;
    unsigned int selected_count;
    unsigned int first_candidate;
    unsigned int first_row;
    unsigned int total_rows;
    unsigned int max_first_row;
    unsigned int preview_count;
    unsigned int game_count;
    float preview_x;
    const unsigned int columns = 6u;
    const unsigned int visible_rows = 3u;
    const float grid_x = 69.0f;
    const float grid_y = 139.0f;
    const float tile_size = 78.0f;
    const float gap_x = 8.0f;
    const float gap_y = 16.0f;
    char count_text[24];

    /* Orbis Folder > Select Content composition. Keep the content viewport
     * completely above the help bar: the BF19 6x4 layout leaked a fourth row
     * underneath the footer and made selection markers look detached from
     * their tiles. */
    TitanFont_DrawTextAlpha(46.0f, 25.0f, 1.04f, "Selecionar conteudo", 249, 250, 253, alpha);

    selected_count = TitanShellScene_CountFolderSelectedItems();
    snprintf(count_text, sizeof(count_text), "%u", selected_count);
    TitanFont_DrawTextAlpha(72.0f, 91.0f, 0.56f, count_text, 245, 247, 252, alpha);

    preview_x = 112.0f;
    preview_count = 0u;
    for (index = 0u; index < TITAN_FOLDER_SHELL_SELECTION_COUNT && preview_count < 9u; index++)
    {
        const TitanShellItem *item;
        if (g_scene.folder_selected_shell[index] == 0u) continue;
        item = TitanShellCatalog_Get(index);
        if (item == 0) continue;
        TitanIcons_Draw(item->icon, preview_x, 99.0f, 0.36f, 237, 241, 249, alpha);
        preview_x += 36.0f;
        preview_count++;
    }
    game_count = TitanNeutrino_GetGameCount();
    for (index = 0u; index < game_count && preview_count < 9u; index++)
    {
        if (g_scene.folder_selected_games[index] == 0u) continue;
        TitanShellScene_DrawOutline(preview_x - 14.0f, 85.0f, 28.0f, 28.0f, 1.0f,
            176, 180, 190, TitanShellScene_ModulateAlpha(130u, alpha));
        TitanUiAssets_DrawGamePlaceholder(preview_x, 99.0f, 24.0f, 24.0f,
            237, 241, 249, alpha);
        preview_x += 36.0f;
        preview_count++;
    }

    TitanVideo_DrawRectAlpha(64.0f, 122.0f, 727.0f, 1.0f,
        97, 100, 112, TitanShellScene_ModulateAlpha(150u, alpha));
    TitanVideo_DrawRectAlpha(626.0f, 132.0f, 1.0f, 258.0f,
        112, 115, 126, TitanShellScene_ModulateAlpha(175u, alpha));

    candidate_count = TitanShellScene_GetFolderCandidateCount();
    total_rows = (candidate_count + columns - 1u) / columns;
    first_row = g_scene.folder_content_cursor >= 0
        ? (unsigned int)g_scene.folder_content_cursor / columns : 0u;
    if (first_row >= visible_rows) first_row -= visible_rows - 1u;
    else first_row = 0u;
    max_first_row = total_rows > visible_rows ? total_rows - visible_rows : 0u;
    if (first_row > max_first_row) first_row = max_first_row;
    first_candidate = first_row * columns;

    for (index = 0u; index < columns * visible_rows && first_candidate + index < candidate_count; index++)
    {
        float x;
        float y;
        unsigned int row = index / columns;
        unsigned int column = index % columns;
        int focused;
        int enabled;
        int is_shell;
        unsigned int item_index;
        unsigned int candidate_index = first_candidate + index;
        unsigned char item_alpha;
        const char *label = "Conteudo";
        const TitanShellItem *shell_item = 0;
        const TitanNeutrinoGame *game = 0;
        int selected;

        if (!TitanShellScene_GetFolderCandidate(candidate_index, &is_shell, &item_index)) continue;
        x = grid_x + ((float)column * (tile_size + gap_x));
        y = grid_y + ((float)row * (tile_size + gap_y));
        focused = !g_scene.folder_content_side_active &&
            (int)candidate_index == g_scene.folder_content_cursor;
        enabled = TitanShellScene_CanSelectFolderCandidate(candidate_index);
        selected = TitanShellScene_FolderCandidateSelected(candidate_index);
        item_alpha = enabled ? alpha : TitanShellScene_ModulateAlpha(88u, alpha);

        /* Orbis items are square cards.  The source icon sits inside the card;
         * the checkbox belongs to its top-left corner instead of floating in a
         * separate row above the grid. */
        if (focused)
        {
            TitanVideo_DrawRectAlpha(x, y, tile_size, tile_size, 238, 45, 88,
                TitanShellScene_ModulateAlpha(18u, item_alpha));
        }

        if (is_shell)
        {
            shell_item = TitanShellCatalog_Get(item_index);
            if (shell_item != 0)
            {
                label = shell_item->title;
                TitanIcons_Draw(shell_item->icon, x + tile_size*0.5f, y + tile_size*0.5f,
                    0.88f, 238, 241, 248, item_alpha);
            }
        }
        else
        {
            game = TitanNeutrino_GetGame(item_index);
            if (game != 0) label = game->title;
            TitanUiAssets_DrawGamePlaceholder(x + tile_size*0.5f, y + tile_size*0.5f,
                68.0f, 68.0f, 238, 241, 248, item_alpha);
        }

        TitanVideo_DrawRectAlpha(x + 5.0f, y + 5.0f, 15.0f, 15.0f,
            selected ? 238 : 20, selected ? 45 : 21, selected ? 88 : 27,
            TitanShellScene_ModulateAlpha(selected ? 255u : 208u, item_alpha));
        TitanShellScene_DrawOutline(x + 5.0f, y + 5.0f, 15.0f, 15.0f, 1.4f,
            selected ? 238 : 232, selected ? 45 : 234, selected ? 88 : 240, item_alpha);
        if (selected)
        {
            TitanFont_DrawTextAlpha(x + 8.0f, y + 2.0f, 0.48f, "X",
                255, 255, 255, item_alpha);
        }

        if (focused)
        {
            TitanShellScene_DrawOutline(x - 3.0f, y - 3.0f,
                tile_size + 6.0f, tile_size + 6.0f, 2.4f,
                238, 45, 88, alpha);
            TitanFont_DrawTextAlpha(x, y + tile_size + 6.0f, 0.43f, label,
                244, 247, 252, alpha);
        }
    }

    if (total_rows > visible_rows)
    {
        const float track_y = grid_y;
        const float track_h = (tile_size * (float)visible_rows) +
            (gap_y * (float)(visible_rows - 1u));
        float thumb_h = track_h * ((float)visible_rows / (float)total_rows);
        float thumb_y;
        if (thumb_h < 25.0f) thumb_h = 25.0f;
        thumb_y = track_y + ((track_h - thumb_h) *
            ((float)first_row / (float)max_first_row));
        TitanVideo_DrawRectAlpha(613.0f, track_y, 2.0f, track_h,
            210, 214, 224, TitanShellScene_ModulateAlpha(70u, alpha));
        TitanVideo_DrawRectAlpha(613.0f, thumb_y, 2.0f, thumb_h,
            247, 249, 253, TitanShellScene_ModulateAlpha(220u, alpha));
    }

    TitanShellScene_DrawOrbisButton(650.0f, 145.0f, 158.0f, 40.0f,
        "Selecionar todos",
        g_scene.folder_content_side_active && g_scene.folder_content_side_selected == 0, alpha);
    TitanShellScene_DrawOrbisButton(650.0f, 196.0f, 158.0f, 40.0f,
        "Desmarcar todos",
        g_scene.folder_content_side_active && g_scene.folder_content_side_selected == 1, alpha);
    TitanShellScene_DrawOrbisButton(650.0f, 343.0f, 158.0f, 40.0f,
        "Confirmar",
        g_scene.folder_content_side_active && g_scene.folder_content_side_selected == 2, alpha);

    TitanFont_DrawTextAlpha(38.0f, 421.0f, 0.48f,
        "X Marcar/Confirmar     O Cancelar", 222, 231, 247, alpha);
}

static void TitanShellScene_DrawFolderScreenForMode(
    TitanFolderUiMode mode,
    unsigned char alpha
)
{
    if (alpha <= 4u)
    {
        return;
    }

    if (mode == TITAN_FOLDER_UI_DESTINATION)
    {
        TitanShellScene_DrawFolderDestinationScreen(alpha);
    }
    else if (mode == TITAN_FOLDER_UI_EDITOR)
    {
        TitanShellScene_DrawFolderEditorScreen(alpha);
    }
    else if (mode == TITAN_FOLDER_UI_CONTENT)
    {
        TitanShellScene_DrawFolderContentScreen(alpha);
    }
}

static void TitanShellScene_DrawFolderUi(void)
{
    float progress;
    float backdrop_visibility;
    float previous_offset_x;
    float current_offset_x;
    unsigned char current_alpha;
    unsigned char previous_alpha;
    unsigned char backdrop_alpha;
    int current_is_screen;
    int previous_is_screen;

    TitanShellScene_DrawFolderContextMenu();

    progress = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(g_scene.folder_screen_transition, 0.0f, 1.0f)
    );
    current_is_screen = TitanShellScene_IsFolderScreenMode(g_scene.folder_ui_mode);
    previous_is_screen = TitanShellScene_IsFolderScreenMode(g_scene.folder_previous_ui_mode);

    current_alpha = current_is_screen
        ? TitanShellScene_Alpha(255.0f * progress)
        : 0u;
    previous_alpha = previous_is_screen
        ? TitanShellScene_Alpha(255.0f * (1.0f - progress))
        : 0u;

    /* Global hierarchical menu motion: every folder child screen now uses
     * the same Orbis-style parent-left / child-from-right choreography used
     * by Settings categories. This fixes Editor -> Content previously being
     * only an alpha cut with no spatial transition. */
    previous_offset_x = previous_is_screen
        ? (current_is_screen ? -92.0f * progress : 92.0f * progress)
        : 0.0f;
    current_offset_x = current_is_screen
        ? 92.0f * (1.0f - progress)
        : 0.0f;

    if (current_is_screen && previous_is_screen)
    {
        backdrop_visibility = 1.0f;
    }
    else if (current_is_screen)
    {
        backdrop_visibility = progress;
    }
    else if (previous_is_screen)
    {
        backdrop_visibility = 1.0f - progress;
    }
    else
    {
        backdrop_visibility = 0.0f;
    }

    backdrop_alpha = TitanShellScene_Alpha(255.0f * backdrop_visibility);
    TitanShellScene_DrawFolderScreenBackdrop(backdrop_alpha);

    if (previous_alpha > 4u && g_scene.folder_previous_ui_mode != g_scene.folder_ui_mode)
    {
        TitanVideo_SetTransform(
            1.0f,
            (float)TITAN_UI_WIDTH * 0.5f,
            (float)TITAN_UI_HEIGHT * 0.5f,
            previous_offset_x,
            0.0f
        );
        TitanShellScene_DrawFolderScreenForMode(
            g_scene.folder_previous_ui_mode,
            previous_alpha
        );
        TitanVideo_ResetTransform();
    }

    if (current_alpha > 4u)
    {
        TitanVideo_SetTransform(
            1.0f,
            (float)TITAN_UI_WIDTH * 0.5f,
            (float)TITAN_UI_HEIGHT * 0.5f,
            current_offset_x,
            0.0f
        );
        TitanShellScene_DrawFolderScreenForMode(
            g_scene.folder_ui_mode,
            current_alpha
        );
        TitanVideo_ResetTransform();
    }
}

static void TitanShellScene_DrawHome(
    float offset_x,
    unsigned char alpha,
    float focus_reveal,
    float entry_progress
)
{
    unsigned int item_count;
    unsigned int index;
    unsigned int selected_index;
    unsigned int label_anchor_index;
    unsigned int asset_visible_games[TITAN_ASSET_VISIBLE_GAME_CAPACITY];
    unsigned int asset_visible_count = 0u;
    unsigned int asset_selected_game = 0xffffffffu;
    float focus[TITAN_SHELL_LAYOUT_CAPACITY];
    float widths[TITAN_SHELL_LAYOUT_CAPACITY];
    float heights[TITAN_SHELL_LAYOUT_CAPACITY];
    float tile_x[TITAN_SHELL_LAYOUT_CAPACITY];
    float tile_y[TITAN_SHELL_LAYOUT_CAPACITY];
    float track_center[TITAN_SHELL_LAYOUT_CAPACITY];
    float track_offset_x;
    float track_focus_center;
    float track_fraction;
    float track_anchor_x;
    unsigned int track_index;
    float distance;
    float label_scale;
    float label_width;
    float label_x;
    float label_y;
    float detail_center_x;
    float detail_line_width;
    float detail_panel_width;
    float detail_panel_x;
    float detail_alpha_factor;
    float menu_transition;
    float overview_transition;
    float grid_offset_y;
    float grid_scale;
    float grid_gap;
    float layout_focus_scale;
    float action_visibility;
    float item_reveal;
    float item_reveal_start;
    float selected_entry_reveal;
    unsigned char grid_alpha;
    unsigned char item_alpha;
    unsigned char name_alpha;
    unsigned char detail_alpha;
    float launch_other_visibility;
    float launch_selected_visibility;
    float launch_label_visibility;
    float launch_stage_progress;
    float folder_nav_progress;
    float home_grid_visibility;
    TitanHomeEntry item;
    TitanHomeEntry selected;
    const TitanThemeShellStyle *theme_style;

    theme_style = TitanTheme_GetShellStyle();

    /* Installed-app icons live in unused cells of the existing avatar atlas.
     * The call is intentionally cheap and remains here as the mode-switch
     * restoration hook for future atlas backends. */
    TitanAppAssets_EnsureUploaded();
    item_count = TitanShellScene_GetHomeEntryCount();

    menu_transition = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(
            g_scene.top_menu_transition,
            0.0f,
            1.0f
        )
    );

    overview_transition =
        g_scene.page == TITAN_SHELL_PAGE_OVERVIEW
            ? TitanAnimation_SmoothStep(
                TitanShellScene_Clamp(
                    g_scene.page_transition,
                    0.0f,
                    1.0f
                )
            )
            : 0.0f;

    grid_offset_y =
        (menu_transition * TITAN_SHELL_TOP_MENU_GRID_SHIFT) -
        (overview_transition * 82.0f);

    grid_scale =
        1.0f -
        ((menu_transition > overview_transition
            ? menu_transition
            : overview_transition) * 0.22f);

    grid_gap =
        (TITAN_SHELL_TILE_GAP + 0.5f) *
        grid_scale *
        TitanShellScene_Clamp(theme_style->home_tile_spacing, 0.80f, 1.25f);

    layout_focus_scale =
        1.0f -
        (menu_transition > overview_transition
            ? menu_transition
            : overview_transition);

    action_visibility =
        1.0f -
        (menu_transition > overview_transition
            ? menu_transition
            : overview_transition);
    focus_reveal = TitanShellScene_Clamp(
        focus_reveal,
        0.0f,
        1.0f
    );
    entry_progress = TitanShellScene_Clamp(
        entry_progress,
        0.0f,
        1.0f
    );
    action_visibility *= focus_reveal;

    launch_other_visibility = 1.0f;
    launch_selected_visibility = 1.0f;
    launch_label_visibility = 1.0f;
    launch_stage_progress = 0.0f;
    if (g_scene.game_launch_active)
    {
        launch_other_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_OTHER_FADE_START,
            TITAN_GAME_LAUNCH_OTHER_FADE_END
        );
        launch_selected_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_SELECTED_FADE_START,
            TITAN_GAME_LAUNCH_SELECTED_FADE_END
        );
        launch_label_visibility = 1.0f - TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_LABEL_FADE_START,
            TITAN_GAME_LAUNCH_LABEL_FADE_END
        );
        launch_stage_progress = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_STAGE_IN_START,
            TITAN_GAME_LAUNCH_STAGE_IN_END
        );
        action_visibility *= launch_label_visibility;
    }

    folder_nav_progress = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(g_scene.folder_open_transition, 0.0f, 1.0f)
    );
    home_grid_visibility = 1.0f - folder_nav_progress;
    grid_offset_y -= folder_nav_progress * 24.0f;

    grid_alpha = alpha;

    if (item_count > TITAN_SHELL_LAYOUT_CAPACITY)
    {
        item_count = TITAN_SHELL_LAYOUT_CAPACITY;
    }

    if (item_count == 0u)
    {
        return;
    }

    selected_index =
        (unsigned int)g_scene.selected_item;

    if (selected_index >= item_count)
    {
        return;
    }

    if (!TitanShellScene_GetHomeEntry(selected_index, &selected))
    {
        return;
    }
    if (selected.type == TITAN_HOME_ENTRY_GAME)
    {
        asset_selected_game = selected.game_index;
        (void)TitanAssetService_DrawBackground(
            selected.game_index,
            0.0f, 0.0f,
            (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
            TitanShellScene_ModulateAlpha(92u, alpha)
        );
    }

    for (index = 0; index < item_count; index++)
    {
        distance = TitanAnimation_Absolute(
            (float)index -
            g_scene.selected_position
        );

        focus[index] = 1.0f -
            TitanShellScene_Clamp(
                distance,
                0.0f,
                1.0f
            );

        focus[index] = TitanAnimation_SmoothStep(
            focus[index]
        ) * focus_reveal;

        widths[index] =
            (TITAN_SHELL_TILE_SMALL_WIDTH +
            (
                (focus[index] * layout_focus_scale) *
                (
                    (TITAN_SHELL_TILE_FOCUSED_WIDTH *
                        TitanShellScene_Clamp(theme_style->home_selected_scale, 0.85f, 1.25f)) -
                    TITAN_SHELL_TILE_SMALL_WIDTH
                )
            )) * grid_scale;

        heights[index] =
            (TITAN_SHELL_TILE_SMALL_HEIGHT +
            (
                (focus[index] * layout_focus_scale) *
                (
                    (TITAN_SHELL_TILE_FOCUSED_HEIGHT *
                        TitanShellScene_Clamp(theme_style->home_selected_scale, 0.85f, 1.25f)) -
                    TITAN_SHELL_TILE_SMALL_HEIGHT
                )
            )) * grid_scale;
    }

    /* Continuous center-based carousel. Adjacent edges keep a fixed gap while
       both the outgoing and incoming tiles resize. Anchoring the interpolated
       focus CENTER eliminates the horizontal rubber-band visible in 0.3.17. */
    track_center[0] = widths[0] * 0.5f;

    for (index = 1u; index < item_count; index++)
    {
        track_center[index] =
            track_center[index - 1u] +
            (widths[index - 1u] * 0.5f) +
            grid_gap +
            (widths[index] * 0.5f);
    }

    track_index = (unsigned int)g_scene.selected_position;

    if (track_index >= item_count)
    {
        track_index = item_count - 1u;
    }

    track_fraction =
        g_scene.selected_position -
        (float)track_index;

    if (track_index + 1u < item_count)
    {
        track_focus_center =
            track_center[track_index] +
            ((track_center[track_index + 1u] - track_center[track_index]) *
            track_fraction);
    }
    else
    {
        track_focus_center = track_center[track_index];
    }

    track_anchor_x =
        TITAN_SHELL_TILE_FOCUS_ANCHOR_CENTER_X +
        (menu_transition * 34.0f) -
        (overview_transition * 130.0f);

    track_offset_x =
        track_anchor_x +
        offset_x -
        track_focus_center;

    for (index = 0; index < item_count; index++)
    {
        tile_x[index] =
            track_offset_x +
            track_center[index] -
            (widths[index] * 0.5f);
        tile_y[index] =
            TITAN_SHELL_TILE_TOP +
            grid_offset_y -
            (
                (focus[index] * layout_focus_scale) *
                TITAN_SHELL_TILE_FOCUS_RISE
            );

        if (g_scene.game_launch_active && index == selected_index)
        {
            const float target_width = 128.0f;
            const float target_height = 170.0f;
            const float target_x = 76.0f;
            const float target_y = 112.0f;

            widths[index] += (target_width - widths[index]) * launch_stage_progress;
            heights[index] += (target_height - heights[index]) * launch_stage_progress;
            tile_x[index] += (target_x - tile_x[index]) * launch_stage_progress;
            tile_y[index] += (target_y - tile_y[index]) * launch_stage_progress;
        }

        if (
            tile_x[index] + widths[index] < -8.0f ||
            tile_x[index] > (float)TITAN_UI_WIDTH + 8.0f
        )
        {
            continue;
        }

        if (!TitanShellScene_GetHomeEntry(index, &item))
        {
            continue;
        }
        if (item.type == TITAN_HOME_ENTRY_GAME &&
            asset_visible_count < TITAN_ASSET_VISIBLE_GAME_CAPACITY)
        {
            asset_visible_games[asset_visible_count++] = item.game_index;
        }

        item_reveal_start =
            0.12f + ((float)index * 0.055f);
        item_reveal = TitanAnimation_EaseOutCubic(
            TitanShellScene_Clamp(
                (entry_progress - item_reveal_start) / 0.22f,
                0.0f,
                1.0f
            )
        );
        item_alpha = TitanShellScene_Alpha(
            (float)grid_alpha * item_reveal *
            (index == selected_index
                ? launch_selected_visibility
                : launch_other_visibility) *
            home_grid_visibility
        );

        if (item_alpha > 4u)
        {
            TitanShellScene_DrawTile(
                &item,
                tile_x[index],
                tile_y[index],
                widths[index],
                heights[index],
                focus[index] * layout_focus_scale,
                index == selected_index,
                g_scene.game_launch_active && index == selected_index,
                grid_scale,
                action_visibility,
                item_alpha
            );
        }
    }

    TitanAssetService_SetVisibleGames(
        asset_selected_game,
        asset_visible_games,
        asset_visible_count,
        g_scene.local_frame
    );

    label_scale =
        1.02f *
        (1.0f - (menu_transition * 0.18f));
    label_width = TitanFont_MeasureText(
        label_scale,
        selected.title
    );

    label_anchor_index = selected_index;
    label_x =
        tile_x[label_anchor_index] +
        widths[label_anchor_index] +
        10.0f;

    /* Library is the permanent final Home tile.  Keep its title attached to
     * the right edge of the selected tile; never flip/clamp it back over the
     * tile as the generic right-edge protection can do for other entries. */
    if (selected.page == TITAN_SHELL_PAGE_LIBRARY)
    {
        label_x = tile_x[label_anchor_index] + widths[label_anchor_index] + 14.0f;
    }

    label_y =
        tile_y[label_anchor_index] +
        heights[label_anchor_index] -
        (31.0f * grid_scale) +
        theme_style->title_offset_y;

    if (
        selected.page != TITAN_SHELL_PAGE_LIBRARY &&
        label_x + label_width > (float)TITAN_UI_WIDTH - 24.0f
    )
    {
        label_x = (float)TITAN_UI_WIDTH - 24.0f - label_width;
    }

    item_reveal_start =
        0.12f + ((float)selected_index * 0.055f);
    selected_entry_reveal = TitanAnimation_EaseOutCubic(
        TitanShellScene_Clamp(
            (entry_progress - item_reveal_start) / 0.22f,
            0.0f,
            1.0f
        )
    );
    name_alpha = TitanShellScene_Alpha(
        (float)grid_alpha *
        focus[selected_index] *
        selected_entry_reveal *
        (1.0f - menu_transition) *
        (1.0f - overview_transition) *
        launch_label_visibility *
        home_grid_visibility
    );

    if (name_alpha > 4)
    {
        TitanFont_DrawTextShadow(
            label_x,
            label_y,
            label_scale,
            selected.title,
            248,
            249,
            252,
            name_alpha
        );
    }

    if (selected.type == TITAN_HOME_ENTRY_FOLDER)
    {
        unsigned char folder_row_alpha;

        /* The folder preview belongs to the Home focus layer.  Previously it
         * kept drawing at full alpha while the top menu took focus, leaving a
         * child game icon stranded over the menu transition.  Only modulate
         * that transient layer; the normal Home/folder composition is unchanged. */
        folder_row_alpha = TitanShellScene_Alpha(
            (float)alpha *
            (1.0f - menu_transition) *
            (1.0f - overview_transition)
        );
        if (folder_row_alpha > 4u)
        {
            TitanShellScene_DrawFolderOpenRow(
                selected.folder_index,
                folder_row_alpha
            );
        }
        return;
    }

    detail_alpha_factor =
        ((float)alpha / 255.0f) *
        g_scene.detail_visibility *
        (1.0f - menu_transition) *
        (1.0f - overview_transition);

    detail_alpha = TitanShellScene_Alpha(
        255.0f * detail_alpha_factor
    );

    if (detail_alpha <= 4)
    {
        return;
    }

    detail_panel_x = 0.0f;
    detail_panel_width = (float)TITAN_UI_WIDTH;
    detail_center_x = TitanShellScene_Clamp(
        label_x + 6.0f,
        146.0f,
        272.0f
    );

    detail_line_width = detail_panel_width;

    {
        float detail_bottom_bleed;
        float logical_scale_y;
        float logical_offset_y;

        detail_bottom_bleed = 0.0f;
        logical_scale_y = TitanVideo_GetLogicalScaleY();
        logical_offset_y = TitanVideo_GetLogicalOffsetY();
        if (logical_scale_y > 0.0f && logical_offset_y > 0.0f)
        {
            detail_bottom_bleed =
                (logical_offset_y / logical_scale_y) + 1.0f;
        }

        TitanVideo_DrawRectAlpha(
        detail_panel_x,
        296.0f,
        detail_line_width,
        152.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(12.0f * detail_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        308.0f,
        detail_line_width,
        140.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(24.0f * detail_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        322.0f,
        detail_line_width,
        126.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(38.0f * detail_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        338.0f,
        detail_line_width,
        110.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(56.0f * detail_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        356.0f,
        detail_line_width,
        92.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(78.0f * detail_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        382.0f,
        detail_line_width,
        66.0f + detail_bottom_bleed,
        0,
        0,
        0,
        TitanShellScene_Alpha(104.0f * detail_alpha_factor)
    );
    }

    TitanVideo_DrawRectAlpha(
        detail_panel_x,
        296.0f,
        detail_line_width,
        1.0f,
        112,
        112,
        120,
        TitanShellScene_Alpha(54.0f * detail_alpha_factor)
    );

    TitanFont_DrawTextAlpha(
        detail_center_x,
        352.0f,
        0.82f,
        selected.subtitle,
        232,
        236,
        244,
        detail_alpha
    );

    TitanFont_DrawTextAlpha(
        detail_center_x,
        380.0f,
        0.68f,
        selected.status,
        233,
        75,
        109,
        detail_alpha
    );
}

static void TitanShellScene_DrawPanelBase(
    const char *title,
    const char *subtitle,
    float offset_x,
    unsigned char alpha
)
{
    (void)subtitle;

    TitanFont_DrawTextShadow(
        28.0f + offset_x,
        20.0f,
        1.02f,
        TitanLocalization_Compat(title),
        246,
        247,
        251,
        alpha
    );

    /* Header chrome/separators are theme-owned. The shell must not paint the
     * default red line over Flow/community theme top-menu details. */
}

static void TitanShellScene_DrawValueRow(
    const char *label,
    const char *value,
    float y,
    float offset_x,
    unsigned char alpha,
    int accent
)
{
    TitanFont_DrawTextAlpha(
        116.0f + offset_x,
        y,
        0.68f,
        TitanLocalization_Compat(label),
        151,
        159,
        179,
        alpha
    );

    TitanFont_DrawTextAlpha(
        498.0f + offset_x,
        y,
        0.68f,
        TitanLocalization_Compat(value != 0 ? value : ""),
        accent ? 237 : 225,
        accent ? 74 : 228,
        accent ? 108 : 235,
        alpha
    );
}

static void TitanShellScene_DrawOverviewPage(
    const TitanContext *context,
    const TitanInputState *input,
    float offset_x,
    unsigned char alpha
)
{
    float info_alpha_factor;
    unsigned char panel_alpha;
    unsigned char text_alpha;
    unsigned char accent_alpha;

    (void)context;
    (void)input;

    info_alpha_factor = (float)alpha / 255.0f;
    panel_alpha = TitanShellScene_Alpha(118.0f * info_alpha_factor);
    text_alpha = TitanShellScene_Alpha(255.0f * info_alpha_factor);
    accent_alpha = TitanShellScene_Alpha(224.0f * info_alpha_factor);

    TitanVideo_DrawRectAlpha(
        0.0f,
        112.0f,
        (float)TITAN_UI_WIDTH,
        336.0f,
        0,
        0,
        0,
        TitanShellScene_Alpha(22.0f * info_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        0.0f,
        142.0f,
        (float)TITAN_UI_WIDTH,
        306.0f,
        0,
        0,
        0,
        TitanShellScene_Alpha(38.0f * info_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        0.0f,
        178.0f,
        (float)TITAN_UI_WIDTH,
        270.0f,
        0,
        0,
        0,
        TitanShellScene_Alpha(58.0f * info_alpha_factor)
    );

    TitanVideo_DrawRectAlpha(
        0.0f,
        220.0f,
        (float)TITAN_UI_WIDTH,
        228.0f,
        0,
        0,
        0,
        panel_alpha
    );

    TitanVideo_DrawRectAlpha(
        0.0f,
        112.0f,
        (float)TITAN_UI_WIDTH,
        1.0f,
        114,
        114,
        121,
        TitanShellScene_Alpha(64.0f * info_alpha_factor)
    );

    TitanFont_DrawTextShadow(
        34.0f + offset_x,
        148.0f,
        1.18f,
        "Project Titan",
        244,
        246,
        250,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        180.0f,
        0.62f,
        "System shell",
        232,
        75,
        109,
        accent_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        216.0f,
        0.72f,
        "A native modular system shell for PlayStation 2.",
        232,
        236,
        244,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        246.0f,
        0.62f,
        "Fast startup, background services and a responsive console interface.",
        223,
        226,
        234,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        276.0f,
        0.62f,
        "Library verification now continues after the Home screen is available.",
        223,
        226,
        234,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        306.0f,
        0.62f,
        "Notifications report background work without covering the main grid.",
        223,
        226,
        234,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        346.0f,
        0.56f,
        "Current release:",
        154,
        161,
        178,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        200.0f + offset_x,
        346.0f,
        0.56f,
        "Fast Boot Catalog / Background Activities / Titan VU Wave",
        233,
        236,
        244,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        376.0f,
        0.56f,
        "Versions:",
        154,
        161,
        178,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        200.0f + offset_x,
        376.0f,
        0.56f,
        TITAN_KERNEL_VERSION_STRING "  /  " TITAN_UI_VERSION_STRING,
        233,
        75,
        109,
        accent_alpha
    );

    TitanFont_DrawTextAlpha(
        34.0f + offset_x,
        406.0f,
        0.56f,
        "Runtime:",
        154,
        161,
        178,
        text_alpha
    );

    TitanFont_DrawTextAlpha(
        200.0f + offset_x,
        406.0f,
        0.56f,
        "Independent, modular and console-first",
        233,
        236,
        244,
        text_alpha
    );
}


static char TitanShellScene_LibraryAsciiLower(char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static int TitanShellScene_LibraryContainsInsensitive(
    const char *text,
    const char *needle
)
{
    size_t text_index;
    size_t needle_index;

    if (needle == 0 || needle[0] == '\0')
    {
        return 1;
    }
    if (text == 0)
    {
        return 0;
    }

    for (text_index = 0u; text[text_index] != '\0'; text_index++)
    {
        needle_index = 0u;
        while (
            needle[needle_index] != '\0' &&
            text[text_index + needle_index] != '\0' &&
            TitanShellScene_LibraryAsciiLower(text[text_index + needle_index]) ==
                TitanShellScene_LibraryAsciiLower(needle[needle_index])
        )
        {
            needle_index++;
        }
        if (needle[needle_index] == '\0')
        {
            return 1;
        }
    }
    return 0;
}

static int TitanShellScene_LibraryCompareInsensitive(
    const char *left,
    const char *right
)
{
    unsigned int index;
    char a;
    char b;

    if (left == 0) left = "";
    if (right == 0) right = "";

    for (index = 0u; left[index] != '\0' || right[index] != '\0'; index++)
    {
        a = TitanShellScene_LibraryAsciiLower(left[index]);
        b = TitanShellScene_LibraryAsciiLower(right[index]);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return 0;
}

static unsigned int TitanShellScene_BuildLibraryEntries(
    TitanLibraryEntry *entries,
    unsigned int capacity
)
{
    unsigned int count;
    unsigned int index;
    unsigned int game_generation;
    unsigned int application_generation;
    unsigned int folder_count;
    int category;
    const char *query;
    TitanLibraryEntry *work_entries;

    if (entries == 0 || capacity == 0u)
        return 0u;

    category = g_scene.library_selected_category;
    query = category < 0 ? g_scene.library_search : "";
    game_generation = TitanNeutrino_GetCatalogGeneration();
    application_generation = TitanApplications_GetCatalogGeneration();
    folder_count = TitanHomeFolders_GetCount();

    if (g_library_view_cache.valid &&
        g_library_view_cache.category == category &&
        g_library_view_cache.game_generation == game_generation &&
        g_library_view_cache.application_generation == application_generation &&
        g_library_view_cache.folder_count == folder_count &&
        strcmp(g_library_view_cache.query, query) == 0)
    {
        unsigned int copy_count = g_library_view_cache.count < capacity
            ? g_library_view_cache.count : capacity;
        memcpy(entries, g_library_view_cache.entries,
            copy_count * sizeof(g_library_view_cache.entries[0]));
        g_library_view_cache.pending_hits++;
        if (g_library_view_cache.pending_hits >= 32u)
        {
            TitanVlombisCore_RecordWork(
                TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN |
                    TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
                TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT,
                g_library_view_cache.pending_hits, 0u);
            g_library_view_cache.pending_hits = 0u;
        }
        return copy_count;
    }

    memset(&g_library_view_cache, 0, sizeof(g_library_view_cache));
    work_entries = g_library_view_cache.entries;
    count = 0u;

    if (category < 0 || category == TITAN_LIBRARY_CATEGORY_ALL ||
        category == TITAN_LIBRARY_CATEGORY_FOLDERS)
    {
        for (index = 0u; index < folder_count && count < TITAN_LIBRARY_ENTRY_CAPACITY; index++)
        {
            const TitanHomeFolder *folder = TitanHomeFolders_Get(index);
            if (folder != 0 && TitanShellScene_LibraryContainsInsensitive(folder->name, query))
            {
                work_entries[count].type = TITAN_LIBRARY_ENTRY_FOLDER;
                work_entries[count].index = index;
                work_entries[count].title = folder->name;
                count++;
            }
        }
    }

    if (category < 0 || category == TITAN_LIBRARY_CATEGORY_ALL ||
        category == TITAN_LIBRARY_CATEGORY_GAMES)
    {
        unsigned int game_count = TitanNeutrino_GetGameCount();
        for (index = 0u; index < game_count && count < TITAN_LIBRARY_ENTRY_CAPACITY; index++)
        {
            const TitanNeutrinoGame *game = TitanNeutrino_GetGame(index);
            if (game != 0 && TitanShellScene_LibraryContainsInsensitive(game->title, query))
            {
                work_entries[count].type = TITAN_LIBRARY_ENTRY_GAME;
                work_entries[count].index = index;
                work_entries[count].title = game->title;
                count++;
            }
        }
    }

    if (category < 0 || category == TITAN_LIBRARY_CATEGORY_ALL ||
        category == TITAN_LIBRARY_CATEGORY_APPLICATIONS)
    {
        unsigned int application_count = TitanApplications_GetCount();
        for (index = 0u; index < application_count && count < TITAN_LIBRARY_ENTRY_CAPACITY; index++)
        {
            const TitanApplication *application = TitanApplications_Get(index);
            if (application != 0 &&
                TitanShellScene_LibraryContainsInsensitive(application->title, query))
            {
                work_entries[count].type = TITAN_LIBRARY_ENTRY_APPLICATION;
                work_entries[count].index = index;
                work_entries[count].title = application->title;
                count++;
            }
        }
    }

    /* Sort only when the view key changes. The previous BF30 rebuilt this
     * insertion sort repeatedly during count, input and draw calls. */
    for (index = 1u; index < count; index++)
    {
        TitanLibraryEntry value = work_entries[index];
        unsigned int position = index;
        while (position > 0u &&
            TitanShellScene_LibraryCompareInsensitive(
                work_entries[position - 1u].title, value.title) > 0)
        {
            work_entries[position] = work_entries[position - 1u];
            position--;
        }
        work_entries[position] = value;
    }

    g_library_view_cache.count = count;
    g_library_view_cache.game_generation = game_generation;
    g_library_view_cache.application_generation = application_generation;
    g_library_view_cache.folder_count = folder_count;
    g_library_view_cache.category = category;
    strncpy(g_library_view_cache.query, query, sizeof(g_library_view_cache.query) - 1u);
    g_library_view_cache.query[sizeof(g_library_view_cache.query) - 1u] = '\0';
    g_library_view_cache.valid = 1;

    if (count > capacity)
        count = capacity;
    memcpy(entries, work_entries, count * sizeof(work_entries[0]));
    return count;
}

static unsigned int TitanShellScene_GetLibraryEntryCount(void)
{
    static TitanLibraryEntry entries[TITAN_LIBRARY_ENTRY_CAPACITY];
    return TitanShellScene_BuildLibraryEntries(
        entries,
        TITAN_LIBRARY_ENTRY_CAPACITY
    );
}

static void TitanShellScene_ClampLibrarySelection(void)
{
    unsigned int count = TitanShellScene_GetLibraryEntryCount();

    if (count == 0u)
    {
        g_scene.library_selected_item = 0;
        g_scene.library_item_position = 0.0f;
        g_scene.library_grid_active = 0;
        return;
    }
    if (g_scene.library_selected_item < 0)
    {
        g_scene.library_selected_item = 0;
    }
    if ((unsigned int)g_scene.library_selected_item >= count)
    {
        g_scene.library_selected_item = (int)count - 1;
        g_scene.library_item_position = (float)g_scene.library_selected_item;
    }
}

static void TitanShellScene_ConfirmLibrarySearch(
    const char *text,
    void *user_data
)
{
    (void)user_data;
    TitanShellScene_SetTextInputMessage(
        g_scene.library_search,
        sizeof(g_scene.library_search),
        text != 0 ? text : ""
    );
    g_scene.library_selected_item = 0;
    g_scene.library_item_position = 0.0f;
    TitanShellScene_ClampLibrarySelection();
}

static int TitanShellScene_OpenLibrarySearchInput(void)
{
    TitanTextInputRequest request;

    request.title = TitanLocalization_Compat("Search");
    request.initial_text = g_scene.library_search;
    request.placeholder = TitanLocalization_Compat("Search Library");
    request.maximum_codepoints = 48u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = 0;
    request.confirm_callback = TitanShellScene_ConfirmLibrarySearch;
    request.cancel_callback = 0;
    request.user_data = 0;
    return TitanTextInput_Open(&request);
}

static void TitanShellScene_DrawLibrarySearchIcon(
    float x,
    float y,
    unsigned char alpha
)
{
    TitanVideo_DrawCircleAlpha(x, y, 9.0f, 242, 244, 248, alpha);
    TitanVideo_DrawCircleAlpha(x, y, 6.6f, 7, 7, 11, alpha);
    TitanVideo_DrawLineAlpha(
        x + 6.5f,
        y + 6.5f,
        x + 14.0f,
        y + 14.0f,
        2.2f,
        242,
        244,
        248,
        alpha
    );
}

static void TitanShellScene_DrawLibraryCategoryIcon(
    TitanLibraryCategory category,
    float x,
    float y,
    unsigned char alpha
)
{
    if (category == TITAN_LIBRARY_CATEGORY_ALL)
    {
        const float size = 8.0f;
        const float gap = 4.0f;
        unsigned int row;
        unsigned int column;
        for (row = 0u; row < 2u; row++)
        {
            for (column = 0u; column < 2u; column++)
            {
                TitanVideo_DrawRectAlpha(
                    x + ((float)column * (size + gap)),
                    y + ((float)row * (size + gap)),
                    size, size, 242, 244, 248, alpha
                );
            }
        }
    }
    else if (category == TITAN_LIBRARY_CATEGORY_APPLICATIONS)
    {
        /* Preserve the Applications glyph that used to live on Home. */
        TitanIcons_Draw(TITAN_ICON_APPLICATIONS, x + 13.0f, y + 12.0f,
            0.42f, 242, 244, 248, alpha);
    }
    else if (category == TITAN_LIBRARY_CATEGORY_FOLDERS)
    {
        TitanVideo_DrawRoundedRectAlpha(
            x,
            y + 5.0f,
            25.0f,
            16.0f,
            2.0f,
            242, 244, 248, alpha
        );
        TitanVideo_DrawRectAlpha(
            x + 2.0f,
            y + 1.0f,
            10.0f,
            6.0f,
            242, 244, 248, alpha
        );
    }
    else
    {
        /* PS4 Library gamepad glyph, deliberately vector-drawn so it keeps
         * the Orbis silhouette without adding/changing a curated asset. */
        TitanVideo_DrawRoundedRectAlpha(
            x,
            y + 4.0f,
            29.0f,
            17.0f,
            7.0f,
            242, 244, 248, alpha
        );
        TitanVideo_DrawRectAlpha(x + 7.0f, y + 10.0f, 8.0f, 2.0f, 38, 40, 46, alpha);
        TitanVideo_DrawRectAlpha(x + 10.0f, y + 7.0f, 2.0f, 8.0f, 38, 40, 46, alpha);
        TitanVideo_DrawCircleAlpha(x + 21.0f, y + 10.0f, 1.7f, 38, 40, 46, alpha);
        TitanVideo_DrawCircleAlpha(x + 25.0f, y + 14.0f, 1.7f, 38, 40, 46, alpha);
    }
}

static void TitanShellScene_DrawLibraryEntry(
    const TitanLibraryEntry *entry,
    float x,
    float y,
    unsigned char alpha,
    int focused
)
{
    char line1[96];
    char line2[96];
    unsigned int consumed_bytes;
    unsigned char border_alpha;

    if (entry == 0 || entry->title == 0)
    {
        return;
    }

    border_alpha = focused ? alpha : TitanShellScene_ModulateAlpha(35u, alpha);
    TitanVideo_DrawRectAlpha(
        x,
        y,
        172.0f,
        76.0f,
        83, 86, 96,
        TitanShellScene_ModulateAlpha(focused ? 156u : 110u, alpha)
    );
    TitanShellScene_DrawOutline(
        x,
        y,
        172.0f,
        76.0f,
        focused ? 2.0f : 1.0f,
        242, 244, 248,
        border_alpha
    );

    TitanVideo_DrawRectAlpha(
        x,
        y,
        78.0f,
        76.0f,
        20, 21, 27,
        TitanShellScene_ModulateAlpha(232u, alpha)
    );

    if (entry->type == TITAN_LIBRARY_ENTRY_GAME)
    {
        if (!TitanAssetService_DrawIcon(
                entry->index,
                x + 8.0f,
                y + 7.0f,
                62.0f,
                62.0f,
                alpha))
        {
            TitanUiAssets_DrawGamePlaceholder(
                x + 39.0f,
                y + 38.0f,
                62.0f,
                62.0f,
                242, 244, 248,
                alpha
            );
        }
    }
    else if (entry->type == TITAN_LIBRARY_ENTRY_APPLICATION)
    {
        if (!TitanAppAssets_DrawIcon(
                entry->index,
                x + 9.0f,
                y + 8.0f,
                60.0f,
                60.0f,
                alpha))
        {
            TitanIcons_Draw(
                TITAN_ICON_APPLICATIONS,
                x + 39.0f,
                y + 38.0f,
                0.90f,
                242, 244, 248,
                alpha
            );
        }
    }
    else
    {
        TitanVideo_DrawRoundedRectAlpha(
            x + 16.0f,
            y + 27.0f,
            46.0f,
            29.0f,
            4.0f,
            242, 244, 248,
            alpha
        );
        TitanVideo_DrawRectAlpha(
            x + 19.0f,
            y + 21.0f,
            19.0f,
            9.0f,
            242, 244, 248,
            alpha
        );
    }

    consumed_bytes = 0u;
    TitanShellScene_CopyFittedTextPrefix(
        line1, sizeof(line1), entry->title, 0.48f, 82.0f, &consumed_bytes
    );
    line2[0] = '\0';
    if (entry->title[consumed_bytes] != '\0')
    {
        TitanShellScene_CopyEllipsizedText(
            line2, sizeof(line2), entry->title + consumed_bytes,
            0.48f, 82.0f
        );
    }

    TitanFont_DrawTextAlpha(
        x + 83.0f,
        y + 10.0f,
        0.48f,
        line1,
        238, 240, 245,
        alpha
    );
    if (line2[0] != '\0')
    {
        TitanFont_DrawTextAlpha(
            x + 83.0f,
            y + 28.0f,
            0.48f,
            line2,
            238, 240, 245,
            alpha
        );
    }
}

static void TitanShellScene_DrawLibraryPage(
    float offset_x,
    unsigned char alpha
)
{
    static TitanLibraryEntry entries[TITAN_LIBRARY_ENTRY_CAPACITY];
    static const char *category_names[TITAN_LIBRARY_CATEGORY_COUNT] =
    {
        "All",
        "Folders",
        "Games",
        "Applications"
    };
    unsigned int counts[TITAN_LIBRARY_CATEGORY_COUNT];
    unsigned int count;
    unsigned int first;
    unsigned int selected;
    unsigned int index;
    unsigned int total_rows;
    unsigned int first_row;
    unsigned int max_first_row;
    unsigned int asset_visible_games[TITAN_ASSET_VISIBLE_GAME_CAPACITY];
    unsigned int asset_visible_count = 0u;
    unsigned int asset_selected_game = 0xffffffffu;
    float sidebar_x;
    float category_y;
    float grid_x;
    float grid_y;
    float cell_gap_x;
    float cell_gap_y;
    int sidebar_selection;
    char count_text[16];

    counts[TITAN_LIBRARY_CATEGORY_ALL] =
        TitanHomeFolders_GetCount() +
        TitanNeutrino_GetGameCount() +
        TitanApplications_GetCount();
    counts[TITAN_LIBRARY_CATEGORY_FOLDERS] = TitanHomeFolders_GetCount();
    counts[TITAN_LIBRARY_CATEGORY_GAMES] = TitanNeutrino_GetGameCount();
    counts[TITAN_LIBRARY_CATEGORY_APPLICATIONS] = TitanApplications_GetCount();

    count = TitanShellScene_BuildLibraryEntries(
        entries,
        TITAN_LIBRARY_ENTRY_CAPACITY
    );
    selected = g_scene.library_selected_item < 0
        ? 0u
        : (unsigned int)g_scene.library_selected_item;
    if (count > 0u && selected >= count)
    {
        selected = count - 1u;
    }

    TitanFont_DrawTextAlpha(
        48.0f + offset_x,
        24.0f,
        1.12f,
        "Library",
        246, 247, 250,
        alpha
    );
    TitanVideo_DrawRectAlpha(
        0.0f + offset_x,
        61.0f,
        (float)TITAN_UI_WIDTH,
        1.0f,
        226, 229, 235,
        TitanShellScene_ModulateAlpha(92u, alpha)
    );

    sidebar_x = 70.0f + offset_x;
    sidebar_selection = !g_scene.library_grid_active;

    TitanShellScene_DrawLibrarySearchIcon(
        sidebar_x + 8.0f,
        90.0f,
        alpha
    );
    TitanFont_DrawTextAlpha(
        sidebar_x + 34.0f,
        81.0f,
        0.56f,
        g_scene.library_search[0] != '\0' ? g_scene.library_search : "Search",
        235, 238, 244,
        alpha
    );
    if (sidebar_selection && g_scene.library_selected_category < 0)
    {
        TitanShellScene_DrawOutline(
            sidebar_x - 10.0f,
            70.0f,
            166.0f,
            42.0f,
            2.0f,
            244, 246, 250,
            alpha
        );
    }

    TitanFont_DrawTextAlpha(
        sidebar_x,
        126.0f,
        0.50f,
        "This PS2",
        196, 200, 211,
        alpha
    );
    TitanVideo_DrawRectAlpha(
        sidebar_x,
        146.0f,
        166.0f,
        1.0f,
        215, 218, 225,
        TitanShellScene_ModulateAlpha(80u, alpha)
    );

    for (index = 0u; index < TITAN_LIBRARY_CATEGORY_COUNT; index++)
    {
        category_y = 164.0f + ((float)index * 48.0f);
        if (
            sidebar_selection &&
            g_scene.library_selected_category == (int)index
        )
        {
            TitanShellScene_DrawOutline(
                sidebar_x - 10.0f,
                category_y - 12.0f,
                166.0f,
                41.0f,
                2.0f,
                244, 246, 250,
                alpha
            );
        }
        TitanShellScene_DrawLibraryCategoryIcon(
            (TitanLibraryCategory)index,
            sidebar_x,
            category_y - 2.0f,
            alpha
        );
        TitanFont_DrawTextAlpha(
            sidebar_x + 38.0f,
            category_y,
            0.56f,
            category_names[index],
            238, 240, 245,
            alpha
        );
        snprintf(count_text, sizeof(count_text), "%u", counts[index]);
        TitanFont_DrawTextAlpha(
            sidebar_x + 137.0f,
            category_y,
            0.50f,
            count_text,
            238, 240, 245,
            alpha
        );
    }

    TitanVideo_DrawRectAlpha(
        243.0f + offset_x,
        64.0f,
        1.0f,
        344.0f,
        224, 227, 233,
        TitanShellScene_ModulateAlpha(74u, alpha)
    );

    /* Name A-Z dropdown mirrors the Orbis Library control. The current
       catalog is actually sorted A-Z by BuildLibraryEntries above. */
    TitanVideo_DrawRectAlpha(
        466.0f + offset_x,
        69.0f,
        337.0f,
        35.0f,
        41, 43, 51,
        TitanShellScene_ModulateAlpha(196u, alpha)
    );
    TitanShellScene_DrawOutline(
        466.0f + offset_x,
        69.0f,
        337.0f,
        35.0f,
        1.4f,
        226, 229, 235,
        TitanShellScene_ModulateAlpha(165u, alpha)
    );
    TitanFont_DrawTextAlpha(
        476.0f + offset_x,
        78.0f,
        0.52f,
        "Name: A-Z",
        224, 227, 234,
        alpha
    );
    TitanVideo_DrawLineAlpha(782.0f + offset_x, 82.0f, 790.0f + offset_x, 90.0f, 1.2f, 224,227,234,alpha);
    TitanVideo_DrawLineAlpha(790.0f + offset_x, 90.0f, 798.0f + offset_x, 82.0f, 1.2f, 224,227,234,alpha);

    if (count == 0u)
    {
        TitanFont_DrawTextAlpha(
            276.0f + offset_x,
            138.0f,
            0.62f,
            g_scene.library_selected_category < 0 && g_scene.library_search[0] != '\0'
                ? "No matching content"
                : "No content",
            230, 233, 240,
            alpha
        );
        return;
    }

    total_rows = (count + TITAN_LIBRARY_VISIBLE_COLUMNS - 1u) /
        TITAN_LIBRARY_VISIBLE_COLUMNS;
    first_row = selected / TITAN_LIBRARY_VISIBLE_COLUMNS;
    if (first_row >= TITAN_LIBRARY_VISIBLE_ROWS)
    {
        first_row -= TITAN_LIBRARY_VISIBLE_ROWS - 1u;
    }
    else
    {
        first_row = 0u;
    }
    max_first_row = total_rows > TITAN_LIBRARY_VISIBLE_ROWS
        ? total_rows - TITAN_LIBRARY_VISIBLE_ROWS
        : 0u;
    if (first_row > max_first_row)
    {
        first_row = max_first_row;
    }
    first = first_row * TITAN_LIBRARY_VISIBLE_COLUMNS;

    grid_x = 264.0f + offset_x;
    grid_y = 115.0f;
    cell_gap_x = 9.0f;
    cell_gap_y = 7.0f;

    for (index = 0u;
         index < TITAN_LIBRARY_VISIBLE_ITEMS && first + index < count;
         index++)
    {
        const TitanLibraryEntry *asset_entry = &entries[first + index];
        if (asset_entry->type == TITAN_LIBRARY_ENTRY_GAME &&
            asset_visible_count < TITAN_ASSET_VISIBLE_GAME_CAPACITY)
        {
            asset_visible_games[asset_visible_count++] = asset_entry->index;
            if (first + index == selected) asset_selected_game = asset_entry->index;
        }
    }
    TitanAssetService_SetVisibleGames(
        asset_selected_game,
        asset_visible_games,
        asset_visible_count,
        g_scene.local_frame
    );

    for (
        index = 0u;
        index < TITAN_LIBRARY_VISIBLE_ITEMS && first + index < count;
        index++
    )
    {
        unsigned int item_index = first + index;
        unsigned int row = index / TITAN_LIBRARY_VISIBLE_COLUMNS;
        unsigned int column = index % TITAN_LIBRARY_VISIBLE_COLUMNS;
        TitanShellScene_DrawLibraryEntry(
            &entries[item_index],
            grid_x + ((float)column * (172.0f + cell_gap_x)),
            grid_y + ((float)row * (76.0f + cell_gap_y)),
            alpha,
            g_scene.library_grid_active && item_index == selected
        );
    }

    if (total_rows > TITAN_LIBRARY_VISIBLE_ROWS)
    {
        float track_y = 115.0f;
        float track_h = 324.0f;
        float thumb_h = track_h *
            ((float)TITAN_LIBRARY_VISIBLE_ROWS / (float)total_rows);
        float thumb_y;
        if (thumb_h < 26.0f) thumb_h = 26.0f;
        thumb_y = track_y +
            ((track_h - thumb_h) *
            ((float)first_row / (float)max_first_row));
        TitanVideo_DrawRectAlpha(831.0f + offset_x, track_y, 2.0f, track_h, 220,223,230, TitanShellScene_ModulateAlpha(64u,alpha));
        TitanVideo_DrawRectAlpha(831.0f + offset_x, thumb_y, 2.0f, thumb_h, 244,246,250, TitanShellScene_ModulateAlpha(210u,alpha));
    }
}


static void TitanShellScene_OpenLibrarySelectedEntry(void)
{
    static TitanLibraryEntry entries[TITAN_LIBRARY_ENTRY_CAPACITY];
    unsigned int count;
    unsigned int selected;
    const TitanLibraryEntry *entry;

    count = TitanShellScene_BuildLibraryEntries(
        entries,
        TITAN_LIBRARY_ENTRY_CAPACITY
    );
    if (count == 0u)
    {
        return;
    }

    selected = g_scene.library_selected_item < 0
        ? 0u
        : (unsigned int)g_scene.library_selected_item;
    if (selected >= count)
    {
        selected = count - 1u;
    }
    entry = &entries[selected];

    if (entry->type == TITAN_LIBRARY_ENTRY_GAME)
    {
        TitanShellScene_BeginGameLaunch(entry->index);
    }
    else if (entry->type == TITAN_LIBRARY_ENTRY_APPLICATION)
    {
        TitanShellScene_BeginApplicationLaunch(entry->index);
    }
    else
    {
        const TitanHomeFolder *folder = TitanHomeFolders_Get(entry->index);
        if (folder != 0 && folder->item_count > 0u)
        {
            g_scene.folder_open = 1;
            g_scene.folder_open_index = entry->index;
            g_scene.folder_open_selected = 0;
            g_scene.folder_open_selected_position = 0.0f;
        }
    }
}

static void TitanShellScene_DrawApplicationsPage(
    float offset_x,
    unsigned char alpha
)
{
    unsigned int count;
    unsigned int first;
    unsigned int visible;
    unsigned int selected;

    TitanShellScene_DrawPanelBase(
        "Applications",
        "ELF applications and Titan Apps detected on connected storage",
        offset_x,
        alpha
    );

    count = TitanApplications_GetCount();
    selected = g_scene.applications_selected_row < 0
        ? 0u
        : (unsigned int)g_scene.applications_selected_row;

    if (count == 0u)
    {
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_APPLICATIONS,
            "No applications found",
            "Scan mass:/APPS, mass:/TITAN/apps and connected storage.",
            "TRIANGLE Scan again",
            102.0f,
            offset_x,
            alpha,
            1.0f
        );
        return;
    }

    if (selected >= count)
    {
        selected = count - 1u;
    }

    first = selected >= TITAN_APPLICATION_VISIBLE_ROWS - 1u
        ? selected - (TITAN_APPLICATION_VISIBLE_ROWS - 1u)
        : 0u;

    if (first + TITAN_APPLICATION_VISIBLE_ROWS > count)
    {
        first = count > TITAN_APPLICATION_VISIBLE_ROWS
            ? count - TITAN_APPLICATION_VISIBLE_ROWS
            : 0u;
    }

    for (
        visible = 0u;
        visible < TITAN_APPLICATION_VISIBLE_ROWS && first + visible < count;
        visible++
    )
    {
        unsigned int application_index;
        const TitanApplication *application;
        float focus;

        application_index = first + visible;
        application = TitanApplications_Get(application_index);
        if (application == 0)
        {
            continue;
        }

        focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute(
                (float)application_index - g_scene.applications_row_position
            ),
            0.0f,
            1.0f
        );

        {
            char app_value[80];
            const char *app_description;

            app_description = application->description[0] != '\0'
                ? application->description
                : application->path;
            snprintf(
                app_value,
                sizeof(app_value),
                "%s%s%s",
                application->type == TITAN_APPLICATION_SYSTEM_BROWSER
                    ? "System App"
                    : (application->type == TITAN_APPLICATION_TITAN_NATIVE
                        ? "Titan App"
                        : "ELF"),
                application->version[0] != '\0' ? " / v" : "",
                application->version[0] != '\0' ? application->version : ""
            );
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_APPLICATIONS,
                application->title,
                app_description,
                app_value,
                TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP),
                offset_x,
                alpha,
                focus
            );
        }
    }

    TitanShellScene_DrawSettingsScrollbar(
        (int)count,
        (int)selected,
        offset_x,
        alpha
    );
}


static void TitanShellScene_DrawStoragePage(
    float offset_x,
    unsigned char alpha
)
{
    TitanShellScene_DrawPanelBase(
        "Storage",
        "Filesystem and protected system resources",
        offset_x,
        alpha
    );

    TitanShellScene_DrawValueRow(
        "Filesystem service",
        "Planned",
        199.0f,
        offset_x,
        alpha,
        0
    );

    TitanShellScene_DrawValueRow(
        "Device discovery",
        "Pending",
        235.0f,
        offset_x,
        alpha,
        0
    );

    TitanShellScene_DrawValueRow(
        "Memory card",
        "Boot environment",
        271.0f,
        offset_x,
        alpha,
        0
    );

    TitanShellScene_DrawValueRow(
        "USB storage",
        "System and apps",
        307.0f,
        offset_x,
        alpha,
        0
    );

    TitanShellScene_DrawValueRow(
        "Protection",
        "System files hidden",
        343.0f,
        offset_x,
        alpha,
        1
    );
}

static const TitanInputDeviceDescriptor *TitanShellScene_PrimaryInputDevice(void)
{
    const TitanInputDeviceDescriptor *d = TitanUnifiedInput_GetPlayer(0u);
    unsigned int i;
    if (d != 0) return d;
    for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); ++i) {
        d = TitanUnifiedInput_GetDevice(i);
        if (d != 0 && d->connected) return d;
    }
    return 0;
}

static const char *TitanShellScene_EarlyInputStateName(TitanEarlyInputState state)
{
    switch (state) {
        case TITAN_EARLY_INPUT_STATE_STARTING: return "Starting";
        case TITAN_EARLY_INPUT_STATE_LOADING: return "Loading";
        case TITAN_EARLY_INPUT_STATE_READY: return "Ready";
        case TITAN_EARLY_INPUT_STATE_FAILED: return "Failed";
        case TITAN_EARLY_INPUT_STATE_TIMED_OUT: return "Timed out";
        default: return "Idle";
    }
}

static const char *TitanShellScene_EarlyBackendName(TitanEarlyInputBackend backend)
{
    switch(backend){case TITAN_EARLY_INPUT_USB:return "USB";case TITAN_EARLY_INPUT_BT:return "Bluetooth";default:return "Disabled";}
}

static const char *TitanShellScene_BtStageName(uint32_t stage)
{
    switch(stage){
        case TITAN_PADEMU_BT_STAGE_DRIVER_REGISTERED:return "driver registered";
        case TITAN_PADEMU_BT_STAGE_PROBE_SEEN:return "USB probe";
        case TITAN_PADEMU_BT_STAGE_PROBE_ACCEPTED:return "USB probe accepted";
        case TITAN_PADEMU_BT_STAGE_CONNECT_SEEN:return "USB connect";
        case TITAN_PADEMU_BT_STAGE_ENDPOINTS_READY:return "endpoints ready";
        case TITAN_PADEMU_BT_STAGE_CONFIG_SUBMITTED:return "USB set-config";
        case TITAN_PADEMU_BT_STAGE_CONFIGURED:return "USB configured";
        case TITAN_PADEMU_BT_STAGE_HCI_RESET_SUBMITTED:return "HCI reset";
        case TITAN_PADEMU_BT_STAGE_HCI_RESET_COMPLETE:return "HCI reset OK";
        case TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_SUBMITTED:return "scan enable";
        case TITAN_PADEMU_BT_STAGE_READY:return "HCI ready";
        case TITAN_PADEMU_BT_STAGE_PROBE_REJECTED:return "USB probe rejected";
        case TITAN_PADEMU_BT_STAGE_CONNECT_FAILED:return "USB connect failed";
        case TITAN_PADEMU_BT_STAGE_CONFIG_FAILED:return "USB set-config failed";
        case TITAN_PADEMU_BT_STAGE_HCI_RESET_FAILED:return "HCI reset failed";
        case TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_FAILED:return "scan enable failed";
        case TITAN_PADEMU_BT_STAGE_DRIVER_REGISTER_FAILED:return "USB driver failed";
        case TITAN_PADEMU_BT_STAGE_DISCONNECTED:return "dongle disconnected";
        default:return "idle/no dongle";
    }
}

static const char *TitanShellScene_PairingStateName(TitanPademuPairingState state)
{
    switch(state){
        case TITAN_PADEMU_PAIRING_PENDING:return "Starting...";
        case TITAN_PADEMU_PAIRING_STARTED:return "Listening";
        case TITAN_PADEMU_PAIRING_UNAVAILABLE:return "BT dongle / PADEMU not ready";
        case TITAN_PADEMU_PAIRING_ERROR:return "Pairing command failed";
        case TITAN_PADEMU_PAIRING_TIMED_OUT:return "PADEMU RPC timed out";
        default:return "Press X to start listening";
    }
}

static void TitanShellScene_DrawInputPage(
    const TitanInputState *input,
    float offset_x,
    unsigned char alpha
)
{
    const TitanInputDeviceDescriptor *d = TitanShellScene_PrimaryInputDevice();
    const TitanEarlyInputStatus *early = TitanEarlyInput_GetStatus();
    const TitanPademuPairingRuntime *pairing = TitanPademuInputBridge_GetBluetoothPairingRuntime();
    TitanEarlyInputBackend configured = TitanEarlyInput_GetConfiguredBackend();
    char link_text[96], player_text[96], battery_text[96], runtime_text[96], early_text[128], pair_text[96];
    (void)input;
    TitanShellScene_DrawPanelBase("Controllers", "Pairing, startup backend and live diagnostics", offset_x, alpha);
    if(d){
        snprintf(link_text,sizeof(link_text),"%s / %s  %04X:%04X",TitanUnifiedInput_TransportName(d->transport),TitanUnifiedInput_BackendName(d->backend),(unsigned)d->vendor_id,(unsigned)d->product_id);
        snprintf(player_text,sizeof(player_text),"Controller %u  System %s  Rumble %s",d->player<TITAN_UNIFIED_INPUT_MAX_PLAYERS?(unsigned)d->player+1u:0u,(d->capabilities&TITAN_INPUT_CAP_SYSTEM_BUTTON)?"yes":"no",(d->capabilities&TITAN_INPUT_CAP_RUMBLE)?"yes":"no");
        if(d->battery_valid)snprintf(battery_text,sizeof(battery_text),"%s %u%%  raw=%u",TitanUnifiedInput_BatteryStateName(d->battery_state),(unsigned)d->battery_percent,(unsigned)d->battery_raw);
        else snprintf(battery_text,sizeof(battery_text),"%s  raw=%u",TitanUnifiedInput_BatteryStateName(d->battery_state),(unsigned)d->battery_raw);
        snprintf(runtime_text,sizeof(runtime_text),"pkt=%lu reconnect=%lu err=%lu latency=%luus",(unsigned long)d->packet_rate,(unsigned long)d->reconnect_count,(unsigned long)d->errors,(unsigned long)d->last_latency_us);
    } else {snprintf(link_text,sizeof(link_text),"No active controller");snprintf(player_text,sizeof(player_text),"Unassigned");snprintf(battery_text,sizeof(battery_text),"Unsupported");snprintf(runtime_text,sizeof(runtime_text),"pkt=0 reconnect=0 err=0 latency=0us");}
    snprintf(early_text,sizeof(early_text),"%s  runtime=%s  irx=%d rc=%d pumps=%u",TitanShellScene_EarlyBackendName(configured),early?TitanShellScene_EarlyInputStateName(early->state):"Unavailable",early?early->pademu_module_id:-1,early?early->last_module_result:0,early?early->pump_count:0u);
    if(configured!=TITAN_EARLY_INPUT_BT)snprintf(pair_text,sizeof(pair_text),"Select Bluetooth, then reboot");
    else if(!early||early->state!=TITAN_EARLY_INPUT_STATE_READY)snprintf(pair_text,sizeof(pair_text),"Backend %s (rc=%d)",early?TitanShellScene_EarlyInputStateName(early->state):"Unavailable",early?early->last_module_result:0);
    else if(pairing && (pairing->state==TITAN_PADEMU_PAIRING_UNAVAILABLE || pairing->state==TITAN_PADEMU_PAIRING_ERROR))
        snprintf(pair_text,sizeof(pair_text),"%s rc=%d usb=%02X ep=%X",TitanShellScene_BtStageName(pairing->transport_stage),(int)pairing->transport_error,(unsigned)pairing->usb_status,(unsigned)pairing->endpoints_mask);
    else snprintf(pair_text,sizeof(pair_text),"%s",pairing?TitanShellScene_PairingStateName(pairing->state):"Unavailable");

    TitanShellScene_DrawValueRow("Startup input backend", early_text, 163.0f, offset_x, alpha, g_scene.input_selected_row==0);
    TitanShellScene_DrawValueRow("Pair wireless controller", pair_text, 199.0f, offset_x, alpha, g_scene.input_selected_row==1);
    TitanShellScene_DrawValueRow("Device", d?TitanUnifiedInput_DeviceName(d->type):"None", 235.0f, offset_x, alpha, 0);
    TitanShellScene_DrawValueRow("Transport / backend", link_text, 271.0f, offset_x, alpha, 0);
    TitanShellScene_DrawValueRow("Assignment / capabilities", player_text, 307.0f, offset_x, alpha, 0);
    TitanShellScene_DrawValueRow("Battery", battery_text, 343.0f, offset_x, alpha, 0);
    if(TitanDex_IsEnabled()) TitanShellScene_DrawValueRow("DEX runtime", runtime_text, 379.0f, offset_x, alpha, 0);
    TitanFont_DrawTextAlpha(54.0f+offset_x, 424.0f, 0.45f, "Up/Down Select   Left/Right Change backend   X Pair   O Back", 212,216,226,alpha);
}

static void TitanShellScene_DrawMenuOptionRow(
    TitanIconId icon,
    const char *title,
    const char *line1,
    const char *line2,
    float x,
    float y,
    float width,
    unsigned char alpha,
    float focus
);

static void TitanShellScene_DrawNotificationsPage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanBackgroundActivityStatus *activity;
    const TitanBackgroundActivityStatus *downloads;
    static const char *titles[] =
    {
        "Library Activity",
        "Downloads",
        "Uploads",
        "System",
        "Clear All"
    };
    static const char *default_lines1[] =
    {
        "No library task is active right now.",
        "No background downloads are active right now.",
        "No background uploads are active right now.",
        "System notices and completed tasks are stored here.",
        "Remove completed entries from the notification center."
    };
    static const char *default_lines2[] =
    {
        "A background scan appears here when storage is verified.",
        "Downloads and updates will use this activity list.",
        "Future transfer services will use this activity list.",
        "Errors and storage interruptions remain visible.",
        "Active tasks cannot be removed while they are running."
    };
    char activity_counts[144];
    char download_counts[144];
    const char *line1;
    const char *line2;
    int index;
    float row_y;

    activity = TitanNotifications_GetBackgroundActivity();
    downloads = TitanNotifications_GetDownloadActivity();
    activity_counts[0] = '\0';
    download_counts[0] = '\0';
    if (activity != 0 && activity->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
    {
        if (activity->state == TITAN_BACKGROUND_ACTIVITY_COMPLETE)
        {
            snprintf(
                activity_counts,
                sizeof(activity_counts),
                "%u games   %u apps   Complete",
                activity->games_found,
                activity->applications_found
            );
        }
        else if (activity->total > 0u)
        {
            snprintf(
                activity_counts,
                sizeof(activity_counts),
                "%u / %u items   %u games   %u apps   %u%%",
                activity->processed,
                activity->total,
                activity->games_found,
                activity->applications_found,
                activity->progress_percent
            );
        }
        else
        {
            snprintf(
                activity_counts,
                sizeof(activity_counts),
                "%u items scanned   %u games   %u apps   %u%%",
                activity->processed,
                activity->games_found,
                activity->applications_found,
                activity->progress_percent
            );
        }
    }

    if (downloads != 0 && downloads->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
    {
        unsigned int speed_kib = (downloads->transfer_bytes_per_second + 512u) / 1024u;
        unsigned int received_kib = (downloads->transfer_bytes_received + 1023u) / 1024u;
        unsigned int total_kib = (downloads->transfer_bytes_total + 1023u) / 1024u;
        if (downloads->total > 0u && downloads->transfer_bytes_total > 0u)
            snprintf(download_counts, sizeof(download_counts),
                "%u / %u assets  %u%% | atual %u%%  %u KiB/s  %u/%u KiB",
                downloads->processed, downloads->total, downloads->progress_percent,
                downloads->transfer_progress_percent, speed_kib, received_kib, total_kib);
        else if (downloads->total > 0u && downloads->transfer_bytes_received > 0u)
            snprintf(download_counts, sizeof(download_counts),
                "%u / %u assets  %u%% | %u KiB/s  %u KiB",
                downloads->processed, downloads->total, downloads->progress_percent,
                speed_kib, received_kib);
        else if (downloads->total > 0u)
            snprintf(download_counts, sizeof(download_counts), "%u / %u assets   %u%%",
                downloads->processed, downloads->total, downloads->progress_percent);
        else
            snprintf(download_counts, sizeof(download_counts), "%u assets processados", downloads->processed);
    }

    TitanShellScene_DrawPanelBase(
        "Notifications",
        "Background activity, system alerts and recent results",
        offset_x,
        alpha
    );

    /* Progress bars are painted before rows so the selected-card outline is always the top layer. */
    if (activity != 0 && activity->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
    {
        float progress_width = 476.0f * ((float)activity->progress_percent / 100.0f);
        unsigned char progress_alpha = TitanShellScene_ModulateAlpha(230u, alpha);
        /* Keep the bar below both text baselines instead of crossing line 2. */
        TitanVideo_DrawRectAlpha(188.0f + offset_x, TITAN_SETTINGS_ROW_TOP + 60.0f,
            476.0f, 1.0f, 56u, 61u, 72u, TitanShellScene_ModulateAlpha(176u, alpha));
        TitanVideo_DrawRectAlpha(188.0f + offset_x, TITAN_SETTINGS_ROW_TOP + 60.0f,
            progress_width, 1.0f,
            activity->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 223u : 236u,
            activity->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 46u : 238u,
            activity->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 58u : 244u, progress_alpha);
    }

    if (downloads != 0 && downloads->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
    {
        float row_base = TITAN_SETTINGS_ROW_TOP + TITAN_SETTINGS_ROW_STEP;
        float overall_track = 344.0f;
        float transfer_track = 124.0f;
        float overall_width = overall_track * ((float)downloads->progress_percent / 100.0f);
        /* Keep both rails on one inset baseline. They no longer reach the
         * selected-card bottom outline even after 720p scaling. */
        TitanVideo_DrawRectAlpha(188.0f + offset_x, row_base + 60.0f, overall_track, 1.0f,
            56u, 61u, 72u, TitanShellScene_ModulateAlpha(176u, alpha));
        TitanVideo_DrawRectAlpha(188.0f + offset_x, row_base + 60.0f, overall_width, 1.0f,
            downloads->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 223u : 236u,
            downloads->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 46u : 238u,
            downloads->state == TITAN_BACKGROUND_ACTIVITY_ERROR ? 58u : 244u,
            TitanShellScene_ModulateAlpha(230u, alpha));
        if (downloads->state == TITAN_BACKGROUND_ACTIVITY_RUNNING &&
            downloads->transfer_bytes_total > 0u)
        {
            float transfer_width = transfer_track * ((float)downloads->transfer_progress_percent / 100.0f);
            TitanVideo_DrawRectAlpha(540.0f + offset_x, row_base + 60.0f, transfer_track, 1.0f,
                42u, 47u, 58u, TitanShellScene_ModulateAlpha(150u, alpha));
            TitanVideo_DrawRectAlpha(540.0f + offset_x, row_base + 60.0f, transfer_width, 1.0f,
                246u, 247u, 251u, TitanShellScene_ModulateAlpha(225u, alpha));
        }
    }
    for (index = 0; index < 5; index++)
    {
        line1 = default_lines1[index];
        line2 = default_lines2[index];
        if (index == 0 && activity != 0 && activity->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
        {
            line1 = activity->message;
            line2 = activity_counts;
        }
        else if (index == 1 && downloads != 0 && downloads->state != TITAN_BACKGROUND_ACTIVITY_IDLE)
        {
            line1 = downloads->message;
            line2 = download_counts;
        }

        row_y = TITAN_SETTINGS_ROW_TOP + ((float)index * TITAN_SETTINGS_ROW_STEP);
        TitanShellScene_DrawMenuOptionRow(
            TITAN_ICON_NOTIFICATIONS,
            index == 0 && activity != 0 && activity->state != TITAN_BACKGROUND_ACTIVITY_IDLE
                ? activity->title
                : (index == 1 && downloads != 0 && downloads->state != TITAN_BACKGROUND_ACTIVITY_IDLE
                    ? downloads->title : titles[index]),
            line1,
            line2,
            108.0f + offset_x,
            row_y,
            580.0f,
            alpha,
            TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute((float)index - g_scene.notifications_row_position), 0.0f, 1.0f)
        );
    }


}

static void TitanShellScene_DrawUserProfilePage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanUserProfile *profile;
    char profile_count[16];
    char active_position[16];

    profile = TitanUserProfiles_GetActive();

    TitanShellScene_DrawPanelBase(
        "Profile",
        "User identity, local profile data and exclusive VMC",
        offset_x,
        alpha
    );

    TitanShellScene_FormatUnsigned(
        TitanUserProfiles_GetCount(),
        profile_count,
        sizeof(profile_count)
    );

    TitanShellScene_FormatUnsigned(
        TitanUserProfiles_GetActiveIndex() + 1u,
        active_position,
        sizeof(active_position)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_USER,
        "Active profile",
        profile != 0 ? profile->name : "Unavailable",
        "Use LEFT and RIGHT to switch the current active user.",
        108.0f + offset_x,
        102.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(0.0f - g_scene.user_profile_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_PROFILE_SETTINGS,
        "Profile count",
        profile_count,
        active_position,
        108.0f + offset_x,
        172.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(1.0f - g_scene.user_profile_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_PROFILE_SETTINGS,
        "Add profile",
        "Press SQUARE to create a new user profile.",
        "The profile is stored in TITAN.DAT and receives an exclusive VMC.",
        108.0f + offset_x,
        242.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(2.0f - g_scene.user_profile_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_VMC_UTILITY,
        "Exclusive VMC",
        profile != 0
            ? TitanVmc_GetStatusName(TitanVmc_GetCachedStatus(profile))
            : "Unavailable",
        profile != 0 ? profile->vmc_path : "Press TRIANGLE to remove the profile.",
        108.0f + offset_x,
        312.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(3.0f - g_scene.user_profile_row_position), 0.0f, 1.0f)
    );
}

static void TitanShellScene_DrawSavedDataPage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanUserProfile *profile;

    profile = TitanUserProfiles_GetActive();

    TitanShellScene_DrawPanelBase(
        "VMC Utility",
        "Create, format, validate and repair the active profile VMC",
        offset_x,
        alpha
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_VMC_UTILITY,
        "VMC status",
        profile != 0
            ? TitanVmc_GetStatusName(TitanVmc_GetCachedStatus(profile))
            : "Unavailable",
        profile != 0 ? profile->vmc_path : "No active profile.",
        108.0f + offset_x,
        76.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(0.0f - g_scene.saved_data_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_VMC_UTILITY,
        "Create / verify VMC",
        "Press X",
        "Creates an 8 MiB file or validates its PS2 filesystem.",
        108.0f + offset_x,
        143.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(1.0f - g_scene.saved_data_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_VMC_UTILITY,
        "Format active VMC",
        "Press X",
        "Builds a valid PS2 superblock, IFC, FAT and root directory.",
        108.0f + offset_x,
        210.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(2.0f - g_scene.saved_data_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_VMC_UTILITY,
        "Rebuild active VMC",
        "Press X",
        "Replaces a damaged image and preserves the previous file as .bak.",
        108.0f + offset_x,
        277.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(3.0f - g_scene.saved_data_row_position), 0.0f, 1.0f)
    );

    TitanShellScene_DrawMenuOptionRow(
        TITAN_ICON_SYSTEM,
        "Physical card format",
        TitanShellScene_GetMemoryCardName(g_scene.memory_card_selected_port),
        "Erases the selected physical card after two confirmations.",
        108.0f + offset_x,
        344.0f,
        580.0f,
        alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(4.0f - g_scene.saved_data_row_position), 0.0f, 1.0f)
    );
}

static int TitanShellScene_GetSettingsCategoryCount(void)
{
    /* The debug category is intentionally the last entry so retail mode can
     * hide it without remapping any of the normal settings categories. */
    return TitanDex_IsEnabled()
        ? TITAN_SETTINGS_CATEGORY_COUNT
        : TITAN_SETTINGS_CATEGORY_COUNT - 1;
}

static const char *TitanShellScene_GetSettingsCategoryName(
    int category
)
{
    static const TitanStringId names[TITAN_SETTINGS_CATEGORY_COUNT - 1] =
    {
        TITAN_STR_USERS_PROFILES,
        TITAN_STR_SAVED_DATA,
        TITAN_STR_NETWORK,
        TITAN_STR_ACCESSORIES,
        TITAN_STR_STORAGE,
        TITAN_STR_VIDEO_OUTPUT,
        TITAN_STR_THEMES,
        TITAN_STR_SYSTEM,
        TITAN_STR_ABOUT_SOFTWARE,
        TITAN_STR_DEBUG_SETTINGS
    };

    if (category == TITAN_SETTINGS_CONTENT)
        return TitanLocalization_Compat("Content");
    if (category < 0 || category >= TITAN_SETTINGS_CATEGORY_COUNT)
        return TitanLocalization_Get(TITAN_STR_SETTINGS);
    if (category > TITAN_SETTINGS_CONTENT) category--;
    return TitanLocalization_Get(names[category]);
}

static TitanIconId TitanShellScene_GetSettingsCategoryIcon(
    int category
)
{
    switch ((TitanSettingsCategory)category)
    {
        case TITAN_SETTINGS_USERS:
            return TITAN_ICON_PROFILE_SETTINGS;
        case TITAN_SETTINGS_SAVED_DATA:
            return TITAN_ICON_VMC_UTILITY;
        case TITAN_SETTINGS_CONTENT:
            return TITAN_ICON_SYSTEM;
        case TITAN_SETTINGS_NETWORK:
            return TITAN_ICON_SYSTEM;
        case TITAN_SETTINGS_ACCESSORIES:
            /* Reuse the former Controllers identity, adapted by the Settings row renderer. */
            return TITAN_ICON_INPUT;
        case TITAN_SETTINGS_STORAGE:
            return TITAN_ICON_STORAGE;
        case TITAN_SETTINGS_VIDEO:
            return TITAN_ICON_VIDEO;
        case TITAN_SETTINGS_THEMES:
            return TITAN_ICON_SYSTEM;
        case TITAN_SETTINGS_SYSTEM:
            return TITAN_ICON_SYSTEM;
        case TITAN_SETTINGS_DEBUG:
            return TITAN_ICON_DEBUG_SETTINGS;
        case TITAN_SETTINGS_ABOUT:
        default:
            return TITAN_ICON_ABOUT;
    }
}

static int TitanShellScene_GetSettingsRowCount(
    int category
)
{
    switch ((TitanSettingsCategory)category)
    {
        case TITAN_SETTINGS_USERS:
            return 5;
        case TITAN_SETTINGS_SAVED_DATA:
            return 5;
        case TITAN_SETTINGS_CONTENT:
            return 1;
        case TITAN_SETTINGS_NETWORK:
            return 6;
        case TITAN_SETTINGS_ACCESSORIES:
        {
            unsigned int i, wired = 0u;
            for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); i++)
            {
                const TitanInputDeviceDescriptor *device = TitanUnifiedInput_GetDevice(i);
                if (device != 0 && device->connected &&
                    device->transport != TITAN_INPUT_TRANSPORT_BLUETOOTH)
                    wired++;
            }
            if (wired > (unsigned int)(TITAN_SETTINGS_MAX_ROW_COUNT - 3))
                wired = (unsigned int)(TITAN_SETTINGS_MAX_ROW_COUNT - 3);
            return 3 + (int)wired;
        }
        case TITAN_SETTINGS_STORAGE:
            return 4;
        case TITAN_SETTINGS_VIDEO:
            return 5;
        case TITAN_SETTINGS_THEMES:
        {
            unsigned int count = TitanTheme_GetCount() + 1u; /* + Adicionar tema */
            if (count > TITAN_SETTINGS_MAX_ROW_COUNT)
                count = TITAN_SETTINGS_MAX_ROW_COUNT;
            return (int)count;
        }
        case TITAN_SETTINGS_SYSTEM:
            return 16;
        case TITAN_SETTINGS_DEBUG:
            return TITAN_DEBUG_SETTINGS_ROW_COUNT;
        case TITAN_SETTINGS_ABOUT:
        default:
            return 5;
    }
}

static void TitanShellScene_DrawMenuOptionRow(
    TitanIconId icon,
    const char *title,
    const char *line1,
    const char *line2,
    float x,
    float y,
    float width,
    unsigned char alpha,
    float focus
)
{
    unsigned char title_alpha;
    unsigned char body_alpha;
    unsigned char border_alpha;
    unsigned char icon_alpha;
    unsigned char glow_outer_alpha;
    unsigned char glow_middle_alpha;
    float title_y;
    float content_x;
    float clamped_focus;
    float eased_focus;
    float card_x;
    float card_y;
    float card_width;
    float card_height;
    const TitanThemeShellStyle *theme_style;
    unsigned char accent_red;
    unsigned char accent_green;
    unsigned char accent_blue;
    float chrome_alpha;
    float text_width;
    char bounded_title[128];
    char bounded_line1[192];
    char bounded_line2[192];

    theme_style = TitanTheme_GetShellStyle();
    accent_red = theme_style->accent.red;
    accent_green = theme_style->accent.green;
    accent_blue = theme_style->accent.blue;
    chrome_alpha = TitanShellScene_Clamp(theme_style->chrome_alpha, 0.25f, 1.0f);
    clamped_focus = TitanShellScene_Clamp(focus, 0.0f, 1.0f);
    eased_focus = TitanAnimation_SmoothStep(clamped_focus);
    title_alpha = TitanShellScene_Alpha((float)alpha * 0.92f);
    body_alpha = TitanShellScene_Alpha((float)alpha * 0.80f);
    icon_alpha = TitanShellScene_Alpha((float)alpha * (0.80f + (eased_focus * 0.20f)));
    border_alpha = TitanShellScene_Alpha((float)alpha * eased_focus * chrome_alpha);
    glow_outer_alpha = TitanShellScene_Alpha((float)alpha * eased_focus * 0.18f * chrome_alpha);
    glow_middle_alpha = TitanShellScene_Alpha((float)alpha * eased_focus * 0.34f * chrome_alpha);
    title_y = y + 3.0f + (eased_focus * 10.0f);
    content_x = x + 26.0f;
    card_x = x - 2.0f;
    card_y = y - 2.0f;
    card_width = width + 4.0f;
    card_height = 70.0f;
    text_width = width > 82.0f ? width - 82.0f : width;
    TitanShellScene_CopyEllipsizedText(
        bounded_title, sizeof(bounded_title), title, 0.84f, text_width
    );
    TitanShellScene_CopyEllipsizedText(
        bounded_line1, sizeof(bounded_line1), line1, 0.54f, text_width
    );
    TitanShellScene_CopyEllipsizedText(
        bounded_line2, sizeof(bounded_line2), line2, 0.54f, text_width
    );

    /* Match the Power Options page: only the selected row receives the
       red multi-outline highlight. All non-selected rows stay text-only. */
    if (eased_focus > 0.01f)
    {
        TitanShellScene_DrawOutline(
            card_x - 3.0f,
            card_y - 3.0f,
            card_width + 6.0f,
            card_height + 6.0f,
            2.0f,
            accent_red,
            accent_green,
            accent_blue,
            glow_outer_alpha
        );

        TitanShellScene_DrawOutline(
            card_x,
            card_y,
            card_width,
            card_height,
            2.0f,
            accent_red,
            accent_green,
            accent_blue,
            glow_middle_alpha
        );

        TitanShellScene_DrawOutline(
            card_x + 2.0f,
            card_y + 2.0f,
            card_width - 4.0f,
            card_height - 4.0f,
            2.0f,
            accent_red,
            accent_green,
            accent_blue,
            border_alpha
        );
    }

    TitanIcons_Draw(
        icon,
        content_x + 8.0f,
        title_y + 8.0f,
        0.58f,
        (unsigned char)(238.0f + (eased_focus * 12.0f)),
        (unsigned char)(241.0f + (eased_focus * 11.0f)),
        (unsigned char)(248.0f + (eased_focus * 7.0f)),
        icon_alpha
    );

    TitanFont_DrawTextShadow(
        content_x + 31.0f,
        title_y,
        0.84f,
        bounded_title,
        248,
        250,
        255,
        title_alpha
    );

    if (bounded_line1[0] != '\0')
    {
        TitanFont_DrawTextAlpha(
            content_x + 31.0f,
            title_y + 21.0f,
            0.54f,
            bounded_line1,
            229,
            233,
            241,
            body_alpha
        );
    }

    if (bounded_line2[0] != '\0')
    {
        TitanFont_DrawTextAlpha(
            content_x + 31.0f,
            title_y + 39.0f,
            0.54f,
            bounded_line2,
            236,
            240,
            246,
            body_alpha
        );
    }
}

static void TitanShellScene_DrawSettingsListRow(
    TitanIconId icon,
    const char *label,
    const char *line1,
    const char *line2,
    float y,
    float offset_x,
    unsigned char alpha,
    float focus
)
{
    TitanShellScene_DrawMenuOptionRow(
        icon,
        TitanLocalization_Compat(label),
        TitanLocalization_Compat(line1 != 0 ? line1 : ""),
        TitanLocalization_Compat(line2 != 0 ? line2 : ""),
        108.0f + offset_x,
        y,
        580.0f,
        alpha,
        focus
    );
}

static void TitanShellScene_DrawSettingsScrollbar(
    int count,
    int selected,
    float offset_x,
    unsigned char alpha
)
{
    float track_height;
    float thumb_height;
    float thumb_y;

    if (count <= 1)
    {
        return;
    }

    track_height = TITAN_SETTINGS_SCROLL_HEIGHT;
    thumb_height = track_height / (float)count;

    if (thumb_height < 22.0f)
    {
        thumb_height = 22.0f;
    }

    thumb_y =
        TITAN_SETTINGS_SCROLL_TOP +
        ((track_height - thumb_height) *
        ((float)selected / (float)(count - 1)));

    TitanVideo_DrawRectAlpha(
        782.0f + offset_x,
        TITAN_SETTINGS_SCROLL_TOP,
        2.0f,
        track_height,
        230,
        232,
        238,
        TitanShellScene_Alpha((float)alpha * 0.18f)
    );

    TitanVideo_DrawRectAlpha(
        782.0f + offset_x,
        thumb_y,
        2.0f,
        thumb_height,
        248,
        249,
        252,
        TitanShellScene_Alpha((float)alpha * 0.78f)
    );
}

static void TitanShellScene_DrawSettingsRoot(
    float offset_x,
    unsigned char alpha
)
{
    static const char *descriptions1[TITAN_SETTINGS_CATEGORY_COUNT] =
    {
        "Manage users, exclusive VMC assignments and active identity.",
        "Transfer save data between VMC storage and Memory Card.",
        "Convert local PNG/JPEG game artwork to Titan TIMG assets.",
        "Configure Wi-Fi AR9271 and RJ45 Ethernet connectivity.",
        "Gerencie acessórios sem fio e veja dispositivos com fio conectados.",
        "Veja o espaço do Titan e gerencie aplicativos, jogos nativos e temas.",
        "Adjust display output, canvas size and framing behavior.",
        "Choose installed themes for the system background and visual style.",
        "Configure shell services and renderer behavior.",
        "Review Project Titan, bootloader and console information.",
        "Install packages and access Project Titan diagnostics."
    };
    static const char *descriptions2[TITAN_SETTINGS_CATEGORY_COUNT] =
    {
        "Create, remove and switch profiles.",
        "The VMC utility workflow foundation is ready.",
        "Prepare the currently selected game or the full library, offline.",
        "Choose interface, credentials and connection state.",
        "Bluetooth em uma área própria; acessórios USB/SIO2 aparecem somente aqui.",
        "Inventário em background, informações e exclusão segura de conteúdo instalado.",
        "Display settings for the current hardware mode.",
        "Open the theme gallery and apply a visual variation to Project Titan.",
        "Core options for the proof of concept.",
        "Version, BIOS and model details.",
        "Developer tools with real USB-backed actions."
    };
    int category_count;
    int index;
    int first_visible;
    int visible;
    float row_y;

    category_count = TitanShellScene_GetSettingsCategoryCount();

    TitanShellScene_DrawPanelBase(
        "Settings",
        "",
        offset_x,
        alpha
    );

    first_visible = g_scene.settings_selected_category -
        (TITAN_SETTINGS_VISIBLE_ROW_COUNT - 1);
    if (first_visible < 0)
    {
        first_visible = 0;
    }
    if (
        first_visible >
        category_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT
    )
    {
        first_visible =
            category_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT;
    }

    visible = 0;
    for (
        index = first_visible;
        index < category_count &&
            visible < TITAN_SETTINGS_VISIBLE_ROW_COUNT;
        index++, visible++
    )
    {
        row_y = TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP);

        TitanShellScene_DrawSettingsListRow(
            TitanShellScene_GetSettingsCategoryIcon(index),
            TitanShellScene_GetSettingsCategoryName(index),
            descriptions1[index],
            descriptions2[index],
            row_y,
            offset_x,
            alpha,
            TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute((float)index - g_scene.settings_category_position), 0.0f, 1.0f)
        );
    }

    TitanShellScene_DrawSettingsScrollbar(
        category_count,
        g_scene.settings_selected_category,
        offset_x,
        alpha
    );
}

static float TitanShellScene_ThemePreviewBezierY(
    float p0,
    float p1,
    float p2,
    float p3,
    float amount
)
{
    float inverse = 1.0f - amount;
    float a = inverse * inverse * inverse;
    float b = 3.0f * inverse * inverse * amount;
    float c = 3.0f * inverse * amount * amount;
    float d = amount * amount * amount;
    return p0 * a + p1 * b + p2 * c + p3 * d;
}

static void TitanShellScene_DrawThemePreview(
    const TitanThemeInfo *theme,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    const TitanThemeFlowStyle *flow;
    const TitanThemePrimaryWave *wave;
    unsigned int segment;
    unsigned int layer;
    const unsigned int segments = 9u;

    if (theme == 0 || alpha == 0u)
        return;

    flow = &theme->flow;
    wave = &flow->primary_waves[0];

    TitanVideo_DrawRectAlpha(
        x, y, width, height,
        flow->background_top.red,
        flow->background_top.green,
        flow->background_top.blue,
        TitanShellScene_Alpha((float)alpha * 0.96f)
    );
    TitanVideo_DrawRectAlpha(
        x, y + height * 0.50f, width, height * 0.50f,
        flow->background_bottom.red,
        flow->background_bottom.green,
        flow->background_bottom.blue,
        TitanShellScene_Alpha((float)alpha * 0.94f)
    );

    for (layer = 0u; layer < 4u; layer++)
    {
        float previous_x = 0.0f;
        float previous_y = 0.0f;

        for (segment = 0u; segment <= segments; segment++)
        {
            float amount = (float)segment / (float)segments;
            float logical_y = TitanShellScene_ThemePreviewBezierY(
                wave->start_y,
                wave->control1_y,
                wave->control2_y,
                wave->end_y,
                amount
            );
            float current_x = x + amount * width;
            float current_y = y +
                TitanShellScene_Clamp(
                    logical_y / (float)TITAN_UI_HEIGHT,
                    0.0f,
                    1.0f
                ) * height + ((float)layer - 1.5f) * 4.0f;

            if (segment > 0u)
            {
                int highlight = layer == 1u || layer == 2u;
                unsigned char r = highlight
                    ? flow->wave_highlight.red : flow->wave_base.red;
                unsigned char g = highlight
                    ? flow->wave_highlight.green : flow->wave_base.green;
                unsigned char b = highlight
                    ? flow->wave_highlight.blue : flow->wave_base.blue;
                unsigned char line_alpha = TitanShellScene_Alpha(
                    (float)alpha * (highlight ? 0.72f : 0.42f)
                );
                TitanVideo_DrawLineAlpha(
                    previous_x,
                    previous_y,
                    current_x,
                    current_y,
                    highlight ? 2.1f : 1.2f,
                    r, g, b, line_alpha
                );
            }
            previous_x = current_x;
            previous_y = current_y;
        }
    }
}

static void TitanShellScene_DrawThemeCheck(
    float center_x,
    float center_y,
    unsigned char alpha
)
{
    TitanVideo_DrawCircleAlpha(
        center_x, center_y, 8.0f,
        248, 249, 252, alpha
    );
    TitanVideo_DrawLineAlpha(
        center_x - 4.0f, center_y,
        center_x - 1.0f, center_y + 3.0f,
        2.0f, 34, 42, 55, alpha
    );
    TitanVideo_DrawLineAlpha(
        center_x - 1.0f, center_y + 3.0f,
        center_x + 5.0f, center_y - 4.0f,
        2.0f, 34, 42, 55, alpha
    );
}

static void TitanShellScene_DrawThemeCard(
    const TitanThemeInfo *theme,
    float x,
    float y,
    unsigned char alpha,
    int selected,
    int applied
)
{
    unsigned char border_alpha;
    unsigned char text_alpha;
    float label_x;
    char version_line[48];

    if (theme == 0)
        return;

    TitanVideo_DrawRectAlpha(
        x + TITAN_THEME_GRID_PREVIEW_WIDTH,
        y,
        TITAN_THEME_GRID_CARD_WIDTH - TITAN_THEME_GRID_PREVIEW_WIDTH,
        TITAN_THEME_GRID_CARD_HEIGHT,
        8, 11, 20,
        TitanShellScene_Alpha((float)alpha * 0.42f)
    );
    TitanShellScene_DrawThemePreview(
        theme,
        x,
        y,
        TITAN_THEME_GRID_PREVIEW_WIDTH,
        TITAN_THEME_GRID_CARD_HEIGHT,
        alpha
    );

    border_alpha = TitanShellScene_Alpha(
        (float)alpha * (selected ? 0.98f : 0.24f)
    );
    TitanShellScene_DrawOutline(
        x,
        y,
        TITAN_THEME_GRID_CARD_WIDTH,
        TITAN_THEME_GRID_CARD_HEIGHT,
        selected ? 2.0f : 1.0f,
        selected ? 248 : 214,
        selected ? 249 : 219,
        selected ? 252 : 228,
        border_alpha
    );

    label_x = x + TITAN_THEME_GRID_PREVIEW_WIDTH + 8.0f;
    text_alpha = TitanShellScene_Alpha((float)alpha * 0.94f);
    TitanFont_DrawTextAlpha(
        label_x,
        y + 13.0f,
        0.60f,
        theme->name,
        245, 247, 252,
        text_alpha
    );
    snprintf(version_line, sizeof(version_line), "v%s", theme->version);
    TitanFont_DrawTextAlpha(
        label_x,
        y + 35.0f,
        0.42f,
        version_line,
        202, 208, 220,
        TitanShellScene_Alpha((float)alpha * 0.72f)
    );
    TitanFont_DrawTextAlpha(
        label_x,
        y + 55.0f,
        0.42f,
        theme->background_engine == TITAN_THEME_BACKGROUND_FLOW3D
            ? "Flow" : theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2
            ? TitanLocalization_Get(TITAN_STR_THEME_SCENE) : TitanLocalization_Get(TITAN_STR_THEME_SYSTEM),
        theme->flow.wave_highlight.red,
        theme->flow.wave_highlight.green,
        theme->flow.wave_highlight.blue,
        TitanShellScene_Alpha((float)alpha * 0.86f)
    );

    if (applied)
    {
        TitanShellScene_DrawThemeCheck(
            x + TITAN_THEME_GRID_CARD_WIDTH - 12.0f,
            y + TITAN_THEME_GRID_CARD_HEIGHT - 12.0f,
            TitanShellScene_Alpha((float)alpha * 0.96f)
        );
    }
}

static void TitanShellScene_DrawThemeSelectionPage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanUserProfile *profile;
    unsigned int count;
    unsigned int active;
    unsigned int selected;
    unsigned int page_start;
    unsigned int visible_index;
    unsigned int page_number;
    unsigned int page_count;
    char page_text[24];
    float title_x = 46.0f + offset_x;

    count = TitanTheme_GetCount();
    active = TitanTheme_GetActiveIndex();
    selected = g_scene.settings_selected_row < 0
        ? 0u : (unsigned int)g_scene.settings_selected_row;
    if (selected > count)
        selected = count; /* count is the virtual "Add theme" tile. */

    if (count == 0u)
    {
        page_start = 0u;
        page_number = 0u;
        page_count = 1u;
    }
    else
    {
        unsigned int page_anchor = selected == count ? count - 1u : selected;
        page_start = (page_anchor / TITAN_THEME_GRID_PAGE_SIZE) *
            TITAN_THEME_GRID_PAGE_SIZE;
        page_number = page_start / TITAN_THEME_GRID_PAGE_SIZE;
        page_count = (count + TITAN_THEME_GRID_PAGE_SIZE - 1u) /
            TITAN_THEME_GRID_PAGE_SIZE;
    }

    TitanFont_DrawTextShadow(
        title_x,
        30.0f,
        1.05f,
        "Select theme",
        248, 250, 255,
        TitanShellScene_Alpha((float)alpha * 0.98f)
    );
    TitanVideo_DrawRectAlpha(
        44.0f + offset_x,
        67.0f,
        764.0f,
        1.0f,
        235, 238, 245,
        TitanShellScene_Alpha((float)alpha * 0.28f)
    );

    if (page_count > 1u)
    {
        snprintf(
            page_text,
            sizeof(page_text),
            "%u / %u",
            page_number + 1u,
            page_count
        );
        TitanFont_DrawTextAlpha(
            746.0f + offset_x,
            43.0f,
            0.48f,
            page_text,
            224, 229, 239,
            TitanShellScene_Alpha((float)alpha * 0.76f)
        );
    }

    for (visible_index = 0u;
         visible_index < TITAN_THEME_GRID_PAGE_SIZE &&
         page_start + visible_index < count;
         visible_index++)
    {
        unsigned int theme_index = page_start + visible_index;
        unsigned int column = visible_index % TITAN_THEME_GRID_COLUMNS;
        unsigned int row = visible_index / TITAN_THEME_GRID_COLUMNS;
        float x = TITAN_THEME_GRID_ORIGIN_X + offset_x +
            (float)column *
                (TITAN_THEME_GRID_CARD_WIDTH + TITAN_THEME_GRID_GAP_X);
        float y = TITAN_THEME_GRID_ORIGIN_Y +
            (float)row *
                (TITAN_THEME_GRID_CARD_HEIGHT + TITAN_THEME_GRID_GAP_Y);

        TitanShellScene_DrawThemeCard(
            TitanTheme_Get(theme_index),
            x,
            y,
            alpha,
            theme_index == selected,
            theme_index == active
        );
    }

    /* Equivalent to the Store tile in the OrbisOS reference. Titan uses this
     * space to explain where installable .tthm themes enter the system. */
    TitanVideo_DrawRectAlpha(
        TITAN_THEME_GRID_ORIGIN_X + offset_x,
        319.0f,
        TITAN_THEME_GRID_CARD_WIDTH,
        70.0f,
        selected == count ? 28 : 10,
        selected == count ? 31 : 13,
        selected == count ? 46 : 23,
        TitanShellScene_Alpha((float)alpha * (selected == count ? 0.72f : 0.38f))
    );
    TitanShellScene_DrawOutline(
        TITAN_THEME_GRID_ORIGIN_X + offset_x,
        319.0f,
        TITAN_THEME_GRID_CARD_WIDTH,
        70.0f,
        selected == count ? 2.0f : 1.0f,
        225, 230, 240,
        TitanShellScene_Alpha((float)alpha * (selected == count ? 0.88f : 0.34f))
    );
    TitanIcons_Draw(
        TITAN_ICON_SYSTEM,
        TITAN_THEME_GRID_ORIGIN_X + offset_x + 25.0f,
        354.0f,
        0.72f,
        236, 240, 248,
        TitanShellScene_Alpha((float)alpha * 0.86f)
    );
    TitanFont_DrawTextAlpha(
        TITAN_THEME_GRID_ORIGIN_X + offset_x + 54.0f,
        335.0f,
        0.56f,
        "Add theme",
        244, 247, 252,
        TitanShellScene_Alpha((float)alpha * 0.92f)
    );
    TitanFont_DrawTextAlpha(
        TITAN_THEME_GRID_ORIGIN_X + offset_x + 54.0f,
        358.0f,
        0.42f,
        ".tthm pelo Package Installer",
        201, 208, 220,
        TitanShellScene_Alpha((float)alpha * 0.72f)
    );

    profile = TitanUserProfiles_GetActive();
    if (profile != 0)
    {
        TitanFont_DrawTextAlpha(
            690.0f + offset_x,
            402.0f,
            0.46f,
            profile->name,
            228, 233, 241,
            TitanShellScene_Alpha((float)alpha * 0.76f)
        );
    }

    TitanFont_DrawTextAlpha(
        47.0f + offset_x,
        405.0f,
        0.43f,
        selected == count
            ? "X Install theme    O Back    D-pad Select"
            : (TitanTheme_CanDelete(selected)
                ? "X Aplicar    TRIANGLE Excluir    O Voltar    Direcional Selecionar"
                : "X Aplicar    O Voltar    Direcional Selecionar"),
        221, 227, 238,
        TitanShellScene_Alpha((float)alpha * 0.82f)
    );
}

static void TitanShellScene_FormatStorageBytes(
    uint64_t bytes,
    int valid,
    char *output,
    unsigned int capacity
)
{
    const char *unit = "B";
    unsigned long long whole = (unsigned long long)bytes;
    unsigned long long decimal = 0ull;
    unsigned long long divisor = 1ull;

    if (output == 0 || capacity == 0u) return;
    if (!valid)
    {
        snprintf(output, capacity, "Calculando...");
        return;
    }
    if (bytes >= (1024ull * 1024ull * 1024ull))
    {
        unit = "GiB";
        divisor = 1024ull * 1024ull * 1024ull;
    }
    else if (bytes >= (1024ull * 1024ull))
    {
        unit = "MiB";
        divisor = 1024ull * 1024ull;
    }
    else if (bytes >= 1024ull)
    {
        unit = "KiB";
        divisor = 1024ull;
    }
    whole = (unsigned long long)(bytes / divisor);
    decimal = (unsigned long long)(((bytes % divisor) * 10ull) / divisor);
    if (divisor == 1ull)
        snprintf(output, capacity, "%llu %s", whole, unit);
    else
        snprintf(output, capacity, "%llu.%llu %s", whole, decimal, unit);
}

static const TitanInputDeviceDescriptor *TitanShellScene_GetWiredAccessory(
    unsigned int visible_index,
    unsigned int *source_index
)
{
    unsigned int i;
    unsigned int count = 0u;
    for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); i++)
    {
        const TitanInputDeviceDescriptor *device = TitanUnifiedInput_GetDevice(i);
        if (device == 0 || !device->connected ||
            device->transport == TITAN_INPUT_TRANSPORT_BLUETOOTH)
            continue;
        if (count++ == visible_index)
        {
            if (source_index != 0) *source_index = i;
            return device;
        }
    }
    return 0;
}

static const char *TitanShellScene_GetWirelessAccessoryName(
    const TitanPademuWirelessAccessory *accessory
)
{
    if (accessory == 0) return "Acessório Bluetooth";
    if (accessory->name[0] != '\0') return accessory->name;
    if (accessory->flags & TITAN_PADEMU_WIRELESS_CONTROLLER)
        return "Controle sem fio";
    return "Acessório Bluetooth";
}

static void TitanShellScene_DrawAccessoriesSettings(float offset_x, unsigned char alpha)
{
    int row_count = TitanShellScene_GetSettingsRowCount(TITAN_SETTINGS_ACCESSORIES);
    int selected = g_scene.settings_selected_row;
    int first_visible;
    int visible;
    int row;

    TitanShellScene_DrawPanelBase(
        "Acessórios",
        "Conexões, atribuição de jogadores e Multitap",
        offset_x,
        alpha
    );

    first_visible = selected - (TITAN_SETTINGS_VISIBLE_ROW_COUNT - 1);
    if (first_visible < 0) first_visible = 0;
    if (first_visible > row_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT)
        first_visible = row_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT;
    if (first_visible < 0) first_visible = 0;

    visible = 0;
    for (row = first_visible;
         row < row_count && visible < TITAN_SETTINGS_VISIBLE_ROW_COUNT;
         row++, visible++)
    {
        float y = TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP);
        float focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)row - g_scene.settings_row_position),
            0.0f, 1.0f);
        if (row == 0)
        {
            char count_text[48];
            unsigned int registered = TitanWirelessAccessories_GetRegisteredCount();
            snprintf(count_text, sizeof(count_text), "%u registrado%s",
                registered, registered == 1u ? "" : "s");
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_INPUT,
                "Acessórios sem fio",
                "Conecte e gerencie somente acessórios Bluetooth.",
                count_text,
                y, offset_x, alpha, focus
            );
        }
        else if (row == 1)
        {
            const TitanInputDeviceDescriptor *p1 = TitanUnifiedInput_GetPlayer(0u);
            char value[64];
            snprintf(value, sizeof(value), "%s",
                p1 != 0 ? TitanUnifiedInput_DeviceName(p1->type) : TitanLocalization_Compat("Automatic"));
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_INPUT,
                "Atribuição de controles",
                "Escolha quais dispositivos serão Jogador 1–4.",
                value,
                y, offset_x, alpha, focus
            );
        }
        else if (row == 2)
        {
            char value[64];
            snprintf(value, sizeof(value), TitanLocalization_Compat("%s / Port %u"),
                TitanLocalization_Compat(TitanControllerAssignment_GetMultitapEnabled() ? "Enabled" : "Disabled"),
                TitanControllerAssignment_GetMultitapPort() + 1u);
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_INPUT,
                "Multitap",
                "Configura o Multitap virtual usado pelo PADEMU no lançamento de jogos.",
                value,
                y, offset_x, alpha, focus
            );
        }
        else
        {
            const TitanInputDeviceDescriptor *device =
                TitanShellScene_GetWiredAccessory((unsigned int)(row - 3), 0);
            char status[80];
            char detail[112];
            if (device == 0) continue;
            snprintf(status, sizeof(status), "%s / %s",
                TitanUnifiedInput_TransportName(device->transport),
                TitanUnifiedInput_BackendName(device->backend));
            snprintf(detail, sizeof(detail), "%s",
                TitanLocalization_Compat(
                    device->player < TITAN_UNIFIED_INPUT_MAX_PLAYERS
                        ? "Connected / player assigned"
                        : "Connected"));
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_INPUT,
                TitanUnifiedInput_DeviceName(device->type),
                status,
                detail,
                y, offset_x, alpha, focus
            );
        }
    }
    TitanShellScene_DrawSettingsScrollbar(row_count, selected, offset_x, alpha);
}

static void TitanShellScene_DrawStorageSettings(float offset_x, unsigned char alpha)
{
    const TitanStorageInventorySnapshot *snapshot = TitanStorageInventory_GetSnapshot();
    const char *labels[4] = {"Espaço do Titan", "Aplicativos", "Jogos nativos Titan", "Temas"};
    const char *descriptions[4] = {
        "Uso total do volume onde o Project Titan está instalado.",
        "Aplicativos instalados pelo Titan e conteúdo gerenciável.",
        "Jogos nativos executados pelo Titan Runtime.",
        "Temas instalados; o tema ativo continua protegido pelo Theme Service."
    };
    TitanIconId icons[4] = {TITAN_ICON_STORAGE, TITAN_ICON_APPLICATIONS, TITAN_ICON_APPLICATIONS, TITAN_ICON_SYSTEM};
    char values[4][64];
    int row;

    TitanShellScene_FormatStorageBytes(snapshot->titan_bytes,
        snapshot->titan_bytes_valid && !snapshot->scanning, values[0], sizeof(values[0]));
    snprintf(values[1], sizeof(values[1]),
        TitanLocalization_Compat(snapshot->application_count == 1u ? "%u installed" : "%u items installed"),
        snapshot->application_count);
    snprintf(values[2], sizeof(values[2]),
        TitanLocalization_Compat(snapshot->native_game_count == 1u ? "%u installed" : "%u items installed"),
        snapshot->native_game_count);
    snprintf(values[3], sizeof(values[3]),
        TitanLocalization_Compat(snapshot->theme_count == 1u ? "%u installed" : "%u items installed"),
        snapshot->theme_count);

    TitanShellScene_DrawPanelBase(
        "Armazenamento",
        snapshot->scanning ? "Atualizando inventário em background..." : "Conteúdo instalado e uso do armazenamento Titan",
        offset_x,
        alpha
    );

    for (row = 0; row < 4; row++)
    {
        TitanShellScene_DrawSettingsListRow(
            icons[row], labels[row], descriptions[row], values[row],
            TITAN_SETTINGS_ROW_TOP + ((float)row * TITAN_SETTINGS_ROW_STEP),
            offset_x, alpha,
            TitanShellScene_Clamp(
                1.0f - TitanAnimation_Absolute((float)row - g_scene.settings_row_position),
                0.0f, 1.0f)
        );
    }
}

static TitanStorageInventoryKind TitanShellScene_GetStorageLeafKind(void)
{
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_STORAGE_GAMES)
        return TITAN_STORAGE_INVENTORY_NATIVE_GAME;
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_STORAGE_THEMES)
        return TITAN_STORAGE_INVENTORY_THEME;
    return TITAN_STORAGE_INVENTORY_APPLICATION;
}

static const char *TitanShellScene_GetStorageLeafTitle(void)
{
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_STORAGE_GAMES)
        return "Jogos nativos Titan";
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_STORAGE_THEMES)
        return "Temas";
    return "Aplicativos";
}

static void TitanShellScene_DrawStorageLeaf(float offset_x, unsigned char alpha)
{
    TitanStorageInventoryKind kind = TitanShellScene_GetStorageLeafKind();
    unsigned int count = TitanStorageInventory_GetKindCount(kind);
    int selected = g_scene.settings_leaf_selected_row;
    int first_visible;
    int visible;
    int row;

    TitanShellScene_DrawPanelBase(
        TitanShellScene_GetStorageLeafTitle(),
        count == 0u ? "Nenhum conteúdo instalado nesta categoria." : "X Opções   O Voltar",
        offset_x, alpha
    );

    if (count == 0u)
    {
        TitanShellScene_DrawSettingsListRow(
            kind == TITAN_STORAGE_INVENTORY_THEME ? TITAN_ICON_SYSTEM : TITAN_ICON_APPLICATIONS,
            "Nenhum conteúdo instalado",
            "O inventário do Titan não encontrou itens gerenciáveis nesta categoria.",
            "",
            TITAN_SETTINGS_ROW_TOP, offset_x, alpha, 0.0f
        );
        return;
    }

    if (selected < 0) selected = 0;
    if ((unsigned int)selected >= count) selected = (int)count - 1;
    first_visible = selected - (TITAN_SETTINGS_VISIBLE_ROW_COUNT - 1);
    if (first_visible < 0) first_visible = 0;
    if (first_visible > (int)count - TITAN_SETTINGS_VISIBLE_ROW_COUNT)
        first_visible = (int)count - TITAN_SETTINGS_VISIBLE_ROW_COUNT;
    if (first_visible < 0) first_visible = 0;

    visible = 0;
    for (row = first_visible;
         (unsigned int)row < count && visible < TITAN_SETTINGS_VISIBLE_ROW_COUNT;
         row++, visible++)
    {
        const TitanStorageInventoryEntry *entry =
            TitanStorageInventory_GetKindEntry(kind, (unsigned int)row);
        char size_text[48];
        float focus;
        if (entry == 0) continue;
        TitanShellScene_FormatStorageBytes(entry->bytes, entry->bytes_valid,
            size_text, sizeof(size_text));
        focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)row - g_scene.settings_leaf_row_position),
            0.0f, 1.0f);
        TitanShellScene_DrawSettingsListRow(
            kind == TITAN_STORAGE_INVENTORY_THEME ? TITAN_ICON_SYSTEM : TITAN_ICON_APPLICATIONS,
            entry->name,
            entry->path,
            size_text,
            TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP),
            offset_x, alpha, focus
        );
    }
    TitanShellScene_DrawSettingsScrollbar((int)count, selected, offset_x, alpha);
}

static void TitanShellScene_DrawWirelessMenu(float offset_x, unsigned char alpha)
{
    unsigned int registered = TitanWirelessAccessories_GetRegisteredCount();
    char registered_text[48];
    snprintf(registered_text, sizeof(registered_text),
        TitanLocalization_Compat(registered == 1u ? "%u accessory" : "%u accessories"),
        registered);

    TitanShellScene_DrawPanelBase(
        "Acessórios sem fio",
        "Somente acessórios Bluetooth são exibidos nesta área.",
        offset_x, alpha
    );
    TitanShellScene_DrawSettingsListRow(
        TITAN_ICON_INPUT,
        "Conectar acessório",
        "Ao entrar, o Titan inicia a busca Bluetooth automaticamente.",
        "Buscar e parear",
        TITAN_SETTINGS_ROW_TOP, offset_x, alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(g_scene.settings_leaf_row_position), 0.0f, 1.0f)
    );
    TitanShellScene_DrawSettingsListRow(
        TITAN_ICON_INPUT,
        "Acessórios conectados",
        "Veja acessórios registrados, estado da conexão e opções do dispositivo.",
        registered_text,
        TITAN_SETTINGS_ROW_TOP + TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(1.0f - g_scene.settings_leaf_row_position), 0.0f, 1.0f)
    );
}

static void TitanShellScene_DrawWirelessDetail(float offset_x, unsigned char alpha)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    unsigned int count;
    int selected = g_scene.wireless_detail_selected_row;
    int first_visible;
    int visible;
    int row;

    if (g_scene.wireless_detail_mode == TITAN_WIRELESS_DETAIL_SCAN)
    {
        count = TitanWirelessAccessories_GetSeenCount();
        {
            int transport_ready = state != 0 && (state->flags & TITAN_PADEMU_WIRELESS_TRANSPORT_READY);
            int scan_error = state != 0 && (state->flags & TITAN_PADEMU_WIRELESS_HAS_ERROR) && state->last_error != 0;
            const char *panel_help = !transport_ready
                ? "Inicializando Bluetooth... a busca começa automaticamente quando o HCI estiver pronto."
                : (scan_error
                    ? "A busca Bluetooth falhou. O código abaixo também será registrado nos logs do Titan."
                    : ((state->flags & TITAN_PADEMU_WIRELESS_SCANNING)
                        ? "Procurando acessórios...  DualSense: segure Create + PS."
                        : "Busca Bluetooth. Selecione um acessório encontrado para conectar."));
            TitanShellScene_DrawPanelBase("Conectar acessório", panel_help, offset_x, alpha);
            if (count == 0u)
            {
                char status_text[48];
                const char *label;
                const char *detail;
                status_text[0] = 0;
                if (!transport_ready) { label = "Inicializando Bluetooth..."; detail = "Preparando o dongle para iniciar a busca."; }
                else if (scan_error) { label = "Falha ao iniciar busca"; detail = "Consulte o log para o estágio HCI exato."; snprintf(status_text,sizeof(status_text),TitanLocalization_Compat("Error %d"),state->last_error); }
                else if (state->flags & TITAN_PADEMU_WIRELESS_SCANNING) { label = "Procurando acessórios..."; detail = "Coloque o acessório em modo de pareamento."; }
                else { label = "Nenhum acessório encontrado"; detail = "A busca terminou sem encontrar dispositivos."; }
                TitanShellScene_DrawSettingsListRow(
                    TITAN_ICON_INPUT,label,detail,status_text,
                    TITAN_SETTINGS_ROW_TOP, offset_x, alpha, 0.0f
                );
                return;
            }
        }
    }
    else
    {
        count = TitanWirelessAccessories_GetRegisteredCount();
        TitanShellScene_DrawPanelBase(
            "Acessórios conectados",
            "Acessórios Bluetooth registrados no Titan.",
            offset_x, alpha
        );
        if (count == 0u)
        {
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_INPUT,
                "Nenhum acessório registrado",
                "Use Conectar acessório para iniciar o primeiro pareamento.",
                "",
                TITAN_SETTINGS_ROW_TOP, offset_x, alpha, 0.0f
            );
            return;
        }
    }

    if (selected < 0) selected = 0;
    if ((unsigned int)selected >= count) selected = (int)count - 1;
    first_visible = selected - (TITAN_SETTINGS_VISIBLE_ROW_COUNT - 1);
    if (first_visible < 0) first_visible = 0;
    if (first_visible > (int)count - TITAN_SETTINGS_VISIBLE_ROW_COUNT)
        first_visible = (int)count - TITAN_SETTINGS_VISIBLE_ROW_COUNT;
    if (first_visible < 0) first_visible = 0;

    visible = 0;
    for (row = first_visible;
         (unsigned int)row < count && visible < TITAN_SETTINGS_VISIBLE_ROW_COUNT;
         row++, visible++)
    {
        const TitanPademuWirelessAccessory *accessory;
        char state_text[64];
        char addr[32];
        float focus;
        if (g_scene.wireless_detail_mode == TITAN_WIRELESS_DETAIL_SCAN)
            accessory = TitanWirelessAccessories_GetSeen((unsigned int)row, 0);
        else
            accessory = TitanWirelessAccessories_GetRegistered((unsigned int)row, 0);
        if (accessory == 0) continue;
        snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            accessory->address[5], accessory->address[4], accessory->address[3],
            accessory->address[2], accessory->address[1], accessory->address[0]);
        if (accessory->flags & TITAN_PADEMU_WIRELESS_CONNECTED)
            snprintf(state_text, sizeof(state_text), "Conectado");
        else if (accessory->flags & TITAN_PADEMU_WIRELESS_CONNECTING)
            snprintf(state_text, sizeof(state_text), "Conectando...");
        else if (accessory->flags & TITAN_PADEMU_WIRELESS_PAIRED)
            snprintf(state_text, sizeof(state_text), "Registrado / desconectado");
        else
            snprintf(state_text, sizeof(state_text), "Encontrado");
        focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)row - g_scene.wireless_detail_row_position),
            0.0f, 1.0f);
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_INPUT,
            TitanShellScene_GetWirelessAccessoryName(accessory),
            addr,
            state_text,
            TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP),
            offset_x, alpha, focus
        );
    }
    TitanShellScene_DrawSettingsScrollbar((int)count, selected, offset_x, alpha);
}


static void TitanShellScene_DrawControllerAssignmentLeaf(float offset_x, unsigned char alpha)
{
    int row;
    TitanShellScene_DrawPanelBase(
        "Atribuição de controles",
        "X Alternar dispositivo   △ Automático   O Voltar",
        offset_x, alpha);
    for (row = 0; row < (int)TITAN_UNIFIED_INPUT_MAX_PLAYERS; row++) {
        TitanControllerAssignmentBinding binding;
        const TitanInputDeviceDescriptor *connected =
            TitanControllerAssignment_GetPreferredConnected((unsigned int)row);
        char title[32];
        char value[96];
        const char *detail;
        float focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)row - g_scene.settings_leaf_row_position),
            0.0f, 1.0f);
        snprintf(title, sizeof(title), TitanLocalization_Compat("Player %d"), row + 1);
        (void)TitanControllerAssignment_GetBinding((unsigned int)row, &binding);
        if (!binding.valid) {
            snprintf(value, sizeof(value), "Automático");
            detail = "O Titan escolhe um controle disponível.";
        } else if (connected != 0) {
            snprintf(value, sizeof(value), "%s / %s",
                TitanUnifiedInput_DeviceName(connected->type),
                TitanUnifiedInput_TransportName(connected->transport));
            detail = "Preferência persistente / conectado";
        } else {
            snprintf(value, sizeof(value), TitanLocalization_Compat("%s / disconnected"),
                TitanUnifiedInput_DeviceName(binding.type));
            detail = "Preferência persistente; fallback temporário permitido";
        }
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_INPUT, title, detail, value,
            TITAN_SETTINGS_ROW_TOP + ((float)row * TITAN_SETTINGS_ROW_STEP),
            offset_x, alpha, focus);
    }
}

static void TitanShellScene_DrawMultitapLeaf(float offset_x, unsigned char alpha)
{
    char port[32];
    snprintf(port, sizeof(port), TitanLocalization_Compat("Port %u"), TitanControllerAssignment_GetMultitapPort() + 1u);
    TitanShellScene_DrawPanelBase(
        "Multitap",
        "Configuração global aplicada ao handoff PADEMU dos jogos",
        offset_x, alpha);
    TitanShellScene_DrawSettingsListRow(
        TITAN_ICON_INPUT, "Multitap",
        "Habilita quatro slots lógicos no backend PADEMU.",
        TitanLocalization_Compat(TitanControllerAssignment_GetMultitapEnabled() ? "Enabled" : "Disabled"),
        TITAN_SETTINGS_ROW_TOP, offset_x, alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(g_scene.settings_leaf_row_position), 0.0f, 1.0f));
    TitanShellScene_DrawSettingsListRow(
        TITAN_ICON_INPUT, "Porta do Multitap",
        "Escolha a porta PS2 virtual na qual o Multitap será apresentado.",
        port,
        TITAN_SETTINGS_ROW_TOP + TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(1.0f - g_scene.settings_leaf_row_position), 0.0f, 1.0f));
}

static void TitanShellScene_DrawSettingsLeaf(float offset_x, unsigned char alpha)
{
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_WIRELESS)
    {
        float detail = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(g_scene.wireless_detail_transition, 0.0f, 1.0f));
        unsigned char menu_alpha = TitanShellScene_Alpha((float)alpha * (1.0f - detail));
        unsigned char detail_alpha = TitanShellScene_Alpha((float)alpha * detail);
        float menu_offset = offset_x - (detail * 96.0f);
        float detail_offset = offset_x + ((1.0f - detail) * 96.0f);
        if (menu_alpha > 4) TitanShellScene_DrawWirelessMenu(menu_offset, menu_alpha);
        if (detail_alpha > 4) TitanShellScene_DrawWirelessDetail(detail_offset, detail_alpha);
        return;
    }
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_CONTROLLER_ASSIGNMENT) {
        TitanShellScene_DrawControllerAssignmentLeaf(offset_x, alpha);
        return;
    }
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_MULTITAP) {
        TitanShellScene_DrawMultitapLeaf(offset_x, alpha);
        return;
    }
    TitanShellScene_DrawStorageLeaf(offset_x, alpha);
}

static void TitanShellScene_OpenSettingsAction(
    TitanSettingsActionKind kind,
    unsigned int source_index
)
{
    g_scene.settings_action_kind = kind;
    g_scene.settings_action_source_index = source_index;
    g_scene.settings_action_selected = 0;
    g_scene.settings_action_row_position = 0.0f;
    g_scene.settings_action_open = 1;
}

static void TitanShellScene_ShowWiredAccessoryInformation(unsigned int source_index)
{
    const TitanInputDeviceDescriptor *device = TitanUnifiedInput_GetDevice(source_index);
    char connection[96], identity[96], battery[96], backend[96], player[96];
    if (device == 0 || !device->connected) return;
    snprintf(connection, sizeof(connection), TitanLocalization_Compat("Connection: %s"), TitanUnifiedInput_TransportName(device->transport));
    snprintf(identity, sizeof(identity), "USB ID: %04X:%04X", (unsigned)device->vendor_id, (unsigned)device->product_id);
    if (device->battery_valid)
        snprintf(battery, sizeof(battery), TitanLocalization_Compat("Battery: %u%%"), (unsigned)device->battery_percent);
    else
        snprintf(battery, sizeof(battery), "%s", TitanLocalization_Compat("Battery: Not available"));
    snprintf(backend, sizeof(backend), "Backend: %s", TitanUnifiedInput_BackendName(device->backend));
    if (device->player < TITAN_UNIFIED_INPUT_MAX_PLAYERS)
        snprintf(player, sizeof(player), TitanLocalization_Compat("Player: %u"), (unsigned)device->player + 1u);
    else
        snprintf(player, sizeof(player), "%s", TitanLocalization_Compat("Player: Not assigned"));
    (void)TitanNotifications_OpenInformation(
        TitanUnifiedInput_DeviceName(device->type), connection, identity, battery, backend, player);
}

static void TitanShellScene_ShowWirelessAccessoryInformation(unsigned int source_index)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    const TitanPademuWirelessAccessory *accessory;
    char connection[64], status[80], firmware[80], battery[80], backend[80];
    if (state == 0 || source_index >= state->count) return;
    accessory = &state->accessories[source_index];
    snprintf(connection, sizeof(connection), "%s", TitanLocalization_Compat("Connection: Bluetooth"));
    snprintf(status, sizeof(status), TitanLocalization_Compat("State: %s"),
        (accessory->flags & TITAN_PADEMU_WIRELESS_CONNECTED) ? TitanLocalization_Compat("Connected") : TitanLocalization_Compat("Disconnected"));
    snprintf(firmware, sizeof(firmware), "%s", TitanLocalization_Compat("Firmware: Not available"));
    if (accessory->battery_percent > 0u)
        snprintf(battery, sizeof(battery), TitanLocalization_Compat("Battery: %u%%"), (unsigned)accessory->battery_percent);
    else
        snprintf(battery, sizeof(battery), "%s", TitanLocalization_Compat("Battery: Not available"));
    snprintf(backend, sizeof(backend), "Backend: Titan PADEMU");
    (void)TitanNotifications_OpenInformation(
        TitanShellScene_GetWirelessAccessoryName(accessory),
        connection, status, firmware, battery, backend);
}

static void TitanShellScene_ShowStorageEntryInformation(
    TitanSettingsActionKind kind,
    unsigned int source_index
)
{
    char version[96], id_line[96], type_line[96], size_line[96], location[112];
    const TitanStorageInventoryEntry *entry = 0;
    TitanStorageInventoryKind inv_kind;
    unsigned int i;

    if (kind == TITAN_SETTINGS_ACTION_STORAGE_THEME)
        inv_kind = TITAN_STORAGE_INVENTORY_THEME;
    else if (kind == TITAN_SETTINGS_ACTION_STORAGE_GAME)
        inv_kind = TITAN_STORAGE_INVENTORY_NATIVE_GAME;
    else
        inv_kind = TITAN_STORAGE_INVENTORY_APPLICATION;

    for (i = 0u; i < TitanStorageInventory_GetKindCount(inv_kind); i++)
    {
        const TitanStorageInventoryEntry *candidate = TitanStorageInventory_GetKindEntry(inv_kind, i);
        if (candidate != 0 && candidate->source_index == source_index)
        {
            entry = candidate;
            break;
        }
    }
    if (entry == 0) return;
    TitanShellScene_FormatStorageBytes(entry->bytes, entry->bytes_valid, size_line, sizeof(size_line));
    {
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%s", size_line);
        snprintf(size_line, sizeof(size_line), TitanLocalization_Compat("Size: %s"), tmp);
    }
    snprintf(location, sizeof(location), TitanLocalization_Compat("Location: %s"), entry->path);

    if (kind == TITAN_SETTINGS_ACTION_STORAGE_THEME)
    {
        const TitanThemeInfo *theme = TitanTheme_Get(source_index);
        if (theme == 0) return;
        snprintf(version, sizeof(version), TitanLocalization_Compat("Version: %s"), theme->version[0] ? theme->version : TitanLocalization_Compat("Not available"));
        snprintf(id_line, sizeof(id_line), "ID: %s", theme->id[0] ? theme->id : TitanLocalization_Compat("Not available"));
        snprintf(type_line, sizeof(type_line), "%s", TitanLocalization_Compat("Type: Titan theme"));
        (void)TitanNotifications_OpenInformation(theme->name, version, id_line, type_line, size_line, location);
    }
    else
    {
        const TitanApplication *app = TitanApplications_Get(source_index);
        if (app == 0) return;
        snprintf(version, sizeof(version), TitanLocalization_Compat("Version: %s"), app->version[0] ? app->version : TitanLocalization_Compat("Not available"));
        snprintf(id_line, sizeof(id_line), "Title ID: %s", app->title_id[0] ? app->title_id : TitanLocalization_Compat("Not available"));
        snprintf(type_line, sizeof(type_line), TitanLocalization_Compat("Type: %s"),
            kind == TITAN_SETTINGS_ACTION_STORAGE_GAME ? TitanLocalization_Compat("Titan native game") :
            TitanApplications_GetClassificationName(app->classification));
        (void)TitanNotifications_OpenInformation(app->title, version, id_line, type_line, size_line, location);
    }
}

static void TitanShellScene_ConfirmDeleteStoredTheme(int confirmed, void *user_data)
{
    unsigned int index = (unsigned int)(uintptr_t)user_data;
    char error[96];
    if (!confirmed) return;
    if (!TitanTheme_CanDelete(index))
    {
        TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, "Temas", "O tema ativo ou protegido não pode ser excluído");
        return;
    }
    if (!TitanTheme_Delete(index, error, sizeof(error)))
    {
        TitanNotifications_Push(TITAN_NOTIFICATION_ERROR, "Temas", error[0] ? error : "Não foi possível excluir o tema");
        return;
    }
    TitanShellBackground_RefreshThemeAssets();
    TitanAudio_ReloadThemeAssets();
    if (TitanAudio_IsReady()) TitanAudio_StartMusic();
    TitanStorageInventory_RequestRefresh();
    TitanNotifications_Push(TITAN_NOTIFICATION_SUCCESS, "Temas", "Tema excluído");
}

static void TitanShellScene_RequestStoredApplicationUninstall(unsigned int source_index)
{
    if (TitanApplications_GetUninstallTarget(
            source_index,
            g_scene.pending_uninstall_root, sizeof(g_scene.pending_uninstall_root),
            g_scene.pending_uninstall_title_id, sizeof(g_scene.pending_uninstall_title_id),
            g_scene.pending_uninstall_title, sizeof(g_scene.pending_uninstall_title)))
    {
        (void)TitanNotifications_OpenConfirmation(
            "Excluir conteúdo?",
            g_scene.pending_uninstall_title,
            "O aplicativo/jogo e seus arquivos instalados serão removidos. Saves não serão apagados.",
            "Cancel", "Excluir",
            TitanShellScene_ConfirmUninstallApplication, 0);
    }
    else
    {
        TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, "Armazenamento", "Este item não pode ser removido pelo Package Service");
    }
}

static void TitanShellScene_DrawSettingsActionModal(unsigned char alpha)
{
    const char *title = "Opções";
    int row_count = 2;
    const char *rows[3] = {"Informações", "Excluir", ""};
    int i;
    float panel_x = 238.0f, panel_y = 122.0f, panel_w = 484.0f, panel_h;
    const TitanThemeShellStyle *style = TitanTheme_GetShellStyle();
    unsigned char panel_alpha = TitanShellScene_Alpha((float)alpha * 0.95f);

    if (!g_scene.settings_action_open) return;
    if (g_scene.settings_action_kind == TITAN_SETTINGS_ACTION_WIRELESS_DEVICE)
    {
        title = "Acessório Bluetooth";
        rows[1] = "Desconectar";
        rows[2] = "Esquecer acessório";
        row_count = 3;
    }
    panel_h = 86.0f + (float)row_count * 54.0f;
    TitanVideo_DrawRectAlpha(0.0f, 0.0f, (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
        0, 0, 0, TitanShellScene_Alpha((float)alpha * 0.48f));
    TitanVideo_DrawRectAlpha(panel_x, panel_y, panel_w, panel_h, 20, 22, 29, panel_alpha);
    TitanVideo_DrawRectAlpha(panel_x, panel_y, 4.0f, panel_h,
        style->accent.red, style->accent.green, style->accent.blue,
        TitanShellScene_Alpha((float)alpha * 0.88f));
    TitanFont_DrawTextShadow(panel_x + 28.0f, panel_y + 22.0f, 0.82f, title,
        248, 250, 255, alpha);
    for (i = 0; i < row_count; i++)
    {
        float focus = TitanShellScene_Clamp(
            1.0f - TitanAnimation_Absolute((float)i - g_scene.settings_action_row_position),
            0.0f, 1.0f);
        if (focus > 0.01f)
            TitanVideo_DrawRectAlpha(panel_x + 22.0f, panel_y + 62.0f + i * 54.0f,
                panel_w - 44.0f, 44.0f,
                style->accent.red, style->accent.green, style->accent.blue,
                TitanShellScene_Alpha((float)alpha * 0.18f * focus));
        TitanFont_DrawTextAlpha(panel_x + 38.0f, panel_y + 74.0f + i * 54.0f,
            0.64f, rows[i], 240, 244, 250,
            TitanShellScene_Alpha((float)alpha * (0.78f + 0.20f * focus)));
    }
}

static void TitanShellScene_DrawSettingsSubpage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanUserProfile *profile;
    const TitanSystemInfo *system_info;
    TitanIconId page_icon;
    TitanIconId row_icons[TITAN_SETTINGS_MAX_ROW_COUNT];
    const char *labels[TITAN_SETTINGS_MAX_ROW_COUNT];
    const char *values[TITAN_SETTINGS_MAX_ROW_COUNT];
    const char *descriptions[TITAN_SETTINGS_MAX_ROW_COUNT];
    char profile_count[16];
    char package_count[16];
    char theme_values[TITAN_SETTINGS_MAX_ROW_COUNT][32];
    char theme_descriptions[TITAN_SETTINGS_MAX_ROW_COUNT][112];
    char technology_values[2][64];
    char technology_descriptions[2][224];
    TitanPikaVrTelemetry pikavr_telemetry;
    TitanVlombisTelemetry vlombis_telemetry;
    int row_count;
    int index;
    int first_visible;
    int visible;
    float row_y;

    if (g_scene.settings_selected_category == TITAN_SETTINGS_ACCESSORIES)
    {
        TitanShellScene_DrawAccessoriesSettings(offset_x, alpha);
        return;
    }
    if (g_scene.settings_selected_category == TITAN_SETTINGS_STORAGE)
    {
        TitanShellScene_DrawStorageSettings(offset_x, alpha);
        return;
    }
    if (g_scene.settings_selected_category == TITAN_SETTINGS_THEMES)
    {
        TitanShellScene_DrawThemeSelectionPage(offset_x, alpha);
        return;
    }

    profile = TitanUserProfiles_GetActive();
    system_info = TitanSystemInfo_Get();
    memset(&pikavr_telemetry, 0, sizeof(pikavr_telemetry));
    memset(&vlombis_telemetry, 0, sizeof(vlombis_telemetry));
    technology_values[0][0] = '\0';
    technology_values[1][0] = '\0';
    technology_descriptions[0][0] = '\0';
    technology_descriptions[1][0] = '\0';
    page_icon = TitanShellScene_GetSettingsCategoryIcon(
        g_scene.settings_selected_category
    );

    for (index = 0; index < TITAN_SETTINGS_MAX_ROW_COUNT; index++)
    {
        labels[index] = "";
        values[index] = "";
        descriptions[index] = "";
        row_icons[index] = page_icon;
        theme_values[index][0] = '\0';
        theme_descriptions[index][0] = '\0';
    }

    row_count = TitanShellScene_GetSettingsRowCount(
        g_scene.settings_selected_category
    );

    switch ((TitanSettingsCategory)g_scene.settings_selected_category)
    {
        case TITAN_SETTINGS_USERS:
            TitanShellScene_FormatUnsigned(
                TitanUserProfiles_GetCount(),
                profile_count,
                sizeof(profile_count)
            );
            labels[0] = "Active User";
            descriptions[0] = "Switch the current user profile assigned to Titan.";
            values[0] = profile != 0 ? profile->name : "Unavailable";
            labels[1] = "Profiles";
            descriptions[1] = "Total number of stored profiles available locally.";
            values[1] = profile_count;
            labels[2] = "Add User";
            descriptions[2] = "Create a profile in TITAN.DAT with its own exclusive VMC.";
            values[2] = "Press SQUARE";
            labels[3] = "Remove User";
            descriptions[3] = "Delete the current profile after confirmation in a future build.";
            values[3] = "Press TRIANGLE";
            labels[4] = "Exclusive VMC";
            descriptions[4] = profile != 0 ? profile->vmc_path : "No active profile.";
            values[4] = profile != 0
                ? TitanVmc_GetStatusName(TitanVmc_GetCachedStatus(profile))
                : "Unavailable";
            break;

        case TITAN_SETTINGS_SAVED_DATA:
            labels[0] = "VMC Status";
            descriptions[0] = profile != 0 ? profile->vmc_path : "No active profile.";
            values[0] = profile != 0
                ? TitanVmc_GetStatusName(TitanVmc_GetCachedStatus(profile))
                : "Unavailable";
            labels[1] = "Create / Verify VMC";
            descriptions[1] = "Create the 8 MiB file or verify the current PS2 filesystem.";
            values[1] = "Press X";
            labels[2] = "Format Active VMC";
            descriptions[2] = "Create a valid PS2 superblock, FAT, IFC and root directory.";
            values[2] = profile != 0 ? profile->name : "Unavailable";
            labels[3] = "Rebuild Active VMC";
            descriptions[3] = "Replace a damaged VMC with a newly formatted 8 MiB image.";
            values[3] = profile != 0 ? "Keeps .bak" : "Unavailable";
            labels[4] = "Format Physical Memory Card";
            descriptions[4] = "Erase every save and file on the selected physical Memory Card.";
            values[4] = TitanShellScene_GetMemoryCardName(
                g_scene.memory_card_selected_port
            );
            break;

        case TITAN_SETTINGS_CONTENT:
        {
            const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
            labels[0] = "Asset Wizard";
            descriptions[0] = "Convert local PNG/JPEG artwork for the selected game or the entire library.";
            values[0] = assets != 0 && assets->artwork_wizard_active
                ? "Conversion in progress"
                : "Offline conversion";
            break;
        }

        case TITAN_SETTINGS_NETWORK:
        {
            const TitanNetworkStatus *network = TitanNetwork_GetStatus();
            labels[0] = "Connect to the Internet";
            descriptions[0] = "Enable or disable Project Titan network services.";
            values[0] = TitanNetwork_IsEnabled() ? "Enabled" : "Disabled";
            labels[1] = "Set Up Internet Connection";
            descriptions[1] = "Configure Wi-Fi with automatic scan or an RJ45 connection.";
            values[1] = network->preference == TITAN_NETWORK_PREFER_ETHERNET
                ? "Use LAN Cable" : "Use Wi-Fi";
            labels[2] = "Test Internet Connection";
            descriptions[2] = "Checks the interface, IP acquisition and available connectivity.";
            values[2] = TitanNetwork_GetStateName();
            labels[3] = "View Connection Status";
            descriptions[3] = "Shows live state from the currently selected interface.";
            values[3] = network->ip[0] ? network->ip : "Offline";
            labels[4] = "View Network Services Status";
            descriptions[4] = "Shows live backend readiness without reusing an old connection test.";
            if (network->preference == TITAN_NETWORK_PREFER_ETHERNET)
            {
                if (!network->main_interface_ready)
                    values[4] = network->ethernet_stack_loaded
                        ? "SMAP driver loaded"
                        : "Ethernet backend not ready";
                else if (!network->ethernet_link_known)
                    values[4] = "RJ45 / SMAP";
                else
                    values[4] = network->ethernet_link_up
                        ? "Cable connected"
                        : "Cable disconnected";
            }
            else
            {
                values[4] = network->main_interface_ready && network->wifi_adapter_present
                    ? "AR9271 / WFI"
                    : (network->wifi_adapter_present
                        ? "AR9271 detected"
                        : "Waiting for AR9271");
            }
            labels[5] = "Game data";
            descriptions[5] = "Download, update or clear icons and backgrounds independently from connectivity.";
            {
                const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
                values[5] = assets != 0 && assets->manual_session_active
                    ? "Operation in progress"
                    : "Manual download";
            }
            break;
        }

        case TITAN_SETTINGS_VIDEO:
            labels[0] = "Configured Output";
            descriptions[0] = "Output preference read from the PlayStation 2 OSD configuration.";
            values[0] = TitanVideoOutput_GetConfiguredOutputName();
            labels[1] = "Aspect Ratio";
            descriptions[1] = "Logical shell layout; its visual scale remains stable between modes.";
#ifdef PROJECT_TITAN_WIDESCREEN
            values[1] = "16:9 Anamorphic";
#else
            values[1] = "4:3 Standard";
#endif
            labels[2] = "Native 720p Output Test";
            descriptions[2] = TitanVideoOutput_CanTestProgressive()
                ? "Experimental test. X confirms; O or timeout restores the previous mode."
                : "Requires Component output selected in the PS2 OSD.";
            if (g_scene.video_test_active)
            {
                values[2] = "TEST MODE - X keep / O restore";
            }
            else if (TitanVideoOutput_IsShellProgressive())
            {
                values[2] = strcmp(TitanVideoOutput_GetRegionName(), "PAL") == 0
                    ? "720p Progressive"
                    : "720p Progressive";
            }
            else
            {
                values[2] = strcmp(TitanVideoOutput_GetRegionName(), "PAL") == 0
                    ? "576i Interlaced"
                    : "480i Interlaced";
            }
            labels[3] = "Resolution options";
            descriptions[3] = "Configure each title from its SELECT menu.";
            values[3] = "SELECT on a game";
            labels[4] = "Framebuffer";
            descriptions[4] = "Actual GS render target allocated for the selected output mode.";
            values[4] = TitanVideoOutput_IsShellProgressive()
                ? "1280 x 720 PSM16 / double"
                : "640 x 448 PSM32 / double";
            break;

        case TITAN_SETTINGS_THEMES:
        {
            unsigned int theme_count = TitanTheme_GetCount();
            unsigned int active_theme = TitanTheme_GetActiveIndex();
            unsigned int theme_index;

            if (theme_count > TITAN_SETTINGS_MAX_ROW_COUNT)
                theme_count = TITAN_SETTINGS_MAX_ROW_COUNT;
            for (theme_index = 0u; theme_index < theme_count; theme_index++)
            {
                const TitanThemeInfo *theme = TitanTheme_Get(theme_index);
                if (theme == 0)
                    continue;
                labels[theme_index] = theme->name;
                snprintf(
                    theme_values[theme_index],
                    sizeof(theme_values[theme_index]),
                    "%s",
                    theme_index == active_theme ? TitanLocalization_Get(TITAN_STR_APPLIED) : TitanLocalization_Get(TITAN_STR_APPLY)
                );
                snprintf(
                    theme_descriptions[theme_index],
                    sizeof(theme_descriptions[theme_index]),
                    "%s v%s%s",
                    theme->author,
                    theme->version,
                    theme->built_in ? " / Built-in fallback" : " / Installed theme"
                );
                values[theme_index] = theme_values[theme_index];
                descriptions[theme_index] = theme_descriptions[theme_index];
            }
            break;
        }

        case TITAN_SETTINGS_SYSTEM:
            /* These counters are only rendered on System. Do not collect
             * them on every Settings page/frame. */
            TitanVideo_GetPikaVrTelemetry(&pikavr_telemetry);
            TitanVlombisCore_GetTelemetry(&vlombis_telemetry);
            labels[0] = "System Software Update";
            descriptions[0] = "Install official Project Titan updates from a .pup package on USB.";
            values[0] = "Press X";
            row_icons[0] = TITAN_ICON_DEBUG_SETTINGS;
            labels[1] = "DEX Mode";
            descriptions[1] = "Enable developer overlays, APP_HOME and FTP_HOME.";
            values[1] = TitanDex_GetStateName();
            labels[2] = "Renderer";
            descriptions[2] = "Packet-based GS renderer with cached texture atlases.";
            values[2] = "GS dynamic PSM32/PSM16 + VU1";
            labels[3] = "Controller Service";
            descriptions[3] = "Input driver stack used for DualShock controllers.";
            values[3] = "PADMAN";
            labels[4] = "Theme Engine";
            descriptions[4] = "Active declarative background and shell style. Manage it in Settings > Themes.";
            values[4] = TitanTheme_GetActive()->name;
            labels[5] = "Boot Path";
            descriptions[5] = "Executable path used to start the shell on this session.";
            values[5] = system_info->boot_path[0] != '\0'
                ? system_info->boot_path
                : "Not reported";
            {
                const TitanDateTimeSnapshot *dt = TitanDateTime_GetSnapshot();
                labels[6] = "Date";
                descriptions[6] = dt->automatic ? "Current local date. Disable Automatic Time to adjust by day." : "Manual date; LEFT/RIGHT adjusts one day.";
                values[6] = dt->date_text;
                labels[7] = "Time";
                descriptions[7] = dt->automatic ? "Current local time from RTC or validated Internet Time." : "Manual time; LEFT/RIGHT adjusts one minute.";
                values[7] = dt->time_text;
                labels[8] = "Automatic Time";
                descriptions[8] = "Synchronize with SNTP when the network is online; boot never waits for Internet.";
                values[8] = dt->automatic ? "Enabled" : "Disabled";
                labels[9] = "Time Zone";
                descriptions[9] = "Local offset is applied separately from the raw RTC/UTC clock.";
                values[9] = TitanDateTime_GetTimezoneText();
                labels[10] = "Clock Format";
                descriptions[10] = "Choose 12-hour or 24-hour display without changing the stored clock.";
                values[10] = TitanDateTime_GetClockFormatName();
                labels[11] = "Date Format";
                descriptions[11] = "Choose the local date presentation format.";
                values[11] = TitanDateTime_GetDateFormatName();
                labels[12] = "Internet Time";
                descriptions[12] = "Request a bounded SNTP synchronization. DNS/network failure falls back to RTC.";
                values[12] = TitanDateTime_GetSyncStateName();
                labels[13] = TitanLocalization_Get(TITAN_STR_LANGUAGE);
                descriptions[13] = TitanLocalization_Get(TITAN_STR_LANGUAGE_DESCRIPTION);
                values[13] = TitanLocalization_GetLanguageName();
            }
            labels[14] = "pikaVR";
            snprintf(
                technology_values[0], sizeof(technology_values[0]),
                "%s - %u KiB free",
                pikavr_telemetry.vram_total != 0u ? "ACTIVE" : "OFFLINE",
                pikavr_telemetry.vram_free / 1024u
            );
            snprintf(
                technology_descriptions[0], sizeof(technology_descriptions[0]),
                "VRAM optimization: %s, %ux%u internal; saved=%u KiB, resident=%u, cache=%u/%u.",
                TitanPikaVr_IsOptimizedPathActive() ? "enabled" : "bypassed",
                TitanVideo_GetInternalWidth(), TitanVideo_GetInternalHeight(),
                pikavr_telemetry.vram_saved / 1024u, pikavr_telemetry.textures_resident,
                pikavr_telemetry.cache_hits, pikavr_telemetry.cache_misses
            );
            values[14] = technology_values[0];
            descriptions[14] = technology_descriptions[0];
            labels[15] = "VlombisCore";
            snprintf(
                technology_values[1], sizeof(technology_values[1]),
                "%s - %u/10 domains",
                vlombis_telemetry.measured_events != 0u ? "ACTIVE" : "READY",
                TitanVlombisCore_GetActiveCapabilityCount()
            );
            snprintf(
                technology_descriptions[1], sizeof(technology_descriptions[1]),
                "caps=0x%03X pressure=%u; lib cache=%u/reuse=%u; render-cache=%u; pkg=%u KiB; audio=%u net=%u; avoided=%u KiB.",
                vlombis_telemetry.capabilities_active, vlombis_telemetry.pressure_level,
                vlombis_telemetry.library_cache_hits, vlombis_telemetry.library_metadata_reuses,
                vlombis_telemetry.render_state_cache_hits,
                vlombis_telemetry.async_package_bytes / 1024u,
                vlombis_telemetry.audio_buffer_refills, vlombis_telemetry.network_background_ticks,
                vlombis_telemetry.bytes_avoided / 1024u
            );
            values[15] = technology_values[1];
            descriptions[15] = technology_descriptions[1];
            break;

        case TITAN_SETTINGS_DEBUG:
            TitanShellScene_FormatUnsigned(
                TitanPackageInstaller_GetCount(),
                package_count,
                sizeof(package_count)
            );
            labels[0] = "Package Installer";
            descriptions[0] = "Install verified .pkg applications and .tthm themes from mass:/TITAN/packages.";
            values[0] = package_count;
            labels[1] = "DEX Mode";
            descriptions[1] = "Keep developer features and Debug Settings enabled.";
            values[1] = TitanDex_GetStateName();
            labels[2] = "Performance Overlay";
            descriptions[2] = "Show or hide EE, GS, RAM and IP diagnostics without disabling DEX Mode.";
            values[2] = TitanDex_GetOverlayStateName();
            labels[3] = "Reload Home Catalog";
            descriptions[3] = "Force-rescan after USB changes; normal boot reuses a valid saved catalog.";
            values[3] = TitanStorageScheduler_IsLibraryScanBusy() ? "Working" : "Press X";
            labels[4] = "Clear Library Cache";
            descriptions[4] = "Remove catalog cache, temporary file and backup before a clean scan.";
            values[4] = "Press X";
            labels[5] = "Export Debug Report";
            descriptions[5] = "Write runtime, renderer, catalog and package status to the USB.";
            values[5] = "mass:/TITAN/debug";
            labels[6] = "★ APP_HOME";
            descriptions[6] = "Open the complete ELF and Titan App catalog.";
            values[6] = "Open";
            labels[7] = "★ FTP_HOME";
            descriptions[7] = "Open transfer and background activity diagnostics.";
            values[7] = "Open";
            labels[8] = "Controller Diagnostics";
            descriptions[8] = "Open live PADMAN connection and input information.";
            values[8] = "Open";
            labels[9] = "Export Full Diagnostics";
            descriptions[9] = "Export flight recorder and performance profile for this boot session.";
            values[9] = "mass:/TITAN/logs/titan-last.txt";
            labels[10] = "Export Previous Boot";
            descriptions[10] = "Export the preserved recorder from the boot immediately before this one, including crashes and hard resets.";
            values[10] = TitanDiagnostics_HasPreviousSession()
                ? "mass:/TITAN/logs/titan-previous.txt"
                : "No previous session";
            labels[11] = "720p Composition";
            descriptions[11] = "DEX validation selector. Optimized 960x540 is the release/default 720p composition; Reference is retained only for A/B diagnostics.";
            values[11] = TitanVideo_Get720pPath() == TITAN_VIDEO_720P_PATH_OPTIMIZED
                ? "Optimized 960x540"
                : "Reference";
            break;

        case TITAN_SETTINGS_ABOUT:
        default:
            labels[0] = "Bootloader Version";
            descriptions[0] = "Installed bootloader revision detected on this console.";
            values[0] = system_info->bootloader_version;
            labels[1] = "ShellUI Version";
            descriptions[1] = "Current user interface package revision.";
            values[1] = TITAN_UI_VERSION_STRING;
            labels[2] = "Kernel Version";
            descriptions[2] = "Current Project Titan kernel build identifier.";
            values[2] = TITAN_KERNEL_VERSION_STRING;
            labels[3] = "Console BIOS";
            descriptions[3] = "BIOS revision reported by the PlayStation 2.";
            values[3] = system_info->bios_version;
            labels[4] = "Console Model";
            descriptions[4] = "Console hardware model name or identification string.";
            values[4] = system_info->console_model;
            break;
    }

    TitanShellScene_DrawPanelBase(
        TitanShellScene_GetSettingsCategoryName(
            g_scene.settings_selected_category
        ),
        "",
        offset_x,
        alpha
    );

    first_visible = g_scene.settings_selected_row -
        (TITAN_SETTINGS_VISIBLE_ROW_COUNT - 1);
    if (first_visible < 0)
    {
        first_visible = 0;
    }
    if (first_visible > row_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT)
    {
        first_visible = row_count - TITAN_SETTINGS_VISIBLE_ROW_COUNT;
    }
    if (first_visible < 0)
    {
        first_visible = 0;
    }

    visible = 0;
    for (
        index = first_visible;
        index < row_count && visible < TITAN_SETTINGS_VISIBLE_ROW_COUNT;
        index++, visible++
    )
    {
        row_y = TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP);

        TitanShellScene_DrawSettingsListRow(
            row_icons[index],
            labels[index],
            descriptions[index],
            values[index],
            row_y,
            offset_x,
            alpha,
            TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute((float)index - g_scene.settings_row_position), 0.0f, 1.0f)
        );
    }

    TitanShellScene_DrawSettingsScrollbar(
        row_count,
        g_scene.settings_selected_row,
        offset_x,
        alpha
    );
}

static unsigned int TitanShellScene_Utf8SequenceLength(unsigned char first)
{
    if ((first & 0x80u) == 0u) return 1u;
    if ((first & 0xe0u) == 0xc0u) return 2u;
    if ((first & 0xf0u) == 0xe0u) return 3u;
    if ((first & 0xf8u) == 0xf0u) return 4u;
    return 1u;
}

static void TitanShellScene_CopyFittedTextPrefix(
    char *destination,
    unsigned int capacity,
    const char *source,
    float scale,
    float max_width,
    unsigned int *consumed_bytes
)
{
    unsigned int source_index = 0u;
    unsigned int length = 0u;

    if (consumed_bytes != 0)
        *consumed_bytes = 0u;
    if (destination == 0 || capacity == 0u)
        return;
    destination[0] = '\0';
    if (source == 0)
        return;

    while (source[source_index] != '\0')
    {
        unsigned int sequence;
        unsigned int available = 0u;
        unsigned int index;
        float measured;

        sequence = TitanShellScene_Utf8SequenceLength(
            (unsigned char)source[source_index]
        );
        while (available < sequence && source[source_index + available] != '\0')
            available++;
        if (available != sequence)
            sequence = 1u;
        if (length + sequence + 1u > capacity)
            break;
        for (index = 0u; index < sequence; index++)
            destination[length + index] = source[source_index + index];
        destination[length + sequence] = '\0';
        measured = TitanUiText_MeasureCached(scale, destination, TitanFont_MeasureText);
        if (measured > max_width)
        {
            destination[length] = '\0';
            break;
        }
        length += sequence;
        source_index += sequence;
    }
    if (consumed_bytes != 0)
        *consumed_bytes = source_index;
}

static void TitanShellScene_CopyEllipsizedText(
    char *destination,
    unsigned int capacity,
    const char *source,
    float scale,
    float max_width
)
{
    unsigned int source_index;
    unsigned int length;

    if (destination == 0 || capacity == 0u)
        return;

    destination[0] = '\0';
    source_index = 0u;
    length = 0u;
    if (source == 0)
        source = "";

    /* Copy only complete UTF-8 code points. Invalid/truncated sequences are
     * treated as single bytes so the helper never reads beyond NUL. */
    while (source[source_index] != '\0')
    {
        unsigned int sequence;
        unsigned int available;
        unsigned int index;

        sequence = TitanShellScene_Utf8SequenceLength(
            (unsigned char)source[source_index]
        );
        available = 0u;
        while (available < sequence && source[source_index + available] != '\0')
            available++;
        if (available != sequence)
            sequence = 1u;
        if (length + sequence + 1u > capacity)
            break;
        for (index = 0u; index < sequence; index++)
            destination[length + index] = source[source_index + index];
        length += sequence;
        source_index += sequence;
        destination[length] = '\0';
    }

    if (source[source_index] == '\0' &&
        TitanUiText_MeasureCached(scale, destination, TitanFont_MeasureText) <= max_width)
        return;

    /* Remove complete code points until an ellipsis fits the measured bound. */
    while (length > 0u)
    {
        length--;
        while (length > 0u &&
               (((unsigned char)destination[length] & 0xc0u) == 0x80u))
            length--;
        destination[length] = '\0';
        if (length + 4u <= capacity)
        {
            destination[length] = '.';
            destination[length + 1u] = '.';
            destination[length + 2u] = '.';
            destination[length + 3u] = '\0';
            if (TitanUiText_MeasureCached(scale, destination, TitanFont_MeasureText) <= max_width)
                return;
            destination[length] = '\0';
        }
    }
    destination[0] = '\0';
}

static void TitanShellScene_DrawPackageInstaller(
    float offset_x,
    unsigned char alpha
)
{
    const TitanPackageInstallerStatus *status;
    const TitanPackageInfo *selected_package;
    unsigned int count;

    TitanShellScene_DrawPanelBase(
        g_scene.package_installer_theme_only ? "Install theme" : "★ Package Installer",
        g_scene.package_installer_theme_only ? "Pacotes .tthm em mass:/TITAN/packages" : "",
        offset_x,
        alpha
    );

    count = TitanPackageInstaller_GetCount();
    status = TitanPackageInstaller_GetStatus();
    selected_package = count > 0u
        ? TitanPackageInstaller_Get((unsigned int)g_scene.package_installer_selected_row)
        : 0;

    if (g_scene.package_installer_detail_open)
    {
        char app_line[160];
        char type_line[96];
        char progress_text[32];
        const char *filename;
        const char *message;
        unsigned int percent;
        float progress_width;

        filename = status != 0 && status->package_filename[0] != '\0'
            ? status->package_filename
            : selected_package != 0
                ? selected_package->filename
                : "app.pkg";
        message = status != 0 && status->message[0] != '\0'
            ? status->message
            : "Preparando instalacao";
        percent = status != 0 ? status->progress_percent : 0u;
        if (percent > 100u) percent = 100u;

        snprintf(
            app_line,
            sizeof(app_line),
            "%s  /  %s",
            selected_package != 0 ? selected_package->title : "Application",
            selected_package != 0 ? selected_package->title_id : ""
        );
        snprintf(
            type_line,
            sizeof(type_line),
            "%s  /  v%s",
            selected_package != 0
                ? TitanPackageInstaller_GetTypeName(selected_package->app_type)
                : "Package",
            selected_package != 0 ? selected_package->version : ""
        );
        snprintf(progress_text, sizeof(progress_text), "%u%%", percent);

        TitanFont_DrawTextShadow(
            112.0f + offset_x,
            112.0f,
            0.90f,
            filename,
            246, 248, 252, alpha
        );
        TitanFont_DrawTextAlpha(
            112.0f + offset_x,
            151.0f,
            0.58f,
            app_line,
            210, 216, 230, alpha
        );
        TitanFont_DrawTextAlpha(
            112.0f + offset_x,
            178.0f,
            0.54f,
            type_line,
            210, 216, 230, alpha
        );

        TitanVideo_DrawRectAlpha(
            112.0f + offset_x,
            239.0f,
            500.0f,
            5.0f,
            56, 61, 72,
            TitanShellScene_ModulateAlpha(180u, alpha)
        );
        progress_width = 500.0f * ((float)percent / 100.0f);
        if (progress_width > 0.0f)
        {
            TitanVideo_DrawRectAlpha(
                112.0f + offset_x,
                239.0f,
                progress_width,
                5.0f,
                status != 0 && status->phase == TITAN_PACKAGE_PHASE_ERROR ? 223 : 236,
                status != 0 && status->phase == TITAN_PACKAGE_PHASE_ERROR ? 46 : 43,
                status != 0 && status->phase == TITAN_PACKAGE_PHASE_ERROR ? 58 : 82,
                alpha
            );
        }
        TitanFont_DrawTextShadow(
            112.0f + offset_x,
            260.0f,
            0.64f,
            message,
            246, 248, 252, alpha
        );
        TitanFont_DrawTextAlpha(
            617.0f + offset_x,
            233.0f,
            0.52f,
            progress_text,
            233, 75, 109, alpha
        );
        TitanFont_DrawTextAlpha(
            112.0f + offset_x,
            316.0f,
            0.52f,
            TitanPackageInstaller_IsActive()
                ? "O Cancelar instalacao"
                : "O Voltar para pacotes",
            233, 75, 109, alpha
        );
        return;
    }

    if (count == 0u)
    {
        TitanFont_DrawTextShadow(
            112.0f + offset_x,
            150.0f,
            0.88f,
            "Nenhum pacote valido encontrado",
            246, 248, 252, alpha
        );
        TitanFont_DrawTextAlpha(
            112.0f + offset_x,
            185.0f,
            0.58f,
            g_scene.package_installer_theme_only
                ? "Copie arquivos .tthm para mass:/TITAN/packages"
                : "Copie arquivos .pkg ou .tthm para mass:/TITAN/packages",
            210, 216, 230, alpha
        );
        TitanFont_DrawTextAlpha(
            112.0f + offset_x,
            214.0f,
            0.52f,
            g_scene.package_installer_theme_only
                ? "TRIANGLE Atualizar    O Voltar"
                : "TRIANGLE Atualizar    SQUARE Desinstalar    O Voltar",
            233, 75, 109, alpha
        );
        return;
    }

    {
        int first_visible;
        int index;
        int visible;

        first_visible = g_scene.package_installer_selected_row -
            (TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS - 1);
        if (first_visible < 0) first_visible = 0;
        if (first_visible > (int)count - TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS)
            first_visible = (int)count - TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS;
        if (first_visible < 0) first_visible = 0;

        visible = 0;
        for (index = first_visible;
             index < (int)count && visible < TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS;
             index++, visible++)
        {
            const TitanPackageInfo *package;
            char filename[128];
            char description[144];
            char bounded_description[144];
            char value[32];

            package = TitanPackageInstaller_Get((unsigned int)index);
            if (package == 0) continue;
            TitanShellScene_CopyEllipsizedText(
                filename,
                sizeof(filename),
                package->filename,
                0.84f,
                TITAN_PACKAGE_INSTALLER_TEXT_WIDTH
            );
            snprintf(
                description,
                sizeof(description),
                "%s / %s v%s / %u KiB",
                package->title,
                TitanPackageInstaller_GetTypeName(package->app_type),
                package->version,
                (package->payload_size + 1023u) / 1024u
            );
            TitanShellScene_CopyEllipsizedText(
                bounded_description,
                sizeof(bounded_description),
                description,
                0.54f,
                TITAN_PACKAGE_INSTALLER_TEXT_WIDTH
            );
            snprintf(
                value,
                sizeof(value),
                "%s",
                package->installed ? "Instalado" : "X Instalar"
            );

            /* Package rows use their own vertical budget. The generic Settings
             * row is 70 px tall, so a 74 px step guarantees that title,
             * metadata, action/status and selection chrome cannot invade the
             * next package row. */
            TitanShellScene_DrawSettingsListRow(
                TITAN_ICON_DEBUG_SETTINGS,
                filename,
                bounded_description,
                value,
                TITAN_PACKAGE_INSTALLER_ROW_TOP +
                    ((float)visible * TITAN_PACKAGE_INSTALLER_ROW_STEP),
                offset_x,
                alpha,
                TitanShellScene_Clamp(
                    1.0f - TitanAnimation_Absolute(
                        (float)index - g_scene.package_installer_row_position
                    ),
                    0.0f,
                    1.0f
                )
            );
        }

        TitanShellScene_DrawSettingsScrollbar(
            (int)count,
            g_scene.package_installer_selected_row,
            offset_x,
            alpha
        );
    }
}


static void TitanShellScene_DrawSystemUpdatePage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanSystemUpdateStatus *status;
    unsigned int count;
    unsigned int selected;
    unsigned int first;
    unsigned int visible;
    char current[64];

    TitanShellScene_DrawPanelBase(
        "System Software Update",
        "Official Project Titan .pup packages via USB",
        offset_x,
        alpha
    );

    snprintf(current, sizeof(current), TitanLocalization_Compat("Current version: %s / build %u"),
        TITAN_KERNEL_VERSION_STRING, TITAN_SYSTEM_UPDATE_CURRENT_BUILD);
    TitanFont_DrawTextAlpha(108.0f + offset_x, 74.0f, 0.54f, current,
        218, 223, 233, alpha);

    status = TitanSystemUpdate_GetStatus();
    if (TitanSystemUpdate_IsActive() ||
        (status != 0 && (status->phase == TITAN_SYSTEM_UPDATE_COMPLETE ||
                        status->phase == TITAN_SYSTEM_UPDATE_ERROR)))
    {
        char progress[32];
        unsigned int percent = status != 0 ? status->progress_percent : 0u;
        if (percent > 100u) percent = 100u;
        snprintf(progress, sizeof(progress), "%u%%", percent);
        TitanFont_DrawTextShadow(108.0f + offset_x, 128.0f, 0.80f,
            status != 0 ? status->message : "Updating", 246,248,252,alpha);
        TitanVideo_DrawRectAlpha(108.0f+offset_x, 184.0f, 560.0f, 6.0f,
            54,58,69,TitanShellScene_ModulateAlpha(190u,alpha));
        TitanVideo_DrawRectAlpha(108.0f+offset_x, 184.0f,
            560.0f*((float)percent/100.0f), 6.0f,
            233,43,82,alpha);
        TitanFont_DrawTextAlpha(681.0f+offset_x, 176.0f, 0.52f,
            progress,233,75,109,alpha);
        TitanFont_DrawTextAlpha(108.0f+offset_x, 226.0f, 0.52f,
            TitanSystemUpdate_IsActive() ? "O Cancelar" : "O Voltar",
            233,75,109,alpha);
        return;
    }

    count = TitanSystemUpdate_GetCount();
    if (count == 0u)
    {
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_DEBUG_SETTINGS,
            "No update file found",
            "Copie um .pup para mass:/TITAN/update ou mass:/TITAN/packages.",
            "TRIANGLE Procurar novamente",
            116.0f, offset_x, alpha, 1.0f
        );
        return;
    }

    selected = g_scene.system_update_selected_row < 0
        ? 0u : (unsigned int)g_scene.system_update_selected_row;
    if (selected >= count) selected = count - 1u;
    first = selected >= 4u ? selected - 4u : 0u;
    if (first + 5u > count) first = count > 5u ? count - 5u : 0u;

    for (visible=0u; visible<5u && first+visible<count; visible++)
    {
        const TitanSystemUpdateInfo *info = TitanSystemUpdate_Get(first+visible);
        char value[64];
        if (info == 0) continue;
        snprintf(value,sizeof(value),"v%s / build %u",info->version,info->build_number);
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_DEBUG_SETTINGS,
            info->filename,
            "System Software Update",
            value,
            TITAN_SETTINGS_ROW_TOP + ((float)visible*TITAN_SETTINGS_ROW_STEP),
            offset_x,
            alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(
                (float)(first+visible)-g_scene.system_update_row_position),0.0f,1.0f)
        );
    }
    TitanShellScene_DrawSettingsScrollbar((int)count,(int)selected,offset_x,alpha);
}

static int TitanShellScene_GetNetworkPageRowCount(void)
{
    unsigned int count;
    switch (g_scene.network_page_mode)
    {
        case TITAN_NETWORK_PAGE_SETUP_METHOD:
        case TITAN_NETWORK_PAGE_WIFI_MODE:
        case TITAN_NETWORK_PAGE_LAN_MODE:
            return 2;
        case TITAN_NETWORK_PAGE_WIFI_LIST:
            count = TitanNetwork_GetWifiNetworkCount() + 1u; /* + Outra rede */
            if (count > TITAN_SETTINGS_MAX_ROW_COUNT) count = TITAN_SETTINGS_MAX_ROW_COUNT;
            return (int)count;
        case TITAN_NETWORK_PAGE_TEST:
            return 0; /* diagnostic/result page: no selectable rows */
        case TITAN_NETWORK_PAGE_STATUS:
        case TITAN_NETWORK_PAGE_SERVICES:
            return 5;
        case TITAN_NETWORK_PAGE_GAME_DATA:
            return 4;
        case TITAN_NETWORK_PAGE_ASSET_WIZARD:
            return 4;
        case TITAN_NETWORK_PAGE_ASSET_PROGRESS:
            return 0;
        default:
            return 0;
    }
}

static void TitanShellScene_DrawNetworkPage(float offset_x, unsigned char alpha)
{
    const TitanNetworkStatus *network = TitanNetwork_GetStatus();
    int row_count = TitanShellScene_GetNetworkPageRowCount();
    int selected = g_scene.network_page_selected_row;
    int first = 0;
    int visible;

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_GAME_DATA)
    {
        const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
        char progress[64];
        char auto_value[24];
        if (assets != 0 && assets->manual_session_active)
            snprintf(progress,sizeof(progress),"%u / %u",assets->session_completed_assets + assets->session_failed_assets,assets->session_total_assets);
        else
            snprintf(progress,sizeof(progress),"Press X");
        snprintf(auto_value,sizeof(auto_value),"%s",TitanAssetService_IsAutomaticDownloadEnabled()?"Enabled":"Disabled");
        TitanShellScene_DrawPanelBase(
            "Game data",
            "Artwork downloads are separate from connectivity; automatic mode is disabled by default.",
            offset_x, alpha);
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Download game assets",
            "Downloads only missing artwork. Game icons are processed first; backgrounds follow.",
            assets != 0 && assets->manual_session_active ? progress : "Manual",
            TITAN_SETTINGS_ROW_TOP,offset_x,alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Update existing assets",
            "Refreshes each game icon and background one transfer at a time, replacing the cache atomically.",
            assets != 0 && assets->manual_session_active ? "Active operation" : "Manual",
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP,offset_x,alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(1.0f-g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Clear asset cache",
            "Removes TIMG cache through Storage Scheduler quanta without blocking navigation.",
            assets != 0 && assets->manual_clear_cache ? progress : "Local cache",
            TITAN_SETTINGS_ROW_TOP+2.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(2.0f-g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Automatic download",
            "Optional. Waits for 30 s ONLINE plus idle Home; any input pauses it. Disabled by default.",
            auto_value,
            TITAN_SETTINGS_ROW_TOP+3.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(3.0f-g_scene.network_page_row_position),0.0f,1.0f));
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_WIZARD)
    {
        const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
        const TitanNeutrinoGame *selected_game = assets != 0
            ? TitanNeutrino_GetGame(assets->selected_game_index) : 0;
        const char *title = selected_game != 0 && selected_game->title[0] != '\0'
            ? selected_game->title : "No game selected";
        TitanShellScene_DrawPanelBase(
            "Asset Wizard",
            "Convert local PNG/JPEG from ART to persistent Titan TIMG assets. No Internet required.",
            offset_x, alpha);
        TitanShellScene_DrawSettingsListRow(TITAN_ICON_SYSTEM,
            "Convert selected icon",
            "Converts the selected game's OPL icon; _COV is a last-resort square crop.",
            title, TITAN_SETTINGS_ROW_TOP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(TITAN_ICON_SYSTEM,
            "Convert selected background",
            "Converts the selected game's OPL background or screenshot.",
            title, TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(1.0f-g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(TITAN_ICON_SYSTEM,
            "Convert all artwork",
            "Processes available icons and backgrounds for every game in the library.",
            assets != 0 && assets->artwork_wizard_active ? "Converting" : "Offline batch",
            TITAN_SETTINGS_ROW_TOP+2.0f*TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(2.0f-g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(TITAN_ICON_SYSTEM,
            "Clear asset cache",
            "Removes TIMG files only; this is separate from the library cache.",
            assets != 0 && assets->manual_clear_cache ? "Working" : "Local cache",
            TITAN_SETTINGS_ROW_TOP+3.0f*TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(3.0f-g_scene.network_page_row_position),0.0f,1.0f));
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_PROGRESS)
    {
        const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
        const TitanNeutrinoGame *current_game = 0;
        char headline[96];
        char detail[128];
        char percent_text[16];
        unsigned int done = assets != 0 ? assets->session_completed_assets +
            assets->session_failed_assets + assets->session_unavailable_assets : 0u;
        unsigned int total = assets != 0 ? assets->session_total_assets : 0u;
        unsigned int percent = 0u;
        int clear_cache = g_scene.asset_wizard_progress_is_clear_cache ||
            (assets != 0 && assets->manual_clear_cache);
        int wizard = g_scene.asset_wizard_return_mode == TITAN_NETWORK_PAGE_ASSET_WIZARD && !clear_cache;
        if (wizard && assets != 0 && done < total)
            percent = assets->current_asset_progress_percent;
        else if (total != 0u)
            percent = done >= total ? 100u : (done * 100u) / total;
        if (percent > 100u) percent = 100u;
        if (assets != 0 && assets->current_game_index < TitanNeutrino_GetGameCount())
            current_game = TitanNeutrino_GetGame(assets->current_game_index);

        TitanShellScene_DrawPanelBase(wizard ? "Asset Wizard" :
            (clear_cache ? "Clear asset cache" : "Asset download"),
            "Current item progress", offset_x, alpha);
        if (current_game != 0)
        {
            snprintf(headline, sizeof(headline), "%s  |  %s", current_game->title,
                assets->current_kind == TITAN_ASSET_KIND_BACKGROUND ?
                    TitanLocalization_Compat("Background") : TitanLocalization_Compat("Icon"));
        }
        else
            snprintf(headline, sizeof(headline), "%s", TitanAssetService_GetManualPhaseName(
                assets != 0 ? assets->manual_phase : TITAN_ASSET_MANUAL_IDLE));
        if (wizard && assets != 0)
            snprintf(detail, sizeof(detail), TitanLocalization_Compat("Asset %u of %u  |  %s  |  %u%%"),
                done < total ? done + 1u : total, total,
                TitanAssetService_GetManualPhaseName(assets->manual_phase), percent);
        else
            snprintf(detail, sizeof(detail), TitanLocalization_Compat("%s  |  %u / %u complete  |  %u%%"),
                TitanAssetService_GetManualPhaseName(assets != 0 ? assets->manual_phase : TITAN_ASSET_MANUAL_IDLE),
                done, total, percent);
        snprintf(percent_text, sizeof(percent_text), "%u%%", percent);
        TitanFont_DrawTextShadow(108.0f + offset_x, 103.0f, 0.72f, headline,
            246, 248, 252, alpha);
        TitanFont_DrawTextAlpha(108.0f + offset_x, 141.0f, 0.50f, detail,
            174, 184, 201, alpha);
        TitanVideo_DrawRectAlpha(108.0f + offset_x, 180.0f, 560.0f, 8.0f,
            54, 58, 69, TitanShellScene_ModulateAlpha(205u, alpha));
        if (percent != 0u)
            TitanVideo_DrawRectAlpha(108.0f + offset_x, 180.0f,
                560.0f * ((float)percent / 100.0f), 8.0f,
                233, 43, 82, alpha);
        TitanFont_DrawTextAlpha(681.0f + offset_x, 172.0f, 0.56f,
            percent_text, 233, 75, 109, alpha);
        TitanFont_DrawTextAlpha(108.0f + offset_x, 218.0f, 0.48f,
            wizard ? TitanLocalization_Compat("Source artwork is read, converted, then saved before the next item.") :
            TitanLocalization_Compat("The operation continues in bounded storage steps."),
            151, 159, 179, alpha);
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SETUP_METHOD)
    {
        TitanShellScene_DrawPanelBase(
            "Set Up Internet Connection",
            "How do you want to connect to the network?",
            offset_x, alpha);
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM, "Use Wi-Fi",
            "Use the Qualcomm Atheros AR9271 USB adapter.",
            network->wifi_adapter_present ? "Adapter detected" : "Automatic scan when detected",
            TITAN_SETTINGS_ROW_TOP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM, "Use LAN Cable",
            "Use the RJ45 Ethernet port on the Network Adapter / PS2 Slim.",
            !network->ethernet_link_known
                ? "RJ45 / SMAP"
                : (network->ethernet_link_up ? "Cable connected" : "Cable disconnected"),
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(1.0f-g_scene.network_page_row_position),0.0f,1.0f));
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_MODE ||
        g_scene.network_page_mode == TITAN_NETWORK_PAGE_LAN_MODE)
    {
        TitanShellScene_DrawPanelBase(
            "Set Up Internet Connection",
            g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_MODE
                ? "How do you want to configure the Wi-Fi connection?"
                : "How do you want to configure the LAN connection?",
            offset_x, alpha);
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM, "Easy",
            "Titan configures IP address, DHCP and DNS automatically.",
            "Recommended",
            TITAN_SETTINGS_ROW_TOP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(g_scene.network_page_row_position),0.0f,1.0f));
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM, "Custom",
            "Keeps the advanced configuration and interface-selection flow.",
            "Advanced configuration",
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP, offset_x, alpha,
            TitanShellScene_Clamp(1.0f-TitanAnimation_Absolute(1.0f-g_scene.network_page_row_position),0.0f,1.0f));
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_LIST)
    {
        unsigned int count = TitanNetwork_GetWifiNetworkCount();
        TitanShellScene_DrawPanelBase(
            "Set Up Internet Connection",
            network->wifi_scanning
                ? "Searching for nearby Wi-Fi networks..."
                : "Which Wi-Fi network do you want to use?",
            offset_x, alpha);

        if (selected >= 4) first = selected - 4;
        if (first + 5 > row_count) first = row_count > 5 ? row_count - 5 : 0;
        for (visible=0; visible<5 && first+visible<row_count; ++visible)
        {
            unsigned int index = (unsigned int)(first + visible);
            float focus = TitanShellScene_Clamp(
                1.0f-TitanAnimation_Absolute((float)index-g_scene.network_page_row_position),0.0f,1.0f);
            if (index < count)
            {
                const TitanNetworkWifiAp *ap = TitanNetwork_GetWifiNetwork(index);
                char value[80];
                char detail[112];
                if (ap == 0) continue;
                snprintf(value,sizeof(value),"%s  %u%%",
                    TitanLocalization_Compat(
                        TitanNetwork_GetWifiSecurityName(ap->security)
                    ),
                    TitanNetwork_GetWifiSignalPercent(ap));
                snprintf(detail,sizeof(detail),TitanLocalization_Compat("Channel %u / %d dBm%s"),
                    (unsigned int)ap->channel,(int)ap->rssi_dbm,
                    ap->security == TITAN_NETWORK_WIFI_WPA2 ? TitanLocalization_Compat(" / protected") : "");
                TitanShellScene_DrawSettingsListRow(
                    TITAN_ICON_SYSTEM,
                    ap->ssid[0] ? ap->ssid : "Hidden network",
                    detail,value,
                    TITAN_SETTINGS_ROW_TOP+((float)visible*TITAN_SETTINGS_ROW_STEP),
                    offset_x,alpha,focus);
            }
            else
            {
                TitanShellScene_DrawSettingsListRow(
                    TITAN_ICON_SYSTEM,"Other network...",
                    "Use only for a hidden network or an SSID that did not appear in the scan.",
                    "Insert SSID manually",
                    TITAN_SETTINGS_ROW_TOP+((float)visible*TITAN_SETTINGS_ROW_STEP),
                    offset_x,alpha,focus);
            }
        }
        TitanShellScene_DrawSettingsScrollbar(row_count,selected,offset_x,alpha);
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_TEST)
    {
        const TitanNetworkConnectionTestStatus *test = TitanNetwork_GetConnectionTestStatus();
        const int test_valid = test != 0 && test->valid;
        const char *ip_result = test_valid
            ? TitanNetwork_GetTestResultName(test->ip_result) : "Not started";
        const char *internet_result = test_valid
            ? TitanNetwork_GetTestResultName(test->internet_result) : "Not started";
        const char *physical_result = test_valid
            ? TitanNetwork_GetTestResultName(test->physical_result) : "Not started";
        const char *software_detail = test_valid && test->software_detail[0]
            ? test->software_detail
            : (test != 0 && test->generation > 0u
                ? "Connection settings changed; run the test again."
                : "Software self-test has not started");
        const char *physical_detail = test_valid && test->physical_detail[0]
            ? test->physical_detail
            : "Physical link was not tested";
        const char *ip_detail = test_valid && test->ip_detail[0]
            ? test->ip_detail : "Address acquisition has not started";
        const char *internet_detail = test_valid && test->internet_detail[0]
            ? test->internet_detail : "External connectivity probe has not started";

        TitanShellScene_DrawPanelBase(
            "Test Internet Connection",
            test_valid && test->running ? "Running connection test..." : "Current connection test",
            offset_x,alpha
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Obtain IP Address",ip_detail,ip_result,
            TITAN_SETTINGS_ROW_TOP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Internet Connection",internet_detail,internet_result,
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Connection Type",software_detail,
            TitanNetwork_GetPreferenceName(),
            TITAN_SETTINGS_ROW_TOP+2.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Physical Link",physical_detail,physical_result,
            TITAN_SETTINGS_ROW_TOP+3.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"IP Address","Current IPv4 address.",
            network->ip[0]?network->ip:"-",
            TITAN_SETTINGS_ROW_TOP+4.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_STATUS)
    {
        const char *connection_detail;
        const char *second_label;
        const char *second_detail;
        const char *second_value;

        if (!network->enabled)
            connection_detail = "Network services are disabled";
        else if (network->main_interface_ready)
            connection_detail = "Selected interface is active.";
        else if (network->state == TITAN_NETWORK_ERROR)
            connection_detail = "Selected interface failed to become ready.";
        else
            connection_detail = "Selected interface is still initializing.";

        if (network->preference == TITAN_NETWORK_PREFER_ETHERNET)
        {
            second_label = "Physical Link";
            if (!network->ethernet_link_known)
            {
                second_detail = "Ethernet physical link has not been verified yet.";
                second_value = "Not tested";
            }
            else if (network->ethernet_link_up)
            {
                second_detail = "Ethernet physical link is active.";
                second_value = "Cable connected";
            }
            else
            {
                second_detail = "Ethernet PHY reported link down.";
                second_value = "Cable disconnected";
            }
        }
        else
        {
            second_label = "SSID";
            second_detail = "Selected Wi-Fi network.";
            second_value = network->ssid[0] ? network->ssid : "-";
        }

        TitanShellScene_DrawPanelBase(
            "View Connection Status","Current connection information",
            offset_x,alpha
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Connection Type",connection_detail,
            TitanNetwork_GetPreferenceName(),
            TITAN_SETTINGS_ROW_TOP,offset_x,alpha,1.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,second_label,second_detail,second_value,
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"IP Address","Current IPv4 address.",
            network->ip[0]?network->ip:"-",
            TITAN_SETTINGS_ROW_TOP+2.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Gateway","Router/gateway learned from the connection.",
            network->gateway[0]?network->gateway:"-",
            TITAN_SETTINGS_ROW_TOP+3.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"DNS","DNS server learned from the connection.",
            network->dns[0]?network->dns:"-",
            TITAN_SETTINGS_ROW_TOP+4.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        return;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SERVICES)
    {
        const char *netman_value;
        const char *netman_detail;
        const char *ethernet_value;
        const char *ethernet_detail;
        const char *wifi_value;
        const char *wifi_detail;
        const char *ps2ip_value;
        const char *ps2ip_detail;
        const char *active_value;
        const char *active_detail;

        if (network->netman_rpc_ready && network->netman_ee_ready)
        {
            netman_value = "Ready";
            netman_detail = "IOP RPC server and EE NETMAN client are ready.";
        }
        else if (network->state == TITAN_NETWORK_ERROR &&
                 (!network->netman_rpc_ready || !network->netman_ee_ready))
        {
            netman_value = "Failed";
            netman_detail = "NETMAN did not complete its live bootstrap.";
        }
        else
        {
            netman_value = "Starting";
            netman_detail = "NETMAN bootstrap is still in progress.";
        }

        if (network->dev9_ready && network->ethernet_stack_loaded)
        {
            ethernet_value = "Ready";
            if (!network->ethernet_link_known)
                ethernet_detail = "DEV9 and SMAP are ready; physical link is not verified yet.";
            else if (network->ethernet_link_up)
                ethernet_detail = "DEV9 and SMAP are ready; physical link is up.";
            else
                ethernet_detail = "DEV9 and SMAP are ready; physical link is down.";
        }
        else if (network->preference == TITAN_NETWORK_PREFER_ETHERNET &&
                 network->state == TITAN_NETWORK_ERROR)
        {
            ethernet_value = "Failed";
            ethernet_detail = "Ethernet backend did not complete DEV9 / SMAP initialization.";
        }
        else
        {
            ethernet_value = "Inactive";
            ethernet_detail = "Ethernet backend has not been initialized in this session.";
        }

        if (network->wifi_irx_loaded && network->wifi_adapter_present &&
            network->wifi_rpc_bound)
        {
            wifi_value = "Ready";
            wifi_detail = "AR9271 driver, adapter and control RPC are available.";
        }
        else if (network->wifi_irx_loaded)
        {
            wifi_value = "Starting";
            wifi_detail = network->wifi_adapter_present
                ? "AR9271 detected; control path is still initializing."
                : "AR9271 driver is resident; waiting for the USB adapter.";
        }
        else
        {
            wifi_value = "Inactive";
            wifi_detail = "AR9271 / WFI backend is not active.";
        }

        if (network->ps2ip_ready)
        {
            ps2ip_value = "Ready";
            ps2ip_detail = "ps2ip / lwIP is initialized on sm0.";
        }
        else if (network->state == TITAN_NETWORK_ERROR &&
                 network->netman_ee_ready)
        {
            ps2ip_value = "Failed";
            ps2ip_detail = "ps2ip / lwIP did not complete initialization.";
        }
        else
        {
            ps2ip_value = "Starting";
            ps2ip_detail = "ps2ip / lwIP has not completed initialization yet.";
        }

        if (network->main_interface_ready)
        {
            active_value = TitanNetwork_GetPreferenceName();
            active_detail = network->preference == TITAN_NETWORK_PREFER_ETHERNET
                ? "SMAP is the live NETMAN main interface."
                : "WFI is the live NETMAN main interface.";
        }
        else if (!network->enabled)
        {
            active_value = "Disabled";
            active_detail = "Network services are disabled";
        }
        else if (network->state == TITAN_NETWORK_ERROR)
        {
            active_value = "Failed";
            active_detail = "The selected interface did not become the NETMAN main interface.";
        }
        else
        {
            active_value = "Starting";
            active_detail = "The selected interface is still being prepared.";
        }

        TitanShellScene_DrawPanelBase(
            "Network Services Status","Project Titan drivers and backends",
            offset_x,alpha
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"NETMAN",netman_detail,netman_value,
            TITAN_SETTINGS_ROW_TOP,offset_x,alpha,1.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"DEV9 / SMAP",ethernet_detail,ethernet_value,
            TITAN_SETTINGS_ROW_TOP+TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"AR9271 / WFI",wifi_detail,wifi_value,
            TITAN_SETTINGS_ROW_TOP+2.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"ps2ip / lwIP",ps2ip_detail,ps2ip_value,
            TITAN_SETTINGS_ROW_TOP+3.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        TitanShellScene_DrawSettingsListRow(
            TITAN_ICON_SYSTEM,"Active interface",active_detail,active_value,
            TITAN_SETTINGS_ROW_TOP+4.0f*TITAN_SETTINGS_ROW_STEP,offset_x,alpha,0.0f
        );
        return;
    }
}

static void TitanShellScene_OpenSettingsCategoryAnimated(void)
{
    /* Every Settings category, including Themes and DEX-only Debug Settings,
     * enters through the exact same depth transition. Resetting the source
     * position here prevents a quick back/re-enter from inheriting a partially
     * completed transition and visually "teleporting" into the subpage. */
    g_scene.settings_depth_transition = 0.0f;
    g_scene.settings_category_open = 1;
    g_scene.settings_leaf_mode = TITAN_SETTINGS_LEAF_CLOSED;
    g_scene.settings_leaf_open = 0;
    g_scene.settings_leaf_transition = 0.0f;
    g_scene.wireless_detail_mode = TITAN_WIRELESS_DETAIL_CLOSED;
    g_scene.wireless_detail_open = 0;
    g_scene.settings_action_open = 0;
    TitanWirelessAccessories_SetVisible(0);
    if (g_scene.settings_selected_category == TITAN_SETTINGS_STORAGE)
        TitanStorageInventory_RequestRefresh();
    if (g_scene.settings_selected_category == TITAN_SETTINGS_THEMES)
    {
        g_scene.settings_selected_row = (int)TitanTheme_GetActiveIndex();
        g_scene.settings_row_position = (float)g_scene.settings_selected_row;
    }
    else
    {
        g_scene.settings_selected_row = 0;
        g_scene.settings_row_position = 0.0f;
    }
}

static void TitanShellScene_CloseSettingsCategoryAnimated(void)
{
    g_scene.settings_category_open = 0;
    g_scene.settings_selected_row = 0;
    g_scene.settings_leaf_open = 0;
    g_scene.wireless_detail_open = 0;
    g_scene.settings_action_open = 0;
    TitanWirelessAccessories_SetVisible(0);
}

static void TitanShellScene_OpenSettingsLeaf(TitanSettingsLeafMode mode)
{
    g_scene.settings_leaf_mode = mode;
    g_scene.settings_leaf_open = 1;
    g_scene.settings_leaf_transition = 0.0f;
    g_scene.settings_leaf_selected_row = 0;
    g_scene.settings_leaf_row_position = 0.0f;
    g_scene.wireless_detail_open = 0;
    g_scene.wireless_detail_mode = TITAN_WIRELESS_DETAIL_CLOSED;
    g_scene.wireless_detail_transition = 0.0f;
    g_scene.wireless_detail_selected_row = 0;
    g_scene.wireless_detail_row_position = 0.0f;
    if (mode == TITAN_SETTINGS_LEAF_WIRELESS)
        TitanWirelessAccessories_SetVisible(1);
}

static void TitanShellScene_CloseSettingsLeaf(void)
{
    g_scene.settings_leaf_open = 0;
    g_scene.wireless_detail_open = 0;
    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_WIRELESS)
        TitanWirelessAccessories_SetVisible(0);
}

static void TitanShellScene_OpenWirelessDetail(TitanWirelessDetailMode mode)
{
    g_scene.wireless_detail_mode = mode;
    g_scene.wireless_detail_open = 1;
    g_scene.wireless_detail_transition = 0.0f;
    g_scene.wireless_detail_selected_row = 0;
    g_scene.wireless_detail_row_position = 0.0f;
    TitanWirelessAccessories_SetVisible(1);
    if (mode == TITAN_WIRELESS_DETAIL_SCAN)
    {
        if (TitanWirelessAccessories_BeginScan())
            TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY, "Bluetooth", "Procurando acessórios...");
        else
            TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, "Bluetooth", "A busca já está ocupada; o Titan continuará consultando o estado");
    }
}

static void TitanShellScene_CloseWirelessDetail(void)
{
    g_scene.wireless_detail_open = 0;
    g_scene.wireless_connect_source_index = 0xffffffffu;
}

static int TitanShellScene_UpdateSettingsAction(const TitanInputState *input)
{
    int row_count;
    if (!g_scene.settings_action_open) return 0;
    row_count = g_scene.settings_action_kind == TITAN_SETTINGS_ACTION_WIRELESS_DEVICE ? 3 : 2;

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        g_scene.settings_action_open = 0;
        g_scene.settings_action_kind = TITAN_SETTINGS_ACTION_NONE;
        return 1;
    }
    if (input->pressed & TITAN_INPUT_BUTTON_UP)
    {
        g_scene.settings_action_selected--;
        if (g_scene.settings_action_selected < 0) g_scene.settings_action_selected = row_count - 1;
    }
    if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
    {
        g_scene.settings_action_selected++;
        if (g_scene.settings_action_selected >= row_count) g_scene.settings_action_selected = 0;
    }
    if (!(input->pressed & TITAN_INPUT_BUTTON_CROSS)) return 1;

    if (g_scene.settings_action_selected == 0)
    {
        if (g_scene.settings_action_kind == TITAN_SETTINGS_ACTION_WIRELESS_DEVICE)
            TitanShellScene_ShowWirelessAccessoryInformation(g_scene.settings_action_source_index);
        else
            TitanShellScene_ShowStorageEntryInformation(
                g_scene.settings_action_kind, g_scene.settings_action_source_index);
        g_scene.settings_action_open = 0;
        return 1;
    }

    if (g_scene.settings_action_kind == TITAN_SETTINGS_ACTION_WIRELESS_DEVICE)
    {
        const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
        const char *name = "Acessório Bluetooth";
        if (state != 0 && g_scene.settings_action_source_index < state->count)
            name = TitanShellScene_GetWirelessAccessoryName(
                &state->accessories[g_scene.settings_action_source_index]);
        if (g_scene.settings_action_selected == 1)
        {
            if (TitanWirelessAccessories_Disconnect(g_scene.settings_action_source_index))
                TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY, name, "Desconectando acessório...");
            else
                TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, name, "Não foi possível enfileirar a desconexão");
        }
        else
        {
            if (TitanWirelessAccessories_Forget(g_scene.settings_action_source_index))
                TitanNotifications_Push(TITAN_NOTIFICATION_INFO, name, "Acessório removido da lista de pareados");
            else
                TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, name, "Não foi possível esquecer o acessório agora");
        }
    }
    else if (g_scene.settings_action_kind == TITAN_SETTINGS_ACTION_STORAGE_THEME)
    {
        const TitanThemeInfo *theme = TitanTheme_Get(g_scene.settings_action_source_index);
        if (theme != 0)
        {
            if (!TitanTheme_CanDelete(g_scene.settings_action_source_index))
                TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, "Temas", "O tema ativo ou protegido não pode ser excluído");
            else
                (void)TitanNotifications_OpenConfirmation(
                    "Excluir tema?", theme->name,
                    "O tema instalado será removido do armazenamento Titan.",
                    "Cancel", "Excluir", TitanShellScene_ConfirmDeleteStoredTheme,
                    (void *)(uintptr_t)g_scene.settings_action_source_index);
        }
    }
    else
    {
        TitanShellScene_RequestStoredApplicationUninstall(g_scene.settings_action_source_index);
    }
    g_scene.settings_action_open = 0;
    return 1;
}

static int TitanShellScene_UpdateSettingsLeaf(const TitanInputState *input)
{
    unsigned int count;

    if (!g_scene.settings_leaf_open && g_scene.settings_leaf_transition <= 0.001f)
    {
        if (g_scene.settings_leaf_mode != TITAN_SETTINGS_LEAF_CLOSED)
            g_scene.settings_leaf_mode = TITAN_SETTINGS_LEAF_CLOSED;
        return 0;
    }

    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_WIRELESS)
    {
        const TitanPademuWirelessState *state;
        TitanWirelessAccessories_SetVisible(g_scene.settings_leaf_open ? 1 : 0);
        state = TitanWirelessAccessories_GetState();
        (void)state;
    }

    if (TitanShellScene_UpdateSettingsAction(input)) return 1;

    if (g_scene.wireless_detail_open)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanShellScene_CloseWirelessDetail();
            return 1;
        }
        count = g_scene.wireless_detail_mode == TITAN_WIRELESS_DETAIL_SCAN
            ? TitanWirelessAccessories_GetSeenCount()
            : TitanWirelessAccessories_GetRegisteredCount();
        if (count > 0u)
        {
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.wireless_detail_selected_row--;
                if (g_scene.wireless_detail_selected_row < 0)
                    g_scene.wireless_detail_selected_row = (int)count - 1;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.wireless_detail_selected_row++;
                if ((unsigned int)g_scene.wireless_detail_selected_row >= count)
                    g_scene.wireless_detail_selected_row = 0;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                unsigned int source_index = 0u;
                const TitanPademuWirelessAccessory *accessory;
                if (g_scene.wireless_detail_mode == TITAN_WIRELESS_DETAIL_SCAN)
                {
                    accessory = TitanWirelessAccessories_GetSeen(
                        (unsigned int)g_scene.wireless_detail_selected_row, &source_index);
                    if (accessory != 0)
                    {
                        const TitanPademuWirelessState *wireless_state = TitanWirelessAccessories_GetState();
                        if (wireless_state != 0 &&
                            (wireless_state->flags & TITAN_PADEMU_WIRELESS_SCANNING) != 0u)
                        {
                            /* The Classic HCI command endpoint is serialized. Do not
                             * overlap Create Connection with Inquiry/Remote Name; the
                             * scan is intentionally short and selection remains visible. */
                            TitanNotifications_Push(TITAN_NOTIFICATION_INFO, "Bluetooth", "Concluindo a busca; selecione novamente em instantes");
                        }
                        else if (TitanWirelessAccessories_Connect(source_index))
                        {
                            TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY,
                                TitanShellScene_GetWirelessAccessoryName(accessory),
                                "Conectando e registrando acessório...");
                        }
                        else
                            TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, "Bluetooth", "O transporte está ocupado; tente novamente");
                    }
                }
                else
                {
                    accessory = TitanWirelessAccessories_GetRegistered(
                        (unsigned int)g_scene.wireless_detail_selected_row, &source_index);
                    if (accessory != 0)
                        TitanShellScene_OpenSettingsAction(TITAN_SETTINGS_ACTION_WIRELESS_DEVICE, source_index);
                }
            }
        }
        return 1;
    }

    if (!g_scene.settings_leaf_open) return 1;

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        TitanShellScene_CloseSettingsLeaf();
        return 1;
    }

    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_WIRELESS)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_UP)
            g_scene.settings_leaf_selected_row = 1 - g_scene.settings_leaf_selected_row;
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            g_scene.settings_leaf_selected_row = 1 - g_scene.settings_leaf_selected_row;
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            TitanShellScene_OpenWirelessDetail(
                g_scene.settings_leaf_selected_row == 0
                    ? TITAN_WIRELESS_DETAIL_SCAN
                    : TITAN_WIRELESS_DETAIL_REGISTERED);
        }
        return 1;
    }

    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_CONTROLLER_ASSIGNMENT)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_UP) {
            g_scene.settings_leaf_selected_row--;
            if (g_scene.settings_leaf_selected_row < 0)
                g_scene.settings_leaf_selected_row = (int)TITAN_UNIFIED_INPUT_MAX_PLAYERS - 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN) {
            g_scene.settings_leaf_selected_row++;
            if (g_scene.settings_leaf_selected_row >= (int)TITAN_UNIFIED_INPUT_MAX_PLAYERS)
                g_scene.settings_leaf_selected_row = 0;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            (void)TitanControllerAssignment_CyclePlayer((unsigned int)g_scene.settings_leaf_selected_row);
        if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
            (void)TitanControllerAssignment_SetAutomatic((unsigned int)g_scene.settings_leaf_selected_row);
        return 1;
    }

    if (g_scene.settings_leaf_mode == TITAN_SETTINGS_LEAF_MULTITAP)
    {
        if ((input->pressed & TITAN_INPUT_BUTTON_UP) || (input->pressed & TITAN_INPUT_BUTTON_DOWN))
            g_scene.settings_leaf_selected_row = 1 - g_scene.settings_leaf_selected_row;
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS) {
            if (g_scene.settings_leaf_selected_row == 0)
                (void)TitanControllerAssignment_SetMultitapEnabled(
                    !TitanControllerAssignment_GetMultitapEnabled());
            else
                (void)TitanControllerAssignment_SetMultitapPort(
                    1u - TitanControllerAssignment_GetMultitapPort());
        }
        return 1;
    }

    count = TitanStorageInventory_GetKindCount(TitanShellScene_GetStorageLeafKind());
    if (count > 0u)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_UP)
        {
            g_scene.settings_leaf_selected_row--;
            if (g_scene.settings_leaf_selected_row < 0)
                g_scene.settings_leaf_selected_row = (int)count - 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
        {
            g_scene.settings_leaf_selected_row++;
            if ((unsigned int)g_scene.settings_leaf_selected_row >= count)
                g_scene.settings_leaf_selected_row = 0;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            const TitanStorageInventoryEntry *entry = TitanStorageInventory_GetKindEntry(
                TitanShellScene_GetStorageLeafKind(),
                (unsigned int)g_scene.settings_leaf_selected_row);
            if (entry != 0)
            {
                TitanSettingsActionKind action = TITAN_SETTINGS_ACTION_STORAGE_APPLICATION;
                if (entry->kind == TITAN_STORAGE_INVENTORY_NATIVE_GAME)
                    action = TITAN_SETTINGS_ACTION_STORAGE_GAME;
                else if (entry->kind == TITAN_STORAGE_INVENTORY_THEME)
                    action = TITAN_SETTINGS_ACTION_STORAGE_THEME;
                TitanShellScene_OpenSettingsAction(action, entry->source_index);
            }
        }
    }
    return 1;
}

static void TitanShellScene_DrawSettingsPage(
    float offset_x,
    unsigned char alpha
)
{
    float depth;
    float leaf_depth;
    unsigned char settings_alpha;
    unsigned char root_alpha;
    unsigned char subpage_alpha;
    unsigned char leaf_alpha;
    float settings_offset;
    float root_offset;
    float subpage_offset;
    float leaf_offset;

    leaf_depth = g_scene.package_installer_open
        ? TitanAnimation_SmoothStep(TitanShellScene_Clamp(g_scene.package_installer_transition,0.0f,1.0f))
        : (g_scene.system_update_open
            ? TitanAnimation_SmoothStep(TitanShellScene_Clamp(g_scene.system_update_transition,0.0f,1.0f))
            : (g_scene.network_page_mode != TITAN_NETWORK_PAGE_CLOSED
                ? TitanAnimation_SmoothStep(TitanShellScene_Clamp(g_scene.network_page_transition,0.0f,1.0f))
                : ((g_scene.settings_leaf_mode != TITAN_SETTINGS_LEAF_CLOSED || g_scene.settings_leaf_transition > 0.001f)
                    ? TitanAnimation_SmoothStep(TitanShellScene_Clamp(g_scene.settings_leaf_transition,0.0f,1.0f))
                    : 0.0f)));
    settings_alpha = TitanShellScene_Alpha((float)alpha * (1.0f - leaf_depth));
    leaf_alpha = TitanShellScene_Alpha((float)alpha * leaf_depth);
    settings_offset = offset_x - (leaf_depth * 96.0f);
    leaf_offset = offset_x + ((1.0f - leaf_depth) * 96.0f);

    depth = TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(g_scene.settings_depth_transition,0.0f,1.0f));
    root_alpha = TitanShellScene_Alpha((float)settings_alpha * (1.0f - depth));
    subpage_alpha = TitanShellScene_Alpha((float)settings_alpha * depth);
    root_offset = settings_offset - (depth * 96.0f);
    subpage_offset = settings_offset + ((1.0f - depth) * 96.0f);

    if (root_alpha > 4) TitanShellScene_DrawSettingsRoot(root_offset,root_alpha);
    if (subpage_alpha > 4) TitanShellScene_DrawSettingsSubpage(subpage_offset,subpage_alpha);

    if (leaf_alpha > 4)
    {
        if (g_scene.package_installer_open)
            TitanShellScene_DrawPackageInstaller(leaf_offset,leaf_alpha);
        else if (g_scene.system_update_open)
            TitanShellScene_DrawSystemUpdatePage(leaf_offset,leaf_alpha);
        else if (g_scene.network_page_mode != TITAN_NETWORK_PAGE_CLOSED)
            TitanShellScene_DrawNetworkPage(leaf_offset,leaf_alpha);
        else if (g_scene.settings_leaf_mode != TITAN_SETTINGS_LEAF_CLOSED || g_scene.settings_leaf_transition > 0.001f)
            TitanShellScene_DrawSettingsLeaf(leaf_offset,leaf_alpha);
    }

    if (g_scene.settings_action_open)
        TitanShellScene_DrawSettingsActionModal(alpha);
}

static int TitanShellScene_ExportDebugReport(void)
{
    const TitanSystemInfo *system_info;
    const TitanLibraryScanStatus *library_status;
    const TitanPackageInstallerStatus *package_status;
    char directory[TITAN_APPLICATION_PATH_CAPACITY];
    char target[TITAN_APPLICATION_PATH_CAPACITY];
    char temporary[TITAN_APPLICATION_PATH_CAPACITY];
    char backup[TITAN_APPLICATION_PATH_CAPACITY];
    char report[8192];
    TitanPikaVrTelemetry pikavr;
    TitanVlombisTelemetry vlombis;
    TitanThemeV4Telemetry theme_v4;
    TitanTargetMemoryTelemetry target_memory;
    const TitanDiagPerfSnapshot *perf;
    int descriptor;
    int length;
    int is_directory;

    system_info = TitanSystemInfo_Get();
    library_status = TitanLibraryScan_GetStatus();
    package_status = TitanPackageInstaller_GetStatus();
    TitanVideo_GetPikaVrTelemetry(&pikavr);
    TitanVlombisCore_GetTelemetry(&vlombis);
    TitanShellBackground_GetThemeV4Telemetry(&theme_v4);
    TitanTargetMemory_Capture();
    TitanTargetMemory_GetTelemetry(&target_memory);
    perf = TitanDiagnostics_GetPerfSnapshot();
    snprintf(directory, sizeof(directory), "%s/debug", TITAN_STORAGE_DEVICE_ROOT);
    snprintf(target, sizeof(target), "%s/titan-debug.txt", directory);
    snprintf(temporary, sizeof(temporary), "%s/titan-debug.tmp", directory);
    snprintf(backup, sizeof(backup), "%s/titan-debug.bak", directory);

    if (TitanStorageIo_PathInfo64(
        TITAN_STORAGE_DEVICE_ROOT,
        &is_directory,
        0
    ))
    {
        if (!is_directory)
        {
            return 0;
        }
    }
    else if (TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT) < 0)
    {
        return 0;
    }

    if (TitanStorageIo_PathInfo64(directory, &is_directory, 0))
    {
        if (!is_directory)
        {
            return 0;
        }
    }
    else if (TitanStorageIo_CreateDirectory(directory) < 0)
    {
        return 0;
    }

    length = snprintf(
        report,
        sizeof(report),
        "Project Titan Magenta Edition Debug Report\n"
        "Kernel: %s\n"
        "Recovery build: %s\n"
        "UI: %s\n"
        "Bootloader: %s\n"
        "Boot path: %s\n"
        "Console model: %s\n"
        "BIOS: %s\n"
        "Video: %s / %s\n"
        "DEX: %s\n"
        "Performance Overlay: %s\n"
        "EE load: %u%%\n"
        "GS load: %u%%\n"
        "RAM used: %u KiB\n"
        "EE RAM estimate: valid=%u used=%u free=%u peak=%u min_free=%u alloc_fail=%u\n"
        "IOP RAM: valid=%u used=%u free=%u min_free=%u known_modules=%u\n"
        "Boot recovery: %s reason=%s failures=%u home_ready=%u\n"
        "IP: %s\n"
        "Network bootstrap: %s / complete=%u\n"
        "Games: %u\n"
        "Applications: %u\n"
        "Packages visible: %u\n"
        "Library phase: %u / %s\n"
        "Package phase: %u / %s\n"
        "720p path: %s\n"
        "Internal resolution: %ux%u\n"
        "Output resolution: %ux%u\n"
        "pikaVR: %s api=%u optimized=%u\n"
        "pikaVR VRAM: total=%u used=%u free=%u peak=%u saved=%u reserved=%u\n"
        "pikaVR textures: budget=%u used=%u resident=%u evicted=%u dedup=%u palettized=%u clut=%u\n"
        "pikaVR cache/render: tex=%u/%u sampler=%u/%u draws=%u prim=%u path3_qw=%u ribbon_path1_qw=%u\n"
        "VlombisCore: %s api=%u caps=0x%03X active_domains=%u/10 measured=%u pressure=%u\n"
        "Vlombis storage: handoff=%u background_preempt=%u foreground=%u storage_arb=%u\n"
        "Vlombis renderer/package: state_cache_hits=%u package_quanta=%u package_bytes=%u pika_opt=%u\n"
        "Vlombis library: cache_hits=%u scan_steps=%u metadata_reused=%u avoided_bytes=%u\n"
        "Vlombis qos: audio_refills=%u network_ticks=%u frame_hit=%u frame_miss=%u last_frame_us=%u budget_us=%u\n"
        "VRAM allocator used/free: %u / %u\n"
        "Reserved headroom: %u\n"
        "Texture residency: %u (%u bytes)\n"
        "Theme resident frames: %u\n"
        "Theme v4: active=%u visible=%u animated=%u static=%u draws=%u degrade=%u\n"
        "Theme v4 resources: ee_declared=%u vram_scratch=%u uploads=%u upload_bytes=%u clut=%u\n"
        "Theme v4 perf: update_us=%u render_us=%u pika_hits=%u pika_misses=%u evictions=%u rare_events=%u\n"
        "Upload/cache: hits=%u misses=%u palettized=%u dedup=%u\n"
        "Frame time: %u us p95=%u us max=%u us\n"
        "Scale mode: %s\n",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_RECOVERY_BUILD_STRING,
        TITAN_UI_VERSION_STRING,
        system_info->bootloader_version,
        system_info->boot_path,
        system_info->console_model,
        system_info->bios_version,
        TitanVideoOutput_GetConfiguredOutputName(),
        TitanVideoOutput_IsShellProgressive() ? "720p" : "interlaced",
        TitanDex_GetStateName(),
        TitanDex_GetOverlayStateName(),
        TitanDex_GetEeLoadPercent(),
        TitanDex_GetGsLoadPercent(),
        TitanDex_GetRamUsedKilobytes(),
        target_memory.ee_valid, target_memory.ee_used_estimate, target_memory.ee_free_estimate,
        target_memory.ee_peak_used, target_memory.ee_min_free, target_memory.ee_allocation_failures,
        target_memory.iop_ram_valid, target_memory.iop_used, target_memory.iop_free,
        target_memory.iop_min_free, target_memory.iop_modules_known_resident,
        TitanBootRecovery_GetModeName(), TitanBootRecovery_GetReasonName(),
        TitanBootRecovery_GetState()->failure_count, TitanBootRecovery_GetState()->home_ready,
        TitanDex_GetIpAddress(),
        TitanNetwork_GetBootstrapStageName(),
        (unsigned int)TitanNetwork_IsBootstrapComplete(),
        TitanNeutrino_GetGameCount(),
        TitanApplications_GetCount(),
        TitanPackageInstaller_GetCount(),
        library_status != 0 ? (unsigned int)library_status->phase : 0u,
        library_status != 0 ? library_status->message : "unavailable",
        package_status != 0 ? (unsigned int)package_status->phase : 0u,
        package_status != 0 ? package_status->message : "unavailable",
        TitanVideo_Get720pPath() == TITAN_VIDEO_720P_PATH_OPTIMIZED ? "OPTIMIZED_960x540" : "REFERENCE",
        TitanVideo_GetInternalWidth(), TitanVideo_GetInternalHeight(),
        TitanVideo_GetOutputWidth(), TitanVideo_GetOutputHeight(),
        pikavr.vram_total != 0u ? "ACTIVE" : "OFFLINE", TITAN_PIKAVR_API_VERSION,
        (unsigned int)TitanPikaVr_IsOptimizedPathActive(),
        pikavr.vram_total, pikavr.vram_used, pikavr.vram_free, pikavr.vram_peak,
        pikavr.vram_saved, pikavr.vram_reserved,
        pikavr.texture_budget, pikavr.texture_used, pikavr.textures_resident,
        pikavr.textures_evicted, pikavr.textures_deduplicated, pikavr.textures_palettized,
        pikavr.clut_bytes,
        pikavr.cache_hits, pikavr.cache_misses, pikavr.sampler_state_hits,
        pikavr.sampler_state_misses, pikavr.draw_calls, pikavr.primitives,
        pikavr.path3_qwords, pikavr.ribbon_path1_qwords,
        vlombis.measured_events != 0u ? "ACTIVE" : "READY", TITAN_VLOMBISCORE_API_VERSION,
        vlombis.capabilities_active, TitanVlombisCore_GetActiveCapabilityCount(),
        vlombis.measured_events, vlombis.pressure_level,
        vlombis.exclusive_handoffs, vlombis.background_preemptions,
        vlombis.foreground_jobs, vlombis.storage_arbitrations,
        vlombis.render_state_cache_hits, vlombis.async_package_quanta,
        vlombis.async_package_bytes, vlombis.pikavr_optimizations,
        vlombis.library_cache_hits, vlombis.library_scan_steps,
        vlombis.library_metadata_reuses, vlombis.bytes_avoided,
        vlombis.audio_buffer_refills, vlombis.network_background_ticks,
        vlombis.frame_budget_hits, vlombis.frame_budget_misses,
        vlombis.last_frame_us, vlombis.frame_budget_us,
        TitanVideo_GetVramBytesReserved(), TitanVideo_GetVramBytesFree(), pikavr.vram_reserved,
        pikavr.textures_resident, pikavr.texture_used,
        TitanVramBootGuard_GetThemeResidentFrames(),
        theme_v4.active_layers, theme_v4.visible_layers, theme_v4.animated_layers,
        theme_v4.static_layers, theme_v4.draw_count, theme_v4.degradation_state,
        theme_v4.declared_ee_ram, theme_v4.theme_vram, theme_v4.texture_uploads,
        theme_v4.upload_bytes, theme_v4.clut_usage,
        theme_v4.animation_update_us, theme_v4.theme_render_us, theme_v4.pikavr_hits,
        theme_v4.pikavr_misses, theme_v4.evictions, theme_v4.rare_events_active,
        pikavr.cache_hits, pikavr.cache_misses, pikavr.textures_palettized, pikavr.textures_deduplicated,
        perf != 0 ? perf->frame_us : 0u,
        perf != 0 ? perf->frame_p95_us : 0u,
        perf != 0 ? perf->frame_worst_us : 0u,
        TitanVideo_Get720pPath() == TITAN_VIDEO_720P_PATH_OPTIMIZED ? "LINEAR half-texel/clamped" : "REFERENCE"
    );
    if (length >= 0 && (unsigned int)length < sizeof(report))
    {
        unsigned int history_index;
        int appended = snprintf(report + length, sizeof(report) - (unsigned int)length, "\nBOOT VRAM HISTORY\n");
        if (appended < 0 || (unsigned int)appended >= sizeof(report) - (unsigned int)length) return 0;
        length += appended;
        for (history_index = 0u; history_index < TitanVramBootGuard_GetHistoryCount(); history_index++)
        {
            const TitanVramBootSnapshot *snapshot = TitanVramBootGuard_GetHistory(history_index);
            if (snapshot == 0) continue;
            appended = snprintf(report + length, sizeof(report) - (unsigned int)length,
                "%s vram_used=%u vram_free=%u reserved=%u font=%u textures=%u theme=%u fails=%u pressure=%u ee_valid=%u ee_used=%u ee_free=%u ee_peak=%u ee_min_free=%u iop_valid=%u iop_used=%u iop_free=%u iop_modules=%u\n",
                TitanVramBootGuard_PhaseName(snapshot->phase), snapshot->vram_used, snapshot->vram_free, snapshot->vram_reserved, snapshot->vram_font, snapshot->vram_textures, snapshot->vram_theme, snapshot->allocation_failures, (unsigned int)snapshot->pressure, snapshot->ee_ram_valid, snapshot->ee_used, snapshot->ee_free, snapshot->ee_peak, snapshot->ee_min_free, snapshot->iop_ram_valid, snapshot->iop_used, snapshot->iop_free, snapshot->iop_modules_known);
            if (appended < 0 || (unsigned int)appended >= sizeof(report) - (unsigned int)length) return 0;
            length += appended;
        }
    }

    if (length < 0 || (unsigned int)length >= sizeof(report))
    {
        return 0;
    }

    TitanStorageIo_Remove(temporary);
    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE |
            TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        return 0;
    }
    if (
        !TitanStorageIo_WriteExact(descriptor, report, (unsigned int)length) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(temporary);
        return 0;
    }
    return TitanStorageIo_AtomicReplace(temporary, target, backup);
}

static int TitanShellScene_OpenWifiPasswordInput(void);

static int TitanShellScene_ValidateWifiSsid(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    unsigned int length;
    (void)user_data;
    length = text != 0 ? (unsigned int)strlen(text) : 0u;
    if (length < 1u || length > 32u)
    {
        TitanShellScene_SetTextInputMessage(
            message,
            message_capacity,
            "O SSID deve ter entre 1 e 32 caracteres."
        );
        return 0;
    }
    return 1;
}

static void TitanShellScene_ConfirmWifiSsid(const char *text, void *user_data)
{
    (void)user_data;
    if (TitanNetwork_SetWifiSsid(text))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Wi-Fi",
            "Network selected; enter the WPA2 password."
        );
        (void)TitanShellScene_OpenWifiPasswordInput();
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Wi-Fi",
            "Could not save the SSID"
        );
    }
}

static int TitanShellScene_OpenWifiSsidInput(void)
{
    TitanTextInputRequest request;
    const TitanNetworkStatus *network = TitanNetwork_GetStatus();
    request.title = TitanLocalization_Compat("Wi-Fi Network");
    request.initial_text = network != 0 ? network->ssid : "";
    request.placeholder = TitanLocalization_Compat("SSID 2.4 GHz");
    request.maximum_codepoints = 32u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = TitanShellScene_ValidateWifiSsid;
    request.confirm_callback = TitanShellScene_ConfirmWifiSsid;
    request.cancel_callback = 0;
    request.user_data = 0;
    return TitanTextInput_Open(&request);
}

static int TitanShellScene_ValidateWifiPassword(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    unsigned int length;
    (void)user_data;
    length = text != 0 ? (unsigned int)strlen(text) : 0u;
    if (length < 8u || length > 63u)
    {
        TitanShellScene_SetTextInputMessage(
            message,
            message_capacity,
            "A senha WPA2 deve ter entre 8 e 63 caracteres."
        );
        return 0;
    }
    return 1;
}

static void TitanShellScene_ConfirmWifiPassword(const char *text, void *user_data)
{
    (void)user_data;
    if (!TitanNetwork_SetWifiPassword(text))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Wi-Fi",
            "Could not save the Wi-Fi credential to Titan storage"
        );
        return;
    }
    if (TitanNetwork_ConnectWifi())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ACTIVITY,
            "Wi-Fi",
            "Conectando com o driver AR9271 TT1426"
        );
        (void)TitanNetwork_StartConnectionTest();
        g_scene.network_page_mode = TITAN_NETWORK_PAGE_TEST;
        g_scene.network_page_selected_row = 0;
        g_scene.network_page_row_position = 0.0f;
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Wi-Fi",
            "Credential saved; AR9271 service is not ready yet"
        );
    }
}

static int TitanShellScene_OpenWifiPasswordInput(void)
{
    TitanTextInputRequest request;
    request.title = TitanLocalization_Compat("Senha Wi-Fi");
    request.initial_text = "";
    request.placeholder = TitanLocalization_Compat("Senha WPA2");
    request.maximum_codepoints = 63u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_PASSWORD;
    request.validation_callback = TitanShellScene_ValidateWifiPassword;
    request.confirm_callback = TitanShellScene_ConfirmWifiPassword;
    request.cancel_callback = 0;
    request.user_data = 0;
    return TitanTextInput_Open(&request);
}

static void TitanShellScene_OpenNetworkPage(TitanNetworkPageMode mode)
{
    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_CLOSED)
        g_scene.network_page_transition = 0.0f;
    if (mode == TITAN_NETWORK_PAGE_GAME_DATA ||
        mode == TITAN_NETWORK_PAGE_ASSET_WIZARD)
        g_scene.asset_wizard_return_mode = mode;
    if (mode != TITAN_NETWORK_PAGE_ASSET_PROGRESS)
        g_scene.asset_wizard_progress_is_clear_cache = 0;
    g_scene.network_page_mode = mode;
    g_scene.network_page_selected_row = 0;
    g_scene.network_page_row_position = 0.0f;
}

static void TitanShellScene_RunNetworkAction(int row)
{
    if (row == 0)
    {
        TitanNetwork_SetEnabled(!TitanNetwork_IsEnabled());
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Network",
            TitanNetwork_IsEnabled() ? "Internet connection enabled" : "Internet connection disabled"
        );
    }
    else if (row == 1)
    {
        if (!TitanNetwork_IsEnabled()) TitanNetwork_SetEnabled(1);
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_SETUP_METHOD);
    }
    else if (row == 2)
    {
        (void)TitanNetwork_StartConnectionTest();
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_TEST);
    }
    else if (row == 3)
    {
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_STATUS);
    }
    else if (row == 4)
    {
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_SERVICES);
    }
    else if (row == 5)
    {
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_GAME_DATA);
    }
}

static int TitanShellScene_UpdateNetworkPage(const TitanInputState *input)
{
    int row_count;
    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_CLOSED) return 0;
    if (input == 0) return 1;

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_PROGRESS)
        {
            if (TitanAssetService_IsManualOperationActive())
            {
                TitanAssetService_CancelManualOperation();
                TitanNotifications_Push(TITAN_NOTIFICATION_INFO,"Assets","Operação de assets cancelada");
            }
            TitanShellScene_OpenNetworkPage(
                g_scene.asset_wizard_return_mode == TITAN_NETWORK_PAGE_ASSET_WIZARD
                    ? TITAN_NETWORK_PAGE_ASSET_WIZARD
                    : TITAN_NETWORK_PAGE_GAME_DATA);
            return 1;
        }
        if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_LIST)
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_WIFI_MODE);
        else if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_MODE ||
                 g_scene.network_page_mode == TITAN_NETWORK_PAGE_LAN_MODE)
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_SETUP_METHOD);
        else
            g_scene.network_page_mode = TITAN_NETWORK_PAGE_CLOSED;
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_LIST &&
        (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE))
    {
        if (!TitanNetwork_RequestWifiScan())
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_WARNING,"Wi-Fi",
                "The scan cannot start yet; wait for the AR9271 adapter"
            );
        }
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_PROGRESS)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,"Assets",
                TitanAssetService_IsManualOperationActive()
                    ? "Operação de assets continua em segundo plano"
                    : "Operação de assets concluída"
            );
            TitanShellScene_OpenNetworkPage(
                g_scene.asset_wizard_return_mode == TITAN_NETWORK_PAGE_ASSET_WIZARD
                    ? TITAN_NETWORK_PAGE_ASSET_WIZARD
                    : TITAN_NETWORK_PAGE_GAME_DATA);
        }
        return 1;
    }

    row_count = TitanShellScene_GetNetworkPageRowCount();
    if (row_count > 0 && (input->pressed & TITAN_INPUT_BUTTON_UP))
    {
        g_scene.network_page_selected_row--;
        if (g_scene.network_page_selected_row < 0)
            g_scene.network_page_selected_row = row_count - 1;
    }
    if (row_count > 0 && (input->pressed & TITAN_INPUT_BUTTON_DOWN))
    {
        g_scene.network_page_selected_row++;
        if (g_scene.network_page_selected_row >= row_count)
            g_scene.network_page_selected_row = 0;
    }

    if (!(input->pressed & TITAN_INPUT_BUTTON_CROSS)) return 1;

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_WIZARD)
    {
        const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
        unsigned int selected_game = assets != 0 ? assets->selected_game_index : 0xffffffffu;
        int started = 0;
        if (g_scene.network_page_selected_row == 3)
        {
            if (TitanAssetService_IsManualOperationActive())
            {
                started = 1;
                g_scene.asset_wizard_progress_is_clear_cache =
                    assets != 0 && assets->manual_clear_cache;
            }
            else
            {
                started = TitanAssetService_StartClearCache();
                g_scene.asset_wizard_progress_is_clear_cache = started;
            }
        }
        else if (assets != 0 && assets->artwork_wizard_active)
        {
            g_scene.asset_wizard_progress_is_clear_cache = 0;
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_PROGRESS);
            return 1;
        }
        else
        {
            g_scene.asset_wizard_progress_is_clear_cache = 0;
            int all_games = g_scene.network_page_selected_row == 2;
            TitanAssetKind kind = g_scene.network_page_selected_row == 0
                ? TITAN_ASSET_KIND_ICON
                : (g_scene.network_page_selected_row == 1
                    ? TITAN_ASSET_KIND_BACKGROUND : 0);
            started = TitanAssetService_StartArtworkWizard(
                all_games, selected_game, kind);
        }
        if (!started)
        {
            TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,"Asset Wizard",
                selected_game < TitanNeutrino_GetGameCount()
                    ? "Could not start conversion; another asset operation may be active"
                    : "Select a game in the library before converting its artwork");
            return 1;
        }
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_PROGRESS);
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_GAME_DATA)
    {
        int started = 0;
        if (g_scene.network_page_selected_row == 0)
        {
            if (TitanAssetService_IsManualOperationActive())
                started = 1;
            else
                started = TitanAssetService_StartManualDownload(0);
            if (!started)
            {
                TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,"Assets",
                    TitanNetwork_GetStatus()->state == TITAN_NETWORK_ONLINE
                        ? TitanAssetService_GetStatus()->detail
                        : "Connect Titan to the Internet before downloading assets");
                return 1;
            }
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_PROGRESS);
        }
        else if (g_scene.network_page_selected_row == 1)
        {
            if (TitanAssetService_IsManualOperationActive())
                started = 1;
            else
                started = TitanAssetService_StartManualDownload(1);
            if (!started)
            {
                TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,"Assets",
                    TitanNetwork_GetStatus()->state == TITAN_NETWORK_ONLINE
                        ? TitanAssetService_GetStatus()->detail
                        : "Connect Titan to the Internet before updating assets");
                return 1;
            }
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_PROGRESS);
        }
        else if (g_scene.network_page_selected_row == 2)
        {
            if (!TitanAssetService_IsManualOperationActive())
            {
                if (!TitanAssetService_StartClearCache())
                {
                    TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,"Assets","Could not start cache cleanup");
                    return 1;
                }
                g_scene.asset_wizard_progress_is_clear_cache = 1;
            }
            else
            {
                const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
                g_scene.asset_wizard_progress_is_clear_cache =
                    assets != 0 && assets->manual_clear_cache;
            }
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_PROGRESS);
        }
        else if (g_scene.network_page_selected_row == 3)
        {
            TitanAssetService_SetAutomaticDownloadEnabled(!TitanAssetService_IsAutomaticDownloadEnabled());
            TitanNotifications_Push(TITAN_NOTIFICATION_INFO,"Assets",
                TitanAssetService_IsAutomaticDownloadEnabled()
                    ? "Automatic download enabled"
                    : "Automatic download disabled");
        }
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SETUP_METHOD)
    {
        if (g_scene.network_page_selected_row == 0)
        {
            TitanNetwork_RequestPreference(TITAN_NETWORK_PREFER_WIFI);
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_WIFI_MODE);
        }
        else
        {
            TitanNetwork_RequestPreference(TITAN_NETWORK_PREFER_ETHERNET);
            TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_LAN_MODE);
        }
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_MODE)
    {
        g_scene.network_setup_custom = g_scene.network_page_selected_row == 1;
        TitanNetwork_RequestPreference(TITAN_NETWORK_PREFER_WIFI);
        (void)TitanNetwork_RequestWifiScan();
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_WIFI_LIST);
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_LAN_MODE)
    {
        g_scene.network_setup_custom = g_scene.network_page_selected_row == 1;
        TitanNetwork_RequestPreference(TITAN_NETWORK_PREFER_ETHERNET);
        (void)TitanNetwork_StartConnectionTest();
        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_TEST);
        return 1;
    }

    if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_WIFI_LIST)
    {
        unsigned int count = TitanNetwork_GetWifiNetworkCount();
        unsigned int selected = g_scene.network_page_selected_row < 0
            ? 0u : (unsigned int)g_scene.network_page_selected_row;
        if (selected < count)
        {
            const TitanNetworkWifiAp *ap = TitanNetwork_GetWifiNetwork(selected);
            if (ap == 0) return 1;
            if (ap->security != TITAN_NETWORK_WIFI_WPA2)
            {
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_WARNING,"Wi-Fi",
                    "Esta integração usa o caminho WPA2-PSK hardware-proven do TT1426"
                );
                return 1;
            }
            if (!TitanNetwork_SetWifiSsid(ap->ssid))
            {
                TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Wi-Fi","Could not select the network");
                return 1;
            }
            (void)TitanShellScene_OpenWifiPasswordInput();
        }
        else
        {
            (void)TitanShellScene_OpenWifiSsidInput();
        }
        return 1;
    }

    return 1;
}

static void TitanShellScene_OpenPackageInstaller(void)
{
    if (!TitanDex_IsEnabled())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Package Installer",
            "Available only when DEX Mode is enabled"
        );
        return;
    }
    (void)TitanPackageInstaller_Refresh();
    g_scene.package_installer_theme_only = 0;
    g_scene.package_installer_open = 1;
    g_scene.package_installer_detail_open = 0;
    g_scene.package_installer_selected_row = 0;
    g_scene.package_installer_row_position = 0.0f;
}

static void TitanShellScene_OpenThemeInstaller(void)
{
    (void)TitanPackageInstaller_RefreshThemes();
    g_scene.package_installer_theme_only = 1;
    g_scene.package_installer_open = 1;
    g_scene.package_installer_detail_open = 0;
    g_scene.package_installer_selected_row = 0;
    g_scene.package_installer_row_position = 0.0f;
}

static void TitanShellScene_ResumeMusicAfterExclusiveStorage(void)
{
    if (g_scene.package_music_suspended)
    {
        TitanAudio_StartMusic();
        g_scene.package_music_suspended = 0;
    }
}

static void TitanShellScene_LeavePackageInstallerExclusiveMode(void)
{
    TitanStorageScheduler_SetPackageExclusiveMode(0);
    TitanShellScene_ResumeMusicAfterExclusiveStorage();

    /* Publishing an app while a previous catalog pass was paused can make
     * that pass stale. Queue one forced refresh only after the user leaves the
     * installer, so installation itself remains a storage-exclusive mode. */
    if (g_scene.package_scan_deferred)
    {
        (void)TitanStorageScheduler_RequestLibraryScan(1);
        g_scene.package_scan_deferred = 0;
    }
}

static void TitanShellScene_PollPackageInstaller(void)
{
    const TitanPackageInstallerStatus *status;

    if (!g_scene.package_install_tracking || TitanPackageInstaller_IsActive())
    {
        return;
    }

    g_scene.package_install_tracking = 0;
    TitanShellBusy_End();
    /* Each completed package releases the single-owner FileXio lane
     * immediately. BF19 kept package-exclusive mode until leaving the whole
     * installer, which made a second install in the same session inherit a
     * stale storage critical section on real hardware. */
    TitanStorageScheduler_SetPackageExclusiveMode(0);
    TitanShellScene_ResumeMusicAfterExclusiveStorage();
    status = TitanPackageInstaller_GetStatus();
    if (status != 0 && status->result == TITAN_PACKAGE_INSTALLER_OK)
    {
        const TitanPackageInfo *package = TitanPackageInstaller_Get(
            (unsigned int)g_scene.package_installer_selected_row
        );
        if (package != 0 && package->app_type == TITAN_PACKAGE_APP_THEME)
        {
            (void)TitanTheme_Refresh();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Package Installer",
                "Theme installed; select it in Settings > Themes"
            );
        }
        else
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Package Installer",
                "Application installed; catalog updated without a full rescan"
            );
        }
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Package Installer",
            status != 0
                ? TitanPackageInstaller_GetResultName(status->result)
                : "Falha de instalacao desconhecida"
        );
    }
}


static void TitanShellScene_OpenSystemUpdate(void)
{
    (void)TitanSystemUpdate_Refresh();
    g_scene.system_update_open = 1;
    g_scene.system_update_selected_row = 0;
    g_scene.system_update_row_position = 0.0f;
    g_scene.system_update_transition = 0.0f;
    g_scene.network_page_row_position = 0.0f;
    g_scene.network_page_transition = 0.0f;
}

static int TitanShellScene_QuiesceStorageForPower(void)
{
    unsigned int attempt;

    /* Native apps release their app-exclusive FileXio lane and owned media
     * before the system acquires the global package/power lane. */
    TitanAppRuntime_QuiesceForSystemShutdown();

    /* Re-use the already proven single-owner storage lane.  A power transition
     * must never race the scanner, VMC work, audio prefetch or another FileXio
     * user while files are being committed. */
    TitanStorageScheduler_SetPackageExclusiveMode(1);
    for (attempt = 0u; attempt < TITAN_POWER_QUIESCE_MAX_ATTEMPTS; attempt++)
    {
        if (TitanStorageScheduler_IsPackageWorkerParked())
        {
            return 1;
        }
        DelayThread(TITAN_POWER_QUIESCE_DELAY_US);
    }

    TitanStorageScheduler_SetPackageExclusiveMode(0);
    return 0;
}

static int TitanShellScene_InitializePoweroffServer(void)
{
#if defined(__EE__) || defined(_EE)
    int module_id;
    int module_result = -1;

    /* The EE libpwroff client only talks to PWROFF_IRX; linking -lpwroff does
     * not put that IOP server into memory.  Ship and start the SDK IRX here so
     * software shutdown is self-contained instead of depending on a previous
     * application having left the server resident. */
    module_id = SifExecModuleBuffer(
        titan_poweroff_irx,
        size_titan_poweroff_irx,
        0,
        0,
        &module_result
    );
    if (module_id < 0 || module_result < 0)
    {
        return 0;
    }
#endif

    return poweroffInit() >= 0;
}

static void TitanShellScene_ShutdownRuntimeLayers(void)
{
    /* BF28 lifecycle invariant: in-game state must disappear before theme and
     * lower platform services are torn down. */
    TitanOverlay_Reset();
    TitanDiscEngine_Shutdown();
    TitanDiscService_Shutdown();
    TitanStoreService_Shutdown();
    TitanDownloadService_Shutdown();
    TitanSession_Shutdown();
    TitanDateTime_Shutdown();
    TitanShellBackground_Shutdown();
}

static int TitanShellScene_PrepareConsolePowerTransition(void)
{
    if (!TitanShellScene_QuiesceStorageForPower())
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Power",
            "Storage is still busy; operation cancelled"
        );
        return 0;
    }

    TitanBrowser_Shutdown();
    return 1;
}

static void TitanShellScene_RestartConsoleNow(const char *reason)
{
    TitanResult iop_result;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        "Restarting console",
        reason != 0 ? reason : "Restarting PlayStation 2"
    );
    TitanShellBusy_Begin();
    TitanShellBusy_Pulse();

    if (!TitanShellScene_PrepareConsolePowerTransition())
    {
        TitanShellBusy_End();
        return;
    }

    /* Persist a clean-session marker while storage is still mounted. */
    TitanDiagnostics_MarkCleanShutdown();

    /* Close EE clients before the IOP reset.  This is a console restart, not
     * Titan's hot re-exec action: the IOP is reset/synchronised and control is
     * returned to ROM OSDSYS, reproducing the software boot boundary. */
    TitanShellScene_ShutdownRuntimeLayers();
    TitanAudio_Shutdown();
    TitanInput_Shutdown();
    TitanIopServiceManager_Shutdown();
    iop_result = TitanIopServiceManager_Initialize(0, 0);
    (void)iop_result;

    LoadExecPS2("rom0:OSDSYS", 0, 0);

    /* LoadExecPS2 is noreturn on hardware.  This path exists only so host
     * stubs/emulators report a visible failure instead of silently continuing. */
    TitanShellBusy_End();
    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        "Restart failed",
        "OSDSYS returned unexpectedly"
    );
}

static void TitanShellScene_PollSystemUpdate(void)
{
    const TitanSystemUpdateStatus *status;

    if (!g_scene.system_update_tracking || TitanSystemUpdate_IsActive())
    {
        return;
    }

    g_scene.system_update_tracking = 0;
    g_scene.network_page_mode = TITAN_NETWORK_PAGE_CLOSED;
    g_scene.network_page_selected_row = 0;
    g_scene.network_setup_custom = 0;
    TitanShellBusy_End();
    status = TitanSystemUpdate_GetStatus();

    if (status != 0 && status->result == TITAN_SYSTEM_UPDATE_OK)
    {
        /* Keep the storage lane exclusive.  Releasing it for even one frame
         * after a successful publish could let a scanner/prefetch start while
         * the console is crossing into reboot. */
        TitanShellScene_RestartConsoleNow(
            "PUP update complete; automatic reboot"
        );
        return;
    }

    /* Failure/cancel paths are the only ones that return to the running shell. */
    TitanStorageScheduler_SetPackageExclusiveMode(0);
    TitanShellScene_ResumeMusicAfterExclusiveStorage();
    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        "System Software Update",
        status != 0
            ? TitanSystemUpdate_GetResultName(status->result)
            : "Unknown failure"
    );
}

static int TitanShellScene_UpdateSystemUpdate(const TitanInputState *input)
{
    unsigned int count;
    TitanShellScene_PollSystemUpdate();
    if (!g_scene.system_update_open) return 0;
    if (input == 0) return 1;
    count = TitanSystemUpdate_GetCount();

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        if (TitanSystemUpdate_IsActive())
            TitanSystemUpdate_Cancel();
        else
            g_scene.system_update_open = 0;
        return 1;
    }
    if (TitanSystemUpdate_IsActive()) return 1;
    if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
    {
        count = TitanSystemUpdate_Refresh();
        g_scene.system_update_selected_row = 0;
        g_scene.system_update_row_position = 0.0f;
        TitanNotifications_Push(TITAN_NOTIFICATION_INFO,
            "System Software Update",
            count ? "PUP packages refreshed" : "No compatible .pup package found");
        return 1;
    }
    if (count > 0u && (input->pressed & TITAN_INPUT_BUTTON_UP))
    {
        g_scene.system_update_selected_row--;
        if (g_scene.system_update_selected_row < 0) g_scene.system_update_selected_row=(int)count-1;
    }
    if (count > 0u && (input->pressed & TITAN_INPUT_BUTTON_DOWN))
    {
        g_scene.system_update_selected_row++;
        if ((unsigned int)g_scene.system_update_selected_row >= count) g_scene.system_update_selected_row=0;
    }
    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        TitanSystemUpdateResult result;
        if (count == 0u)
        {
            TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,
                "System Software Update", "No .pup package found");
            return 1;
        }
        result = TitanSystemUpdate_Begin((unsigned int)g_scene.system_update_selected_row);
        if (result == TITAN_SYSTEM_UPDATE_OK)
        {
            TitanAudio_StopMusic();
            TitanStorageScheduler_SetPackageExclusiveMode(1);
            g_scene.package_music_suspended = 1;
            g_scene.system_update_tracking = 1;
            TitanShellBusy_Begin();
            TitanShellBusy_Pulse();
        }
        else
        {
            TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,
                "System Software Update", TitanSystemUpdate_GetResultName(result));
        }
    }
    return 1;
}

static void TitanShellScene_RunDebugSettingsAction(int row)
{
    if (!TitanDex_IsEnabled())
    {
        return;
    }

    if (row == 0)
    {
        TitanShellScene_OpenPackageInstaller();
    }
    else if (row == 1)
    {
        TitanShellScene_ToggleDexMode();
    }
    else if (row == 2)
    {
        int requested;

        requested = !TitanDex_IsOverlayEnabled();
        TitanNotifications_Push(
            TitanDex_SetOverlayEnabled(requested)
                ? TITAN_NOTIFICATION_SUCCESS
                : TITAN_NOTIFICATION_ERROR,
            "Performance Overlay",
            TitanDex_IsOverlayEnabled()
                ? "DEX overlay enabled"
                : "DEX overlay disabled"
        );
    }
    else if (row == 3)
    {
        if (!TitanStorageScheduler_IsLibraryScanBusy())
        {
            (void)TitanStorageScheduler_RequestLibraryScan(1);
        }
        else
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,
                "Catalogo",
                "Library update is already in progress"
            );
        }
    }
    else if (row == 4)
    {
        if (TitanStorageScheduler_IsLibraryScanBusy())
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_WARNING,
                "Cache da biblioteca",
                "Wait for the current update to finish"
            );
        }
        else if (TitanLibraryCache_Clear())
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Cache da biblioteca",
                "Cache, temporario e backup removidos"
            );
            (void)TitanStorageScheduler_RequestLibraryScan(1);
        }
        else
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ERROR,
                "Cache da biblioteca",
                "Could not clear all files"
            );
        }
    }
    else if (row == 5)
    {
        int exported;

        exported = TitanShellScene_ExportDebugReport();
        TitanNotifications_Push(
            exported ? TITAN_NOTIFICATION_SUCCESS : TITAN_NOTIFICATION_ERROR,
            "Relatorio de debug",
            exported
                ? "Salvo em mass:/TITAN/debug/titan-debug.txt"
                : "Failed to write the report to USB"
        );
    }
    else if (row == 6)
    {
        TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_APP_HOME);
    }
    else if (row == 7)
    {
        TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_FTP_HOME);
    }
    else if (row == 8)
    {
        TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_INPUT);
    }
    else if (row == 9)
    {
        int exported = TitanDiagnostics_ExportTextNow();
        TitanNotifications_Push(
            exported ? TITAN_NOTIFICATION_SUCCESS : TITAN_NOTIFICATION_ERROR,
            "Titan Diagnostics",
            exported ? "Salvo em mass:/TITAN/logs/titan-last.txt" : "Falha ao exportar diagnostico"
        );
    }
    else if (row == 10)
    {
        int exported = TitanDiagnostics_ExportPreviousTextNow();
        TitanNotifications_Push(
            exported ? TITAN_NOTIFICATION_SUCCESS : TITAN_NOTIFICATION_ERROR,
            "Titan Diagnostics",
            exported
                ? "Boot anterior salvo em mass:/TITAN/logs/titan-previous.txt"
                : "Nenhuma sessao anterior preservada"
        );
    }    else if (row == 11)
    {
        TitanVideo720pPath next = TitanVideo_Get720pPath() == TITAN_VIDEO_720P_PATH_OPTIMIZED
            ? TITAN_VIDEO_720P_PATH_REFERENCE
            : TITAN_VIDEO_720P_PATH_OPTIMIZED;
        TitanResult result = TitanVideo_Set720pValidationPath(next);
        TitanNotifications_Push(
            result == TITAN_RESULT_OK ? TITAN_NOTIFICATION_SUCCESS : TITAN_NOTIFICATION_ERROR,
            "720p Composition",
            result == TITAN_RESULT_OK
                ? (next == TITAN_VIDEO_720P_PATH_OPTIMIZED ? "Optimized 960x540 selected" : "Reference selected")
                : "Path switch rejected; current video state preserved"
        );
    }
}

static int TitanShellScene_UpdatePackageInstaller(
    const TitanInputState *input
)
{
    unsigned int count;

    TitanShellScene_PollPackageInstaller();
    if (!g_scene.package_installer_open)
    {
        return 0;
    }
    if (!TitanDex_IsEnabled() && !g_scene.package_installer_theme_only)
    {
        if (TitanPackageInstaller_IsActive())
            TitanPackageInstaller_Cancel();
        g_scene.package_installer_open = 0;
        g_scene.package_installer_detail_open = 0;
        g_scene.package_installer_theme_only = 0;
        TitanShellScene_LeavePackageInstallerExclusiveMode();
        return 1;
    }
    if (input == 0)
    {
        return 1;
    }

    count = TitanPackageInstaller_GetCount();
    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        if (TitanPackageInstaller_IsActive())
        {
            TitanPackageInstaller_Cancel();
        }
        else if (g_scene.package_installer_detail_open)
        {
            g_scene.package_installer_detail_open = 0;
        }
        else
        {
            g_scene.package_installer_open = 0;
            g_scene.package_installer_theme_only = 0;
            TitanShellScene_LeavePackageInstallerExclusiveMode();
        }
        return 1;
    }
    if (TitanPackageInstaller_IsActive())
    {
        return 1;
    }
    if (g_scene.package_installer_detail_open)
    {
        /* Completed/error detail remains visible until Circle so the filename,
         * percentage and final result cannot disappear on the last frame. */
        return 1;
    }
    if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
    {
        if (g_scene.package_music_suspended)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,
                "Package Installer",
                "Saia do instalador para retomar o acesso ao armazenamento"
            );
            return 1;
        }
        count = g_scene.package_installer_theme_only
            ? TitanPackageInstaller_RefreshThemes()
            : TitanPackageInstaller_Refresh();
        g_scene.package_installer_selected_row = 0;
        g_scene.package_installer_row_position = 0.0f;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Package Installer",
            count > 0u ? "Lista de pacotes atualizada" : "Nenhum pacote encontrado"
        );
        return 1;
    }
    if (count > 0u && (input->pressed & TITAN_INPUT_BUTTON_UP))
    {
        g_scene.package_installer_selected_row--;
        if (g_scene.package_installer_selected_row < 0)
            g_scene.package_installer_selected_row = (int)count - 1;
    }
    if (count > 0u && (input->pressed & TITAN_INPUT_BUTTON_DOWN))
    {
        g_scene.package_installer_selected_row++;
        if ((unsigned int)g_scene.package_installer_selected_row >= count)
            g_scene.package_installer_selected_row = 0;
    }
    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        TitanPackageInstallerResult result;
        const TitanPackageInfo *package;

        package = count > 0u
            ? TitanPackageInstaller_Get((unsigned int)g_scene.package_installer_selected_row)
            : 0;
        if (package == 0)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_WARNING,
                "Package Installer",
                "Nenhum pacote selecionado"
            );
            return 1;
        }

        /* A successful click enters a dedicated storage-exclusive install
         * mode. Any background catalog pass simply pauses at its next worker
         * boundary instead of blocking installation. */
        result = TitanPackageInstaller_BeginInstall(
            (unsigned int)g_scene.package_installer_selected_row
        );
        if (result == TITAN_PACKAGE_INSTALLER_OK)
        {
            TitanAudio_StopMusic();
            TitanStorageScheduler_SetPackageExclusiveMode(1);
            g_scene.package_music_suspended = 1;
            g_scene.package_installer_detail_open = 1;
            g_scene.package_install_tracking = 1;
            TitanShellBusy_Begin();
            TitanShellBusy_Pulse();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ACTIVITY,
                "Instalando pacote",
                package->filename
            );
        }
        else
        {
            TitanNotifications_Push(
                result == TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED
                    ? TITAN_NOTIFICATION_INFO
                    : TITAN_NOTIFICATION_ERROR,
                "Package Installer",
                TitanPackageInstaller_GetResultName(result)
            );
        }
    }
    return 1;
}

static void TitanShellScene_RunPowerAction(void)
{
    const TitanSystemInfo *system_info;
    const char *boot_path;
    char *restart_argv[2];

    system_info = TitanSystemInfo_Get();
    boot_path = system_info->boot_path[0] != '\0'
        ? system_info->boot_path
        : "mass:/TITAN/titan.elf";

    if (g_scene.power_selected_row == 0)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ACTIVITY,
            "Desligando",
            "Stopping PlayStation 2 services"
        );
        TitanShellBusy_Begin();
        TitanShellBusy_Pulse();

        if (!TitanShellScene_QuiesceStorageForPower())
        {
            TitanShellBusy_End();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ERROR,
                "Shutdown failed",
                "Storage is still busy"
            );
            return;
        }
        if (!TitanShellScene_InitializePoweroffServer())
        {
            TitanStorageScheduler_SetPackageExclusiveMode(0);
            TitanShellBusy_End();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ERROR,
                "Shutdown failed",
                "PWROFF_IRX did not start"
            );
            return;
        }

        TitanBrowser_Shutdown();
        TitanDiagnostics_MarkCleanShutdown();
        TitanShellScene_ShutdownRuntimeLayers();
        TitanAudio_PlayShutdownAndWait();
        TitanAudio_Shutdown();
        TitanInput_Shutdown();
        poweroffShutdown();

        TitanShellBusy_End();
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Shutdown failed",
            "The power command returned unexpectedly"
        );
        return;
    }

    if (g_scene.power_selected_row == 1)
    {
        restart_argv[0] = (char *)boot_path;
        restart_argv[1] = 0;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ACTIVITY,
            "Restarting Project Titan",
            boot_path
        );
        TitanShellBusy_Begin();
        TitanShellBusy_Pulse();
        TitanAudio_PrepareHandoff();
        if (TitanExecHandoff_Run(1, restart_argv) == TITAN_EXEC_HANDOFF_OK)
        {
            return;
        }
        TitanAudio_CancelHandoff();
        TitanShellBusy_End();

        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Restart failed",
            "Titan handoff did not complete"
        );
        return;
    }

    TitanShellScene_RestartConsoleNow("Restart requested from Power menu");
}

static void TitanShellScene_ConfirmHideHomeShell(
    int confirmed,
    void *user_data
)
{
    unsigned int catalog_index;

    if (!confirmed)
    {
        return;
    }

    catalog_index = (unsigned int)(uintptr_t)user_data;
    if (
        TitanShellLayout_SetHomeHidden(catalog_index, 1) &&
        TitanShellLayout_Save()
    )
    {
        TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
        TitanShellScene_ClampHomeSelection();
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Tile oculto",
            "Use Restaurar padroes em Settings para recuperar o layout"
        );
    }
    else
    {
        (void)TitanShellLayout_SetHomeHidden(catalog_index, 0);
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Grade principal",
            "Could not save the change"
        );
    }
}

static void TitanShellScene_ConfirmResetShellLayout(
    int confirmed,
    void *user_data
)
{
    (void)user_data;

    if (!confirmed)
    {
        return;
    }

    if (TitanShellLayout_ResetDefaults())
    {
        g_scene.top_menu_selected = 0;
        g_scene.top_menu_selected_position = 0.0f;
        TitanShellScene_ApplyDexLayout();
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Configuracoes restauradas",
            "Ordem e visibilidade dos tiles voltaram ao padrao"
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Settings",
            "Could not restore the layout"
        );
    }
}

static void TitanShellScene_ConfirmControllerPort(
    int confirmed,
    void *user_data
)
{
    int target_port;
    char message[64];

    if (!confirmed)
    {
        return;
    }

    target_port = (int)(uintptr_t)user_data;
    if (TitanInput_SetPort(target_port))
    {
        snprintf(
            message,
            sizeof(message),
            TitanLocalization_Compat("Primary controller assigned to port %d"),
            target_port + 1
        );
        TitanNotifications_Push(
            TITAN_NOTIFICATION_SUCCESS,
            "Controller",
            message
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Controller",
            "Could not reopen the controller port"
        );
    }
}

static void TitanShellScene_BeginShellMove(void)
{
    int slot;

    if (g_scene.context_source == TITAN_CONTEXT_SOURCE_SYSTEM_MENU)
    {
        slot = TitanShellLayout_GetSystemSlot(g_scene.context_shell_catalog_index);
        if (slot < 0)
        {
            return;
        }
        g_scene.shell_move_from_system_menu = 1;
    }
    else
    {
        slot = TitanShellLayout_GetHomeSlot(g_scene.context_shell_catalog_index);
        if (slot < 0)
        {
            return;
        }
        g_scene.shell_move_from_system_menu = 0;
    }

    g_scene.shell_move_active = 1;
    g_scene.shell_move_catalog_index = g_scene.context_shell_catalog_index;
    g_scene.shell_move_original_slot = slot;
    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);

    TitanNotifications_Push(
        TITAN_NOTIFICATION_INFO,
        "Mover tile",
        "Use LEFT/RIGHT e confirme com X; O cancela"
    );
}

static void TitanShellScene_RestoreShellMove(void)
{
    int slot;

    if (!g_scene.shell_move_active || g_scene.shell_move_original_slot < 0)
    {
        return;
    }

    if (g_scene.shell_move_from_system_menu)
    {
        slot = TitanShellLayout_GetSystemSlot(g_scene.shell_move_catalog_index);
        while (slot >= 0 && slot < g_scene.shell_move_original_slot)
        {
            if (!TitanShellLayout_MoveSystem(g_scene.shell_move_catalog_index, 1))
            {
                break;
            }
            slot = TitanShellLayout_GetSystemSlot(g_scene.shell_move_catalog_index);
        }
        while (slot > g_scene.shell_move_original_slot)
        {
            if (!TitanShellLayout_MoveSystem(g_scene.shell_move_catalog_index, -1))
            {
                break;
            }
            slot = TitanShellLayout_GetSystemSlot(g_scene.shell_move_catalog_index);
        }
        if (slot >= 0)
        {
            g_scene.top_menu_selected = slot;
            g_scene.top_menu_selected_position = (float)slot;
        }
    }
    else
    {
        slot = TitanShellLayout_GetHomeSlot(g_scene.shell_move_catalog_index);
        while (slot >= 0 && slot < g_scene.shell_move_original_slot)
        {
            if (!TitanShellLayout_MoveHome(g_scene.shell_move_catalog_index, 1))
            {
                break;
            }
            slot = TitanShellLayout_GetHomeSlot(g_scene.shell_move_catalog_index);
        }
        while (slot > g_scene.shell_move_original_slot)
        {
            if (!TitanShellLayout_MoveHome(g_scene.shell_move_catalog_index, -1))
            {
                break;
            }
            slot = TitanShellLayout_GetHomeSlot(g_scene.shell_move_catalog_index);
        }
        slot = TitanShellScene_FindHomeDisplayIndexForShell(
            g_scene.shell_move_catalog_index
        );
        if (slot >= 0)
        {
            g_scene.selected_item = slot;
            g_scene.selected_position = (float)slot;
        }
    }
}

static int TitanShellScene_UpdateShellMove(const TitanInputState *input)
{
    int direction;
    int moved;
    int slot;

    if (!g_scene.shell_move_active)
    {
        return 0;
    }

    if (input == 0 || !input->connected)
    {
        return 1;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        TitanShellScene_RestoreShellMove();
        g_scene.shell_move_active = 0;
        g_scene.shell_move_original_slot = -1;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Mover tile",
            "Movimento cancelado"
        );
        return 1;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        if (TitanShellLayout_Save())
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Mover tile",
                "Nova posicao salva"
            );
        }
        else
        {
            TitanShellScene_RestoreShellMove();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_ERROR,
                "Mover tile",
                "Could not save the new position"
            );
        }
        g_scene.shell_move_active = 0;
        g_scene.shell_move_original_slot = -1;
        return 1;
    }

    direction = 0;
    if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
    {
        direction = -1;
    }
    else if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
    {
        direction = 1;
    }

    if (direction == 0)
    {
        return 1;
    }

    if (g_scene.shell_move_from_system_menu)
    {
        moved = TitanShellLayout_MoveSystem(
            g_scene.shell_move_catalog_index,
            direction
        );
        slot = TitanShellLayout_GetSystemSlot(g_scene.shell_move_catalog_index);
        if (moved && slot >= 0)
        {
            g_scene.top_menu_selected = slot;
            g_scene.top_menu_selected_position = (float)slot;
            (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_TOP_MENU_NAV);
        }
    }
    else
    {
        moved = TitanShellLayout_MoveHome(
            g_scene.shell_move_catalog_index,
            direction
        );
        slot = TitanShellScene_FindHomeDisplayIndexForShell(
            g_scene.shell_move_catalog_index
        );
        if (moved && slot >= 0)
        {
            g_scene.selected_item = slot;
            g_scene.selected_position = (float)slot;
            (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_HOME_GRID_NAV);
        }
    }

    return 1;
}

static void TitanShellScene_ShowContextInformation(void)
{
    if (g_scene.context_source == TITAN_CONTEXT_SOURCE_SYSTEM_MENU)
    {
        const TitanShellItem *item;

        item = TitanShellCatalog_GetSystem(g_scene.context_shell_catalog_index);
        if (item != 0)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,
                item->title,
                item->subtitle
            );
        }
        return;
    }

    {
        TitanHomeEntry entry;

        if (!TitanShellScene_GetHomeEntry((unsigned int)g_scene.selected_item, &entry))
        {
            return;
        }

        if (entry.type == TITAN_HOME_ENTRY_FOLDER)
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,
                "Folder information",
                entry.subtitle
            );
        }
        else
        {
            TitanNotifications_Push(
                TITAN_NOTIFICATION_INFO,
                entry.title,
                entry.subtitle
            );
        }
    }
}

static unsigned int TitanShellScene_BuildResolutionModes(
    const TitanNeutrinoGame *game,
    TitanPerGameGsmMode modes[4]
)
{
    unsigned int count = 0u;
    if (modes == 0) return 0u;
    modes[count++] = TITAN_PER_GAME_GSM_DEFAULT;
    modes[count++] = TITAN_PER_GAME_GSM_480P;
    if (game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME)
        modes[count++] = TITAN_PER_GAME_GSM_720P;
    modes[count++] = TITAN_PER_GAME_GSM_1080I;
    return count;
}

static int TitanShellScene_GetSelectedResolutionGame(
    TitanHomeEntry *selected_home,
    const TitanNeutrinoGame **game
)
{
    if (selected_home == 0 || game == 0 ||
        g_scene.context_source != TITAN_CONTEXT_SOURCE_HOME ||
        !TitanShellScene_GetHomeEntry((unsigned int)g_scene.selected_item, selected_home) ||
        selected_home->type != TITAN_HOME_ENTRY_GAME)
        return 0;
    *game = TitanNeutrino_GetGame(selected_home->game_index);
    return *game != 0 && TitanGameGsm_IsValidStartup((*game)->startup);
}

static void TitanShellScene_OpenResolutionOptions(void)
{
    TitanHomeEntry selected_home;
    const TitanNeutrinoGame *game;
    TitanPerGameGsmMode modes[4];
    TitanPerGameGsmMode current;
    unsigned int index;
    unsigned int count;

    memset(&selected_home, 0, sizeof(selected_home));
    if (!TitanShellScene_GetSelectedResolutionGame(&selected_home, &game)) return;
    count = TitanShellScene_BuildResolutionModes(game, modes);
    current = TitanGameGsm_GetMode(game->startup);
    /* A stale pre-fix Neutrino 720p profile is represented as native/default. */
    if (game->format != TITAN_NEUTRINO_GAME_USBEXTREME && current == TITAN_PER_GAME_GSM_720P)
        current = TITAN_PER_GAME_GSM_DEFAULT;
    g_scene.resolution_options_selected = 0;
    for (index = 0u; index < count; ++index)
        if (modes[index] == current) g_scene.resolution_options_selected = (int)index;
    g_scene.patch_options_open = 0;
    g_scene.resolution_options_open = 1;
}

static void TitanShellScene_OpenPatchOptions(TitanGamePatchKind kind)
{
    TitanHomeEntry selected_home;
    const TitanNeutrinoGame *game;
    TitanGamePatchResult result;
    unsigned int count;

    memset(&selected_home, 0, sizeof(selected_home));
    if (!TitanShellScene_GetSelectedResolutionGame(&selected_home, &game))
        return;

    result = TitanGamePatches_Load(game->startup, kind);
    if (result != TITAN_GAME_PATCH_OK)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            TitanLocalization_Compat(TitanGamePatches_GetKindTitle(kind)),
            TitanLocalization_Compat(TitanGamePatches_GetResultName(result))
        );
        return;
    }

    count = TitanGamePatches_GetEntryCount(kind);
    g_scene.resolution_options_open = 0;
    g_scene.patch_options_kind = kind;
    g_scene.patch_options_selected = 0;
    g_scene.patch_options_scroll = 0;
    g_scene.patch_options_open = 1;
    if (count == 0u)
        g_scene.patch_options_selected = 0;
}

static int TitanShellScene_SetSelectedGameResolution(TitanPerGameGsmMode mode)
{
    TitanHomeEntry selected_home;
    const TitanNeutrinoGame *game;
    TitanPerGameGsmMode previous;

    memset(&selected_home, 0, sizeof(selected_home));
    if (!TitanShellScene_GetSelectedResolutionGame(&selected_home, &game)) return 0;
    if (game->format != TITAN_NEUTRINO_GAME_USBEXTREME && mode == TITAN_PER_GAME_GSM_720P)
        return 0;

    previous = TitanGameGsm_GetMode(game->startup);
    if (!TitanGameGsm_SetMode(game->startup, mode)) goto fail;
    if (game->format == TITAN_NEUTRINO_GAME_USBEXTREME &&
        !TitanGameGsm_PrepareUlGoldenConfig(game->startup, mode))
    {
        (void)TitanGameGsm_SetMode(game->startup, previous);
        (void)TitanGameGsm_PrepareUlGoldenConfig(game->startup, previous);
        goto fail;
    }
    return 1;

fail:
    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        "Resolution options",
        "GSM setting could not be saved"
    );
    return 0;
}

static void TitanShellScene_RunContextAction(TitanContextAction action)
{
    TitanHomeEntry selected_home;

    memset(&selected_home, 0, sizeof(selected_home));
    if (g_scene.context_source == TITAN_CONTEXT_SOURCE_HOME)
    {
        (void)TitanShellScene_GetHomeEntry(
            (unsigned int)g_scene.selected_item,
            &selected_home
        );
    }

    switch (action)
    {
        case TITAN_CONTEXT_ACTION_OPEN:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            TitanShellScene_OpenShellPage(
                g_scene.context_source == TITAN_CONTEXT_SOURCE_SYSTEM_MENU
                    ? g_scene.context_shell_page
                    : selected_home.page
            );
            return;

        case TITAN_CONTEXT_ACTION_LAUNCH_APPLICATION:
            if (selected_home.type == TITAN_HOME_ENTRY_APPLICATION)
            {
                TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
                TitanShellScene_BeginApplicationLaunch(selected_home.application_index);
            }
            return;

        case TITAN_CONTEXT_ACTION_UNINSTALL_APPLICATION:
            if (selected_home.type == TITAN_HOME_ENTRY_APPLICATION &&
                TitanApplications_GetUninstallTarget(
                    selected_home.application_index,
                    g_scene.pending_uninstall_root,
                    sizeof(g_scene.pending_uninstall_root),
                    g_scene.pending_uninstall_title_id,
                    sizeof(g_scene.pending_uninstall_title_id),
                    g_scene.pending_uninstall_title,
                    sizeof(g_scene.pending_uninstall_title)))
            {
                (void)TitanNotifications_OpenConfirmation(
                    "Uninstall application?",
                    g_scene.pending_uninstall_title,
                    "The application and its installed local data will be removed.",
                    "Cancel",
                    "Desinstalar",
                    TitanShellScene_ConfirmUninstallApplication,
                    0
                );
            }
            else
            {
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_WARNING,
                    "Desinstalacao",
                    "This item is not an application installed by Package Installer"
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_ADD_FOLDER:
            if (selected_home.type == TITAN_HOME_ENTRY_GAME)
            {
                g_scene.folder_origin_game_index = selected_home.game_index;
                g_scene.folder_origin_shell_catalog_index = -1;
                if (TitanHomeFolders_GetCount() == 0u)
                {
                    TitanShellScene_BeginFolderEditor(
                        selected_home.game_index,
                        -1,
                        -1,
                        0
                    );
                }
                else
                {
                    g_scene.folder_destination_selected = 0;
                    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_DESTINATION);
                }
            }
            else if (
                selected_home.type == TITAN_HOME_ENTRY_SHELL &&
                !selected_home.shell_is_dex &&
                TitanShellScene_HomeShellCanFolder(selected_home.shell_catalog_index)
            )
            {
                g_scene.folder_origin_game_index = TITAN_NEUTRINO_GAME_CAPACITY;
                g_scene.folder_origin_shell_catalog_index =
                    (int)selected_home.shell_catalog_index;
                if (TitanHomeFolders_GetCount() == 0u)
                {
                    TitanShellScene_BeginFolderEditor(
                        TITAN_NEUTRINO_GAME_CAPACITY,
                        (int)selected_home.shell_catalog_index,
                        -1,
                        0
                    );
                }
                else
                {
                    g_scene.folder_destination_selected = 0;
                    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_DESTINATION);
                }
            }
            return;

        case TITAN_CONTEXT_ACTION_MOVE:
            TitanShellScene_BeginShellMove();
            return;

        case TITAN_CONTEXT_ACTION_INFO:
            TitanShellScene_ShowContextInformation();
            return;

        case TITAN_CONTEXT_ACTION_HIDE:
            (void)TitanNotifications_OpenConfirmation(
                "Ocultar tile?",
                selected_home.title,
                "O tile pode ser restaurado pelas configuracoes padrao.",
                "Cancel",
                "Ocultar",
                TitanShellScene_ConfirmHideHomeShell,
                (void *)(uintptr_t)selected_home.shell_catalog_index
            );
            return;

        case TITAN_CONTEXT_ACTION_CHEATS:
            TitanShellScene_OpenPatchOptions(TITAN_GAME_PATCH_CHEAT);
            return;

        case TITAN_CONTEXT_ACTION_GRAPHICS_ENHANCEMENTS:
            TitanShellScene_OpenPatchOptions(TITAN_GAME_PATCH_ENHANCEMENT);
            return;

        case TITAN_CONTEXT_ACTION_RESOLUTION_OPTIONS:
            TitanShellScene_OpenResolutionOptions();
            return;

        case TITAN_CONTEXT_ACTION_ENGINE:
            if (selected_home.type == TITAN_HOME_ENTRY_GAME)
            {
                const TitanNeutrinoGame *game;

                game = TitanNeutrino_GetGame(selected_home.game_index);
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_ACTIVITY,
                    game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME
                        ? "Titan UL Engine"
                        : "Titan-Neutrino Engine",
                    selected_home.title
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_EDIT_FOLDER:
            if (selected_home.type == TITAN_HOME_ENTRY_FOLDER)
            {
                TitanShellScene_BeginFolderEditor(
                    TITAN_NEUTRINO_GAME_CAPACITY,
                    -1,
                    (int)selected_home.folder_index,
                    0
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_DELETE_FOLDER:
            if (selected_home.type == TITAN_HOME_ENTRY_FOLDER)
            {
                const TitanHomeFolder *folder;

                folder = TitanHomeFolders_Get(selected_home.folder_index);
                (void)TitanNotifications_OpenConfirmation(
                    "Delete folder?",
                    folder != 0 ? folder->name : "Folder",
                    "Os itens retornarao para a grade principal.",
                    "Cancel",
                    "Excluir",
                    TitanShellScene_ConfirmDeleteFolder,
                    (void *)(uintptr_t)selected_home.folder_index
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_STORAGE_INFO:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_STORAGE);
            return;

        case TITAN_CONTEXT_ACTION_REFRESH_DEVICES:
            if (!TitanStorageScheduler_IsLibraryScanBusy())
            {
                (void)TitanStorageScheduler_RequestLibraryScan(1);
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_ACTIVITY,
                    "Storage",
                    "Atualizando dispositivos e biblioteca"
                );
            }
            else
            {
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_INFO,
                    "Storage",
                    "An update is already in progress"
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_TEST_CONTROLLER:
        case TITAN_CONTEXT_ACTION_CONTROLLER_SETTINGS:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_INPUT);
            return;

        case TITAN_CONTEXT_ACTION_REASSIGN_CONTROLLER:
        {
            int target_port;
            char line[64];

            target_port = 1 - TitanInput_GetPort();
            snprintf(
                line,
                sizeof(line),
                TitanLocalization_Compat("Move the primary controller to port %d?"),
                target_port + 1
            );
            (void)TitanNotifications_OpenConfirmation(
                "Reatribuir controle",
                line,
                "Conecte o controle na nova porta antes de confirmar.",
                "Cancel",
                "Reatribuir",
                TitanShellScene_ConfirmControllerPort,
                (void *)(uintptr_t)target_port
            );
            return;
        }

        case TITAN_CONTEXT_ACTION_MARK_NOTIFICATIONS_READ:
            TitanNotifications_DismissAllCards();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Notificacoes",
                "Todas foram marcadas como lidas"
            );
            return;

        case TITAN_CONTEXT_ACTION_CLEAR_NOTIFICATIONS:
            TitanNotifications_ClearAll();
            TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                "Notificacoes",
                "Central de notificacoes limpa"
            );
            return;

        case TITAN_CONTEXT_ACTION_NOTIFICATION_SETTINGS:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_SETTINGS);
            g_scene.settings_selected_category = TITAN_SETTINGS_SYSTEM;
            g_scene.settings_category_position = (float)TITAN_SETTINGS_SYSTEM;
            return;

        case TITAN_CONTEXT_ACTION_SWITCH_PROFILE:
            if (TitanUserProfiles_SelectNext())
            {
                const TitanUserProfile *profile;

                TitanShellScene_SaveProfileState(0, 0, 0);
                profile = TitanUserProfiles_GetActive();
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_SUCCESS,
                    "Perfil ativo",
                    profile != 0 ? profile->name : "Perfil"
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_EDIT_PROFILE:
            (void)TitanShellScene_OpenProfileRename();
            return;

        case TITAN_CONTEXT_ACTION_PROFILE_VMC:
        case TITAN_CONTEXT_ACTION_MANAGE_VMC:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            TitanShellScene_OpenShellPage(TITAN_SHELL_PAGE_SAVED_DATA);
            return;

        case TITAN_CONTEXT_ACTION_CREATE_VMC:
        case TITAN_CONTEXT_ACTION_VERIFY_VMC:
            TitanShellScene_VerifyActiveVmc();
            return;

        case TITAN_CONTEXT_ACTION_RESET_SETTINGS:
            (void)TitanNotifications_OpenConfirmation(
                "Restaurar padroes?",
                "Tile order and visibility will be restored.",
                "Profiles, VMCs and games will not be erased.",
                "Cancel",
                "Restaurar",
                TitanShellScene_ConfirmResetShellLayout,
                0
            );
            return;

        case TITAN_CONTEXT_ACTION_EXPORT_SETTINGS:
        {
            int exported;

            exported = TitanShellLayout_Export();
            TitanNotifications_Push(
                exported ? TITAN_NOTIFICATION_SUCCESS : TITAN_NOTIFICATION_ERROR,
                "Exportar configuracoes",
                exported
                    ? "Layout exportado para SHELLLAY.EXP"
                    : "Falha ao gravar SHELLLAY.EXP"
            );
            return;
        }

        case TITAN_CONTEXT_ACTION_IMPORT_SETTINGS:
            if (TitanShellLayout_Import())
            {
                g_scene.top_menu_selected = 0;
                g_scene.top_menu_selected_position = 0.0f;
                TitanShellScene_ApplyDexLayout();
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_SUCCESS,
                    "Importar configuracoes",
                    "Layout importado de SHELLLAY.EXP"
                );
            }
            else
            {
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_ERROR,
                    "Importar configuracoes",
                    "SHELLLAY.EXP ausente ou invalido"
                );
            }
            return;

        case TITAN_CONTEXT_ACTION_RESTART_TITAN:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            g_scene.power_selected_row = 1;
            TitanShellScene_RunPowerAction();
            return;

        case TITAN_CONTEXT_ACTION_RESTART_CONSOLE:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            g_scene.power_selected_row = 2;
            TitanShellScene_RunPowerAction();
            return;

        case TITAN_CONTEXT_ACTION_POWER_OFF:
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            g_scene.power_selected_row = 0;
            TitanShellScene_RunPowerAction();
            return;

        case TITAN_CONTEXT_ACTION_NONE:
        default:
            return;
    }
}

static void TitanShellScene_DrawPowerRestartIcon(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    static const float arc_x[] =
    {
        3.0f,
        5.2f,
        6.0f,
        5.2f,
        3.0f,
        0.0f,
        -3.0f,
        -5.2f,
        -6.0f,
        -5.2f,
        -3.0f
    };
    static const float arc_y[] =
    {
        -5.2f,
        -3.0f,
        0.0f,
        3.0f,
        5.2f,
        6.0f,
        5.2f,
        3.0f,
        0.0f,
        -3.0f,
        -5.2f
    };
    unsigned int index;
    float arrow_x;
    float arrow_y;

    for (index = 0u; index + 1u < sizeof(arc_x) / sizeof(arc_x[0]); index++)
    {
        TitanVideo_DrawLineAlpha(
            center_x + (arc_x[index] * scale),
            center_y + (arc_y[index] * scale),
            center_x + (arc_x[index + 1u] * scale),
            center_y + (arc_y[index + 1u] * scale),
            1.35f * scale,
            red,
            green,
            blue,
            alpha
        );
    }

    arrow_x = center_x + (3.0f * scale);
    arrow_y = center_y - (5.2f * scale);

    TitanVideo_DrawLineAlpha(
        arrow_x,
        arrow_y,
        center_x + (0.6f * scale),
        center_y - (5.8f * scale),
        1.35f * scale,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        arrow_x,
        arrow_y,
        center_x + (2.4f * scale),
        center_y - (2.7f * scale),
        1.35f * scale,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanShellScene_DrawPowerOptionRow(
    float x,
    float y,
    float width,
    float focus,
    int icon_kind,
    const char *title,
    const char *line1,
    const char *line2,
    unsigned char alpha
)
{
    unsigned char title_alpha;
    unsigned char body_alpha;
    unsigned char border_alpha;
    unsigned char icon_alpha;
    unsigned char glow_outer_alpha;
    unsigned char glow_middle_alpha;
    float title_y;
    float content_x;
    float clamped_focus;
    float eased_focus;

    clamped_focus = TitanShellScene_Clamp(focus, 0.0f, 1.0f);
    eased_focus = TitanAnimation_SmoothStep(clamped_focus);
    title_alpha = TitanShellScene_Alpha((float)alpha * 0.98f);
    body_alpha = TitanShellScene_Alpha((float)alpha * 0.84f);
    icon_alpha = TitanShellScene_Alpha((float)alpha * (0.82f + (eased_focus * 0.18f)));
    border_alpha = TitanShellScene_Alpha((float)alpha * eased_focus);
    glow_outer_alpha = TitanShellScene_Alpha((float)alpha * eased_focus * 0.18f);
    glow_middle_alpha = TitanShellScene_Alpha((float)alpha * eased_focus * 0.34f);
    title_y = y + 3.0f + (eased_focus * 10.0f);
    content_x = x + 24.0f;

    if (eased_focus > 0.01f)
    {
        TitanShellScene_DrawOutline(
            x - 5.0f,
            y - 5.0f,
            width + 10.0f,
            76.0f,
            2.0f,
            231,
            42,
            81,
            glow_outer_alpha
        );

        TitanShellScene_DrawOutline(
            x - 2.0f,
            y - 2.0f,
            width + 4.0f,
            70.0f,
            2.0f,
            236,
            45,
            88,
            glow_middle_alpha
        );

        TitanShellScene_DrawOutline(
            x,
            y,
            width,
            66.0f,
            2.0f,
            244,
            55,
            101,
            border_alpha
        );
    }

    if (icon_kind == 0)
    {
        TitanIcons_Draw(
            TITAN_ICON_POWER,
            content_x + 7.0f,
            title_y + 7.0f,
            0.54f,
            244,
            247,
            252,
            icon_alpha
        );
    }
    else
    {
        TitanShellScene_DrawPowerRestartIcon(
            content_x + 7.0f,
            title_y + 7.0f,
            1.0f,
            244,
            247,
            252,
            icon_alpha
        );
    }

    TitanFont_DrawTextShadow(
        content_x + 28.0f,
        title_y,
        0.76f,
        title,
        248,
        250,
        255,
        title_alpha
    );

    TitanFont_DrawTextAlpha(
        content_x + 28.0f,
        title_y + 16.0f,
        0.42f,
        line1,
        210,
        213,
        222,
        body_alpha
    );

    if (line2 != 0 && line2[0] != '\0')
    {
        TitanFont_DrawTextAlpha(
            content_x + 28.0f,
            title_y + 28.0f,
            0.48f,
            line2,
            232,
            235,
            242,
            body_alpha
        );
    }
}

static void TitanShellScene_DrawPowerPage(
    float offset_x,
    unsigned char alpha
)
{
    float x;
    float content_width;
    float row_width;
    float row_x;
    unsigned char text_alpha;

    x = offset_x;
    content_width = (float)TITAN_UI_WIDTH;
    row_width = 708.0f;
    row_x = x + ((content_width - row_width) * 0.5f);
    text_alpha = TitanShellScene_Alpha((float)alpha * 0.96f);

    TitanVideo_DrawRectAlpha(
        x,
        46.0f,
        content_width,
        1.0f,
        231,
        42,
        81,
        TitanShellScene_Alpha((float)alpha * 0.72f)
    );

    TitanFont_DrawTextShadow(
        x + 26.0f,
        14.0f,
        0.96f,
        "Power Options",
        243,
        246,
        252,
        text_alpha
    );

    TitanShellScene_DrawPowerOptionRow(
        row_x,
        96.0f,
        row_width,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(0.0f - g_scene.power_row_position), 0.0f, 1.0f),
        0,
        "Turn Off PS2",
        "Close all applications and turn off the PS2.",
        "Select this before unplugging the AC power cord.",
        alpha
    );

    TitanShellScene_DrawPowerOptionRow(
        row_x,
        180.0f,
        row_width,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(1.0f - g_scene.power_row_position), 0.0f, 1.0f),
        1,
        "Restart Project Titan",
        "Close the current shell session and restart Project Titan.",
        "Useful after changing system modules or renderer settings.",
        alpha
    );

    TitanShellScene_DrawPowerOptionRow(
        row_x,
        264.0f,
        row_width,
        TitanShellScene_Clamp(1.0f - TitanAnimation_Absolute(2.0f - g_scene.power_row_position), 0.0f, 1.0f),
        2,
        "Restart PS2",
        "Close Project Titan and restart the PlayStation 2 system software.",
        "The console reinitializes the IOP before returning through ROM OSDSYS.",
        alpha
    );
}

static int TitanShellScene_ValidateBrowserAddress(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
)
{
    char normalized[TITAN_BROWSER_URL_CAPACITY];

    (void)user_data;
    if (
        text == 0 ||
        text[0] == '\0' ||
        !TitanBrowser_NormalizeUrl(text, normalized, sizeof(normalized))
    )
    {
        TitanShellScene_SetTextInputMessage(
            message,
            message_capacity,
            "Digite um endereco HTTP ou HTTPS valido."
        );
        return 0;
    }
    return 1;
}

static void TitanShellScene_ConfirmBrowserAddress(
    const char *text,
    void *user_data
)
{
    (void)user_data;
    if (!TitanBrowser_Navigate(text))
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            "Titan Browser",
            "Could not start browsing"
        );
    }
}

static int TitanShellScene_OpenBrowserAddressInput(void)
{
    TitanTextInputRequest request;
    const TitanBrowserSnapshot *browser;

    browser = TitanBrowser_GetSnapshot();
    request.title = TitanLocalization_Compat("Endereco da Web");
    request.initial_text =
        browser != 0 && browser->url != 0 &&
        strncmp(browser->url, "titan://", 8u) != 0
            ? browser->url
            : "http://";
    request.placeholder = "http://192.168.0.2:8080";
    request.maximum_codepoints = 180u;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.validation_callback = TitanShellScene_ValidateBrowserAddress;
    request.confirm_callback = TitanShellScene_ConfirmBrowserAddress;
    request.cancel_callback = 0;
    request.user_data = 0;
    return TitanTextInput_Open(&request);
}

static void TitanShellScene_HandleBrowserAction(TitanBrowserAction action)
{
    if (action == TITAN_BROWSER_ACTION_OPEN_ADDRESS)
    {
        (void)TitanShellScene_OpenBrowserAddressInput();
    }
    else if (action == TITAN_BROWSER_ACTION_DOWNLOAD_STARTED)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_WARNING,
            "Titan Browser",
            "Downloads ficam desativados nesta primeira versao interna"
        );
        TitanBrowser_Cancel();
    }
    else if (action == TITAN_BROWSER_ACTION_PLAY_STREAM)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_INFO,
            "Titan Browser",
            "Video ainda precisa ser portado para o player interno do Titan"
        );
    }
}

static void TitanShellScene_DrawBrowserPage(
    float offset_x,
    unsigned char alpha
)
{
    const TitanBrowserSnapshot *browser;
    char address[128];
    char progress[64];
    unsigned int index;
    unsigned int line_end;
    unsigned int link_start;
    unsigned int link_end;
    float content_x;
    float link_x;

    browser = TitanBrowser_GetSnapshot();
    if (browser == 0)
    {
        return;
    }

    TitanShellScene_DrawPanelBase(
        "Titan Browser",
        "Web leve dentro do Project Titan; HTTPS via Titan Stream Bridge",
        offset_x,
        alpha
    );

    content_x = 28.0f + offset_x;
    link_x = 630.0f + offset_x;
    snprintf(
        address,
        sizeof(address),
        "%.104s",
        browser->url != 0 ? browser->url : "titan://home"
    );

    TitanVideo_DrawRoundedRectAlpha(
        content_x,
        70.0f,
        838.0f,
        38.0f,
        8.0f,
        18, 20, 29,
        TitanShellScene_Alpha((float)alpha * 0.88f)
    );
    TitanShellScene_DrawOutline(
        content_x,
        70.0f,
        838.0f,
        38.0f,
        1.5f,
        207, 43, 82,
        TitanShellScene_Alpha((float)alpha * 0.82f)
    );
    TitanFont_DrawTextAlpha(
        content_x + 14.0f,
        82.0f,
        0.48f,
        address,
        238, 241, 248,
        alpha
    );
    TitanFont_DrawTextAlpha(
        content_x + 660.0f,
        84.0f,
        0.40f,
        "TRIANGLE Editar",
        231, 55, 96,
        alpha
    );

    TitanVideo_DrawRoundedRectAlpha(
        content_x,
        120.0f,
        582.0f,
        268.0f,
        10.0f,
        8, 10, 16,
        TitanShellScene_Alpha((float)alpha * 0.78f)
    );
    TitanVideo_DrawRoundedRectAlpha(
        link_x,
        120.0f,
        236.0f,
        268.0f,
        10.0f,
        8, 10, 16,
        TitanShellScene_Alpha((float)alpha * 0.78f)
    );

    TitanFont_DrawTextShadow(
        content_x + 18.0f,
        135.0f,
        0.72f,
        browser->title != 0 ? browser->title : "Titan Browser",
        248, 249, 253,
        alpha
    );
    line_end = browser->scroll_line + 9u;
    if (line_end > browser->line_count)
    {
        line_end = browser->line_count;
    }
    for (index = browser->scroll_line; index < line_end; index++)
    {
        const char *line;

        line = TitanBrowser_GetLine(index);
        TitanFont_DrawTextAlpha(
            content_x + 18.0f,
            171.0f + ((float)(index - browser->scroll_line) * 22.0f),
            0.43f,
            line != 0 ? line : "",
            220, 225, 235,
            alpha
        );
    }

    TitanFont_DrawTextShadow(
        link_x + 14.0f,
        136.0f,
        0.58f,
        "Links",
        248, 249, 253,
        alpha
    );
    link_start = browser->selected_link > 3u
        ? browser->selected_link - 3u
        : 0u;
    link_end = link_start + 7u;
    if (link_end > browser->link_count)
    {
        link_end = browser->link_count;
    }
    for (index = link_start; index < link_end; index++)
    {
        const TitanBrowserLink *link;
        char label[48];
        float row_y;

        link = TitanBrowser_GetLink(index);
        row_y = 168.0f + ((float)(index - link_start) * 29.0f);
        snprintf(
            label,
            sizeof(label),
            "%.40s",
            link != 0 ? link->label : ""
        );

        if (index == browser->selected_link)
        {
            TitanShellScene_DrawOutline(
                link_x + 10.0f,
                row_y - 7.0f,
                216.0f,
                25.0f,
                2.0f,
                231, 42, 81,
                alpha
            );
        }
        TitanFont_DrawTextAlpha(
            link_x + 18.0f,
            row_y,
            0.42f,
            label,
            index == browser->selected_link ? 255 : 221,
            index == browser->selected_link ? 235 : 225,
            index == browser->selected_link ? 241 : 234,
            alpha
        );
    }

    if (
        browser->state == TITAN_BROWSER_LOADING ||
        browser->state == TITAN_BROWSER_DOWNLOADING
    )
    {
        float ratio;

        ratio = browser->content_length > 0u
            ? (float)browser->bytes_received / (float)browser->content_length
            : 0.12f;
        ratio = TitanShellScene_Clamp(ratio, 0.02f, 1.0f);
        TitanVideo_DrawRoundedRectAlpha(
            content_x,
            398.0f,
            838.0f,
            5.0f,
            2.5f,
            50, 53, 64,
            alpha
        );
        TitanVideo_DrawRoundedRectAlpha(
            content_x,
            398.0f,
            838.0f * ratio,
            5.0f,
            2.5f,
            231, 42, 81,
            alpha
        );
    }

    snprintf(
        progress,
        sizeof(progress),
        "%.54s",
        browser->status != 0 ? browser->status : "Ready"
    );
    TitanFont_DrawTextAlpha(
        content_x,
        413.0f,
        0.41f,
        progress,
        browser->state == TITAN_BROWSER_ERROR ? 255 : 207,
        browser->state == TITAN_BROWSER_ERROR ? 112 : 213,
        browser->state == TITAN_BROWSER_ERROR ? 128 : 224,
        alpha
    );
    TitanFont_DrawTextAlpha(
        475.0f + offset_x,
        413.0f,
        0.40f,
        "L1/R1 Historico   X Abrir   TRIANGLE Endereco   O Voltar",
        214, 220, 234,
        alpha
    );
}

static void TitanShellScene_DrawAboutPage(
    float offset_x,
    unsigned char alpha
)
{
    int previous_category;

    previous_category = g_scene.settings_selected_category;
    g_scene.settings_selected_category = TITAN_SETTINGS_ABOUT;

    TitanShellScene_DrawSettingsPage(
        offset_x,
        alpha
    );

    g_scene.settings_selected_category = previous_category;
}

static void TitanShellScene_DrawDexPlaceholder(
    TitanShellPage page,
    float offset_x,
    unsigned char alpha
)
{
    const char *title;
    const char *subtitle;
    TitanIconId icon;

    if (page == TITAN_SHELL_PAGE_FTP_HOME)
    {
        title = "★ FTP_HOME";
        subtitle = "Activity shortcut; FTP server is not included";
        icon = TITAN_ICON_FTP_HOME;
    }
    else
    {
        title = "★ APP_HOME";
        subtitle = "Development application service placeholder";
        icon = TITAN_ICON_APP_HOME;
    }

    TitanShellScene_DrawPanelBase(title, subtitle, offset_x, alpha);

    TitanVideo_DrawRoundedRectAlpha(
        118.0f + offset_x,
        104.0f,
        618.0f,
        228.0f,
        10.0f,
        8, 10, 16,
        TitanShellScene_Alpha((float)alpha * 0.78f)
    );
    TitanShellScene_DrawOutline(
        118.0f + offset_x,
        104.0f,
        618.0f,
        228.0f,
        2.0f,
        216, 38, 76,
        TitanShellScene_Alpha((float)alpha * 0.76f)
    );
    TitanIcons_Draw(
        icon,
        212.0f + offset_x,
        216.0f,
        1.45f,
        248, 249, 253, alpha
    );
    TitanFont_DrawTextShadow(
        292.0f + offset_x,
        156.0f,
        0.86f,
        title,
        248, 249, 253, alpha
    );
    TitanFont_DrawTextAlpha(
        292.0f + offset_x,
        196.0f,
        0.58f,
        "DEX tile registered and ready in the Home grid.",
        221, 225, 234, alpha
    );
    TitanFont_DrawTextAlpha(
        292.0f + offset_x,
        226.0f,
        0.58f,
        "This tile now routes to its active service or hub.",
        221, 225, 234, alpha
    );
    TitanFont_DrawTextAlpha(
        292.0f + offset_x,
        268.0f,
        0.52f,
        "O Back",
        231, 55, 96, alpha
    );
}

static void TitanShellScene_DrawPage(
    const TitanContext *context,
    const TitanInputState *input,
    float offset_x,
    unsigned char alpha
)
{
    switch (g_scene.page)
    {
        case TITAN_SHELL_PAGE_APP_HOME:
        case TITAN_SHELL_PAGE_FTP_HOME:
            TitanShellScene_DrawDexPlaceholder(
                g_scene.page,
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_BROWSER:
            TitanShellScene_DrawBrowserPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_LIBRARY:
            TitanShellScene_DrawLibraryPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_APPLICATIONS:
            TitanShellScene_DrawApplicationsPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_STORAGE:
            TitanShellScene_DrawStoragePage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_INPUT:
            TitanShellScene_DrawInputPage(
                input,
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_NOTIFICATIONS:
            TitanShellScene_DrawNotificationsPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_USER_PROFILE:
            TitanShellScene_DrawUserProfilePage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_SAVED_DATA:
            TitanShellScene_DrawSavedDataPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_SETTINGS:
            TitanShellScene_DrawSettingsPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_POWER:
            TitanShellScene_DrawPowerPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_ABOUT:
            TitanShellScene_DrawAboutPage(
                offset_x,
                alpha
            );
            break;

        case TITAN_SHELL_PAGE_OVERVIEW:
        case TITAN_SHELL_PAGE_HOME:
        default:
            TitanShellScene_DrawOverviewPage(
                context,
                input,
                offset_x,
                alpha
            );
            break;
    }
}

static void TitanShellScene_DrawDexOverlay(void)
{
    char ee_value[16];
    char gs_value[16];
    char ram_value[16];
    char tech_text[96];
    TitanPikaVrTelemetry pikavr;
    TitanVlombisTelemetry vlombis;
    unsigned int ram_kb;
    unsigned int ram_mb;
    unsigned int ram_fraction;
    unsigned int length;
    float x;
    float y;
    const float width = 178.0f;
    const float height = 262.0f;
    TitanUiRect exclusions[3];
    TitanUiRect overlay_rect;
    TitanUiAnchor overlay_anchor;

    if (!TitanDex_IsEnabled() || !TitanDex_IsOverlayEnabled())
    {
        return;
    }

    memset(&pikavr, 0, sizeof(pikavr));
    memset(&vlombis, 0, sizeof(vlombis));
    TitanVideo_GetPikaVrTelemetry(&pikavr);
    TitanVlombisCore_GetTelemetry(&vlombis);

    /* Header/footer and mandatory left content are exclusion regions. The
     * anchor chooser keeps the DEX panel out of required UI without changing
     * the approved page layouts. */
    exclusions[0] = (TitanUiRect){0.0f, 0.0f, (float)TITAN_UI_WIDTH, 70.0f};
    exclusions[1] = (TitanUiRect){0.0f, TITAN_SHELL_FOOTER_TOP, (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT - TITAN_SHELL_FOOTER_TOP};
    exclusions[2] = (TitanUiRect){24.0f, 70.0f, 612.0f, 350.0f};
    overlay_anchor = TitanUiLayout_SelectSafeAnchor((float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT, width, height, 24.0f, exclusions, 3u);
    overlay_rect = TitanUiLayout_PlaceAnchored((float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT, width, height, 24.0f, overlay_anchor);
    x = overlay_rect.x;
    y = overlay_rect.y;

    TitanShellScene_FormatUnsigned(
        TitanDex_GetEeLoadPercent(),
        ee_value,
        sizeof(ee_value)
    );
    TitanShellScene_FormatUnsigned(
        TitanDex_GetGsLoadPercent(),
        gs_value,
        sizeof(gs_value)
    );

    length = 0u;
    while (ee_value[length] != '\0' && length + 2u < sizeof(ee_value))
    {
        length++;
    }
    ee_value[length++] = '%';
    ee_value[length] = '\0';

    length = 0u;
    while (gs_value[length] != '\0' && length + 2u < sizeof(gs_value))
    {
        length++;
    }
    gs_value[length++] = '%';
    gs_value[length] = '\0';

    ram_kb = TitanDex_GetRamUsedKilobytes();
    ram_mb = ram_kb / 1024u;
    ram_fraction = ((ram_kb % 1024u) * 10u) / 1024u;
    TitanShellScene_FormatUnsigned(ram_mb, ram_value, sizeof(ram_value));
    length = 0u;
    while (ram_value[length] != '\0' && length + 5u < sizeof(ram_value))
    {
        length++;
    }
    ram_value[length++] = '.';
    ram_value[length++] = (char)('0' + ram_fraction);
    ram_value[length++] = 'M';
    ram_value[length++] = 'B';
    ram_value[length] = '\0';

    TitanVideo_DrawRoundedRectAlpha(
        x, y, width, height, 8.0f,
        5, 7, 12, 214
    );
    TitanShellScene_DrawOutline(
        x, y, width, height, 1.0f,
        222, 43, 82, 224
    );
    TitanVideo_DrawRectAlpha(x, y, width, 24.0f, 202, 25, 61, 228);
    TitanFont_DrawTextShadow(x + 12.0f, y + 6.0f, 0.48f, "DEX MODE", 255, 255, 255, 255);

    TitanFont_DrawTextAlpha(x + 12.0f, y + 34.0f, 0.42f, "HEAP", 178, 186, 202, 255);
    TitanFont_DrawTextAlpha(x + 84.0f, y + 34.0f, 0.42f, ram_value, 246, 248, 252, 255);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 54.0f, 0.42f, "UI CPU", 178, 186, 202, 255);
    TitanFont_DrawTextAlpha(x + 84.0f, y + 54.0f, 0.42f, ee_value, 246, 248, 252, 255);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 74.0f, 0.42f, "GIF", 178, 186, 202, 255);
    TitanFont_DrawTextAlpha(x + 84.0f, y + 74.0f, 0.42f, gs_value, 246, 248, 252, 255);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 92.0f, 0.42f, "IP", 178, 186, 202, 255);
    TitanFont_DrawTextAlpha(x + 48.0f, y + 92.0f, 0.38f, TitanDex_GetIpAddress(), 246, 248, 252, 255);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 112.0f, 0.42f, "VU1", 178, 186, 202, 255);
    TitanFont_DrawTextAlpha(
        x + 48.0f,
        y + 112.0f,
        0.38f,
        TitanShellBackground_IsVuActive() ? "ACTIVE" : "FALLBACK",
        TitanShellBackground_IsVuActive() ? 120u : 245u,
        TitanShellBackground_IsVuActive() ? 230u : 176u,
        TitanShellBackground_IsVuActive() ? 164u : 65u,
        255u
    );
    snprintf(tech_text, sizeof(tech_text), "PIKA %s  H/M %u/%u",
        pikavr.vram_total != 0u ? "ACTIVE" : "OFF", pikavr.cache_hits, pikavr.cache_misses);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 132.0f, 0.34f, tech_text,
        pikavr.vram_total != 0u ? 120u : 245u, pikavr.vram_total != 0u ? 230u : 176u,
        pikavr.vram_total != 0u ? 164u : 65u, 255u);
    snprintf(tech_text, sizeof(tech_text), "VRAM %u/%uK  SAVE %uK",
        pikavr.vram_used / 1024u, pikavr.vram_total / 1024u, pikavr.vram_saved / 1024u);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 150.0f, 0.32f, tech_text, 178, 186, 202, 255);
    snprintf(tech_text, sizeof(tech_text), "VLOM %s %u/10 P%u E%u",
        vlombis.measured_events != 0u ? "ACTIVE" : "READY",
        TitanVlombisCore_GetActiveCapabilityCount(), vlombis.pressure_level,
        vlombis.measured_events);
    TitanFont_DrawTextAlpha(x + 12.0f, y + 168.0f, 0.32f, tech_text,
        vlombis.measured_events != 0u ? 120u : 220u,
        vlombis.measured_events != 0u ? 230u : 220u,
        vlombis.measured_events != 0u ? 164u : 180u, 255u);
    {
        const TitanDiagPerfSnapshot *perf = TitanDiagnostics_GetPerfSnapshot();
        char perf_text[64];
        snprintf(perf_text, sizeof(perf_text), "FPS %u.%02u  FT %ums", perf->fps_x100 / 100u, perf->fps_x100 % 100u, perf->frame_us / 1000u);
        TitanFont_DrawTextAlpha(x + 12.0f, y + 188.0f, 0.38f, perf_text, 246, 248, 252, 255);
        snprintf(perf_text, sizeof(perf_text), "P95 %ums MAX %ums SPK %u", perf->frame_p95_us / 1000u, perf->frame_worst_us / 1000u, perf->spikes);
        TitanFont_DrawTextAlpha(x + 12.0f, y + 206.0f, 0.32f, perf_text, 178, 186, 202, 255);
    }
    {
        const TitanDiscInfo *disc = TitanDiscService_GetInfo();
        const TitanDiscSession *disc_session = TitanDiscEngine_GetSession();
        char disc_text[96];
        snprintf(disc_text, sizeof(disc_text), "DISC %s  %s",
            disc != 0 ? TitanDiscState_Name(disc->state) : "N/A",
            disc != 0 && disc->title_id[0] != '\0' ? disc->title_id : "-");
        TitanFont_DrawTextAlpha(x + 12.0f, y + 224.0f, 0.31f, disc_text, 178, 186, 202, 255);
        snprintf(disc_text, sizeof(disc_text), "IGR %u OV %u READ %ums",
            disc_session != 0 ? (unsigned int)disc_session->igr_compatibility : 0u,
            disc_session != 0 ? (unsigned int)disc_session->overlay_level : 0u,
            disc != 0 ? disc->read_time_ms : 0u);
        TitanFont_DrawTextAlpha(x + 12.0f, y + 242.0f, 0.31f, disc_text, 178, 186, 202, 255);
    }
}

static void TitanShellScene_DrawFooterText(
    const char *text,
    float preferred_scale,
    unsigned char alpha
)
{
    const float left = 38.0f;
    const float right = 38.0f;
    float available_width;
    float measured_width;
    float scale;

    if (text == 0 || text[0] == '\0')
    {
        return;
    }

    available_width = (float)TITAN_UI_WIDTH - left - right;
    scale = preferred_scale;
    measured_width = TitanFont_MeasureText(scale, text);
    if (measured_width > available_width && measured_width > 0.0f)
    {
        scale *= available_width / measured_width;
    }

    TitanFont_DrawTextAlpha(
        left,
        TITAN_SHELL_FOOTER_TEXT_Y,
        scale,
        text,
        151,
        159,
        178,
        alpha
    );
}

static void TitanShellScene_DrawFooter(void)
{
    unsigned char footer_alpha;
    const TitanThemeShellStyle *theme_style;

    theme_style = TitanTheme_GetShellStyle();
    if (
        g_scene.page_transition > 0.02f &&
        g_scene.page_transition < 0.98f
    )
    {
        return;
    }

    footer_alpha = TitanShellScene_Alpha(
        255.0f * g_scene.footer_visibility *
        TitanShellScene_Clamp(theme_style->help_alpha, 0.0f, 1.0f)
    );

    if (footer_alpha <= 4)
    {
        return;
    }

    {
        float footer_height;
        float logical_scale_y;
        float logical_offset_y;

        footer_height = TITAN_SHELL_FOOTER_HEIGHT;
        logical_scale_y = TitanVideo_GetLogicalScaleY();
        logical_offset_y = TitanVideo_GetLogicalOffsetY();

        /* 720p centers the 854x448 authored canvas vertically. The footer
         * used to stop at logical y=448, exposing the lower 720p letterbox
         * as a magenta strip. Extend only the background through that physical
         * margin while leaving the hint text at its authored position. */
        if (logical_scale_y > 0.0f && logical_offset_y > 0.0f)
        {
            footer_height +=
                (logical_offset_y / logical_scale_y) + 1.0f;
        }

        TitanVideo_DrawRectAlpha(
            0.0f,
            TITAN_SHELL_FOOTER_TOP,
            (float)TITAN_UI_WIDTH,
            footer_height,
            5,
            8,
            16,
            footer_alpha
        );
    }

    if (g_scene.page_transition_target < 0.5f)
    {
        const char *footer_text;

        if (g_scene.shell_move_active)
        {
            footer_text = "LEFT/RIGHT Mover    X Confirmar    O Cancelar";
        }
        else if (g_scene.top_menu_active)
        {
            footer_text = "LEFT/RIGHT Navigate    DOWN Home    X Open    SELECT Opcoes";
        }
        else
        {
            footer_text = "D-Pad Navigate    UP System    X Open    SELECT Opcoes    TRIANGLE Refresh";
        }

        TitanShellScene_DrawFooterText(footer_text, 0.46f, footer_alpha);
    }
    else if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_ASSET_PROGRESS)
    {
        const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
        int active = assets != 0 && assets->manual_session_active;
        const char *hint = active
            ? "X Return    O Cancel & Return"
            : "X Return    O Back";
        TitanShellScene_DrawFooterText(hint, 0.50f, footer_alpha);
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_NOTIFICATIONS
    )
    {
        TitanShellScene_DrawFooterText(
            "UP/DOWN Navigate    O Back",
            0.50f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_USER_PROFILE
    )
    {
        TitanShellScene_DrawFooterText(
            "UP/DOWN Navigate    LEFT/RIGHT Profile    SQUARE Add    TRIANGLE Remove    O Back",
            0.50f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_SAVED_DATA
    )
    {
        TitanShellScene_DrawFooterText(
            "UP/DOWN Navigate    X Enter    O Back",
            0.50f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_SETTINGS
    )
    {
        TitanShellScene_DrawFooterText(
            g_scene.package_installer_open
                ? g_scene.package_installer_detail_open
                    ? (TitanPackageInstaller_IsActive()
                        ? "Installing package    O Cancel"
                        : "O Back")
                    : "UP/DOWN Navigate    X Install    TRIANGLE Refresh    O Back"
                : g_scene.settings_action_open
                    ? "UP/DOWN Navigate    X Select    O Back"
                : g_scene.wireless_detail_open
                    ? (g_scene.wireless_detail_mode == TITAN_WIRELESS_DETAIL_SCAN
                        ? "UP/DOWN Navigate    X Connect    O Back"
                        : "UP/DOWN Navigate    X Options    O Back")
                : g_scene.settings_leaf_open
                    ? "UP/DOWN Navigate    X Enter    O Back"
                : g_scene.settings_category_open
                    ? "UP/DOWN Navigate    LEFT/RIGHT Change    X Select    O Back"
                    : "UP/DOWN Navigate    X Enter    O Back",
            0.44f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_LIBRARY
    )
    {
        TitanShellScene_DrawFooterText(
            g_scene.library_grid_active
                ? "D-PAD Navigate    X Open    LEFT Categories    O Back"
                : "UP/DOWN Categories    RIGHT/X Content    O Back",
            0.44f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_APPLICATIONS
    )
    {
        TitanShellScene_DrawFooterText(
            "UP/DOWN Navigate    X Start    TRIANGLE Scan    O Back",
            0.50f,
            footer_alpha
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_POWER
    )
    {
        TitanShellScene_DrawFooterText(
            "UP/DOWN Navigate    X Enter    O Back",
            0.50f,
            footer_alpha
        );
    }
    else
    {
        TitanShellScene_DrawFooterText(
            "O Back",
            0.56f,
            footer_alpha
        );
    }
}

int TitanShellScene_IsStartupActive(void)
{
    return g_scene.startup_active != 0;
}

void TitanShellScene_Initialize(void)
{
    g_scene.initialized = 1;
    g_scene.selected_item = 0;
    g_scene.page = TITAN_SHELL_PAGE_OVERVIEW;
    g_scene.top_menu_active = 0;
    g_scene.top_menu_selected = 0;
    g_scene.settings_selected_category = TITAN_SETTINGS_USERS;
    g_scene.settings_category_open = 0;
    g_scene.settings_selected_row = 0;
    g_scene.power_selected_row = 0;
    g_scene.notifications_selected_row = 0;
    g_scene.user_profile_selected_row = 0;
    g_scene.saved_data_selected_row = 0;
    g_scene.applications_selected_row = 0;
    g_scene.library_selected_category = TITAN_LIBRARY_CATEGORY_ALL;
    g_scene.library_grid_active = 0;
    g_scene.library_selected_item = 0;
    g_scene.library_search[0] = '\0';
    g_scene.package_installer_open = 0;
    g_scene.package_installer_detail_open = 0;
    g_scene.package_installer_selected_row = 0;
    g_scene.system_update_open = 0;
    g_scene.system_update_selected_row = 0;
    g_scene.system_update_tracking = 0;
    g_scene.settings_leaf_mode = TITAN_SETTINGS_LEAF_CLOSED;
    g_scene.settings_leaf_open = 0;
    g_scene.settings_leaf_selected_row = 0;
    g_scene.wireless_detail_mode = TITAN_WIRELESS_DETAIL_CLOSED;
    g_scene.wireless_detail_open = 0;
    g_scene.wireless_detail_selected_row = 0;
    g_scene.settings_action_kind = TITAN_SETTINGS_ACTION_NONE;
    g_scene.settings_action_open = 0;
    g_scene.settings_action_selected = 0;
    g_scene.settings_action_source_index = 0u;
    g_scene.wireless_connect_source_index = 0xffffffffu;
    g_scene.package_install_tracking = 0;
    g_scene.package_music_suspended = 0;
    g_scene.package_scan_deferred = 0;
    memset(g_scene.pending_uninstall_root, 0, sizeof(g_scene.pending_uninstall_root));
    memset(g_scene.pending_uninstall_title_id, 0, sizeof(g_scene.pending_uninstall_title_id));
    memset(g_scene.pending_uninstall_title, 0, sizeof(g_scene.pending_uninstall_title));
    g_scene.folder_ui_mode = TITAN_FOLDER_UI_CLOSED;
    g_scene.folder_previous_ui_mode = TITAN_FOLDER_UI_CLOSED;
    g_scene.folder_context_selected = 0;
    g_scene.resolution_options_open = 0;
    g_scene.resolution_options_selected = 0;
    g_scene.patch_options_open = 0;
    g_scene.patch_options_kind = TITAN_GAME_PATCH_CHEAT;
    g_scene.patch_options_selected = 0;
    g_scene.patch_options_scroll = 0;
    g_scene.folder_destination_selected = 0;
    g_scene.folder_editor_selected = 0;
    g_scene.folder_content_cursor = 0;
    g_scene.folder_content_side_selected = 0;
    g_scene.folder_content_side_active = 0;
    g_scene.folder_edit_existing = -1;
    g_scene.folder_name_locked = 0;
    g_scene.context_source = TITAN_CONTEXT_SOURCE_HOME;
    g_scene.context_shell_page = TITAN_SHELL_PAGE_HOME;
    g_scene.context_shell_catalog_index = 0u;
    g_scene.shell_move_active = 0;
    g_scene.shell_move_from_system_menu = 0;
    g_scene.shell_move_catalog_index = 0u;
    g_scene.shell_move_original_slot = -1;
    g_scene.folder_open = 0;
    g_scene.folder_open_index = 0u;
    g_scene.folder_open_selected = 0;
    g_scene.folder_open_transition = 0.0f;
    g_scene.folder_open_selected_position = 0.0f;
    g_scene.folder_origin_game_index = TITAN_NEUTRINO_GAME_CAPACITY;
    g_scene.folder_origin_shell_catalog_index = -1;
    memset(g_scene.folder_selected_games, 0, sizeof(g_scene.folder_selected_games));
    memset(g_scene.folder_selected_shell, 0, sizeof(g_scene.folder_selected_shell));
    g_scene.folder_pending_name[0] = '\0';
    g_scene.folder_context_transition = 0.0f;
    g_scene.resolution_options_transition = 0.0f;
    g_scene.patch_options_transition = 0.0f;
    g_scene.folder_screen_transition = 1.0f;
    g_scene.video_test_active = 0;
    g_scene.video_test_previous_progressive = 0;
    g_scene.shutdown_screen_active = 0;
    g_scene.startup_active = 1;
    g_scene.startup_phase = TITAN_STARTUP_PHASE_INITIAL_BLACK;
    g_welcome_candidate_id = 0u;
    TitanUnifiedInput_SetBootUiActive(1);
    g_scene.startup_profile_selected = 1;
    g_scene.profile_creation_from_startup = 0;
    g_scene.storage_boot_notice_shown = 0;
    g_scene.memory_card_selected_port = 0;
    g_scene.memory_card_action_port = 0;
    g_scene.pending_profile_name[0] = '\0';
    g_scene.selected_position = 0.0f;
    g_scene.page_transition = 0.0f;
    g_scene.page_transition_target = 0.0f;
    g_scene.top_menu_transition = 0.0f;
    g_scene.top_menu_selected_position = 0.0f;
    g_scene.settings_depth_transition = 0.0f;
    g_scene.settings_category_position = 0.0f;
    g_scene.settings_row_position = 0.0f;
    g_scene.power_row_position = 0.0f;
    g_scene.notifications_row_position = 0.0f;
    g_scene.user_profile_row_position = 0.0f;
    g_scene.saved_data_row_position = 0.0f;
    g_scene.applications_row_position = 0.0f;
    g_scene.library_category_position = 0.0f;
    g_scene.library_item_position = 0.0f;
    g_scene.package_installer_row_position = 0.0f;
    g_scene.package_installer_transition = 0.0f;
    g_scene.system_update_row_position = 0.0f;
    g_scene.system_update_transition = 0.0f;
    g_scene.settings_leaf_transition = 0.0f;
    g_scene.settings_leaf_row_position = 0.0f;
    g_scene.wireless_detail_transition = 0.0f;
    g_scene.wireless_detail_row_position = 0.0f;
    g_scene.settings_action_row_position = 0.0f;
    g_scene.shutdown_screen_fade = 0.0f;
    g_scene.local_frame = 0u;
    g_scene.selected_hold_frames = 0u;
    g_scene.inactivity_frames = 0u;
    g_scene.startup_phase_frames = 0u;
    g_scene.video_test_frames_remaining = 0u;
    g_scene.last_update_ms = TitanTime_NowMilliseconds();
    g_scene.fixed_step_accumulator = 0u;
    g_scene.detail_visibility = 0.0f;
    g_scene.startup_profile_position = 1.0f;
    g_scene.footer_visibility = 0.0f;

    TitanSystemClock_Initialize();
    TitanUserProfiles_Initialize();
    TitanThemeLifecycle_Initialize();
    TitanWirelessAccessories_Initialize();
    TitanShellBackground_Initialize();
    (void)TitanAvatars_Initialize();

    /*
     * poweroffInit() não pode ser chamado durante o boot:
     * o servidor PWROFF_IRX ainda não está disponível.
     */
}

static int TitanShellScene_CanSelectGameForFolder(unsigned int game_index)
{
    const TitanNeutrinoGame *game;
    int owner;

    game = TitanNeutrino_GetGame(game_index);
    if (game == 0)
    {
        return 0;
    }

    owner = TitanShellScene_FindFolderForGame(game_index, game);
    return owner < 0 || owner == g_scene.folder_edit_existing;
}


static void TitanShellScene_BeginGameLaunch(unsigned int game_index)
{
    const TitanNeutrinoGame *game;
    TitanGamePatchLaunchState patch_state;
    TitanGamePatchResult patch_result;

    game = TitanNeutrino_GetGame(game_index);
    if (game == 0 || g_scene.game_launch_active)
        return;

    memset(&patch_state, 0, sizeof(patch_state));
    g_scene.game_launch_enhanced = 0;
    g_scene.game_launch_cheat_count = 0u;
    g_scene.game_launch_cheat_taunt[0] = '\0';

    /* Patch preparation happens before the launch choreography so the visual
     * indicators describe the payload that will actually be handed off. An
     * unknown/malformed startup remains launchable through the normal engine;
     * patch I/O/validation failures do not silently start a different state. */
    patch_result = TitanGamePatches_PrepareLaunch(game->startup, &patch_state);
    if (patch_result == TITAN_GAME_PATCH_INVALID_STARTUP)
    {
        memset(&patch_state, 0, sizeof(patch_state));
    }
    else if (patch_result != TITAN_GAME_PATCH_OK)
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            TitanLocalization_Compat("Game Enhancements"),
            TitanLocalization_Compat(TitanGamePatches_GetResultName(patch_result))
        );
        return;
    }

    g_scene.game_launch_enhanced = patch_state.enhancement_active ? 1 : 0;
    g_scene.game_launch_cheat_count = patch_state.gameplay_count;
    if (patch_state.taunt != 0 && patch_state.taunt[0] != '\0')
    {
        strncpy(
            g_scene.game_launch_cheat_taunt,
            patch_state.taunt,
            sizeof(g_scene.game_launch_cheat_taunt) - 1u
        );
        g_scene.game_launch_cheat_taunt[
            sizeof(g_scene.game_launch_cheat_taunt) - 1u
        ] = '\0';
    }

    g_scene.game_launch_active = 1;
    g_scene.game_launch_frames = 0u;
    g_scene.game_launch_index = game_index;
    g_scene.game_launch_ul_engine =
        game->format == TITAN_NEUTRINO_GAME_USBEXTREME ? 1 : 0;
    g_scene.game_launch_disc = 0;
    g_scene.game_launch_application = 0;
    g_scene.game_launch_application_index = 0u;
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        TitanLocalization_Compat("Starting game"),
        game->title
    );
}

static void TitanShellScene_BeginDiscLaunch(void)
{
    const TitanDiscInfo *disc;

    if (g_scene.game_launch_active || !TitanDiscService_IsReady())
        return;
    disc = TitanDiscService_GetInfo();
    if (disc == 0)
        return;

    g_scene.game_launch_active = 1;
    g_scene.game_launch_frames = 0u;
    g_scene.game_launch_index = 0u;
    g_scene.game_launch_ul_engine = 0;
    g_scene.game_launch_disc = 1;
    g_scene.game_launch_application = 0;
    g_scene.game_launch_application_index = 0u;
    g_scene.game_launch_enhanced = 0;
    g_scene.game_launch_cheat_count = 0u;
    g_scene.game_launch_cheat_taunt[0] = '\0';
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        TitanLocalization_Compat("Starting disc"),
        disc->title
    );
}

static void TitanShellScene_BeginApplicationLaunch(
    unsigned int application_index
)
{
    const TitanApplication *application;

    application = TitanApplications_Get(application_index);
    if (application == 0 || g_scene.game_launch_active)
        return;

    /* Browser is a shell page, not an application handoff/runtime. Keep its
     * direct page transition; every executable app uses the same launch
     * choreography as ISO/UL/Disc. */
    if (TitanApplications_IsSystemBrowser(application_index))
    {
        TitanShellScene_LaunchApplication(application_index);
        return;
    }

    g_scene.game_launch_active = 1;
    g_scene.game_launch_frames = 0u;
    g_scene.game_launch_index = 0u;
    g_scene.game_launch_ul_engine = 0;
    g_scene.game_launch_disc = 0;
    g_scene.game_launch_application = 1;
    g_scene.game_launch_application_index = application_index;
    g_scene.game_launch_enhanced = 0;
    g_scene.game_launch_cheat_count = 0u;
    g_scene.game_launch_cheat_taunt[0] = '\0';
    g_scene.selected_hold_frames = 0u;
    g_scene.detail_visibility = 0.0f;

    /* Keep the selected app live-area resident for the launch choreography.
     * It is released only at the runtime handoff, so Titan Native apps enter
     * with the same visual continuity as ISO/UL titles instead of flashing the
     * shell waves back in at frame zero. */

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        TitanLocalization_Compat("Starting application"),
        application->title
    );
}

static void TitanShellScene_RunGameLaunch(void)
{
    TitanNeutrinoResult launch_result;

    if (g_scene.game_launch_application)
    {
        unsigned int application_index = g_scene.game_launch_application_index;

        /* Animation is complete. Release the 640x480 live-area only now, at
         * the ownership boundary, so the runtime arena gets the memory without
         * making the app launch animation visually diverge from ISO/UL. */
        TitanAppAssets_ReleaseSelectedBackground();

        /* Drop the shell launch-state before opening an in-process Titan
         * runtime so TitanAppRuntime becomes the sole visual owner next frame. */
        g_scene.game_launch_active = 0;
        g_scene.game_launch_frames = 0u;
        g_scene.game_launch_application = 0;
        g_scene.game_launch_application_index = 0u;
        g_scene.game_launch_enhanced = 0;
        g_scene.game_launch_cheat_count = 0u;
        g_scene.game_launch_cheat_taunt[0] = '\0';
        TitanShellScene_LaunchApplication(application_index);
        return;
    }

    TitanShellBusy_Begin();
    TitanShellBusy_Pulse();
    TitanAudio_PrepareHandoff();
    if (g_scene.game_launch_disc)
    {
        if (TitanDiscEngine_Prepare() && TitanDiscEngine_Launch())
            return; /* successful target handoff never returns */
        launch_result = TITAN_NEUTRINO_EXEC_FAILED;
    }
    else
    {
        launch_result = TitanNeutrino_Launch(g_scene.game_launch_index);
    }
    TitanAudio_CancelHandoff();
    TitanShellBusy_End();

    /* ExecPS2 does not return after a successful handoff. */
    g_scene.game_launch_active = 0;
    g_scene.game_launch_frames = 0u;
    g_scene.game_launch_enhanced = 0;
    g_scene.game_launch_cheat_count = 0u;
    g_scene.game_launch_cheat_taunt[0] = '\0';
    if (g_scene.game_launch_disc)
    {
        g_scene.game_launch_disc = 0;
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            TitanLocalization_Compat("Failed to start disc"),
            TitanDiscEngine_GetLastError()
        );
    }
    else
    {
        TitanNotifications_Push(
            TITAN_NOTIFICATION_ERROR,
            TitanLocalization_Compat("Failed to start game"),
            TitanLocalization_Compat(TitanNeutrino_GetResultName(launch_result))
        );
    }
}

static int TitanShellScene_DrawApplicationLiveBackground(
    unsigned int frame,
    unsigned int application_index,
    unsigned char alpha
)
{
    const TitanApplication *application;

    application = TitanApplications_Get(application_index);
    if (application == 0 || application->background_path[0] == '\0')
        return 0;

    if (TitanAppAssets_DrawSelectedBackground(
            application_index,
            0.0f, 0.0f,
            (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
            alpha))
    {
        return 1;
    }

    if (TitanAppAssets_IsSelectionLoading())
    {
        /* A declared app background owns the live-area as soon as selection
         * begins. Never reveal waves/ribbons while its 480-line image streams
         * from storage: use the shell's existing loading-symbol animation on a
         * neutral backing until the buffer is published. */
        TitanVideo_DrawRectAlpha(
            0.0f, 0.0f,
            (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
            5, 6, 10, alpha
        );
        TitanShellScene_DrawShutdownSymbolCluster(
            frame,
            (float)TITAN_UI_WIDTH * 0.5f,
            (float)TITAN_UI_HEIGHT * 0.5f,
            TitanShellScene_ModulateAlpha(230u, alpha)
        );
        return 1;
    }

    /* Missing/corrupt legacy art is a real fallback case; only then allow the
     * configured shell background to return. */
    return 0;
}

static float TitanShellScene_GameLaunchProgress(unsigned int start, unsigned int end)
{
    if (!g_scene.game_launch_active || end <= start)
    {
        return 0.0f;
    }

    return TitanAnimation_SmoothStep(
        TitanShellScene_Clamp(
            ((float)g_scene.game_launch_frames - (float)start) /
            ((float)end - (float)start),
            0.0f,
            1.0f
        )
    );
}

static void TitanShellScene_DrawEngineBadge(unsigned char alpha)
{
    float x;
    float y;
    float width;
    float height;
    float text_x;
    float engine_word_x;
    float engine_prefix_width;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char panel_alpha;
    const char *engine_prefix;
    const char *engine_word;
    int native_badge;

    if (alpha <= 4u)
        return;

    native_badge = g_scene.game_launch_application != 0;
    engine_word = "ENGINE";
    height = 50.0f;

    if (native_badge)
    {
        const TitanApplication *application = TitanApplications_Get(
            g_scene.game_launch_application_index
        );
        int native_game = application != 0 &&
            application->type == TITAN_APPLICATION_TITAN_GAME;

        red = 238u;
        green = 45u;
        blue = 88u;
        engine_prefix = native_game ? "TITAN NATIVE" : "TITAN";
        engine_word = native_game ? "GAME" : "NATIVE";
        width = native_game ? 246.0f : 214.0f;
    }
    else if (g_scene.game_launch_disc)
    {
        red = 122u;
        green = 210u;
        blue = 255u;
        engine_prefix = "TITAN DISC";
        width = 214.0f;
    }
    else if (g_scene.game_launch_ul_engine)
    {
        red = 0u;
        green = 158u;
        blue = 255u;
        engine_prefix = "TITAN UL";
        width = 214.0f;
    }
    else
    {
        red = 238u;
        green = 45u;
        blue = 88u;
        engine_prefix = "TITAN-NEUTRINO";
        width = 214.0f;
    }

    x = (float)TITAN_UI_WIDTH - width - 24.0f;
    y = (float)TITAN_UI_HEIGHT - height - 24.0f;
    panel_alpha = TitanShellScene_Alpha((float)alpha * 0.90f);

    TitanVideo_DrawRoundedRectAlpha(
        x, y, width, height, 8.0f,
        5, 6, 10, panel_alpha
    );
    TitanShellScene_DrawOutline(
        x + 0.5f, y + 0.5f, width - 1.0f, height - 1.0f,
        1.4f, red, green, blue,
        TitanShellScene_Alpha((float)alpha * 0.96f)
    );

    if (!native_badge && g_scene.game_launch_ul_engine)
    {
        TitanVideo_DrawLineAlpha(x + 15.0f, y + 13.0f, x + 15.0f, y + 31.0f, 2.8f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 15.0f, y + 31.0f, x + 23.0f, y + 38.0f, 2.8f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 23.0f, y + 38.0f, x + 31.0f, y + 31.0f, 2.8f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 31.0f, y + 31.0f, x + 31.0f, y + 13.0f, 2.8f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 23.0f, y + 9.0f, x + 23.0f, y + 27.0f, 2.6f, red, green, blue, alpha);
    }
    else
    {
        /* Same visual badge component/glyph family is used for ISO/UL and
         * Titan Native classifications; only label/accent semantics differ. */
        TitanVideo_DrawLineAlpha(x + 14.0f, y + 14.0f, x + 34.0f, y + 14.0f, 3.0f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 34.0f, y + 14.0f, x + 24.0f, y + 37.0f, 3.0f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 24.0f, y + 37.0f, x + 14.0f, y + 14.0f, 3.0f, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + 20.0f, y + 20.0f, x + 29.0f, y + 20.0f, 2.0f, 250, 250, 252, TitanShellScene_Alpha((float)alpha * 0.82f));
    }

    TitanVideo_DrawLineAlpha(
        x + 42.0f, y + 9.0f, x + 42.0f, y + 41.0f,
        1.0f, 100, 103, 114,
        TitanShellScene_Alpha((float)alpha * 0.78f)
    );

    text_x = x + 52.0f;
    TitanFont_DrawTextAlpha(
        text_x, y + 7.0f, 0.40f,
        "Powered by:",
        205, 208, 216, alpha
    );
    TitanFont_DrawTextAlpha(
        text_x, y + 25.0f, 0.44f,
        engine_prefix,
        246, 247, 250, alpha
    );

    engine_prefix_width = TitanFont_MeasureText(0.44f, engine_prefix);
    engine_word_x = text_x + engine_prefix_width + 5.0f;
    TitanFont_DrawTextAlpha(
        engine_word_x, y + 25.0f, 0.44f,
        engine_word, red, green, blue, alpha
    );
}

static void TitanShellScene_DrawTitanEnhanced(unsigned char alpha)
{
    const char *label = "Titan Enhanced";
    const float x = 28.0f;
    const float y = 31.0f;
    const float scale = 0.68f;
    const float outline = 1.35f;
    unsigned char outline_alpha;

    if (!g_scene.game_launch_enhanced || alpha <= 4u)
        return;

    outline_alpha = TitanShellScene_Alpha((float)alpha * 0.96f);
    TitanFont_DrawTextAlpha(x - outline, y, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x + outline, y, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x, y - outline, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x, y + outline, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x - outline, y - outline, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x + outline, y - outline, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x - outline, y + outline, scale, label, 238, 45, 88, outline_alpha);
    TitanFont_DrawTextAlpha(x + outline, y + outline, scale, label, 238, 45, 88, outline_alpha);

    /* A tiny second white pass gives the existing regular face a semibold
     * presence without introducing a second font asset into the release. */
    TitanFont_DrawTextAlpha(x + 0.35f, y, scale, label, 252, 252, 255, alpha);
    TitanFont_DrawTextAlpha(x, y, scale, label, 252, 252, 255, alpha);
}

static void TitanShellScene_DrawCheatTaunt(unsigned char alpha)
{
    const char *taunt;
    float scale = 0.46f;
    float width;
    float y;

    if (g_scene.game_launch_cheat_count == 0u ||
        g_scene.game_launch_cheat_taunt[0] == '\0' || alpha <= 4u)
        return;

    taunt = TitanLocalization_Compat(g_scene.game_launch_cheat_taunt);
    width = TitanFont_MeasureText(scale, taunt);
    if (width > (float)TITAN_UI_WIDTH - 56.0f && width > 0.0f)
        scale *= ((float)TITAN_UI_WIDTH - 56.0f) / width;
    y = g_scene.game_launch_enhanced ? 66.0f : 36.0f;

    TitanFont_DrawTextShadow(
        28.0f, y, scale, taunt,
        241, 243, 248,
        TitanShellScene_Alpha((float)alpha * 0.96f)
    );
}

static int TitanShellScene_UpdateFolderUi(
    const TitanInputState *input
)
{
    if (input == 0 || !input->connected)
    {
        return
            g_scene.folder_ui_mode != TITAN_FOLDER_UI_CLOSED ||
            g_scene.folder_open ||
            g_scene.folder_open_transition > 0.02f;
    }

    if (g_scene.folder_ui_mode == TITAN_FOLDER_UI_CONTEXT)
    {
        TitanContextMenuItem items[TITAN_CONTEXT_MENU_MAX_ITEMS];
        unsigned int menu_count;

        menu_count = TitanShellScene_BuildContextMenu(items);
        if (menu_count == 0u)
        {
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            return 1;
        }

        if (g_scene.folder_context_selected < 0)
        {
            g_scene.folder_context_selected = (int)menu_count - 1;
        }
        else if ((unsigned int)g_scene.folder_context_selected >= menu_count)
        {
            g_scene.folder_context_selected = 0;
        }

        if (g_scene.patch_options_open)
        {
            const unsigned int visible_rows = 8u;
            unsigned int entry_count = TitanGamePatches_GetEntryCount(
                g_scene.patch_options_kind
            );

            if ((input->pressed & TITAN_INPUT_BUTTON_CIRCLE) ||
                (input->pressed & TITAN_INPUT_BUTTON_LEFT))
            {
                g_scene.patch_options_open = 0;
                return 1;
            }

            if (entry_count > 0u)
            {
                if (g_scene.patch_options_selected < 0 ||
                    (unsigned int)g_scene.patch_options_selected >= entry_count)
                    g_scene.patch_options_selected = 0;

                if (input->pressed & TITAN_INPUT_BUTTON_UP)
                {
                    g_scene.patch_options_selected--;
                    if (g_scene.patch_options_selected < 0)
                        g_scene.patch_options_selected = (int)entry_count - 1;
                }
                if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
                {
                    g_scene.patch_options_selected++;
                    if ((unsigned int)g_scene.patch_options_selected >= entry_count)
                        g_scene.patch_options_selected = 0;
                }

                if ((unsigned int)g_scene.patch_options_selected <
                    (unsigned int)g_scene.patch_options_scroll)
                    g_scene.patch_options_scroll = g_scene.patch_options_selected;
                else if ((unsigned int)g_scene.patch_options_selected >=
                         (unsigned int)g_scene.patch_options_scroll + visible_rows)
                    g_scene.patch_options_scroll =
                        g_scene.patch_options_selected - (int)visible_rows + 1;

                if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
                {
                    TitanGamePatchResult patch_result = TitanGamePatches_ToggleEntry(
                        g_scene.patch_options_kind,
                        (unsigned int)g_scene.patch_options_selected
                    );
                    if (patch_result != TITAN_GAME_PATCH_OK)
                    {
                        TitanNotifications_Push(
                            TITAN_NOTIFICATION_ERROR,
                            TitanLocalization_Compat(
                                TitanGamePatches_GetKindTitle(g_scene.patch_options_kind)
                            ),
                            TitanLocalization_Compat("Patch settings could not be saved")
                        );
                    }
                }
            }
            return 1;
        }

        if (g_scene.resolution_options_open)
        {
            TitanHomeEntry selected_home;
            const TitanNeutrinoGame *game;
            TitanPerGameGsmMode modes[4];
            unsigned int mode_count;

            memset(&selected_home, 0, sizeof(selected_home));
            if (!TitanShellScene_GetSelectedResolutionGame(&selected_home, &game))
            {
                g_scene.resolution_options_open = 0;
                return 1;
            }
            mode_count = TitanShellScene_BuildResolutionModes(game, modes);
            if ((input->pressed & TITAN_INPUT_BUTTON_CIRCLE) ||
                (input->pressed & TITAN_INPUT_BUTTON_LEFT))
            {
                g_scene.resolution_options_open = 0;
                return 1;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.resolution_options_selected--;
                if (g_scene.resolution_options_selected < 0)
                    g_scene.resolution_options_selected = (int)mode_count - 1;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.resolution_options_selected++;
                if ((unsigned int)g_scene.resolution_options_selected >= mode_count)
                    g_scene.resolution_options_selected = 0;
            }
            if ((input->pressed & TITAN_INPUT_BUTTON_CROSS) && mode_count > 0u)
                (void)TitanShellScene_SetSelectedGameResolution(
                    modes[g_scene.resolution_options_selected]);
            return 1;
        }

        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            return 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_UP)
        {
            g_scene.folder_context_selected--;
            if (g_scene.folder_context_selected < 0)
            {
                g_scene.folder_context_selected = (int)menu_count - 1;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
        {
            g_scene.folder_context_selected++;
            if ((unsigned int)g_scene.folder_context_selected >= menu_count)
            {
                g_scene.folder_context_selected = 0;
            }
        }


        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            TitanShellScene_RunContextAction(
                items[g_scene.folder_context_selected].action
            );
        }
        return 1;
    }

    if (g_scene.folder_ui_mode == TITAN_FOLDER_UI_DESTINATION)
    {
        int maximum;

        maximum = (int)TitanHomeFolders_GetCount();
        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CONTEXT);
            return 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_UP)
        {
            g_scene.folder_destination_selected--;
            if (g_scene.folder_destination_selected < 0)
            {
                g_scene.folder_destination_selected = maximum;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
        {
            g_scene.folder_destination_selected++;
            if (g_scene.folder_destination_selected > maximum)
            {
                g_scene.folder_destination_selected = 0;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            if (g_scene.folder_destination_selected == 0)
            {
                TitanShellScene_BeginFolderEditor(
                    g_scene.folder_origin_game_index,
                    g_scene.folder_origin_shell_catalog_index,
                    -1,
                    0
                );
            }
            else
            {
                TitanShellScene_BeginFolderEditor(
                    g_scene.folder_origin_game_index,
                    g_scene.folder_origin_shell_catalog_index,
                    g_scene.folder_destination_selected - 1,
                    1
                );
            }
        }
        return 1;
    }

    if (g_scene.folder_ui_mode == TITAN_FOLDER_UI_EDITOR)
    {
        int minimum_selection;

        minimum_selection = g_scene.folder_name_locked ? 1 : 0;
        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CONTEXT);
            return 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_UP)
        {
            g_scene.folder_editor_selected--;
            if (g_scene.folder_editor_selected < minimum_selection)
            {
                g_scene.folder_editor_selected = 3;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
        {
            g_scene.folder_editor_selected++;
            if (g_scene.folder_editor_selected > 3)
            {
                g_scene.folder_editor_selected = minimum_selection;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
        {
            if (g_scene.folder_editor_selected >= 2)
            {
                g_scene.folder_editor_selected = 2;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
        {
            if (g_scene.folder_editor_selected >= 2)
            {
                g_scene.folder_editor_selected = 3;
            }
        }

        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            if (g_scene.folder_editor_selected == 0)
            {
                (void)TitanShellScene_OpenFolderNameInput();
            }
            else if (g_scene.folder_editor_selected == 1)
            {
                g_scene.folder_content_side_active = 0;
                TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CONTENT);
            }
            else if (g_scene.folder_editor_selected == 2)
            {
                TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
            }
            else
            {
                (void)TitanShellScene_ApplyFolderEditor();
            }
        }
        return 1;
    }

    if (g_scene.folder_ui_mode == TITAN_FOLDER_UI_CONTENT)
    {
        const int columns = 6;
        unsigned int candidate_count;
        int cursor;

        candidate_count = TitanShellScene_GetFolderCandidateCount();
        if (candidate_count == 0u)
        {
            if (input->pressed & (TITAN_INPUT_BUTTON_CIRCLE | TITAN_INPUT_BUTTON_CROSS))
            {
                TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_EDITOR);
            }
            return 1;
        }

        cursor = g_scene.folder_content_cursor;
        if (cursor < 0)
        {
            cursor = 0;
        }
        if ((unsigned int)cursor >= candidate_count)
        {
            cursor = (int)candidate_count - 1;
        }

        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_EDITOR);
            g_scene.folder_content_side_active = 0;
            return 1;
        }

        if (g_scene.folder_content_side_active)
        {
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.folder_content_side_selected--;
                if (g_scene.folder_content_side_selected < 0)
                {
                    g_scene.folder_content_side_selected = 2;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.folder_content_side_selected++;
                if (g_scene.folder_content_side_selected > 2)
                {
                    g_scene.folder_content_side_selected = 0;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
            {
                g_scene.folder_content_side_active = 0;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                unsigned int index;

                if (g_scene.folder_content_side_selected == 0)
                {
                    unsigned int selected_total;

                    selected_total = TitanShellScene_CountFolderSelectedItems();
                    for (
                        index = 0u;
                        index < candidate_count &&
                        selected_total < TITAN_HOME_FOLDER_ITEM_CAPACITY;
                        index++
                    )
                    {
                        if (
                            TitanShellScene_CanSelectFolderCandidate(index) &&
                            !TitanShellScene_FolderCandidateSelected(index)
                        )
                        {
                            TitanShellScene_SetFolderCandidateSelected(index, 1);
                            selected_total++;
                        }
                    }
                }
                else if (g_scene.folder_content_side_selected == 1)
                {
                    memset(
                        g_scene.folder_selected_games,
                        0,
                        sizeof(g_scene.folder_selected_games)
                    );
                    memset(
                        g_scene.folder_selected_shell,
                        0,
                        sizeof(g_scene.folder_selected_shell)
                    );

                    if (
                        g_scene.folder_origin_game_index < TitanNeutrino_GetGameCount() &&
                        TitanShellScene_CanSelectGameForFolder(
                            g_scene.folder_origin_game_index
                        )
                    )
                    {
                        g_scene.folder_selected_games[
                            g_scene.folder_origin_game_index
                        ] = 1u;
                    }
                    else if (
                        g_scene.folder_origin_shell_catalog_index >= 0 &&
                        (unsigned int)g_scene.folder_origin_shell_catalog_index <
                            TITAN_FOLDER_SHELL_SELECTION_COUNT &&
                        TitanShellScene_CanSelectShellForFolder(
                            (unsigned int)g_scene.folder_origin_shell_catalog_index
                        )
                    )
                    {
                        g_scene.folder_selected_shell[
                            g_scene.folder_origin_shell_catalog_index
                        ] = 1u;
                    }
                }
                else
                {
                    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_EDITOR);
                    g_scene.folder_content_side_active = 0;
                }
            }
        }
        else
        {
            if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
            {
                if ((cursor % columns) > 0)
                {
                    cursor--;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
            {
                if (
                    (cursor % columns) == columns - 1 ||
                    (unsigned int)(cursor + 1) >= candidate_count
                )
                {
                    g_scene.folder_content_side_active = 1;
                }
                else
                {
                    cursor++;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                if (cursor - columns >= 0)
                {
                    cursor -= columns;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                if ((unsigned int)(cursor + columns) < candidate_count)
                {
                    cursor += columns;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                if (TitanShellScene_CanSelectFolderCandidate((unsigned int)cursor))
                {
                    if (
                        !TitanShellScene_FolderCandidateSelected((unsigned int)cursor) &&
                        TitanShellScene_CountFolderSelectedItems() >=
                            TITAN_HOME_FOLDER_ITEM_CAPACITY
                    )
                    {
                        TitanNotifications_Push(
                            TITAN_NOTIFICATION_WARNING,
                            "Conteudo",
                            "Folder item limit reached"
                        );
                    }
                    else
                    {
                        TitanShellScene_SetFolderCandidateSelected(
                            (unsigned int)cursor,
                            !TitanShellScene_FolderCandidateSelected((unsigned int)cursor)
                        );
                    }
                }
                else
                {
                    TitanNotifications_Push(
                        TITAN_NOTIFICATION_WARNING,
                        "Conteudo",
                        "This item already belongs to another folder"
                    );
                }
            }
            g_scene.folder_content_cursor = cursor;
        }
        return 1;
    }

    if (g_scene.folder_open)
    {
        const TitanHomeFolder *folder;

        folder = TitanHomeFolders_Get(g_scene.folder_open_index);
        if (folder == 0 || folder->item_count == 0u)
        {
            g_scene.folder_open = 0;
            return 1;
        }

        /* Do not let a second action interrupt the Orbis-style enter
           choreography. Circle may still reverse it immediately. */
        if (g_scene.folder_open_transition < 0.90f)
        {
            if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
            {
                g_scene.folder_open = 0;
            }
            return 1;
        }

        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            g_scene.folder_open = 0;
            return 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_SELECT)
        {
            TitanHomeEntry folder_entry;

            if (TitanShellScene_GetHomeEntry(
                (unsigned int)g_scene.selected_item,
                &folder_entry
            ))
            {
                TitanShellScene_OpenHomeContext(&folder_entry);
            }
            return 1;
        }
        if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
        {
            g_scene.folder_open_selected--;
            if (g_scene.folder_open_selected < 0)
            {
                g_scene.folder_open_selected = (int)folder->item_count - 1;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
        {
            g_scene.folder_open_selected++;
            if ((unsigned int)g_scene.folder_open_selected >= folder->item_count)
            {
                g_scene.folder_open_selected = 0;
            }
        }
        if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            TitanHomeGameKey key;
            unsigned int shell_index;
            const TitanShellItem *shell_item;
            unsigned int game_index;

            key = folder->item_keys[g_scene.folder_open_selected];
            if (TitanShellScene_FindShellByKey(key, &shell_index, &shell_item))
            {
                (void)shell_index;
                g_scene.folder_open = 0;
                TitanShellScene_OpenShellPage(
                    shell_item != 0 ? shell_item->page : TITAN_SHELL_PAGE_HOME
                );
            }
            else if (TitanShellScene_FindGameByKey(key, &game_index))
            {
                /* Keep the folder navigation scene alive during launch so the
                   selected child, not the parent folder tile, performs the
                   Orbis-style handoff animation. */
                TitanShellScene_BeginGameLaunch(game_index);
            }
            else
            {
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_WARNING,
                    "Conteudo indisponivel",
                    "The item is not present in the current catalog"
                );
            }
        }
        return 1;
    }

    if (g_scene.folder_open_transition > 0.02f)
    {
        return 1;
    }

    return 0;
}

static void TitanShellScene_UpdateSelectedAppAssets(void)
{
    TitanHomeEntry selected_home;

    /* Live-area app art belongs only to a stable HOME selection. Releasing it
     * on every functional/top/folder surface keeps the large 640x480 CPU
     * buffer transient and leaves runtime launch memory untouched. */
    if (g_scene.startup_active ||
        g_scene.game_launch_active ||
        g_scene.game_return_active ||
        g_scene.page_transition_target >= 0.5f ||
        g_scene.page_transition > 0.05f ||
        g_scene.top_menu_active ||
        g_scene.top_menu_transition > 0.05f ||
        g_scene.folder_open ||
        g_scene.folder_ui_mode != TITAN_FOLDER_UI_CLOSED)
    {
        TitanAppAssets_Select(TITAN_APP_ASSET_NO_SELECTION);
        return;
    }

    if (TitanShellScene_GetHomeEntry(
            (unsigned int)g_scene.selected_item,
            &selected_home) &&
        selected_home.type == TITAN_HOME_ENTRY_APPLICATION)
    {
        TitanAppAssets_Select(selected_home.application_index);
    }
    else
    {
        TitanAppAssets_Select(TITAN_APP_ASSET_NO_SELECTION);
    }
}

static void TitanShellScene_UpdateFixedStep(
    const TitanInputState *input
)
{
    unsigned int item_count;
    TitanHomeEntry selected_home;
    const TitanShellItem *selected;
    int has_activity;
    int selection_changed;
    float detail_target;
    float footer_target;

    if (!g_scene.initialized)
    {
        return;
    }

    g_scene.local_frame++;

    if (g_scene.game_launch_active)
    {
        g_scene.game_launch_frames++;
        if (g_scene.game_launch_frames >= TITAN_GAME_LAUNCH_TOTAL_FRAMES)
        {
            TitanShellScene_RunGameLaunch();
        }
        return;
    }

    if (g_scene.game_return_active)
    {
        g_scene.game_return_frames++;
        if (g_scene.game_return_frames >= TITAN_GAME_RETURN_TOTAL_FRAMES)
        {
            g_scene.game_return_active = 0;
            g_scene.game_return_frames = 0u;
            g_scene.selected_hold_frames = 0u;
            g_scene.detail_visibility = 0.0f;
        }
        return;
    }

    TitanShellScene_PlayNavigationAudio(input);

    if (g_scene.package_install_tracking)
    {
        TitanShellScene_PollPackageInstaller();
    }

    if (g_scene.video_test_active)
    {
        if (g_scene.video_test_frames_remaining > 0u)
        {
            g_scene.video_test_frames_remaining--;
        }
        else
        {
            TitanShellScene_CancelVideoTest("Tempo de confirmação esgotado");
        }
    }

    if (g_scene.startup_active)
    {
        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_INITIAL_BLACK)
        {
            g_scene.startup_phase_frames++;

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_BLACK_HOLD_FRAMES)
            {
                g_scene.startup_phase = TITAN_STARTUP_PHASE_HEALTH;
                g_scene.startup_phase_frames = 0u;

                /* RC5.6 H&S quiet mode: mute health-freeze trace while
                 * the Health & Safety screen is visible. This also makes
                 * nested Critical() calls true no-ops, avoiding USB flush
                 * traffic and the bounded 180 ms waits during this phase. */
                TitanHealthTrace_SetEnabled(0);
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_HEALTH_TRANSITION, 0u,
                    TITAN_STARTUP_PHASE_INITIAL_BLACK, TITAN_STARTUP_PHASE_HEALTH, 0u);
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_AUDIO_BEFORE, 0u, 0u, 0u, 0u);
                TitanAudio_StartMusic();
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_AUDIO_AFTER, 0u, 0u, 0u, 0u);
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_WIRELESS_BEFORE, 0u, 0u, 0u, 0u);
                TitanWirelessAccessories_BeginStartupReconnect();
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_WIRELESS_AFTER, 0u, 0u, 0u, 0u);
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HEALTH)
        {
            g_scene.startup_phase_frames++;
            TitanHealthTrace_Mark(TITAN_HT_STARTUP_HEALTH_FRAME,
                g_scene.startup_phase_frames, g_scene.startup_phase_frames,
                TITAN_STARTUP_HEALTH_DURATION_FRAMES, 0u);

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_HEALTH_DURATION_FRAMES)
            {
                TitanHealthTrace_Critical(TITAN_HT_STARTUP_WELCOME_TRANSITION,
                    g_scene.startup_phase_frames, TITAN_STARTUP_PHASE_HEALTH,
                    TITAN_STARTUP_PHASE_WELCOME_ENTER, 0u);
                g_scene.startup_phase = TITAN_STARTUP_PHASE_WELCOME_ENTER;
                g_scene.startup_phase_frames = 0u;

                /* H&S is no longer visible: restore normal tracing for
                 * Welcome, network, input, HOME and later UI. */
                TitanHealthTrace_SetEnabled(1);
                TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_WELCOME);
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_ENTER)
        {
            g_scene.startup_phase_frames++;

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_WELCOME_ENTER_FRAMES)
            {
                g_scene.startup_phase = TITAN_STARTUP_PHASE_WELCOME_WAIT;
                g_scene.startup_phase_frames = 0u;
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_WAIT)
        {
            const TitanInputDeviceDescriptor *candidate=TitanShellScene_WelcomeCandidate();
            if (TitanShellScene_WelcomeConfirm(candidate))
            {
                g_scene.startup_phase = TITAN_STARTUP_PHASE_WELCOME_EXIT;
                g_scene.startup_phase_frames = 0u;
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_WELCOME_EXIT)
        {
            g_scene.startup_phase_frames++;

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_WELCOME_EXIT_FRAMES)
            {
                g_scene.startup_phase = TITAN_STARTUP_PHASE_PROFILE_ENTER;
                g_scene.startup_phase_frames = 0u;
                TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_PROFILE);
                TitanShellScene_NotifyStorageBootStatus();
                g_scene.startup_profile_selected =
                    TitanUserProfiles_GetCount() < TITAN_USER_PROFILE_CAPACITY
                        ? (int)TitanUserProfiles_GetActiveIndex() + 1
                        : (int)TitanUserProfiles_GetActiveIndex();
                g_scene.startup_profile_position =
                    (float)g_scene.startup_profile_selected;
            }
        }
        else if (
            g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_ENTER ||
            g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_WAIT ||
            g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_EXIT
        )
        {
            unsigned int profile_count;
            int tile_count;
            int has_add_tile;

            profile_count = TitanUserProfiles_GetCount();
            has_add_tile = profile_count < TITAN_USER_PROFILE_CAPACITY;
            tile_count = (int)profile_count + (has_add_tile ? 1 : 0);

            g_scene.startup_profile_position =
                TitanAnimation_Approach(
                    g_scene.startup_profile_position,
                    (float)g_scene.startup_profile_selected,
                    0.28f
                );

            if (g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_ENTER)
            {
                g_scene.startup_phase_frames++;

                if (g_scene.startup_phase_frames >= TITAN_STARTUP_PROFILE_ENTER_FRAMES)
                {
                    g_scene.startup_phase = TITAN_STARTUP_PHASE_PROFILE_WAIT;
                    g_scene.startup_phase_frames = 0u;
                }
            }
            else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_PROFILE_EXIT)
            {
                g_scene.startup_phase_frames++;

                if (g_scene.startup_phase_frames >= TITAN_STARTUP_PROFILE_EXIT_FRAMES)
                {
                    g_scene.startup_phase = TITAN_STARTUP_PHASE_HOME_PAUSE;
                    g_scene.startup_phase_frames = 0u;
                }
            }
            else if (input != 0 && input->connected != 0)
            {
                if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
                {
                    g_scene.startup_profile_selected--;
                    if (g_scene.startup_profile_selected < 0)
                    {
                        g_scene.startup_profile_selected = tile_count - 1;
                    }
                }
                if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
                {
                    g_scene.startup_profile_selected++;
                    if (g_scene.startup_profile_selected >= tile_count)
                    {
                        g_scene.startup_profile_selected = 0;
                        g_scene.startup_profile_position = 0.0f;
                    }
                }
                if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
                {
                    if (has_add_tile && g_scene.startup_profile_selected == 0)
                    {
                        TitanShellScene_OpenProfileCreation(1);
                    }
                    else
                    {
                        unsigned int profile_index;

                        profile_index = (unsigned int)g_scene.startup_profile_selected;
                        if (has_add_tile)
                        {
                            profile_index--;
                        }
                        if (TitanUserProfiles_Select(profile_index))
                        {
                            TitanShellScene_SaveProfileState(0, 0, 0);
                            (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_PROFILE_ENTRY);
                            g_scene.startup_phase = TITAN_STARTUP_PHASE_PROFILE_EXIT;
                            g_scene.startup_phase_frames = 0u;
                        }
                    }
                }
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_PAUSE)
        {
            g_scene.startup_phase_frames++;

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_HOME_PAUSE_FRAMES)
            {
                g_scene.startup_phase = TITAN_STARTUP_PHASE_HOME_ENTER;
                g_scene.startup_phase_frames = 0u;
                g_scene.detail_visibility = 0.0f;
            }
        }
        else if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_ENTER)
        {
            float home_progress;
            float detail_progress;

            g_scene.startup_phase_frames++;
            home_progress = TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames,
                TITAN_STARTUP_HOME_ENTER_FRAMES
            );
            detail_progress = TitanAnimation_SmoothStep(
                TitanShellScene_Clamp(
                    (home_progress - 0.76f) / 0.24f,
                    0.0f,
                    1.0f
                )
            );
            g_scene.detail_visibility = detail_progress;

            if (g_scene.startup_phase_frames >= TITAN_STARTUP_HOME_ENTER_FRAMES)
            {
                g_scene.startup_active = 0;
                g_scene.startup_phase_frames = 0u;
                TitanWirelessAccessories_EndStartupReconnect();
                TitanUnifiedInput_SetBootUiActive(0);
                TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_HOME);
                g_scene.detail_visibility = 1.0f;
                g_scene.selected_hold_frames = TITAN_SHELL_DETAIL_DELAY_FRAMES;
            }
        }

        return;
    }

    has_activity =
        input != 0 &&
        (input->pressed != 0 || input->released != 0);

    if (has_activity)
    {
        g_scene.inactivity_frames = 0u;
        g_scene.footer_visibility = 0.0f;
    }
    else
    {
        g_scene.inactivity_frames++;
    }

    footer_target =
        g_scene.shell_move_active ||
        g_scene.inactivity_frames >= TITAN_SHELL_FOOTER_DELAY_FRAMES
        ? 1.0f
        : 0.0f;

    g_scene.footer_visibility =
        TitanAnimation_Approach(
            g_scene.footer_visibility,
            footer_target,
            0.10f
        );

    g_scene.selected_position =
        TitanUiFocus_Step(g_scene.selected_position, g_scene.selected_item, 0.34f);

    g_scene.top_menu_transition =
        TitanAnimation_Approach(
            g_scene.top_menu_transition,
            g_scene.top_menu_active ? 1.0f : 0.0f,
            0.18f
        );

    g_scene.top_menu_selected_position =
        TitanUiFocus_Step(g_scene.top_menu_selected_position, g_scene.top_menu_selected, 0.30f);

    g_scene.folder_context_transition =
        TitanAnimation_Approach(
            g_scene.folder_context_transition,
            g_scene.folder_ui_mode == TITAN_FOLDER_UI_CONTEXT ? 1.0f : 0.0f,
            0.15f
        );

    g_scene.resolution_options_transition =
        TitanAnimation_Approach(
            g_scene.resolution_options_transition,
            g_scene.folder_ui_mode == TITAN_FOLDER_UI_CONTEXT &&
                    g_scene.resolution_options_open
                ? 1.0f
                : 0.0f,
            0.18f
        );

    g_scene.patch_options_transition =
        TitanAnimation_Approach(
            g_scene.patch_options_transition,
            g_scene.folder_ui_mode == TITAN_FOLDER_UI_CONTEXT &&
                    g_scene.patch_options_open
                ? 1.0f
                : 0.0f,
            0.18f
        );

    g_scene.folder_screen_transition =
        TitanAnimation_Approach(
            g_scene.folder_screen_transition,
            1.0f,
            0.14f
        );

    g_scene.folder_open_transition =
        TitanAnimation_Approach(
            g_scene.folder_open_transition,
            g_scene.folder_open ? 1.0f : 0.0f,
            0.16f
        );

    g_scene.folder_open_selected_position =
        TitanAnimation_Approach(
            g_scene.folder_open_selected_position,
            (float)g_scene.folder_open_selected,
            0.32f
        );

    g_scene.settings_depth_transition =
        TitanAnimation_Approach(
            g_scene.settings_depth_transition,
            g_scene.settings_category_open ? 1.0f : 0.0f,
            0.18f
        );

    g_scene.settings_category_position =
        TitanAnimation_Approach(
            g_scene.settings_category_position,
            (float)g_scene.settings_selected_category,
            0.24f
        );

    g_scene.settings_row_position =
        TitanAnimation_Approach(
            g_scene.settings_row_position,
            (float)g_scene.settings_selected_row,
            0.24f
        );

    /* Every new Settings descendant deliberately reuses the native Settings
       timing/easing: 0.18 depth and 0.24 row focus. */
    g_scene.settings_leaf_transition =
        TitanAnimation_Approach(
            g_scene.settings_leaf_transition,
            g_scene.settings_leaf_open ? 1.0f : 0.0f,
            0.18f
        );
    g_scene.settings_leaf_row_position =
        TitanAnimation_Approach(
            g_scene.settings_leaf_row_position,
            (float)g_scene.settings_leaf_selected_row,
            0.24f
        );
    g_scene.wireless_detail_transition =
        TitanAnimation_Approach(
            g_scene.wireless_detail_transition,
            g_scene.wireless_detail_open ? 1.0f : 0.0f,
            0.18f
        );
    g_scene.wireless_detail_row_position =
        TitanAnimation_Approach(
            g_scene.wireless_detail_row_position,
            (float)g_scene.wireless_detail_selected_row,
            0.24f
        );
    g_scene.settings_action_row_position =
        TitanAnimation_Approach(
            g_scene.settings_action_row_position,
            (float)g_scene.settings_action_selected,
            0.24f
        );

    g_scene.package_installer_row_position =
        TitanAnimation_Approach(
            g_scene.package_installer_row_position,
            (float)g_scene.package_installer_selected_row,
            0.24f
        );

    g_scene.package_installer_transition =
        TitanAnimation_Approach(
            g_scene.package_installer_transition,
            g_scene.package_installer_open ? 1.0f : 0.0f,
            0.18f
        );

    g_scene.system_update_row_position =
        TitanAnimation_Approach(
            g_scene.system_update_row_position,
            (float)g_scene.system_update_selected_row,
            0.24f
        );

    g_scene.system_update_transition =
        TitanAnimation_Approach(
            g_scene.system_update_transition,
            g_scene.system_update_open ? 1.0f : 0.0f,
            0.18f
        );

    g_scene.network_page_row_position =
        TitanAnimation_Approach(
            g_scene.network_page_row_position,
            (float)g_scene.network_page_selected_row,
            0.24f
        );

    g_scene.network_page_transition =
        TitanAnimation_Approach(
            g_scene.network_page_transition,
            g_scene.network_page_mode != TITAN_NETWORK_PAGE_CLOSED ? 1.0f : 0.0f,
            0.18f
        );

    g_scene.power_row_position =
        TitanAnimation_Approach(
            g_scene.power_row_position,
            (float)g_scene.power_selected_row,
            0.24f
        );

    g_scene.notifications_row_position =
        TitanAnimation_Approach(
            g_scene.notifications_row_position,
            (float)g_scene.notifications_selected_row,
            0.24f
        );

    g_scene.user_profile_row_position =
        TitanAnimation_Approach(
            g_scene.user_profile_row_position,
            (float)g_scene.user_profile_selected_row,
            0.24f
        );

    g_scene.saved_data_row_position =
        TitanAnimation_Approach(
            g_scene.saved_data_row_position,
            (float)g_scene.saved_data_selected_row,
            0.24f
        );

    g_scene.applications_row_position =
        TitanAnimation_Approach(
            g_scene.applications_row_position,
            (float)g_scene.applications_selected_row,
            0.24f
        );

    g_scene.library_category_position =
        TitanAnimation_Approach(
            g_scene.library_category_position,
            (float)g_scene.library_selected_category,
            0.24f
        );

    g_scene.library_item_position =
        TitanAnimation_Approach(
            g_scene.library_item_position,
            (float)g_scene.library_selected_item,
            0.28f
        );

    g_scene.shutdown_screen_fade =
        TitanAnimation_Approach(
            g_scene.shutdown_screen_fade,
            g_scene.shutdown_screen_active ? 1.0f : 0.0f,
            0.10f
        );

    g_scene.page_transition =
        TitanShellScene_MoveTowards(
            g_scene.page_transition,
            g_scene.page_transition_target,
            TITAN_SHELL_PAGE_TRANSITION_STEP
        );

    if (
        g_scene.page_transition_target < 0.5f &&
        g_scene.page_transition < 0.05f &&
        !g_scene.top_menu_active &&
        g_scene.top_menu_transition < 0.05f &&
        TitanAnimation_Absolute(
            g_scene.selected_position -
            (float)g_scene.selected_item
        ) < 0.03f
    )
    {
        g_scene.selected_hold_frames++;
    }
    else
    {
        g_scene.selected_hold_frames = 0u;
    }

    detail_target =
        g_scene.selected_hold_frames >=
            TITAN_SHELL_DETAIL_DELAY_FRAMES
        ? 1.0f
        : 0.0f;

    g_scene.detail_visibility =
        TitanAnimation_Approach(
            g_scene.detail_visibility,
            detail_target,
            0.12f
        );

    TitanShellScene_UpdateSelectedAppAssets();

    if (input == 0 || !input->connected)
    {
        return;
    }

    if (TitanShellScene_UpdateFolderUi(input))
    {
        return;
    }

    if (TitanShellScene_UpdateShellMove(input))
    {
        return;
    }

    if (g_scene.shutdown_screen_active)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            g_scene.shutdown_screen_active = 0;
        }

        return;
    }

    if (g_scene.page_transition_target > 0.5f)
    {
        if (g_scene.page_transition < 0.95f)
        {
            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_BROWSER)
        {
            const TitanBrowserSnapshot *browser;

            browser = TitanBrowser_GetSnapshot();
            if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
            {
                if (
                    browser != 0 &&
                    (
                        browser->state == TITAN_BROWSER_LOADING ||
                        browser->state == TITAN_BROWSER_DOWNLOADING
                    )
                )
                {
                    TitanBrowser_Cancel();
                }
                else
                {
                    g_scene.page_transition_target = 0.0f;
                    g_scene.selected_hold_frames = 0u;
                    g_scene.detail_visibility = 0.0f;
                }
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
            {
                (void)TitanShellScene_OpenBrowserAddressInput();
                return;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_START)
            {
                TitanBrowser_GoHome();
                return;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_L1)
            {
                (void)TitanBrowser_GoBack();
                return;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_R1)
            {
                (void)TitanBrowser_GoForward();
                return;
            }
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                TitanBrowser_MoveLink(-1);
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                TitanBrowser_MoveLink(1);
            }
            if (input->pressed & TITAN_INPUT_BUTTON_L2)
            {
                TitanBrowser_Scroll(-5);
            }
            if (input->pressed & TITAN_INPUT_BUTTON_R2)
            {
                TitanBrowser_Scroll(5);
            }
            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                TitanShellScene_HandleBrowserAction(
                    TitanBrowser_ActivateSelected()
                );
                return;
            }
            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_LIBRARY)
        {
            unsigned int library_count;

            TitanShellScene_ClampLibrarySelection();
            library_count = TitanShellScene_GetLibraryEntryCount();

            if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
            {
                if (g_scene.library_grid_active)
                {
                    g_scene.library_grid_active = 0;
                }
                else
                {
                    g_scene.page_transition_target = 0.0f;
                    g_scene.selected_hold_frames = 0u;
                    g_scene.detail_visibility = 0.0f;
                }
                return;
            }

            if (!g_scene.library_grid_active)
            {
                if (input->pressed & TITAN_INPUT_BUTTON_UP)
                {
                    if (g_scene.library_selected_category > -1)
                    {
                        g_scene.library_selected_category--;
                        g_scene.library_selected_item = 0;
                        g_scene.library_item_position = 0.0f;
                    }
                }
                if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
                {
                    if (
                        g_scene.library_selected_category <
                            TITAN_LIBRARY_CATEGORY_COUNT - 1
                    )
                    {
                        g_scene.library_selected_category++;
                        g_scene.library_selected_item = 0;
                        g_scene.library_item_position = 0.0f;
                    }
                }

                if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
                {
                    if (g_scene.library_selected_category < 0)
                    {
                        (void)TitanShellScene_OpenLibrarySearchInput();
                    }
                    else if (library_count > 0u)
                    {
                        g_scene.library_grid_active = 1;
                    }
                    return;
                }

                if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
                {
                    if (
                        g_scene.library_selected_category < 0 &&
                        g_scene.library_search[0] == '\0'
                    )
                    {
                        (void)TitanShellScene_OpenLibrarySearchInput();
                    }
                    else if (library_count > 0u)
                    {
                        g_scene.library_grid_active = 1;
                    }
                    return;
                }
                return;
            }

            if (library_count == 0u)
            {
                g_scene.library_grid_active = 0;
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
            {
                if (
                    (g_scene.library_selected_item %
                        TITAN_LIBRARY_VISIBLE_COLUMNS) == 0
                )
                {
                    g_scene.library_grid_active = 0;
                }
                else
                {
                    g_scene.library_selected_item--;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
            {
                if (
                    (unsigned int)(g_scene.library_selected_item + 1) <
                        library_count &&
                    (g_scene.library_selected_item %
                        TITAN_LIBRARY_VISIBLE_COLUMNS) !=
                        TITAN_LIBRARY_VISIBLE_COLUMNS - 1
                )
                {
                    g_scene.library_selected_item++;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                if (
                    g_scene.library_selected_item -
                        TITAN_LIBRARY_VISIBLE_COLUMNS >= 0
                )
                {
                    g_scene.library_selected_item -=
                        TITAN_LIBRARY_VISIBLE_COLUMNS;
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                if (
                    (unsigned int)(g_scene.library_selected_item +
                        TITAN_LIBRARY_VISIBLE_COLUMNS) < library_count
                )
                {
                    g_scene.library_selected_item +=
                        TITAN_LIBRARY_VISIBLE_COLUMNS;
                }
                else
                {
                    /* On the final partial row, land on the last available
                     * item instead of trapping focus above the overflow. */
                    unsigned int last = library_count - 1u;
                    if (
                        last > (unsigned int)g_scene.library_selected_item &&
                        last / TITAN_LIBRARY_VISIBLE_COLUMNS >
                            (unsigned int)g_scene.library_selected_item /
                            TITAN_LIBRARY_VISIBLE_COLUMNS
                    )
                    {
                        g_scene.library_selected_item = (int)last;
                    }
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                TitanShellScene_OpenLibrarySelectedEntry();
                return;
            }
            TitanShellScene_ClampLibrarySelection();
            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_APPLICATIONS)
        {
            unsigned int application_count;

            application_count = TitanApplications_GetCount();

            if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
            {
                g_scene.page_transition_target = 0.0f;
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
            {
                if (!TitanStorageScheduler_IsLibraryScanBusy())
                {
                    (void)TitanStorageScheduler_RequestLibraryScan(1);
                    g_scene.applications_selected_row = 0;
                    g_scene.applications_row_position = 0.0f;
                }
                return;
            }

            if (
                application_count > 0u &&
                (input->pressed & TITAN_INPUT_BUTTON_UP)
            )
            {
                g_scene.applications_selected_row--;
                if (g_scene.applications_selected_row < 0)
                {
                    g_scene.applications_selected_row =
                        (int)application_count - 1;
                    g_scene.applications_row_position =
                        (float)g_scene.applications_selected_row;
                }
            }

            if (
                application_count > 0u &&
                (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            )
            {
                g_scene.applications_selected_row++;
                if (
                    (unsigned int)g_scene.applications_selected_row >=
                    application_count
                )
                {
                    g_scene.applications_selected_row = 0;
                    g_scene.applications_row_position = 0.0f;
                }
            }

            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                if (application_count == 0u)
                {
                    TitanNotifications_Push(
                        TITAN_NOTIFICATION_WARNING,
                        TitanLocalization_Compat("Applications"),
                        "No applications are installed"
                    );
                    return;
                }
                TitanShellScene_BeginApplicationLaunch(
                    (unsigned int)g_scene.applications_selected_row
                );
                return;
            }
            return;
        }


        if (g_scene.page == TITAN_SHELL_PAGE_INPUT)
        {
            if(input->pressed & TITAN_INPUT_BUTTON_CIRCLE){g_scene.page_transition_target=0.0f;g_scene.selected_hold_frames=0u;g_scene.detail_visibility=0.0f;return;}
            if(input->pressed & TITAN_INPUT_BUTTON_UP){g_scene.input_selected_row--;if(g_scene.input_selected_row<0)g_scene.input_selected_row=1;}
            if(input->pressed & TITAN_INPUT_BUTTON_DOWN){g_scene.input_selected_row++;if(g_scene.input_selected_row>1)g_scene.input_selected_row=0;}
            g_scene.input_row_position=TitanAnimation_Approach(g_scene.input_row_position,(float)g_scene.input_selected_row,0.24f);
            if(g_scene.input_selected_row==0 && (input->pressed&(TITAN_INPUT_BUTTON_LEFT|TITAN_INPUT_BUTTON_RIGHT))){
                TitanEarlyInputBackend b=TitanEarlyInput_GetConfiguredBackend();
                int dir=(input->pressed&TITAN_INPUT_BUTTON_LEFT)?-1:1;
                int v=(int)b+dir;if(v<0)v=2;if(v>2)v=0;
                if(TitanEarlyInput_SaveBackendPreference((TitanEarlyInputBackend)v)) TitanNotifications_Push(TITAN_NOTIFICATION_SUCCESS,"Controllers",v==2?"Bluetooth selected for next boot":(v==1?"USB selected for next boot":"Early modern-controller backend disabled"));
                else TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Controllers","Could not save startup input backend");
                return;
            }
            if(g_scene.input_selected_row==1 && (input->pressed&TITAN_INPUT_BUTTON_CROSS)){
                const TitanEarlyInputStatus *early=TitanEarlyInput_GetStatus();
                if(TitanEarlyInput_GetConfiguredBackend()!=TITAN_EARLY_INPUT_BT){
                    if(!TitanEarlyInput_SaveBackendPreference(TITAN_EARLY_INPUT_BT))
                        TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Bluetooth pairing","Could not save Bluetooth startup backend");
                    else
                        TitanNotifications_Push(TITAN_NOTIFICATION_INFO,"Bluetooth pairing","Bluetooth selected for next boot; reboot Titan, then pair");
                    return;
                }
                if(!early||early->state!=TITAN_EARLY_INPUT_STATE_READY){
                    char msg[112];snprintf(msg,sizeof(msg),"Bluetooth backend %s (module rc=%d); reboot/inspect startup diagnostics",early?TitanShellScene_EarlyInputStateName(early->state):"Unavailable",early?early->last_module_result:0);
                    TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Bluetooth pairing",msg);return;
                }
                if(TitanPademuInputBridge_StartBluetoothPairing()) TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY,"Bluetooth pairing","Starting PADEMU Bluetooth listening asynchronously");
                else TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Bluetooth pairing","Could not queue Bluetooth pairing request");
                return;
            }
            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_SETTINGS)
        {
            int row_count;

            if (TitanShellScene_UpdateNetworkPage(input))
            {
                return;
            }
            if (TitanShellScene_UpdateSystemUpdate(input))
            {
                return;
            }
            if (TitanShellScene_UpdatePackageInstaller(input))
            {
                return;
            }
            if (TitanShellScene_UpdateSettingsLeaf(input))
            {
                return;
            }

            row_count = TitanShellScene_GetSettingsRowCount(
                g_scene.settings_selected_category
            );
            if (row_count < 1) row_count = 1;
            if (g_scene.settings_selected_row >= row_count)
                g_scene.settings_selected_row = row_count - 1;
            if (g_scene.settings_selected_row < 0) g_scene.settings_selected_row = 0;

            if (
                g_scene.video_test_active &&
                (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            )
            {
                TitanShellScene_ConfirmVideoTest();
                return;
            }

            if (
                g_scene.video_test_active &&
                (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
            )
            {
                TitanShellScene_CancelVideoTest("Cancelado pelo usuário");
                return;
            }

            if (
                input->pressed &
                TITAN_INPUT_BUTTON_CIRCLE
            )
            {
                if (g_scene.settings_category_open)
                {
                    TitanShellScene_CloseSettingsCategoryAnimated();
                }
                else
                {
                    g_scene.page_transition_target = 0.0f;
                    g_scene.selected_hold_frames = 0u;
                    g_scene.detail_visibility = 0.0f;
                }

                return;
            }

            if (g_scene.settings_category_open)
            {
                if (g_scene.settings_selected_category == TITAN_SETTINGS_THEMES)
                {
                    int selected = g_scene.settings_selected_row;
                    int theme_count = (int)TitanTheme_GetCount();
                    int total_count = theme_count + 1; /* virtual installer tile */

                    if (selected < 0) selected = 0;
                    if (selected >= total_count) selected = total_count - 1;

                    if (selected == theme_count)
                    {
                        if (input->pressed & (TITAN_INPUT_BUTTON_UP | TITAN_INPUT_BUTTON_LEFT))
                            selected = theme_count > 0 ? theme_count - 1 : theme_count;
                        else if (input->pressed & (TITAN_INPUT_BUTTON_DOWN | TITAN_INPUT_BUTTON_RIGHT))
                            selected = theme_count > 0 ? 0 : theme_count;
                    }
                    else
                    {
                        int column = selected % TITAN_THEME_GRID_COLUMNS;
                        if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
                        {
                            int row_start = selected - column;
                            int row_end = row_start + TITAN_THEME_GRID_COLUMNS - 1;
                            if (row_end >= theme_count) row_end = theme_count - 1;
                            selected = column == 0 ? row_end : selected - 1;
                        }
                        if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
                        {
                            int row_start = selected - column;
                            int row_end = row_start + TITAN_THEME_GRID_COLUMNS - 1;
                            if (row_end >= theme_count) row_end = theme_count - 1;
                            selected = selected >= row_end ? row_start : selected + 1;
                        }
                        if (input->pressed & TITAN_INPUT_BUTTON_UP)
                        {
                            int candidate = selected - TITAN_THEME_GRID_COLUMNS;
                            if (candidate < 0)
                            {
                                int last_row_start = ((theme_count - 1) / TITAN_THEME_GRID_COLUMNS) * TITAN_THEME_GRID_COLUMNS;
                                candidate = last_row_start + column;
                                if (candidate >= theme_count) candidate = theme_count - 1;
                            }
                            selected = candidate;
                        }
                        if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
                        {
                            int candidate = selected + TITAN_THEME_GRID_COLUMNS;
                            selected = candidate >= theme_count ? theme_count : candidate;
                        }
                    }

                    g_scene.settings_selected_row = selected;
                    g_scene.settings_row_position = (float)selected;
                }
                else if (
                    input->pressed &
                    TITAN_INPUT_BUTTON_UP
                )
                {
                    g_scene.settings_selected_row--;

                    if (g_scene.settings_selected_row < 0)
                    {
                        g_scene.settings_selected_row = row_count - 1;
                    }
                }

                if (
                    g_scene.settings_selected_category != TITAN_SETTINGS_THEMES &&
                    (input->pressed & TITAN_INPUT_BUTTON_DOWN)
                )
                {
                    g_scene.settings_selected_row++;

                    if (g_scene.settings_selected_row >= row_count)
                    {
                        g_scene.settings_selected_row = 0;
                    }
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_USERS &&
                    g_scene.settings_selected_row == 0
                )
                {
                    if (
                        input->pressed &
                        TITAN_INPUT_BUTTON_LEFT
                    )
                    {
                        if (TitanUserProfiles_SelectPrevious())
                        {
                            TitanShellScene_SaveProfileState(0, 0, 0);
                        }
                    }

                    if (
                        input->pressed &
                        TITAN_INPUT_BUTTON_RIGHT
                    )
                    {
                        if (TitanUserProfiles_SelectNext())
                        {
                            TitanShellScene_SaveProfileState(0, 0, 0);
                        }
                    }
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_SAVED_DATA &&
                    g_scene.settings_selected_row == 4 &&
                    (
                        input->pressed &
                        (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT)
                    )
                )
                {
                    g_scene.memory_card_selected_port =
                        1 - g_scene.memory_card_selected_port;
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                    g_scene.settings_selected_row == 1 &&
                    (
                        input->pressed &
                        (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT)
                    )
                )
                {
                    TitanShellScene_ToggleDexMode();
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                    (input->pressed & (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT))
                )
                {
                    int direction = (input->pressed & TITAN_INPUT_BUTTON_LEFT) ? -1 : 1;
                    const TitanDateTimeSnapshot *dt = TitanDateTime_GetSnapshot();
                    if (g_scene.settings_selected_row == 6 && !dt->automatic)
                        (void)TitanDateTime_AdjustManualDays(direction);
                    else if (g_scene.settings_selected_row == 7 && !dt->automatic)
                        (void)TitanDateTime_AdjustManualSeconds((int64_t)direction * 60ll);
                    else if (g_scene.settings_selected_row == 9)
                        TitanDateTime_CycleTimezone(direction);
                    else if (g_scene.settings_selected_row == 10)
                        TitanDateTime_ToggleClockFormat();
                    else if (g_scene.settings_selected_row == 11)
                        TitanDateTime_CycleDateFormat(direction);
                    else if (g_scene.settings_selected_row == 13)
                        TitanLocalization_CycleLanguage(direction);
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_DEBUG &&
                    g_scene.settings_selected_row == 1 &&
                    (
                        input->pressed &
                        (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT)
                    )
                )
                {
                    TitanShellScene_ToggleDexMode();
                }

                if (
                    g_scene.settings_selected_category == TITAN_SETTINGS_THEMES &&
                    (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
                )
                {
                    unsigned int theme_count = TitanTheme_GetCount();
                    unsigned int selected = g_scene.settings_selected_row < 0
                        ? 0u : (unsigned int)g_scene.settings_selected_row;
                    if (selected < theme_count && TitanTheme_CanDelete(selected))
                    {
                        char error[96];
                        if (TitanTheme_Delete(selected, error, sizeof(error)))
                        {
                            unsigned int new_count = TitanTheme_GetCount();
                            TitanShellBackground_RefreshThemeAssets();
                            TitanAudio_ReloadThemeAssets();
                            if (TitanAudio_IsReady()) TitanAudio_StartMusic();
                            TitanStorageInventory_RequestRefresh();
                            if ((unsigned int)g_scene.settings_selected_row > new_count)
                                g_scene.settings_selected_row = (int)new_count;
                            g_scene.settings_row_position = (float)g_scene.settings_selected_row;
                            TitanNotifications_Push(
                                TITAN_NOTIFICATION_SUCCESS,
                                "Themes",
                                "Installed theme removed"
                            );
                        }
                        else
                        {
                            TitanNotifications_Push(
                                TITAN_NOTIFICATION_ERROR,
                                "Themes",
                                error[0] ? error : "Could not delete the theme"
                            );
                        }
                    }
                    return;
                }

                if (
                    input->pressed &
                    TITAN_INPUT_BUTTON_CROSS
                )
                {
                    if (g_scene.settings_selected_category == TITAN_SETTINGS_ACCESSORIES)
                    {
                        if (g_scene.settings_selected_row == 0)
                        {
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_WIRELESS);
                        }
                        else if (g_scene.settings_selected_row == 1)
                        {
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_CONTROLLER_ASSIGNMENT);
                        }
                        else if (g_scene.settings_selected_row == 2)
                        {
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_MULTITAP);
                        }
                        else
                        {
                            unsigned int source_index = 0u;
                            const TitanInputDeviceDescriptor *device =
                                TitanShellScene_GetWiredAccessory(
                                    (unsigned int)(g_scene.settings_selected_row - 3),
                                    &source_index);
                            if (device != 0)
                                TitanShellScene_ShowWiredAccessoryInformation(source_index);
                        }
                    }
                    else if (g_scene.settings_selected_category == TITAN_SETTINGS_STORAGE)
                    {
                        if (g_scene.settings_selected_row == 0)
                        {
                            const TitanStorageInventorySnapshot *snapshot = TitanStorageInventory_GetSnapshot();
                            char total[80], apps[80], games[80], themes[80];
                            TitanShellScene_FormatStorageBytes(snapshot->titan_bytes,
                                snapshot->titan_bytes_valid && !snapshot->scanning, total, sizeof(total));
                            { char tmp[80]; snprintf(tmp, sizeof(tmp), "%s", total); snprintf(total, sizeof(total), TitanLocalization_Compat("Titan usage: %s"), tmp); }
                            TitanShellScene_FormatStorageBytes(snapshot->application_bytes, !snapshot->scanning, apps, sizeof(apps));
                            { char tmp[80]; snprintf(tmp, sizeof(tmp), "%s", apps); snprintf(apps, sizeof(apps), TitanLocalization_Compat("Applications: %s"), tmp); }
                            TitanShellScene_FormatStorageBytes(snapshot->native_game_bytes, !snapshot->scanning, games, sizeof(games));
                            { char tmp[80]; snprintf(tmp, sizeof(tmp), "%s", games); snprintf(games, sizeof(games), TitanLocalization_Compat("Native games: %s"), tmp); }
                            TitanShellScene_FormatStorageBytes(snapshot->theme_bytes, !snapshot->scanning, themes, sizeof(themes));
                            { char tmp[80]; snprintf(tmp, sizeof(tmp), "%s", themes); snprintf(themes, sizeof(themes), TitanLocalization_Compat("Themes: %s"), tmp); }
                            (void)TitanNotifications_OpenInformation("Armazenamento Titan", total, apps, games, themes,
                                snapshot->scanning ? "Inventário: atualizando em background" : "Inventário: atualizado");
                        }
                        else if (g_scene.settings_selected_row == 1)
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_STORAGE_APPLICATIONS);
                        else if (g_scene.settings_selected_row == 2)
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_STORAGE_GAMES);
                        else if (g_scene.settings_selected_row == 3)
                            TitanShellScene_OpenSettingsLeaf(TITAN_SETTINGS_LEAF_STORAGE_THEMES);
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_THEMES
                    )
                    {
                        unsigned int theme_count = TitanTheme_GetCount();
                        unsigned int selected = g_scene.settings_selected_row < 0
                            ? 0u : (unsigned int)g_scene.settings_selected_row;
                        if (selected == theme_count)
                        {
                            TitanShellScene_OpenThemeInstaller();
                        }
                        else
                        {
                            const TitanThemeInfo *theme = TitanTheme_Get(selected);
                            if (theme != 0 && TitanTheme_Apply(selected))
                            {
                                TitanShellBackground_RefreshThemeAssets();
                                TitanAudio_ReloadThemeAssets();
                                if (TitanAudio_IsReady())
                                    TitanAudio_StartMusic();
                            }
                            else
                            {
                                TitanNotifications_Push(
                                    TITAN_NOTIFICATION_ERROR,
                                    "Themes",
                                    "Could not save the selected theme"
                                );
                            }
                        }
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_DEBUG
                    )
                    {
                        TitanShellScene_RunDebugSettingsAction(
                            g_scene.settings_selected_row
                        );
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_CONTENT &&
                        g_scene.settings_selected_row == 0
                    )
                    {
                        TitanShellScene_OpenNetworkPage(TITAN_NETWORK_PAGE_ASSET_WIZARD);
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_NETWORK
                    )
                    {
                        TitanShellScene_RunNetworkAction(g_scene.settings_selected_row);
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                        g_scene.settings_selected_row == 0
                    )
                    {
                        TitanShellScene_OpenSystemUpdate();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                        g_scene.settings_selected_row == 1
                    )
                    {
                        TitanShellScene_ToggleDexMode();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                        g_scene.settings_selected_row == 8
                    )
                    {
                        const TitanDateTimeSnapshot *dt = TitanDateTime_GetSnapshot();
                        int enable_automatic = dt != 0 && !dt->automatic;
                        TitanDateTime_SetAutomatic(enable_automatic);
                        if (enable_automatic) TitanDateTime_RequestSync();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                        g_scene.settings_selected_row == 12
                    )
                    {
                        TitanDateTime_RequestSync();
                        TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY, "Internet Time", "Synchronization requested");
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&
                        g_scene.settings_selected_row == 13
                    )
                    {
                        TitanLocalization_CycleLanguage(1);
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_VIDEO &&
                        g_scene.settings_selected_row == 2
                    )
                    {
                        TitanShellScene_StartVideoTest();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_USERS &&
                        g_scene.settings_selected_row == 2
                    )
                    {
                        TitanShellScene_OpenProfileCreation(0);
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_USERS &&
                        g_scene.settings_selected_row == 3
                    )
                    {
                        if (TitanUserProfiles_RemoveActive())
                        {
                            TitanShellScene_SaveProfileState(0, 1, 1);
                        }
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SAVED_DATA &&
                        g_scene.settings_selected_row <= 1
                    )
                    {
                        TitanShellScene_VerifyActiveVmc();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SAVED_DATA &&
                        g_scene.settings_selected_row == 2
                    )
                    {
                        TitanShellScene_BeginVmcFormat();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SAVED_DATA &&
                        g_scene.settings_selected_row == 3
                    )
                    {
                        TitanShellScene_BeginVmcRecreate();
                    }
                    else if (
                        g_scene.settings_selected_category == TITAN_SETTINGS_SAVED_DATA &&
                        g_scene.settings_selected_row == 4
                    )
                    {
                        TitanShellScene_BeginPhysicalFormat();
                    }
                }
            }
            else
            {
                int category_count;

                category_count = TitanShellScene_GetSettingsCategoryCount();

                if (
                    input->pressed &
                    TITAN_INPUT_BUTTON_UP
                )
                {
                    g_scene.settings_selected_category--;

                    if (g_scene.settings_selected_category < 0)
                    {
                        g_scene.settings_selected_category =
                            category_count - 1;
                    }
                }

                if (
                    input->pressed &
                    TITAN_INPUT_BUTTON_DOWN
                )
                {
                    g_scene.settings_selected_category++;

                    if (
                        g_scene.settings_selected_category >=
                        category_count
                    )
                    {
                        g_scene.settings_selected_category = 0;
                    }
                }

                if (
                    input->pressed &
                    TITAN_INPUT_BUTTON_CROSS
                )
                {
                    TitanShellScene_OpenSettingsCategoryAnimated();
                }
            }

            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_POWER)
        {
            if (
                input->pressed &
                TITAN_INPUT_BUTTON_CIRCLE
            )
            {
                g_scene.page_transition_target = 0.0f;
                g_scene.selected_hold_frames = 0u;
                g_scene.detail_visibility = 0.0f;
                return;
            }

            if (
                input->pressed &
                TITAN_INPUT_BUTTON_UP
            )
            {
                g_scene.power_selected_row--;

                if (g_scene.power_selected_row < 0)
                {
                    g_scene.power_selected_row = 2;
                }
            }

            if (
                input->pressed &
                TITAN_INPUT_BUTTON_DOWN
            )
            {
                g_scene.power_selected_row++;

                if (g_scene.power_selected_row > 2)
                {
                    g_scene.power_selected_row = 0;
                    g_scene.power_row_position = 0.0f;
                }
            }

            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                TitanShellScene_RunPowerAction();
            }

            return;
        }


        if (g_scene.page == TITAN_SHELL_PAGE_NOTIFICATIONS)
        {
            if (
                input->pressed &
                TITAN_INPUT_BUTTON_CIRCLE
            )
            {
                g_scene.page_transition_target = 0.0f;
                g_scene.selected_hold_frames = 0u;
                g_scene.detail_visibility = 0.0f;
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.notifications_selected_row--;
                if (g_scene.notifications_selected_row < 0)
                {
                    g_scene.notifications_selected_row = 4;
                }
            }

            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.notifications_selected_row++;
                if (g_scene.notifications_selected_row > 4)
                {
                    g_scene.notifications_selected_row = 0;
                    g_scene.notifications_row_position = 0.0f;
                }
            }

            if (
                (input->pressed & TITAN_INPUT_BUTTON_CROSS) &&
                g_scene.notifications_selected_row == 4
            )
            {
                const TitanBackgroundActivityStatus *activity;

                activity = TitanNotifications_GetBackgroundActivity();
                if (activity != 0 && activity->state != TITAN_BACKGROUND_ACTIVITY_RUNNING)
                    TitanNotifications_ClearBackgroundActivity();
                {
                    const TitanBackgroundActivityStatus *downloads = TitanNotifications_GetDownloadActivity();
                    if (downloads != 0 && downloads->state != TITAN_BACKGROUND_ACTIVITY_RUNNING)
                        TitanNotifications_ClearDownloadActivity();
                }
            }

            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_USER_PROFILE)
        {
            if (
                input->pressed &
                TITAN_INPUT_BUTTON_CIRCLE
            )
            {
                g_scene.page_transition_target = 0.0f;
                g_scene.selected_hold_frames = 0u;
                g_scene.detail_visibility = 0.0f;
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.user_profile_selected_row--;
                if (g_scene.user_profile_selected_row < 0)
                {
                    g_scene.user_profile_selected_row = 3;
                }
            }

            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.user_profile_selected_row++;
                if (g_scene.user_profile_selected_row > 3)
                {
                    g_scene.user_profile_selected_row = 0;
                    g_scene.user_profile_row_position = 0.0f;
                }
            }

            if (
                g_scene.user_profile_selected_row == 0 &&
                (input->pressed & TITAN_INPUT_BUTTON_LEFT)
            )
            {
                if (TitanUserProfiles_SelectPrevious())
                {
                    TitanShellScene_SaveProfileState(0, 0, 0);
                }
            }
            if (
                g_scene.user_profile_selected_row == 0 &&
                (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
            )
            {
                if (TitanUserProfiles_SelectNext())
                {
                    TitanShellScene_SaveProfileState(0, 0, 0);
                }
            }
            if (input->pressed & TITAN_INPUT_BUTTON_SQUARE)
            {
                TitanShellScene_OpenProfileCreation(0);
            }
            if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
            {
                if (TitanUserProfiles_RemoveActive())
                {
                    TitanShellScene_SaveProfileState(0, 1, 1);
                }
            }

            if (
                g_scene.user_profile_selected_row == 3 &&
                (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            )
            {
                TitanShellScene_VerifyActiveVmc();
            }

            return;
        }

        if (g_scene.page == TITAN_SHELL_PAGE_SAVED_DATA)
        {
            if (
                input->pressed &
                TITAN_INPUT_BUTTON_CIRCLE
            )
            {
                g_scene.page_transition_target = 0.0f;
                g_scene.selected_hold_frames = 0u;
                g_scene.detail_visibility = 0.0f;
                return;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_UP)
            {
                g_scene.saved_data_selected_row--;
                if (g_scene.saved_data_selected_row < 0)
                {
                    g_scene.saved_data_selected_row = 4;
                }
            }

            if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
            {
                g_scene.saved_data_selected_row++;
                if (g_scene.saved_data_selected_row > 4)
                {
                    g_scene.saved_data_selected_row = 0;
                    g_scene.saved_data_row_position = 0.0f;
                }
            }

            if (
                g_scene.saved_data_selected_row == 4 &&
                (
                    input->pressed &
                    (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT)
                )
            )
            {
                g_scene.memory_card_selected_port =
                    1 - g_scene.memory_card_selected_port;
            }

            if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
            {
                if (g_scene.saved_data_selected_row <= 1)
                {
                    TitanShellScene_VerifyActiveVmc();
                }
                else if (g_scene.saved_data_selected_row == 2)
                {
                    TitanShellScene_BeginVmcFormat();
                }
                else if (g_scene.saved_data_selected_row == 3)
                {
                    TitanShellScene_BeginVmcRecreate();
                }
                else
                {
                    TitanShellScene_BeginPhysicalFormat();
                }
            }

            return;
        }

        if (
            input->pressed &
            TITAN_INPUT_BUTTON_CIRCLE
        )
        {
            g_scene.page_transition_target = 0.0f;
            g_scene.selected_hold_frames = 0u;
            g_scene.detail_visibility = 0.0f;
            return;
        }

        return;
    }

    if (g_scene.page_transition > 0.05f)
    {
        return;
    }

    if (g_scene.top_menu_active)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_SELECT)
        {
            TitanShellScene_OpenSystemMenuContext();
            return;
        }

        if (
            input->pressed &
            (TITAN_INPUT_BUTTON_DOWN | TITAN_INPUT_BUTTON_CIRCLE)
        )
        {
            g_scene.top_menu_active = 0;
            g_scene.selected_hold_frames = 0u;
            g_scene.detail_visibility = 0.0f;
            return;
        }

        if (
            input->pressed &
            TITAN_INPUT_BUTTON_LEFT
        )
        {
            unsigned int system_count;
            system_count = TitanShellCatalog_GetSystemCount();
            if (system_count > 0u)
            {
                g_scene.top_menu_selected--;
                if (g_scene.top_menu_selected < 0)
                {
                    g_scene.top_menu_selected = (int)system_count - 1;
                    g_scene.top_menu_selected_position =
                        (float)g_scene.top_menu_selected;
                }
            }
        }

        if (
            input->pressed &
            TITAN_INPUT_BUTTON_RIGHT
        )
        {
            unsigned int system_count;
            system_count = TitanShellCatalog_GetSystemCount();
            if (system_count > 0u)
            {
                g_scene.top_menu_selected++;
                if ((unsigned int)g_scene.top_menu_selected >= system_count)
                {
                    g_scene.top_menu_selected = 0;
                    g_scene.top_menu_selected_position = 0.0f;
                }
            }
        }

        if (
            input->pressed &
            TITAN_INPUT_BUTTON_CROSS
        )
        {
            selected = TitanShellCatalog_GetSystem(
                TitanShellLayout_GetSystemCatalogIndex(
                    (unsigned int)g_scene.top_menu_selected
                )
            );

            if (selected != 0)
            {
                TitanShellScene_OpenShellPage(selected->page);
            }
        }

        return;
    }

    if (
        input->pressed &
        TITAN_INPUT_BUTTON_UP
    )
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_TOP_MENU_NAV);
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_THEME_TOP_ENTER);
        g_scene.top_menu_active = 1;
        g_scene.top_menu_selected = 0;
        g_scene.top_menu_selected_position = 0.0f;
        g_scene.selected_hold_frames = 0u;
        g_scene.detail_visibility = 0.0f;
        return;
    }

    item_count = TitanShellScene_GetHomeEntryCount();

    if (item_count == 0u)
    {
        return;
    }

    TitanShellScene_ClampHomeSelection();
    selection_changed = 0;

    if (input->pressed & TITAN_INPUT_BUTTON_SELECT)
    {
        if (
            TitanShellScene_GetHomeEntry(
                (unsigned int)g_scene.selected_item,
                &selected_home
            )
        )
        {
            TitanShellScene_OpenHomeContext(&selected_home);
        }
        return;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
    {
        if (!TitanStorageScheduler_IsLibraryScanBusy())
        {
            (void)TitanStorageScheduler_RequestLibraryScan(1);
            TitanShellScene_ClampHomeSelection();
        }
        return;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
    {
        if (g_scene.selected_item > 0)
        {
            g_scene.selected_item--;
            selection_changed = 1;
        }
    }

    if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
    {
        if (g_scene.selected_item + 1 < (int)item_count)
        {
            g_scene.selected_item++;
            selection_changed = 1;
        }
    }

    if (selection_changed)
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_HOME_GRID_NAV);
        g_scene.selected_hold_frames = 0u;
        g_scene.detail_visibility = 0.0f;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        if (
            !TitanShellScene_GetHomeEntry(
                (unsigned int)g_scene.selected_item,
                &selected_home
            )
        )
        {
            return;
        }

        if (selected_home.type == TITAN_HOME_ENTRY_FOLDER)
        {
            const TitanHomeFolder *folder;

            folder = TitanHomeFolders_Get(selected_home.folder_index);
            if (folder != 0 && folder->item_count > 0u)
            {
                g_scene.folder_open = 1;
                g_scene.folder_open_index = selected_home.folder_index;
                g_scene.folder_open_selected = 0;
                g_scene.folder_open_selected_position = 0.0f;
                g_scene.selected_hold_frames = 0u;
                g_scene.detail_visibility = 0.0f;
            }
            return;
        }

        if (selected_home.type == TITAN_HOME_ENTRY_GAME)
        {
            TitanShellScene_BeginGameLaunch(selected_home.game_index);
            return;
        }
        if (selected_home.type == TITAN_HOME_ENTRY_DISC)
        {
            TitanShellScene_BeginDiscLaunch();
            return;
        }

        if (selected_home.type == TITAN_HOME_ENTRY_APPLICATION)
        {
            TitanShellScene_BeginApplicationLaunch(selected_home.application_index);
            return;
        }

        TitanShellScene_OpenShellPage(selected_home.page);
    }
}


void TitanShellScene_BeginGameReturn(unsigned int game_index)
{
    unsigned int index;
    unsigned int count;
    TitanHomeEntry entry;

    if (!g_scene.initialized)
    {
        return;
    }

    count = TitanShellScene_GetHomeEntryCount();
    for (index = 0u; index < count; ++index)
    {
        if (
            TitanShellScene_GetHomeEntry(index, &entry) &&
            entry.type == TITAN_HOME_ENTRY_GAME &&
            entry.game_index == game_index
        )
        {
            g_scene.selected_item = (int)index;
            g_scene.selected_position = (float)index;
            break;
        }
    }

    g_scene.page = TITAN_SHELL_PAGE_HOME;
    g_scene.page_transition = 0.0f;
    g_scene.page_transition_target = 0.0f;
    g_scene.top_menu_active = 0;
    g_scene.top_menu_transition = 0.0f;
    TitanShellScene_SetFolderUiMode(TITAN_FOLDER_UI_CLOSED);
    g_scene.folder_open = 0;
    g_scene.detail_visibility = 0.0f;
    g_scene.selected_hold_frames = 0u;
    g_scene.game_return_active = 1;
    g_scene.game_return_frames = 0u;
}

void TitanShellScene_Update(
    const TitanInputState *input
)
{
    unsigned long long now_ms;
    unsigned int delta_ms;
    unsigned int steps;
    unsigned int step_index;
    TitanInputState neutral_input;
    const TitanInputState *step_input;

    if (!g_scene.initialized)
    {
        return;
    }

    if (input != 0 && (input->pressed != 0u || input->released != 0u))
        TitanAssetService_NotifyUserActivity();
    TitanAssetService_SetAutomaticIdleEligible(
        g_scene.page == TITAN_SHELL_PAGE_HOME &&
        !g_scene.top_menu_active &&
        !g_scene.folder_open
    );

    TitanShellScene_PollStorageScheduler();
    /* Wireless pairing may finish after the user leaves the discovery page.
     * Keep its async state machine alive globally; it issues RPC queries only
     * while wireless Settings are visible or a user-requested connection is pending. */
    TitanWirelessAccessories_Update();

    now_ms = TitanTime_NowMilliseconds();
    delta_ms = TitanTime_ClampedDeltaMilliseconds(
        g_scene.last_update_ms,
        now_ms,
        100u
    );
    g_scene.last_update_ms = now_ms;

    if (TitanAppRuntime_OwnsShell())
    {
        /* Titan Apps own the shell content/update surface while launching or active, but
         * remain inside the same kernel process. Global notifications and the
         * optional DEX overlay continue to be composed by Project Titan. */
        TitanAppRuntime_Update(input, delta_ms);
        return;
    }

    /* Fixed 60 Hz simulation gives PAL, NTSC and 720p the same durations. */
    g_scene.fixed_step_accumulator += delta_ms * 60u;
    steps = g_scene.fixed_step_accumulator / 1000u;
    g_scene.fixed_step_accumulator %= 1000u;

    if (steps > 6u)
    {
        steps = 6u;
    }

    if (steps == 0u)
    {
        return;
    }

    if (input != 0)
    {
        neutral_input = *input;
        neutral_input.pressed = 0u;
        neutral_input.released = 0u;
    }
    else
    {
        memset(&neutral_input, 0, sizeof(neutral_input));
    }

    for (step_index = 0u; step_index < steps; ++step_index)
    {
        step_input = step_index == 0u ? input : &neutral_input;
        TitanShellScene_UpdateFixedStep(step_input);
    }
}


static void TitanShellScene_UpdateThemeLifecycle(void)
{
    TitanThemeLifecyclePhase phase = TITAN_THEME_LIFECYCLE_HOME;
    float progress = 1.0f;
    if (g_scene.startup_active)
    {
        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_ENTER)
        {
            phase = TITAN_THEME_LIFECYCLE_HOME_ENTER;
            progress = TitanShellScene_FrameProgress(g_scene.startup_phase_frames, TITAN_STARTUP_HOME_ENTER_FRAMES);
        }
        else
        {
            phase = TITAN_THEME_LIFECYCLE_PRE_HOME;
            progress = 0.0f;
        }
    }
    else if (g_scene.game_return_active)
    {
        phase = TITAN_THEME_LIFECYCLE_RETURN_HOME;
        progress = TitanShellScene_FrameProgress(g_scene.game_return_frames, TITAN_GAME_RETURN_TOTAL_FRAMES);
    }
    else if (g_scene.game_launch_active)
    {
        phase = TITAN_THEME_LIFECYCLE_HOME_EXIT;
        progress = TitanShellScene_FrameProgress(g_scene.game_launch_frames, TITAN_GAME_LAUNCH_TOTAL_FRAMES);
    }
    else if (TitanShellScene_IsFolderScreenMode(g_scene.folder_ui_mode))
    {
        phase = TITAN_THEME_LIFECYCLE_SUBMENU;
        progress = 1.0f;
    }
    else if (g_scene.page != TITAN_SHELL_PAGE_HOME)
    {
        /* RC1 hardware: page identifies the destination/last functional page
         * and intentionally remains non-HOME while page_transition animates
         * back to zero. Treating every non-HOME page as SUBMENU therefore
         * killed scene-v4 at the exact moment HOME became visible: the custom
         * artwork flashed, scene_opacity dropped to zero and Flow/Waves won.
         *
         * Drive lifecycle from transition direction while a functional page
         * is crossing the HOME boundary. This also restores the complementary
         * scene<->Flow crossfade already implemented by the background renderer. */
        if (g_scene.page_transition_target < 0.5f)
        {
            phase = TITAN_THEME_LIFECYCLE_RETURN_HOME;
            progress = 1.0f - TitanShellScene_Clamp(g_scene.page_transition, 0.0f, 1.0f);
        }
        else if (g_scene.page_transition < 0.999f)
        {
            phase = TITAN_THEME_LIFECYCLE_HOME_EXIT;
            progress = TitanShellScene_Clamp(g_scene.page_transition, 0.0f, 1.0f);
        }
        else
        {
            phase = TITAN_THEME_LIFECYCLE_SUBMENU;
            progress = 1.0f;
        }
    }
    else if (g_scene.top_menu_active || g_scene.top_menu_transition > 0.001f)
    {
        phase = TITAN_THEME_LIFECYCLE_TOP;
        progress = g_scene.top_menu_transition;
    }
    TitanThemeLifecycle_SetPhase(phase, progress);
    if (TitanThemeLifecycle_DynamicSceneAllowed())
        TitanShellBackground_RequestDynamicActivation();
    if (TitanTheme_GetAudioStyle()->music_home_only && TitanThemeLifecycle_MusicAllowed())
        TitanAudio_StartMusic();
}

void TitanShellScene_Render(
    const TitanContext *context,
    const TitanInputState *input
)
{
    unsigned int frame;
    float wave_transition;
    float home_visibility;
    float home_exit_transition;
    float page_approach_transition;
    float home_scale;
    float page_scale;
    float page_vertical_offset;
    float theme_horizontal_motion;
    float theme_vertical_transition;
    float theme_system_view_transition;
    const TitanThemeShellStyle *theme_style;
    unsigned char home_alpha;
    unsigned char page_alpha;
    int folder_fullscreen_active;

    frame =
        context != 0
            ? context->frame_count
            : g_scene.local_frame;

    TitanShellScene_UpdateThemeLifecycle();

    folder_fullscreen_active =
        TitanShellScene_IsFolderScreenMode(g_scene.folder_ui_mode) ||
        TitanShellScene_IsFolderScreenMode(g_scene.folder_previous_ui_mode);

    /* scene-v2 keeps its full artwork on HOME/top-menu. Only the explicitly
     * functional surfaces requested by the theme contract reduce to Flow. */
    theme_system_view_transition = 0.0f;
    if (folder_fullscreen_active)
    {
        theme_system_view_transition = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(g_scene.folder_screen_transition, 0.0f, 1.0f)
        );
    }
    else if (
        g_scene.page == TITAN_SHELL_PAGE_SETTINGS ||
        g_scene.page == TITAN_SHELL_PAGE_USER_PROFILE ||
        g_scene.page == TITAN_SHELL_PAGE_SAVED_DATA ||
        g_scene.page == TITAN_SHELL_PAGE_LIBRARY ||
        g_scene.page == TITAN_SHELL_PAGE_APPLICATIONS ||
        g_scene.page == TITAN_SHELL_PAGE_OVERVIEW ||
        g_scene.page == TITAN_SHELL_PAGE_APP_HOME ||
        g_scene.page == TITAN_SHELL_PAGE_FTP_HOME ||
        g_scene.page == TITAN_SHELL_PAGE_BROWSER ||
        g_scene.page == TITAN_SHELL_PAGE_STORAGE ||
        g_scene.page == TITAN_SHELL_PAGE_INPUT ||
        g_scene.page == TITAN_SHELL_PAGE_NOTIFICATIONS ||
        g_scene.page == TITAN_SHELL_PAGE_POWER ||
        g_scene.page == TITAN_SHELL_PAGE_ABOUT
    )
    {
        /* Functional shell pages deliberately reduce scene-v2 HOME artwork
         * to the active theme palette + Flow/Ribbon. Keep the list explicit:
         * it is both the presentation contract and a guard against a future
         * HOME-like page being silently classified as system UI. TOP MENU is
         * not a TitanShellPage transition and therefore keeps full artwork. */
        theme_system_view_transition = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(g_scene.page_transition, 0.0f, 1.0f)
        );
    }

    theme_style = TitanTheme_GetShellStyle();
    theme_horizontal_motion = TitanShellScene_Clamp(
        TitanAnimation_Absolute(
            g_scene.selected_position - (float)g_scene.selected_item
        ) * 1.85f *
        TitanShellScene_Clamp(theme_style->horizontal_motion_scale, 0.50f, 1.60f),
        0.0f,
        1.0f
    );
    theme_vertical_transition = TitanShellScene_Clamp(
        g_scene.top_menu_transition *
        TitanShellScene_Clamp(theme_style->vertical_motion_scale, 0.50f, 1.60f),
        0.0f,
        1.0f
    );

    TitanSystemInfo_Initialize(context);
    TitanVideo_ResetTransform();

    if (TitanAppRuntime_OwnsShell())
    {
        if (!TitanAppRuntime_IsFullscreen())
        {
            /* Orbis-style native app composition: keep a frozen Home/live-area
             * behind the application and use it as the visual frame. Input and
             * animation state remain owned by the app until it exits. */
            TitanShellBackground_Draw(frame, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            TitanShellScene_DrawHeader(frame, input, 188u);
            TitanShellScene_DrawHome(0.0f, 158u, 1.0f, 1.0f);
            TitanVideo_DrawRectAlpha(
                0.0f, 0.0f,
                (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
                0, 0, 0, 62u
            );
        }

        TitanAppRuntime_Draw(frame);
        TitanShellScene_DrawDexOverlay();
        return;
    }

    if (g_scene.startup_active)
    {
        TitanShellScene_DrawStartupSequence(frame);

        if (g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_ENTER)
        {
            float home_progress;
            float header_reveal;
            float focus_reveal;
            unsigned char header_alpha;

            home_progress = TitanShellScene_FrameProgress(
                g_scene.startup_phase_frames,
                TITAN_STARTUP_HOME_ENTER_FRAMES
            );
            header_reveal = TitanAnimation_EaseOutCubic(
                TitanShellScene_Clamp(
                    home_progress / 0.24f,
                    0.0f,
                    1.0f
                )
            );
            focus_reveal = TitanAnimation_EaseOutCubic(
                TitanShellScene_Clamp(
                    (home_progress - 0.55f) / 0.30f,
                    0.0f,
                    1.0f
                )
            );
            header_alpha = TitanShellScene_Alpha(255.0f * header_reveal);

            if (header_alpha > 4u)
            {
                TitanShellScene_DrawHeader(
                    frame,
                    input,
                    header_alpha
                );
            }

            TitanShellScene_DrawHome(
                0.0f,
                255,
                focus_reveal,
                home_progress
            );
        }

        return;
    }

    if (
        g_scene.shutdown_screen_active ||
        g_scene.shutdown_screen_fade > 0.01f
    )
    {
        unsigned char shutdown_alpha;

        shutdown_alpha = TitanShellScene_Alpha(255.0f * g_scene.shutdown_screen_fade);

        TitanShellBackground_Draw(frame, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

        if (shutdown_alpha > 0u)
        {
            TitanShellScene_DrawShutdownScreen(frame, shutdown_alpha);
        }

        return;
    }

    if (g_scene.game_launch_active)
    {
        float stage_progress;
        float black_progress;
        float badge_in;
        float badge_out;
        float badge_visibility;
        unsigned char header_alpha;
        unsigned char stage_dimmer_alpha;
        unsigned char black_alpha;
        unsigned char badge_alpha;

        stage_progress = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_STAGE_IN_START,
            TITAN_GAME_LAUNCH_STAGE_IN_END
        );
        black_progress = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_BLACK_FADE_START,
            TITAN_GAME_LAUNCH_BLACK_FADE_END
        );
        badge_in = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_BADGE_IN_FRAME,
            TITAN_GAME_LAUNCH_BADGE_IN_END_FRAME
        );
        badge_out = TitanShellScene_GameLaunchProgress(
            TITAN_GAME_LAUNCH_BADGE_OUT_FRAME,
            TITAN_GAME_LAUNCH_BADGE_OUT_END_FRAME
        );
        badge_visibility = badge_in * (1.0f - badge_out);

        if (!(g_scene.game_launch_application &&
              TitanShellScene_DrawApplicationLiveBackground(
                  frame,
                  g_scene.game_launch_application_index,
                  255u)))
        {
            TitanShellBackground_Draw(frame, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        }

        /* Orbis live-area takeover: the wallpaper recedes before the selected
           content settles in place. This remains render-only; no engine is
           touched until the full choreography has completed. */
        stage_dimmer_alpha = TitanShellScene_Alpha(108.0f * stage_progress);
        if (stage_dimmer_alpha > 0u)
        {
            TitanVideo_DrawRectAlpha(
                0.0f,
                0.0f,
                (float)TITAN_UI_WIDTH,
                (float)TITAN_UI_HEIGHT,
                0,
                0,
                0,
                stage_dimmer_alpha
            );
        }

        header_alpha = TitanShellScene_Alpha(
            255.0f * (1.0f - TitanShellScene_GameLaunchProgress(
                TITAN_GAME_LAUNCH_LABEL_FADE_START,
                TITAN_GAME_LAUNCH_LABEL_FADE_END
            ))
        );
        if (header_alpha > 4u)
        {
            TitanShellScene_DrawHeader(frame, input, header_alpha);
        }

        TitanShellScene_DrawHome(0.0f, 255u, 1.0f, 1.0f);

        black_alpha = TitanShellScene_Alpha(255.0f * black_progress);
        if (black_alpha > 0u)
        {
            TitanVideo_DrawRectAlpha(
                0.0f,
                0.0f,
                (float)TITAN_UI_WIDTH,
                (float)TITAN_UI_HEIGHT,
                0,
                0,
                0,
                black_alpha
            );
        }

        badge_alpha = TitanShellScene_Alpha(255.0f * badge_visibility);
        TitanShellScene_DrawEngineBadge(badge_alpha);
        TitanShellScene_DrawTitanEnhanced(badge_alpha);
        TitanShellScene_DrawCheatTaunt(badge_alpha);
        return;
    }

    if (g_scene.game_return_active)
    {
        float progress;
        float background_reveal;
        float selected_reveal;
        float home_reveal;
        unsigned char home_alpha;
        unsigned char header_alpha;
        unsigned char black_alpha;

        progress = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(
                (float)g_scene.game_return_frames /
                (float)TITAN_GAME_RETURN_TOTAL_FRAMES,
                0.0f,
                1.0f
            )
        );
        background_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp((progress - 0.08f) / 0.34f, 0.0f, 1.0f)
        );
        selected_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp((progress - 0.30f) / 0.30f, 0.0f, 1.0f)
        );
        home_reveal = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp((progress - 0.48f) / 0.40f, 0.0f, 1.0f)
        );

        TitanShellBackground_Draw(frame, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        black_alpha = TitanShellScene_Alpha(255.0f * (1.0f - background_reveal));
        if (black_alpha > 0u)
        {
            TitanVideo_DrawRectAlpha(
                0.0f, 0.0f,
                (float)TITAN_UI_WIDTH, (float)TITAN_UI_HEIGHT,
                0, 0, 0, black_alpha
            );
        }

        home_alpha = TitanShellScene_Alpha(
            255.0f * (0.35f * selected_reveal + 0.65f * home_reveal)
        );
        if (home_alpha > 4u)
        {
            TitanShellScene_DrawHome(0.0f, home_alpha, selected_reveal, 1.0f);
        }
        header_alpha = TitanShellScene_Alpha(255.0f * home_reveal);
        if (header_alpha > 4u)
        {
            TitanShellScene_DrawHeader(frame, input, header_alpha);
        }
        return;
    }

    if (
        g_scene.page == TITAN_SHELL_PAGE_OVERVIEW &&
        (
            g_scene.page_transition > 0.001f ||
            g_scene.page_transition_target > 0.5f
        )
    )
    {
        float overview_content_transition;
        unsigned char overview_content_alpha;

        overview_content_transition = TitanAnimation_SmoothStep(
            TitanShellScene_Clamp(
                (g_scene.page_transition - 0.28f) / 0.72f,
                0.0f,
                1.0f
            )
        );

        overview_content_alpha = TitanShellScene_Alpha(
            255.0f * overview_content_transition
        );

        TitanShellBackground_Draw(
            frame,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            theme_system_view_transition
        );

        TitanShellScene_DrawHeader(
            frame,
            input,
            255
        );

        TitanShellScene_DrawHome(
            0.0f,
            255,
            1.0f,
            1.0f
        );

        if (overview_content_alpha > 4)
        {
            TitanShellScene_DrawOverviewPage(
                context,
                input,
                0.0f,
                overview_content_alpha
            );
        }
        TitanShellScene_DrawDexOverlay();

        TitanShellScene_DrawFooter();
        return;
    }

    /* BuildFix23 CP05: all transition transforms are derived from the immutable
       base state plus the current scalar progress. No frame-to-frame transform
       accumulation is permitted. */
    {
        TitanUiTransitionSample transition_sample;
        TitanUiTransition_Sample(
            g_scene.page_transition,
            g_scene.page_transition_target > 0.5f,
            &transition_sample
        );
        home_exit_transition = 1.0f - transition_sample.source_visibility;
        wave_transition = transition_sample.wave_progress;
        page_approach_transition = transition_sample.destination_visibility;
        home_scale = transition_sample.source_scale;
        page_scale = transition_sample.destination_scale;
        page_vertical_offset = transition_sample.destination_offset_y;
    }

    {
        int selected_app_background_drawn = 0;
        TitanHomeEntry selected_home;

        if (g_scene.page_transition_target < 0.5f &&
            g_scene.page_transition < 0.05f &&
            !g_scene.top_menu_active &&
            g_scene.top_menu_transition < 0.05f &&
            !folder_fullscreen_active &&
            !g_scene.folder_open &&
            TitanShellScene_GetHomeEntry(
                (unsigned int)g_scene.selected_item,
                &selected_home) &&
            selected_home.type == TITAN_HOME_ENTRY_APPLICATION)
        {
            selected_app_background_drawn =
                TitanShellScene_DrawApplicationLiveBackground(
                    frame,
                    selected_home.application_index,
                    255u
                );
        }

        if (!selected_app_background_drawn)
        {
            TitanShellBackground_Draw(
                frame,
                wave_transition,
                (
                    g_scene.page_transition_target < 0.5f &&
                    !g_scene.top_menu_active &&
                    g_scene.inactivity_frames >= 600u
                ) ? 1.0f : 0.0f,
                theme_horizontal_motion,
                theme_vertical_transition,
                theme_system_view_transition
            );
        }
    }

    if (!folder_fullscreen_active)
    {
        TitanShellScene_DrawHeader(
            frame,
            input,
            255
        );
    }

    home_visibility =
        1.0f - home_exit_transition;

    home_alpha = TitanShellScene_Alpha(
        255.0f * home_visibility
    );

    page_alpha = TitanShellScene_Alpha(
        255.0f * page_approach_transition
    );

    if (!folder_fullscreen_active && home_alpha > 4)
    {
        TitanVideo_SetTransform(
            home_scale,
            (float)TITAN_UI_WIDTH * 0.5f,
            (float)TITAN_UI_HEIGHT * 0.5f,
            0.0f,
            -10.0f * home_exit_transition
        );

        TitanShellScene_DrawHome(
            0.0f,
            home_alpha,
            1.0f,
            1.0f
        );

        TitanShellScene_DrawSystemMenu(
            0.0f,
            home_alpha
        );

        TitanVideo_ResetTransform();
    }

    TitanShellScene_DrawFolderUi();

    if (page_alpha > 4)
    {
        TitanVideo_SetTransform(
            page_scale,
            (float)TITAN_UI_WIDTH * 0.5f,
            (float)TITAN_UI_HEIGHT * 0.5f,
            0.0f,
            page_vertical_offset
        );

        TitanShellScene_DrawPage(
            context,
            input,
            0.0f,
            page_alpha
        );

        TitanVideo_ResetTransform();
    }
        TitanShellScene_DrawDexOverlay();

    if (g_scene.folder_ui_mode == TITAN_FOLDER_UI_CLOSED)
    {
        TitanShellScene_DrawFooter();
    }
}
