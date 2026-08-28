#ifndef TITAN_ASSOC_RATES_H
#define TITAN_ASSOC_RATES_H

#include <types.h>

#define TITAN_ASSOC_RATE_MODE_MAC80211      0
#define TITAN_ASSOC_RATE_MODE_BASIC_ONLY    1
#define TITAN_ASSOC_RATE_MODE_AP_COMBINED   2
#define TITAN_ASSOC_RATE_MODE_AP_EXACT      3
#define TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR  4
#define TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS 5

typedef struct TitanAssocRateIes {
    u8 supp[8];
    u8 ext[22];
    u8 supp_len;
    u8 ext_len;
    u8 total_len;
    u16 legacy_mask;
    u16 basic_mask;
} TitanAssocRateIes;

int titan_assoc_legacy_rate_index(u8 encoded);
int titan_assoc_build_rate_ies(const u8 *ap_supp,int nsupp,
                               const u8 *ap_ext,int next,
                               int mode,TitanAssocRateIes *out);

#endif
