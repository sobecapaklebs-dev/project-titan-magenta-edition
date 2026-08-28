#ifndef TITAN_ATH9K_RATE_H
#define TITAN_ATH9K_RATE_H

#include <types.h>

#define TITAN_ATH9K_RATE_MAX              30
#define TITAN_ATH9K_TARGET_RATE_LEN       72

#define TITAN_WLAN_RC_DS_FLAG             0x01u
#define TITAN_WLAN_RC_40_FLAG             0x02u
#define TITAN_WLAN_RC_SGI_FLAG            0x04u
#define TITAN_WLAN_RC_HT_FLAG             0x08u
#define TITAN_ATH_RC_TX_STBC_FLAG         0x20u

/* Build the ath9k_htc target-rate payload used by WMI_RC_RATE_UPDATE_CMDID.
 * This is the AR9271/1-stream subset of Linux 5.4 ath9k_htc_setup_rate().
 *
 * `legacy` entries are 500-kbit/s encoded rates and may contain the 802.11
 * Basic bit; the target rate table receives the rate value only.
 * `ap_ht_cap` is the 26-byte HT Capability IE payload from the associated AP.
 * Titan is HT20-only and AR9271 is one stream, so only peer MCS0..7 can enter
 * the HT rateset and neither DS nor 40-MHz target flags are emitted.
 *
 * Returns TITAN_ATH9K_TARGET_RATE_LEN, or a negative value on invalid input.
 */
int titan_ath9k_build_target_rate(u8 out[TITAN_ATH9K_TARGET_RATE_LEN],
                                  u8 sta_index,
                                  const u8 *legacy,u8 legacy_count,
                                  u8 use_ht,const u8 *ap_ht_cap);

#endif
