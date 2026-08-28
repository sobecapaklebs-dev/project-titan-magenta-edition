#include "titan_target_memory.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    TitanTargetMemoryTelemetry t;
    TitanTargetMemory_Initialize();
    TitanTargetMemory_SetHostEeSample(1024u, 4096u);
    TitanTargetMemory_SetHostIopSample(2048u, 6144u, 5u);
    TitanTargetMemory_Capture();
    TitanTargetMemory_SetHostEeSample(1536u, 3584u);
    TitanTargetMemory_SetHostIopSample(2304u, 5888u, 6u);
    TitanTargetMemory_RecordEeAllocationFailure();
    TitanTargetMemory_Capture();
    TitanTargetMemory_GetTelemetry(&t);
    assert(t.ee_valid == 1u);
    assert(t.ee_used_estimate == 1536u);
    assert(t.ee_free_estimate == 3584u);
    assert(t.ee_peak_used == 1536u);
    assert(t.ee_min_free == 3584u);
    assert(t.ee_allocation_failures == 1u);
    assert(t.iop_ram_valid == 1u);
    assert(t.iop_used == 2304u && t.iop_free == 5888u);
    assert(t.iop_min_free == 5888u);
    assert(t.iop_modules_known_resident == 6u);
    puts("titan-target-memory-watermarks: PASS");
    return 0;
}
