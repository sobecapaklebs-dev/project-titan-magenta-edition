#ifndef TITAN_AR9271_SCANNER_PROTOCOL_H
#define TITAN_AR9271_SCANNER_PROTOCOL_H

#define TITAN_AR9271_SCAN_RPC_ID 0x41523953u /* AR9S */
#define TITAN_AR9271_SCAN_RPC_GET_INFO 1
#define TITAN_AR9271_SCAN_RPC_FR_META 2
#define TITAN_AR9271_SCAN_RPC_FR_CHUNK 3
#define TITAN_AR9271_SCAN_RPC_FR_FORCE_FREEZE 4
#define TITAN_AR9271_SCAN_RPC_SET_CONFIG 5
#define TITAN_AR9271_SCAN_RPC_RETRY 6
#define TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE 7
#define TITAN_AR9271_SCAN_RPC_START_SCAN 8

#define TITAN_FR_MAGIC 0x46523134u /* FR14 */
#define TITAN_FR_VERSION 0x00010000u
#define TITAN_FR_BUILD_ID 0x1426u
/* Resident Titan integration: TT1426 ONLINE used ~140 records / ~13 KiB.
 * Keep generous diagnostic headroom without reserving the standalone lab
 * recorder's ~400 KiB inside the shared IOP runtime. */
#define TITAN_FR_RECORD_CAP 1024u
#define TITAN_FR_BLOB_CAP 32768u
#define TITAN_FR_BLOB_MAX 768u
#define TITAN_FR_CHUNK_DATA 960u
#define TITAN_FR_F_BLOB_TRUNCATED 0x4000u
#define TITAN_FR_F_SECRET_REDACTED 0x2000u
#define TITAN_FR_F_BLOB_DROPPED   0x8000u

enum TitanFlightRecType {
    TITAN_FR_EV_RUN_BEGIN = 1, TITAN_FR_EV_MARK = 2, TITAN_FR_EV_FAIL = 3, TITAN_FR_EV_USB = 4,
    TITAN_FR_EV_RADIO_STAGE = 10, TITAN_FR_EV_REG_POLL = 11, TITAN_FR_EV_RADIO_DIAG = 12,
    TITAN_FR_EV_WMI_TX = 20, TITAN_FR_EV_WMI_RX = 21, TITAN_FR_EV_WMI_ERR = 22,
    TITAN_FR_EV_RX_TARGET_BSS = 30, TITAN_FR_EV_RX_MGMT = 31, TITAN_FR_EV_TX_MGMT = 32,
    TITAN_FR_EV_TX_WIRE = 33, TITAN_FR_EV_TX_STATUS = 34, TITAN_FR_EV_TARGET = 35,
    TITAN_FR_EV_TARGET_TXDBG = 36, TITAN_FR_EV_RX_DATA = 37,
    TITAN_FR_EV_AUTH_RESULT = 40, TITAN_FR_EV_ASSOC_RESULT = 41,
    TITAN_FR_EV_WPA = 50, TITAN_FR_EV_NET = 51, TITAN_FR_EV_FREEZE = 0x7ffe
};
enum TitanFlightRecFreezeReason {
    TITAN_FR_FREEZE_NONE = 0, TITAN_FR_FREEZE_ERROR = 1, TITAN_FR_FREEZE_ASSOC_RESPONSE = 2,
    TITAN_FR_FREEZE_DISCONNECT = 3, TITAN_FR_FREEZE_MANUAL = 4,
    TITAN_FR_FREEZE_ONLINE_PASS = 5
};
typedef struct __attribute__((packed)) TitanFlightRecRecord {
    unsigned int seq, timestamp_us;
    unsigned short type, flags;
    int rc;
    unsigned int a,b,c,d,blob_off;
    unsigned short blob_len, aux;
    unsigned int guard;
} TitanFlightRecRecord;
typedef struct __attribute__((packed)) TitanFlightRecMeta {
    unsigned int magic,version,build_id,record_size,record_capacity,record_count;
    unsigned int blob_capacity,blob_used,dropped_records,dropped_blob_bytes,start_us,freeze_us;
    int freeze_reason;
    unsigned int frozen,secrets_redacted,guard_failures;
} TitanFlightRecMeta;
typedef struct __attribute__((packed)) TitanFlightRecChunkRequest {
    unsigned int kind; /* 0=records, 1=blob */
    unsigned int offset;
    unsigned int want;
} TitanFlightRecChunkRequest;
typedef struct __attribute__((packed)) TitanFlightRecChunkReply {
    unsigned int kind,offset,count,total;
    unsigned char data[TITAN_FR_CHUNK_DATA];
} TitanFlightRecChunkReply;

