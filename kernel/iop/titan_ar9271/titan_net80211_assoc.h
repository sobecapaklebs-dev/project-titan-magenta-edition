#ifndef TITAN_NET80211_ASSOC_H
#define TITAN_NET80211_ASSOC_H

#include <types.h>

/* Minimal association-frame constructor surgically adapted from the
 * OpenBSD net80211 association path for Project Titan's PS2/IOP environment.
 * The caller owns the 802.11 MAC header; this routine emits the fixed
 * Association Request fields and Information Elements in net80211 order. */
int titan_net80211_build_assoc_body(u16 capability,u16 listen_interval,
                                    const u8 *ssid,u8 ssid_len,
                                    const u8 *rates,u8 rate_count,
                                    const u8 *rsn_ie,int rsn_ie_len,
                                    u8 add_qos,const u8 *ht_cap,u8 add_ht,
                                    u8 add_wmm,
                                    u8 *out,int out_cap);

#endif
