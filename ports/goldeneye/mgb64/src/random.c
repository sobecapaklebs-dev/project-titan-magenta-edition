#if defined(NATIVE_PORT) && defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <ultra64.h>
#ifdef NATIVE_PORT
/* dladdr() is POSIX-only; the random-trace symbolizer degrades gracefully
 * without it on Windows. */
#if !defined(_WIN32) && !defined(PROJECT_TITAN_TGR)
#define PORT_HAVE_DLADDR 1
#include <dlfcn.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/ramromreplay.h"
#endif

/**
 * EU .data, offset from start of data_seg : 0x36a0
*/

/**
 * @file random.c
 * This file contains code to get a random value and set the next seed.
 * The methods here are the same as in tlb_random and chrObjRandom (but different globals).
 */

u64 g_randomSeed = 0xAB8D9F7781280783;

#ifdef NATIVE_PORT
static u64 s_pcRandomGetNextCallCount = 0;
static s32 s_pcRandomTraceRamromActive = 0;
static s32 s_pcRandomTraceEnabled = -1;
static s32 s_pcRandomTraceForceEnabled = -1;
static s32 s_pcRandomTraceRemaining = 0;
static u64 s_pcRandomTraceAfterCall = 0;

typedef struct PcScriptedRandomCallSeedEvent {
    u64 call;
    u64 seed;
    s32 applied;
} PcScriptedRandomCallSeedEvent;

static PcScriptedRandomCallSeedEvent s_pcRandomCallSeedEvents[64];
static s32 s_pcRandomCallSeedEventCount = 0;
static s32 s_pcRandomCallSeedEventsLoaded = 0;

u64 pcRandomGetNextCallCount(void)
{
    return s_pcRandomGetNextCallCount;
}

void pcRandomTraceSetRamromActive(s32 active)
{
    s_pcRandomTraceRamromActive = active ? 1 : 0;
}

static void pcRandomAddCallSeedEvent(u64 call, u64 seed)
{
    PcScriptedRandomCallSeedEvent *event;

    if (call == 0 ||
        s_pcRandomCallSeedEventCount >=
            (s32)(sizeof(s_pcRandomCallSeedEvents) / sizeof(s_pcRandomCallSeedEvents[0]))) {
        return;
    }

    event = &s_pcRandomCallSeedEvents[s_pcRandomCallSeedEventCount++];
    event->call = call;
    event->seed = seed;
    event->applied = 0;
}

static s32 pcRandomParseCallSeedSpec(const char *spec)
{
    char *end = NULL;
    const char *seed_spec;
    u64 call;
    u64 seed;

    if (spec == NULL || *spec == '\0') {
        return 0;
    }

    call = strtoull(spec, &end, 0);
    if (end == spec || *end != ':') {
        return 0;
    }

    seed_spec = end + 1;
    seed = strtoull(seed_spec, &end, 0);
    if (end == seed_spec || *end != '\0') {
        return 0;
    }

    pcRandomAddCallSeedEvent(call, seed);
    return 1;
}

static void pcRandomLoadCallSeedEvents(void)
{
    const char *script;
    const char *call_env;
    const char *seed_env;

    if (s_pcRandomCallSeedEventsLoaded) {
        return;
    }
    s_pcRandomCallSeedEventsLoaded = 1;

    script = getenv("GE007_AUTO_RNG_CALL_SEED_SCRIPT");
    if (script != NULL) {
        while (*script != '\0' &&
               s_pcRandomCallSeedEventCount <
                   (s32)(sizeof(s_pcRandomCallSeedEvents) / sizeof(s_pcRandomCallSeedEvents[0]))) {
            char item[128];
            size_t index = 0;
            size_t count;

            while (*script == ' ' || *script == '\t' || *script == ',' || *script == ';') {
                script++;
            }
            if (*script == '\0') {
                break;
            }
            while (script[index] != '\0' &&
                   script[index] != ' ' &&
                   script[index] != '\t' &&
                   script[index] != ',' &&
                   script[index] != ';') {
                if (index + 1 < sizeof(item)) {
                    item[index] = script[index];
                }
                index++;
            }
            count = index < sizeof(item) ? index : sizeof(item) - 1;
            item[count] = '\0';
            if (!pcRandomParseCallSeedSpec(item)) {
                fprintf(stderr, "[AUTO_RNG_CALL_SEED] ignored malformed spec: %s\n", item);
            }
            script += index;
        }
    }

    call_env = getenv("GE007_AUTO_RNG_CALL_SEED_CALL");
    seed_env = getenv("GE007_AUTO_RNG_CALL_SEED");
    if (s_pcRandomCallSeedEventCount == 0 &&
        call_env != NULL && *call_env != '\0' &&
        seed_env != NULL && *seed_env != '\0') {
        char *call_end = NULL;
        char *seed_end = NULL;
        u64 call = strtoull(call_env, &call_end, 0);
        u64 seed = strtoull(seed_env, &seed_end, 0);

        if (call_end != call_env && *call_end == '\0' &&
            seed_end != seed_env && *seed_end == '\0') {
            pcRandomAddCallSeedEvent(call, seed);
        }
    }
}

