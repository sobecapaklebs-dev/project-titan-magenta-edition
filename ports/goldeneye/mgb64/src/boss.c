#include <os_extension.h>
#include <PR/os.h>
#include "bondview.h"
#include <bondconstants.h>
#include <bondgame.h>
#include "boss.h"
#include "deb.h"
#include "debugmenu.h"
#include "game/dyn.h"
#include "game/game_debug.h"
#include "game/file.h"
#include "game/file2.h"
#include "game/indy_comms.h"
#include "init.h"
#include "joy.h"
#include "game/lvl.h"
#include "game/front.h"
#include "mema.h"
#include "memp.h"
#include "music.h"
#include "game/ob.h"
#include "ramrom.h"
#include "random.h"
#include "rmon.h" /*<PR/rmon.h>*/
#include "sched.h"
#include "speed_graph.h"
#include "token.h"
#include "tlb_manage.h"
#include "fr.h"
#include "game/image.h"
#include "unk_0C0A70.h"
#include "game/initgamedata.h"
#ifdef NATIVE_PORT
#include "game/initmenus.h"
#endif
#ifdef PROJECT_TITAN_TGR
extern int platformShouldQuit(void);
#endif
#include "vi.h"
#include "game/bg.h"
#include "game/debugmenu_handler.h"
#include "game/lvl.h"
#include "game/ramromreplay.h"
#include "game/rsp.h"
#include "game/stan.h"
#include "game/textrelated.h"
#include "game/lvl_text.h"
#include "game/player.h"
#include "game/player_2.h"
#include "game/objective_status.h"
#include "game/initactorpropstuff.h"
#include "game/initanitable.h"
#include "game/unk_0C0A70.h"
#include "PR/R4300.h"

#ifdef PROJECT_TITAN_TGR
#include "../../titan_ge_runtime.h"
#define TITAN_BOSS_TRACE(stage) TitanGeRuntime_Trace(stage)
static unsigned char s_titan_main_trace_seen[64];
static int s_titan_main_trace_done;
static void TitanBoss_MainTrace(unsigned int slot, const char *stage)
{
    if (s_titan_main_trace_done || slot >= sizeof(s_titan_main_trace_seen)) return;
    if (s_titan_main_trace_seen[slot]) return;
    s_titan_main_trace_seen[slot] = 1u;
    TitanGeRuntime_Trace(stage);
}
#define TITAN_MAIN_TRACE(slot, stage) TitanBoss_MainTrace((slot), (stage))
#define TITAN_MAIN_TRACE_DONE() do { s_titan_main_trace_done = 1; } while (0)
#else
#define TITAN_BOSS_TRACE(stage) ((void)0)
#define TITAN_MAIN_TRACE(slot, stage) ((void)0)
#define TITAN_MAIN_TRACE_DONE() ((void)0)
#endif

/**
 * @file boss.c
 * @brief Main game loop and initialization functions.
 * 
 * This file contains the core functions that drive the game loop, manage stages, and handle debug operations.
 */


#ifdef NATIVE_PORT
/* Set by lvlStageLoad to signal that room buffers were freed mid-frame.
 * bossMainloop skips rspGfxTaskStart when this is set to avoid processing
 * stale DL commands that reference freed memory. */
int g_pc_stage_reload_pending = 0;

/* The native game memory arena (replaces N64 BSS-relative addressing). Lifted to
 * file scope so the sim-state-hash invariance gate can hash it as the primary
 * mutable-state region. Base + size are stable for a run; contents are the
 * simulation state. */
static u8    *s_pcPool = NULL;
static size_t s_pcPoolSize = 0;
const void *bossGetPcPoolBase(void) { return s_pcPool; }
size_t      bossGetPcPoolSize(void) { return s_pcPoolSize; }
#endif

#ifdef REFRESH_PAL
#define MAIN_LOOP_TICK_INTERVAL frameDelay * 0xe34ea - 0x71a75U
#else
#define MAIN_LOOP_TICK_INTERVAL 0x5eb61U
#endif

/**
 * Scheduler message payload used by graphics-task completion.
 */
typedef union {

    struct {
        short   type;
    } gen;

    struct {
        short   type;
    } done;

    OSScMsg      app;

} GFXMsg;

// forward declarations
void bossMainloop(void);
void init_weapon_animation_groups_maybe(void);
void init_player_gait_object(void);
void null_init_main_1(void);
void null_init_main_2(void);
void null_init_main_3(void);
void set_gu_scale(void);

/* data */
u32 g_BossDebugNoticeEntry = 0;
s32 g_DebugAndUpdateStageFlag = FALSE;
s32 g_StageNum = LEVELID_TITLE;
u32 g_CurentMMallocValue = 0x234800;
u32 g_CurentMaMallocValue = 0x4B000;
s32 g_ShowMemUseFlag = FALSE;
s32 g_ShowMemBarsFlag = FALSE;

