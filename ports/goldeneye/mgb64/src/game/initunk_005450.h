#ifndef _INITUNK_005450_H_
#define _INITUNK_005450_H_
#include <ultra64.h>

#ifdef NATIVE_PORT
/**
 * ModelRenderSlot — 64-bit-safe version of the N64 render slot pool entry.
 *
 * On N64, slots are 80-byte (0x50) structs with hardcoded byte offsets:
 *   +0x00 Model*, +0x04 ModelNode*, +0x08 f32 sort_z,
 *   +0x0C next link, +0x10 prev link, +0x20..0x4C skip-list links.
 *
 * On 64-bit, pointers are 8 bytes so the byte offsets are all wrong.
 * This struct replaces the byte-offset arithmetic with named fields.
 * The skip-list links (used only by N64 ASM sort functions) are omitted.
 */
typedef struct ModelRenderSlot {
    void *model;                     /* Model* that owns this node */
    void *node;                      /* ModelNode* in the model tree */
    f32 sort_z;                      /* z-depth for painter's sort */
    struct ModelRenderSlot *next;    /* forward link (free list / result chain) */
    struct ModelRenderSlot *prev;    /* backward link */
} ModelRenderSlot;
#endif

void sub_GAME_7F005450(void);

#endif
