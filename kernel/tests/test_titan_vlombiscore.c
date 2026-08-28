#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "titan_vlombiscore.h"
#include "titan_diagnostics.h"

static uint32_t g_logs;
void TitanDiagnostics_Event(TitanDiagLevel level, TitanDiagSubsystem subsystem, uint16_t event, int32_t result, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    (void)level;(void)event;(void)result;(void)a;(void)b;(void)c;(void)d;
    if (subsystem == TITAN_DIAG_VLOMBIS) g_logs++;
}

int main(void)
{
    TitanVlombisTelemetry t;
    TitanVlombisCore_Initialize();
    TitanVlombisCore_GetTelemetry(&t);
    assert(t.capabilities_active == 0u && t.measured_events == 0u);
    TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_EXCLUSIVE_HANDOFF);
    TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_BACKGROUND_BUDGETING | TITAN_VLOMBIS_CAP_IO_COORDINATION, TITAN_VLOMBIS_EVENT_BACKGROUND_PREEMPTION);
    TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING, TITAN_VLOMBIS_EVENT_FOREGROUND_JOB);
    TitanVlombisCore_GetTelemetry(&t);
    assert(t.measured_events == 3u);
    assert(t.exclusive_handoffs == 1u);
    assert(t.background_preemptions == 1u);
    assert(t.foreground_jobs == 1u);
    assert((t.capabilities_active & TITAN_VLOMBIS_CAP_IO_COORDINATION) != 0u);
    assert((t.capabilities_active & TITAN_VLOMBIS_CAP_BACKGROUND_BUDGETING) != 0u);
    assert(g_logs == 3u); /* first sample of each event class */
    TitanVlombisCore_Record(0u, TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION);
    TitanVlombisCore_Record(TITAN_VLOMBIS_CAP_STORAGE_BUDGETING, (TitanVlombisEvent)99);
    TitanVlombisCore_GetTelemetry(&t);
    assert(t.measured_events == 3u);

    TitanVlombisCore_RecordWork(
        TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO | TITAN_VLOMBIS_CAP_STORAGE_QOS,
        TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM, 4096u, 0u);
    TitanVlombisCore_RecordLibraryMetadataReuse(2u, 4096u);
    TitanVlombisCore_RecordWork(
        TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE | TITAN_VLOMBIS_CAP_PIKAVR,
        TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT, 7u, 0u);
    TitanVlombisCore_RecordFrameBudget(12000u, 16667u);
    assert(TitanVlombisCore_GetRecommendedIoQuantum(4096u, 1024u, 4096u) == 4096u);
    TitanVlombisCore_RecordFrameBudget(30000u, 16667u);
    assert(TitanVlombisCore_GetRecommendedIoQuantum(4096u, 1024u, 4096u) == 1024u);
    assert(TitanVlombisCore_GetBackgroundCadence(20u, 40u) == 40u);
    TitanVlombisCore_GetTelemetry(&t);
    assert(t.async_package_quanta == 1u && t.async_package_bytes == 4096u);
    assert(t.library_metadata_reuses == 2u && t.bytes_avoided == 4096u);
    assert(t.render_state_cache_hits == 7u);
    assert(t.frame_budget_hits >= 1u && t.frame_budget_misses >= 1u);
    assert(TitanVlombisCore_GetActiveCapabilityCount() >= 6u);
    puts("titan-vlombiscore: OK measured domains, adaptive QoS, savings and sampled logging");
    return 0;
}
