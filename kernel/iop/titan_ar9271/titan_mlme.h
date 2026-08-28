#ifndef TITAN_MLME_H
#define TITAN_MLME_H

#include <types.h>

#define TITAN_MLME_MAX_SSID       32
#define TITAN_MLME_MAX_RATES      30
#define TITAN_MLME_LISTEN_INTERVAL 1
#define TITAN_MLME_MAX_TEMPLATE_IES 384
#define TITAN_MLME_MAX_AUTH_TRIES 3
#define TITAN_MLME_MAX_ASSOC_TRIES 3

enum TitanMlmeState {
    TITAN_MLME_IDLE = 0,
    TITAN_MLME_JOINED,
    TITAN_MLME_AUTHENTICATING,
    TITAN_MLME_AUTHENTICATED,
    TITAN_MLME_ASSOCIATING,
    TITAN_MLME_ASSOCIATED,
    TITAN_MLME_FAILED
};

typedef struct TitanMlmeBssInput {
    const u8 *bssid;
    const u8 *ssid;
    u8 ssid_len;
    u8 channel;
    u16 capability;
    const u8 *supp_rates;
    u8 supp_rate_count;
    const u8 *ext_rates;
    u8 ext_rate_count;
    u8 ht_present;
    const u8 *ht_cap;      /* 26-byte IE payload when ht_present != 0. */
    u8 ht_op_present;
    const u8 *ht_op;       /* 22-byte IE payload when ht_op_present != 0. */
    u8 wmm_present;
} TitanMlmeBssInput;

typedef struct TitanMlme {
    u8 state;
    u8 local_mac[6];
    u8 bssid[6];
    u8 reassoc;
    u8 current_ap[6];
    u8 ssid[TITAN_MLME_MAX_SSID];
    u8 ssid_len;
    u8 channel;
    u16 ap_capability;
    u16 assoc_capability;
    u16 listen_interval;

    u8 rates[TITAN_MLME_MAX_RATES];
    u8 rate_count;
    u8 supp_rates[8];
    u8 supp_rate_count;
    u8 ext_rates[22];
    u8 ext_rate_count;
    u16 legacy_rate_mask;
    u16 basic_rate_mask;

    u8 use_ht;
    u8 use_ht40;
    u8 use_qos;
    u8 use_wmm;
    u8 use_rrm;
    u8 rrm_caps[5];
    u8 ht_cap_ie[26];
    u8 template_ies[TITAN_MLME_MAX_TEMPLATE_IES];
    u16 template_ies_len;
    u8 auth_tries;
    u8 assoc_tries;
    u16 auth_status;
    u16 assoc_status;
    u16 aid;
} TitanMlme;

/* Build one coherent client-BSS state before any authentication is sent.
 * The legacy rate set follows current Linux mac80211 Association Request
 * behavior: advertise the local/AP intersection in local rate-table order and
 * do not copy the AP's Basic marker bits into the station's rate IEs. AP Basic
 * requirements are still validated before authentication. Returns -3 if the
 * BSS requires an unsupported legacy Basic rate. */
int titan_mlme_join(TitanMlme *m,const u8 local_mac[6],const TitanMlmeBssInput *bss);

/* These builders create complete raw 802.11 management frames. seq_ctrl must
 * already contain the sequence number in bits 4..15 (fragment number zero). */

/* Apply the non-secret shape learned from a successful third-party association
 * on the same BSS. The local MAC, local HT capability and WPA implementation
 * remain Titan's; the accepted station only supplies ordering/compatibility IEs. */
int titan_mlme_apply_accepted_template(TitanMlme *m,
                                       u16 capability,u16 listen_interval,
                                       const u8 *supp_rates,u8 supp_rate_count,
                                       const u8 *ext_rates,u8 ext_rate_count,
                                       const u8 *template_ies,u16 template_ies_len,
                                       u8 has_ht,u8 has_wmm,
                                       const u8 rrm_caps[5],u8 has_rrm);

/* Compatibility controls used only by bounded, evidence-backed fallbacks.
 * Reassociation is never enabled for the normal path; the caller may request it
 * only when an AP-accepted oracle exchange proved that exact subtype/Current-AP
 * shape. PHY mode can be reduced without mutating identity or security state. */
int titan_mlme_set_reassoc(TitanMlme *m,const u8 current_ap[6]);
void titan_mlme_clear_reassoc(TitanMlme *m);
void titan_mlme_set_phy_mode(TitanMlme *m,u8 use_ht,u8 use_wmm);

int titan_mlme_build_auth(TitanMlme *m,u16 seq_ctrl,u8 *out,int out_cap);
int titan_mlme_build_assoc(TitanMlme *m,u16 seq_ctrl,
                           const u8 *rsn_ie,int rsn_ie_len,
                           u8 *out,int out_cap);

/* Feed only responses already address/BSSID-filtered by the driver layer. */
void titan_mlme_begin_auth(TitanMlme *m);
int titan_mlme_rx_auth(TitanMlme *m,u16 algorithm,u16 transaction,u16 status);
void titan_mlme_begin_assoc(TitanMlme *m);
int titan_mlme_rx_assoc(TitanMlme *m,u16 status,u16 aid);

#endif
