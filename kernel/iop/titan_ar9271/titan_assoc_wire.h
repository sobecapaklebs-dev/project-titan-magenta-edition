#ifndef TITAN_ASSOC_WIRE_H
#define TITAN_ASSOC_WIRE_H

#include <types.h>

/* Flags describing properties proven directly from the final 802.11
 * Association Request bytes, immediately before they are wrapped for USB. */
#define TITAN_ASSOC_WIRE_F_STA_ADDR       0x01u
#define TITAN_ASSOC_WIRE_F_BSSID_ADDRS    0x02u
#define TITAN_ASSOC_WIRE_F_RATES_VALID    0x04u
#define TITAN_ASSOC_WIRE_F_BASIC_SUBSET   0x08u
#define TITAN_ASSOC_WIRE_F_BASIC_CLEAR    0x10u
#define TITAN_ASSOC_WIRE_F_RSN_PRESENT    0x20u
#define TITAN_ASSOC_WIRE_F_HT_MATCH       0x40u
#define TITAN_ASSOC_WIRE_F_WMM_MATCH      0x80u

/* The active LINUX-GOLD-PCAP path is intentionally strict. Unknown/reordered IEs
 * are rejected locally instead of being transmitted to the AP. */
typedef struct TitanAssocWireResult {
    u32 dot11_hash;
    u16 legacy_mask;
    u16 basic_marker_mask;
    u8 supp_len;
    u8 ext_len;
    u8 ie_count;
    u8 flags;
} TitanAssocWireResult;

u32 titan_assoc_fnv1a32(const u8 *data,int len);

/* Return 1 for a byte-level valid managed-station Association Request.
 * Values >1 identify a local validation failure and MUST NOT be transmitted. */
int titan_assoc_wire_check(const u8 *frame,int len,
                           const u8 bssid[6],const u8 sta[6],
                           const u8 *ssid,int ssid_len,
                           u16 target_basic_mask,
                           int expect_rsn,int expect_ht,int expect_wmm,
                           TitanAssocWireResult *out);

#endif
