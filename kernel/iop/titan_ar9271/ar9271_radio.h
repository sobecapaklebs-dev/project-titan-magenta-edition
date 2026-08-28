#ifndef TITAN_AR9271_RADIO_H
#define TITAN_AR9271_RADIO_H
#include <types.h>

typedef struct TitanRadioIo {
    void *ctx;
    int (*wmi_cmd)(void *ctx, u16 cmd, const void *payload, int payload_len,
                   void *response, int response_cap, int *response_len);
} TitanRadioIo;

typedef struct TitanAr9271RadioDiag {
    u32 rtc_status;
    u32 phy_active;
    u32 mac_cr;
    u32 rx_filter;
    u32 diag_sw;
    u32 synth_control;
    u32 agc_control;
} TitanAr9271RadioDiag;

typedef struct TitanAr9271Radio {
    TitanRadioIo io;
    u32 mac_version;
    u32 mac_rev;
    u32 phy_rev;
    int reset_power_on;
    int htc_reset_init;
    u8 eeprom[376];
    u8 macaddr[6];
    u8 rxchainmask;
    u8 txchainmask;
    u32 save_def_antenna;
    u32 mac_sta_id1;
    u32 save_led_state;
    u64 tsf;
    u32 imrs2_reg;
    u32 sta_id1_defaults;
    u32 current_channel;
    /* V10.13 live ath9k TX-power state. Values are half-dBm units, matching ath9k. */
    u16 txpower_limit;
    u16 txpower_max;
    u16 txpower_applies;
    u8 opmode; /* HTC_M_STA=1 or HTC_M_MONITOR=8 */
    /* V10.13.1 B1311: exact live-init failure telemetry.  IOP-internal only;
     * the 1024-byte EE/IOP RPC ABI remains unchanged. */
    u8 init_stage;
    int init_rc;
    u16 last_wmi_cmd;
    int last_wmi_rc;
    u32 last_reg;
    u32 last_reg_val;
} TitanAr9271Radio;

/* Cold-init the MAC/PHY on channel 1. Does not start target RX by itself. */
int titan_ar9271_radio_init(TitanAr9271Radio *ah, const TitanRadioIo *io);
/* Program monitor-mode RX controls after WMI_START_RECV. */
int titan_ar9271_radio_start_monitor(TitanAr9271Radio *ah);
/* Fast same-band retune for passive scan. */
int titan_ar9271_radio_set_channel(TitanAr9271Radio *ah, int channel);
/* Lightweight target/host RX restart without replaying cold init. */
int titan_ar9271_radio_rx_kick(TitanAr9271Radio *ah);

/* Linux-PCAP restart stage before station VAP/node creation. */
int titan_ar9271_radio_restart_station_base(TitanAr9271Radio *ah, int channel);
/* Linux post-add-interface phase: set hardware MAC/BSSID mask and STA opmode
 * after VAP/local-node creation, before the final channel reset. */
int titan_ar9271_radio_post_vif_station(TitanAr9271Radio *ah);
/* Final station channel reset after station VAP/node creation. */
int titan_ar9271_radio_start_station(TitanAr9271Radio *ah, int channel);
/* Temporarily widen/restore the host RX filter for the passive air-oracle. */
int titan_ar9271_radio_set_promisc(TitanAr9271Radio *ah);
int titan_ar9271_radio_set_station_rx(TitanAr9271Radio *ah);
/* Program BSSID/AID after a successful association response. */
int titan_ar9271_radio_set_assoc(TitanAr9271Radio *ah, const u8 bssid[6], u16 aid);
/* Program one AES-CCMP key-cache entry. mac=NULL installs a group key. */
int titan_ar9271_radio_set_ccmp_key(TitanAr9271Radio *ah, u8 entry,
                                    const u8 key[16], const u8 *mac);
/* Program a receive-side WPA2 TKIP group key. key[0..15] is TK,
 * key[16..23] TX MIC, key[24..31] RX MIC (nl80211/mac80211 layout). */
int titan_ar9271_radio_set_tkip_group_key(TitanAr9271Radio *ah, u8 entry,
                                          const u8 key[32]);
/* Read-only snapshot used by the standalone lab screen. */
int titan_ar9271_radio_diag(TitanAr9271Radio *ah, TitanAr9271RadioDiag *diag);

#endif
