
#include <ultra64.h>
#include "debugmenu_handler.h"
#include "lvl.h"
#include "initcheattext.h"
#include "front.h"
#include "bondview.h"
#include "indy_comms.h"
#include "mp_weapon.h"
#include "ramromreplay.h"
#include <ramrom.h>
#include <macro.h>
#include "file.h"
#include "file2.h"
#include <random.h>
#include "joy.h"
#include "unk_0C0A70.h"
#if defined(NATIVE_PORT)
#include "byteswap.h"
#include <stdio.h>
#include <stdlib.h>
#endif
//D:800483F0

struct ramrom_struct
{
    ramromfilestructure *fdata;
    s32 locked;
};

struct ramrom_blockbuf
{
    s8 stick_x;
    s8 stick_y;
    u8 button_low;
    u8 button_high;
};
struct ramrom_seed
{
    u8 speedframes;
    u8 count;
    u8 randseed;
    u8 check;
};

#if defined(NATIVE_PORT)
#if defined(VERSION_EU)
#define RAMROM_DAM_1_OFFSET       0x002AC240U
#define RAMROM_DAM_2_OFFSET       0x002AE550U
#define RAMROM_FACILITY_1_OFFSET  0x002B3300U
#define RAMROM_FACILITY_2_OFFSET  0x002B6590U
#define RAMROM_FACILITY_3_OFFSET  0x002B9B10U
#define RAMROM_RUNWAY_1_OFFSET    0x002BD050U
#define RAMROM_RUNWAY_2_OFFSET    0x002BF740U
#define RAMROM_BUNKERI_1_OFFSET   0x002C6310U
#define RAMROM_BUNKERI_2_OFFSET   0x002CB3E0U
#define RAMROM_SILO_1_OFFSET      0x002CF800U
#define RAMROM_SILO_2_OFFSET      0x002D1D00U
#define RAMROM_FRIGATE_1_OFFSET   0x002D5150U
#define RAMROM_FRIGATE_2_OFFSET   0x002D7B90U
#define RAMROM_TRAIN_OFFSET       0x002D9BB0U
#elif defined(VERSION_JP)
#define RAMROM_DAM_1_OFFSET       0x002BFFF0U
#define RAMROM_DAM_2_OFFSET       0x002C51F0U
#define RAMROM_FACILITY_1_OFFSET  0x002C71C0U
#define RAMROM_FACILITY_2_OFFSET  0x002C8C70U
#define RAMROM_FACILITY_3_OFFSET  0x002CB050U
#define RAMROM_RUNWAY_1_OFFSET    0x002CCCC0U
#define RAMROM_RUNWAY_2_OFFSET    0x002CF410U
#define RAMROM_BUNKERI_1_OFFSET   0x002D1D20U
#define RAMROM_BUNKERI_2_OFFSET   0x002D50B0U
#define RAMROM_SILO_1_OFFSET      0x002DA330U
#define RAMROM_SILO_2_OFFSET      0x002DC4C0U
#define RAMROM_FRIGATE_1_OFFSET   0x002DE490U
#define RAMROM_FRIGATE_2_OFFSET   0x002DFE40U
#define RAMROM_TRAIN_OFFSET       0x002E3320U
#else
#define RAMROM_DAM_1_OFFSET       0x002BF2D0U
#define RAMROM_DAM_2_OFFSET       0x002C44D0U
#define RAMROM_FACILITY_1_OFFSET  0x002C64A0U
#define RAMROM_FACILITY_2_OFFSET  0x002C7F50U
#define RAMROM_FACILITY_3_OFFSET  0x002CA330U
#define RAMROM_RUNWAY_1_OFFSET    0x002CBFA0U
#define RAMROM_RUNWAY_2_OFFSET    0x002CE6F0U
#define RAMROM_BUNKERI_1_OFFSET   0x002D1000U
#define RAMROM_BUNKERI_2_OFFSET   0x002D4390U
#define RAMROM_SILO_1_OFFSET      0x002D9610U
#define RAMROM_SILO_2_OFFSET      0x002DB7A0U
#define RAMROM_FRIGATE_1_OFFSET   0x002DD770U
#define RAMROM_FRIGATE_2_OFFSET   0x002DF120U
#define RAMROM_TRAIN_OFFSET       0x002E2600U
#endif

#define RAMROM_ENTRY(offset) ((ramromfilestructure *)(uintptr_t)(offset))
#else
//move me to better home
extern u32 ramrom_Dam_1;
extern u32 ramrom_Dam_2;
extern u32 ramrom_Facility_1;
extern u32 ramrom_Facility_2;
extern u32 ramrom_Facility_3;
extern u32 ramrom_Runway_1;
extern u32 ramrom_Runway_2;
extern u32 ramrom_BunkerI_1;
extern u32 ramrom_BunkerI_2;
extern u32 ramrom_Silo_1;
extern u32 ramrom_Silo_2;
extern u32 ramrom_Frigate_1;
extern u32 ramrom_Frigate_2;
extern u32 ramrom_Train;
#endif

extern u64 g_randomSeed;
extern u64 g_chrObjRandomSeed;
#if defined(NATIVE_PORT)
extern struct ramrom_seed *ramrom_blkbuf_2;
extern s32 is_ramrom_flag;
extern s32 ramrom_demo_related_3;
extern s32 g_ramromPlayBackFlag;
extern s32 recording_ramrom_flag;
#endif

#if defined(NATIVE_PORT)
static void ramromByteswapSaveData(save_data *save)
{
    save->chksum1 = bswap_s32(save->chksum1);
    save->chksum2 = bswap_s32(save->chksum2);
    save->options = bswap16(save->options);
}

static void ramromByteswapHeader(ramromfilestructure *state)
{
    state->randomseed = __builtin_bswap64(state->randomseed);
    state->randomizer = __builtin_bswap64(state->randomizer);
    state->stagenum = (enum LEVELID)bswap32((u32)state->stagenum);
    state->difficulty = (enum DIFFICULTY)bswap32((u32)state->difficulty);
    state->size_cmds = bswap32(state->size_cmds);
    ramromByteswapSaveData(&state->savefile);
    state->totaltime_ms = bswap_s32(state->totaltime_ms);
    state->filesize = bswap_s32(state->filesize);
    state->mode = (enum GAMEMODE)bswap32((u32)state->mode);
    state->slotnum = bswap32(state->slotnum);
    state->numplayers = bswap32(state->numplayers);
    state->scenario = bswap32(state->scenario);
    state->mpstage_sel = bswap32(state->mpstage_sel);
    state->gamelength = bswap32(state->gamelength);
    state->mp_weapon_set = bswap32(state->mp_weapon_set);

    for (s32 i = 0; i < 4; i++)
    {
        state->mp_char[i] = bswap32(state->mp_char[i]);
        state->mp_handi[i] = bswap32(state->mp_handi[i]);
        state->mp_contstyle[i] = bswap32(state->mp_contstyle[i]);
        state->mp_flags[i] = bswap32(state->mp_flags[i]);
    }

    state->aim_option = bswap32(state->aim_option);
}
#endif

struct ramrom_struct ramrom_table[] = {
#if defined(NATIVE_PORT)
    {RAMROM_ENTRY(RAMROM_DAM_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_DAM_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_FACILITY_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_FACILITY_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_FACILITY_3_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_RUNWAY_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_RUNWAY_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_BUNKERI_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_BUNKERI_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_SILO_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_SILO_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_FRIGATE_1_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_FRIGATE_2_OFFSET), 0},
    {RAMROM_ENTRY(RAMROM_TRAIN_OFFSET), 0},
#else
    {(ramromfilestructure *)&ramrom_Dam_1, 0},
    {(ramromfilestructure *)&ramrom_Dam_2, 0},
    {(ramromfilestructure *)&ramrom_Facility_1, 0},
    {(ramromfilestructure *)&ramrom_Facility_2, 0},
    {(ramromfilestructure *)&ramrom_Facility_3, 0},
    {(ramromfilestructure *)&ramrom_Runway_1, 0},
    {(ramromfilestructure *)&ramrom_Runway_2, 0},
    {(ramromfilestructure *)&ramrom_BunkerI_1, 0},
    {(ramromfilestructure *)&ramrom_BunkerI_2, 0},
    {(ramromfilestructure *)&ramrom_Silo_1, 0},
    {(ramromfilestructure *)&ramrom_Silo_2, 0},
    {(ramromfilestructure *)&ramrom_Frigate_1, 0},
    {(ramromfilestructure *)&ramrom_Frigate_2, 0},
    {(ramromfilestructure *)&ramrom_Train, 0},
#endif
    {0,0}
};

void replay_recorded_ramrom_at_address(ramromfilestructure *demofile);
extern ramromfilestructure *ptr_active_demofile;

#if defined(NATIVE_PORT)
typedef struct pc_ramrom_named_entry
{
    const char *symbol;
    const char *alias;
    ramromfilestructure *fdata;
    u32 rom_offset;
} pc_ramrom_named_entry;

static const pc_ramrom_named_entry pc_ramrom_named_entries[] = {
    {"ramrom_Dam_1", "dam1", RAMROM_ENTRY(RAMROM_DAM_1_OFFSET), RAMROM_DAM_1_OFFSET},
    {"ramrom_Dam_2", "dam2", RAMROM_ENTRY(RAMROM_DAM_2_OFFSET), RAMROM_DAM_2_OFFSET},
    {"ramrom_Facility_1", "facility1", RAMROM_ENTRY(RAMROM_FACILITY_1_OFFSET), RAMROM_FACILITY_1_OFFSET},
    {"ramrom_Facility_2", "facility2", RAMROM_ENTRY(RAMROM_FACILITY_2_OFFSET), RAMROM_FACILITY_2_OFFSET},
    {"ramrom_Facility_3", "facility3", RAMROM_ENTRY(RAMROM_FACILITY_3_OFFSET), RAMROM_FACILITY_3_OFFSET},
    {"ramrom_Runway_1", "runway1", RAMROM_ENTRY(RAMROM_RUNWAY_1_OFFSET), RAMROM_RUNWAY_1_OFFSET},
    {"ramrom_Runway_2", "runway2", RAMROM_ENTRY(RAMROM_RUNWAY_2_OFFSET), RAMROM_RUNWAY_2_OFFSET},
    {"ramrom_BunkerI_1", "bunkeri1", RAMROM_ENTRY(RAMROM_BUNKERI_1_OFFSET), RAMROM_BUNKERI_1_OFFSET},
    {"ramrom_BunkerI_2", "bunkeri2", RAMROM_ENTRY(RAMROM_BUNKERI_2_OFFSET), RAMROM_BUNKERI_2_OFFSET},
    {"ramrom_Silo_1", "silo1", RAMROM_ENTRY(RAMROM_SILO_1_OFFSET), RAMROM_SILO_1_OFFSET},
    {"ramrom_Silo_2", "silo2", RAMROM_ENTRY(RAMROM_SILO_2_OFFSET), RAMROM_SILO_2_OFFSET},
    {"ramrom_Frigate_1", "frigate1", RAMROM_ENTRY(RAMROM_FRIGATE_1_OFFSET), RAMROM_FRIGATE_1_OFFSET},
    {"ramrom_Frigate_2", "frigate2", RAMROM_ENTRY(RAMROM_FRIGATE_2_OFFSET), RAMROM_FRIGATE_2_OFFSET},
    {"ramrom_Train", "train", RAMROM_ENTRY(RAMROM_TRAIN_OFFSET), RAMROM_TRAIN_OFFSET},
};

