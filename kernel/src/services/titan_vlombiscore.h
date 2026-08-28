#ifndef PROJECT_TITAN_VLOMBISCORE_H
#define PROJECT_TITAN_VLOMBISCORE_H

#include <stdint.h>

/* BF28 pre-freeze capability taxonomy. Bits are append-only. A bit describes
 * measured participation, never mere availability. HWFix2 materializes a
 * producer for every canonical domain while preserving API v1 spellings. */
#define TITAN_VLOMBISCORE_API_VERSION 1u

#define TITAN_VLOMBIS_CAP_STORAGE_QOS              (1u << 0u)
#define TITAN_VLOMBIS_CAP_BACKGROUND_QOS           (1u << 1u)
#define TITAN_VLOMBIS_CAP_IO_COORDINATION          (1u << 2u)
#define TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE       (1u << 3u)
#define TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO         (1u << 4u)
#define TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN (1u << 5u)
#define TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS         (1u << 6u)
#define TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS   (1u << 7u)
#define TITAN_VLOMBIS_CAP_FRAME_BUDGETING          (1u << 8u)
#define TITAN_VLOMBIS_CAP_PIKAVR                   (1u << 9u)
#define TITAN_VLOMBIS_CAP_ALL                      ((1u << 10u) - 1u)

/* Source-compatible legacy spellings. Do not reuse their bit positions. */
#define TITAN_VLOMBIS_CAP_STORAGE_BUDGETING    TITAN_VLOMBIS_CAP_STORAGE_QOS
#define TITAN_VLOMBIS_CAP_BACKGROUND_BUDGETING TITAN_VLOMBIS_CAP_BACKGROUND_QOS
#define TITAN_VLOMBIS_CAP_RENDER_COORDINATION  TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE

typedef enum TitanVlombisEvent
{
    TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF = 1,
    TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION,
    TITAN_VLOMBIS_EVENT_FOREGROUND_JOB,
    TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION,
    TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT,
    TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM,
    TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT,
    TITAN_VLOMBIS_EVENT_LIBRARY_SCAN_STEP,
    TITAN_VLOMBIS_EVENT_AUDIO_BUFFER_REFILL,
    TITAN_VLOMBIS_EVENT_NETWORK_BACKGROUND_TICK,
    TITAN_VLOMBIS_EVENT_FRAME_BUDGET_HIT,
    TITAN_VLOMBIS_EVENT_FRAME_BUDGET_MISS,
    TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION
} TitanVlombisEvent;

typedef struct TitanVlombisTelemetry
{
    /* Original API-v1 prefix. Keep field order stable. */
    uint32_t capabilities_active;
    uint32_t measured_events;
    uint32_t exclusive_handoffs;
    uint32_t background_preemptions;
    uint32_t foreground_jobs;
    uint32_t storage_arbitrations;

    /* Append-only measured work telemetry. */
    uint32_t render_state_cache_hits;
    uint32_t async_package_quanta;
    uint32_t async_package_bytes;
    uint32_t library_cache_hits;
    uint32_t library_scan_steps;
    uint32_t library_metadata_reuses;
    uint32_t audio_buffer_refills;
    uint32_t network_background_ticks;
    uint32_t frame_budget_hits;
    uint32_t frame_budget_misses;
    uint32_t pikavr_optimizations;
    uint32_t work_units_avoided;
    uint32_t bytes_avoided;
    uint32_t last_frame_us;
    uint32_t frame_budget_us;
    uint32_t pressure_level;
} TitanVlombisTelemetry;

void TitanVlombisCore_Initialize(void);
void TitanVlombisCore_Record(uint32_t capability_mask, TitanVlombisEvent event);
void TitanVlombisCore_RecordWork(
    uint32_t capability_mask,
    TitanVlombisEvent event,
    uint32_t work_units,
    uint32_t bytes_avoided
);
void TitanVlombisCore_RecordLibraryMetadataReuse(uint32_t count, uint32_t bytes_avoided);
void TitanVlombisCore_RecordFrameBudget(uint32_t frame_us, uint32_t budget_us);
uint32_t TitanVlombisCore_GetRecommendedIoQuantum(
    uint32_t normal_bytes,
    uint32_t minimum_bytes,
    uint32_t maximum_bytes
);
uint32_t TitanVlombisCore_GetBackgroundCadence(
    uint32_t normal_frames,
    uint32_t pressured_frames
);
uint32_t TitanVlombisCore_GetActiveCapabilityCount(void);
void TitanVlombisCore_GetTelemetry(TitanVlombisTelemetry *telemetry);

#endif
