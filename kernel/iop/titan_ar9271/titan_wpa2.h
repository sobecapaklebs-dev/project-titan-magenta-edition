#ifndef TITAN_WIFI_WPA2_H
#define TITAN_WIFI_WPA2_H
#include <types.h>

#define TITAN_WPA2_EAPOL_MAX 384
#define TITAN_WPA2_MAX_RSN_IE 66
#define TITAN_RSN_CIPHER_TKIP 2
#define TITAN_RSN_CIPHER_CCMP 4

enum TitanWpa2Result {
    TITAN_WPA2_IGNORE = 0,
    TITAN_WPA2_SEND_M2 = 2,
    TITAN_WPA2_SEND_M4 = 4,
    TITAN_WPA2_SECURED = 5
};

typedef struct TitanWpa2 {
    u8 pmk[32];
    u8 sta[6];
    u8 ap[6];

    /* RSN IE sent by this station and exact AP RSN IE learned from scan.
     * Keep both buffers at the same validated maximum. Older builds used only
     * 32 bytes here while AP-exact compatibility accepted up to 66, which made
     * a long AP RSN IE a latent overwrite. */
    u8 rsn_ie[TITAN_WPA2_MAX_RSN_IE];
    u8 rsn_ie_len;
    u8 ap_rsn_ie[TITAN_WPA2_MAX_RSN_IE];
    u8 ap_rsn_ie_len;

    u8 anonce[32];
    u8 snonce[32];

    /* Follow wpa_supplicant's TPTK -> PTK promotion model. */
    u8 tptk[64];
    u8 ptk[64];
    /* Snapshot of the PTK actually installed in hardware. This is distinct
     * from ptk so a rekey can keep the old key active until M3 is verified. */
    u8 installed_ptk[64];
    /* GTK is 16 bytes for CCMP and 32 bytes for TKIP (TK + TX/RX MIC). */
    u8 gtk[32];
    u8 installed_gtk[32];
    u8 gtk_rsc[8];
    u8 gtk_keyid;
    u8 installed_gtk_keyid;
    u8 gtk_len;
    u8 installed_gtk_len;
    /* Negotiated RSN group cipher type: 4=CCMP, 2=TKIP. Pairwise stays CCMP. */
    u8 group_cipher;

    u8 descriptor_version;
    u8 eapol_version;
    u8 have_m1;
    u8 have_tptk;
    u8 have_ptk;
    u8 have_m3;
    u8 have_gtk;
    u8 ptk_hw_installed;
    u8 gtk_hw_installed;
    u8 ptk_install_pending;
    u8 gtk_install_pending;
    u8 secured;

    /* Software CCMP replay state. 0..15 QoS TIDs, 16 non-QoS. */
    u64 pairwise_rx_pn[17];
    u64 gtk_rx_pn[17];

    /* Last accepted EAPOL replay and last accepted M3 replay. */
    u8 replay[8];
    u8 m3_replay[8];
    u8 entropy[16];
} TitanWpa2;

void titan_wpa2_init(TitanWpa2 *w, const u8 pmk[32],
                     const u8 sta[6], const u8 ap[6],
                     const u8 entropy[16]);
int titan_wpa2_set_ap_rsn_ie(TitanWpa2 *w, const u8 *ie, int len);
int titan_wpa2_use_ap_rsn_ie_compat(TitanWpa2 *w);
/* Use an observed station RSN IE only when it is still within this tester's
 * actual security scope: WPA2 version 1, target group cipher, one CCMP
 * pairwise suite, one PSK AKM, and PMF not required. */
int titan_wpa2_use_station_rsn_ie_compat(TitanWpa2 *w,const u8 *ie,int len);
/* Keep the station-normal one-CCMP/one-PSK selection, but copy the AP's RSN
 * capability word. This catches consumer firmware that incorrectly treats the
 * capability word as a BSS echo field without advertising multiple pairwise
 * ciphers from the AP in the station RSN IE. */
int titan_wpa2_use_selected_ap_caps_rsn_ie_compat(TitanWpa2 *w);
void titan_wpa2_use_selected_rsn_ie(TitanWpa2 *w);
const u8 *titan_wpa2_rsn_ie(const TitanWpa2 *w, int *len);
/* Process one raw EAPOL payload (starting at EAPOL version byte).
 * On SEND_M2/SEND_M4, out contains the reply EAPOL payload. */
int titan_wpa2_rx_key(TitanWpa2 *w, const u8 *eapol, int len,
                      u8 *out, int out_cap, int *out_len);
const u8 *titan_wpa2_tk(const TitanWpa2 *w);
const u8 *titan_wpa2_gtk(const TitanWpa2 *w, u8 *keyid);
int titan_wpa2_gtk_len(const TitanWpa2 *w);
/* Raw GTK KDE TKIP layout is authenticator-oriented. Build the local
 * driver layout TK || local-TX-MIC || local-RX-MIC by swapping the
 * two Michael subkeys. */
int titan_wpa2_build_tkip_local_gtk(const TitanWpa2 *w, u8 out[32]);
/* installed_gtk intentionally retains the raw KDE bytes for duplicate
 * detection; for a supplicant, the AP TX-MIC (raw +16) is local RX. */
const u8 *titan_wpa2_installed_tkip_rx_mic(const TitanWpa2 *w);
u8 titan_wpa2_group_cipher(const TitanWpa2 *w);
const u8 *titan_wpa2_gtk_rsc(const TitanWpa2 *w);
int titan_wpa2_ptk_install_needed(const TitanWpa2 *w);
int titan_wpa2_gtk_install_needed(const TitanWpa2 *w);
void titan_wpa2_mark_ptk_installed(TitanWpa2 *w);
void titan_wpa2_mark_gtk_installed(TitanWpa2 *w);

#endif