static const char *s_pcRamromTraceSymbol = "";
static u32 s_pcRamromTraceBaseOffset = 0;
static const char *s_pcRamromTraceStopReason = "";
static s32 s_pcRamromTraceStopEventCount = 0;
static u32 s_pcRamromTraceStopInputStreamOffset = 0;
static s32 s_pcRamromTraceAbortButtons = 0;
static s32 s_pcRamromTraceRandseedExpected = -1;
static s32 s_pcRamromTraceRandseedActual = -1;
static s32 s_pcRamromTraceChecksumExpected = -1;
static s32 s_pcRamromTraceChecksumActual = -1;
static u64 s_pcRamromTraceRandomCallBase = 0;
static u64 s_pcRamromTraceRestoredRandomSeed = 0;
static u64 s_pcRamromTraceStopRandomCallCount = 0;
static u64 s_pcRamromTraceStopRandomCallsSinceRestore = 0;
static u64 s_pcRamromTraceStopRandomSeed = 0;
static s32 s_pcRamromTraceBlocksEnabled = -1;
static s32 s_pcRamromTraceLoopEnabled = -1;
static s32 s_pcRamromTraceLoopRemaining = 0;
static s32 s_pcRamromTraceUpcomingEnabled = -1;
static s32 s_pcRamromTraceUpcomingRemaining = 0;
static u32 s_pcRamromTraceCurrentBlockStreamOffset = 0;
static u32 s_pcRamromTraceCurrentBlockAfterOffset = 0;
static s32 s_pcRamromTraceBlockLoadCount = 0;
static s32 s_pcRamromUpcomingSeedPrevalidated = 0;
static u32 s_pcRamromUpcomingSeedPrevalidatedStreamOffset = 0;
static u32 s_pcRamromUpcomingSeedPrevalidatedAfterOffset = 0;
static s32 s_pcRamromUpcomingSeedPrevalidatedRandseed = -1;
static s32 s_pcRamromUpcomingSeedPrevalidatedCheck = -1;
static s32 s_pcRamromUpcomingSeedPrevalidatedCount = -1;
static s32 s_pcRamromUpcomingSeedPrevalidatedSpeedframes = -1;
static u64 s_pcRamromUpcomingSeedPrevalidatedCallCount = 0;
static u64 s_pcRamromUpcomingSeedPrevalidatedCallsSinceRestore = 0;
static u64 s_pcRamromUpcomingSeedPrevalidatedSeed = 0;
static s32 s_pcRamromCurrentBlockSeedPrevalidated = 0;
static s32 s_pcRamromFirstBlockDeferredRngDrained = 0;
static s32 s_pcRamromUpcomingBlockBoundaryDeferredActionTicksDrained = 0;

void pcChrlvRunDeferredRamromFirstBlockIdleAnimation(void);
void pcChrlvRunDeferredRamromUpcomingBlockRngEvents(void);
void pcBheadRunDeferredRamromFirstBlockIdleRoll(void);

static char pcRamromTokenChar(char c)
{
    if (c >= 'A' && c <= 'Z') {
        c = (char)(c + ('a' - 'A'));
    }

    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return c;
    }

    return '\0';
}

static s32 pcRamromNamesEqual(const char *left, const char *right)
{
    char lc;
    char rc;

    if (left == NULL || right == NULL) {
        return FALSE;
    }

    while (*left != '\0' || *right != '\0') {
        lc = '\0';
        while (*left != '\0' && lc == '\0') {
            lc = pcRamromTokenChar(*left++);
        }

        rc = '\0';
        while (*right != '\0' && rc == '\0') {
            rc = pcRamromTokenChar(*right++);
        }

        if (lc != rc) {
            return FALSE;
        }
    }

    return TRUE;
}

static const pc_ramrom_named_entry *pcRamromFindNamedEntry(const char *name)
{
    size_t i;

    for (i = 0; i < ARRAYCOUNT(pc_ramrom_named_entries); i++) {
        const pc_ramrom_named_entry *entry = &pc_ramrom_named_entries[i];

        if (pcRamromNamesEqual(name, entry->symbol) || pcRamromNamesEqual(name, entry->alias)) {
            return entry;
        }
    }

    return NULL;
}

static void pcRamromSetTraceSource(ramromfilestructure *demofile)
{
    uintptr_t address = (uintptr_t)demofile;
    size_t i;

    s_pcRamromTraceSymbol = "";
    s_pcRamromTraceBaseOffset = (u32)address;
    s_pcRamromTraceStopReason = "";
    s_pcRamromTraceStopEventCount = 0;
    s_pcRamromTraceStopInputStreamOffset = 0;
    s_pcRamromTraceAbortButtons = 0;
    s_pcRamromTraceRandseedExpected = -1;
    s_pcRamromTraceRandseedActual = -1;
    s_pcRamromTraceChecksumExpected = -1;
    s_pcRamromTraceChecksumActual = -1;
    s_pcRamromTraceRandomCallBase = pcRandomGetNextCallCount();
    s_pcRamromTraceRestoredRandomSeed = 0;
    s_pcRamromTraceStopRandomCallCount = 0;
    s_pcRamromTraceStopRandomCallsSinceRestore = 0;
    s_pcRamromTraceStopRandomSeed = 0;
    s_pcRamromTraceCurrentBlockStreamOffset = 0;
    s_pcRamromTraceCurrentBlockAfterOffset = 0;
    s_pcRamromTraceBlockLoadCount = 0;
    s_pcRamromUpcomingSeedPrevalidated = 0;
    s_pcRamromUpcomingSeedPrevalidatedStreamOffset = 0;
    s_pcRamromUpcomingSeedPrevalidatedAfterOffset = 0;
    s_pcRamromUpcomingSeedPrevalidatedRandseed = -1;
    s_pcRamromUpcomingSeedPrevalidatedCheck = -1;
    s_pcRamromUpcomingSeedPrevalidatedCount = -1;
    s_pcRamromUpcomingSeedPrevalidatedSpeedframes = -1;
    s_pcRamromUpcomingSeedPrevalidatedCallCount = 0;
    s_pcRamromUpcomingSeedPrevalidatedCallsSinceRestore = 0;
    s_pcRamromUpcomingSeedPrevalidatedSeed = 0;
    s_pcRamromCurrentBlockSeedPrevalidated = 0;
    s_pcRamromFirstBlockDeferredRngDrained = 0;
    s_pcRamromUpcomingBlockBoundaryDeferredActionTicksDrained = 0;
    s_pcRamromTraceLoopEnabled = -1;
    s_pcRamromTraceLoopRemaining = 0;
    s_pcRamromTraceUpcomingEnabled = -1;
    s_pcRamromTraceUpcomingRemaining = 0;
    pcRandomTraceSetRamromActive(0);

    for (i = 0; i < ARRAYCOUNT(pc_ramrom_named_entries); i++) {
        const pc_ramrom_named_entry *entry = &pc_ramrom_named_entries[i];

        if (entry->fdata == demofile || entry->rom_offset == (u32)address) {
            s_pcRamromTraceSymbol = entry->symbol;
            s_pcRamromTraceBaseOffset = entry->rom_offset;
            return;
        }
    }
}

static u32 pcRamromCurrentInputStreamOffset(void)
{
    uintptr_t cursor = (uintptr_t)address_demo_loaded;
    u32 cursor_offset;

    if (s_pcRamromTraceBaseOffset == 0 || cursor < (uintptr_t)s_pcRamromTraceBaseOffset) {
        return 0;
    }

    cursor_offset = (u32)(cursor - (uintptr_t)s_pcRamromTraceBaseOffset);
    if (cursor_offset < sizeof(ramromfilestructure)) {
        return 0;
    }

    return cursor_offset - (u32)sizeof(ramromfilestructure);
}

static s32 pcRamromTraceBlocksEnabled(void)
{
    const char *value;

    if (s_pcRamromTraceBlocksEnabled < 0) {
        value = getenv("GE007_TRACE_RAMROM_BLOCKS");
        s_pcRamromTraceBlocksEnabled = value != NULL && *value != '\0' ? 1 : 0;
    }

    return s_pcRamromTraceBlocksEnabled;
}

static s32 pcRamromTraceLoopEnabled(void)
{
    const char *value;

    if (s_pcRamromTraceLoopEnabled < 0) {
        s_pcRamromTraceLoopRemaining = 512;
        value = getenv("GE007_TRACE_RAMROM_LOOP_BUDGET");
        if (value != NULL && *value != '\0') {
            s_pcRamromTraceLoopRemaining = atoi(value);
        }

        value = getenv("GE007_TRACE_RAMROM_LOOP");
        s_pcRamromTraceLoopEnabled = value != NULL && *value != '\0' ? 1 : 0;
    }

    return s_pcRamromTraceLoopEnabled && s_pcRamromTraceLoopRemaining != 0;
}

static s32 pcRamromTraceUpcomingEnabled(void)
{
    const char *value;

    if (s_pcRamromTraceUpcomingEnabled < 0) {
        s_pcRamromTraceUpcomingRemaining = 256;
        value = getenv("GE007_TRACE_RAMROM_UPCOMING_BUDGET");
        if (value != NULL && *value != '\0') {
            s_pcRamromTraceUpcomingRemaining = atoi(value);
        }

        value = getenv("GE007_TRACE_RAMROM_UPCOMING");
        s_pcRamromTraceUpcomingEnabled = value != NULL && *value != '\0' ? 1 : 0;
    }

    return s_pcRamromTraceUpcomingEnabled && s_pcRamromTraceUpcomingRemaining != 0;
}

static void pcRamromTraceLoopPhaseImpl(const char *phase, s32 detail0, s32 detail1, s32 detail2)
{
    u64 call_count;
    u64 calls_since_restore;
    u32 cursor_offset;

    if (!pcRamromTraceLoopEnabled()) {
        return;
    }

    if (!is_ramrom_flag
        && !recording_ramrom_flag
        && !g_ramromPlayBackFlag
        && (s_pcRamromTraceSymbol == NULL || s_pcRamromTraceSymbol[0] == '\0'))
    {
        return;
    }

    call_count = pcRandomGetNextCallCount();
    calls_since_restore = call_count - s_pcRamromTraceRandomCallBase;
    cursor_offset = 0;
    if (s_pcRamromTraceBaseOffset != 0
        && address_demo_loaded != NULL
        && (uintptr_t)address_demo_loaded >= (uintptr_t)s_pcRamromTraceBaseOffset)
    {
        cursor_offset = (u32)((uintptr_t)address_demo_loaded - (uintptr_t)s_pcRamromTraceBaseOffset);
    }

    fprintf(stderr,
            "[RAMROM-LOOP] phase=%s detail0=%d detail1=%d detail2=%d "
            "active=%d playback_pending=%d demo_related=%d recording=%d symbol=%s "
            "cursor_offset=%u input_stream_offset=%u current_block_stream_offset=%u "
            "current_block_after_offset=%u block_load_count=%d call_count=%llu "
            "calls_since_restore=%llu seed=0x%016llx seed_low=%u clock=%d global=%d "
            "global_delta=%.3f speedgraphframes=%d block_count=%d block_speedframes=%d "
            "block_randseed=%d block_check=%d\n",
            phase != NULL ? phase : "",
            detail0,
            detail1,
            detail2,
            is_ramrom_flag,
            g_ramromPlayBackFlag,
            ramrom_demo_related_3,
            recording_ramrom_flag,
            s_pcRamromTraceSymbol != NULL ? s_pcRamromTraceSymbol : "",
            cursor_offset,
            pcRamromCurrentInputStreamOffset(),
            s_pcRamromTraceCurrentBlockStreamOffset,
            s_pcRamromTraceCurrentBlockAfterOffset,
            s_pcRamromTraceBlockLoadCount,
            (unsigned long long)call_count,
            (unsigned long long)calls_since_restore,
            (unsigned long long)g_randomSeed,
            (unsigned int)((u8)g_randomSeed),
            g_ClockTimer,
            g_GlobalTimer,
            (double)g_GlobalTimerDelta,
            speedgraphframes,
            ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->count : -1,
            ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->speedframes : -1,
            ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->randseed : -1,
            ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->check : -1);

    if (s_pcRamromTraceLoopRemaining > 0) {
        s_pcRamromTraceLoopRemaining--;
    }
}

