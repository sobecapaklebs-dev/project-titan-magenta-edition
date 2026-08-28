#ifndef _MBI_H_
#define _MBI_H_

/*
 * Clean-room compatibility umbrella for the media binary interfaces used by
 * display-list and audio command code.
 */

#include "platform_info.h"

#define _SHIFTL(v, s, w) ((unsigned int)(((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((unsigned int)(((unsigned int)(v) >> (s)) & ((0x01 << (w)) - 1)))
#define _SHIFT _SHIFTL

#define G_ON 1
#define G_OFF 0

#include <PR/gbi.h>
#include <PR/abi.h>

#define M_GFXTASK 1
#define M_AUDTASK 2
#define M_VIDTASK 3
#define M_HVQTASK 6
#define M_HVQMTASK 7

#define NUM_SEGMENTS 16
#define SEGMENT_OFFSET(a) ((unsigned int)(a) & 0x00ffffff)
#define SEGMENT_NUMBER(a) (((unsigned int)(a) << 4) >> 28)
#define SEGMENT_ADDR(num, off) (((num) << 24) + (off))

#ifndef NULL
#define NULL 0
#endif

#endif
