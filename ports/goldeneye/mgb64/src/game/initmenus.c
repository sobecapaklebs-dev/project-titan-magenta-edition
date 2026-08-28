#include <ultra64.h>
#include <memp.h>
#include "front.h"
#include "fr.h"
#include "initmenus.h"
#include "initintromatrices.h"
#include <macro.h>

#ifdef NATIVE_PORT
#ifdef PROJECT_TITAN_TGR
#include "platform/sdl_titan_compat.h"
#else
#include <SDL.h>
#endif
#include "blood_animation.h"
#include "ramromreplay.h"
#include "textrelated.h"

/* ===== PC Level Selector ===== */

static const struct {
    LEVELID id;
    const char *name;
} pc_level_table[] = {
    { LEVELID_DAM,       "Dam" },
    { LEVELID_FACILITY,  "Facility" },
    { LEVELID_RUNWAY,    "Runway" },
    { LEVELID_SURFACE,   "Surface 1" },
    { LEVELID_BUNKER1,   "Bunker 1" },
    { LEVELID_SILO,      "Silo" },
    { LEVELID_FRIGATE,   "Frigate" },
    { LEVELID_SURFACE2,  "Surface 2" },
    { LEVELID_BUNKER2,   "Bunker 2" },
    { LEVELID_STATUE,    "Statue" },
    { LEVELID_ARCHIVES,  "Archives" },
    { LEVELID_STREETS,   "Streets" },
    { LEVELID_DEPOT,     "Depot" },
    { LEVELID_TRAIN,     "Train" },
    { LEVELID_JUNGLE,    "Jungle" },
    { LEVELID_CONTROL,   "Control" },
    { LEVELID_CAVERNS,   "Caverns" },
    { LEVELID_CRADLE,    "Cradle" },
    { LEVELID_TEMPLE,    "Temple" },
    { LEVELID_COMPLEX,   "Complex" },
    { LEVELID_AZTEC,     "Aztec" },
    { LEVELID_EGYPT,     "Egyptian" },
};
#define PC_NUM_LEVELS (sizeof(pc_level_table) / sizeof(pc_level_table[0]))

static s32 pc_selector_cursor = 0;
static s32 pc_selector_active = 0;
static u8  pc_key_prev_up = 0, pc_key_prev_down = 0, pc_key_prev_enter = 0;

static const char *pc_difficulty_names[] = { "Agent", "Secret Agent", "00 Agent" };
static s32 pc_difficulty_cursor = 0;
static s32 pc_selecting_difficulty = 0;
static s32 pc_auto_level_index = -2;
static s32 pc_auto_difficulty = DIFFICULTY_AGENT;
static s32 pc_auto_delay = 0;
static s32 pc_auto_frame = 0;

static s32 portEnvEnabled(const char *name)
{
    const char *env = getenv(name);

    if (env == NULL || env[0] == '\0') {
        return FALSE;
    }

    if (env[0] == '0' && env[1] == '\0') {
        return FALSE;
    }

    return TRUE;
}

static s32 portUsePcSelector(void)
{
    static s32 cached = -1;

    if (cached < 0) {
        cached = portEnvEnabled("GE007_PC_SELECTOR");

        if (!cached) {
            const char *auto_level_env = getenv("GE007_AUTO_SELECT_LEVEL");
            cached = (auto_level_env != NULL && auto_level_env[0] != '\0');
        }
    }

    return cached;
}

void pcPrimePostStageMenuForDirectBoot(LEVELID level, s32 multiplayer)
{
    /* MENU_RUN_STAGE normally primes the return menu before gameplay loads.
     * PC direct-boot paths skip that frontend frame, so preserve the same
     * post-stage route explicitly. */
    if (multiplayer) {
        frontChangeMenu(MENU_MP_OPTIONS, TRUE);
    } else if (level == LEVELID_CUBA) {
        do_extended_cast_display(TRUE);
        frontChangeMenu(MENU_DISPLAY_CAST, TRUE);
    } else {
        frontChangeMenu(MENU_MISSION_FAILED, TRUE);
    }
}

static char pc_ascii_tolower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }

    return c;
}

static void pc_build_token(const char *src, char *dst, s32 dstlen)
{
    s32 i = 0;

    if (dstlen <= 0) {
        return;
    }

    while (*src != '\0' && i < dstlen - 1) {
        char c = pc_ascii_tolower(*src++);

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            dst[i++] = c;
        }
    }

    dst[i] = '\0';
}