void pcRamromTraceLoopPhase(const char *phase)
{
    pcRamromTraceLoopPhaseImpl(phase, -1, -1, -1);
}

void pcRamromTraceLoopPhaseWithDetails(const char *phase, s32 detail0, s32 detail1, s32 detail2)
{
    pcRamromTraceLoopPhaseImpl(phase, detail0, detail1, detail2);
}

static void pcRamromTraceBlockTiming(const char *tag,
                                     const char *phase,
                                     u32 stream_offset,
                                     u32 stream_offset_after_block)
{
    u64 call_count;
    u64 calls_since_restore;

    if (!pcRamromTraceBlocksEnabled() || ramrom_blkbuf_2 == NULL) {
        return;
    }

    call_count = pcRandomGetNextCallCount();
    calls_since_restore = call_count - s_pcRamromTraceRandomCallBase;

    fprintf(stderr,
            "[%s] phase=%s stream_offset=%u stream_offset_after_block=%u "
            "call_count=%llu calls_since_restore=%llu seed=0x%016llx seed_low=%u "
            "block_count=%d speedframes=%d randseed=%d check=%d\n",
            tag,
            phase != NULL ? phase : "",
            stream_offset,
            stream_offset_after_block,
            (unsigned long long)call_count,
            (unsigned long long)calls_since_restore,
            (unsigned long long)g_randomSeed,
            (unsigned int)((u8)g_randomSeed),
            ramrom_blkbuf_2->count,
            ramrom_blkbuf_2->speedframes,
            ramrom_blkbuf_2->randseed,
            ramrom_blkbuf_2->check);
}

s32 pcRamromShouldDeferFirstBlockRngConsumers(void)
{
    return get_is_ramrom_flag()
        && !s_pcRamromFirstBlockDeferredRngDrained
        && s_pcRamromTraceCurrentBlockAfterOffset == 0
        && pcRamromCurrentInputStreamOffset() == 0;
}

static struct ramrom_seed *pcRamromPeekUpcomingBlockSeed(u32 *stream_offset, u32 *after_offset)
{
    struct ramrom_seed *peeked_seed;
    u32 current_stream_offset;

    if (!get_is_ramrom_flag()
        || ptr_active_demofile == NULL
        || address_demo_loaded == NULL
        || s_pcRamromTraceCurrentBlockAfterOffset == 0)
    {
        return NULL;
    }

    current_stream_offset = pcRamromCurrentInputStreamOffset();
    if (current_stream_offset != s_pcRamromTraceCurrentBlockAfterOffset) {
        return NULL;
    }

    peeked_seed = romCopyAligned(ramrom_data_target + 0x300, address_demo_loaded, sizeof(struct ramrom_seed));
    if (peeked_seed == NULL || (peeked_seed->count == 0 && peeked_seed->speedframes == 0)) {
        return NULL;
    }

    if (stream_offset != NULL) {
        *stream_offset = current_stream_offset;
    }
    if (after_offset != NULL) {
        *after_offset = current_stream_offset +
            (u32)align_addr_even((ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * peeked_seed->count) + 5);
    }

    return peeked_seed;
}

static s32 pcRamromRecordUpcomingSeedPrevalidation(struct ramrom_seed *peeked_seed,
                                                   u32 stream_offset,
                                                   u32 after_offset)
{
    u64 call_count;
    u64 calls_since_restore;

    if (peeked_seed == NULL || peeked_seed->randseed != (u8)g_randomSeed) {
        return FALSE;
    }

    if (s_pcRamromUpcomingSeedPrevalidated
        && s_pcRamromUpcomingSeedPrevalidatedStreamOffset == stream_offset
        && s_pcRamromUpcomingSeedPrevalidatedAfterOffset == after_offset
        && s_pcRamromUpcomingSeedPrevalidatedRandseed == peeked_seed->randseed
        && s_pcRamromUpcomingSeedPrevalidatedCheck == peeked_seed->check)
    {
        return TRUE;
    }

    call_count = pcRandomGetNextCallCount();
    calls_since_restore = call_count - s_pcRamromTraceRandomCallBase;

    s_pcRamromUpcomingSeedPrevalidated = 1;
    s_pcRamromUpcomingSeedPrevalidatedStreamOffset = stream_offset;
    s_pcRamromUpcomingSeedPrevalidatedAfterOffset = after_offset;
    s_pcRamromUpcomingSeedPrevalidatedRandseed = peeked_seed->randseed;
    s_pcRamromUpcomingSeedPrevalidatedCheck = peeked_seed->check;
    s_pcRamromUpcomingSeedPrevalidatedCount = peeked_seed->count;
    s_pcRamromUpcomingSeedPrevalidatedSpeedframes = peeked_seed->speedframes;
    s_pcRamromUpcomingSeedPrevalidatedCallCount = call_count;
    s_pcRamromUpcomingSeedPrevalidatedCallsSinceRestore = calls_since_restore;
    s_pcRamromUpcomingSeedPrevalidatedSeed = g_randomSeed;

    return TRUE;
}

static void pcRamromAttachPrevalidatedSeedToCurrentBlock(u32 stream_offset, u32 after_offset)
{
    s_pcRamromCurrentBlockSeedPrevalidated = 0;

    if (!s_pcRamromUpcomingSeedPrevalidated || ramrom_blkbuf_2 == NULL) {
        return;
    }

    if (s_pcRamromUpcomingSeedPrevalidatedAfterOffset <= stream_offset
        && s_pcRamromUpcomingSeedPrevalidatedStreamOffset != stream_offset)
    {
        s_pcRamromUpcomingSeedPrevalidated = 0;
        return;
    }

    if (s_pcRamromUpcomingSeedPrevalidatedStreamOffset == stream_offset
        && s_pcRamromUpcomingSeedPrevalidatedAfterOffset == after_offset
        && s_pcRamromUpcomingSeedPrevalidatedRandseed == ramrom_blkbuf_2->randseed
        && s_pcRamromUpcomingSeedPrevalidatedCheck == ramrom_blkbuf_2->check
        && s_pcRamromUpcomingSeedPrevalidatedCount == ramrom_blkbuf_2->count
        && s_pcRamromUpcomingSeedPrevalidatedSpeedframes == ramrom_blkbuf_2->speedframes)
    {
        s_pcRamromCurrentBlockSeedPrevalidated = 1;
        pcRamromTraceLoopPhaseWithDetails("ramrom_iterate_seed_prevalidated",
                                          stream_offset,
                                          after_offset,
                                          ramrom_blkbuf_2->randseed);
    }
}

static s32 pcRamromCurrentBlockSeedPrevalidationMatches(void)
{
    if (!s_pcRamromCurrentBlockSeedPrevalidated
        || !s_pcRamromUpcomingSeedPrevalidated
        || ramrom_blkbuf_2 == NULL)
    {
        return FALSE;
    }

    return s_pcRamromUpcomingSeedPrevalidatedStreamOffset == s_pcRamromTraceCurrentBlockStreamOffset
        && s_pcRamromUpcomingSeedPrevalidatedAfterOffset == s_pcRamromTraceCurrentBlockAfterOffset
        && s_pcRamromUpcomingSeedPrevalidatedRandseed == ramrom_blkbuf_2->randseed
        && s_pcRamromUpcomingSeedPrevalidatedCheck == ramrom_blkbuf_2->check
        && s_pcRamromUpcomingSeedPrevalidatedCount == ramrom_blkbuf_2->count
        && s_pcRamromUpcomingSeedPrevalidatedSpeedframes == ramrom_blkbuf_2->speedframes;
}

static void pcRamromClearCurrentBlockSeedPrevalidation(void)
{
    if (pcRamromCurrentBlockSeedPrevalidationMatches()) {
        s_pcRamromUpcomingSeedPrevalidated = 0;
    }
    s_pcRamromCurrentBlockSeedPrevalidated = 0;
}

void pcRamromObserveUpcomingBlockSeedWindow(const char *source)
{
    struct ramrom_seed *peeked_seed;
    u64 call_count;
    u64 calls_since_restore;
    u32 stream_offset;
    u32 after_offset;
    u32 cursor_offset;
    s32 prevalidated;

    peeked_seed = pcRamromPeekUpcomingBlockSeed(&stream_offset, &after_offset);
    if (peeked_seed == NULL || peeked_seed->randseed != (u8)g_randomSeed) {
        return;
    }

    prevalidated = pcRamromRecordUpcomingSeedPrevalidation(peeked_seed, stream_offset, after_offset);

    if (!pcRamromTraceUpcomingEnabled()) {
        return;
    }

    call_count = pcRandomGetNextCallCount();
    calls_since_restore = call_count - s_pcRamromTraceRandomCallBase;
    cursor_offset = 0;
    if (s_pcRamromTraceBaseOffset != 0
        && address_demo_loaded != NULL
        && (uintptr_t)address_demo_loaded >= (uintptr_t)s_pcRamromTraceBaseOffset)
    {
        cursor_offset = (u32)((uintptr_t)address_demo_loaded - (uintptr_t)s_pcRamromTraceBaseOffset);
    }

    fprintf(stderr,
            "[RAMROM-UPCOMING] event=seed_match source=%s symbol=%s "
            "cursor_offset=%u input_stream_offset=%u upcoming_stream_offset=%u "
            "upcoming_stream_offset_after_block=%u current_block_stream_offset=%u "
            "current_block_after_offset=%u block_load_count=%d call_count=%llu "
            "calls_since_restore=%llu seed=0x%016llx seed_low=%u expected_randseed=%d "
            "upcoming_count=%d upcoming_speedframes=%d upcoming_check=%d "
            "clock=%d global=%d global_delta=%.3f speedgraphframes=%d "
            "prevalidated=%d prevalidated_call_count=%llu "
            "prevalidated_calls_since_restore=%llu\n",
            source != NULL ? source : "",
            s_pcRamromTraceSymbol != NULL ? s_pcRamromTraceSymbol : "",
            cursor_offset,
            stream_offset,
            stream_offset,
            after_offset,
            s_pcRamromTraceCurrentBlockStreamOffset,
            s_pcRamromTraceCurrentBlockAfterOffset,
            s_pcRamromTraceBlockLoadCount,
            (unsigned long long)call_count,
            (unsigned long long)calls_since_restore,
            (unsigned long long)g_randomSeed,
            (unsigned int)((u8)g_randomSeed),
            peeked_seed->randseed,
            peeked_seed->count,
            peeked_seed->speedframes,
            peeked_seed->check,
            g_ClockTimer,
            g_GlobalTimer,
            (double)g_GlobalTimerDelta,
            speedgraphframes,
            prevalidated,
            (unsigned long long)s_pcRamromUpcomingSeedPrevalidatedCallCount,
            (unsigned long long)s_pcRamromUpcomingSeedPrevalidatedCallsSinceRestore);

    if (s_pcRamromTraceUpcomingRemaining > 0) {
        s_pcRamromTraceUpcomingRemaining--;
    }
}

