#ifndef TITAN_TEST_TIMER_H
#define TITAN_TEST_TIMER_H
#include <tamtypes.h>
static inline u64 GetTimerSystemTime(void) { static u64 value; value += 1000u; return value; }
#endif
