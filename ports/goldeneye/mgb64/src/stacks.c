#include <ultra64.h>

#if defined(NATIVE_PORT)
#define NATIVE_ZERO_INIT = {0}
#else
#define NATIVE_ZERO_INIT
#endif

u8 sp_boot[0x10] NATIVE_ZERO_INIT;
u8 sp_rmon[0x300] NATIVE_ZERO_INIT;
u8 sp_idle[0x40] NATIVE_ZERO_INIT;
u8 sp_shed[0x200] NATIVE_ZERO_INIT;
u8 sp_main[0x8000] NATIVE_ZERO_INIT;
u8 sp_audi[0x1000] NATIVE_ZERO_INIT;

#if defined(LEFTOVERDEBUG)
u8 sp_debug[0x6B0] NATIVE_ZERO_INIT;
#endif

#undef NATIVE_ZERO_INIT