s32 pcRamromShouldDeferUpcomingBlockBoundaryActionTick(void)
{
    struct ramrom_seed *peeked_seed;

    if (!get_is_ramrom_flag()
        || s_pcRamromUpcomingBlockBoundaryDeferredActionTicksDrained
        || s_pcRamromTraceBlockLoadCount != 1
        || s_pcRamromTraceCurrentBlockAfterOffset == 0)
    {
        return FALSE;
    }

    peeked_seed = pcRamromPeekUpcomingBlockSeed(NULL, NULL);
    if (peeked_seed == NULL) {
        return FALSE;
    }

    return peeked_seed->randseed == (u8)g_randomSeed;
}

static void pcRamromRunDeferredFirstBlockRngConsumers(void)
{
    if (s_pcRamromFirstBlockDeferredRngDrained) {
        return;
    }

    s_pcRamromFirstBlockDeferredRngDrained = 1;
    pcChrlvRunDeferredRamromFirstBlockIdleAnimation();
    pcBheadRunDeferredRamromFirstBlockIdleRoll();
}

static void pcRamromRunDeferredUpcomingBlockBoundaryActionTicks(void)
{
    if (s_pcRamromUpcomingBlockBoundaryDeferredActionTicksDrained
        || s_pcRamromTraceBlockLoadCount != 2)
    {
        return;
    }

    s_pcRamromUpcomingBlockBoundaryDeferredActionTicksDrained = 1;
    pcChrlvRunDeferredRamromUpcomingBlockRngEvents();
}

static void pcRamromMarkStopReason(const char *reason,
                                   s32 abort_buttons,
                                   s32 randseed_expected,
                                   s32 randseed_actual,
                                   s32 checksum_expected,
                                   s32 checksum_actual)
{
    s_pcRamromTraceStopReason = reason != NULL ? reason : "";
    s_pcRamromTraceStopEventCount++;
    s_pcRamromTraceStopInputStreamOffset = pcRamromCurrentInputStreamOffset();
    s_pcRamromTraceStopRandomCallCount = pcRandomGetNextCallCount();
    s_pcRamromTraceStopRandomCallsSinceRestore =
        s_pcRamromTraceStopRandomCallCount - s_pcRamromTraceRandomCallBase;
    s_pcRamromTraceStopRandomSeed = g_randomSeed;
    s_pcRamromTraceAbortButtons = abort_buttons;
    s_pcRamromTraceRandseedExpected = randseed_expected;
    s_pcRamromTraceRandseedActual = randseed_actual;
    s_pcRamromTraceChecksumExpected = checksum_expected;
    s_pcRamromTraceChecksumActual = checksum_actual;
}

s32 pcRamromReplayNameIsValid(const char *name)
{
    return pcRamromFindNamedEntry(name) != NULL;
}

s32 pcRamromStartReplayByName(const char *name)
{
    const pc_ramrom_named_entry *entry = pcRamromFindNamedEntry(name);

    if (entry == NULL) {
        fprintf(stderr, "[RAMROM-PC] unknown RAMROM demo '%s'\n", name != NULL ? name : "");
        return FALSE;
    }

    printf("[RAMROM-PC] Start demo: %s\n", entry->symbol);
    pcRamromSetTraceSource(entry->fdata);
    replay_recorded_ramrom_at_address(entry->fdata);
    return TRUE;
}
#endif

//D:80048468
ramromfilestructure* ptr_active_demofile = 0;
//D:8004846C
struct ramrom_seed * ramrom_blkbuf_2 = NULL;
//D:80048470
struct ramrom_blockbuf * ramrom_blkbuf_3 = NULL;
//D:80048474
s32 is_ramrom_flag = 0;
//D:80048478
s32 ramrom_demo_related_3 = 0;
//D:8004847C
s32 g_ramromPlayBackFlag = 0;
//D:80048480
s32 recording_ramrom_flag = 0;
//D:80048484
s32 ramrom_demo_related_6 = 0;
//D:80048488
s32 g_ramromRecordFlag = 0;
//D:8004848C
//                     .align 4





void ramromFadeToTitle(void);
s32 ramrom_replay_handler(struct contsample *arg0, s32 arg1);
void record_player_input_as_packet(struct contsample *arg0, s32 arg1, s32 arg2);
void copy_current_ingame_registers_before_ramrom_playback(ramromfilestructure *state);
void copy_recorded_ramrom_registers_to_proper_place_ingame(ramromfilestructure *state);


void clear_ramrom_block_buffer_heading_ptrs(void) {
    ptr_active_demofile = 0;
    ramrom_blkbuf_2 = 0;
    ramrom_blkbuf_3 = 0;
}


s32 get_is_ramrom_flag(void) {
    return is_ramrom_flag;
}


s32 get_recording_ramrom_flag(void) {
    return recording_ramrom_flag;
}

#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
#ifndef GE007_ROM_RNG_TRACE_CAPACITY
#define GE007_ROM_RNG_TRACE_CAPACITY 512
#endif

#define GE007_ROM_RNG_TRACE_MAGIC 0x47375254U /* G7RT */
#define GE007_ROM_RNG_TRACE_SCHEMA_VERSION 1U
#define GE007_ROM_RNG_TRACE_INVALID_U32 0xFFFFFFFFU

#define GE007_ROM_RNG_TRACE_KIND_RESTORE 1U
#define GE007_ROM_RNG_TRACE_KIND_ITERATE_ENTER 2U
#define GE007_ROM_RNG_TRACE_KIND_BLOCK_LOAD 3U
#define GE007_ROM_RNG_TRACE_KIND_AFTER_UPDATE_FRAME_COUNTERS 4U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_ENTER 5U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_AFTER_SAMPLES 6U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_SEED_MATCH 7U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_SEED_MISMATCH 8U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_CHECKSUM_MISMATCH 9U
#define GE007_ROM_RNG_TRACE_KIND_HANDLER_AFTER_CHECKS 10U
#define GE007_ROM_RNG_TRACE_KIND_TERMINATOR 11U
#define GE007_ROM_RNG_TRACE_KIND_TOTAL_TIME_STOP 12U
#define GE007_ROM_RNG_TRACE_KIND_ABORT_BUTTONS 13U

typedef struct ge007_rom_rng_trace_event {
    u32 schema_version;
    u32 sequence;
    u32 kind;
    u32 flags;
    u32 stream_offset;
    u32 stream_offset_after_block;
    u32 random_seed_hi;
    u32 random_seed_lo;
    u32 seed_low;
    u32 block_randseed;
    u32 block_check;
    u32 block_count;
    u32 block_speedframes;
    u32 checksum_actual;
    u32 clock_timer;
    u32 global_timer;
    u32 speedgraphframes_value;
    u32 last_frame_counter;
    u32 current_frame_counter;
    u32 os_count_copy0;
    u32 os_count_copy1;
    u32 address_demo_loaded_value;
    u32 active_demo_base;
    u32 current_block_ptr;
    u32 payload_block_ptr;
} ge007_rom_rng_trace_event;

volatile u32 g_Ge007RomRngTraceMagic = GE007_ROM_RNG_TRACE_MAGIC;
volatile u32 g_Ge007RomRngTraceSchemaVersion = GE007_ROM_RNG_TRACE_SCHEMA_VERSION;
volatile u32 g_Ge007RomRngTraceCapacity = GE007_ROM_RNG_TRACE_CAPACITY;
volatile u32 g_Ge007RomRngTraceEventSize = sizeof(ge007_rom_rng_trace_event);
volatile u32 g_Ge007RomRngTraceWriteIndex = 0;
volatile u32 g_Ge007RomRngTraceOverflowCount = 0;
volatile u32 g_Ge007RomRngTraceDemoBase = 0;
volatile ge007_rom_rng_trace_event g_Ge007RomRngTrace[GE007_ROM_RNG_TRACE_CAPACITY];

static void ge007RomRngTraceSetDemoBase(void *demofile)
{
    g_Ge007RomRngTraceDemoBase = (u32)demofile;
}

static void ge007RomRngTraceReset(void)
{
    g_Ge007RomRngTraceWriteIndex = 0;
    g_Ge007RomRngTraceOverflowCount = 0;
}

static u32 ge007RomRngTraceInputStreamOffset(void)
{
    s32 cursor_offset;

    if (g_Ge007RomRngTraceDemoBase == 0 || address_demo_loaded == NULL) {
        return GE007_ROM_RNG_TRACE_INVALID_U32;
    }

    cursor_offset = (s32)address_demo_loaded - (s32)g_Ge007RomRngTraceDemoBase;
    if (cursor_offset < (s32)sizeof(ramromfilestructure)) {
        return GE007_ROM_RNG_TRACE_INVALID_U32;
    }

    return (u32)(cursor_offset - (s32)sizeof(ramromfilestructure));
}

static u32 ge007RomRngTraceCurrentBlockAdvance(void)
{
    u32 payload_size;

    if (ptr_active_demofile == NULL || ramrom_blkbuf_2 == NULL) {
        return GE007_ROM_RNG_TRACE_INVALID_U32;
    }

    payload_size =
        ptr_active_demofile->size_cmds *
        sizeof(struct ramrom_blockbuf) *
        ramrom_blkbuf_2->count;

    return (u32)align_addr_even(payload_size + 5);
}

static u32 ge007RomRngTraceCurrentBlockStreamOffset(void)
{
    u32 input_stream_offset;
    u32 advance;

    input_stream_offset = ge007RomRngTraceInputStreamOffset();
    advance = ge007RomRngTraceCurrentBlockAdvance();

    if (input_stream_offset == GE007_ROM_RNG_TRACE_INVALID_U32
        || advance == GE007_ROM_RNG_TRACE_INVALID_U32
        || input_stream_offset < advance)
    {
        return GE007_ROM_RNG_TRACE_INVALID_U32;
    }

    return input_stream_offset - advance;
}

