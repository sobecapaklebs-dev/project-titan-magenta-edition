#ifndef _OBJECTIVE_STATUS_H_
#define _OBJECTIVE_STATUS_H_
#include <ultra64.h>
#include <bondtypes.h>

extern struct objective_entry * objective_ptrs[OBJECTIVES_MAX];
extern  OBJECTIVESTATUS objectiveStatuses[OBJECTIVES_MAX]; //This is an array of 10 OBJECTIVESTATUS,

extern TagObjectRecord *ptr_last_tag_entry_type16;
extern struct setup_objective_text *ptr_last_briefing_setup_entry_type23;
extern struct criteria_roomentered *ptr_last_enter_room_subobject_entry_type20;
extern struct criteria_deposit *ptr_last_deposit_in_room_subobject_entry_type21;
extern struct criteria_picture *ptr_last_photo_obj_in_room_subobject_entry_type1E;

extern s32 objective_count;
extern s32 objective_count_related;

void             objectiveTakePictureHandler(void);
bool             objectiveIsAllComplete(void);
TagObjectRecord *sub_GAME_7F057080(s32 TagID);
s32              objectiveGetCount(void);
ObjectRecord *   objFindByTagId(s32 TagID);
u8 *             get_ptr_text_for_watch_breifing_page(WATCH_BRIEFING_PAGE page);
u16              objectiveGetTextId(s32 objectiveIndex);
u8 *             get_text_for_objective(s32 objectiveIndex);
s32              objectiveGetFlags(s32 objectiveIndex);
s32              get_difficulty_for_objective(s32 objectiveIndex);
OBJECTIVESTATUS  get_status_of_objective(s32 objectiveNum);
OBJECTIVESTATUS  objectiveGetStatus_WEAK(s32 objectiveNum, s32 unused);
void             sub_GAME_7F057C14(coord3d *coord, Mtxf *mtx);
void objectivestatusCheckDeposit(s32 weaponnum, s32 roomid);
#endif
