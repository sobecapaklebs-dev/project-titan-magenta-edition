#include "titan_time.h"

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#include <timer.h>
#else
#include <time.h>
#endif

uint64_t TitanTime_NowMilliseconds(void)
{
#if defined(__EE__) || defined(_EE)
    const uint64_t ticks_per_ms = (uint64_t)kBUSCLK / 1000u;
    const uint64_t ticks = (uint64_t)GetTimerSystemTime();
    return ticks_per_ms != 0u ? ticks / ticks_per_ms : 0u;
#else
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    {
        return 0u;
    }

    return
        ((uint64_t)value.tv_sec * 1000u) +
        ((uint64_t)value.tv_nsec / 1000000u);
#endif
}

uint64_t TitanTime_NowMicroseconds(void)
{
#if defined(__EE__) || defined(_EE)
    const uint64_t ticks_per_us = (uint64_t)kBUSCLK / 1000000u;
    const uint64_t ticks = (uint64_t)GetTimerSystemTime();
    return ticks_per_us != 0u ? ticks / ticks_per_us : 0u;
#else
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    {
        return 0u;
    }

    return
        ((uint64_t)value.tv_sec * 1000000u) +
        ((uint64_t)value.tv_nsec / 1000u);
#endif
}

uint32_t TitanTime_ClampedDeltaMilliseconds(
    uint64_t previous_ms,
    uint64_t current_ms,
    uint32_t maximum_ms
)
{
    uint64_t delta;

    if (current_ms <= previous_ms)
    {
        return 0u;
    }

    delta = current_ms - previous_ms;
    if (delta > maximum_ms)
    {
        delta = maximum_ms;
    }

    return (uint32_t)delta;
}