static void ge007RomRngTraceRecord(
    u32 kind,
    u32 stream_offset,
    u32 stream_offset_after_block,
    u32 checksum_actual,
    u32 flags)
{
    u32 sequence;
    u32 index;
    volatile ge007_rom_rng_trace_event *event;

    sequence = g_Ge007RomRngTraceWriteIndex;
    index = sequence % GE007_ROM_RNG_TRACE_CAPACITY;
    event = &g_Ge007RomRngTrace[index];

    event->schema_version = GE007_ROM_RNG_TRACE_SCHEMA_VERSION;
    event->sequence = sequence;
    event->kind = kind;
    event->flags = flags;
    event->stream_offset = stream_offset;
    event->stream_offset_after_block = stream_offset_after_block;
    event->random_seed_hi = (u32)(g_randomSeed >> 32);
    event->random_seed_lo = (u32)g_randomSeed;
    event->seed_low = (u32)((u8)g_randomSeed);

    if (ramrom_blkbuf_2 != NULL) {
        event->block_randseed = (u32)ramrom_blkbuf_2->randseed;
        event->block_check = (u32)ramrom_blkbuf_2->check;
        event->block_count = (u32)ramrom_blkbuf_2->count;
        event->block_speedframes = (u32)ramrom_blkbuf_2->speedframes;
    } else {
        event->block_randseed = GE007_ROM_RNG_TRACE_INVALID_U32;
        event->block_check = GE007_ROM_RNG_TRACE_INVALID_U32;
        event->block_count = GE007_ROM_RNG_TRACE_INVALID_U32;
        event->block_speedframes = GE007_ROM_RNG_TRACE_INVALID_U32;
    }

    event->checksum_actual = checksum_actual;
    event->clock_timer = (u32)g_ClockTimer;
    event->global_timer = (u32)g_GlobalTimer;
    event->speedgraphframes_value = (u32)speedgraphframes;
    event->last_frame_counter = (u32)lastFrameCounter;
    event->current_frame_counter = (u32)currentFrameCounter;
    event->os_count_copy0 = copy_of_osgetcount_value_0;
    event->os_count_copy1 = copy_of_osgetcount_value_1;
    event->address_demo_loaded_value = (u32)address_demo_loaded;
    event->active_demo_base = g_Ge007RomRngTraceDemoBase;
    event->current_block_ptr = (u32)ramrom_blkbuf_2;
    event->payload_block_ptr = (u32)ramrom_blkbuf_3;

    if (sequence >= GE007_ROM_RNG_TRACE_CAPACITY) {
        g_Ge007RomRngTraceOverflowCount++;
    }
    g_Ge007RomRngTraceWriteIndex = sequence + 1;
}
#endif

#if defined(NATIVE_PORT)
void pcRamromGetTraceState(pc_ramrom_trace_state *out)
{
    uintptr_t cursor;
    u32 cursor_offset = 0;

    if (out == NULL) {
        return;
    }

    out->active = is_ramrom_flag;
    out->playback_pending = g_ramromPlayBackFlag;
    out->demo_related = ramrom_demo_related_3;
    out->recording = recording_ramrom_flag;
    out->stage = ptr_active_demofile != NULL ? (s32)ptr_active_demofile->stagenum : -1;
    out->difficulty = ptr_active_demofile != NULL ? (s32)ptr_active_demofile->difficulty : -1;
    out->size_cmds = ptr_active_demofile != NULL ? (s32)ptr_active_demofile->size_cmds : -1;
    out->total_time = ptr_active_demofile != NULL ? ptr_active_demofile->totaltime_ms : -1;
    out->file_size = ptr_active_demofile != NULL ? ptr_active_demofile->filesize : -1;
    out->base_offset = s_pcRamromTraceBaseOffset;
    out->cursor_offset = 0;
    out->input_stream_offset = 0;
    out->current_block_count = ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->count : -1;
    out->current_block_speedframes = ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->speedframes : -1;
    out->current_block_randseed = ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->randseed : -1;
    out->current_block_check = ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->check : -1;
    out->random_call_count = pcRandomGetNextCallCount();
    out->random_call_base = s_pcRamromTraceRandomCallBase;
    out->random_calls_since_restore = out->random_call_count - out->random_call_base;
    out->restored_random_seed = s_pcRamromTraceRestoredRandomSeed;
    out->random_seed_low = (s32)((u8)g_randomSeed);
    out->symbol = s_pcRamromTraceSymbol != NULL ? s_pcRamromTraceSymbol : "";
    out->last_stop_reason = s_pcRamromTraceStopReason != NULL ? s_pcRamromTraceStopReason : "";
    out->last_stop_event_count = s_pcRamromTraceStopEventCount;
    out->last_stop_input_stream_offset = s_pcRamromTraceStopInputStreamOffset;
    out->last_stop_random_call_count = s_pcRamromTraceStopRandomCallCount;
    out->last_stop_random_calls_since_restore = s_pcRamromTraceStopRandomCallsSinceRestore;
    out->last_stop_random_seed = s_pcRamromTraceStopRandomSeed;
    out->last_abort_buttons = s_pcRamromTraceAbortButtons;
    out->last_randseed_expected = s_pcRamromTraceRandseedExpected;
    out->last_randseed_actual = s_pcRamromTraceRandseedActual;
    out->last_checksum_expected = s_pcRamromTraceChecksumExpected;
    out->last_checksum_actual = s_pcRamromTraceChecksumActual;

    cursor = (uintptr_t)address_demo_loaded;
    if (s_pcRamromTraceBaseOffset != 0 && cursor >= (uintptr_t)s_pcRamromTraceBaseOffset) {
        cursor_offset = (u32)(cursor - (uintptr_t)s_pcRamromTraceBaseOffset);
        out->cursor_offset = cursor_offset;
        if (cursor_offset >= sizeof(ramromfilestructure)) {
            out->input_stream_offset = cursor_offset - (u32)sizeof(ramromfilestructure);
        }
    }
}
#endif


s32 interface_menu0B_runstage(void) {
    return g_ramromPlayBackFlag;
}

// Address 0x7F0BFCB0 NTSC.
void finalize_ramrom_on_hw(void)
{
    u8 buffer[0x28];
    u8 *p;
    void *a1;

    p = (u8 *)ALIGN16_a((uintptr_t)buffer);
    p[0] = 0;
    p[1] = 0;

    romWrite((void *) p, (void *) address_demo_loaded, 0x10U);
    
    address_demo_loaded += 4;

    a1 = INDY_RAMROM_DEMO_POINTER;

    ptr_active_demofile = romCopyAligned(ramrom_data_target, a1, 0xf0);
    ptr_active_demofile->totaltime_ms = g_GlobalTimer - g_ClockTimer;
#ifdef NATIVE_PORT
    ptr_active_demofile->filesize = (s32)((u8 *)address_demo_loaded - (u8 *)a1);
#else
    ptr_active_demofile->filesize = (s32)address_demo_loaded - (s32)a1;
#endif
    romWrite(ptr_active_demofile, a1, 0xf0);
}


// Address 0x7F0BFD60 NTSC.
void save_ramrom_to_devtool(void)
{
    int i;
    char indyFileName [256];
    s32 size;
    
    for (i = 1; ; i++)
    {
        ge007_sprintf(indyFileName, "replay/demo.%d", i);
        
        if (!indycommHostCheckFileExists(indyFileName, &size))
        {
            break;
        }
    }
    
    ge007_sprintf(indyFileName, "replay/demo.%d", i);
    indycommHostSaveFile(indyFileName, ptr_active_demofile->filesize, (u8 *)INDY_RAMROM_DEMO_POINTER);
}





void load_ramrom_from_devtool(void)
{

    static const char strDemoFileName[] = "replay/demo.load";
    s32 size;

    if (indycommHostCheckFileExists((char *)strDemoFileName, &size) != 0)
    {
        indycommHostRamRomLoad((char *)strDemoFileName, (u8 *)INDY_RAMROM_DEMO_ADDRESS, size);
        ptr_active_demofile = romCopyAligned(&ramrom_data_target, (u8 *)INDY_RAMROM_DEMO_ADDRESS, sizeof(struct ramromfilestructure));
    }
}






#ifdef NONMATCHING
#if defined(PORT_FIXME_STUBS) && !defined(NATIVE_PORT)
void record_player_input_as_packet(struct contsample *arg0, s32 arg1, s32 arg2) {
    (void)arg0; (void)arg1; (void)arg2;
}
#else
// Address 0x7F0BFE5C NTSC
//
// https://decomp.me/scratch/RNdWO 93%
void record_player_input_as_packet(struct contsample *arg0, s32 arg1, s32 arg2)
{
    uintptr_t temp_t5;
    s32 temp_t1;
    s32 var_a0;
    s32 var_a2;
    u8 var_a3;
    s32 var_t2;
    struct ramrom_blockbuf *temp_v0;
    s32 temp_s0;
    u8 t1;
    s32 others0;
#ifdef NATIVE_PORT
    u32 pc_record_stream_offset;

    pc_record_stream_offset = pcRamromCurrentInputStreamOffset();
    pcRamromTraceLoopPhaseWithDetails("record_func_enter", arg1, arg2, pc_record_stream_offset);
#endif

    temp_t5 = (uintptr_t)ALIGN16_a((uintptr_t)&ramrom_data_target[0x1f8]);
    temp_t1 = ptr_active_demofile->size_cmds;

    var_t2 = 0;
    var_a3 = 0;
    
    ramrom_blkbuf_2 = (struct ramrom_seed *)temp_t5;
    ramrom_blkbuf_3 = (struct ramrom_blockbuf *)(ramrom_blkbuf_2 + 1);

    // loop structure based on: void joyConsumeSamples(struct contdata *contdata)
    if (arg1 != arg2)
    {
        var_a2 = (s32) (arg1 + 1) % CONTSAMPLE_LEN;
        while (1)
        {
            for (var_a0 = 0; var_a0 < temp_t1; var_a0++)
            {
                temp_v0 = ramrom_blkbuf_3 + (var_t2 * temp_t1) + var_a0;

                temp_v0->stick_x = arg0->pads[var_a2*4 + var_a0].stick_x;
                temp_v0->stick_y = arg0->pads[var_a2*4 + var_a0].stick_y;
                temp_v0->button_low = arg0->pads[var_a2*4 + var_a0].button;
                temp_v0->button_high = arg0->pads[var_a2*4 + var_a0].button >> 8;
    
                var_a3 += (u8) (
                    (u8)temp_v0->stick_x
                    + (u8)temp_v0->stick_y
                    + temp_v0->button_low
                    + temp_v0->button_high);
            }
    
            var_t2++;

            if (var_a2 == arg2)
            {
                break;
            }

            var_a2 = (s32) (var_a2 + 1) % CONTSAMPLE_LEN;
        }
    }
        
    ramrom_blkbuf_2->count = var_t2;
    ramrom_blkbuf_2->speedframes = speedgraphframes;
    ramrom_blkbuf_2->randseed = g_randomSeed;
    
    var_a3 += (u8) ((u8)ramrom_blkbuf_2->speedframes + (u8)ramrom_blkbuf_2->count + ramrom_blkbuf_2->randseed);
    ramrom_blkbuf_2->check = var_a3;

    temp_s0 = (temp_t1 * 4 * var_t2) + sizeof(s32);

#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("record_header_ready",
                                      ramrom_blkbuf_2->count,
                                      ramrom_blkbuf_2->speedframes,
                                      ramrom_blkbuf_2->check);
#endif

    romWrite((void *) ramrom_blkbuf_2, address_demo_loaded, ALIGN16_a(temp_s0));

    address_demo_loaded += align_addr_even(temp_s0 + 1);

#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("record_func_exit",
                                      pc_record_stream_offset,
                                      pcRamromCurrentInputStreamOffset(),
                                      var_t2);
