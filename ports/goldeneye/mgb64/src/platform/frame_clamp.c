/*
 * frame_clamp.c — speedgraphframes spike clamp (mirrors lvl.c g_ClockTimer cap).
 *
 * Pure, ROM-free. Factored out of unk_0C0A70.c so the runtime path and a
 * ROM-free unit test share one implementation (FID-0017 / M2.4). Behavior is
 * byte-identical to the original inline NATIVE_PORT clamp.
 */
#include "frame_clamp.h"

int clampSpeedgraphFrames(int deltaFrames, int is_ramrom, int is_first_tick) {
    if (is_ramrom) {
        /* preserve raw timing for RAMROM playback fidelity */
        return deltaFrames;
    }
    if (is_first_tick && deltaFrames > 1) {
        return 1;
    }
    if (deltaFrames > FRAME_SPIKE_CAP) {
        return FRAME_SPIKE_CAP;
    }
    return deltaFrames;
}
