#include <ultra64.h>
#include <memp.h>
#ifdef NATIVE_PORT
#include <string.h>
#endif
#include <ramrom.h>
#include "initanitable.h"
#include "objecthandler.h"
#include "bondgame.h"

void sub_GAME_7F0009E0(struct bondstruct_unk_animation_related *animBuffer, OSMesgQueue *mq, uintptr_t unused);

//bss

// Where animation frames are saved. Can possibly hold as much as nine, but the game will ever store four at maximum.
char animations_frame_buffer[0x2D0];

// Msg Queue stuff (unused)
OSMesgQueue animMsgQ;
char dword_CODE_bss_80069458[0xC0]; // Unused. Possibly meant for unused message queue.
OSMesg animMesg[8];

// Animation table ptr
struct animation_table_data * ptr_animation_table;

//data
struct bondstruct_unk_animation_related D_80029D60 = {
    NULL,
    animations_frame_buffer, // Two pointers. One always points to the start of the buffer, the other can be modified.
    animations_frame_buffer
};

#ifdef NATIVE_PORT
uintptr_t animation_table_ptrs1[] = {
#else
s32 animation_table_ptrs1[] = {
#endif
    PTR_ANIM_idle,
    PTR_ANIM_fire_standing,
    PTR_ANIM_fire_standing_fast,
    PTR_ANIM_fire_hip,
    PTR_ANIM_fire_shoulder_left,
    PTR_ANIM_fire_turn_right1,
    PTR_ANIM_fire_turn_right2,
    PTR_ANIM_fire_kneel_right_leg,
    PTR_ANIM_fire_kneel_left_leg,
    PTR_ANIM_fire_kneel_left,
    PTR_ANIM_fire_kneel_right,
    PTR_ANIM_fire_roll_left,
    PTR_ANIM_fire_roll_right1,
    PTR_ANIM_fire_roll_left_fast,
    PTR_ANIM_hit_left_shoulder,
    PTR_ANIM_hit_right_shoulder,
    PTR_ANIM_hit_left_arm,
    PTR_ANIM_hit_right_arm,
    PTR_ANIM_hit_left_hand,
    PTR_ANIM_hit_right_hand,
    PTR_ANIM_hit_left_leg,
    PTR_ANIM_hit_right_leg,
    PTR_ANIM_death_genitalia,
    PTR_ANIM_hit_neck,
    PTR_ANIM_death_neck,
    PTR_ANIM_death_stagger_back_to_wall,
    PTR_ANIM_death_forward_face_down,
    PTR_ANIM_death_forward_spin_face_up,
    PTR_ANIM_death_backward_fall_face_up1,
    PTR_ANIM_death_backward_spin_face_down_right,
    PTR_ANIM_death_backward_spin_face_up_right,
    PTR_ANIM_death_backward_spin_face_down_left,
    PTR_ANIM_death_backward_spin_face_up_left,
    PTR_ANIM_death_forward_face_down_hard,
    PTR_ANIM_death_forward_face_down_soft,
    PTR_ANIM_death_fetal_position_right,
    PTR_ANIM_death_fetal_position_left,
    PTR_ANIM_death_backward_fall_face_up2,
    PTR_ANIM_side_step_left,
    PTR_ANIM_fire_roll_right2,
    PTR_ANIM_walking,
    PTR_ANIM_sprinting,
    PTR_ANIM_running,
    PTR_ANIM_bond_eye_walk,
    PTR_ANIM_bond_eye_fire,
    PTR_ANIM_bond_watch,
    PTR_ANIM_surrendering_armed,
    PTR_ANIM_surrendering_armed_drop_weapon,
    PTR_ANIM_fire_walking,
    PTR_ANIM_fire_running,
    PTR_ANIM_null50,
    PTR_ANIM_null51,
    PTR_ANIM_fire_jump_to_side_left,
    PTR_ANIM_fire_jump_to_side_right,
    PTR_ANIM_hit_butt_long,
    PTR_ANIM_hit_butt_short,
    PTR_ANIM_death_head,
    PTR_ANIM_death_left_leg,
    PTR_ANIM_slide_right,
    PTR_ANIM_slide_left,
    PTR_ANIM_jump_backwards,
    PTR_ANIM_extending_left_hand,
    PTR_ANIM_fire_throw_grenade,
    PTR_ANIM_spotting_bond,
    PTR_ANIM_look_around,
    PTR_ANIM_fire_standing_one_handed_weapon,
    PTR_ANIM_fire_standing_draw_one_handed_weapon_fast,
    PTR_ANIM_fire_standing_draw_one_handed_weapon_slow,
    PTR_ANIM_fire_hip_one_handed_weapon_fast,
    PTR_ANIM_fire_hip_one_handed_weapon_slow,
    PTR_ANIM_fire_hip_forward_one_handed_weapon,
    PTR_ANIM_fire_standing_right_one_handed_weapon,
    PTR_ANIM_fire_step_right_one_handed_weapon,
    PTR_ANIM_fire_standing_left_one_handed_weapon_slow,
    PTR_ANIM_fire_standing_left_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_forward_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_right_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_right_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_left_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_left_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_left_one_handed_weapon,
    PTR_ANIM_aim_walking_one_handed_weapon,
    PTR_ANIM_aim_walking_left_one_handed_weapon,
    PTR_ANIM_aim_walking_right_one_handed_weapon,
    PTR_ANIM_aim_running_one_handed_weapon,
    PTR_ANIM_aim_running_right_one_handed_weapon,
    PTR_ANIM_aim_running_left_one_handed_weapon,
    PTR_ANIM_aim_sprinting_one_handed_weapon,
    PTR_ANIM_running_one_handed_weapon,
    PTR_ANIM_sprinting_one_handed_weapon,
    PTR_ANIM_null91,
    PTR_ANIM_null92,
    PTR_ANIM_null93,
    PTR_ANIM_null94,
    PTR_ANIM_null95,
    PTR_ANIM_null96,
    PTR_ANIM_draw_one_handed_weapon_and_look_around,
    PTR_ANIM_draw_one_handed_weapon_and_stand_up,
    PTR_ANIM_aim_one_handed_weapon_left_right,
    PTR_ANIM_cock_one_handed_weapon_and_turn_around,
    PTR_ANIM_holster_one_handed_weapon_and_cross_arms,
    PTR_ANIM_cock_one_handed_weapon_turn_around_and_stand_up,
    PTR_ANIM_draw_one_handed_weapon_and_turn_around,
    PTR_ANIM_step_forward_and_hold_one_handed_weapon,
    PTR_ANIM_holster_one_handed_weapon_and_adjust_suit,
    PTR_ANIM_idle_unarmed,
    PTR_ANIM_walking_unarmed,
    PTR_ANIM_fire_walking_dual_wield,
    PTR_ANIM_fire_walking_dual_wield_hands_crossed,
    PTR_ANIM_fire_running_dual_wield,
    PTR_ANIM_fire_running_dual_wield_hands_crossed,
    PTR_ANIM_fire_sprinting_dual_wield,
    PTR_ANIM_fire_sprinting_dual_wield_hands_crossed,
    PTR_ANIM_walking_female,
    PTR_ANIM_running_female,
    PTR_ANIM_fire_kneel_dual_wield,
    PTR_ANIM_fire_kneel_dual_wield_left,
    PTR_ANIM_fire_kneel_dual_wield_right,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed_left,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed_right,
    PTR_ANIM_fire_standing_dual_wield,
    PTR_ANIM_fire_standing_dual_wield_left,
    PTR_ANIM_fire_standing_dual_wield_right,
    PTR_ANIM_fire_standing_dual_wield_hands_crossed_left,
    PTR_ANIM_fire_standing_dual_wield_hands_crossed_right,
    PTR_ANIM_fire_standing_aiming_down_sights,
    PTR_ANIM_fire_kneel_aiming_down_sights,
    PTR_ANIM_hit_taser,
    PTR_ANIM_death_explosion_forward,
    PTR_ANIM_death_explosion_left1,
    PTR_ANIM_death_explosion_back_left,
    PTR_ANIM_death_explosion_back1,
    PTR_ANIM_death_explosion_right,
    PTR_ANIM_death_explosion_forward_right1,
    PTR_ANIM_death_explosion_back2,
    PTR_ANIM_death_explosion_forward_roll,
    PTR_ANIM_death_explosion_forward_face_down,
    PTR_ANIM_death_explosion_left2,
    PTR_ANIM_death_explosion_forward_right2,
    PTR_ANIM_death_explosion_forward_right2_alt,
    PTR_ANIM_death_explosion_forward_right3,
    PTR_ANIM_null143,
    PTR_ANIM_null144,
    PTR_ANIM_null145,
    PTR_ANIM_null146,
    PTR_ANIM_running_hands_up,
    PTR_ANIM_sprinting_hands_up,
    PTR_ANIM_aim_and_blow_one_handed_weapon,
    PTR_ANIM_aim_one_handed_weapon_left,
    PTR_ANIM_aim_one_handed_weapon_right,
    PTR_ANIM_conversation,
    PTR_ANIM_drop_weapon_and_show_fight_stance,
    PTR_ANIM_yawning,
    PTR_ANIM_swatting_flies,
    PTR_ANIM_scratching_leg,
    PTR_ANIM_scratching_butt,
    PTR_ANIM_adjusting_crotch,
    PTR_ANIM_sneeze,
    PTR_ANIM_conversation_cleaned,
    PTR_ANIM_conversation_listener,
    PTR_ANIM_startled_and_looking_around,
    PTR_ANIM_laughing_in_disbelief,
    PTR_ANIM_surrendering_unarmed,
    PTR_ANIM_coughing_standing,
    PTR_ANIM_coughing_kneel1,
    PTR_ANIM_coughing_kneel2,
    PTR_ANIM_standing_up,
    PTR_ANIM_null169,
    PTR_ANIM_dancing,
    PTR_ANIM_dancing_one_handed_weapon,
    PTR_ANIM_keyboard_right_hand1,
    PTR_ANIM_keyboard_right_hand2,
    PTR_ANIM_keyboard_left_hand,
    PTR_ANIM_keyboard_right_hand_tapping,
    PTR_ANIM_bond_eye_fire_alt,
    PTR_ANIM_dam_jump,
    PTR_ANIM_surface_vent_jump,
    PTR_ANIM_cradle_jump,
    PTR_ANIM_cradle_fall,
    PTR_ANIM_credits_bond_kissing,
    PTR_ANIM_credits_natalya_kissing,
    0
};

#ifdef NATIVE_PORT
uintptr_t animation_table_ptrs2[] = {
#else
struct ModelAnimation *animation_table_ptrs2[] = {
#endif
    PTR_ANIM_helicopter_cradle,
    PTR_ANIM_plane_runway,
    PTR_ANIM_helicopter_takeoff,
    0
};


#ifdef NATIVE_PORT
/**
 * Byte-swap a single ModelAnimation record from big-endian (N64 ROM) to native.
 * Layout (64 bytes total):
 *   0x00: s32 address       — ROM offset to animation entry data
 *   0x04: u16 unk04         — next frame count
 *   0x06: u8  unk06         — nbits (bits per value in bitstream)
 *   0x07: u8  unk07         — padding/flags
 *   0x08: s32 (unk08+unk0A) — treated as single s32 offset by expand_ani_table_entries
 *   0x0C: u16 unk0C + u16 unk0E
 *   0x10-0x3C: 12 × s32     — various frame/offset data
 */
static inline u16 bswap16(u16 v) { return (v >> 8) | (v << 8); }
static inline u32 bswap32(u32 v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}

static void animConvertN64Record(u8 *rec, s32 recsize) {
    /* Animation record header layout (all multi-byte fields are big-endian):
     *   0x00: s32  ROM address for frame data
     *   0x04: u16  frame count
     *   0x06: u8   nbits (bitstream field width)
     *   0x07: u8   flags
     *   0x08: s32  entry table offset (relative to animation buffer base)
     *   0x0C: u16  unk0C
     *   0x0E: u16  bits per frame
     *   0x10: s32  bitstream offset (relative to animation buffer base)
     *
     * The entry table (6-byte records at the offset from 0x08) and bitstream
     * (at offset from 0x10) are SEPARATE data structures in the animation
     * buffer. We swap ONLY the header fields here. Entry table u16 fields
     * are swapped in a separate pass below. */

    *(u32 *)(rec + 0x00) = bswap32(*(u32 *)(rec + 0x00));
    *(u16 *)(rec + 0x04) = bswap16(*(u16 *)(rec + 0x04));
    *(u32 *)(rec + 0x08) = bswap32(*(u32 *)(rec + 0x08));
    *(u16 *)(rec + 0x0C) = bswap16(*(u16 *)(rec + 0x0C));
    *(u16 *)(rec + 0x0E) = bswap16(*(u16 *)(rec + 0x0E));
    /* The bitstream offset at 0x10 must always be swapped — it's needed
     * by animSwapEntryTable to compute the entry count.  The original
     * recsize > 0x13 guard skipped this for short records, leaving
     * bits_off in big-endian and causing animSwapEntryTable to bail
     * out, which left entry table base values unswapped (byte-swapped
     * root bone Y → guards floating in mid-air). */
    *(u32 *)(rec + 0x10) = bswap32(*(u32 *)(rec + 0x10));
}

/**
 * Byte-swap the animation entry table referenced by a record.
 * Entry layout: [u16 bitoff, u8 nbits, u8 pad, u16 base] = 6 bytes.
 * The u16 fields at offsets 0 and 4 need swapping from BE to native.
 * Entry count = (bitstream_offset - entry_offset) / 6.
 */
static void animSwapEntryTable(u8 *base, u8 *rec) {
    s32 entry_off = *(s32 *)(rec + 0x08);   /* already native after header swap */
    s32 bits_off  = *(s32 *)(rec + 0x10);   /* already native after header swap */
    /* entry_off==0 is valid — the entry table can start at the buffer base.
     * Only skip truly invalid offsets (negative). */
    if (entry_off < 0 || bits_off <= entry_off) return;

    s32 table_size = bits_off - entry_off;
    if (table_size <= 0 || table_size > 0x10000) return; /* sanity */

    s32 entry_count = table_size / 6;
    u8 *entries = base + entry_off;

    for (s32 i = 0; i < entry_count; i++) {
        u8 *e = entries + i * 6;
        /* Swap u16 at offset 0 (bit_offset) */
        *(u16 *)(e + 0) = bswap16(*(u16 *)(e + 0));
        /* offset 2,3: u8 fields — no swap */
        /* Swap u16 at offset 4 (base value) */
        *(u16 *)(e + 4) = bswap16(*(u16 *)(e + 4));
    }
}

/**
 * Compare function for qsort: sort uintptr_t offsets ascending.
 */
static int animOffsetCmp(const void *a, const void *b) {
    uintptr_t va = *(const uintptr_t *)a;
    uintptr_t vb = *(const uintptr_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/**
 * Byte-swap ModelAnimation records referenced by native animation tables.
 * Must be called AFTER romCopy but BEFORE expand_ani_table_entries.
 *
 * Records are variable-size: stride between consecutive sorted offsets
 * determines each record's bounds. We sort the offsets, compute strides,
 * and swap only within each record's actual size.
 */
static int animOffsetAlreadyCollected(uintptr_t *offsets, s32 count, uintptr_t offset) {
    for (s32 i = 0; i < count; i++) {
        if (offsets[i] == offset) {
            return 1;
        }
    }
    return 0;
}

static s32 animCountValidOffsets(uintptr_t *ptrs) {
    s32 count = 0;

    for (uintptr_t *p = ptrs; *p != 0; p++) {
        if (*p != 1) {
            count++;
        }
    }

    return count;
}

static s32 animCollectValidOffsets(uintptr_t *ptrs,
                                   uintptr_t *offsets,
                                   s32 count,
                                   s32 capacity) {
    for (uintptr_t *p = ptrs; *p != 0; p++) {
        if (*p != 1 &&
            count < capacity &&
            !animOffsetAlreadyCollected(offsets, count, *p)) {
            offsets[count++] = *p;
        }
    }

    return count;
}

static void animByteSwapRecordOffsets(uintptr_t *sorted, s32 count, s32 bufsize) {
    u8 *base = (u8 *)ptr_animation_table;

    if (count == 0) return;

    qsort(sorted, count, sizeof(uintptr_t), animOffsetCmp);

    /* Pass 1: Byte-swap each record's header fields */
    for (s32 i = 0; i < count; i++) {
        s32 recsize;
        if (i + 1 < count) {
            recsize = (s32)(sorted[i + 1] - sorted[i]);
        } else {
            recsize = bufsize - (s32)sorted[i];
        }
        if (recsize > 64) recsize = 64;
        if (recsize < 16) recsize = 16;
        animConvertN64Record(base + sorted[i], recsize);
    }

    /* Pass 2: Byte-swap each record's entry table u16 fields.
     * Must happen AFTER pass 1 because animSwapEntryTable reads the
     * header offsets (rec+0x08, rec+0x10) which are now in native order.
     * Entry tables may be shared across records — track swapped offsets
     * to avoid double-swapping. */
    {
        s32 *swapped_offsets = (s32 *)calloc(count, sizeof(s32));
        s32 swapped_count = 0;

        for (s32 i = 0; i < count; i++) {
            u8 *rec = base + sorted[i];
            s32 entry_off = *(s32 *)(rec + 0x08);

            /* Check if this entry table was already swapped */
            int already = 0;
            for (s32 j = 0; j < swapped_count; j++) {
                if (swapped_offsets[j] == entry_off) { already = 1; break; }
            }
            if (!already) {
                animSwapEntryTable(base, rec);
                if (swapped_count < count) {
                    swapped_offsets[swapped_count++] = entry_off;
                }
            }
        }

        free(swapped_offsets);
    }
}

static void animByteSwapAllRecordTables(uintptr_t *ptrs1,
                                        uintptr_t *ptrs2,
                                        s32 bufsize) {
    s32 capacity = animCountValidOffsets(ptrs1) + animCountValidOffsets(ptrs2);
    s32 count = 0;
    uintptr_t *sorted;

    if (capacity == 0) {
        return;
    }

    sorted = (uintptr_t *)malloc((size_t)capacity * sizeof(uintptr_t));
    if (sorted == NULL) {
        return;
    }

    count = animCollectValidOffsets(ptrs1, sorted, count, capacity);
    count = animCollectValidOffsets(ptrs2, sorted, count, capacity);
    animByteSwapRecordOffsets(sorted, count, bufsize);
    free(sorted);
}

static int animTableContainsOffset(uintptr_t *ptrs, uintptr_t offset) {
    for (uintptr_t *p = ptrs; *p != 0; p++) {
        if (*p != 1 && *p == offset) {
            return 1;
        }
    }

    return 0;
}

static void animAddRomStartToRecordsMissingFromTable(uintptr_t *ptrs,
                                                     uintptr_t *covered_ptrs) {
    s32 rom_entries_off = (s32)(uintptr_t)ROM_OFFSET(_animation_entriesSegmentRomStart);
    s32 capacity = animCountValidOffsets(ptrs);
    s32 processed_count = 0;
    uintptr_t *processed;

    if (capacity == 0) {
        return;
    }

    processed = (uintptr_t *)malloc((size_t)capacity * sizeof(uintptr_t));
    if (processed == NULL) {
        return;
    }

    for (uintptr_t *p = ptrs; *p != 0; p++) {
        if (*p != 1 &&
            !animTableContainsOffset(covered_ptrs, *p) &&
            !animOffsetAlreadyCollected(processed, processed_count, *p)) {
            s32 *rec = (s32 *)((u8 *)ptr_animation_table + *p);
            rec[0] += rom_entries_off;
            processed[processed_count++] = *p;
        }
    }

    free(processed);
}
#endif

/**
 * Converts animation table entries from offsets to absolute pointers.
 * Each entry in the array starts as an offset into ptr_animation_table.
 * This function converts them to absolute addresses, fixes up internal
 * pointers at offsets 0x08 and 0x10, and adds the ROM start address
 * to the first word of each entry.
 * Entries with value 0 mark the end of the array, and value 1 is skipped.
 */
#ifdef NATIVE_PORT
void expand_ani_table_entries(uintptr_t *arg0)
{
    uintptr_t *var_v0 = arg0;
    uintptr_t base = (uintptr_t)ptr_animation_table;

    /* Convert each offset to an absolute pointer into the animation data buffer.
     * After this loop, animation_table_ptrs1[i] holds a direct ModelAnimation*
     * (as a uintptr_t). Callers that use these entries directly (chr.c, front.c)
     * cast them to ModelAnimation*. */
    while (*var_v0 != 0)
    {
        if (*var_v0 != 1)
        {
            *var_v0 = *var_v0 + base;
        }
        var_v0++;
    }

    /* Add ROM entries segment start to each record's address field (s32 at offset 0).
     * This makes it a valid ROM file offset for PC romCopy in loadAnimationFrame. */
    s32 rom_entries_off = (s32)(uintptr_t)ROM_OFFSET(_animation_entriesSegmentRomStart);
    for (var_v0 = arg0; *var_v0 != 0; var_v0++)
    {
        if (*var_v0 != 1)
        {
            s32 *rec = (s32 *)*var_v0;
            rec[0] += rom_entries_off;
        }
    }
}
#else
void expand_ani_table_entries(s32 **arg0)
{
    s32 **var_v0 = arg0;

    while (*var_v0 != 0)
    {
        if (*var_v0 != (s32 *)1)
        {
            *var_v0 = (s32 *)((s32)*var_v0 + (s32)ptr_animation_table);
            ((s32 *)*var_v0)[2] += (s32)ptr_animation_table;
            ((s32 *)*var_v0)[4] += (s32)ptr_animation_table;
        }
        var_v0++;
    }

    for (var_v0 = arg0; *var_v0 != 0; var_v0++)
    {
        if (*var_v0 != (s32 *)1)
        {
            **var_v0 += (s32)&_animation_entriesSegmentRomStart;
        }
    }
}
#endif

void alloc_load_expand_ani_table(void)
{
    s32 animsDataSegmentSize;
    
    osCreateMesgQueue(&animMsgQ, animMesg, 8);
    sub_GAME_7F0009E0(&D_80029D60, &animMsgQ, (uintptr_t)dword_CODE_bss_80069458);
    
#ifdef NATIVE_PORT
    animsDataSegmentSize = ROM_SPAN(_animation_dataSegmentStart, _animation_dataSegmentEnd);
#else
    animsDataSegmentSize = (s32)&_animation_dataSegmentEnd - (s32)&_animation_dataSegmentStart;
#endif

    ptr_animation_table = mempAllocBytesInBank(animsDataSegmentSize, MEMPOOL_PERMANENT);

#ifdef NATIVE_PORT
    romCopy(ptr_animation_table, ROM_OFFSET(_animation_dataSegmentRomStart), animsDataSegmentSize);
#else
    romCopy(ptr_animation_table, &_animation_dataSegmentRomStart, animsDataSegmentSize);
#endif
#ifdef NATIVE_PORT
    /* Byte-swap all ModelAnimation records from big-endian (N64 ROM) to native.
     * Must happen before expand_ani_table_entries which reads the s32 fields. */
    animByteSwapAllRecordTables((uintptr_t *)animation_table_ptrs1,
                                (uintptr_t *)animation_table_ptrs2,
                                animsDataSegmentSize);
    /* Table 2 stays as offsets for ANIM_TABLE_PTR2(), so apply the ROM entries
     * base to any vehicle/aircraft records not covered by table 1 without
     * converting the table entries themselves into pointers. */
    animAddRomStartToRecordsMissingFromTable((uintptr_t *)animation_table_ptrs2,
                                             (uintptr_t *)animation_table_ptrs1);
    expand_ani_table_entries((uintptr_t *)animation_table_ptrs1);
    /* Keep animation_table_ptrs2 unexpanded on PC: ANIM_TABLE_PTR2() converts
     * these offsets at call sites. */
#else
    expand_ani_table_entries((s32*)&animation_table_ptrs1);
    expand_ani_table_entries((s32*)&animation_table_ptrs2);
#endif
}
