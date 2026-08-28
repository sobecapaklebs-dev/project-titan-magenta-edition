#ifndef _INITMENUS_H_
#define _INITMENUS_H_
#include <ultra64.h>
#include <bondconstants.h>

void init_menus_or_reset(void);

#ifdef NATIVE_PORT
void pcPrimePostStageMenuForDirectBoot(LEVELID level, s32 multiplayer);
#endif

#endif