#endif
}
#endif /* PORT_FIXME_STUBS */
#else
GLOBAL_ASM(
.text
glabel record_player_input_as_packet
/* 0F498C 7F0BFE5C 27BDFFE0 */  addiu $sp, $sp, -0x20
/* 0F4990 7F0BFE60 3C0E8005 */  lui   $t6, %hi(ptr_active_demofile)
/* 0F4994 7F0BFE64 3C0F8009 */  lui   $t7, %hi(ramrom_data_target + 0x1F8)
/* 0F4998 7F0BFE68 8DCE8468 */  lw    $t6, %lo(ptr_active_demofile)($t6)
/* 0F499C 7F0BFE6C 25EFC468 */  addiu $t7, %lo(ramrom_data_target + 0x1F8) # addiu $t7, $t7, -0x3b98
/* 0F49A0 7F0BFE70 25F8000F */  addiu $t8, $t7, 0xf
/* 0F49A4 7F0BFE74 AFBF001C */  sw    $ra, 0x1c($sp)
/* 0F49A8 7F0BFE78 AFB10018 */  sw    $s1, 0x18($sp)
/* 0F49AC 7F0BFE7C AFB00014 */  sw    $s0, 0x14($sp)
/* 0F49B0 7F0BFE80 3719000F */  ori   $t9, $t8, 0xf
/* 0F49B4 7F0BFE84 3C088005 */  lui   $t0, %hi(ramrom_blkbuf_3)
/* 0F49B8 7F0BFE88 3C0C8005 */  lui   $t4, %hi(ramrom_blkbuf_2)
/* 0F49BC 7F0BFE8C 3B2D000F */  xori  $t5, $t9, 0xf
/* 0F49C0 7F0BFE90 8DC90018 */  lw    $t1, 0x18($t6)
/* 0F49C4 7F0BFE94 258C846C */  addiu $t4, %lo(ramrom_blkbuf_2) # addiu $t4, $t4, -0x7b94
/* 0F49C8 7F0BFE98 25088470 */  addiu $t0, %lo(ramrom_blkbuf_3) # addiu $t0, $t0, -0x7b90
/* 0F49CC 7F0BFE9C 25AE0004 */  addiu $t6, $t5, 4
/* 0F49D0 7F0BFEA0 00C08025 */  move  $s0, $a2
/* 0F49D4 7F0BFEA4 00808825 */  move  $s1, $a0
/* 0F49D8 7F0BFEA8 00005025 */  move  $t2, $zero
/* 0F49DC 7F0BFEAC 00003825 */  move  $a3, $zero
/* 0F49E0 7F0BFEB0 AD8D0000 */  sw    $t5, ($t4)
/* 0F49E4 7F0BFEB4 AD0E0000 */  sw    $t6, ($t0)
/* 0F49E8 7F0BFEB8 10A60048 */  beq   $a1, $a2, .L7F0BFFDC
/* 0F49EC 7F0BFEBC 01A01025 */   move  $v0, $t5
/* 0F49F0 7F0BFEC0 240B0014 */  li    $t3, 20
/* 0F49F4 7F0BFEC4 24AF0001 */  addiu $t7, $a1, 1
/* 0F49F8 7F0BFEC8 01EB001A */  div   $zero, $t7, $t3
/* 0F49FC 7F0BFECC 00003010 */  mfhi  $a2
/* 0F4A00 7F0BFED0 00002025 */  move  $a0, $zero
/* 0F4A04 7F0BFED4 15600002 */  bnez  $t3, .L7F0BFEE0
/* 0F4A08 7F0BFED8 00000000 */   nop
/* 0F4A0C 7F0BFEDC 0007000D */  break 7
.L7F0BFEE0:
/* 0F4A10 7F0BFEE0 2401FFFF */  li    $at, -1
/* 0F4A14 7F0BFEE4 15610004 */  bne   $t3, $at, .L7F0BFEF8
/* 0F4A18 7F0BFEE8 3C018000 */   lui   $at, 0x8000
/* 0F4A1C 7F0BFEEC 15E10002 */  bne   $t7, $at, .L7F0BFEF8
/* 0F4A20 7F0BFEF0 00000000 */   nop
/* 0F4A24 7F0BFEF4 0006000D */  break 6
.L7F0BFEF8:
/* 0F4A28 7F0BFEF8 19200025 */  blez  $t1, .L7F0BFF90
/* 0F4A2C 7F0BFEFC 00000000 */   nop
/* 0F4A30 7F0BFF00 01490019 */  multu $t2, $t1
/* 0F4A34 7F0BFF04 0006C880 */  sll   $t9, $a2, 2
/* 0F4A38 7F0BFF08 00196880 */  sll   $t5, $t9, 2
/* 0F4A3C 7F0BFF0C 01B96823 */  subu  $t5, $t5, $t9
/* 0F4A40 7F0BFF10 000D6840 */  sll   $t5, $t5, 1
/* 0F4A44 7F0BFF14 022D1821 */  addu  $v1, $s1, $t5
/* 0F4A48 7F0BFF18 00002812 */  mflo  $a1
/* 0F4A4C 7F0BFF1C 0005C080 */  sll   $t8, $a1, 2
/* 0F4A50 7F0BFF20 03002825 */  move  $a1, $t8
.L7F0BFF24:
/* 0F4A54 7F0BFF24 8D0E0000 */  lw    $t6, ($t0)
/* 0F4A58 7F0BFF28 80790002 */  lb    $t9, 2($v1)
/* 0F4A5C 7F0BFF2C 0004C080 */  sll   $t8, $a0, 2
/* 0F4A60 7F0BFF30 00AE7821 */  addu  $t7, $a1, $t6
/* 0F4A64 7F0BFF34 01F81021 */  addu  $v0, $t7, $t8
/* 0F4A68 7F0BFF38 A0590000 */  sb    $t9, ($v0)
/* 0F4A6C 7F0BFF3C 806D0003 */  lb    $t5, 3($v1)
/* 0F4A70 7F0BFF40 24840001 */  addiu $a0, $a0, 1
/* 0F4A74 7F0BFF44 24630006 */  addiu $v1, $v1, 6
/* 0F4A78 7F0BFF48 A04D0001 */  sb    $t5, 1($v0)
/* 0F4A7C 7F0BFF4C 946FFFFA */  lhu   $t7, -6($v1)
/* 0F4A80 7F0BFF50 904E0001 */  lbu   $t6, 1($v0)
/* 0F4A84 7F0BFF54 904D0000 */  lbu   $t5, ($v0)
/* 0F4A88 7F0BFF58 A04F0002 */  sb    $t7, 2($v0)
/* 0F4A8C 7F0BFF5C 9478FFFA */  lhu   $t8, -6($v1)
/* 0F4A90 7F0BFF60 01AE7821 */  addu  $t7, $t5, $t6
/* 0F4A94 7F0BFF64 0018CA03 */  sra   $t9, $t8, 8
/* 0F4A98 7F0BFF68 90580002 */  lbu   $t8, 2($v0)
/* 0F4A9C 7F0BFF6C A0590003 */  sb    $t9, 3($v0)
/* 0F4AA0 7F0BFF70 904D0003 */  lbu   $t5, 3($v0)
/* 0F4AA4 7F0BFF74 01F8C821 */  addu  $t9, $t7, $t8
/* 0F4AA8 7F0BFF78 032D7021 */  addu  $t6, $t9, $t5
/* 0F4AAC 7F0BFF7C 31CF00FF */  andi  $t7, $t6, 0xff
/* 0F4AB0 7F0BFF80 00EF3821 */  addu  $a3, $a3, $t7
/* 0F4AB4 7F0BFF84 30F800FF */  andi  $t8, $a3, 0xff
/* 0F4AB8 7F0BFF88 1489FFE6 */  bne   $a0, $t1, .L7F0BFF24
/* 0F4ABC 7F0BFF8C 03003825 */   move  $a3, $t8
.L7F0BFF90:
/* 0F4AC0 7F0BFF90 14D00003 */  bne   $a2, $s0, .L7F0BFFA0
/* 0F4AC4 7F0BFF94 254A0001 */   addiu $t2, $t2, 1
/* 0F4AC8 7F0BFF98 10000010 */  b     .L7F0BFFDC
/* 0F4ACC 7F0BFF9C 8D820000 */   lw    $v0, ($t4)
.L7F0BFFA0:
/* 0F4AD0 7F0BFFA0 24D90001 */  addiu $t9, $a2, 1
/* 0F4AD4 7F0BFFA4 032B001A */  div   $zero, $t9, $t3
/* 0F4AD8 7F0BFFA8 00003010 */  mfhi  $a2
/* 0F4ADC 7F0BFFAC 00002025 */  move  $a0, $zero
/* 0F4AE0 7F0BFFB0 15600002 */  bnez  $t3, .L7F0BFFBC
/* 0F4AE4 7F0BFFB4 00000000 */   nop
/* 0F4AE8 7F0BFFB8 0007000D */  break 7
.L7F0BFFBC:
/* 0F4AEC 7F0BFFBC 2401FFFF */  li    $at, -1
/* 0F4AF0 7F0BFFC0 15610004 */  bne   $t3, $at, .L7F0BFFD4
/* 0F4AF4 7F0BFFC4 3C018000 */   lui   $at, 0x8000
/* 0F4AF8 7F0BFFC8 17210002 */  bne   $t9, $at, .L7F0BFFD4
/* 0F4AFC 7F0BFFCC 00000000 */   nop
/* 0F4B00 7F0BFFD0 0006000D */  break 6
.L7F0BFFD4:
/* 0F4B04 7F0BFFD4 1000FFC8 */  b     .L7F0BFEF8
/* 0F4B08 7F0BFFD8 00000000 */   nop
.L7F0BFFDC:
/* 0F4B0C 7F0BFFDC A04A0001 */  sb    $t2, 1($v0)
/* 0F4B10 7F0BFFE0 3C0D8005 */  lui   $t5, %hi(speedgraphframes)
/* 0F4B14 7F0BFFE4 8DAD8498 */  lw    $t5, %lo(speedgraphframes)($t5)
/* 0F4B18 7F0BFFE8 8D8E0000 */  lw    $t6, ($t4)
/* 0F4B1C 7F0BFFEC 3C198002 */  lui   $t9, %hi(g_randomSeed + 0x4)
/* 0F4B20 7F0BFFF0 3C118009 */  lui   $s1, %hi(address_demo_loaded)
/* 0F4B24 7F0BFFF4 A1CD0000 */  sb    $t5, ($t6)
/* 0F4B28 7F0BFFF8 8D8D0000 */  lw    $t5, ($t4)
/* 0F4B2C 7F0BFFFC 8F394464 */  lw    $t9, %lo(g_randomSeed + 0x4)($t9)
/* 0F4B30 7F0C0000 2631C5F4 */  addiu $s1, %lo(address_demo_loaded) # addiu $s1, $s1, -0x3a0c
/* 0F4B34 7F0C0004 A1B90002 */  sb    $t9, 2($t5)
/* 0F4B38 7F0C0008 8D820000 */  lw    $v0, ($t4)
/* 0F4B3C 7F0C000C 904E0000 */  lbu   $t6, ($v0)
/* 0F4B40 7F0C0010 90580001 */  lbu   $t8, 1($v0)
/* 0F4B44 7F0C0014 904F0002 */  lbu   $t7, 2($v0)
/* 0F4B48 7F0C0018 01D8C821 */  addu  $t9, $t6, $t8
/* 0F4B4C 7F0C001C 032F7021 */  addu  $t6, $t9, $t7
/* 0F4B50 7F0C0020 0009C880 */  sll   $t9, $t1, 2
/* 0F4B54 7F0C0024 032A0019 */  multu $t9, $t2
/* 0F4B58 7F0C0028 00EEC021 */  addu  $t8, $a3, $t6
/* 0F4B5C 7F0C002C A0580003 */  sb    $t8, 3($v0)
/* 0F4B60 7F0C0030 8E250000 */  lw    $a1, ($s1)
/* 0F4B64 7F0C0034 8D840000 */  lw    $a0, ($t4)
/* 0F4B68 7F0C0038 00008012 */  mflo  $s0
/* 0F4B6C 7F0C003C 26100004 */  addiu $s0, $s0, 4
/* 0F4B70 7F0C0040 2606000F */  addiu $a2, $s0, 0xf
/* 0F4B74 7F0C0044 34CF000F */  ori   $t7, $a2, 0xf
/* 0F4B78 7F0C0048 0C001742 */  jal   romWrite
/* 0F4B7C 7F0C004C 39E6000F */   xori  $a2, $t7, 0xf
/* 0F4B80 7F0C0050 8E2E0000 */  lw    $t6, ($s1)
/* 0F4B84 7F0C0054 26180001 */  addiu $t8, $s0, 1
/* 0F4B88 7F0C0058 8FBF001C */  lw    $ra, 0x1c($sp)
/* 0F4B8C 7F0C005C 37190001 */  ori   $t9, $t8, 1
/* 0F4B90 7F0C0060 3B2F0001 */  xori  $t7, $t9, 1
/* 0F4B94 7F0C0064 3C018009 */  lui   $at, %hi(address_demo_loaded)
/* 0F4B98 7F0C0068 8FB00014 */  lw    $s0, 0x14($sp)
/* 0F4B9C 7F0C006C 8FB10018 */  lw    $s1, 0x18($sp)
/* 0F4BA0 7F0C0070 01CF6821 */  addu  $t5, $t6, $t7
/* 0F4BA4 7F0C0074 AC2DC5F4 */  sw    $t5, %lo(address_demo_loaded)($at)
/* 0F4BA8 7F0C0078 03E00008 */  jr    $ra
/* 0F4BAC 7F0C007C 27BD0020 */   addiu $sp, $sp, 0x20
)
#endif




