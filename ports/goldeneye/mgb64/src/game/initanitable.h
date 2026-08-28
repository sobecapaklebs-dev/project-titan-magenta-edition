#ifndef _INITANITABLE_H_
#define _INITANITABLE_H_
#include <ultra64.h>
#include <assets/animationtable_data.h>



/**
 * Struct to hold animation data. This is never instantiated.
 * Instead, only a pointer to this will exist.
 */
struct animation_table_data {
    /**
     * Array length is arbitrary and shouldn't matter. The largest offset
     * into this is for the last animation pointer 0xE7C0, so just choosing
     * a value bigger than that, like u16_max_value.
    */
    u8 data[0xffff];
};

/**
 * Data holder for animations.
 */
extern struct animation_table_data* ptr_animation_table;

/**
 * Compute a ModelAnimation* from a named animation constant.
 * On N64, ANIM_DATA_* symbols have linker addresses equal to their byte offsets.
 * On 64-bit, we use the PTR_ANIM_* numeric defines instead.
 */
#ifdef NATIVE_PORT
#define ANIM_ADDR(name) ((struct ModelAnimation *)((u8 *)ptr_animation_table + PTR_ANIM_##name))
#define ANIM_FROM_OFFSET(off) ((struct ModelAnimation *)((u8 *)ptr_animation_table + (uintptr_t)(off)))
#else
#define ANIM_ADDR(name) ((struct ModelAnimation *)&ptr_animation_table->data[(s32)&ANIM_DATA_##name])
#define ANIM_FROM_OFFSET(off) ((struct ModelAnimation *)((s32)(off) + (s32)&ptr_animation_table->data))
#endif

/**
 * Contains offsets into ptr_animation_table for player and guard animations.
 * The index of each value corresponds to `enum ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_idle (same as ANIM_DATA_idle)
*/
#ifdef NATIVE_PORT
extern uintptr_t animation_table_ptrs1[];
#define ANIM_TABLE_PTR1(index) ANIM_FROM_OFFSET(animation_table_ptrs1[(index)])
#else
extern s32 animation_table_ptrs1[];
#define ANIM_TABLE_PTR1(index) ((struct ModelAnimation *)animation_table_ptrs1[(index)])
#endif

/**
 * Contains offsets into ptr_animation_table for object/vehicle animations.
 * The index of each value corresponds to `enum AIRCRAFT_ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_helicopter_cradle (same as ANIM_DATA_helicopter_cradle)
*/
#ifdef NATIVE_PORT
extern uintptr_t animation_table_ptrs2[];
#define ANIM_TABLE_PTR2(index) ANIM_FROM_OFFSET(animation_table_ptrs2[(index)])
#else
extern struct ModelAnimation * animation_table_ptrs2[];
#define ANIM_TABLE_PTR2(index) animation_table_ptrs2[(index)]
#endif

void alloc_load_expand_ani_table(void);

#endif
