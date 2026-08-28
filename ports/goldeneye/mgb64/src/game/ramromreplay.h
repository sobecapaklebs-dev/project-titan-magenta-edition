#ifndef _RAMROMREPLAY_H_
#define _RAMROMREPLAY_H_

#include <ultra64.h>
#include "file.h"
#include "bondconstants.h"

#define INDY_RAMROM_DEMO_ADDRESS 0x00F00000
#define INDY_RAMROM_DEMO_POINTER ((void*)INDY_RAMROM_DEMO_ADDRESS)

typedef struct ramromfilestructure {
    u64 randomseed;
    u64 randomizer;
    enum LEVELID stagenum;
    enum DIFFICULTY difficulty;
    u32 size_cmds;
    save_data savefile;
    s32 totaltime_ms;
    s32 filesize;
    enum GAMEMODE mode;
    u32 slotnum;
    u32 numplayers;
    u32 scenario;
    u32 mpstage_sel;
    u32 gamelength;
    u32 mp_weapon_set;
    u32 mp_char[4];
    u32 mp_handi[4];
    u32 mp_contstyle[4];
    u32 aim_option;
    u32 mp_flags[4];

} ramromfilestructure;
void test_if_recording_demos_this_stage_load(enum LEVELID arg0, enum DIFFICULTY arg1);
void iterate_ramrom_entries_handle_camera_out(void);
void stop_demo_playback(void);
void clear_ramrom_block_buffer_heading_ptrs(void);
s32 get_is_ramrom_flag(void);
s32 get_recording_ramrom_flag(void);
u32 check_ramrom_flags(void);
#ifdef NATIVE_PORT
typedef struct pc_ramrom_trace_state {
    s32 active;
    s32 playback_pending;
    s32 demo_related;
    s32 recording;
    s32 stage;
    s32 difficulty;
    s32 size_cmds;
    s32 total_time;
    s32 file_size;
    u32 base_offset;
    u32 cursor_offset;
    u32 input_stream_offset;
    s32 current_block_count;
    s32 current_block_speedframes;
    s32 current_block_randseed;
    s32 current_block_check;
    u64 random_call_count;
    u64 random_call_base;
    u64 random_calls_since_restore;
    u64 restored_random_seed;
    s32 random_seed_low;
    const char *symbol;
    const char *last_stop_reason;
    s32 last_stop_event_count;
    u32 last_stop_input_stream_offset;
    u64 last_stop_random_call_count;
    u64 last_stop_random_calls_since_restore;
    u64 last_stop_random_seed;
    s32 last_abort_buttons;
    s32 last_randseed_expected;
    s32 last_randseed_actual;
    s32 last_checksum_expected;
    s32 last_checksum_actual;
} pc_ramrom_trace_state;

s32 pcRamromReplayNameIsValid(const char *name);
s32 pcRamromStartReplayByName(const char *name);
void pcRamromGetTraceState(pc_ramrom_trace_state *out);
void pcRamromTraceLoopPhase(const char *phase);
void pcRamromTraceLoopPhaseWithDetails(const char *phase, s32 detail0, s32 detail1, s32 detail2);
void pcRamromObserveUpcomingBlockSeedWindow(const char *source);
s32 pcRamromShouldDeferFirstBlockRngConsumers(void);
s32 pcRamromShouldDeferUpcomingBlockBoundaryActionTick(void);
#endif

#endif
