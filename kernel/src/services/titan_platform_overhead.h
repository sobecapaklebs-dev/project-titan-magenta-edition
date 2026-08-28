#ifndef PROJECT_TITAN_PLATFORM_OVERHEAD_H
#define PROJECT_TITAN_PLATFORM_OVERHEAD_H
#include <stdint.h>
typedef enum TitanOverheadDomain {
 TITAN_OVERHEAD_RUNTIME=0, TITAN_OVERHEAD_TGR, TITAN_OVERHEAD_PIKAVR,
 TITAN_OVERHEAD_VLOMBIS, TITAN_OVERHEAD_VFS, TITAN_OVERHEAD_NETWORK,
 TITAN_OVERHEAD_SESSION, TITAN_OVERHEAD_IGR, TITAN_OVERHEAD_OVERLAY,
 TITAN_OVERHEAD_INPUT, TITAN_OVERHEAD_SAS, TITAN_OVERHEAD_APP_RUNTIME,
 TITAN_OVERHEAD_TELEMETRY, TITAN_OVERHEAD_CLASSIFICATION, TITAN_OVERHEAD_THEME,
 TITAN_OVERHEAD_STORAGE, TITAN_OVERHEAD_AUDIO, TITAN_OVERHEAD_DOMAIN_COUNT
} TitanOverheadDomain;
typedef struct TitanOverheadMetric {
 uint64_t total_us, active_us, idle_us;
 uint32_t calls, active_calls, idle_calls, max_us;
} TitanOverheadMetric;
typedef struct TitanOverheadSnapshot {
 uint32_t enabled;
 uint32_t clock_reads;
 TitanOverheadMetric domains[TITAN_OVERHEAD_DOMAIN_COUNT];
} TitanOverheadSnapshot;
void TitanOverhead_Initialize(void);
void TitanOverhead_SetEnabled(int enabled);
int TitanOverhead_IsEnabled(void);
uint64_t TitanOverhead_Begin(TitanOverheadDomain domain, int active);
void TitanOverhead_End(TitanOverheadDomain domain, uint64_t token, int active);
void TitanOverhead_GetSnapshot(TitanOverheadSnapshot *snapshot);
void TitanOverhead_ResetMetrics(void);
#endif
