#ifndef _IMAGE_BANK_H_
#define _IMAGE_BANK_H_
#include <ultra64.h>
#include <bondtypes.h>
#include "bondview.h"

extern struct sImageTableEntry *crosshairimage;

extern struct sImageTableEntry *ammo9mmimage;
extern struct sImageTableEntry *rifleammoimage;
extern struct sImageTableEntry *shotgunammoimage;
extern struct sImageTableEntry *knifeammoimage;
extern struct sImageTableEntry *glaunchammoimage;
extern struct sImageTableEntry *rocketammoimage;
extern struct sImageTableEntry *genericmineammoimage;
extern struct sImageTableEntry *grenadeammoimage;
extern struct sImageTableEntry *magnumammoimage;
extern struct sImageTableEntry *goldengunammoimage;
extern struct sImageTableEntry *remotemineammoimage;
extern struct sImageTableEntry *timedmineammoimage;
extern struct sImageTableEntry *proxmineammoimage;
extern struct sImageTableEntry *tankammoimage;

extern struct sImageTableEntry *mainfolderimages;
extern struct sImageTableEntry *mpstageselimages;
extern struct sImageTableEntry *genericimage;
extern struct sImageTableEntry *skywaterimages;
extern struct sImageTableEntry *monitorimages;
extern struct sImageTableEntry *mpcharselimages;
extern struct sImageTableEntry *mpradarimages;
extern struct sImageTableEntry *impactimages;

extern u8* img_curpos;
extern s32 img_bitcount;
extern s32 *pGlobalimagetable;

void texReset(void);
u32 texReadBits(s32 bitCount);
#ifdef NATIVE_PORT
s32 portValidateImageEntry(const struct sImageTableEntry *img, const char *label);
#endif
#ifdef NATIVE_PORT
void texSetBitstring(u8 *pos);
#else
void texSetBitstring(s32 pos);
#endif

#endif
