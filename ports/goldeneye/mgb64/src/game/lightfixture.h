#ifndef _LIGHTFIXTURE_H_
#define _LIGHTFIXTURE_H_

#include <ultra64.h>

typedef struct s_lightfixture {
    s16 room_index;
    s16 RESERVED;
    Gfx *ptr_start_pertinent_DL;
    Gfx *ptr_end_pertinent_DL;
} s_lightfixture;

struct s_darkened_light {
    u16 room_index;
    u16 vtx_index;
};

void add_entry_to_init_lightfixture_table(Gfx *DL);
void save_ptrDL_enpoint_to_current_init_lightfixture_table(Gfx *dl_end);
s32 check_if_imageID_is_light(s32 imageID);

#endif