struct memallocstring memallocstringtable[] = {
{ LEVELID_DAM,          "-ml0 -me0 -mgfx70  -mvtx50 -mt625 -ma275"},
{ LEVELID_FACILITY,     "-ml0 -me0 -mgfx70  -mvtx50 -mt650 -ma160"},
{ LEVELID_RUNWAY,       "-ml0 -me0 -mgfx100 -mvtx50 -mt610 -ma300"},
{ LEVELID_SURFACE,      "-ml0 -me0 -mgfx70  -mvtx50 -mt600 -ma300"},
{ LEVELID_BUNKER1,      "-ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma150"},
{ LEVELID_SILO,         "-ml0 -me0 -mgfx70  -mvtx50 -mt660 -ma150"},
{ LEVELID_FRIGATE,      "-ml0 -me0 -mgfx70  -mvtx50 -mt750 -ma225"},
{ LEVELID_SURFACE2,     "-ml0 -me0 -mgfx100 -mvtx50 -mt550 -ma350"},
{ LEVELID_BUNKER2,      "-ml0 -me0 -mgfx100 -mvtx50 -mt725 -ma150"},
{ LEVELID_STATUE,       "-ml0 -me0 -mgfx70  -mvtx50 -mt750 -ma220"},
{ LEVELID_ARCHIVES,     "-ml0 -me0 -mgfx70  -mvtx50 -mt600 -ma250"},
{ LEVELID_STREETS,      "-ml0 -me0 -mgfx60  -mvtx40 -mt635 -ma290"},
{ LEVELID_DEPOT,        "-ml0 -me0 -mgfx60  -mvtx50 -mt710 -ma300"},
{ LEVELID_TRAIN,        "-ml0 -me0 -mgfx100 -mvtx50 -mt600 -ma200"},
{ LEVELID_JUNGLE,       "-ml0 -me0 -mgfx70  -mvtx50 -mt500 -ma200"},
{ LEVELID_CONTROL,      "-ml0 -me0 -mgfx70  -mvtx50 -mt671 -ma200"},
{ LEVELID_CRADLE,       "-ml0 -me0 -mgfx100 -mvtx50 -mt650 -ma250"},
{ LEVELID_CAVERNS,      "-ml0 -me0 -mgfx100 -mvtx50 -mt530 -ma250"},
{ LEVELID_AZTEC,        "-ml0 -me0 -mgfx60  -mvtx40 -mt855 -ma135"},
{ LEVELID_EGYPT,        "-ml0 -me0 -mgfx100 -mvtx50 -mt600 -ma250"},
{ LEVELID_CITADEL,      "-ml0 -me0 -mgfx100 -mvtx50 -mt650 -ma150"},
{ LEVELID_CUBA,         "-ml0 -me0 -mgfx100 -mvtx50 -mt300 -ma300"},
{ LEVELID_TITLE,        "-ml0 -me0 -mgfx80 -mvtx20 -mt646 -ma001"},
{ 0x5B,                 "-ml0 -me0 -mgfx60 -mvtx20 -mt500 -ma001"},
{ 0x63,                 "-ml0 -me0 -mgfx60 -mvtx20 -mt500 -ma001"},
{ LEVELID_TEMPLE,       "-ml0 -me0 -mgfx130 -mvtx100 -mt390 -ma100"},
{ LEVELID_COMPLEX,      "-ml0 -me0 -mgfx130 -mvtx100 -mt400 -ma100"},
{ LEVELID_LIBRARY,      "-ml0 -me0 -mgfx130 -mvtx100 -mt400 -ma100"},
{ LEVELID_BASEMENT,     "-ml0 -me0 -mgfx130 -mvtx100 -mt400 -ma100"},
{ LEVELID_STACK,        "-ml0 -me0 -mgfx130 -mvtx100 -mt400 -ma100"},
{ LEVELID_CAVES,        "-ml0 -me0 -mgfx130 -mvtx100 -mt400 -ma300"},
{ LEVELID_BUNKER2_MP ,  "-ml0 -me0 -mgfx130 -mvtx100 -mt550 -ma170"},
{ LEVELID_ARCHIVES_MP , "-ml0 -me0 -mgfx80  -mvtx100 -mt550 -ma250"},
{ LEVELID_CAVERNS_MP ,  "-ml0 -me0 -mgfx130 -mvtx100 -mt440 -ma220"},
{ LEVELID_FACILITY_MP , "-ml0 -me0 -mgfx90  -mvtx100 -mt550 -ma230"},
{ LEVELID_EGYPT_MP ,    "-ml0 -me0 -mgfx110 -mvtx100 -mt350 -ma400"},
{ LEVELID_DEFAULT,      "-ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma400"},
{ 0x0, },
{ 0x0, },
{ 0x0, }
};

s32 g_MainStageNum = LEVELID_NONE;
s32 g_BossIsDebugMenuOpen = FALSE;

OSScMsg g_bossGfxDoneMsg = { OS_SC_DONE_MSG };


// extern declarations
extern struct player *g_CurrentPlayer;

/**
 * 6930    70005D30
 *     ??? - uses "-level_", "-m" strings
 */