static s32 pc_parse_int(const char *text, s32 *out)
{
    s32 value = 0;
    s32 sign = 1;
    s32 saw_digit = FALSE;

    if (text == NULL || *text == '\0') {
        return FALSE;
    }

    if (*text == '-') {
        sign = -1;
        text++;
    }

    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return FALSE;
        }

        saw_digit = TRUE;
        value = value * 10 + (*text - '0');
        text++;
    }

    if (!saw_digit) {
        return FALSE;
    }

    *out = value * sign;
    return TRUE;
}

static s32 pc_find_auto_level_index(const char *value)
{
    s32 parsed;
    char needle[32];
    s32 i;

    if (value == NULL || *value == '\0') {
        return -1;
    }

    if (pc_parse_int(value, &parsed)) {
        if (parsed >= 0 && parsed < (s32)PC_NUM_LEVELS) {
            return parsed;
        }

        for (i = 0; i < (s32)PC_NUM_LEVELS; i++) {
            if ((s32)pc_level_table[i].id == parsed) {
                return i;
            }
        }

        return -1;
    }

    pc_build_token(value, needle, sizeof(needle));

    for (i = 0; i < (s32)PC_NUM_LEVELS; i++) {
        char candidate[32];
        pc_build_token(pc_level_table[i].name, candidate, sizeof(candidate));

        if (strcmp(needle, candidate) == 0) {
            return i;
        }
    }

    return -1;
}

static s32 pc_find_auto_difficulty(const char *value)
{
    s32 parsed;
    char token[32];

    if (value == NULL || *value == '\0') {
        return DIFFICULTY_AGENT;
    }

    if (pc_parse_int(value, &parsed)) {
        if (parsed >= DIFFICULTY_AGENT && parsed <= DIFFICULTY_00) {
            return parsed;
        }

        return DIFFICULTY_AGENT;
    }

    pc_build_token(value, token, sizeof(token));

    if (strcmp(token, "agent") == 0) {
        return DIFFICULTY_AGENT;
    }

    if (strcmp(token, "secretagent") == 0 || strcmp(token, "sa") == 0) {
        return DIFFICULTY_SECRET;
    }

    if (strcmp(token, "00agent") == 0 || strcmp(token, "00") == 0 || strcmp(token, "double0agent") == 0) {
        return DIFFICULTY_00;
    }

    return DIFFICULTY_AGENT;
}

static void pc_apply_level_selection(LEVELID level, s32 diff)
{
    extern void fileSetCurrentFolder(u32);
    extern void set_selected_difficulty(s32);
    extern void set_solo_and_ptr_briefing(s32);
    extern void bossSetLoadedStage(LEVELID);
    extern void lvlSetSelectedDifficulty(s32);
    extern s32 getmusictrack_or_randomtrack(s32);
    extern void lvlPlayMusicTrack1(s32);

    fileSetCurrentFolder(FOLDER1);
    set_selected_difficulty(diff);
    selected_stage = level;
    selected_difficulty = diff;
    set_solo_and_ptr_briefing(selected_stage);
    bossSetLoadedStage(selected_stage);
    pcPrimePostStageMenuForDirectBoot(selected_stage, FALSE);
    lvlSetSelectedDifficulty(diff);
    lvlPlayMusicTrack1(getmusictrack_or_randomtrack(level));
}

/* Native-port direct-boot into a split-screen multiplayer match, bypassing the
 * frontend the same way pc_apply_level_selection() does for solo. We drive the
 * engine's own MP-setup helpers (reset/init_mp_options_for_scenario) and the
 * multi_stage_setups[] table rather than hand-rolling MP state, so the match is
 * configured exactly as it would be coming out of the MP options menu. */