static void pcRandomMaybeApplyCallSeedEvent(u64 call)
{
    pcRandomLoadCallSeedEvents();

    for (s32 i = 0; i < s_pcRandomCallSeedEventCount; i++) {
        PcScriptedRandomCallSeedEvent *event = &s_pcRandomCallSeedEvents[i];

        if (event->applied || event->call != call) {
            continue;
        }

        g_randomSeed = event->seed;
        event->applied = 1;
        fprintf(stderr,
                "[AUTO_RNG_CALL_SEED] call=%llu seed=0x%016llX\n",
                (unsigned long long)call,
                (unsigned long long)g_randomSeed);
    }
}

static s32 pcRandomTraceEnabled(void)
{
    const char *budget;
    const char *after_call;

    if (s_pcRandomTraceEnabled < 0) {
        s_pcRandomTraceEnabled = getenv("GE007_TRACE_RNG_CALLS") != NULL ? 1 : 0;
        s_pcRandomTraceRemaining = 256;
        budget = getenv("GE007_TRACE_RNG_CALL_BUDGET");
        if (budget != NULL && *budget != '\0') {
            s_pcRandomTraceRemaining = atoi(budget);
        }
        after_call = getenv("GE007_TRACE_RNG_CALLS_AFTER");
        if (after_call != NULL && *after_call != '\0') {
            s_pcRandomTraceAfterCall = strtoull(after_call, NULL, 0);
        }
    }

    return s_pcRandomTraceEnabled && s_pcRandomTraceRemaining != 0;
}

static s32 pcRandomTraceForceEnabled(void)
{
    if (s_pcRandomTraceForceEnabled < 0) {
        s_pcRandomTraceForceEnabled =
            getenv("GE007_TRACE_RNG_CALLS_FORCE") != NULL ? 1 : 0;
    }

    return s_pcRandomTraceForceEnabled;
}

static void pcRandomTraceDescribeAddress(void *address, uintptr_t *offset, const char **symbol)
{
    *offset = 0;
    *symbol = "";

#ifdef PORT_HAVE_DLADDR
    Dl_info info;

    if (address != NULL && dladdr(address, &info) != 0) {
        *offset = (uintptr_t)address - (uintptr_t)info.dli_fbase;
        if (info.dli_sname != NULL) {
            *symbol = info.dli_sname;
        }
    }
#else
    (void)address;
#endif
}

static void pcRandomTraceCall(u64 call_count,
                              u64 before_seed,
                              u64 after_seed,
                              void *caller,
                              void *parent)
{
    uintptr_t caller_offset;
    uintptr_t parent_offset;
    const char *symbol;
    const char *parent_symbol;

    if ((!s_pcRandomTraceRamromActive && !pcRandomTraceForceEnabled()) ||
        !pcRandomTraceEnabled() ||
        call_count < s_pcRandomTraceAfterCall) {
        return;
    }

    pcRandomTraceDescribeAddress(caller, &caller_offset, &symbol);
    pcRandomTraceDescribeAddress(parent, &parent_offset, &parent_symbol);

    fprintf(stderr,
            "[RNG] call=%llu before=0x%016llx after=0x%016llx low=%u "
            "caller_offset=0x%llx symbol=%s parent_offset=0x%llx parent_symbol=%s\n",
            (unsigned long long)call_count,
            (unsigned long long)before_seed,
            (unsigned long long)after_seed,
            (unsigned int)(after_seed & 0xffU),
            (unsigned long long)caller_offset,
            symbol,
            (unsigned long long)parent_offset,
            parent_symbol);

    if (s_pcRandomTraceRemaining > 0) {
        s_pcRandomTraceRemaining--;
    }
}
#endif

#ifdef NONMATCHING
// https://decomp.me/scratch/4prWp
// -mips3 -O2
/**
 * Iterates the current random seed and returns a 32 bit value.
 * Same assembly instructions as tlbRandomGetNext and chrObjRandomGetNext, but different globals.
 */ 
