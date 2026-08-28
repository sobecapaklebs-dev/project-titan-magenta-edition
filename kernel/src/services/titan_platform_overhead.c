#include "titan_platform_overhead.h"
#include "titan_time.h"
#include <limits.h>
#include <string.h>
typedef struct TitanOverheadState { int enabled; uint32_t clock_reads; TitanOverheadMetric metrics[TITAN_OVERHEAD_DOMAIN_COUNT]; } TitanOverheadState;
static TitanOverheadState g_overhead;
static uint32_t SatInc(uint32_t v){return v==UINT_MAX?UINT_MAX:v+1u;}
void TitanOverhead_Initialize(void){memset(&g_overhead,0,sizeof(g_overhead));}
void TitanOverhead_SetEnabled(int enabled){g_overhead.enabled=enabled!=0;}
int TitanOverhead_IsEnabled(void){return g_overhead.enabled;}
uint64_t TitanOverhead_Begin(TitanOverheadDomain domain,int active){(void)active;if(!g_overhead.enabled || domain<0 || domain>=TITAN_OVERHEAD_DOMAIN_COUNT)return 0u;g_overhead.clock_reads=SatInc(g_overhead.clock_reads);return TitanTime_NowMicroseconds();}
void TitanOverhead_End(TitanOverheadDomain domain,uint64_t token,int active){uint64_t now,delta;TitanOverheadMetric*m;if(token==0u||!g_overhead.enabled||domain<0||domain>=TITAN_OVERHEAD_DOMAIN_COUNT)return;g_overhead.clock_reads=SatInc(g_overhead.clock_reads);now=TitanTime_NowMicroseconds();delta=now>token?now-token:0u;m=&g_overhead.metrics[domain];m->calls=SatInc(m->calls);m->total_us+=delta;if(active){m->active_calls=SatInc(m->active_calls);m->active_us+=delta;}else{m->idle_calls=SatInc(m->idle_calls);m->idle_us+=delta;}if(delta>m->max_us)m->max_us=delta>UINT_MAX?UINT_MAX:(uint32_t)delta;}
void TitanOverhead_GetSnapshot(TitanOverheadSnapshot*s){if(!s)return;memset(s,0,sizeof(*s));s->enabled=g_overhead.enabled?1u:0u;s->clock_reads=g_overhead.clock_reads;memcpy(s->domains,g_overhead.metrics,sizeof(g_overhead.metrics));}
void TitanOverhead_ResetMetrics(void){int e=g_overhead.enabled;memset(&g_overhead,0,sizeof(g_overhead));g_overhead.enabled=e;}