static void pc_apply_mp_selection(s32 num_players,
                                  s32 mp_stage,
                                  MPSCENARIOS scenarioid,
                                  s32 timelimit_secs)
{
    extern struct mp_stage_setup multi_stage_setups[];
    extern void reset_mp_options_for_scenario(MPSCENARIOS scenarioid);
    extern void init_mp_options_for_scenario(s32 numplayers);
    extern s32 check_if_mp_stage_unlocked(s32 stage);
    extern void bossSetLoadedStage(LEVELID);
    extern s32 getmusictrack_or_randomtrack(s32);
    extern void lvlPlayMusicTrack1(s32);
    extern void lvlSetSelectedDifficulty(s32);
    extern u32 randomGetNext(void);
    extern s32 g_pcMpTimeLimitOverrideSecs;
    s32 i;

    /* All player slots default to the first control style (1.1 "Honey"); the
     * BSS array is already zeroed, but set it explicitly so a deterministic
     * headless run does not depend on prior frontend state. */
    for (i = 0; i < num_players; i++) {
        controlstyle_player[i] = CONTROLLER_CONFIG_HONEY;
    }

    gamemode = GAMEMODE_MULTI;
    fileSetCurrentFolder(FOLDER1);

    /* Choose the scenario first so reset_mp_options_for_scenario() establishes
     * the per-scenario weapon-set/game-length defaults, then size the match to
     * the requested player count (init clamps to >= 2 and re-validates the stage
     * and scenario player limits for us). */
    reset_mp_options_for_scenario(scenarioid);
    MP_stage_selected = mp_stage;
    init_mp_options_for_scenario(num_players);
    g_pcMpTimeLimitOverrideSecs = (timelimit_secs > 0) ? timelimit_secs : 0;

    if (check_if_mp_stage_unlocked(MP_stage_selected) == FALSE) {
        MP_stage_selected = MP_STAGE_TEMPLE;
    }

    /* Resolve MP_STAGE_RANDOM the same way interface_menu0E_mpoptions() does on
     * Start: pick a random unlocked non-random stage. */
    if (multi_stage_setups[MP_stage_selected].stage_id < 0) {
        s32 pick;
        do {
            pick = 1 + (s32)(randomGetNext() % (u32)(MP_STAGE_SELECTED_MAX - 1));
        } while (check_if_mp_stage_unlocked(pick) == 0);
        selected_stage = multi_stage_setups[pick].stage_id;
    } else {
        selected_stage = multi_stage_setups[MP_stage_selected].stage_id;
    }

    selected_difficulty = DIFFICULTY_AGENT;
    briefingpage = -1;

    /* Hand the resolved stage to the boss main loop, which polls g_MainStageNum
     * and loads the stage directly — the same exit the solo direct-boot uses. */
    bossSetLoadedStage(selected_stage);
    pcPrimePostStageMenuForDirectBoot(selected_stage, TRUE);
    lvlSetSelectedDifficulty(selected_difficulty);
    lvlPlayMusicTrack1(getmusictrack_or_randomtrack(selected_stage));
}

/* Called each frame when the level selector is active.
 * Returns the display list pointer. Sets selected_stage when done. */
