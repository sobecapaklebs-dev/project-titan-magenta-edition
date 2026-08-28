#include "titan_vlombiscore.h"
#include "titan_diagnostics.h"

#include <limits.h>
#include <string.h>

typedef struct TitanVlombisState
{
    TitanVlombisTelemetry telemetry;
} TitanVlombisState;

static TitanVlombisState g_vlombis;

static uint32_t TitanVlombisCore_SaturatingAdd(uint32_t value, uint32_t amount)
{
    if (UINT_MAX - value < amount)
        return UINT_MAX;
    return value + amount;
}

static uint32_t TitanVlombisCore_SaturatingIncrement(uint32_t value)
{
    return TitanVlombisCore_SaturatingAdd(value, 1u);
}

static int TitanVlombisCore_IsLogSample(uint32_t count)
{
    /* First event and powers of two preserve useful growth checkpoints without
     * turning arbitration/frame telemetry into per-frame log traffic. */
    return count == 1u || (count != 0u && (count & (count - 1u)) == 0u);
}

static uint32_t TitanVlombisCore_EventCount(TitanVlombisEvent event)
{
    switch (event)
    {
        case TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF:
            return g_vlombis.telemetry.exclusive_handoffs;
        case TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION:
            return g_vlombis.telemetry.background_preemptions;
        case TITAN_VLOMBIS_EVENT_FOREGROUND_JOB:
            return g_vlombis.telemetry.foreground_jobs;
        case TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION:
            return g_vlombis.telemetry.storage_arbitrations;
        case TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT:
            return g_vlombis.telemetry.render_state_cache_hits;
        case TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM:
            return g_vlombis.telemetry.async_package_quanta;
        case TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT:
            return g_vlombis.telemetry.library_cache_hits;
        case TITAN_VLOMBIS_EVENT_LIBRARY_SCAN_STEP:
            return g_vlombis.telemetry.library_scan_steps;
        case TITAN_VLOMBIS_EVENT_AUDIO_BUFFER_REFILL:
            return g_vlombis.telemetry.audio_buffer_refills;
        case TITAN_VLOMBIS_EVENT_NETWORK_BACKGROUND_TICK:
            return g_vlombis.telemetry.network_background_ticks;
        case TITAN_VLOMBIS_EVENT_FRAME_BUDGET_HIT:
            return g_vlombis.telemetry.frame_budget_hits;
        case TITAN_VLOMBIS_EVENT_FRAME_BUDGET_MISS:
            return g_vlombis.telemetry.frame_budget_misses;
        case TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION:
            return g_vlombis.telemetry.pikavr_optimizations;
        default:
            return 0u;
    }
}

void TitanVlombisCore_Initialize(void)
{
    memset(&g_vlombis, 0, sizeof(g_vlombis));
    g_vlombis.telemetry.frame_budget_us = 16667u;
}

void TitanVlombisCore_RecordWork(
    uint32_t capability_mask,
    TitanVlombisEvent event,
    uint32_t work_units,
    uint32_t bytes_avoided
)
{
    uint32_t count;
    uint32_t previous_count;

    if (capability_mask == 0u ||
        (capability_mask & ~TITAN_VLOMBIS_CAP_ALL) != 0u ||
        event < TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF ||
        event > TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION)
        return;

    if (work_units == 0u)
        work_units = 1u;

    previous_count = TitanVlombisCore_EventCount(event);
    g_vlombis.telemetry.capabilities_active |= capability_mask;
    g_vlombis.telemetry.measured_events = TitanVlombisCore_SaturatingIncrement(
        g_vlombis.telemetry.measured_events
    );
    g_vlombis.telemetry.bytes_avoided = TitanVlombisCore_SaturatingAdd(
        g_vlombis.telemetry.bytes_avoided, bytes_avoided
    );

    switch (event)
    {
        case TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF:
            g_vlombis.telemetry.exclusive_handoffs = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.exclusive_handoffs, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION:
            g_vlombis.telemetry.background_preemptions = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.background_preemptions, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_FOREGROUND_JOB:
            g_vlombis.telemetry.foreground_jobs = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.foreground_jobs, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION:
            g_vlombis.telemetry.storage_arbitrations = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.storage_arbitrations, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT:
            g_vlombis.telemetry.render_state_cache_hits = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.render_state_cache_hits, work_units);
            g_vlombis.telemetry.work_units_avoided = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.work_units_avoided, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM:
            g_vlombis.telemetry.async_package_quanta = TitanVlombisCore_SaturatingIncrement(
                g_vlombis.telemetry.async_package_quanta);
            g_vlombis.telemetry.async_package_bytes = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.async_package_bytes, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT:
            g_vlombis.telemetry.library_cache_hits = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.library_cache_hits, work_units);
            g_vlombis.telemetry.work_units_avoided = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.work_units_avoided, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_LIBRARY_SCAN_STEP:
            g_vlombis.telemetry.library_scan_steps = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.library_scan_steps, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_AUDIO_BUFFER_REFILL:
            g_vlombis.telemetry.audio_buffer_refills = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.audio_buffer_refills, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_NETWORK_BACKGROUND_TICK:
            g_vlombis.telemetry.network_background_ticks = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.network_background_ticks, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_FRAME_BUDGET_HIT:
            g_vlombis.telemetry.frame_budget_hits = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.frame_budget_hits, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_FRAME_BUDGET_MISS:
            g_vlombis.telemetry.frame_budget_misses = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.frame_budget_misses, work_units);
            break;
        case TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION:
            g_vlombis.telemetry.pikavr_optimizations = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.pikavr_optimizations, work_units);
            g_vlombis.telemetry.work_units_avoided = TitanVlombisCore_SaturatingAdd(
                g_vlombis.telemetry.work_units_avoided, work_units);
            break;
        default:
            return;
    }

    count = TitanVlombisCore_EventCount(event);
    if (previous_count == 0u || TitanVlombisCore_IsLogSample(count))
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,
            TITAN_DIAG_VLOMBIS,
            0x700u + (uint16_t)event,
            0,
            capability_mask,
            count,
            g_vlombis.telemetry.measured_events,
            bytes_avoided
        );
}