// Address 0x7F0C0080 NTSC.
s32 ramrom_replay_handler(struct contsample *arg0, s32 arg1)
{
    s32 padding[2];
    s32 var_a3;
    s32 var_a0;
    struct ramrom_blockbuf *temp_v0;
    u8 var_t0;
    s32 temp_a2;
    s32 temp_t2;
    u16 abort_buttons;
#ifdef NATIVE_PORT
    s32 pc_ramrom_checks_ok;
#endif

    var_t0 = 0;
#ifdef NATIVE_PORT
    pc_ramrom_checks_ok = TRUE;
#endif
    temp_a2 = (s32) ptr_active_demofile->size_cmds;
    temp_t2 = ramrom_blkbuf_2->count;
#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("replay_handler_enter", arg1, temp_t2, temp_a2);
#endif
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_HANDLER_ENTER,
        ge007RomRngTraceCurrentBlockStreamOffset(),
        ge007RomRngTraceInputStreamOffset(),
        GE007_ROM_RNG_TRACE_INVALID_U32,
        (u32)arg1);
#endif

    for (var_a3 = 0; var_a3 < temp_t2; var_a3++)
    {
        arg1 = (s32) (arg1 + 1) % CONTSAMPLE_LEN;

        for (var_a0 = 0; var_a0 < MAXCONTROLLERS; var_a0++)
        {
            if (var_a0 < temp_a2)
            {
                temp_v0 = ramrom_blkbuf_3 + (var_a3 * temp_a2) + var_a0;

                arg0->pads[arg1 * 4 + var_a0].stick_x = temp_v0->stick_x;
                arg0->pads[arg1 * 4 + var_a0].stick_y = temp_v0->stick_y;
                arg0->pads[arg1 * 4 + var_a0].button = (temp_v0->button_high << 8) | temp_v0->button_low;

                var_t0 += (u8)((u8)temp_v0->stick_x + (u8)temp_v0->stick_y + temp_v0->button_low + temp_v0->button_high);
            }
            else
            {
                arg0->pads[arg1 * 4 + var_a0].stick_x = 0;
                arg0->pads[arg1 * 4 + var_a0].stick_y = 0;
                arg0->pads[arg1 * 4 + var_a0].button = 0;
            }
        }
    }

#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("replay_handler_after_samples", arg1, temp_t2, var_t0);
#endif
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_HANDLER_AFTER_SAMPLES,
        ge007RomRngTraceCurrentBlockStreamOffset(),
        ge007RomRngTraceInputStreamOffset(),
        (u32)var_t0,
        (u32)arg1);
#endif

#ifdef NATIVE_PORT
    pcRamromTraceBlockTiming("RAMROM-CHECK",
                             "handler",
                             s_pcRamromTraceCurrentBlockStreamOffset,
                             s_pcRamromTraceCurrentBlockAfterOffset);
#endif

    if (ramrom_blkbuf_2->randseed != (u8)g_randomSeed)
    {
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_HANDLER_SEED_MISMATCH,
            ge007RomRngTraceCurrentBlockStreamOffset(),
            ge007RomRngTraceInputStreamOffset(),
            GE007_ROM_RNG_TRACE_INVALID_U32,
            (u32)arg1);
#endif
#ifdef NATIVE_PORT
        if (pcRamromCurrentBlockSeedPrevalidationMatches()) {
            pcRamromTraceLoopPhaseWithDetails("replay_handler_seed_prevalidated",
                                              ramrom_blkbuf_2->randseed,
                                              (u8)g_randomSeed,
                                              (s32)s_pcRamromUpcomingSeedPrevalidatedCallsSinceRestore);
        } else {
            pcRamromMarkStopReason("randseed_mismatch",
                                   0,
                                   ramrom_blkbuf_2->randseed,
                                   (u8)g_randomSeed,
                                   -1,
                                   -1);
            pc_ramrom_checks_ok = FALSE;
        }
#else
        ramromFadeToTitle();
#endif
#ifdef NATIVE_PORT
        if (!pc_ramrom_checks_ok) {
            ramromFadeToTitle();
        }
#endif
    }
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    else
    {
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_HANDLER_SEED_MATCH,
            ge007RomRngTraceCurrentBlockStreamOffset(),
            ge007RomRngTraceInputStreamOffset(),
            GE007_ROM_RNG_TRACE_INVALID_U32,
            (u32)arg1);
    }
#endif

    var_t0 += (u8)((u8)ramrom_blkbuf_2->speedframes + (u8)ramrom_blkbuf_2->count + ramrom_blkbuf_2->randseed);
    if (ramrom_blkbuf_2->check != var_t0)
    {
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_HANDLER_CHECKSUM_MISMATCH,
            ge007RomRngTraceCurrentBlockStreamOffset(),
            ge007RomRngTraceInputStreamOffset(),
            (u32)var_t0,
            (u32)arg1);
#endif
#ifdef NATIVE_PORT
        pcRamromMarkStopReason("checksum_mismatch",
                               0,
                               ramrom_blkbuf_2->randseed,
                               (u8)g_randomSeed,
                               ramrom_blkbuf_2->check,
                               var_t0);
        pc_ramrom_checks_ok = FALSE;
#endif
        ramromFadeToTitle();
    }

#ifdef NATIVE_PORT
    if (pc_ramrom_checks_ok) {
        pcRamromRunDeferredFirstBlockRngConsumers();
        pcRamromRunDeferredUpcomingBlockBoundaryActionTicks();
    }
    pcRamromTraceLoopPhaseWithDetails("replay_handler_after_checks",
                                      pc_ramrom_checks_ok,
                                      ramrom_blkbuf_2->randseed,
                                      (u8)g_randomSeed);
    pcRamromClearCurrentBlockSeedPrevalidation();
#endif
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_HANDLER_AFTER_CHECKS,
        ge007RomRngTraceCurrentBlockStreamOffset(),
        ge007RomRngTraceInputStreamOffset(),
        (u32)var_t0,
        (u32)arg1);
#endif
    
    joySetContDataIndex(0);
    
    abort_buttons = joyGetButtonsPressedThisFrame(0, 0xFFFFU);
    if (abort_buttons != 0)
    {
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_ABORT_BUTTONS,
            ge007RomRngTraceCurrentBlockStreamOffset(),
            ge007RomRngTraceInputStreamOffset(),
            (u32)var_t0,
            (u32)abort_buttons);
#endif
#ifdef NATIVE_PORT
        pcRamromMarkStopReason("abort_buttons",
                               abort_buttons,
                               ramrom_blkbuf_2->randseed,
                               (u8)g_randomSeed,
                               ramrom_blkbuf_2->check,
                               var_t0);
#endif
        ramromFadeToTitle();
        prev_keypresses = TRUE;
    }

    joySetContDataIndex(1);

#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("replay_handler_exit", arg1, abort_buttons, pc_ramrom_checks_ok);
#endif
    return arg1;
}



// Address 0x7F0C0268 NTSC.
void iterate_ramrom_entries_handle_camera_out(void)
{
    s32 temp_v1;
    s32 var_a3;
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    u32 rom_trace_stream_offset;
    u32 rom_trace_stream_offset_after_block;
#endif
#ifdef NATIVE_PORT
    u32 pc_trace_stream_offset;
    u32 pc_trace_stream_offset_after_block;
#endif
    
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    rom_trace_stream_offset = ge007RomRngTraceInputStreamOffset();
    rom_trace_stream_offset_after_block = GE007_ROM_RNG_TRACE_INVALID_U32;
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_ITERATE_ENTER,
        rom_trace_stream_offset,
        rom_trace_stream_offset_after_block,
        GE007_ROM_RNG_TRACE_INVALID_U32,
        0);
#endif
#ifdef NATIVE_PORT
    pc_trace_stream_offset = pcRamromCurrentInputStreamOffset();
    pcRamromTraceLoopPhaseWithDetails("ramrom_iterate_enter", pc_trace_stream_offset, 0, 0);
#endif
    ramrom_blkbuf_2 = romCopyAligned(ramrom_data_target + 0x1F8, address_demo_loaded, sizeof(struct ramrom_seed));
#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("ramrom_iterate_after_header",
                                      ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->count : -1,
                                      ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->speedframes : -1,
                                      ramrom_blkbuf_2 != NULL ? ramrom_blkbuf_2->randseed : -1);
#endif

    var_a3 = ramrom_blkbuf_2->count;
    if (var_a3 > 0)
    {
        ramrom_blkbuf_3 = romCopyAligned(
            ramrom_data_target + 0x21E,
            address_demo_loaded + 4,
            ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count);
    }

    var_a3 = ramrom_blkbuf_2->count;
    if (var_a3 == 0 && ramrom_blkbuf_2->speedframes == 0)
    {
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_TERMINATOR,
            rom_trace_stream_offset,
            rom_trace_stream_offset_after_block,
            GE007_ROM_RNG_TRACE_INVALID_U32,
            0);
#endif
#ifdef NATIVE_PORT
        pcRamromMarkStopReason("terminator", 0, -1, -1, -1, -1);
