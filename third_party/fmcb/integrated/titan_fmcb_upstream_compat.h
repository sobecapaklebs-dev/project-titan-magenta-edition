#ifndef TITAN_FMCB_UPSTREAM_COMPAT_H
#define TITAN_FMCB_UPSTREAM_COMPAT_H

/*
 * Compile-time compatibility boundary for the frozen FMCB installer sources.
 * Keep the upstream MagicGate/KELF/install logic source-identical while
 * supplying declarations that older PS2SDK headers exposed transitively.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iox_stat.h>

/* The integrated backend owns the progress UI instead of upstream menu.c. */
void InitProgressScreen(int label);

/* Modern PS2SDK no longer declares the legacy sio_printf helper through
 * <sio.h>.  FMCB only uses it for diagnostics in system.c, so route those
 * diagnostics through the EE/newlib stdout implementation used by Titan. */
#ifndef sio_printf
#define sio_printf printf
#endif

#endif /* TITAN_FMCB_UPSTREAM_COMPAT_H */