typedef struct __attribute__((packed)) TitanAr9271RuntimeConfig {
    unsigned int version;
    unsigned int ssid_len;
    char ssid[33];
    unsigned char pmk[32];
} TitanAr9271RuntimeConfig;
#define TITAN_AR9271_RUNTIME_CONFIG_VERSION 1u

#define TITAN_AR9271_SCAN_MAGIC 0x5343414eu /* SCAN */
#define TITAN_AR9271_MAX_APS 8

enum TitanAr9271ScanState {
    TITAN_SCAN_WAITING = 0,
    TITAN_SCAN_USB_DETECTED,
    TITAN_SCAN_CONFIGURING,
    TITAN_SCAN_UPLOADING_FW,
    TITAN_SCAN_WAITING_HTC,
    TITAN_SCAN_HTC_READY,
    TITAN_SCAN_RADIO_INIT,
    TITAN_SCAN_RX_START,
    TITAN_SCAN_SCANNING,
    TITAN_SCAN_TARGET_FOUND,
    TITAN_SCAN_STATION_INIT,
    TITAN_SCAN_AUTHENTICATING,
    TITAN_SCAN_ASSOCIATING,
    TITAN_SCAN_WPA_HANDSHAKE,
    TITAN_SCAN_WPA_SECURED,
    TITAN_SCAN_DHCP,
    TITAN_SCAN_ARP_GATEWAY,
    TITAN_SCAN_PING,
    TITAN_SCAN_ONLINE_PASS,
    TITAN_SCAN_ERROR = 100,
    TITAN_SCAN_DISCONNECTED
};

enum TitanWifiSecurity {
    TITAN_WIFI_OPEN = 0,
    TITAN_WIFI_WEP,
    TITAN_WIFI_WPA,
    TITAN_WIFI_WPA2,
    TITAN_WIFI_WPA3,
    TITAN_WIFI_UNKNOWN
};

typedef struct __attribute__((packed)) TitanWifiAp {
    char ssid[33];
    unsigned char bssid[6];
    signed char rssi_dbm;
    unsigned char channel;
    unsigned char security;
    unsigned char seen_count;
    unsigned short reserved;
} TitanWifiAp;

