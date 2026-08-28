#include <assert.h>
#include "../src/services/titan_platform_overhead.h"
int main(void){TitanOverheadSnapshot s;uint64_t t;TitanOverhead_Initialize();
t=TitanOverhead_Begin(TITAN_OVERHEAD_NETWORK,0);assert(t==0);TitanOverhead_GetSnapshot(&s);assert(s.clock_reads==0&&s.domains[TITAN_OVERHEAD_NETWORK].calls==0);
TitanOverhead_SetEnabled(1);t=TitanOverhead_Begin(TITAN_OVERHEAD_NETWORK,0);TitanOverhead_End(TITAN_OVERHEAD_NETWORK,t,0);t=TitanOverhead_Begin(TITAN_OVERHEAD_STORAGE,1);TitanOverhead_End(TITAN_OVERHEAD_STORAGE,t,1);TitanOverhead_GetSnapshot(&s);assert(s.clock_reads==4);assert(s.domains[TITAN_OVERHEAD_NETWORK].idle_calls==1);assert(s.domains[TITAN_OVERHEAD_STORAGE].active_calls==1);TitanOverhead_ResetMetrics();TitanOverhead_GetSnapshot(&s);assert(s.enabled==1&&s.clock_reads==0);return 0;}
