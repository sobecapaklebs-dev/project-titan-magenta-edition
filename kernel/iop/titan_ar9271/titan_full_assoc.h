#ifndef TITAN_FULL_ASSOC_H
#define TITAN_FULL_ASSOC_H

#include <types.h>

/* Managed-station Association Request constructor for the AR9271 proof stack.
 * It follows the ordering used by mac80211 for the generated 2.4 GHz pieces
 * and can also replay a safe subset of the IE ordering captured from a station
 * that this exact AP accepted. The caller owns the 802.11 MAC header. */
int titan_full_build_assoc_body(u16 capability,u16 listen_interval,
                                const u8 *ssid,u8 ssid_len,
                                const u8 *supp_rates,u8 supp_rate_count,
                                const u8 *ext_rates,u8 ext_rate_count,
                                const u8 *rsn_ie,int rsn_ie_len,
                                u8 channel,u8 use_rrm,const u8 rrm_caps[5],
                                const u8 *ht_cap,u8 add_ht,u8 add_wmm,
                                const u8 *template_ies,int template_ies_len,
                                u8 *out,int out_cap);

#endif
