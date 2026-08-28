#ifndef TITAN_FLIGHTREC_H
#define TITAN_FLIGHTREC_H
#include <types.h>
#include "ar9271_scanner_protocol.h"
void titan_fr_reset(void);
int titan_fr_lock_ready(void);
void titan_fr_record(u16 type,u16 flags,s32 rc,u32 a,u32 b,u32 c,u32 d,
                     const void *blob,u16 blob_len,u16 aux);
void titan_fr_mark(u32 tag,u32 a,u32 b,u32 c);
void titan_fr_freeze(s32 reason);
void titan_fr_get_meta(TitanFlightRecMeta *out);
int titan_fr_copy_records(u32 byte_off,void *dst,u32 cap);
int titan_fr_copy_blob(u32 byte_off,void *dst,u32 cap);
int titan_fr_is_frozen(void);
#endif
