#include <ultra64.h>
#include <bondtypes.h>
#include "initactorpropstuff.h"

//uncomment when actor is worked on
//#include "chr.h"

void set_vtxallocator(Vertex *(*allocator)(s32));
void somethingwith_weapon_animation_groups(void);
Vertex *get_ptr_allocated_block_for_vertices(s32 count);

void init_weapon_animation_groups_maybe(void) {
    set_vtxallocator(get_ptr_allocated_block_for_vertices);
    somethingwith_weapon_animation_groups();
}