#endif
        ramromFadeToTitle();
    }
    else
    {
        // 5 is ??
#ifdef NATIVE_PORT
        pc_trace_stream_offset_after_block =
            pc_trace_stream_offset +
            (u32)align_addr_even((ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count) + 5);
        s_pcRamromTraceCurrentBlockStreamOffset = pc_trace_stream_offset;
        s_pcRamromTraceCurrentBlockAfterOffset = pc_trace_stream_offset_after_block;
        s_pcRamromTraceBlockLoadCount++;
        pcRamromAttachPrevalidatedSeedToCurrentBlock(pc_trace_stream_offset,
                                                     pc_trace_stream_offset_after_block);
        pcRamromTraceBlockTiming("RAMROM-BLOCK",
                                 "load",
                                 pc_trace_stream_offset,
                                 pc_trace_stream_offset_after_block);
#endif
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        rom_trace_stream_offset_after_block =
            rom_trace_stream_offset +
            (u32)align_addr_even((ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count) + 5);
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_BLOCK_LOAD,
            rom_trace_stream_offset,
            rom_trace_stream_offset_after_block,
            GE007_ROM_RNG_TRACE_INVALID_U32,
            0);
#endif
        address_demo_loaded += align_addr_even((ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count) + 5);
#ifdef NATIVE_PORT
        pcRamromTraceLoopPhaseWithDetails("ramrom_iterate_after_advance",
                                          pc_trace_stream_offset,
                                          pc_trace_stream_offset_after_block,
                                          ramrom_blkbuf_2->count);
#endif
    }

    updateFrameCounters(ramrom_blkbuf_2->speedframes);
#ifdef NATIVE_PORT
    pcRamromTraceLoopPhaseWithDetails("ramrom_iterate_after_update_frame_counters",
                                      g_ClockTimer,
                                      g_GlobalTimer,
                                      speedgraphframes);
#endif
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_AFTER_UPDATE_FRAME_COUNTERS,
        rom_trace_stream_offset,
        rom_trace_stream_offset_after_block,
        GE007_ROM_RNG_TRACE_INVALID_U32,
        0);
#endif

    // BUG? Does this need to be adjusted for PAL?
    temp_v1 = ptr_active_demofile->totaltime_ms - 0x3C;
    if(0);
    if ((g_GlobalTimer >= temp_v1) && ((g_GlobalTimer - g_ClockTimer) < temp_v1))
    {
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
        ge007RomRngTraceRecord(
            GE007_ROM_RNG_TRACE_KIND_TOTAL_TIME_STOP,
            rom_trace_stream_offset,
            rom_trace_stream_offset_after_block,
            GE007_ROM_RNG_TRACE_INVALID_U32,
            (u32)temp_v1);
#endif
#ifdef NATIVE_PORT
        pcRamromMarkStopReason("total_time", 0, -1, -1, -1, -1);
#endif
        ramromFadeToTitle();
    }
}

void copy_current_ingame_registers_before_ramrom_playback(ramromfilestructure *state)
{
    state->randomseed = g_randomSeed;
    state->randomizer = g_chrObjRandomSeed;
    state->mode = gamemode;
    state->numplayers = selected_num_players;
    state->scenario = scenario;
    state->mpstage_sel = MP_stage_selected;
    state->gamelength = game_length;
    state->mp_weapon_set = getMPWeaponSet();
    state->mp_char[0] = player_char[0];
    state->mp_char[1] = player_char[1];
    state->mp_char[2] = player_char[2];
    state->mp_char[3] = player_char[3];
    state->mp_handi[0] = player_handicap[0];
    state->mp_handi[1] = player_handicap[1];
    state->mp_handi[2] = player_handicap[2];
    state->mp_handi[3] = player_handicap[3];
    state->mp_contstyle[0] = controlstyle_player[0];
    state->mp_contstyle[1] = controlstyle_player[1];
    state->mp_contstyle[2] = controlstyle_player[2];
    state->mp_contstyle[3] = controlstyle_player[3];
    state->aim_option = aim_sight_adjustment;
    state->mp_flags[0] = get_players_team_or_scenario_item_flag(0);
    state->mp_flags[1] = get_players_team_or_scenario_item_flag(1);
    state->mp_flags[2] = get_players_team_or_scenario_item_flag(2);
    state->mp_flags[3] = get_players_team_or_scenario_item_flag(3);
}

void copy_recorded_ramrom_registers_to_proper_place_ingame(ramromfilestructure *state)
{
    g_randomSeed = state->randomseed;
    g_chrObjRandomSeed = state->randomizer;
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceReset();
    ge007RomRngTraceRecord(
        GE007_ROM_RNG_TRACE_KIND_RESTORE,
        ge007RomRngTraceInputStreamOffset(),
        ge007RomRngTraceInputStreamOffset(),
        GE007_ROM_RNG_TRACE_INVALID_U32,
        0);
#endif
#ifdef NATIVE_PORT
    s_pcRamromTraceRandomCallBase = pcRandomGetNextCallCount();
    s_pcRamromTraceRestoredRandomSeed = g_randomSeed;
    pcRandomTraceSetRamromActive(1);
    pcRamromTraceLoopPhase("playback_registers_restored");
#endif
    gamemode = state->mode;
    selected_num_players = state->numplayers;
    scenario = state->scenario;
    MP_stage_selected = state->mpstage_sel;
    game_length = state->gamelength;
    setMPWeaponSet(state->mp_weapon_set);
    player_char[0] = state->mp_char[0];
    player_char[1] = state->mp_char[1];
    player_char[2] = state->mp_char[2];
    player_char[3] = state->mp_char[3];
    player_handicap[0] = state->mp_handi[0];
    player_handicap[1] = state->mp_handi[1];
    player_handicap[2] = state->mp_handi[2];
    player_handicap[3] = state->mp_handi[3];
    controlstyle_player[0] = state->mp_contstyle[0];
    controlstyle_player[1] = state->mp_contstyle[1];
    controlstyle_player[2] = state->mp_contstyle[2];
    controlstyle_player[3] = state->mp_contstyle[3];
    aim_sight_adjustment = state->aim_option;
    set_players_team_or_scenario_item_flag(0, state->mp_flags[0]);
    set_players_team_or_scenario_item_flag(1, state->mp_flags[1]);
    set_players_team_or_scenario_item_flag(2, state->mp_flags[2]);
    set_players_team_or_scenario_item_flag(3, state->mp_flags[3]);
}

// Address 0x7F0C0640 NTSC
void test_if_recording_demos_this_stage_load(enum LEVELID arg0, enum DIFFICULTY arg1)
{
    if (g_ramromRecordFlag != 0)
    {
        ptr_active_demofile = (ramromfilestructure *)ALIGN16_a((uintptr_t)ramrom_data_target);
        dword_CODE_bss_8008C5F8 = 0;
        ptr_active_demofile->stagenum = arg0;
        ptr_active_demofile->difficulty = arg1;
        ptr_active_demofile->size_cmds = joyGetControllerCount();
        ptr_active_demofile->slotnum = record_slot_num;
        sub_GAME_7F01D61C(&ptr_active_demofile->savefile);
        copy_current_ingame_registers_before_ramrom_playback(ptr_active_demofile);
        recording_ramrom_flag = 1;
        ramrom_demo_related_6 = 1;
        joySetRecordFunc(record_player_input_as_packet);
        address_demo_loaded = INDY_RAMROM_DEMO_POINTER;
        romWrite(ptr_active_demofile, address_demo_loaded, 0xF0U);
        address_demo_loaded += sizeof(struct ramromfilestructure);
        g_ramromRecordFlag = 0;
        
        return;
    }
    
    if (g_ramromPlayBackFlag != 0)
    {
        dword_CODE_bss_8008C5F8 = 0;
        set_selected_difficulty(ptr_active_demofile->difficulty);
        set_solo_and_ptr_briefing(ptr_active_demofile->stagenum);
        set_selected_foldernum_and_copy_demo_eeprom(&ptr_active_demofile->savefile);
        copy_current_ingame_registers_before_ramrom_playback((ramromfilestructure *) (ramrom_data_target + 0x110));
        copy_recorded_ramrom_registers_to_proper_place_ingame(ptr_active_demofile);
        is_ramrom_flag = 1;
        ramrom_demo_related_3 = 1;
        joySetPlaybackFunc(ramrom_replay_handler, ptr_active_demofile->size_cmds);
        joySetContDataIndex(1);
#ifdef NATIVE_PORT
        pcRamromTraceLoopPhase("playback_activated");
#endif
        g_ramromPlayBackFlag = 0;
    }
}





void setRamRomRecordSlot(s32 arg0)
{
    g_ramromRecordFlag = 1;
    record_slot_num = arg0;
}

void stop_recording_ramrom(void)
{
    if (ramrom_demo_related_6 != 0)
    {
        finalize_ramrom_on_hw();
        joySetRecordFunc(0);
        ramrom_demo_related_6 = 0;
        recording_ramrom_flag = 0;
    }
}

void replay_recorded_ramrom_at_address(ramromfilestructure *demofile)
{
#ifdef NATIVE_PORT
    pcRamromSetTraceSource(demofile);
#endif
    address_demo_loaded = (u8 *)demofile;
#if defined(TARGET_N64) && defined(GE007_ROM_RNG_TRACE)
    ge007RomRngTraceSetDemoBase(demofile);
#endif
    ptr_active_demofile = romCopyAligned(&ramrom_data_target, address_demo_loaded, sizeof(struct ramromfilestructure));
#ifdef NATIVE_PORT
    ramromByteswapHeader(ptr_active_demofile);
#endif
    address_demo_loaded += sizeof(ramromfilestructure);
    g_ramromPlayBackFlag = 1;
    set_solo_and_ptr_briefing(ptr_active_demofile->stagenum);
    set_selected_difficulty(ptr_active_demofile->difficulty);
    frontChangeMenu(MENU_RUN_STAGE,1);
}

void replay_recorded_ramrom_from_indy(void)
{
    replay_recorded_ramrom_at_address((ramromfilestructure *)INDY_RAMROM_DEMO_ADDRESS);
}

void ramromFadeToTitle(void)
{
    if (bondviewGetCameraMode() != CAMERAMODE_FADE_TO_TITLE)
    {
        bondviewSetCameraMode(CAMERAMODE_FADE_TO_TITLE);
    }
}

void stop_demo_playback(void)
{
    if (ramrom_demo_related_6 != 0)
    {
        stop_recording_ramrom();
        return;
    }
    if (ramrom_demo_related_3 != 0)
    {
#ifdef NATIVE_PORT
        pcRamromTraceLoopPhase("playback_stopping");
#endif
        copy_recorded_ramrom_registers_to_proper_place_ingame((ramromfilestructure *)(ramrom_data_target + 0x110));
        joySetPlaybackFunc(0, -1);
        joySetContDataIndex(0);
        ramrom_demo_related_3 = 0;
        is_ramrom_flag = 0;
#ifdef NATIVE_PORT
        pcRandomTraceSetRamromActive(0);
#endif
    }
}



// Address 0x7F0C0970 NTSC.
void select_ramrom_to_play(void)
{
    s32 i;
    s32 temp_v0;

    temp_v0 = fileGetHighestStageUnlockedAnyFolder();

    for (i = 0; ramrom_table[i].fdata != NULL && temp_v0 >= ramrom_table[i].locked; i++)
    {}

    replay_recorded_ramrom_at_address(ramrom_table[randomGetNext() % i].fdata);
}





u32 check_ramrom_flags(void)
{
    if ((get_is_ramrom_flag() != 0) || (get_recording_ramrom_flag() != 0))
    {
        return ptr_active_demofile->slotnum;

    }
    return 0;
}