typedef struct __attribute__((aligned(16))) TitanAr9271ScanInfo {
    unsigned int magic;
    int state;
    int error_code;
    char detail[80];
    int dev_id;
    int control_pipe;
    int int_in_pipe;
    int int_out_pipe;
    int bulk_in_pipe;
    unsigned int fw_sent;
    unsigned int fw_total;
    unsigned short htc_credits;
    unsigned short htc_credit_size;
    unsigned char htc_max_endpoints;
    unsigned char wmi_endpoint;
    unsigned short fw_major;
    unsigned short fw_minor;
    unsigned char current_channel;
    unsigned char ap_count;
    unsigned short scan_round;
    unsigned int rx_usb_packets;
    unsigned int rx_80211_frames;
    unsigned int beacons_seen;
    unsigned int malformed_rx;
    unsigned int rx_bytes;
    unsigned int rx_completions;
    unsigned short rx_submit_errors;
    unsigned char rx_slots_armed;
    unsigned char rx_slots_peak;
    unsigned int rtc_status;
    unsigned int phy_active;
    unsigned int mac_cr;
    unsigned int rx_filter;
    unsigned int diag_sw;
    unsigned int synth_control;
    unsigned int agc_control;
    /* Firmware-side counters from WMI_RX_STATS_CMDID (ath9k_htc). */
    unsigned int tgt_rx_nobuf;
    unsigned int tgt_rx_host_send;
    unsigned int tgt_rx_host_done;
    /* HIF/USB RX telemetry (PORTCORE V8). */
    unsigned int rx_zero_completions;
    unsigned int rx_processed_completions;
    int rx_last_result;
    int rx_last_count;
    unsigned int rx_resubmits;
    unsigned int hif_packets;
    unsigned int hif_partial_packets;
    unsigned int hif_invalid_packets;
    unsigned short bulk_in_mps;
    unsigned char bulk_in_open_mode; /* 1=normal pipe, 2=aligned fallback */
    unsigned char raw_len;
    unsigned char raw_rx[16];

    /* Online bring-up telemetry (V9.1.1 IOP-LINKFIX). No plaintext PSK is ever exposed here. */
    unsigned char wifi_configured;
    unsigned char target_found;
    unsigned char target_channel;
    signed char target_rssi_dbm;
    unsigned char target_security;
    unsigned char mgmt_endpoint;
    unsigned char data_be_endpoint;
    unsigned char bulk_out_open_mode; /* TT1423: 2=aligned required (MPS64-safe); 1 is legacy/unsafe */
    int bulk_out_pipe;
    char target_ssid[33];
    unsigned char target_bssid[6];
    /* Target-SSID RSN diagnostics. These are populated even when the SSID is
     * deliberately rejected before station mode, so hardware tests can tell
     * whether the blocker is cipher/AKM/PMF rather than USB/TX. */
    unsigned char target_ssid_match;
    unsigned char target_rsn_reject; /* bit0 malformed/no-RSN, bit1 group!=CCMP, bit2 no pairwise CCMP, bit3 no PSK, bit4 MFPR */
    unsigned char target_group_cipher; /* RSN suite type, e.g. 2=TKIP, 4=CCMP */
    unsigned char target_pairwise_flags; /* bit0 CCMP, bit1 TKIP, bit2 GCMP, bit3 CCMP-256 */
    unsigned char target_akm_flags; /* bit0 PSK, bit1 SAE, bit2 PSK-SHA256 */
    unsigned char target_rsn_reserved;
    unsigned short target_rsn_caps;
    unsigned char local_mac[6];
    unsigned short auth_status;
    unsigned short assoc_status;
    unsigned short assoc_first_status;
    unsigned short aid;
    unsigned char assoc_profile;   /* historical diagnostics use older values; V10.2 normal path is 120=ATH9K-STATE */
    unsigned char assoc_attempts;
    unsigned char assoc_responses; /* Association Response frames actually received. */
    unsigned char assoc_timeouts;  /* Attempts with no Association Response. */
    unsigned char assoc_hist_count;
    unsigned char assoc_hist_profile[12];
    unsigned char assoc_hist_result[12]; /* 0..63=status, 0xfd=AUTH_FAIL, 0xfe=TX_FAIL, 0xff=NO_RESPONSE */
    unsigned char assoc_hist_tx[12];     /* 0=unknown, 1=no-ACK, 2=ACK; response status remains authoritative */
    unsigned short target_capability;   /* Beacon/Probe Capability Information */
    unsigned short assoc_tx_capability; /* Capability Information actually sent */
    unsigned short assoc_listen_interval; /* Listen Interval actually sent */
    unsigned char target_ht_present;
    unsigned char target_wmm_present;
    unsigned char target_rate_count;
    unsigned char target_supp_rate_count; /* exact Supported Rates IE (ID 1) length seen from AP */
    unsigned char target_ext_rate_count;  /* exact Extended Supported Rates IE (ID 50) length seen from AP */
    unsigned char target_supp_rates[8];
    unsigned char target_ext_rates[22];
    unsigned char assoc_rate_count;
    unsigned char assoc_supp_rate_count;
    unsigned char assoc_ext_rate_count;
    unsigned char assoc_supp_rates[8];
    unsigned char assoc_ext_rates[22];
    unsigned short target_legacy_rate_mask;
    unsigned short target_basic_rate_mask;
    unsigned short assoc_legacy_rate_mask;
    unsigned short assoc_basic_rate_mask;
    /* V9.4 ROOTCAUSE: show the actual RSN shape used in the Association
     * Request, plus enough of the AP rejection frame to stop guessing from a
     * vendor status code alone. AP RSN IEs observed on this target fit in the
     * 26-byte preview; lengths remain authoritative if a future AP is larger. */
    unsigned char target_rsn_ie_len;
    unsigned char assoc_rsn_mode; /* 0=selected,1=selected+AP caps,2=AP exact,3=none,4=accepted-station exact */
    unsigned char assoc_rsn_ie_len;
    unsigned char assoc_diag_flags; /* V10.6 wire flags: STA,BSSID,rates,basic-subset,basic-clear,RSN,HT,WMM */
    unsigned char target_rsn_preview[26];
    unsigned char assoc_rsn_preview[20];
    /* V10.6 PS2MAC-WIRE: exact bulk-OUT payload fingerprint.  wire_len/hash
     * cover HIF stream header + HTC + ath9k tx_mgmt_hdr + 802.11 frame. */
    unsigned short assoc_wire_len;
    unsigned int assoc_wire_hash;
    unsigned short assoc_resp_capability;
    unsigned char assoc_resp_ie_len;
    unsigned char assoc_resp_preview[8];
    /* V9.5 ADAPTIVEASSOC: capture the AP's rate IEs from the actual
     * Association Response. Several vendor APs return a rate ordering that
     * differs from their Beacon/Probe Response; adaptive profiles can now
     * replay that ordering instead of guessing another permutation. */
    unsigned char assoc_resp_supp_rate_count;
    unsigned char assoc_resp_ext_rate_count;
    unsigned char assoc_resp_supp_rates[8];
    unsigned char assoc_resp_ext_rates[22];
    unsigned short assoc_resp_legacy_rate_mask;
    unsigned short assoc_resp_basic_rate_mask;
    unsigned char assoc_req_selfcheck; /* 0=not run, 1=valid, >1=local AssocReq validation error */
    unsigned char assoc_adapt_flags;   /* historical: response rates learned/differ from target */
    unsigned char wpa_message;
    unsigned char pairwise_installed;
    unsigned char gtk_installed;
    unsigned char gtk_keyid;
    int wpa_last_error;
    unsigned char wpa_m1_seen;
    unsigned char wpa_m2_sent;
    unsigned char wpa_m3_seen;
    unsigned char wpa_m4_sent;
    unsigned char gtk_len;
    unsigned char dhcp_discovers;
    unsigned char dhcp_offers;
    unsigned char dhcp_requests;
    unsigned char dhcp_acks;
    unsigned char arp_probes;
    unsigned char arp_resolved;
    unsigned char online_diag_reserved; /* V10.6: final Association Request IE count */
    unsigned int tx_packets;
    unsigned int tx_errors;
    unsigned int tx_air_acks;
    unsigned int tx_air_noacks;
    unsigned int txstatus_events;
    unsigned int txstatus_timeouts;
    unsigned char tx_last_cookie;
    unsigned char tx_last_status; /* 0=unknown,1=no-ack,2=ack */
    unsigned char tx_last_ep;     /* endpoint encoded in WMI TXSTATUS ts_rate */
    unsigned char tx_reserved; /* V9.4: last local USB-TX error class (0=none,1=submit,2=completion,3=short,4=timeout) */
    unsigned char tx_usb_submit_errs;
    unsigned char tx_usb_completion_errs;
    unsigned char tx_usb_short_errs;
    unsigned char tx_usb_timeout_errs;
    unsigned int rx_eapol_frames;
    unsigned char dhcp_state;
    unsigned char ip_addr[4];
    unsigned char netmask[4];
    unsigned char gateway[4];
    unsigned char dns[4];
    unsigned char gateway_mac[6];
    unsigned short ping_sent;
    unsigned short ping_replies;
    unsigned short online_retries;
    unsigned char auth_tx_rate;  /* WMI TXSTATUS low-nibble rate index; 0xff=unknown */
    unsigned char assoc_tx_rate; /* Linux PS2-MAC success reference is 0 for both */

    TitanWifiAp aps[TITAN_AR9271_MAX_APS];
    /* V10.11 LINUX-HIF-ASYNC: consume the old 4-byte pad without changing
     * the 1024-byte EE<->IOP RPC contract. */
    unsigned char reg_rx_slots_armed;
    unsigned char reg_rx_slots_peak;
    unsigned short reg_rx_submit_errors;
} TitanAr9271ScanInfo;

#endif