Gfx *pc_level_selector_constructor(Gfx *gdl)
{
    extern uintptr_t ptrFontZurichBold;
    extern uintptr_t ptrFontZurichBoldChars;

    const u8 *keys = SDL_GetKeyboardState(NULL);
    u8 up    = keys[SDL_SCANCODE_UP]   || keys[SDL_SCANCODE_W];
    u8 down  = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
    u8 enter = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE];
    u8 back  = keys[SDL_SCANCODE_ESCAPE];
    if (pc_auto_level_index == -2) {
        const char *auto_level_env = getenv("GE007_AUTO_SELECT_LEVEL");
        const char *auto_diff_env = getenv("GE007_AUTO_SELECT_DIFFICULTY");
        const char *auto_delay_env = getenv("GE007_AUTO_SELECT_DELAY");

        pc_auto_level_index = auto_level_env ? pc_find_auto_level_index(auto_level_env) : -1;
        pc_auto_difficulty = pc_find_auto_difficulty(auto_diff_env);
        pc_auto_delay = auto_delay_env ? SDL_atoi(auto_delay_env) : 1;

        if (pc_auto_delay < 0) {
            pc_auto_delay = 0;
        }
    }

    if (pc_auto_level_index >= 0) {
        pc_selector_cursor = pc_auto_level_index;
        pc_difficulty_cursor = pc_auto_difficulty;

        if (pc_auto_frame++ >= pc_auto_delay) {
            pc_apply_level_selection(pc_level_table[pc_selector_cursor].id, pc_difficulty_cursor);
            pc_selector_active = 0;
            return gdl;
        }
    } else if (!pc_selecting_difficulty) {
        /* Level selection mode */
        if (up && !pc_key_prev_up) {
            pc_selector_cursor--;
            if (pc_selector_cursor < 0) pc_selector_cursor = PC_NUM_LEVELS - 1;
        }
        if (down && !pc_key_prev_down) {
            pc_selector_cursor++;
            if (pc_selector_cursor >= (s32)PC_NUM_LEVELS) pc_selector_cursor = 0;
        }
        if (enter && !pc_key_prev_enter) {
            pc_selecting_difficulty = 1;
            pc_difficulty_cursor = 0;
        }
    } else {
        /* Difficulty selection mode */
        if (up && !pc_key_prev_up) {
            pc_difficulty_cursor--;
            if (pc_difficulty_cursor < 0) pc_difficulty_cursor = 2;
        }
        if (down && !pc_key_prev_down) {
            pc_difficulty_cursor++;
            if (pc_difficulty_cursor > 2) pc_difficulty_cursor = 0;
        }
        if (back && !pc_key_prev_enter) {
            pc_selecting_difficulty = 0;
        }
        if (enter && !pc_key_prev_enter) {
            /* Selection made — start the level */
            LEVELID level = pc_level_table[pc_selector_cursor].id;
            s32 diff = pc_difficulty_cursor;

            pc_apply_level_selection(level, diff);
            pc_selector_active = 0;
            return gdl;
        }
    }

    pc_key_prev_up = up;
    pc_key_prev_down = down;
    pc_key_prev_enter = enter;

    /* === Render the selector === */

    /* Safety: video system and fonts must be initialized */
    if (!ptrFontZurichBold || !ptrFontZurichBoldChars) {
        return gdl;
    }

    gdl = insert_imageDL(gdl);
    gDPPipeSync(gdl++);

    /* Dark blue background — direct GBI commands (viSetFillColor crashes
     * because g_ViBackData isn't initialized for title screen).
     * Use 16-bit RGBA5551 fill color. */
    gDPSetCycleType(gdl++, G_CYC_FILL);
    {
        u16 bg_col = GPACK_RGBA5551(16, 24, 64, 1);
        gDPSetFillColor(gdl++, (bg_col << 16) | bg_col);
    }
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    gDPPipeSync(gdl++);
    gdl = microcode_constructor(gdl);

    s32 x, y;
    s32 screen_w = (s32)viGetX();
    s32 screen_h = (s32)viGetY();

    if (!pc_selecting_difficulty) {
        /* Title */
        x = 40; y = 20;
        gdl = textRender(gdl, &x, &y, "GOLDENEYE 007",
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);

        /* Subtitle */
        x = 40; y = 40;
        gdl = textRender(gdl, &x, &y, "Select Mission",
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);

        /* Level list — show visible window around cursor */
        s32 visible_rows = 11;
        s32 start = pc_selector_cursor - visible_rows / 2;
        if (start < 0) start = 0;
        if (start + visible_rows > (s32)PC_NUM_LEVELS)
            start = (s32)PC_NUM_LEVELS - visible_rows;
        if (start < 0) start = 0;

        for (s32 i = start; i < start + visible_rows && i < (s32)PC_NUM_LEVELS; i++) {
            s32 row = i - start;
            y = 60 + row * 16;

            if (i == pc_selector_cursor) {
                /* Selected: render with arrow prefix */
                x = 44;
                gdl = textRender(gdl, &x, &y, ">",
                                 ptrFontZurichBoldChars, ptrFontZurichBold,
                                 -1, (s16)screen_w, (s16)screen_h, 0, 0);
            }
            x = 60;
            y = 60 + row * 16;
            gdl = textRender(gdl, &x, &y,
                             (char *)pc_level_table[i].name,
                             ptrFontZurichBoldChars, ptrFontZurichBold,
                             -1, (s16)screen_w, (s16)screen_h, 0, 0);
        }

        /* Footer */
        x = 40; y = screen_h - 20;
        gdl = textRender(gdl, &x, &y, "ENTER: Select   UP/DOWN: Navigate",
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);
    } else {
        /* Difficulty selection */
        x = 40; y = 30;
        gdl = textRender(gdl, &x, &y, (char *)pc_level_table[pc_selector_cursor].name,
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);

        x = 40; y = 55;
        gdl = textRender(gdl, &x, &y, "Select Difficulty",
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);

        for (s32 i = 0; i < 3; i++) {
            y = 85 + i * 22;

            if (i == pc_difficulty_cursor) {
                x = 60;
                gdl = textRender(gdl, &x, &y, ">",
                                 ptrFontZurichBoldChars, ptrFontZurichBold,
                                 -1, (s16)screen_w, (s16)screen_h, 0, 0);
            }
            x = 80;
            y = 85 + i * 22;

            gdl = textRender(gdl, &x, &y,
                             (char *)pc_difficulty_names[i],
                             ptrFontZurichBoldChars, ptrFontZurichBold,
                             -1, (s16)screen_w, (s16)screen_h, 0, 0);
        }

        x = 40; y = screen_h - 20;
        gdl = textRender(gdl, &x, &y, "ENTER: Start   ESC: Back",
                         ptrFontZurichBoldChars, ptrFontZurichBold,
                         -1, (s16)screen_w, (s16)screen_h, 0, 0);
    }

    return gdl;
}