void TitanVlombisCore_Record(uint32_t capability_mask, TitanVlombisEvent event)
{
    TitanVlombisCore_RecordWork(capability_mask, event, 1u, 0u);
}

void TitanVlombisCore_RecordLibraryMetadataReuse(uint32_t count, uint32_t bytes_avoided)
{
    if (count == 0u)
        return;
    g_vlombis.telemetry.library_metadata_reuses = TitanVlombisCore_SaturatingAdd(
        g_vlombis.telemetry.library_metadata_reuses, count
    );
    TitanVlombisCore_RecordWork(
        TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN |
            TITAN_VLOMBIS_CAP_STORAGE_QOS |
            TITAN_VLOMBIS_CAP_IO_COORDINATION,
        TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT,
        count,
        bytes_avoided
    );
}

void TitanVlombisCore_RecordFrameBudget(uint32_t frame_us, uint32_t budget_us)
{
    TitanVlombisEvent event;
    uint32_t jitter_slack_us;

    if (budget_us == 0u)
        budget_us = 16667u;
    /* A vsync-paced PS2 frame routinely measures a few dozen microseconds
     * above the nominal 16.667 ms period. Treating 16.699 ms as overload kept
     * VlombisCore permanently at pressure=1 and halved background I/O for the
     * entire session. Keep a bounded 0.5 ms scheduling/timer tolerance. */
    jitter_slack_us = budget_us / 32u;
    if (jitter_slack_us < 500u)
        jitter_slack_us = 500u;
    g_vlombis.telemetry.last_frame_us = frame_us;
    g_vlombis.telemetry.frame_budget_us = budget_us;

    if (frame_us > budget_us + budget_us / 2u)
        g_vlombis.telemetry.pressure_level = 2u;
    else if (frame_us > budget_us + jitter_slack_us)
        g_vlombis.telemetry.pressure_level = 1u;
    else
        g_vlombis.telemetry.pressure_level = 0u;

    event = frame_us <= budget_us + jitter_slack_us
        ? TITAN_VLOMBIS_EVENT_FRAME_BUDGET_HIT
        : TITAN_VLOMBIS_EVENT_FRAME_BUDGET_MISS;
    TitanVlombisCore_RecordWork(
        TITAN_VLOMBIS_CAP_FRAME_BUDGETING |
            TITAN_VLOMBIS_CAP_BACKGROUND_QOS,
        event,
        1u,
        0u
    );
}

uint32_t TitanVlombisCore_GetRecommendedIoQuantum(
    uint32_t normal_bytes,
    uint32_t minimum_bytes,
    uint32_t maximum_bytes
)
{
    uint32_t value;

    if (minimum_bytes == 0u)
        minimum_bytes = 1u;
    if (maximum_bytes < minimum_bytes)
        maximum_bytes = minimum_bytes;
    if (normal_bytes < minimum_bytes)
        normal_bytes = minimum_bytes;
    if (normal_bytes > maximum_bytes)
        normal_bytes = maximum_bytes;

    value = normal_bytes;
    if (g_vlombis.telemetry.pressure_level >= 2u)
        value = minimum_bytes;
    else if (g_vlombis.telemetry.pressure_level == 1u)
    {
        value = normal_bytes / 2u;
        if (value < minimum_bytes)
            value = minimum_bytes;
    }
    else if (g_vlombis.telemetry.last_frame_us != 0u &&
             g_vlombis.telemetry.last_frame_us <
                 (g_vlombis.telemetry.frame_budget_us * 3u) / 4u)
    {
        value = normal_bytes <= maximum_bytes / 2u
            ? normal_bytes * 2u : maximum_bytes;
        if (value > maximum_bytes)
            value = maximum_bytes;
    }
    return value;
}

uint32_t TitanVlombisCore_GetBackgroundCadence(
    uint32_t normal_frames,
    uint32_t pressured_frames
)
{
    if (normal_frames == 0u)
        normal_frames = 1u;
    if (pressured_frames < normal_frames)
        pressured_frames = normal_frames;
    return g_vlombis.telemetry.pressure_level != 0u
        ? pressured_frames : normal_frames;
}

uint32_t TitanVlombisCore_GetActiveCapabilityCount(void)
{
    uint32_t mask = g_vlombis.telemetry.capabilities_active & TITAN_VLOMBIS_CAP_ALL;
    uint32_t count = 0u;
    while (mask != 0u)
    {
        count += mask & 1u;
        mask >>= 1u;
    }
    return count;
}

void TitanVlombisCore_GetTelemetry(TitanVlombisTelemetry *telemetry)
{
    if (telemetry != 0)
        *telemetry = g_vlombis.telemetry;
}
