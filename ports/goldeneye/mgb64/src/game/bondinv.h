#ifndef _BONDINV_H_
#define _BONDINV_H_
#include <ultra64.h>
#include <bondconstants.h>
#include "bondview.h"
#include "bondtypes.h"

void bondinvReinitInv(void);
s32 bondinvIsAliveWithFlag(void);
s32 bondinvCountTotalItemsInInv(void);

InvItem *bondinvGetItemByIndex(s32 index);
textoverride *bondinvGetTextbyObj(ObjectRecord *obj);
textoverride *bondinvGetTextbyWeaponID(ITEM_IDS weaponnum);
s32 bondinvGetTextbyInvIndex(s32 index);
u8 *bondinvGetNameByIndex(s32 index);
u8 *bondinvGetLongNameByIndex(s32 index);
s32 bondinvGet45AngleForIndex(s32 index);
s32 bondinvGetHoffsetForIndex(s32 index);
s32 bondinvGetVoffsetForIndex(s32 index);
s32 bondinvGetDepthForIndex(s32 index);
u8 *bondinvGetFirstTitlebyIndex(s32 index);
u8 *bondinvGetSecondTitlebyIndex(s32 index);
s32 bondinvGetDifferent45AngleForIndex(s32 index);
s32 bondinvGetVposWatchForIndex(s32 index);
s32 bondinvGetHposWatchForIndex(s32 index);
s32 bondinvGetDepthWatchForIndex(s32 index);
s32 bondinvGetXrotWatchForIndex(s32 index);
s32 bondinvGetYrotWatchForIndex(s32 index);
s32 bondinvGetCurEquippedItem(void);
void bondinvSetCurEquippedItem(int current_item);
void bondinvDetermineEquippedItem(void);
void bondinvAddTextOverride(textoverride *override);

void bondinvCycleBackward(s32 *nextright, s32 *nextleft, s32 requireammo);
void bondinvCycleForward(s32 *nextright, s32 *nextleft, s32 requireammo);
int bondinvHasGoldenGun(void);
bool bondinvHasGEKey(void);
u32 bondinvGetHeldKeyFlags(void);
int bondinvAddInvItem(ITEM_IDS item);
int bondinvAddDoublesInvItem(ITEM_IDS right, ITEM_IDS left);
s32 bondinvGetAllGunsFlag(void);
void bondinvSetAllGunsFlag(s32 all_guns);
bool          bondinvHasPropInInv(PropRecord *prop);
WeaponObjRecord *bondinvRemovePropWeaponByID(ITEM_IDS weaponnum);
void bondinvRemoveItemByID(ITEM_IDS weaponnum);
s32 bondinvGetWeaponOfChoice(s32 *weapon1, s32 *weapon2);

#endif