s32 pc_level_selector_is_active(void) {
    return pc_selector_active;
}

#endif /* NATIVE_PORT */


void init_menus_or_reset(void)
{
    s32 i;
    current_menu = MENU_INVALID;
    maybe_prev_menu = MENU_INVALID;
    screen_size = SCREEN_SIZE_320x240;
#ifdef NATIVE_PORT
    /* PC: no N64 framebuffer switching — spectrum flag blocks menu transitions */
    spectrum_related_flag = FALSE;
#else
    spectrum_related_flag = TRUE;
#endif
    is_emulating_spectrum = FALSE;
    folder_selection_screen_option_icon = 0;
    folder_selected_for_deletion = -1;
    folder_selected_for_deletion_choice = 1;
    tab_start_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_prev_highlight = FALSE;
    maybe_is_in_menu = TRUE;

#ifdef NATIVE_PORT
    {
        extern const char *g_pcStartRamrom;
        extern int g_pcDirectBootLevelActive;
        extern int g_pcStartLevel;
        extern int g_pcStartMultiplayer;

        if (g_pcStartRamrom != NULL) {
            const char *start_ramrom = g_pcStartRamrom;
            g_pcStartRamrom = NULL;
            g_pcDirectBootLevelActive = 0;
            if (!pcRamromStartReplayByName(start_ramrom)) {
                menu_update = MENU_FILE_SELECT;
            }
        } else if (g_pcStartMultiplayer) {
            extern int g_pcStartMpPlayers;
            extern int g_pcStartMpStage;
            extern int g_pcStartMpScenario;
            extern int g_pcStartMpTimeLimitSecs;
            s32 mp_players = g_pcStartMpPlayers;
            s32 mp_stage = g_pcStartMpStage;
            MPSCENARIOS mp_scenario = (MPSCENARIOS)g_pcStartMpScenario;
            s32 mp_timelimit = g_pcStartMpTimeLimitSecs;
            g_pcStartMultiplayer = 0;
            g_pcDirectBootLevelActive = 0;
            pc_apply_mp_selection(mp_players, mp_stage, mp_scenario, mp_timelimit);
        } else if (g_pcStartLevel >= 0) {
            extern int g_pcStartDifficulty;
            LEVELID start_level = (LEVELID)g_pcStartLevel;
            s32 start_difficulty = g_pcStartDifficulty;
            g_pcStartLevel = -1;
            g_pcDirectBootLevelActive = 1;
            pc_apply_level_selection(start_level, start_difficulty);
        } else if (portUsePcSelector()) {
            /* Explicit PC-only selector for automation/dev workflows.
             * Default player boots should follow the original menu path. */
            g_pcDirectBootLevelActive = 0;
            pc_selector_cursor = 0;
            pc_difficulty_cursor = DIFFICULTY_AGENT;
            pc_selecting_difficulty = 0;
            pc_key_prev_up = 0;
            pc_key_prev_down = 0;
            pc_key_prev_enter = 0;
            pc_auto_frame = 0;
            pc_selector_active = 1;
        } else {
            if (prev_keypresses)
            {
                menu_update = MENU_FILE_SELECT;
            }

            if ((s32)menu_update < 0)
            {
                menu_update = MENU_FILE_SELECT;
            }
        }
    }
#else
    if (prev_keypresses)
    {
        menu_update = MENU_FILE_SELECT;
    }

    if ((s32)menu_update < 0)
    {
        menu_update = MENU_FILE_SELECT;
    }
#endif

    ptr_logo_and_walletbond_DL = (u8 *)mempAllocBytesInBank(0x78000, MEMPOOL_STAGE);

#if defined(VERSION_EU)
    ptr_menu_videobuffer = (uintptr_t)mempAllocBytesInBank(0x55040, MEMPOOL_STAGE);
#else
    ptr_menu_videobuffer = (uintptr_t)mempAllocBytesInBank(0x4b040, MEMPOOL_STAGE);
#endif

    ptr_menu_videobuffer = ALIGN64_V1(ptr_menu_videobuffer);

    for (i = 0; i < 4; i++)
    {
        walletinst[i] = NULL;
    }

    alloc_intro_matrices();
}
