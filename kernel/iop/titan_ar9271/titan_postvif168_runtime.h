#ifndef TITAN_POSTVIF168_RUNTIME_H
#define TITAN_POSTVIF168_RUNTIME_H

#include <types.h>

/*
 * Runtime-only subset of the hardware-proven Linux cold-start capture.
 *
 * TT1426 only consumes original WMI sequence 2962..3129 in the live
 * POSTVIF-168 phase. The standalone tester carried the entire 3928-event
 * laboratory capture (~250 KiB of immutable IOP data) because it also
 * supported old lockstep/oracle experiments. Project Titan does not execute
 * those experiments at runtime, so keep only the 168 commands and the exact
 * historical REG_READ values needed to rebind live save/restore dependencies.
 */
#define TITAN_POSTVIF168_RUNTIME_FIRST_SEQ 2962u
#define TITAN_POSTVIF168_RUNTIME_LAST_SEQ  3129u
#define TITAN_POSTVIF168_RUNTIME_COUNT     168u

typedef struct TitanPostvif168RuntimeEvent
{
    u16 blob_off;
    u16 length;
    u16 original_seq;
    u16 command_id;
    u16 response_off;
    u8 response_len;
    u8 reserved;
} TitanPostvif168RuntimeEvent;

extern const u8 titan_postvif168_runtime_blob[];
extern const u16 titan_postvif168_runtime_blob_size;
extern const u8 titan_postvif168_runtime_response_blob[];
extern const u16 titan_postvif168_runtime_response_blob_size;
extern const TitanPostvif168RuntimeEvent
    titan_postvif168_runtime_events[TITAN_POSTVIF168_RUNTIME_COUNT];

#endif
