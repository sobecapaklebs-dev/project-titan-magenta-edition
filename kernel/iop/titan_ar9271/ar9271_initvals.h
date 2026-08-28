#ifndef TITAN_AR9271_INITVALS_H
#define TITAN_AR9271_INITVALS_H
#include <types.h>
typedef struct { u32 v[5]; } TitanIni5;
typedef struct { u32 reg; u32 val; } TitanIni2;
extern const TitanIni5 titan_modes_9271[]; extern const unsigned int titan_modes_9271_count;
extern const TitanIni2 titan_common_9271[]; extern const unsigned int titan_common_9271_count;
extern const TitanIni5 titan_txgain_normal_9271[]; extern const unsigned int titan_txgain_normal_9271_count;
extern const TitanIni5 titan_txgain_high_9271[]; extern const unsigned int titan_txgain_high_9271_count;
#endif
