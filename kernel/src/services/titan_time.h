#ifndef PROJECT_TITAN_TIME_H
#define PROJECT_TITAN_TIME_H

#include <stdint.h>

/* Monotonic uptime. It is independent from the PS2 real-time clock. */
uint64_t TitanTime_NowMilliseconds(void);
uint64_t TitanTime_NowMicroseconds(void);

/* Clamp a time delta so suspend/stalls do not skip entire UI state machines. */
uint32_t TitanTime_ClampedDeltaMilliseconds(
    uint64_t previous_ms,
    uint64_t current_ms,
    uint32_t maximum_ms
);

#endif
