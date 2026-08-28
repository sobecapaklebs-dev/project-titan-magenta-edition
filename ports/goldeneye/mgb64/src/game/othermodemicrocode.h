#ifndef _OTHERMODEMICROCODE_H_
#define _OTHERMODEMICROCODE_H_
#include <ultra64.h>
#include "bondview.h"

#include "bondtypes.h"


#define NUM_TEXTURES  0xBB9U

void texSelect(Gfx **gdlptr, struct sImageTableEntry *tconfig, u32 arg2, s32 arg3, u32 ulst);
void texDebugPushSourceTag(const char *tag);
void texDebugPopSourceTag(void);
#ifdef NATIVE_PORT
#include <stdio.h>
void texDebugDumpRecentFireEvents(FILE *fp);
#endif

#endif