u32 randomGetNext(void) {
    u64 t;
#ifdef NATIVE_PORT
    u64 next_call_count = s_pcRandomGetNextCallCount + 1;
    pcRandomMaybeApplyCallSeedEvent(next_call_count);
    u64 before_seed = g_randomSeed;
#ifdef __EMSCRIPTEN__
    /* Asyncify + __builtin_return_address(N) needs -sUSE_OFFSET_CONVERTER, which
     * aborts every frame here (randomGetNext runs constantly). The caller/parent
     * pointers feed only the PC-side random *trace* diagnostic — sim-irrelevant.
     * Null them out on wasm; the seed math above is untouched. */
    void *caller = NULL;
    void *parent = NULL;
#else
    void *caller = __builtin_return_address(0);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wframe-address"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"
#endif
    void *parent = __builtin_return_address(1);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif
#endif

    t = (g_randomSeed << 63) >> 31;
    t |= (g_randomSeed << 31) >> 32;
    t ^= (g_randomSeed << 44) >> 32;
    g_randomSeed = t ^ ((t >> 20) & 0xFFF);
#ifdef NATIVE_PORT
    s_pcRandomGetNextCallCount++;
    pcRandomTraceCall(s_pcRandomGetNextCallCount, before_seed, g_randomSeed, caller, parent);
    pcRamromObserveUpcomingBlockSeedWindow("random_get_next");
#endif
    return (u32)g_randomSeed;
}
#else
GLOBAL_ASM(
.text
glabel randomGetNext
/* 00B050 7000A450 3C048002 */  lui   $a0, %hi(g_randomSeed)
/* 00B054 7000A454 DC844460 */  ld    $a0, %lo(g_randomSeed)($a0)
/* 00B058 7000A458 3C018002 */  lui   $at, %hi(g_randomSeed)
/* 00B05C 7000A45C 000437FC */  dsll32 $a2, $a0, 0x1f
/* 00B060 7000A460 00042FF8 */  dsll  $a1, $a0, 0x1f
/* 00B064 7000A464 000637FA */  dsrl  $a2, $a2, 0x1f
/* 00B068 7000A468 0005283E */  dsrl32 $a1, $a1, 0
/* 00B06C 7000A46C 0004233C */  dsll32 $a0, $a0, 0xc
/* 00B070 7000A470 00C53025 */  or    $a2, $a2, $a1
/* 00B074 7000A474 0004203E */  dsrl32 $a0, $a0, 0
/* 00B078 7000A478 00C43026 */  xor   $a2, $a2, $a0
/* 00B07C 7000A47C 0006253A */  dsrl  $a0, $a2, 0x14
/* 00B080 7000A480 30840FFF */  andi  $a0, $a0, 0xfff
/* 00B084 7000A484 00862026 */  xor   $a0, $a0, $a2
/* 00B088 7000A488 0004103C */  dsll32 $v0, $a0, 0
/* 00B08C 7000A48C FC244460 */  sd    $a0, %lo(g_randomSeed)($at)
/* 00B090 7000A490 03E00008 */  jr    $ra
/* 00B094 7000A494 0002103F */   dsra32 $v0, $v0, 0
)
#endif



#ifdef NONMATCHING
/**
 * This sets the global random seed. This is called from boss mainloop by randomSetSeed(osGetCount()),
 * so the argument may just be 32bit.
 * 
 * Assembly assigns zero to $a0 at the end of the function, which seems odd.
 * 
 * Same assembly instructions as chrObjRandomSetSeed.
 */ 
void randomSetSeed(u64 param_1) {
    g_randomSeed = param_1 + 1;
}
#else
GLOBAL_ASM(
.text
glabel randomSetSeed
/* 00B098 7000A498 64840001 */  daddiu $a0, $a0, 1
/* 00B09C 7000A49C 3C018002 */  lui   $at, %hi(g_randomSeed)
/* 00B0A0 7000A4A0 FC244460 */  sd    $a0, %lo(g_randomSeed)($at)
/* 00B0A4 7000A4A4 03E00008 */  jr    $ra
/* 00B0A8 7000A4A8 24040000 */   li    $a0, 0
)
#endif



#ifdef NONMATCHING
/**
 * Iterates the parameter as if it were the random seed and returns the next 32 bit random value.
 * This uses the same logic as randomGetNext.
 */ 
u32 randomGetNextFrom(u64 *param_1) {
    u64 t;

    t = (*param_1 << 63) >> 31;
    t |= (*param_1 << 31) >> 32;
    t ^= (*param_1 << 44) >> 32;
    *param_1 = t ^ ((t >> 20) & 0xFFF);
    return (u32)*param_1;
}
#else
GLOBAL_ASM(
.text
glabel randomGetNextFrom
/* 00B0AC 7000A4AC DC870000 */  ld    $a3, ($a0)
/* 00B0B0 7000A4B0 000737FC */  dsll32 $a2, $a3, 0x1f
/* 00B0B4 7000A4B4 00072FF8 */  dsll  $a1, $a3, 0x1f
/* 00B0B8 7000A4B8 000637FA */  dsrl  $a2, $a2, 0x1f
/* 00B0BC 7000A4BC 0005283E */  dsrl32 $a1, $a1, 0
/* 00B0C0 7000A4C0 00073B3C */  dsll32 $a3, $a3, 0xc
/* 00B0C4 7000A4C4 00C53025 */  or    $a2, $a2, $a1
/* 00B0C8 7000A4C8 0007383E */  dsrl32 $a3, $a3, 0
/* 00B0CC 7000A4CC 00C73026 */  xor   $a2, $a2, $a3
/* 00B0D0 7000A4D0 00063D3A */  dsrl  $a3, $a2, 0x14
/* 00B0D4 7000A4D4 30E70FFF */  andi  $a3, $a3, 0xfff
/* 00B0D8 7000A4D8 00E63826 */  xor   $a3, $a3, $a2
/* 00B0DC 7000A4DC 0007103C */  dsll32 $v0, $a3, 0
/* 00B0E0 7000A4E0 FC870000 */  sd    $a3, ($a0)
/* 00B0E4 7000A4E4 03E00008 */  jr    $ra
/* 00B0E8 7000A4E8 0002103F */   dsra32 $v0, $v0, 0
)
#endif