void bossInitMainthreadData(void)
{
    OSMesg bossmsg;
    OSTimer bosstimer;
    OSMesgQueue bossmq;
    u32 start;
    u32 unused;
    s32 i;

    TITAN_BOSS_TRACE("B01_INIT_ENTER");
    debInit();
    TITAN_BOSS_TRACE("B02_DEB_AFTER");
    romCreateMesgQueue();
    TITAN_BOSS_TRACE("B03_ROM_QUEUE_AFTER");
    tlbmanageEstablishManagementTable();
    TITAN_BOSS_TRACE("B04_TLB_AFTER");
    image_entries_load();
    TITAN_BOSS_TRACE("B05_IMAGES_AFTER");
    mempInit();
    TITAN_BOSS_TRACE("B06_MEMP_AFTER");
    memaInit();
    TITAN_BOSS_TRACE("B07_MEMA_AFTER");
    bgInit();
    TITAN_BOSS_TRACE("B08_BG_AFTER");
    viInit();
    TITAN_BOSS_TRACE("B09_VI_AFTER");
    viInitVideoSettings();
    TITAN_BOSS_TRACE("B10_VI_SETTINGS_AFTER");
    indycommInit();
    TITAN_BOSS_TRACE("B11_INDY_AFTER");
    g_DebugAndUpdateStageFlag = rmonGetToken();
    TITAN_BOSS_TRACE("B12_RMON_AFTER");
    obInit();
    TITAN_BOSS_TRACE("B13_OB_AFTER");
    rspInit();
    TITAN_BOSS_TRACE("B14_RSP_INIT_AFTER");
    dynInit();
    TITAN_BOSS_TRACE("B15_DYN_AFTER");
    joyInit();
    TITAN_BOSS_TRACE("B16_JOY_INIT_AFTER");
    TITAN_BOSS_TRACE("B17_MSGQ_CREATE_BEFORE");
    osCreateMesgQueue(&bossmq, &bossmsg, 1);
    TITAN_BOSS_TRACE("B18_MSGQ_CREATE_AFTER");

    TITAN_BOSS_TRACE("B19_TIMER_LOOP_BEFORE");
    for (i = 0; i != MAXCONTROLLERS; i++)
    {
        TITAN_BOSS_TRACE("B20_TIMER_SET_BEFORE");
        osSetTimer(&bosstimer, OS_USEC_TO_CYCLES(100000), 0, &bossmq, &bossmsg);
        TITAN_BOSS_TRACE("B21_TIMER_SET_AFTER");
        TITAN_BOSS_TRACE("B22_TIMER_RECV_BEFORE");
        osRecvMesg(&bossmq, &bossmsg, OS_MESG_BLOCK);
        TITAN_BOSS_TRACE("B23_TIMER_RECV_AFTER");
        if (i == 1)
        {
            joyCheckStatusThreadSafe();
        }
        else if (i >= 2)
        {
#ifdef NATIVE_PORT
            joyPoll(); /* On N64, scheduler thread calls joyPoll; on PC we must call it explicitly */
#endif
            joyConsumeSamplesWrapper();
        }
        TITAN_BOSS_TRACE("B24_TIMER_ITER_AFTER");
    }
    TITAN_BOSS_TRACE("B25_TIMER_LOOP_AFTER");
#ifdef NATIVE_PORT
    /* Stop the timer so its stale pointer to the stack-local bossmq
     * doesn't get dereferenced by platformCheckTimers after we return. */
    TITAN_BOSS_TRACE("B26_TIMER_STOP_BEFORE");
    osStopTimer(&bosstimer);
    TITAN_BOSS_TRACE("B27_TIMER_STOP_AFTER");
#endif
    if (tokenFind(1, "-level_") == 0)
    {
        g_DebugAndUpdateStageFlag = 1;
    }

    TITAN_BOSS_TRACE("B28_STAN_BEFORE");
    stanInit();
    TITAN_BOSS_TRACE("B29_STAN_AFTER");
    TITAN_BOSS_TRACE("B30_GAME_INIT_BEFORE");
    gameInit();
    TITAN_BOSS_TRACE("B31_GAME_INIT_AFTER");

    if (g_DebugAndUpdateStageFlag != 0)
    {
        tokenSetString("          -ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma400");
    }

    if (tokenFind(1, "-m") != 0)
    {
        g_CurentMMallocValue = (s32) (strtol(tokenFind(1, "-m"), 0, 0) << 0xa);
    }

#ifdef NATIVE_PORT
    /* On PC, allocate a large heap instead of using N64 BSS-relative addressing.
     * N64 has ~4MB RDRAM; allocate 8MB for headroom. */
    {
        #ifdef PROJECT_TITAN_TGR
        /* R5900 pointers are 32-bit like the retail N64 build.  Five MiB keeps
         * the original game banks intact while leaving TGR's 12 MiB budget for
         * the texture arena, streamed ROM cache and bounded decoded-SFX cache. */
        #define PC_POOL_SIZE (5 * 1024 * 1024)
        #else
        #define PC_POOL_SIZE (8 * 1024 * 1024)
        #endif
        if (!s_pcPool) {
            TITAN_BOSS_TRACE("B32_POOL_MALLOC_BEFORE");
            s_pcPool = (u8 *)malloc(PC_POOL_SIZE);
            TITAN_BOSS_TRACE("B33_POOL_MALLOC_AFTER");
            if (!s_pcPool) {
                printf("[FATAL] Failed to allocate %d byte memory pool\n", PC_POOL_SIZE);
#ifdef PROJECT_TITAN_TGR
                extern void TitanGeRuntime_Fail(void);
                TitanGeRuntime_Fail();
                return;
#else
                exit(1);
#endif
            }
            TITAN_BOSS_TRACE("B35_POOL_CLEAR_BEFORE");
            memset(s_pcPool, 0, PC_POOL_SIZE);
            TITAN_BOSS_TRACE("B36_POOL_CLEAR_AFTER");
            s_pcPoolSize = PC_POOL_SIZE;
        }
        TITAN_BOSS_TRACE("B37_MEMP_TOKENS_BEFORE");
        mempCheckMemflagTokens((intptr_t)s_pcPool, PC_POOL_SIZE);
        TITAN_BOSS_TRACE("B38_MEMP_TOKENS_AFTER");
    }
#else
    start = (PHYS_TO_K0(osVirtualToPhysical(&_bssSegmentEnd)));
    mempCheckMemflagTokens(start, ((u32)tlbmanageGetTlbAllocatedBlock() - (u32)start));
#endif
    TITAN_BOSS_TRACE("B39_CORE_TAIL1_BEFORE");
    mempResetBank(MEMPOOL_PERMANENT);
    langInit();
    lvInit();
    bossInit();
    TITAN_BOSS_TRACE("B40_CORE_TAIL1_AFTER");
    TITAN_BOSS_TRACE("B41_CORE_TAIL2_BEFORE");
    textrelatedInit_REMOVED();
    debmenu7000ADA8();
    default_player_perspective_and_height();
    store_osgetcount();
    null_init_main_1();
    speedgraphInit();
    TITAN_BOSS_TRACE("B42_CORE_TAIL2_AFTER");
    TITAN_BOSS_TRACE("B43_CORE_TAIL3_BEFORE");
    set_gu_scale();
    null_init_main_2();
    sub_GAME_7F000980();
    alloc_load_expand_ani_table();
    init_weapon_animation_groups_maybe();
    TITAN_BOSS_TRACE("B44_CORE_TAIL3_AFTER");
    TITAN_BOSS_TRACE("B45_CORE_TAIL4_BEFORE");
    reset_counter_rand_body_head();
    null_init_main_3();
    init_player_gait_object();
    initGameData();
    fileResetRamRomSave();
    clear_ramrom_block_buffer_heading_ptrs();
    TITAN_BOSS_TRACE("B46_CORE_TAIL4_AFTER");
}

