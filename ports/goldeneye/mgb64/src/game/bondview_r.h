#ifndef _BONDVIEW_R_H_
#define _BONDVIEW_R_H_

#include <ultra64.h>
#include <bondconstants.h>

void bondviewLoadSetupIntroSection(void);
u32 weaponLoadProjectileModels(ITEM_IDS modelid);
#ifdef NATIVE_PORT
void portApplyGameplaySpawnFromIntro(void);
int portWarpBondToPad(s32 padnum);
int portWarpBondToPadOffset(s32 padnum, f32 right_offset, f32 forward_offset);
int portWarpBondToPadOffsetY(s32 padnum, f32 right_offset, f32 forward_offset, f32 y_offset);
int portWarpBondNearChr(s32 chrnum, f32 distance);
int portWarpBondNearChrAtAngle(s32 chrnum, f32 distance, f32 angle_deg);
#endif

#endif