/**
 * 6BF4    70005FF4
 *     1 ->"show mem use" debug memory display [800241B4]; fry AT,T6
 */
void bossEnableShowMemUseFlag(void) {
    g_ShowMemUseFlag=TRUE;
}

/**
 * 6C04    70006004
 *     toggle "show mem bars" [800241B8]; fries V0,T6,T7
 */
void bossMemBarsFlagToggle(void) {
    g_ShowMemBarsFlag = g_ShowMemBarsFlag ^ 1;
}

/**
 * 6C1C    7000601C
 *     loads primary resources and starts main program loop
 *     this is infinite.  Loops unconditionally: JAL 70006060
 */
void bossEntry(void) {
    TITAN_BOSS_TRACE("B00_ENTRY");
    TITAN_BOSS_TRACE("B81_INIT_MAINTHREAD_BEFORE");
    bossInitMainthreadData();
    TITAN_BOSS_TRACE("B82_INIT_MAINTHREAD_AFTER");
#ifdef PROJECT_TITAN_TGR
    {
        extern int TitanGeRuntime_HasFailed(void);
        if (TitanGeRuntime_HasFailed()) return;
    }
#endif
    TITAN_BOSS_TRACE("B83_RSP_ALLOC_BEFORE");
    rspAllocateBuffers();
    TITAN_BOSS_TRACE("B84_RSP_ALLOC_AFTER");
    TITAN_BOSS_TRACE("B85_MUSIC_SEQ_BEFORE");
    musicSeqPlayerInit();
    TITAN_BOSS_TRACE("B86_MUSIC_SEQ_AFTER");
#ifdef PROJECT_TITAN_TGR
    while(!platformShouldQuit()){
#else
    while(1){
#endif
       TITAN_BOSS_TRACE("B87_MAINLOOP_BEFORE");
       bossMainloop();
       TITAN_BOSS_TRACE("B88_MAINLOOP_AFTER");
    }
}

/**
 * Main program loop.
 *
 * 6C60    70006060
 *
 * loop:
 *         70006090 tests memstring for "-level_##"
 *         700060DC if not title, tests memstring for "-hard#"
 *         70006160 follows...
 *         700061FC test if debug console unconnected [800241A4]
 *         700062EC follows...
 *         700062FC tests memstring for "-ma"
 *         7000633C allocates "-ma" bytes to mem bank 4
 *         7000635C reset player data pointers
 *         70006364 offsets stage number based on number of players unless main menu
 *         700063A0 parses and sets memory allocation, loads stage, etc.
 *         ...
 *         70006708 displays memory usage when active
 *         70006724 displays in-game debugger when active
 *         7000674C writes a full sync, end display list combo
 *         7000676C display mem use when active    [800241B4]
 *         700067A8 display mem bars when active    [800241B8]
 *         700067C0 follows...
 *         700067D8 tests if "u64.taskgrab.#.core" activated and dumps memory
 *         70006854 follows... (700068BC - stop demos)
 */
void bossMainloop(void)
{
    // declarations

    s32 done;
    const unsigned char *tokenFindLevel;
    GFXMsg *localGfxFrameMsg; // sp 468
    OSScMsg localGfxDoneMsg; // sp 436
    s32 stringIndex;
    s32 toggleFlag; // sp 428
    Gfx *gdl; // sp424
    Gfx *firstGdl;
    u32 nowCount;
    s32 i;
    s32 count;
    s8 joyStickXPos;
    s8 joyStickYPos;
    u16 joyButtons;
    struct player *localPlayer;
    s32 localSelectedNumPlayers;
    u32 pendingGfx = 0;
    s32 freeGfx;
    s32 mainTickElapsed;
    s32 rspReplyMsg;

    u32 unused_stackpadding_[56];

    // end declarations

    TITAN_MAIN_TRACE(0u, "M00_MAINLOOP_ENTER");
    done = 0;
    reset_mem_bank_5();
    TITAN_MAIN_TRACE(1u, "M01_RESET_BANK5_AFTER");

    if (tokenFind(1, "-level_") != NULL)
    {
        tokenFindLevel = (const unsigned char *)tokenFind(1, "-level_");

        // quick hack strltolon, converts the two digit ASCII level to 32bit int
        // eg, "09" = 48*10 + 57 - 528 = 9
        g_StageNum = (((s32)(tokenFindLevel[0] * 10) + (s32)tokenFindLevel[1]) - 0x210);
    }

    if (g_StageNum != LEVELID_TITLE)
    {
        fileValidateSaves();
        fileSetCurrentFolder(FOLDER1);
        set_selected_difficulty(DIFFICULTY_AGENT);
        set_solo_and_ptr_briefing(g_StageNum);

        if (tokenFind(1, "-hard"))
        {
            // convert ASCII difficulty value to int in set difficulty calls eg '1' = 49, 49-48 = 1
            set_selected_difficulty(*(const unsigned char*)tokenFind(1, "-hard") - '0');
            lvlSetSelectedDifficulty(*(const unsigned char*)tokenFind(1, "-hard") - '0');
        }

#ifdef NATIVE_PORT
        pcPrimePostStageMenuForDirectBoot(g_StageNum, get_selected_num_players() >= 2);
#endif
    }

    nowCount = osGetCount();
#ifdef NATIVE_PORT
    {
        extern int g_deterministic;
        if (g_deterministic) {
            randomSetSeed(0x12345678);  /* fixed seed for deterministic replay */
        } else {
            randomSetSeed(nowCount);
        }
    }
#else
    randomSetSeed(nowCount);
#endif

    // 'done' value never changes, and control never breaks -- infinite loop
    TITAN_MAIN_TRACE(2u, "M02_PRE_OUTER_LOOP");
    while (!done)
    {
#ifdef PROJECT_TITAN_TGR
        if (platformShouldQuit()) return;
#endif
        TITAN_MAIN_TRACE(3u, "M03_OUTER_LOOP_ENTER");
        localGfxFrameMsg = NULL;
        localGfxDoneMsg = g_bossGfxDoneMsg;
        toggleFlag = 0;
        pendingGfx = 0;
#ifdef NATIVE_PORT
        extern void portClearAllHeads(void);
        extern void portClearAllDlCol(void);
        extern void portClearAllNodeOverrides(void);
        extern void portWatchdogLoadBegin(void);
        extern void portAudioPrefillQueue(void);
        portClearAllHeads();
        portClearAllDlCol();          /* FID-0122: drop stale DLCOLLISION runtime pointers */
        portClearAllNodeOverrides();  /* FID-0122 class: drop stale node render overrides */
        /* PERF-035: prefill the audio queue to the web cap BEFORE the stage-pool
         * teardown below (mempResetBank/memaReset) so the load has ~200ms of
         * audio. Ordering is load-bearing: prefill drives portAudioFrame while
         * ChrRecord slots are still live (FID-0089). HARD no-op on native and
         * under --deterministic. */
        portAudioPrefillQueue();
        /* Stage (re)load ahead: lvlStageLoad below legitimately blocks the
         * main loop for seconds — suppress the stall watchdog until the
         * frame loop is re-entered. */
        portWatchdogLoadBegin();
        TITAN_MAIN_TRACE(4u, "M04_PRELOAD_NATIVE_AFTER");
#endif

        test_if_recording_demos_this_stage_load(g_StageNum, lvlGetSelectedDifficulty());
        TITAN_MAIN_TRACE(5u, "M05_DEMO_SETUP_AFTER");
        if (g_DebugAndUpdateStageFlag)
        {
            stringIndex = -1;

            if (g_StageNum != LEVELID_TITLE && get_selected_num_players() >= 2)
            {
                stringIndex = 0;

                while(memallocstringtable[stringIndex].id)
                {
                    if (memallocstringtable[stringIndex].id == (g_StageNum + 400))
                    {
                        break;
                    }

                    stringIndex++;
                }

                if (memallocstringtable[stringIndex].id == 0)
                {
                    stringIndex = -1;
                }
            }

            if (stringIndex)
            {
                // empty
            }

            if (stringIndex < 0)
            {
                stringIndex = 0;

                while(memallocstringtable[stringIndex].id)
                {
                    if (memallocstringtable[stringIndex].id == g_StageNum)
                    {
                        break;
                    }

                    stringIndex++;
                }
            }

            tokenSetString(memallocstringtable[stringIndex].string);
        }

        TITAN_MAIN_TRACE(6u, "M06_STAGE_BANK_RESET_BEFORE");
        mempResetBank(MEMPOOL_STAGE);
        obBlankResourcesLoadedInBank(MEMPOOL_STAGE);
        TITAN_MAIN_TRACE(7u, "M07_STAGE_BANK_RESET_AFTER");
        if (tokenFind(1, "-ma"))
        {
            g_CurentMaMallocValue = (s32) (strtol(tokenFind(1, "-ma"), NULL, 0) * 1024);
        }

#ifdef NATIVE_PORT
        {
            TITAN_MAIN_TRACE(8u, "M08_STAGE_ALLOC_BEFORE");
            void *stageAlloc = mempAllocBytesInBank(g_CurentMaMallocValue, MEMPOOL_STAGE);
            TITAN_MAIN_TRACE(9u, "M09_STAGE_ALLOC_AFTER");
            TITAN_MAIN_TRACE(10u, "M10_MEMA_RESET_BEFORE");
            memaReset(stageAlloc, g_CurentMaMallocValue);
            TITAN_MAIN_TRACE(11u, "M11_MEMA_RESET_AFTER");
        }
#else
        memaReset(mempAllocBytesInBank(g_CurentMaMallocValue, MEMPOOL_STAGE), g_CurentMaMallocValue);
#endif
        reset_play_data_ptrs();

        localSelectedNumPlayers = 0;
        if (g_StageNum != LEVELID_TITLE)
        {
            localSelectedNumPlayers = 1;
            if (get_selected_num_players() >= 2)
            {
                localSelectedNumPlayers = get_selected_num_players();
            }
        }

        TITAN_MAIN_TRACE(12u, "M12_VIEWPORT_INIT_BEFORE");
        init_player_data_ptrs_construct_viewports(localSelectedNumPlayers);
        TITAN_MAIN_TRACE(13u, "M13_VIEWPORT_INIT_AFTER");
        TITAN_MAIN_TRACE(14u, "M14_DYN_MEMORY_BEFORE");
        dynInitMemory();
#ifdef PROJECT_TITAN_TGR
        if (TitanGeRuntime_HasFailed()) return;
#endif
        TITAN_MAIN_TRACE(15u, "M15_DYN_MEMORY_AFTER");
        TITAN_MAIN_TRACE(16u, "M16_JOY_STATUS_BEFORE");
        joyCheckStatusThreadSafe();
        TITAN_MAIN_TRACE(17u, "M17_JOY_STATUS_AFTER");
        TITAN_MAIN_TRACE(18u, "M18_STAGE_LOAD_BEFORE");
        lvlStageLoad(g_StageNum);
#ifdef PROJECT_TITAN_TGR
        if (TitanGeRuntime_HasFailed()) return;
#endif
        TITAN_MAIN_TRACE(19u, "M19_STAGE_LOAD_AFTER");
        portLoadYield();  /* PERF-035: unwind the event loop after the stage load */
        TITAN_MAIN_TRACE(20u, "M20_POST_STAGE_YIELD_AFTER");
        TITAN_MAIN_TRACE(21u, "M21_VI_BUFFERS_BEFORE");
        viInitBuffers();
        TITAN_MAIN_TRACE(22u, "M22_VI_BUFFERS_AFTER");
        debmenuInit();
        TITAN_MAIN_TRACE(23u, "M23_DEBMENU_INIT_AFTER");
        portLoadYield();  /* PERF-035: unwind after VI/debug-menu init */
        TITAN_MAIN_TRACE(24u, "M24_POST_VI_YIELD_AFTER");
#ifdef MGB64_WEBGPU_BACKEND
        /* PERF-005 Phase 2 (W4.3): record/replay pipeline prewarm. set_stage points
         * the recorder at this stage (persisting the previous stage's manifest if it
         * grew); prewarm_stage then SYNCHRONOUSLY builds every pipeline recorded for
         * this stage on a prior visit/session, so cold materials don't hitch on first
         * sight. Runs here inside the watchdog-suppressed load window (load screen up)
         * and AFTER portLoadYield, so it never straddles an Asyncify suspend. No-op
         * under --deterministic, when GE007_PIPECACHE=0, or when the webgpu backend
         * isn't the active one (gfx_webgpu.h). g_StageNum is boss.c's authority. */
        extern void gfx_webgpu_set_stage(int stage);
        extern void gfx_webgpu_prewarm_stage(int stage);
        gfx_webgpu_set_stage(g_StageNum);
        gfx_webgpu_prewarm_stage(g_StageNum);
#endif
#ifdef NATIVE_PORT
        /* Reset frame timer before first waitForNextFrame to avoid huge delta
         * from init time being counted as elapsed game frames */
        store_osgetcount();
#endif
#ifdef PROJECT_TITAN_TGR
        if (platformShouldQuit()) return;
#endif
        TITAN_MAIN_TRACE(25u, "M25_INITIAL_WAIT_FRAME_BEFORE");
        waitForNextFrame();
        TITAN_MAIN_TRACE(26u, "M26_INITIAL_WAIT_FRAME_AFTER");
        speedgraphMarkerCommit();

        if(1); // regalloc
        if(1);

        TITAN_MAIN_TRACE(27u, "M27_DRAIN_NOBLOCK_BEFORE");
        while (osRecvMesg(&gfxFrameMsgQ, (OSMesg *)&localGfxFrameMsg, OS_MESG_NOBLOCK) == 0)
        {
           // empty
        }
        TITAN_MAIN_TRACE(28u, "M28_DRAIN_NOBLOCK_AFTER");

        if(1); // regalloc
        if(1);


#ifdef NATIVE_PORT
        /* Drain any SDL events that queued during long init (macOS sends SDL_QUIT) */
        extern void platformDrainEvents(void);
        extern void portWatchdogLoadEnd(void);
        platformDrainEvents();
        TITAN_MAIN_TRACE(29u, "M29_PLATFORM_DRAIN_AFTER");
        /* Reset frame timer so title screen doesn't immediately timeout to demo */
        store_osgetcount();
        /* Frame loop starts below: arm the stall watchdog. */
        portWatchdogLoadEnd();
        TITAN_MAIN_TRACE(30u, "M30_LOAD_WATCHDOG_END_AFTER");
#endif
        TITAN_MAIN_TRACE(31u, "M31_FRAME_MSG_LOOP_BEFORE");
        while (g_MainStageNum < 0 || pendingGfx != 0)
        {
#ifdef PROJECT_TITAN_TGR
            if (platformShouldQuit()) return;
#endif
            TITAN_MAIN_TRACE(32u, "M32_FRAME_MSG_RECV_BEFORE");
            osRecvMesg(&gfxFrameMsgQ, (OSMesg *)&localGfxFrameMsg, OS_MESG_BLOCK);
            TITAN_MAIN_TRACE(33u, "M33_FRAME_MSG_RECV_AFTER");
#ifdef PROJECT_TITAN_TGR
            if (platformShouldQuit()) return;
#endif

            switch (localGfxFrameMsg->gen.type)
            {
                case (OS_SC_RETRACE_MSG):
                {
                    TITAN_MAIN_TRACE(34u, "M34_RETRACE_CASE_ENTER");
#ifdef DEBUG
    /* debug logging from simple.c, I think this requires #include <ultralog.h>
    * //    if (logging)
    * //        osLogEvent(log, LOG_RETRACE, 1, pendingGfx);
    */
#endif

                    mainTickElapsed = (u32) (osGetCount() - copy_of_osgetcount_value_1);
                    if (mainTickElapsed < MAIN_LOOP_TICK_INTERVAL)
                    {
                        // nothing to do.
                    }
                    else
                    {
                        if (g_MainStageNum < 0 && pendingGfx < 2U)
                        {
                            TITAN_MAIN_TRACE(35u, "M35_FRAME_BUILD_ENTER");
                            if (get_is_ramrom_flag())
                            {
#ifdef NATIVE_PORT
                                pcRamromTraceLoopPhase("mainloop_before_ramrom_iterate");
#endif
                                iterate_ramrom_entries_handle_camera_out();
#ifdef NATIVE_PORT
                                pcRamromTraceLoopPhase("mainloop_after_ramrom_iterate");
#endif
                            }
                            else
                            {
                                TITAN_MAIN_TRACE(36u, "M36_FRAME_WAIT2_BEFORE");
                                waitForNextFrame();
                                TITAN_MAIN_TRACE(37u, "M37_FRAME_WAIT2_AFTER");
                            }

                            speedgraphRenderGraph();
                            speedgraphMarkerCommit();
                            speedgraphMarkerHandler(0x20000);
#ifdef NATIVE_PORT
                            pcRamromTraceLoopPhase("mainloop_before_joy_poll");
                            joyPoll(); /* On N64, scheduler thread calls joyPoll; on PC we must call it explicitly */
                            pcRamromTraceLoopPhase("mainloop_after_joy_poll");
                            pcRamromTraceLoopPhase("mainloop_before_joy_consume");
#endif
                            joyConsumeSamplesWrapper();
#ifdef NATIVE_PORT
                            pcRamromTraceLoopPhase("mainloop_after_joy_consume");
#endif
                            permit_stderr(0);

#ifdef NATIVE_PORT
                            /* FID-0033 0-tick purity fuzz (docs/design/UNCAPPED_FPS_PLAN.md
                             * Task 4): a render-only fuzz frame injects ZERO sim ticks.
                             * The N64 engine runs sim + render 1:1 and was never designed
                             * for a 0-tick frame — the sim tick (lvlManageMpGame /
                             * lvlViewMoveTick), lvlRender, the double-buffer swap
                             * (dynSwapBuffers) and the mema defrag pass all advance
                             * per-frame game-pool state, or pool-resident (un-canonicalized)
                             * render pointers into the double-buffered VTX arena, that is
                             * NOT clock-delta-gated. So a genuine 0-tick frame skips the
                             * entire DL-build + sim + present + maintenance block below,
                             * leaving the render/present pipeline to advance exactly once
                             * per TICK frame — identical to a vanilla all-tick run. The
                             * loop / frame-timing machinery still runs and must leave every
                             * hashed sim byte untouched; that is the invariant
                             * tools/uncap_purity_gate.sh proves. Full render-path decoupling
                             * under a variable frame rate is the separate scope of the F5
                             * uncapped-FPS project (Tasks 5-8). Never taken in normal play
                             * (the flag is 0 unless armed by the harness). */
                            if (g_pcUncapRenderOnlyFrame) goto pc_fuzz_render_only_skip;
#endif

                            TITAN_MAIN_TRACE(38u, "M38_MASTER_GDL_BEFORE");
                            gdl = firstGdl = dynGetMasterDisplayList();
                            TITAN_MAIN_TRACE(39u, "M39_MASTER_GDL_AFTER");

#ifdef DEBUGMENU
                            //ported from pd beta, official way to open debug menu
			                // If menu is open (?) or player has pressed C down + C up
			                if (g_BossIsDebugMenuOpen || joyGetButtons(0, U_CBUTTONS | D_CBUTTONS) == (U_CBUTTONS | D_CBUTTONS)) {
			                	joyStickXPos = joyGetStickX(0);
			                	joyStickYPos = joyGetStickY(0);
			                	joyButtons = joyGetButtons(0, ANY_BUTTON);
			                	g_BossIsDebugMenuOpen = debug_menu_processor(joyStickXPos, joyStickYPos, joyButtons, joyGetButtonsPressedThisFrame(0, ANY_BUTTON));
			                } else if (joyGetButtons(0, START_BUTTON) == 0) {
                                g_DebugMode = g_DebugHighlightedOption;
			                } else
#endif
#ifndef DEBUGMENU
                            if (g_BossIsDebugMenuOpen)
#endif
                            {
			                	joyStickXPos = joyGetStickX(0);
			                	joyStickYPos = joyGetStickY(0);
			                	joyButtons = joyGetButtons(0, ANY_BUTTON);
			                	g_BossIsDebugMenuOpen = debug_menu_processor(joyStickXPos, joyStickYPos, joyButtons, joyGetButtonsPressedThisFrame(0, ANY_BUTTON));
			                }

                            TITAN_MAIN_TRACE(40u, "M40_LVL_MANAGE_BEFORE");
                            lvlManageMpGame();
                            TITAN_MAIN_TRACE(41u, "M41_LVL_MANAGE_AFTER");
                            shuffle_player_ids();

                            if (g_StageNum != LEVELID_TITLE)
                            {
                                for (i = 0; i < getPlayerCount(); i++)
                                {
                                    set_cur_player(get_nth_player_from_shuffled(i));

                                    localPlayer = g_CurrentPlayer;
                                    viSetViewSize(localPlayer->viewx, localPlayer->viewy);

                                    localPlayer = g_CurrentPlayer;
                                    viSetViewPosition(localPlayer->viewleft, localPlayer->viewtop);

                                    lvlViewMoveTick();
                                }
                            }

#ifdef NATIVE_PORT
                            {
                                static s32 lastStage = -1;
                                if (g_StageNum != lastStage) {
                                    lastStage = g_StageNum;
                                }
                            }
#endif
                            TITAN_MAIN_TRACE(42u, "M42_LVL_RENDER_BEFORE");
                            gdl = lvlRender(gdl);
                            TITAN_MAIN_TRACE(43u, "M43_LVL_RENDER_AFTER");

                            // Lets Visualise the Coverage Value used for Scilohete Anti-Ailising (edges)
                            // (done on the VI), also produces a cool looking linemode - providing AA is working.
                            if (get_debug_VisCVG_flag())
                            {
                                gDPPipeSync(gdl++); // 0xe7000000, 0x00000000
                                gDPSetCycleType(gdl++, G_CYC_1CYCLE); // 0xba001402, 0x00000000
                                gDPSetBlendColor(gdl++, 0xff, 0xff, 0xff, 0xff); // 0xf9000000, 0xffffffff
                                gDPSetPrimDepth(gdl++, 0xffff, 0xffff); // 0xee000000, 0xffffffff
                                gDPSetDepthSource(gdl++, G_ZS_PRIM); // 0xb9000201, 0x00000004
                                gDPSetRenderMode(gdl++, G_RM_VISCVG, G_RM_VISCVG2); // 0xb900031d, 0x0fa54040
                                gDPFillRectangle(gdl++, 0, 0, viGetX() - 1, viGetY() - 1);
                            }

                            gdl = debmenuDraw(gdl);

                            if (get_memusage_display_flag())
                            {
                                gdl = speedgraphDisplayMetrics(gdl);
                            }

                            if (g_BossIsDebugMenuOpen)
                            {
                                debugmenuUpdate();
                                gdl = debugmenuRender(gdl);
                            }

                            gDPFullSync(gdl++); // 0xe9000000, 0x00000000
                            gSPEndDisplayList(gdl++); // 0xb8000000, 0x00000000
                            TITAN_MAIN_TRACE(44u, "M44_DISPLAY_LIST_FINALIZED");

                            if (g_ShowMemUseFlag)
                            {
                                nulled_mempLoopAllMemBanks();
                                memaDumpPrePostMerge();
                                dynRemovedFunc(gdl);
                                nullsub_41(0);
                                g_ShowMemUseFlag = 0;
                            }

                            if (g_ShowMemBarsFlag)
                            {
                                dynDrawMembars(gdl);
                            }

                            freeGfx = dynGetFreeGfx2(gdl);
                            TITAN_MAIN_TRACE(45u, "M45_FREE_GFX_AFTER");
                            TITAN_MAIN_TRACE(46u, "M46_DYN_SWAP_BEFORE");
                            dynSwapBuffers();
                            TITAN_MAIN_TRACE(47u, "M47_DYN_SWAP_AFTER");
                            video_related_8();
                            TITAN_MAIN_TRACE(48u, "M48_VIDEO_RELATED_AFTER");

                            if ((get_debug_taskgrab_val())
                                && (joyGetButtonsPressedThisFrame(0, (A_BUTTON | B_BUTTON)))
                                && (joyGetButtons(0, (A_BUTTON | B_BUTTON)) == (A_BUTTON | B_BUTTON)))
                            {
                                static s32 taskgrab_ramdump_num = 1;
                                char taskGrabBuffer[28];
                                s32 taskGrabFileSize;

                                while (1)
                                {
                                    s32 unusedSprintf = ge007_sprintf(taskGrabBuffer, "u64.taskgrab.%d.core", taskgrab_ramdump_num);

                                    if (indycommHostCheckFileExists(taskGrabBuffer, &taskGrabFileSize) != NULL)
                                    {
                                        taskgrab_ramdump_num++;
                                        continue;
                                    }

                                    break;
                                }

                                indycommHostSendDump(taskGrabBuffer, (u8*)0x80000000, 0x400000);
                            }

#ifdef NATIVE_PORT
                            if (g_pc_stage_reload_pending) {
                                /* Stage reload freed room buffers mid-frame.
                                 * Skip DL processing — commands reference freed memory.
                                 * Do NOT increment pendingGfx: no task was launched,
                                 * so no OS_SC_DONE_MSG will arrive to decrement it. */
                                g_pc_stage_reload_pending = 0;
                            } else {
                                TITAN_MAIN_TRACE(49u, "M49_RSP_TASK_BEFORE");
                                rspGfxTaskStart(firstGdl, gdl, 0, (OSMesg)&localGfxDoneMsg);
                                TITAN_MAIN_TRACE(50u, "M50_RSP_TASK_AFTER");
                                pendingGfx++;
                                TITAN_MAIN_TRACE_DONE();
                            }
#else
                            rspReplyMsg = (s32)(&localGfxDoneMsg);
                            rspGfxTaskStart(firstGdl, gdl, 0, (s32*)rspReplyMsg);
                            pendingGfx++;
#endif
                            memaSingleDefragPass();
#ifdef VERSION_EU
                            eu_sub_7f0c00a4();
#endif
                            toggleFlag ^= 1;

                            speedgraphMarkerHandler(0x10000);
                            if(1);
#ifdef NATIVE_PORT
pc_fuzz_render_only_skip:;
                            {
                                /* End of a full sim+render frame (after the DL
                                 * was processed/presented in rspGfxTaskStart):
                                 * publish the stall-watchdog heartbeat. A wedge
                                 * anywhere in the frame — sim spin or GL/driver
                                 * present — stops it advancing. */
                                extern void portWatchdogFrameTick(void);
                                portWatchdogFrameTick();
                            }
#endif
                        }
                    }
                }
                break;

                case (OS_SC_DONE_MSG):
                    pendingGfx--;
                    break;

                case OS_SC_PRE_NMI_MSG:
                    pendingGfx = 4U;
                    break;
            }
        }

        lvlUnloadStageTextData();
        stop_demo_playback();
        mempNullNextEntryInBank(MEMPOOL_STAGE);
        obBlankResourcesLoadedInBank(MEMPOOL_STAGE);

#if defined(VERSION_EU)
        if(1);
#endif

        g_StageNum = g_MainStageNum;
        g_MainStageNum = LEVELID_NONE;
    }

    // if(gdl) also works here.
    if (!gdl)
        // removed ... or ido?
    ;

    sub_GAME_7F0D1A7C();
}


/**
 * 7530    70006930
 *     run title [0x5A->loaded stage#]; fry AT
 *     redirect to 70006950: A0=0x5A
 */
void bossRunTitleStage(void) {
    bossSetLoadedStage(LEVELID_TITLE);
}

/**
 * 7550    70006950
 *     A0->loaded stage# [800242FC]; fry AT
 *     0x5A jumps to folder select
 *     0x5B
 *     0x63
 */
void bossSetLoadedStage(LEVELID stage){
    g_MainStageNum = stage;
}

/**
 * 755C    7000695C
 *     V0= stage# [800241A8]
 */
LEVELID bossGetStageNum() {
    return g_StageNum;
}

/**
 * 7568    70006968
 *     return to title screen from stage
 */
void bossReturnTitleStage(void) {
#ifdef BUGFIX_R1
    display_objective_status_text_on_status_change();
    FUN_7f057a40();
#endif
    if ((bossGetStageNum() != LEVELID_CUBA) && (objectiveIsAllComplete() != 0x0)) {
        end_of_mission_briefing();
    }
    bossRunTitleStage();
}

/**
 * 75B4    700069B4
 *     V0=state of debug menu (1:on; 0:off) [80024300]
 */
s32 bossGetDebugParseFlag(void) {
    return g_BossIsDebugMenuOpen;
}


/**
 * 75C0    700069C0
 *     V0= p->debug.notice.list entry for boss_c_debug using data at 800241A0
 */
void bossInit(void) {
    debTryAdd(&g_BossDebugNoticeEntry, "boss_c_debug");
}
