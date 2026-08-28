/*
 * Project Titan Magenta - resident AR9271 Wi-Fi service (IOP side)
 *
 * Scope: Qualcomm Atheros AR9271 (0cf3:9271), 2.4 GHz.  The tester scans,
 * enters station mode, performs open-system 802.11 auth/association, a narrow
 * WPA2-PSK/CCMP 4-way handshake, DHCPv4, ARP and ICMP Echo to 8.8.8.8.
 * Integrated service: resident under the Titan-owned USBD; never resets IOP/USBD.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>
#include <irx.h>
#include <loadcore.h>
#include <intrman.h>
#include <sifrpc.h>
#include <sysclib.h>
#include <thbase.h>
#include <thsemap.h>
#include <usbd.h>
#include <netman.h>
#include <usbd_macro.h>

#include "ar9271_scanner_protocol.h"
#include "ar9271_radio.h"
#include "ar9271_wmi_ids.h"
#include "ar9271_hif_rx.h"
#include "ar9271_tx.h"
#include "titan_wpa2.h"
#include "titan_crypto.h"
#include "titan_net.h"
#include "titan_assoc_rates.h"
#include "titan_assoc_wire.h"
#include "titan_mlme.h"
#include "titan_ath9k_rate.h"
#include "titan_postvif168_runtime.h"
#include "titan_flightrec.h"
#include "titan_ps2_mac.h"

IRX_ID("titan_ar9271", 1, 0);

extern unsigned char htc_9271_fw[];
extern unsigned int size_htc_9271_fw;

#define AR9271_VID                    0x0cf3
#define AR9271_PID                    0x9271
#define AR9271_FIRMWARE_MIN_SIZE      16384u
#define AR9271_FIRMWARE_MAX_SIZE      98304u
#define AR9271_FIRMWARE_ADDR          0x501000u
#define AR9271_FIRMWARE_TEXT_ADDR     0x903000u
#define AR9271_REQ_FW_DOWNLOAD        0x30
#define AR9271_REQ_FW_COMPLETE        0x31
#define AR9271_FW_CHUNK               4096u

#define AR9271_REG_IN_EP              0x83
#define AR9271_REG_OUT_EP             0x04
#define AR9271_BULK_IN_EP             0x82
#define AR9271_BULK_OUT_EP            0x01

#define HTC_MSG_READY_ID                    1
#define HTC_MSG_CONNECT_SERVICE_ID          2
#define HTC_MSG_CONNECT_SERVICE_RESPONSE_ID 3
#define HTC_MSG_SETUP_COMPLETE_ID           4
#define HTC_MSG_CONFIG_PIPE_ID              5
#define HTC_MSG_CONFIG_PIPE_RESPONSE_ID     6
#define HTC_SERVICE_SUCCESS                 0
#define HTC_CREDITS_AR9271                  33
#define USB_WLAN_TX_PIPE                    1

#define WMI_EVENT_BIT                  0x1000
#define WMI_TXSTATUS_EVENTID            0x1007
#define WMI_TITAN_TXDBG_EVENTID0         0x1f01
#define WMI_TITAN_TXDBG_EVENTID1         0x1f02
#define WMI_TITAN_TXDBG_EVENTID2         0x1f03
#define WMI_TITAN_TXDBG_EVENTID3         0x1f04
#define WMI_TITAN_TXDBG_EVENTID4         0x1f05
#define WMI_TITAN_TXDBG_EVENTID5         0x1f06
#define WMI_TITAN_TXDBG_CHUNK_LEN        32
#define WMI_TITAN_TXDBG_PARTS            6
#define WMI_TITAN_TXDBG_FULL_LEN         192
#define HTC_FLAGS_RECV_TRAILER          0x02u
#define ATH9K_HTC_TXSTAT_ACK            0x01u

#define HTC_FRAME_HDR_LEN              8
#define RX_STATUS_LEN                  40
#define RX_FCS_LEN                     4
#define RX_ERR_CRC                     0x01u
#define RX_ERR_PHY                     0x02u
#define RX_ERR_FIFO                    0x04u
#define RX_ERR_DECRYPT                 0x08u
#define RX_ERR_MIC                     0x10u
#define RX_ERR_KEYMISS                 0x20u
#define RX_ERR_CORRUPT_DESC            0x40u
#define RX_DROP_STATUS                 (RX_ERR_CRC | RX_ERR_PHY | RX_ERR_FIFO | RX_ERR_CORRUPT_DESC)
#define RX_KEY_INVALID                 0xff
#define RX_KEY_MAX                     128
#define RX_DEFAULT_NF_DBM              (-95)
#define HTC_M_STA                       1
#define HTC_M_MONITOR                   8
#define ORACLE_MON_VIF_IDX              1
#define ORACLE_MON_STA_IDX              1
#define ATH9K_KEY_TYPE_CLEAR            0
#define ATH9K_KEY_TYPE_AES              2
#define TITAN_PAIRWISE_KEYIX            4
#define ETH_P_IP                        0x0800
#define ETH_P_ARP                       0x0806
#define ETH_P_EAPOL                     0x888e
#define ONLINE_FRAME_MAX                1600
#define EAPOL_EVENT_MAX                 TITAN_WPA2_EAPOL_MAX

#define REG_IO_BUF_SIZE                1024
/* Linux ath9k_htc keeps 64 x 64-byte REG-IN requests permanently queued.
 * On PS2 the physical EP83 descriptor is still opened through the proven
 * BULK host-scheduler shim, but the queueing semantics now match Linux: the
 * firmware always has REG-IN receive capacity and WMI responses/events are
 * drained asynchronously instead of issuing a fresh IN after every OUT. */
#define REG_RX_SLOT_COUNT              64
#define REG_RX_XFER_SIZE               64
#define REG_TX_SLOT_COUNT              1
#define WMI_RSP_PAYLOAD_MAX            64
/* PS2SDK usbmass treats 4096 bytes as the maximum single USB 1.1 transfer.
 * Keep eight transfers queued concurrently, mirroring ath9k_htc's eight RX URBs
 * while staying friendly to the PS2 OHCI/USBD path. */
#define RX_SLOT_COUNT                  8
#define RX_XFER_SIZE                   4096

/* Exact ath9k_htc service-connect order captured on Linux 7.1.5-zen.
 * The order is semantically important because the firmware allocates HTC
 * endpoint numbers in connection order: WMI=1, MGMT=5, DATA_BE=6. */
static const u16 g_service_ids[9] = {
    0x0100, 0x0101, 0x0102, 0x0103, 0x0104, 0x0107, 0x0108, 0x0106, 0x0105
};

static const u8 g_scan_channels[13] = { 1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13 };

static int legacy_rate_index(u8 encoded)
{
    return titan_assoc_legacy_rate_index(encoded);
}

static TitanAr9271ScanInfo g_info;
static TitanAr9271Radio g_radio;
static TitanAr9271Tx g_tx;
static TitanWpa2 g_wpa;
static TitanNet g_net;
static TitanMlme g_mlme;

typedef struct TitanTargetAp {
    volatile int found;
    u8 bssid[6];
    u8 channel;
    s8 rssi_dbm;
    u16 capability;
    u8 rates[30];
    u8 rate_count;
    /* Preserve the AP's exact IE-1 / IE-50 partition as well as the combined
     * rate list. Some legacy/vendor APs are stricter than mac80211/hostapd and
     * status 18 can otherwise be impossible to diagnose from the combined list. */
    u8 supp_rates[8];
    u8 supp_rate_count;
    u8 ext_rates[22];
    u8 ext_rate_count;
    /* Exact RSN IE (ID+length+body) from the selected Beacon/Probe Response. */
    u8 rsn_ie[TITAN_WPA2_MAX_RSN_IE];
    u8 rsn_ie_len;
    /* HT details are kept locally so the policy sweep can reproduce the
     * normal mac80211 decision points instead of guessing from status 18.
     * IE 45 is 26 bytes. IE 61 is 22 bytes; its final 16 bytes are the
     * Basic HT-MCS Set used by an HT BSS membership check. */
    u8 ht_present;
    u8 ht_cap[26];
    u8 ht_op_present;
    u8 ht_op[22];
    u8 wmm_present;
} TitanTargetAp;

static TitanTargetAp g_target;

/* V9.6.1 ORACLEFIX: after the adaptive rate matrix proved that every rate
 * representation is explicitly rejected, preserve the next untested axis:
 * Capability Information/RRM.  If those probes also fail, an optional
 * passive air-oracle can learn the *shape* of an Association Request from a
 * different station that the same AP actually accepts, without cloning its
 * MAC or security credentials. */
typedef struct TitanAssocOracle {
    volatile int active;
    volatile int request_seen;
    volatile int accepted;
    u8 sta[6];
    /* Preserve whether the AP-accepted oracle exchange used Association or
     * Reassociation. A quick phone Wi-Fi reconnect commonly uses subtype 2;
     * replaying it later as subtype 0 is not the same management transaction. */
    u8 reassoc;
    u8 current_ap[6];
    u16 capability;
    u16 listen_interval;
    u8 supp_rates[8];
    u8 supp_rate_count;
    u8 ext_rates[22];
    u8 ext_rate_count;
    /* Exact non-secret RSN IE selected by the accepted station. This is much
     * more useful than replaying the AP Beacon RSN when the AP advertises more
     * than one suite. It is validated against our WPA2-PSK/CCMP scope before
     * use and also becomes the EAPOL-M2 RSN IE through TitanWpa2. */
    u8 rsn_ie[TITAN_WPA2_MAX_RSN_IE];
    u8 rsn_ie_len;
    u8 has_power;
    u8 power_min;
    u8 power_max;
    u8 has_ht;
    u8 has_wmm;
    u8 has_rrm_ie;
    /* Exact 5-byte Radio Measurement Enabled Capabilities element body from
     * the AP-accepted station request. V9.7 only remembered that IE 70 was
     * present, then set Capability bit 0x1000 without emitting the IE at all. */
    u8 rrm_caps[5];
    /* Full IE ordering from the accepted request. The replay builder only
     * consumes a whitelist of non-secret/non-device-specific elements and
     * replaces SSID/rates/RSN/HT/WMM with Titan-local values. */
    u8 template_ies[TITAN_MLME_MAX_TEMPLATE_IES];
    u16 template_ies_len;
} TitanAssocOracle;

static TitanAssocOracle g_oracle;
static volatile int g_assoc_template_mode;
static volatile int g_target_locked;
/* V10.13: proof that the selected BSS is still being received after the final
 * live station reset. This is deliberately independent of scan discovery. */
static volatile unsigned int g_target_live_bss;
static volatile int g_target_live_gate_active;
static volatile int g_target_live_gate_pass;
static volatile int g_auth_seen;
static volatile u16 g_auth_status;
static volatile int g_assoc_seen;
static volatile u16 g_assoc_status;
static volatile u16 g_assoc_aid;
static volatile int g_eapol_pending;
static volatile int g_eapol_len;
static u8 g_eapol_buf[EAPOL_EVENT_MAX] __attribute__((aligned(16)));
static volatile int g_net_event;
static u16 g_dot11_seq;
static u16 g_qos_seq[16];
/* WMI TXSTATUS is asynchronous on REG-IN. Cache per-cookie results so a
 * command response that arrives interleaved with TX status cannot lose ACK
 * telemetry. 1=seen/no-ACK, 2=seen/ACK. */
static u8 g_tx_status[256];
static u8 g_tx_status_ep[256];
/* WMI TXSTATUS ts_rate: high nibble=endpoint, low nibble=hardware rate index.
 * The Linux PS2-MAC success PCAP reports 0x50 for both Auth and Assoc:
 * endpoint 5, legacy rate index 0 (1 Mbps on the AR9271 2.4 GHz table). */
static u8 g_tx_status_rate[256];
/* Result for the most recent unicast management submission: 0=unknown,
 * 1=firmware reported no-ACK, 2=ACK. Association history records this
 * separately from the AP's Association Response. */
static volatile u8 g_last_mgmt_air;

static SifRpcDataQueue_t g_rpc_queue __attribute__((aligned(16)));
static SifRpcServerData_t g_rpc_server __attribute__((aligned(16)));
static unsigned char g_rpc_buffer[1024] __attribute__((aligned(64)));

/* Project Titan runtime configuration. Credentials are supplied by the EE
 * Network Service after boot. The IOP never stores a plaintext passphrase;
 * only the derived 32-byte WPA2 PMK crosses RPC. */
static char g_wifi_ssid[33];
static u8 g_wifi_pmk[32];
static unsigned int g_wifi_ssid_len;
static volatile int g_wifi_configured;
static volatile int g_worker_running;
static volatile int g_discovery_requested;
static volatile int g_discovery_radio_ready;
static int g_netman_if_id = -1;
static volatile int g_netman_bridge_active;
static volatile int g_netman_bridge_requested;
static volatile int g_netman_link_up;
/* Start with the console MAC only as a pre-radio fallback.  Once the AR9271
 * EEPROM MAC is known, NetMan/lwIP must use the associated station MAC. */
static u8 g_netman_mac[6] = { TITAN_PS2_BIOS_MAC_BYTES };

static int send_llc_payload_netman(const u8 dst[6],u16 ethertype,const u8 *payload,int payload_len);
static void titan_netman_rx_frame(const u8 dst[6],const u8 src[6],u16 ethertype,const u8 *payload,int payload_len);
static void titan_netman_set_link(int up);

static int g_dev_id = -1;
static int g_control_pipe = -1;
static int g_int_in_pipe = -1;
static int g_int_out_pipe = -1;
static int g_bulk_in_pipe = -1;
static int g_bulk_out_pipe = -1;

static int g_work_sema = -1;
static int g_ctrl_sema = -1;
static int g_rx_sema = -1;
static int g_reg_rx_sema = -1;
static int g_wmi_rsp_sema = -1;
static int g_eapol_lock = -1;
static int g_netman_tx_sema = -1;
static int g_netman_ctl_sema = -1;

static volatile int g_ctrl_done = 0;
static volatile int g_ctrl_result = 0;
static volatile int g_ctrl_count = 0;
static volatile int g_rx_armed = 0;

typedef struct TitanRxSlot {
    int index;
    volatile int armed;
    volatile int done;
    volatile int result;
    volatile int count;
    unsigned int submits;
    unsigned int completions;
    unsigned int zero_completions;
    u8 *buffer;
} TitanRxSlot;

typedef struct TitanRegRxSlot {
    int index;
    volatile int armed;
    volatile int done;
    volatile int result;
    volatile int count;
    unsigned int submits;
    unsigned int completions;
    u8 *buffer;
} TitanRegRxSlot;

typedef struct TitanRegTxSlot {
    int index;
    volatile int busy;
    volatile int result;
    volatile int count;
    u16 seq;
    u8 *buffer;
} TitanRegTxSlot;

typedef struct TitanWmiResponseMailbox {
    volatile int active;
    volatile int ready;
    volatile u16 expected_seq;
    volatile u16 expected_cmd;
    u16 cmd;
    u16 seq;
    int payload_len;
    u8 payload[WMI_RSP_PAYLOAD_MAX];
} TitanWmiResponseMailbox;

static u8 g_fw_stage[AR9271_FW_CHUNK] __attribute__((aligned(64)));
static u8 g_reg_rx[REG_IO_BUF_SIZE] __attribute__((aligned(64)));
static u8 g_reg_tx[REG_IO_BUF_SIZE] __attribute__((aligned(64)));
static u8 g_fr_wmi_redacted[REG_IO_BUF_SIZE] __attribute__((aligned(64)));
static u8 g_reg_rx_buffers[REG_RX_SLOT_COUNT][REG_RX_XFER_SIZE] __attribute__((aligned(64)));
static u8 g_reg_tx_buffers[REG_TX_SLOT_COUNT][REG_IO_BUF_SIZE] __attribute__((aligned(64)));
static TitanRegRxSlot g_reg_rx_slots[REG_RX_SLOT_COUNT] __attribute__((aligned(64)));
static TitanRegTxSlot g_reg_tx_slots[REG_TX_SLOT_COUNT] __attribute__((aligned(64)));
static volatile int g_reg_async_active = 0;
static volatile int g_reg_rx_pool_ready = 0;
static volatile u16 g_reg_out_error_seq = 0;
static TitanWmiResponseMailbox g_wmi_mailbox;
/* TT1423: only the post-key Linux-gold sync uses a polling wait. This keeps
 * the proven WMI path unchanged everywhere else while making this new phase
 * impossible to block forever inside WaitSema/SetAlarm. */
static int g_postkey_bounded_wmi_wait;
/* TT1417 host fix keeps the TT1416 target-side 192-byte TXDBG/NBUF-DMA snapshot; it is transported
 * as six 32-byte private WMI events. REG-IN stays Linux-matched at 64 bytes;
 * the association path itself is not modified. */
static u8 g_titan_txdbg_reassembly[WMI_TITAN_TXDBG_FULL_LEN];
static u8 g_titan_txdbg_parts = 0;
static u8 g_rx_buffers[RX_SLOT_COUNT][RX_XFER_SIZE] __attribute__((aligned(64)));
static TitanRxSlot g_rx_slots[RX_SLOT_COUNT] __attribute__((aligned(64)));
static TitanHifRx g_hif_rx;
static u16 g_wmi_seq = 0;
static volatile u16 g_lockstep_bad_seq = 0;
static volatile u8 g_lockstep_bad_kind = 0; /* 1=cmd,2=len,3=payload */
/* V10.12.1: register-read return values are observations of live hardware
 * state, not deterministic acknowledgements.  Keep them as diagnostic drift
 * telemetry instead of aborting a timed replay on the first poll sample. */
static volatile u16 g_lockstep_read_drift_count = 0;
static volatile u16 g_lockstep_read_drift_words = 0;
static volatile u16 g_lockstep_read_first_seq = 0;
static volatile u16 g_lockstep_read_first_word_seq = 0;
static volatile u32 g_lockstep_read_first_addr = 0;
static volatile u32 g_lockstep_read_first_expected = 0;
static volatile u32 g_lockstep_read_first_actual = 0;
static volatile u32 g_lockstep_trace_start_us = 0;
/* V10.12.5: the real PS2 returned status 18 for both the immutable Linux-gold
 * Association Request and a fresh-auth AP-rate-mirrored request.  Stop using
 * further rate retries as an active experiment; preserve the helper only as
 * audited historical code while mapping live register/timing divergence. */
#define LINUX_LOCKSTEP_RATE_RETRY 0
static u8 g_wmi_endpoint = 0;
static u8 g_mgmt_endpoint = 0;
static u8 g_data_be_endpoint = 0;
static u8 g_data_vo_endpoint = 0;

static int ar9271_probe(int dev_id);
static int ar9271_connect(int dev_id);
static int ar9271_disconnect(int dev_id);
static void worker_thread(void *arg);
static void reg_rx_thread(void *arg);
static void rx_thread(void *arg);
static void rpc_thread(void *arg);
static void *rpc_dispatch(int command, void *data, int size);

static UsbDriver g_usb_driver = {
    NULL, NULL, "titan_ar9271_scanner",
    ar9271_probe, ar9271_connect, ar9271_disconnect,
    0, 0, 0, 0, 0, NULL
};

static u16 rd_le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u16 rd_be16(const u8 *p) { return ((u16)p[0] << 8) | (u16)p[1]; }
static u32 rd_be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }
static void wr_le16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void wr_be32(u8 *p, u32 v) { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }

static u32 trace_clock_us32(void)
{
    iop_sys_clock_t t;
    u32 sec=0,usec=0;
    memset(&t,0,sizeof(t));
    GetSystemTime(&t);
    SysClock2USec(&t,&sec,&usec);
    return sec*1000000u+usec;
}

/* Preserve the Linux PCAP submission timeline as a minimum deadline.
 * WMI is still response-serialized like ath9k_wmi_cmd(); if PS2/OHCI is
 * slower than Linux we never rush/collapse commands, and if it is faster we
 * wait until the exact capture-relative event time before submitting the next
 * target-facing packet. Unsigned subtraction is wrap-safe for this 2.2 s run. */
static void trace_wait_deadline(u32 start_us,u32 target_elapsed_us)
{
    for(;;){
        u32 elapsed=trace_clock_us32()-start_us;
        s32 remain=(s32)(target_elapsed_us-elapsed);
        if(remain<=0)break;
        /* DelayThread is the native IOP microsecond sleep. Keep short waits
         * intact; these sub-5ms gaps account for over 1.1 s of the Linux
         * cold-start trace and were incorrectly discarded in V10.11. */
        DelayThread((int)(u32)remain);
    }
}

static void set_detail(const char *text)
{
    unsigned int i;
    for (i = 0; i + 1 < sizeof(g_info.detail) && text[i]; ++i) g_info.detail[i] = text[i];
    g_info.detail[i] = 0;
    for (++i; i < sizeof(g_info.detail); ++i) g_info.detail[i] = 0;
}

static void fail_scan(int code, const char *detail)
{
    int n=0;
    g_info.error_code = code;
    set_detail(detail);
    g_info.state = TITAN_SCAN_ERROR;
    while(detail && detail[n] && n<255)++n;
    titan_fr_record(TITAN_FR_EV_FAIL,0,code,(u32)g_info.state,(u32)g_radio.init_stage,
                    (u32)g_radio.last_wmi_cmd,g_radio.last_reg,detail,(u16)n,0);
    titan_fr_freeze(TITAN_FR_FREEZE_ERROR);
}

/* ---------------- synchronous REG/EP0 transfer path ------------------- */
static void drain_sema(int sema)
{
    while (PollSema(sema) == 0) { }
}

static void ctrl_callback(int result, int count, void *arg)
{
    (void)arg;
    g_ctrl_result = result;
    g_ctrl_count = count;
    g_ctrl_done = 1;
    iSignalSema(g_ctrl_sema);
}

static unsigned int ctrl_timeout_alarm(void *arg)
{
    (void)arg;
    iSignalSema(g_ctrl_sema);
    return 0;
}

static void prepare_ctrl(void)
{
    drain_sema(g_ctrl_sema);
    g_ctrl_done = 0;
    g_ctrl_result = 0;
    g_ctrl_count = 0;
}

static int wait_ctrl(int submit_rc, unsigned int timeout_us)
{
    iop_sys_clock_t timeout;
    if (submit_rc != USB_RC_OK) return -1;
    timeout.lo = timeout_us;
    timeout.hi = 0;
    if (SetAlarm(&timeout, ctrl_timeout_alarm, NULL) == 0) {
        WaitSema(g_ctrl_sema);
        CancelAlarm(ctrl_timeout_alarm, NULL);
    } else {
        WaitSema(g_ctrl_sema);
    }
    if (!g_ctrl_done) return -2;
    return g_ctrl_result == USB_RC_OK ? 0 : -3;
}

static int sync_control(int request_type, int request, int value, int index,
                        int length, void *buffer, unsigned int timeout_us)
{
    int rc;
    prepare_ctrl();
    rc = UsbControlTransfer(g_control_pipe, request_type, request, value, index,
                            length, buffer, ctrl_callback, NULL);
    return wait_ctrl(rc, timeout_us);
}

static int sync_set_configuration(int config_value)
{
    int rc;
    prepare_ctrl();
    rc = UsbSetDeviceConfiguration(g_control_pipe, config_value, ctrl_callback, NULL);
    return wait_ctrl(rc, 2000000);
}

static int sync_reg_in(void *buffer, int length, unsigned int timeout_us)
{
    int rc;
    prepare_ctrl();
    memset(buffer, 0, length);
    rc = UsbBulkTransfer(g_int_in_pipe, buffer, length, ctrl_callback, NULL);
    return wait_ctrl(rc, timeout_us);
}

static int sync_reg_out(const void *buffer, int length, unsigned int timeout_us)
{
    int rc;
    prepare_ctrl();
    rc = UsbBulkTransfer(g_int_out_pipe, (void *)buffer, length, ctrl_callback, NULL);
    return wait_ctrl(rc, timeout_us);
}

static unsigned int htc_offset(const u8 *buf, unsigned int count)
{
    /* Some ath9k_htc REG-IN paths prepend this 4-byte transport marker. */
    if (count >= 4 && buf[0] == 0x00 && buf[1] == 0xc6 && buf[2] == 0x00 && buf[3] == 0x00)
        return 4;
    return 0;
}

/* Validate an HTC frame exactly like htc_hst.c before handing its payload
 * upward. payload_len includes a receive trailer when HTC_FLAGS_RECV_TRAILER
 * is set; control[0] is the trailer length and must be removed first. */
static int htc_payload_view(const u8 *frame, int frame_len, u8 *endpoint,
                            const u8 **payload, int *payload_len)
{
    int plen, trailer = 0;
    if (!frame || frame_len < HTC_FRAME_HDR_LEN || !payload || !payload_len) return -1;
    plen = (int)rd_be16(frame + 2);
    if (plen < 0 || HTC_FRAME_HDR_LEN + plen > frame_len) return -2;
    if (frame[1] & HTC_FLAGS_RECV_TRAILER) {
        trailer = frame[4];
        if (trailer <= 0 || trailer > plen) return -3;
    }
    if (endpoint) *endpoint = frame[0];
    *payload = frame + HTC_FRAME_HDR_LEN;
    *payload_len = plen - trailer;
    return 0;
}

static void process_wmi_event(u16 event_id, const u8 *payload, int len)
{
    int i, cnt;
    if (event_id >= WMI_TITAN_TXDBG_EVENTID0 &&
        event_id <= WMI_TITAN_TXDBG_EVENTID5) {
        int part = (int)(event_id - WMI_TITAN_TXDBG_EVENTID0);
        u32 packed;
        if (!payload || len != WMI_TITAN_TXDBG_CHUNK_LEN ||
            part < 0 || part >= WMI_TITAN_TXDBG_PARTS) {
            titan_fr_record(TITAN_FR_EV_WMI_ERR,0,-1416,(u32)event_id,
                            (u32)(len<0?0:len),(u32)g_titan_txdbg_parts,0,NULL,0,0);
            g_titan_txdbg_parts = 0;
            return;
        }
        if (part == 0) {
            if (payload[0] != 0x54 || payload[1] != 0x58) {
                titan_fr_record(TITAN_FR_EV_WMI_ERR,0,-1417,(u32)event_id,
                                (u32)len,0,0,payload,(u16)len,0);
                g_titan_txdbg_parts = 0;
                return;
            }
            memcpy(g_titan_txdbg_reassembly, payload, WMI_TITAN_TXDBG_CHUNK_LEN);
            g_titan_txdbg_parts = 1;
            return;
        }
        if (g_titan_txdbg_parts != (u8)part) {
            titan_fr_record(TITAN_FR_EV_WMI_ERR,0,-1418,(u32)event_id,
                            (u32)len,(u32)g_titan_txdbg_parts,(u32)part,
                            payload,(u16)len,0);
            g_titan_txdbg_parts = 0;
            return;
        }
        memcpy(g_titan_txdbg_reassembly + part * WMI_TITAN_TXDBG_CHUNK_LEN,
               payload, WMI_TITAN_TXDBG_CHUNK_LEN);
        ++g_titan_txdbg_parts;
        if (g_titan_txdbg_parts != WMI_TITAN_TXDBG_PARTS) return;
        g_titan_txdbg_parts = 0;
        payload = g_titan_txdbg_reassembly;
        packed=(u32)payload[5] | ((u32)payload[6]<<8) |
               ((u32)payload[7]<<16) | ((u32)payload[8]<<24);
        titan_fr_record(TITAN_FR_EV_TARGET_TXDBG,0,0,(u32)payload[4],packed,
                        (u32)rd_le16(payload+16),(u32)rd_le16(payload+18),
                        payload,WMI_TITAN_TXDBG_FULL_LEN,(u16)payload[2]);
        return;
    }
    if (event_id != WMI_TXSTATUS_EVENTID || !payload || len < 1) return;
    cnt = payload[0];
    if (cnt > 12) cnt = 12;
    if (1 + cnt * 3 > len) return;
    ++g_info.txstatus_events;
    for (i = 0; i < cnt; ++i) {
        const u8 *st = payload + 1 + i * 3;
        u8 cookie = st[0], epid = (u8)((st[1] >> 4) & 0x0f);
        u8 rate = (u8)(st[1] & 0x0f), flags = st[2];
        g_tx_status[cookie] = (flags & ATH9K_HTC_TXSTAT_ACK) ? 2 : 1;
        g_tx_status_ep[cookie] = epid;
        g_tx_status_rate[cookie] = rate;
        g_info.tx_last_cookie = cookie;
        g_info.tx_last_status = g_tx_status[cookie];
        g_info.tx_last_ep = epid;
        titan_fr_record(TITAN_FR_EV_TX_STATUS,0,0,(u32)cookie,(u32)epid,(u32)rate,(u32)flags,st,3,0);
        if (flags & ATH9K_HTC_TXSTAT_ACK) ++g_info.tx_air_acks;
        else ++g_info.tx_air_noacks;
    }
}

/* Parse one REG-IN HTC/WMI frame. This is also used while waiting for normal
 * WMI command responses so asynchronous TXSTATUS events are never discarded. */
static int parse_reg_wmi_frame(const u8 *buf, int count, u16 *cmd, u16 *seq,
                               const u8 **payload, int *payload_len)
{
    unsigned int off; const u8 *frame, *w; int wlen; u8 ep;
    if (!buf || count <= 0) return -1;
    off = htc_offset(buf, (unsigned int)count);
    if ((int)off >= count) return -2;
    frame = buf + off;
    if (htc_payload_view(frame, count - (int)off, &ep, &w, &wlen) < 0) return -3;
    if (ep != g_wmi_endpoint || wlen < 4) return -4;
    if (cmd) *cmd = rd_be16(w);
    if (seq) *seq = rd_be16(w + 2);
    if (payload) *payload = w + 4;
    if (payload_len) *payload_len = wlen - 4;
    return 0;
}

/* ---------------- Linux-style asynchronous REG-IN/WMI path ------------ */
/*
 * Linux ath9k_htc does not perform one blocking REG-IN transaction after
 * each WMI REG-OUT. It keeps a pool of 64 64-byte receive requests queued
 * continuously. The PS2 host-side endpoint-type shim remains necessary, but
 * from this point upward we preserve those queueing semantics.
 */
static void reg_rx_callback(int result, int count, void *arg)
{
    TitanRegRxSlot *slot = (TitanRegRxSlot *)arg;
    if (!slot) return;
    slot->result = result;
    slot->count = count;
    slot->armed = 0;
    slot->done = 1;
    ++slot->completions;
    SignalSema(g_reg_rx_sema);
}

static void reg_out_callback(int result, int count, void *arg)
{
    TitanRegTxSlot *slot = (TitanRegTxSlot *)arg;
    if (!slot) return;
    slot->result = result;
    slot->count = count;
    if (result != USB_RC_OK) g_reg_out_error_seq = slot->seq;
    slot->busy = 0;
}

static int reg_rx_submit_slot(TitanRegRxSlot *slot)
{
    int rc;
    if (!slot || !g_reg_async_active || g_dev_id < 0 || g_int_in_pipe < 0 || slot->armed) return -1;
    slot->done = 0;
    slot->result = 0;
    slot->count = 0;
    slot->armed = 1;
    ++slot->submits;
    rc = UsbBulkTransfer(g_int_in_pipe, slot->buffer, REG_RX_XFER_SIZE, reg_rx_callback, slot);
    if (rc != USB_RC_OK) {
        slot->armed = 0;
        ++g_info.reg_rx_submit_errors;
        return -2;
    }
    return 0;
}

static int reg_rx_armed_count(void)
{
    int i, n = 0;
    for (i = 0; i < REG_RX_SLOT_COUNT; ++i) if (g_reg_rx_slots[i].armed) ++n;
    g_info.reg_rx_slots_armed = (u8)n;
    if ((u8)n > g_info.reg_rx_slots_peak) g_info.reg_rx_slots_peak = (u8)n;
    return n;
}

static void reg_dispatch_frame(const u8 *buf, int count)
{
    const u8 *wp = NULL;
    int wlen = 0;
    u16 cmd = 0, seq = 0;
    int n;
    if (parse_reg_wmi_frame(buf, count, &cmd, &seq, &wp, &wlen) < 0) return;
    if (cmd & WMI_EVENT_BIT) {
        process_wmi_event(cmd, wp, wlen);
        return;
    }
    if (!g_wmi_mailbox.active || seq != g_wmi_mailbox.expected_seq) return;
    n = wlen;
    if (n < 0) n = 0;
    if (n > WMI_RSP_PAYLOAD_MAX) n = WMI_RSP_PAYLOAD_MAX;
    g_wmi_mailbox.cmd = cmd;
    g_wmi_mailbox.seq = seq;
    g_wmi_mailbox.payload_len = n;
    if (n > 0) memcpy(g_wmi_mailbox.payload, wp, n);
    g_wmi_mailbox.ready = 1;
    g_wmi_mailbox.active = 0;
    SignalSema(g_wmi_rsp_sema);
}

static void reg_rx_thread(void *arg)
{
    int i;
    u8 local[REG_RX_XFER_SIZE] __attribute__((aligned(16)));
    (void)arg;
    for (;;) {
        if (!g_reg_async_active || g_dev_id < 0 || g_int_in_pipe < 0) {
            DelayThread(2000);
            continue;
        }
        WaitSema(g_reg_rx_sema);
        for (i = 0; i < REG_RX_SLOT_COUNT; ++i) {
            TitanRegRxSlot *slot = &g_reg_rx_slots[i];
            int result, count;
            if (!slot->done) continue;
            result = slot->result;
            count = slot->count;
            if (count > REG_RX_XFER_SIZE) count = REG_RX_XFER_SIZE;
            if (result == USB_RC_OK && count > 0) memcpy(local, slot->buffer, count);
            slot->done = 0;
            /* Match ath9k_hif_usb_reg_in_cb ordering: dispatch the completed
             * command/event first, then immediately resubmit the receive slot.
             * We dispatch from a private copy so the slot buffer ownership is
             * unambiguous on PS2SDK. */
            if (result == USB_RC_OK && count > 0) reg_dispatch_frame(local, count);
            if (g_reg_async_active && g_dev_id >= 0 && g_int_in_pipe >= 0)
                (void)reg_rx_submit_slot(slot);
        }
        (void)reg_rx_armed_count();
    }
}

static int reg_async_start(void)
{
    int i, wait;
    if (g_int_in_pipe < 0 || g_dev_id < 0) return -1;
    drain_sema(g_reg_rx_sema);
    drain_sema(g_wmi_rsp_sema);
    memset(&g_wmi_mailbox, 0, sizeof(g_wmi_mailbox));
    g_reg_out_error_seq = 0;
    g_reg_rx_pool_ready = 0;
    g_info.reg_rx_slots_armed = 0;
    g_info.reg_rx_slots_peak = 0;
    g_info.reg_rx_submit_errors = 0;
    for (i = 0; i < REG_RX_SLOT_COUNT; ++i) {
        memset(&g_reg_rx_slots[i], 0, sizeof(g_reg_rx_slots[i]));
        g_reg_rx_slots[i].index = i;
        g_reg_rx_slots[i].buffer = g_reg_rx_buffers[i];
    }
    for (i = 0; i < REG_TX_SLOT_COUNT; ++i) {
        memset(&g_reg_tx_slots[i], 0, sizeof(g_reg_tx_slots[i]));
        g_reg_tx_slots[i].index = i;
        g_reg_tx_slots[i].buffer = g_reg_tx_buffers[i];
    }
    g_reg_async_active = 1;
    for (i = 0; i < REG_RX_SLOT_COUNT; ++i) {
        if (reg_rx_submit_slot(&g_reg_rx_slots[i]) < 0) {
            g_reg_async_active = 0;
            return -2;
        }
    }
    SignalSema(g_reg_rx_sema);
    for (wait = 0; wait < 200; ++wait) {
        if (reg_rx_armed_count() == REG_RX_SLOT_COUNT) {
            g_reg_rx_pool_ready = 1;
            return 0;
        }
        DelayThread(1000);
    }
    g_reg_async_active = 0;
    return -3;
}

static int async_reg_out_submit(const u8 *wire, int wire_len, u16 seq)
{
    int i, spin, rc;
    TitanRegTxSlot *slot = NULL;
    if (!wire || wire_len <= 0 || wire_len > REG_IO_BUF_SIZE || !g_reg_async_active) return -1;
    for (spin = 0; spin < 10000 && !slot; ++spin) {
        for (i = 0; i < REG_TX_SLOT_COUNT; ++i) {
            if (!g_reg_tx_slots[i].busy) { slot = &g_reg_tx_slots[i]; break; }
        }
        if (!slot) DelayThread(100);
    }
    if (!slot) return -2;
    memcpy(slot->buffer, wire, wire_len);
    slot->seq = seq;
    slot->result = 0;
    slot->count = 0;
    slot->busy = 1;
    rc = UsbBulkTransfer(g_int_out_pipe, slot->buffer, wire_len, reg_out_callback, slot);
    if (rc != USB_RC_OK) {
        slot->busy = 0;
        g_reg_out_error_seq = seq;
        return -3;
    }
    return 0;
}

static unsigned int wmi_timeout_alarm(void *arg)
{
    (void)arg;
    iSignalSema(g_wmi_rsp_sema);
    return 0;
}

static void wmi_prepare_response(u16 cmd, u16 seq)
{
    drain_sema(g_wmi_rsp_sema);
    g_wmi_mailbox.ready = 0;
    g_wmi_mailbox.cmd = 0;
    g_wmi_mailbox.seq = 0;
    g_wmi_mailbox.payload_len = 0;
    g_wmi_mailbox.expected_cmd = cmd;
    g_wmi_mailbox.expected_seq = seq;
    g_wmi_mailbox.active = 1;
    if (g_reg_out_error_seq == seq) g_reg_out_error_seq = 0;
}

static int wmi_wait_response(u16 seq, unsigned int timeout_us,
                             u16 *rsp_cmd, void *response, int response_cap,
                             int *response_len)
{
    iop_sys_clock_t timeout;
    int n;
    if(g_postkey_bounded_wmi_wait){
        unsigned int waited=0;
        while(waited < timeout_us){
            volatile TitanWmiResponseMailbox *mb=&g_wmi_mailbox;
            if(g_reg_out_error_seq==seq)break;
            if(mb->ready && mb->seq==seq)break;
            DelayThread(1000);
            waited += 1000;
        }
    }else{
        timeout.lo = timeout_us;
        timeout.hi = 0;
        if (SetAlarm(&timeout, wmi_timeout_alarm, NULL) == 0) {
            WaitSema(g_wmi_rsp_sema);
            CancelAlarm(wmi_timeout_alarm, NULL);
        } else {
            WaitSema(g_wmi_rsp_sema);
        }
    }
    if (g_reg_out_error_seq == seq) {
        g_wmi_mailbox.active = 0;
        return -1;
    }
    if (!g_wmi_mailbox.ready || g_wmi_mailbox.seq != seq) {
        g_wmi_mailbox.active = 0;
        return -2;
    }
    if (rsp_cmd) *rsp_cmd = g_wmi_mailbox.cmd;
    n = g_wmi_mailbox.payload_len;
    if (response_len) *response_len = n;
    if (response && response_cap > 0 && n > 0) {
        if (n > response_cap) n = response_cap;
        memcpy(response, g_wmi_mailbox.payload, n);
    }
    return 0;
}

/* ---------------- HTC / WMI ------------------------------------------- */
static int parse_htc_ready(void)
{
    unsigned int count = g_ctrl_count > 0 ? (unsigned int)g_ctrl_count : 0;
    unsigned int off = htc_offset(g_reg_rx, count);
    const u8 *p;
    if (count < off + 16) return -1;
    p = g_reg_rx + off;
    if (p[0] != 0 || rd_be16(p + 8) != HTC_MSG_READY_ID) return -2;
    g_info.htc_credits = rd_be16(p + 10);
    g_info.htc_credit_size = rd_be16(p + 12);
    g_info.htc_max_endpoints = p[14];
    if (g_info.htc_credits == 0) g_info.htc_credits = HTC_CREDITS_AR9271;
    return 0;
}

static int connect_service(u16 service_id, u8 *endpoint_out)
{
    int attempt, rc;
    u8 dl_pipe = (service_id == 0x0100) ? 3 : 2;
    u8 ul_pipe = (service_id == 0x0100) ? 4 : 1;

    memset(g_reg_tx, 0, sizeof(g_reg_tx));
    g_reg_tx[0] = 0;                     /* HTC endpoint 0 */
    wr_be16(g_reg_tx + 2, 10);           /* control payload size */
    wr_be16(g_reg_tx + 8, HTC_MSG_CONNECT_SERVICE_ID);
    wr_be16(g_reg_tx + 10, service_id);
    wr_be16(g_reg_tx + 12, 0);
    g_reg_tx[14] = dl_pipe;
    g_reg_tx[15] = ul_pipe;

    if (sync_reg_out(g_reg_tx, 18, 2000000) < 0) return -1;
    for (attempt = 0; attempt < 8; ++attempt) {
        unsigned int count, off;
        const u8 *p, *q;
        rc = sync_reg_in(g_reg_rx, sizeof(g_reg_rx), 2000000);
        if (rc == -2) continue;
        if (rc < 0) return -2;
        count = g_ctrl_count > 0 ? (unsigned int)g_ctrl_count : 0;
        off = htc_offset(g_reg_rx, count);
        if (count < off + 18) continue;
        p = g_reg_rx + off;
        if (p[0] != 0) continue;
        q = p + 8;
        if (rd_be16(q) != HTC_MSG_CONNECT_SERVICE_RESPONSE_ID || rd_be16(q + 2) != service_id)
            continue;
        if (q[4] != HTC_SERVICE_SUCCESS || q[5] == 0 || q[5] >= g_info.htc_max_endpoints)
            return -3;
        *endpoint_out = q[5];
        return 0;
    }
    return -4;
}

static int config_pipe_credits(void)
{
    int attempt;
    memset(g_reg_tx, 0, sizeof(g_reg_tx));
    g_reg_tx[0] = 0;
    wr_be16(g_reg_tx + 2, 4);
    wr_be16(g_reg_tx + 8, HTC_MSG_CONFIG_PIPE_ID);
    g_reg_tx[10] = USB_WLAN_TX_PIPE;
    g_reg_tx[11] = (u8)g_info.htc_credits;
    if (sync_reg_out(g_reg_tx, 12, 2000000) < 0) return -1;
    for (attempt = 0; attempt < 6; ++attempt) {
        unsigned int count, off;
        const u8 *p;
        if (sync_reg_in(g_reg_rx, sizeof(g_reg_rx), 2000000) < 0) continue;
        count = g_ctrl_count > 0 ? (unsigned int)g_ctrl_count : 0;
        off = htc_offset(g_reg_rx, count);
        if (count < off + 10) continue;
        p = g_reg_rx + off;
        if (p[0] == 0 && rd_be16(p + 8) == HTC_MSG_CONFIG_PIPE_RESPONSE_ID)
            return 0;
    }
    return -2;
}

static int setup_complete(void)
{
    memset(g_reg_tx, 0, sizeof(g_reg_tx));
    g_reg_tx[0] = 0;
    wr_be16(g_reg_tx + 2, 2);
    wr_be16(g_reg_tx + 8, HTC_MSG_SETUP_COMPLETE_ID);
    return sync_reg_out(g_reg_tx, 10, 2000000);
}

#define TITAN_KEYCACHE_REG_BASE 0x00008800u
#define TITAN_KEYCACHE_REG_END  0x00009800u

static int fr_is_keycache_reg(u32 addr)
{
    return addr>=TITAN_KEYCACHE_REG_BASE && addr<TITAN_KEYCACHE_REG_END;
}

/* TT1424 recorder privacy: preserve WMI command/address shape but zero values
 * written into the AR9271 key cache. This keeps PTK/GTK out of TXT/BIN while
 * retaining enough telemetry to diagnose whether key programming happened. */
static const u8 *fr_wmi_tx_view(u16 cmd_id,const u8 *wire,int wire_len,u16 *flags)
{
    int o,redacted=0;
    if(flags)*flags=0;
    if(!wire||wire_len<=0||wire_len>REG_IO_BUF_SIZE)return wire;
    if(cmd_id!=WMI_REG_WRITE_CMDID && cmd_id!=WMI_REG_RMW_CMDID)return wire;
    memcpy(g_fr_wmi_redacted,wire,wire_len);
    if(cmd_id==WMI_REG_WRITE_CMDID){
        for(o=12;o+8<=wire_len;o+=8){
            if(fr_is_keycache_reg(rd_be32(g_fr_wmi_redacted+o))){
                wr_be32(g_fr_wmi_redacted+o+4,0);redacted=1;
            }
        }
    }else{
        for(o=12;o+12<=wire_len;o+=12){
            if(fr_is_keycache_reg(rd_be32(g_fr_wmi_redacted+o))){
                wr_be32(g_fr_wmi_redacted+o+4,0);
                wr_be32(g_fr_wmi_redacted+o+8,0);redacted=1;
            }
        }
    }
    if(redacted && flags)*flags|=TITAN_FR_F_SECRET_REDACTED;
    return redacted?g_fr_wmi_redacted:wire;
}

static int issue_wmi(u16 cmd_id, const void *payload, int payload_len,
                     void *response, int response_cap, int *response_len)
{
    u16 seq, rsp_cmd = 0;
    int tx_len, rc, got=0; u32 t0,dt;

    if (payload_len < 0 || payload_len + 12 > (int)sizeof(g_reg_tx)) return -1;
    if (!g_reg_async_active || !g_reg_rx_pool_ready) return -2;
    seq = ++g_wmi_seq;
    memset(g_reg_tx, 0, sizeof(g_reg_tx));
    g_reg_tx[0] = g_wmi_endpoint;
    wr_be16(g_reg_tx + 2, (u16)(payload_len + 4));
    wr_be16(g_reg_tx + 8, cmd_id);
    wr_be16(g_reg_tx + 10, seq);
    if (payload_len && payload) memcpy(g_reg_tx + 12, payload, payload_len);
    tx_len = payload_len + 12;
    {
        u16 fr_flags=0;
        const u8 *fr_blob=fr_wmi_tx_view(cmd_id,g_reg_tx,tx_len,&fr_flags);
        titan_fr_record(TITAN_FR_EV_WMI_TX,fr_flags,0,(u32)cmd_id,(u32)seq,(u32)payload_len,(u32)tx_len,
                        fr_blob,(u16)tx_len,0);
    }
    t0=trace_clock_us32();
    wmi_prepare_response(cmd_id, seq);
    if (async_reg_out_submit(g_reg_tx, tx_len, seq) < 0) {
        g_wmi_mailbox.active = 0;
        titan_fr_record(TITAN_FR_EV_WMI_ERR,0,-3,(u32)cmd_id,(u32)seq,0,0,NULL,0,0);
        return -3;
    }
    rc = wmi_wait_response(seq, 3000000, &rsp_cmd, response, response_cap, &got);
    dt=trace_clock_us32()-t0;
    if(response_len)*response_len=got;
    if (rc < 0){
        titan_fr_record(TITAN_FR_EV_WMI_ERR,0,-4,(u32)cmd_id,(u32)seq,(u32)dt,(u32)got,NULL,0,0);
        return -4;
    }
    titan_fr_record(TITAN_FR_EV_WMI_RX,0,0,(u32)rsp_cmd,(u32)seq,(u32)got,(u32)dt,
                    got>0?g_wmi_mailbox.payload:NULL,(u16)(got>0?got:0),0);
    return 0;
}

/* Send one capture-derived WMI packet exactly as Linux submitted it on EP04.
 * Unlike issue_wmi(), this function does not construct or renumber anything:
 * endpoint, HTC length, command id, sequence id and payload are all compared
 * to the cold-start trace and transmitted byte-for-byte. */

/* V10.14.3 PR1413 — preserved bounded POSTVIF-168 phase from PV1412.
 *
 * Two independent successful Linux captures, the historical cold-start trace
 * embedded in this tester and the fresh 2026-08-14 usbmon control, contain the
 * same 168 WMI commands immediately after TARGET_IC_UPDATE/VAP_CREATE/
 * NODE_CREATE and before DISABLE_INTR/DRAIN_TXQ_ALL/STOP_RECV.  After zeroing
 * only the WMI sequence bytes, the complete 3632-byte command stream is
 * byte-for-byte identical (SHA-256 ec0d002a7f274ba2353e5866e8b0fcdec15820a20c59cdfe5cb0a493c0f82174).
 *
 * UL1411 reproduced only six of those 168 commands and then inserted seven
 * diagnostic REG_READs before the reset.  This routine is deliberately a
 * bounded diagnostic experiment, not a return to the V10.12 static replay:
 *
 *   - only original Linux WMI seq 2962..3129 is selected;
 *   - issue_wmi() assigns the CURRENT live sequence numbers;
 *   - every REG_READ response comes from the current AR9271 and is never
 *     replaced by capture data;
 *   - response SHAPE is checked, but dynamic read VALUES are not compared;
 *   - direct read->restore dependencies are rebound to the current live read
 *     result rather than replaying a captured saved value;
 *   - the two station-MAC writes are rebuilt from g_radio.macaddr, with
 *     STA_ID1 preserving the high bits from the immediately preceding LIVE
 *     read;
 *   - no captured inter-command timing is forced.
 *
 * If this changes ASSOC 18 -> 0, the omitted post-VIF hardware lifecycle is
 * causal.  If it does not, the experiment still removes a proven structural
 * mismatch without conflating it with association-IE changes.
 */
#define TITAN_POSTVIF168_FIRST_ORIG_SEQ TITAN_POSTVIF168_RUNTIME_FIRST_SEQ
#define TITAN_POSTVIF168_LAST_ORIG_SEQ  TITAN_POSTVIF168_RUNTIME_LAST_SEQ
#define TITAN_POSTVIF168_COUNT          TITAN_POSTVIF168_RUNTIME_COUNT

static int run_linux_postvif_168_live(void)
{
    unsigned int i, count=0;
    u16 expected_orig=TITAN_POSTVIF168_FIRST_ORIG_SEQ;
    u16 live_first=(u16)(g_wmi_seq+1u);
    u32 live_sta_id1=0;
    int have_sta_id1=0;
    /* Live dependency cache.  The successful trace response is used only as
     * metadata to recognize an outgoing value that Linux copied from an
     * earlier register read.  The value actually written is then replaced by
     * this run's live read result.  No captured read value is fed to hardware. */
    u32 dep_addr[64],dep_hist[64],dep_live[64];
    unsigned int dep_n=0;
    u8 rsp[WMI_RSP_PAYLOAD_MAX];
    u8 patched[REG_IO_BUF_SIZE];

    titan_fr_mark(0x50313638u,TITAN_POSTVIF168_FIRST_ORIG_SEQ,
                  TITAN_POSTVIF168_LAST_ORIG_SEQ,(u32)g_wmi_seq); /* 'P168' */

    for(i=0;i<TITAN_POSTVIF168_RUNTIME_COUNT;i++){
        const TitanPostvif168RuntimeEvent *ev=&titan_postvif168_runtime_events[i];
        const u8 *wire,*send;
        u16 cmd;
        int len,rsp_len=0,rc,expected_rsp;

        if(ev->original_seq!=expected_orig || ev->blob_off+ev->length>titan_postvif168_runtime_blob_size ||
           ev->length<12 || ev->length>(u16)sizeof(patched)){
            titan_fr_mark(0x50313646u,(u32)ev->original_seq,0u,0xfffffff1u); /* 'P16F' */
            return -1;
        }

        wire=titan_postvif168_runtime_blob+ev->blob_off;
        send=wire;
        len=(int)ev->length;
        cmd=rd_be16(wire+8);
        if(ev->command_id!=cmd ||
           (cmd!=WMI_REG_READ_CMDID && cmd!=WMI_REG_WRITE_CMDID &&
            cmd!=WMI_REG_RMW_CMDID)){
            titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,0xfffffff2u);
            return -2;
        }

        /* Rebind directly observed save/restore dependencies to the live
         * hardware state.  In this 168-command block every REG_READ returns
         * one 32-bit register.  If a later captured WRITE value or RMW set
         * value is exactly the historical value from the most recent read of
         * that same address, Linux was restoring/propagating that observation:
         * substitute the CURRENT live value instead. */
        if(cmd==WMI_REG_WRITE_CMDID || cmd==WMI_REG_RMW_CMDID){
            int o;
            memcpy(patched,wire,(size_t)len);
            send=patched;
            if(cmd==WMI_REG_WRITE_CMDID){
                for(o=12;o+8<=len;o+=8){
                    u32 a=rd_be32(patched+o),v=rd_be32(patched+o+4);
                    int j;
                    for(j=(int)dep_n-1;j>=0;--j){
                        if(dep_addr[j]==a){
                            if(v==dep_hist[j])wr_be32(patched+o+4,dep_live[j]);
                            break;
                        }
                    }
                }
            }else{
                for(o=12;o+12<=len;o+=12){
                    u32 a=rd_be32(patched+o),sv=rd_be32(patched+o+4);
                    int j;
                    for(j=(int)dep_n-1;j>=0;--j){
                        if(dep_addr[j]==a){
                            if(sv==dep_hist[j])wr_be32(patched+o+4,dep_live[j]);
                            break;
                        }
                    }
                }
            }
        }

        /* The stable Linux block was captured with the same PS2 BIOS MAC, but
         * keep the phase correct for the live radio identity rather than
         * baking those two words into the diagnostic architecture. */
        if(ev->original_seq==2963u){
            u32 lo=(u32)g_radio.macaddr[0] | ((u32)g_radio.macaddr[1]<<8) |
                   ((u32)g_radio.macaddr[2]<<16) | ((u32)g_radio.macaddr[3]<<24);
            if(send==wire){memcpy(patched,wire,(size_t)len);send=patched;}
            wr_be32(patched+16,lo);
        }else if(ev->original_seq==2965u){
            u32 id1;
            if(!have_sta_id1){
                titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,0xfffffff5u);
                return -5;
            }
            id1=(live_sta_id1&0xffff0000u) |
                (u32)g_radio.macaddr[4] | ((u32)g_radio.macaddr[5]<<8);
            if(send==wire){memcpy(patched,wire,(size_t)len);send=patched;}
            wr_be32(patched+16,id1);
        }

        rc=issue_wmi(cmd,send+12,len-12,rsp,sizeof(rsp),&rsp_len);
        if(rc<0){
            titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,(u32)rc);
            return -3;
        }

        expected_rsp=(int)ev->response_len;
        if((u32)ev->response_off+(u32)ev->response_len >
           (u32)titan_postvif168_runtime_response_blob_size){
            titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,0xfffffff8u);
            return -8;
        }
        if(rsp_len!=expected_rsp){
            titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,
                          ((u32)(expected_rsp&0xffff)<<16)|(u32)(rsp_len&0xffff));
            return -4;
        }

        if(cmd==WMI_REG_READ_CMDID && rsp_len==4 && len>=16){
            const u8 *hist_rsp=titan_postvif168_runtime_response_blob+ev->response_off;
            u32 a=rd_be32(send+12),hv=rd_be32(hist_rsp),lv=rd_be32(rsp);
            unsigned int j;
            for(j=0;j<dep_n;j++)if(dep_addr[j]==a)break;
            if(j==dep_n && dep_n<64u)++dep_n;
            if(j<64u){dep_addr[j]=a;dep_hist[j]=hv;dep_live[j]=lv;}
        }

        if(ev->original_seq==2964u){
            if(cmd!=WMI_REG_READ_CMDID || rsp_len<4){
                titan_fr_mark(0x50313646u,(u32)ev->original_seq,(u32)cmd,0xfffffff6u);
                return -6;
            }
            live_sta_id1=rd_be32(rsp);
            have_sta_id1=1;
        }

        ++count;
        ++expected_orig;
    }

    if(count!=TITAN_POSTVIF168_COUNT ||
       expected_orig!=(u16)(TITAN_POSTVIF168_LAST_ORIG_SEQ+1u)){
        titan_fr_mark(0x50313646u,(u32)count,(u32)expected_orig,0xfffffff7u);
        return -7;
    }

    titan_fr_mark(0x50313645u,(u32)count,(u32)live_first,(u32)g_wmi_seq); /* 'P16E' */
    return 0;
}

#if 0 /* Standalone-only full Linux lockstep path removed from Titan runtime. */
static int issue_wmi_wire_exact(const u8 *wire, int wire_len)
{
    u16 cmd_id, seq, rsp_cmd = 0;
    u8 rsp[WMI_RSP_PAYLOAD_MAX];
    int wlen = 0, rc;
    if(!wire || wire_len < 12 || wire_len > (int)sizeof(g_reg_tx)) return -1;
    if(!g_reg_async_active || !g_reg_rx_pool_ready) return -2;
    if(wire[0] != g_wmi_endpoint) return -3;
    if(rd_be16(wire + 2) != (u16)(wire_len - 8)) return -4;
    cmd_id = rd_be16(wire + 8);
    seq = rd_be16(wire + 10);
    if(seq != (u16)(g_wmi_seq + 1u)) return -5;

    {
        u16 fr_flags=1;
        const u8 *fr_blob=fr_wmi_tx_view(cmd_id,wire,wire_len,&fr_flags);
        titan_fr_record(TITAN_FR_EV_WMI_TX,fr_flags,0,(u32)cmd_id,(u32)seq,(u32)(wire_len-12),(u32)wire_len,
                        fr_blob,(u16)wire_len,0);
    }
    wmi_prepare_response(cmd_id, seq);
    if(async_reg_out_submit(wire, wire_len, seq) < 0){
        g_wmi_mailbox.active = 0;
        return -6;
    }
    g_wmi_seq = seq;
    g_info.scan_round = seq;
    {u32 frt0=trace_clock_us32();
    rc = wmi_wait_response(seq, 3000000, &rsp_cmd, rsp, sizeof(rsp), &wlen);
    if(rc < 0){titan_fr_record(TITAN_FR_EV_WMI_ERR,1,-7,(u32)cmd_id,(u32)seq,0,0,NULL,0,0);return -7;}
    titan_fr_record(TITAN_FR_EV_WMI_RX,1,0,(u32)rsp_cmd,(u32)seq,(u32)wlen,trace_clock_us32()-frt0,
                    rsp,(u16)(wlen>0?wlen:0),0);}
    /* The Linux cold-start reference has one response for every pre-assoc WMI
     * command. Keep sequence-ID completion semantics and strict response shape.
     * Payload bytes are strict only when they represent deterministic command
     * data; WMI_REG_READ values are live hardware observations and are handled
     * below as non-fatal drift. This never changes target-facing bytes. */
    if(rsp_cmd != cmd_id){g_lockstep_bad_seq=seq;g_lockstep_bad_kind=1;return -8;}
    if(seq <= TITAN_LINUX_TRACE_LAST_WMI_SEQ){
        const u8 *expected;
        int expected_len=(int)titan_linux_trace_rsp_len[seq];
        if(wlen != expected_len){g_lockstep_bad_seq=seq;g_lockstep_bad_kind=2;return -9;}
        expected=titan_linux_trace_rsp_blob+titan_linux_trace_rsp_off[seq];
        if(expected_len>0 && memcmp(rsp,expected,expected_len)!=0){
            /* Linux uses WMI_REG_READ as a transport for live register reads.
             * Polling registers such as AR_RTC_STATUS are explicitly expected
             * to change while ath9k waits for a masked condition.  A capture
             * records one particular set of intermediate samples (for example
             * 0x11 -> 0x18 -> ... -> 0x02 at startup), but a real cold start
             * is allowed to advance faster/slower.  Treat those payload bytes
             * as observations while still enforcing command id, sequence and
             * response length.  Non-read payloads (notably FW version) remain
             * strict. */
            if(cmd_id==WMI_REG_READ_CMDID){
                int j,words=expected_len/4;
                if(!g_lockstep_read_first_seq)g_lockstep_read_first_seq=seq;
                if(g_lockstep_read_drift_count!=0xffffu)++g_lockstep_read_drift_count;
                /* Map the first differing register word, not merely the first
                 * command. Multi-register WMI_REG_READ packets are common in
                 * the reset/calibration trace, so command-level drift alone
                 * cannot tell which live register diverged. */
                for(j=0;j<words && 12+4*j+4<=wire_len;j++){
                    if(memcmp(rsp+4*j,expected+4*j,4)!=0){
                        if(g_lockstep_read_drift_words!=0xffffu)++g_lockstep_read_drift_words;
                        if(!g_lockstep_read_first_word_seq){
                            g_lockstep_read_first_word_seq=seq;
                            g_lockstep_read_first_addr=rd_be32(wire+12+4*j);
                            g_lockstep_read_first_expected=rd_be32(expected+4*j);
                            g_lockstep_read_first_actual=rd_be32(rsp+4*j);
                        }
                    }
                }
                /* The legacy radio-diag fields are not otherwise rendered by
                 * the lockstep path. Reuse them as a fixed-size 1024-byte RPC
                 * diagnostic map without changing the ABI. */
                g_info.rtc_status=g_lockstep_read_drift_count;
                g_info.phy_active=g_lockstep_read_drift_words;
                g_info.mac_cr=g_lockstep_read_first_word_seq;
                g_info.diag_sw=g_lockstep_read_first_addr;
                g_info.synth_control=g_lockstep_read_first_expected;
                g_info.agc_control=g_lockstep_read_first_actual;
            }else{
                g_lockstep_bad_seq=seq;g_lockstep_bad_kind=3;return -10;
            }
        }
    }
    /* Linux queries the version at trace seq 317. Preserve telemetry without
     * adding a second command that would shift the trace. */
    if(cmd_id==WMI_GET_FW_VERSION_CMDID && wlen>=4){
        g_info.fw_major=rd_be16(rsp);
        g_info.fw_minor=rd_be16(rsp+2);
    }
    return 0;
}

#endif

static int wait_wmi_tx_status(u8 cookie, u8 expected_ep, unsigned int timeout_us)
{
    unsigned int elapsed = 0;
    const unsigned int slice = 1000;
    if (g_tx_status[cookie] && g_tx_status_ep[cookie] == expected_ep)
        return g_tx_status[cookie] == 2 ? 1 : -1;
    while (elapsed < timeout_us && g_dev_id >= 0 && g_reg_async_active) {
        DelayThread(slice);
        elapsed += slice;
        if (g_tx_status[cookie] && g_tx_status_ep[cookie] == expected_ep)
            return g_tx_status[cookie] == 2 ? 1 : -1;
    }
    ++g_info.txstatus_timeouts;
    g_info.tx_last_cookie = cookie;
    g_info.tx_last_status = 0;
    return 0; /* unknown is telemetry, not a hard failure */
}

static int radio_wmi_cmd(void *ctx, u16 cmd_id, const void *payload, int payload_len,
                         void *response, int response_cap, int *response_len)
{
    (void)ctx;
    return issue_wmi(cmd_id, payload, payload_len, response, response_cap, response_len);
}

static void update_radio_diag(void)
{
    TitanAr9271RadioDiag d;
    if (titan_ar9271_radio_diag(&g_radio, &d) < 0) return;
    g_info.rtc_status = d.rtc_status;
    g_info.phy_active = d.phy_active;
    g_info.mac_cr = d.mac_cr;
    g_info.rx_filter = d.rx_filter;
    g_info.diag_sw = d.diag_sw;
    g_info.synth_control = d.synth_control;
    g_info.agc_control = d.agc_control;
    titan_fr_record(TITAN_FR_EV_RADIO_DIAG,0,0,d.rtc_status,d.phy_active,d.mac_cr,d.rx_filter,
                    &d,(u16)sizeof(d),0);

    /* V10.13 keeps the 1024-byte EE/IOP ABI unchanged.  These legacy target
     * RX counters are no longer used by the active trace path, so publish the
     * decisive live-driver telemetry through them for this standalone lab. */
    g_info.tgt_rx_nobuf = g_radio.txpower_applies;
    g_info.tgt_rx_host_send = ((u32)g_radio.txpower_limit << 16) | (u32)g_radio.txpower_max;
    g_info.tgt_rx_host_done = g_target_live_bss;
}

/* ---------------- bulk RX + beacon/probe parser ----------------------- */
static void rx_callback(int result, int count, void *arg)
{
    TitanRxSlot *slot = (TitanRxSlot *)arg;
    if (!slot) return;
    slot->result = result;
    slot->count = count;
    slot->armed = 0;
    slot->done = 1;
    ++slot->completions;
    ++g_info.rx_completions;
    g_info.rx_last_result = result;
    g_info.rx_last_count = count;
    if (count == 0) { ++slot->zero_completions; ++g_info.rx_zero_completions; }
    /* PS2SDK USB callbacks run in a context where its own usbmass driver uses
       SignalSema(), not iSignalSema(). Using the interrupt-context variant here
       left completed RX slots queued but never drained on the real console. */
    SignalSema(g_rx_sema);
}

static int bytes_equal(const u8 *a, const u8 *b, int len)
{
    int i;
    if (!a || !b || len < 0) return 0;
    for (i = 0; i < len; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int bssid_equal(const u8 *a, const u8 *b)
{
    return bytes_equal(a, b, 6);
}


static int has_wpa3_akm(const u8 *rsn, int len)
{
    int pos, pair_count, akm_count, i;
    if (len < 8) return 0;
    pos = 2 + 4;
    if (pos + 2 > len) return 0;
    pair_count = (int)rsn[pos] | ((int)rsn[pos + 1] << 8);
    pos += 2 + pair_count * 4;
    if (pos + 2 > len) return 0;
    akm_count = (int)rsn[pos] | ((int)rsn[pos + 1] << 8);
    pos += 2;
    for (i = 0; i < akm_count && pos + 4 <= len; ++i, pos += 4) {
        if (rsn[pos] == 0x00 && rsn[pos+1] == 0x0f && rsn[pos+2] == 0xac) {
            u8 type = rsn[pos+3];
            if (type == 8 || type == 9 || type == 12 || type == 18) return 1;
        }
    }
    return 0;
}

/* Analyze the RSN element in the same shape that a supplicant uses when
 * selecting a BSS: keep group, pairwise, AKM and capabilities separate.
 * The first-online profile remains deliberately narrow (CCMP pairwise,
 * CCMP or TKIP group, PSK, no MFPR),
 * but rejection is now observable instead of being silently reported as
 * "searching" forever.
 *
 * rsn points at the RSN body (version first), not the EID/length bytes.
 */
#define RSN_REJ_MALFORMED   0x01u
#define RSN_REJ_GROUP       0x02u
#define RSN_REJ_PAIRWISE    0x04u
#define RSN_REJ_AKM         0x08u
#define RSN_REJ_MFPR        0x10u
#define RSN_PAIR_CCMP       0x01u
#define RSN_PAIR_TKIP       0x02u
#define RSN_PAIR_GCMP       0x04u
#define RSN_PAIR_CCMP256    0x08u
#define RSN_AKM_PSK         0x01u
#define RSN_AKM_SAE         0x02u
#define RSN_AKM_PSK_SHA256  0x04u

static int rsn_analyze(const u8 *rsn, int len, u8 *group_type,
                       u8 *pair_flags, u8 *akm_flags, u16 *caps_out,
                       u8 *reject_out)
{
    int pos, pair_count, akm_count, i;
    u8 group=0, pair=0, akm=0, reject=0;
    u16 caps=0;
    if (!rsn || len < 8 || rd_le16(rsn) != 1) {
        reject=RSN_REJ_MALFORMED; goto out;
    }
    pos=2;
    if(pos+4>len){reject=RSN_REJ_MALFORMED;goto out;}
    if(rsn[pos]==0x00 && rsn[pos+1]==0x0f && rsn[pos+2]==0xac) group=rsn[pos+3];
    else { reject=RSN_REJ_MALFORMED; goto out; }
    pos+=4;
    if(pos+2>len){reject=RSN_REJ_MALFORMED;goto out;}
    pair_count=rd_le16(rsn+pos);pos+=2;
    if(pair_count<=0 || pair_count>32){reject=RSN_REJ_MALFORMED;goto out;}
    for(i=0;i<pair_count;i++){
        u8 t;
        if(pos+4>len){reject=RSN_REJ_MALFORMED;goto out;}
        if(rsn[pos]==0x00 && rsn[pos+1]==0x0f && rsn[pos+2]==0xac){
            t=rsn[pos+3];
            if(t==4)pair|=RSN_PAIR_CCMP;
            else if(t==2)pair|=RSN_PAIR_TKIP;
            else if(t==8)pair|=RSN_PAIR_GCMP;
            else if(t==10)pair|=RSN_PAIR_CCMP256;
        }
        pos+=4;
    }
    if(pos+2>len){reject=RSN_REJ_MALFORMED;goto out;}
    akm_count=rd_le16(rsn+pos);pos+=2;
    if(akm_count<=0 || akm_count>32){reject=RSN_REJ_MALFORMED;goto out;}
    for(i=0;i<akm_count;i++){
        u8 t;
        if(pos+4>len){reject=RSN_REJ_MALFORMED;goto out;}
        if(rsn[pos]==0x00 && rsn[pos+1]==0x0f && rsn[pos+2]==0xac){
            t=rsn[pos+3];
            if(t==2)akm|=RSN_AKM_PSK;
            else if(t==8)akm|=RSN_AKM_SAE;
            else if(t==6)akm|=RSN_AKM_PSK_SHA256;
        }
        pos+=4;
    }
    if(pos+2<=len)caps=rd_le16(rsn+pos);
    if(group!=4&&group!=2)reject|=RSN_REJ_GROUP;
    if(!(pair&RSN_PAIR_CCMP))reject|=RSN_REJ_PAIRWISE;
    if(!(akm&RSN_AKM_PSK))reject|=RSN_REJ_AKM;
    if(caps&0x0040u)reject|=RSN_REJ_MFPR;
out:
    if(group_type)*group_type=group;
    if(pair_flags)*pair_flags=pair;
    if(akm_flags)*akm_flags=akm;
    if(caps_out)*caps_out=caps;
    if(reject_out)*reject_out=reject;
    return reject==0;
}


static void sort_aps(void)
{
    int i, j;
    for (i = 0; i < (int)g_info.ap_count; ++i) {
        for (j = i + 1; j < (int)g_info.ap_count; ++j) {
            if (g_info.aps[j].rssi_dbm > g_info.aps[i].rssi_dbm) {
                TitanWifiAp t = g_info.aps[i];
                g_info.aps[i] = g_info.aps[j];
                g_info.aps[j] = t;
            }
        }
    }
}

static void upsert_ap(const u8 *bssid, const char *ssid, int ssid_len,
                      int channel, int rssi_dbm, int security)
{
    int i, slot = -1, weakest = -1;
    for (i = 0; i < (int)g_info.ap_count; ++i) {
        if (bssid_equal(g_info.aps[i].bssid, bssid)) { slot = i; break; }
        if (weakest < 0 || g_info.aps[i].rssi_dbm < g_info.aps[weakest].rssi_dbm) weakest = i;
    }
    if (slot < 0) {
        if (g_info.ap_count < TITAN_AR9271_MAX_APS) slot = g_info.ap_count++;
        else if (weakest >= 0 && rssi_dbm > g_info.aps[weakest].rssi_dbm) slot = weakest;
        else return;
        memset(&g_info.aps[slot], 0, sizeof(g_info.aps[slot]));
        memcpy(g_info.aps[slot].bssid, bssid, 6);
    }
    if (ssid_len > 0) {
        int n = ssid_len > 32 ? 32 : ssid_len;
        for (i = 0; i < n; ++i) {
            unsigned char c = (unsigned char)ssid[i];
            g_info.aps[slot].ssid[i] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        g_info.aps[slot].ssid[n] = 0;
    }
    g_info.aps[slot].channel = (u8)channel;
    g_info.aps[slot].rssi_dbm = (s8)rssi_dbm;
    g_info.aps[slot].security = (u8)security;
    if (g_info.aps[slot].seen_count != 0xff) ++g_info.aps[slot].seen_count;
    sort_aps();
}

static int ssid_is_target(const char *ssid, int len)
{
    int i;
    if (!g_wifi_configured || len != (int)g_wifi_ssid_len) return 0;
    for (i=0;i<len;i++) if ((u8)ssid[i] != (u8)g_wifi_ssid[i]) return 0;
    return 1;
}

static void remember_target(const u8 *bssid, int channel, int rssi, u16 cap,
                            const u8 *rates, int nrates,
                            const u8 *supp_rates, int nsupp,
                            const u8 *ext_rates, int next,
                            const u8 *rsn_ie, int rsn_ie_len,
                            const u8 *ht_cap_ie, int ht_cap_len,
                            const u8 *ht_op_ie, int ht_op_len,
                            int ht_present, int wmm_present)
{
    int i;
    if (g_target_locked) return;
    if (g_target.found && rssi <= g_target.rssi_dbm) return;
    memset(&g_target,0,sizeof(g_target));
    memcpy(g_target.bssid,bssid,6);
    g_target.channel=(u8)channel;
    g_target.rssi_dbm=(s8)rssi;
    g_target.capability=cap;g_info.target_capability=cap;
    if(nrates>30)nrates=30;
    for(i=0;i<nrates;i++)g_target.rates[i]=rates[i];
    g_target.rate_count=(u8)nrates;
    if(nsupp<0)nsupp=0;
    if(nsupp>8)nsupp=8;
    if(next<0)next=0;
    if(next>22)next=22;
    if(nsupp)memcpy(g_target.supp_rates,supp_rates,nsupp);
    if(next)memcpy(g_target.ext_rates,ext_rates,next);
    g_target.supp_rate_count=(u8)nsupp;g_target.ext_rate_count=(u8)next;
    g_info.target_rate_count=(u8)nrates;
    g_info.target_supp_rate_count=(u8)nsupp;
    g_info.target_ext_rate_count=(u8)next;
    memset(g_info.target_supp_rates,0,sizeof(g_info.target_supp_rates));
    memset(g_info.target_ext_rates,0,sizeof(g_info.target_ext_rates));
    if(nsupp)memcpy(g_info.target_supp_rates,supp_rates,nsupp);
    if(next)memcpy(g_info.target_ext_rates,ext_rates,next);
    g_info.target_legacy_rate_mask=0;g_info.target_basic_rate_mask=0;
    for(i=0;i<nrates;i++){
        int bit=legacy_rate_index(rates[i]);
        if(bit>=0){
            g_info.target_legacy_rate_mask|=(u16)(1u<<bit);
            if(rates[i]&0x80u)g_info.target_basic_rate_mask|=(u16)(1u<<bit);
        }
    }
    g_info.target_rsn_ie_len=0;
    memset(g_info.target_rsn_preview,0,sizeof(g_info.target_rsn_preview));
    if(rsn_ie&&rsn_ie_len>=2&&rsn_ie_len<=TITAN_WPA2_MAX_RSN_IE){
        int n=rsn_ie_len>(int)sizeof(g_info.target_rsn_preview)?(int)sizeof(g_info.target_rsn_preview):rsn_ie_len;
        memcpy(g_target.rsn_ie,rsn_ie,rsn_ie_len);g_target.rsn_ie_len=(u8)rsn_ie_len;
        g_info.target_rsn_ie_len=(u8)rsn_ie_len;
        memcpy(g_info.target_rsn_preview,rsn_ie,n);
    }
    g_target.ht_present=ht_present?1:0;
    if(ht_cap_ie&&ht_cap_len==26)memcpy(g_target.ht_cap,ht_cap_ie,26);
    if(ht_op_ie&&ht_op_len==22){g_target.ht_op_present=1;memcpy(g_target.ht_op,ht_op_ie,22);}
    g_target.wmm_present=wmm_present?1:0;
    g_info.target_ht_present=g_target.ht_present;
    g_info.target_wmm_present=g_target.wmm_present;
    g_target.found=1;
    titan_fr_record(TITAN_FR_EV_TARGET,0,0,(u32)channel,(u32)(s32)rssi,
                    (u32)g_info.target_legacy_rate_mask,(u32)g_info.target_basic_rate_mask,bssid,6,
                    (u16)((ht_present?1:0)|(wmm_present?2:0)));
    g_info.target_found=1;
    g_info.target_channel=(u8)channel;
    g_info.target_rssi_dbm=(s8)rssi;
    g_info.target_security=TITAN_WIFI_WPA2;
    memcpy(g_info.target_bssid,bssid,6);
    for(i=0;i<(int)g_wifi_ssid_len && i<32;i++)g_info.target_ssid[i]=g_wifi_ssid[i];
    g_info.target_ssid[i]=0;
}

static void parse_beacon_probe(const u8 *f, int len, int rssi_dbm)
{
    u16 cap;
    int pos, ssid_len = 0, channel, security;
    char ssid[33];
    int has_rsn = 0, has_wpa = 0, wpa3 = 0, titan_rsn_ok = 0;
    int has_ht = 0, has_wmm = 0;
    u8 target_ht_cap[26]; int target_ht_cap_len=0;
    u8 target_ht_op[22]; int target_ht_op_len=0;
    u8 rates[30]; int nrates=0;
    u8 supp_rates[8],ext_rates[22]; int nsupp=0,next=0;
    u8 target_rsn[TITAN_WPA2_MAX_RSN_IE]; int target_rsn_len=0;
    u8 rsn_group=0,rsn_pair=0,rsn_akm=0,rsn_reject=RSN_REJ_MALFORMED;
    u16 rsn_caps=0;

    if (len < 36) return;
    ++g_info.beacons_seen;
    if(g_target_live_gate_active && g_target.found && bssid_equal(f+16,g_target.bssid)){
        if(g_target_live_bss!=0xffffffffu)++g_target_live_bss;
    }
    memset(ssid, 0, sizeof(ssid));
    memset(rates,0,sizeof(rates));
    memset(target_ht_cap,0,sizeof(target_ht_cap));memset(target_ht_op,0,sizeof(target_ht_op));
    memset(supp_rates,0,sizeof(supp_rates));memset(ext_rates,0,sizeof(ext_rates));
    channel = g_info.current_channel ? g_info.current_channel : 1;
    cap = rd_le16(f + 34);
    pos = 36;

    while (pos + 2 <= len) {
        int id = f[pos], ilen = f[pos + 1], j;
        const u8 *ie;
        pos += 2;
        if (pos + ilen > len) break;
        ie = f + pos;
        if (id == 0 && ssid_len == 0) {
            ssid_len = ilen > 32 ? 32 : ilen;
            if (ssid_len) memcpy(ssid, ie, ssid_len);
        } else if (id == 1) {
            for(j=0;j<ilen && nrates<30;j++) rates[nrates++]=ie[j];
            for(j=0;j<ilen && nsupp<8;j++) supp_rates[nsupp++]=ie[j];
        } else if (id == 50) {
            for(j=0;j<ilen && nrates<30;j++) rates[nrates++]=ie[j];
            for(j=0;j<ilen && next<22;j++) ext_rates[next++]=ie[j];
        } else if (id == 3 && ilen >= 1) {
            if (ie[0] >= 1 && ie[0] <= 14) channel = ie[0];
        } else if (id == 61 && ilen >= 1) {
            if (ie[0] >= 1 && ie[0] <= 14) channel = ie[0];
            if(ilen==22){memcpy(target_ht_op,ie,22);target_ht_op_len=22;}
        } else if (id == 48) {
            has_rsn = 1;
            titan_rsn_ok = rsn_analyze(ie,ilen,&rsn_group,&rsn_pair,&rsn_akm,&rsn_caps,&rsn_reject);
            if(ilen+2<=TITAN_WPA2_MAX_RSN_IE){
                target_rsn[0]=48;target_rsn[1]=(u8)ilen;memcpy(target_rsn+2,ie,ilen);target_rsn_len=ilen+2;
            }
            if (has_wpa3_akm(ie, ilen)) wpa3 = 1;
        } else if (id == 45 && ilen == 26) {
            has_ht = 1;memcpy(target_ht_cap,ie,26);target_ht_cap_len=26;
        } else if (id == 221 && ilen >= 4 && ie[0] == 0x00 && ie[1] == 0x50 && ie[2] == 0xf2 && ie[3] == 0x02) {
            has_wmm = 1;
        } else if (id == 221 && ilen >= 4 && ie[0] == 0x00 && ie[1] == 0x50 && ie[2] == 0xf2 && ie[3] == 0x01) {
            has_wpa = 1;
        }
        pos += ilen;
    }

    if (wpa3 && !titan_rsn_ok) security = TITAN_WIFI_WPA3;
    else if (has_rsn) security = TITAN_WIFI_WPA2;
    else if (has_wpa) security = TITAN_WIFI_WPA;
    else if (cap & 0x0010) security = TITAN_WIFI_WEP;
    else security = TITAN_WIFI_OPEN;

    upsert_ap(f + 16, ssid, ssid_len, channel, rssi_dbm, security);
    if (ssid_is_target(ssid,ssid_len)) {
        titan_fr_record(TITAN_FR_EV_RX_TARGET_BSS,0,0,(u32)channel,(u32)(s32)rssi_dbm,(u32)cap,(u32)len,
                        f,(u16)(len>768?768:len),0);
        g_info.target_ssid_match=1;
        g_info.target_group_cipher=rsn_group;
        g_info.target_pairwise_flags=rsn_pair;
        g_info.target_akm_flags=rsn_akm;
        g_info.target_rsn_caps=rsn_caps;
        g_info.target_rsn_reject=has_rsn?rsn_reject:RSN_REJ_MALFORMED;
        if(titan_rsn_ok)
            remember_target(f+16,channel,rssi_dbm,cap,rates,nrates,
                            supp_rates,nsupp,ext_rates,next,
                            target_rsn,target_rsn_len,
                            target_ht_cap,target_ht_cap_len,target_ht_op,target_ht_op_len,
                            has_ht,has_wmm);
    }
}

static void queue_eapol(const u8 *p,int len)
{
    int n;
    if(len<=0||g_eapol_lock<0)return;
    n=len>EAPOL_EVENT_MAX?EAPOL_EVENT_MAX:len;
    /* This runs in the RX worker, not the USB completion callback, so a
     * normal semaphore is safe here. Keep the shared EAPOL handoff atomic
     * with respect to the station/WPA worker. */
    WaitSema(g_eapol_lock);
    memcpy(g_eapol_buf,p,n);
    g_eapol_len=n;
    g_eapol_pending=1;
    ++g_info.rx_eapol_frames;
    SignalSema(g_eapol_lock);
}

static void clear_eapol(void)
{
    if(g_eapol_lock<0)return;
    WaitSema(g_eapol_lock);
    g_eapol_pending=0;
    g_eapol_len=0;
    SignalSema(g_eapol_lock);
}

static int take_eapol(u8 *out,int cap)
{
    int n=0;
    if(!out||cap<=0||g_eapol_lock<0)return 0;
    WaitSema(g_eapol_lock);
    if(g_eapol_pending && g_eapol_len>0){
        n=g_eapol_len;
        if(n>cap)n=cap;
        memcpy(out,g_eapol_buf,n);
        g_eapol_pending=0;
        g_eapol_len=0;
    }
    SignalSema(g_eapol_lock);
    return n;
}

static int dot11_data_hdrlen(u16 fc)
{
    int to_ds=(fc&0x0100u)!=0, from_ds=(fc&0x0200u)!=0;
    int subtype=(fc>>4)&0xf, hdr=24;
    if(to_ds&&from_ds)hdr+=6;
    if(subtype&8){
        hdr+=2; /* QoS control */
        if(fc&0x8000u)hdr+=4; /* HT control when Order is set on QoS data */
    }
    return hdr;
}

static int dot11_qos_queue(const u8 *f,int len,u16 fc,int hdr)
{
    int subtype=(fc>>4)&0xf;
    int qos_off;
    if(!(subtype&8))return 16; /* non-QoS bucket */
    qos_off=hdr-2;
    if(fc&0x8000u)qos_off-=4;
    if(qos_off<24||qos_off+2>len)return 16;
    return f[qos_off]&0x0f;
}

static u64 ccmp_pn64(const u8 *c)
{
    return (u64)c[0] | ((u64)c[1]<<8) | ((u64)c[4]<<16) |
           ((u64)c[5]<<24) | ((u64)c[6]<<32) | ((u64)c[7]<<40);
}

static int ccmp_rx_accept_pn(const u8 *c,int queue,int group)
{
    u64 pn,last;
    if(queue<0||queue>16)return 0;
    pn=ccmp_pn64(c);
    if(pn==0)return 0;
    if(group){
        last=g_wpa.gtk_rx_pn[queue];
        if(last && pn<=last)return 0;
        g_wpa.gtk_rx_pn[queue]=pn;
    }else{
        last=g_wpa.pairwise_rx_pn[queue];
        if(last && pn<=last)return 0;
        g_wpa.pairwise_rx_pn[queue]=pn;
    }
    return 1;
}


static u64 tkip_tsc64(const u8 *iv)
{
    /* TKIP IV layout: TSC1, WEPSeed, TSC0, KeyID/ExtIV, TSC2..TSC5. */
    return (u64)iv[2] | ((u64)iv[0]<<8) | ((u64)iv[4]<<16) |
           ((u64)iv[5]<<24) | ((u64)iv[6]<<32) | ((u64)iv[7]<<40);
}

static int tkip_group_rx_check_tsc(const u8 *iv,int queue,u64 *out_tsc)
{
    u64 tsc,last;
    if(queue<0||queue>16||!out_tsc)return 0;
    if(!(iv[3]&0x20u))return 0; /* ExtIV is mandatory for TKIP. */
    tsc=tkip_tsc64(iv);
    if(tsc==0)return 0;
    last=g_wpa.gtk_rx_pn[queue];
    if(last && tsc<=last)return 0;
    *out_tsc=tsc;
    return 1;
}

static void tkip_group_rx_commit_tsc(int queue,u64 tsc)
{
    if(queue>=0&&queue<=16)g_wpa.gtk_rx_pn[queue]=tsc;
}

/* TT1424 DATA-CRYPTO-TRACE RX reasons. Diagnostic only: this telemetry does
 * not alter rs_status, key selection, replay state or network parsing. Raw EAPOL
 * is deliberately not recorded. */
enum TitanRxDataTraceReason {
    TT1424_RXD_HIF_STATUS = 1,
    TT1424_RXD_DIRECTION = 2,
    TT1424_RXD_DEST = 3,
    TT1424_RXD_HDRLEN = 4,
    TT1424_RXD_BOUNDS = 5,
    TT1424_RXD_NOT_SECURED = 6,
    TT1424_RXD_DECRYPT = 7,
    TT1424_RXD_MIC = 8,
    TT1424_RXD_KEYMISS = 9,
    TT1424_RXD_CCMP_SHORT = 10,
    TT1424_RXD_KEYIX = 11,
    TT1424_RXD_PN = 12,
    TT1424_RXD_CRYPTO_TAIL = 13,
    TT1424_RXD_GTK = 14,
    TT1424_RXD_LLC_SHORT = 15,
    TT1424_RXD_LLC = 16,
    TT1424_RXD_EAPOL = 17,
    TT1424_RXD_UNPROTECTED_L3 = 18,
    TT1424_RXD_NET_NONE = 19,
    TT1424_RXD_ACCEPT = 20
};

static void tt1424_trace_rx_data(const u8 *f,int len,u16 fc,u8 rs_status,u8 rs_keyix,
                                  int reason,int queue,int keyid,int net_ev)
{
    u32 packed;
    u16 blob_len;
    const void *blob;
    if(!g_wpa.secured||!g_target.found||!f||len<24)return;
    if(!bssid_equal(f+10,g_target.bssid))return; /* transmitter must be selected AP */
    packed=(u32)rs_status | ((u32)rs_keyix<<8) |
           ((u32)((fc&0x4000u)?1:0)<<16) | ((u32)((f[4]&1u)?1:0)<<17) |
           ((u32)(queue<0?0xff:queue&0xff)<<24);
    /* Do not persist raw EAPOL/rekey contents. */
    blob=(reason==TT1424_RXD_EAPOL)?NULL:f;
    blob_len=(u16)((blob && len>0)?(len>256?256:len):0);
    titan_fr_record(TITAN_FR_EV_RX_DATA,0,(s32)reason,(u32)reason,packed,
                    (u32)fc,(u32)len,blob,blob_len,
                    (u16)(((keyid&0xff)<<8)|(net_ev&0xff)));
}

/* Mirror the relevant ath9k/mac80211 receive post-processing for our narrow
 * station profile. ath9k hardware pads the MAC header to a 32-bit boundary.
 * Pairwise traffic is always CCMP. Group traffic may be CCMP or TKIP. For a
 * valid TKIP hardware key index the hardware may strip Michael; with invalid
 * keyix it leaves IV + plaintext MSDU + Michael + ICV, so TT1426 verifies
 * Michael in software before committing replay state. */
static void parse_data_frame(const u8 *f,int len,u16 fc,u8 rs_status,u8 rs_keyix)
{
    int to_ds=(fc&0x0100u)!=0,from_ds=(fc&0x0200u)!=0;
    int hdr,pad,payload_off,payload_end,protected_frame,group,queue,key_known=0;
    const u8 *eth_src,*payload,*ccmp=NULL;
    u16 et;int ev,keyid=0;u64 tkip_tsc=0;

    if(len<24)return;
    /* In station mode, frames from the AP must be FromDS and not ToDS. */
    if(!from_ds||to_ds){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_DIRECTION,-1,0,0);return;}
    if(!bssid_equal(f+10,g_target.bssid))return; /* transmitter/AP */
    if(!(bssid_equal(f+4,g_radio.macaddr) || (f[4]&1u))){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_DEST,-1,0,0);return;} /* us or group */

    hdr=dot11_data_hdrlen(fc);
    if(hdr<24||hdr>len){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_HDRLEN,-1,0,0);return;}
    pad=hdr&3; /* exact ath9k hardware padding rule */
    payload_off=hdr+pad;
    payload_end=len;
    if(payload_off>payload_end){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_BOUNDS,-1,0,0);return;}

    protected_frame=(fc&0x4000u)!=0;
    group=(f[4]&1u)!=0;
    queue=dot11_qos_queue(f,len,fc,hdr);

    if(protected_frame){
        if(!g_wpa.secured){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_NOT_SECURED,queue,0,0);return;}
        if(rs_status&RX_ERR_DECRYPT){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_DECRYPT,queue,0,0);return;}
        /* ath9k does not treat a multicast MIC bit as authoritative unless
         * rs_keyix identifies a known TKIP key. AR9271 can return keyix=255
         * while still decrypting by the on-air KeyID. In that exact TKIP
         * group case TT1426 verifies Michael in software below. */
        if((rs_status&RX_ERR_MIC) &&
           !(group && titan_wpa2_group_cipher(&g_wpa)==TITAN_RSN_CIPHER_TKIP &&
             rs_keyix==RX_KEY_INVALID)){
            tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_MIC,queue,0,0);return;
        }
        if(!group && (rs_status&RX_ERR_KEYMISS)){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_KEYMISS,queue,0,0);return;}
        if(payload_off+8>payload_end){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_CCMP_SHORT,queue,0,0);return;}
        ccmp=f+payload_off;
        keyid=(ccmp[3]>>6)&3;
        if(!group){
            /* Pairwise is deliberately CCMP-only. */
            if(rs_keyix!=TITAN_PAIRWISE_KEYIX){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_KEYIX,queue,keyid,0);return;}
            if(!ccmp_rx_accept_pn(ccmp,queue,0)){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_PN,queue,keyid,0);return;}
            payload_off+=8;
            if(payload_end-payload_off<8){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_CRYPTO_TAIL,queue,keyid,0);return;}
            payload_end-=8; /* CCMP MIC */
        }else if(titan_wpa2_group_cipher(&g_wpa)==TITAN_RSN_CIPHER_TKIP){
            int sw_michael=(rs_keyix==RX_KEY_INVALID);
            /* ath9k may report an invalid keyix for multicast; the on-air
             * KeyID is authoritative once the matching GTK is installed. */
            if(!g_info.gtk_installed||keyid!=g_info.gtk_keyid||g_wpa.installed_gtk_len!=32){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_GTK,queue,keyid,0);return;}
            if(!tkip_group_rx_check_tsc(ccmp,queue,&tkip_tsc)){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_PN,queue,keyid,0);return;}
            payload_off+=8; /* TKIP IV + ExtIV */
            if(sw_michael){
                u8 calc_mic[8];
                int msdu_len;
                /* With invalid rs_keyix ath9k cannot mark Michael as stripped.
                 * TT1424 hardware logs prove these frames contain plaintext
                 * MSDU || Michael(8) || ICV(4), so verify exactly that layout. */
                if(payload_end-payload_off<12){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_CRYPTO_TAIL,queue,keyid,0);return;}
                msdu_len=payload_end-payload_off-12;
                {
                    const u8 *rxmic=titan_wpa2_installed_tkip_rx_mic(&g_wpa);
                    if(!rxmic){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_GTK,queue,keyid,0);return;}
                    titan_michael_mic(rxmic, f+4, f+16,
                                      (u8)(queue>=0&&queue<16?queue:0),
                                      f+payload_off,(u32)msdu_len,calc_mic);
                }
                if(!titan_consttime_eq(calc_mic,f+payload_off+msdu_len,8)){
                    memset(calc_mic,0,sizeof(calc_mic));
                    tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_MIC,queue,keyid,0);return;
                }
                memset(calc_mic,0,sizeof(calc_mic));
                payload_end-=12; /* verified Michael MIC + TKIP ICV */
            }else{
                if(rs_status&RX_ERR_MIC){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_MIC,queue,keyid,0);return;}
                if(payload_end-payload_off<4){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_CRYPTO_TAIL,queue,keyid,0);return;}
                payload_end-=4; /* valid-keyix path: HW stripped Michael MIC */
            }
            /* Replay state is committed only after Michael validation. */
            tkip_group_rx_commit_tsc(queue,tkip_tsc);
        }else{
            if(g_info.gtk_installed && (rs_keyix==g_info.gtk_keyid || keyid==g_info.gtk_keyid))key_known=1;
            if(!key_known){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_GTK,queue,keyid,0);return;}
            if(!ccmp_rx_accept_pn(ccmp,queue,1)){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_PN,queue,keyid,0);return;}
            payload_off+=8;
            if(payload_end-payload_off<8){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_CRYPTO_TAIL,queue,keyid,0);return;}
            payload_end-=8; /* CCMP MIC */
        }
    }

    if(payload_off+8>payload_end){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_LLC_SHORT,queue,keyid,0);return;}
    payload=f+payload_off;
    if(payload[0]!=0xaa||payload[1]!=0xaa||payload[2]!=0x03||
       payload[3]!=0x00||payload[4]!=0x00||payload[5]!=0x00){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_LLC,queue,keyid,0);return;}
    et=rd_be16(payload+6);payload+=8;
    eth_src=f+16; /* FromDS: Address 3 is Ethernet source. */

    if(et==ETH_P_EAPOL){
        tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_EAPOL,queue,keyid,0);
        /* M1/M3 for our supplicant must come from the selected AP to us. */
        if(!group && bssid_equal(f+4,g_radio.macaddr))queue_eapol(payload,payload_end-payload_off-8);
        return;
    }
    if(g_wpa.secured && (et==ETH_P_IP||et==ETH_P_ARP)){
        /* The hardware-proven TT1426 proof parser remains active, while the
         * production bridge mirrors valid protected Ethernet payloads into
         * NetMan/ps2ip only when the EE network service selects Wi-Fi. */
        if(protected_frame && g_netman_bridge_active){
            /* Production handoff is single-owner: after NetMan takes the link,
             * do not also feed the standalone TT1426 DHCP/ARP/ICMP proof parser.
             * The proof stack already completed before ONLINE_PASS. */
            titan_netman_rx_frame(f+4,eth_src,et,payload,payload_end-payload_off-8);
            return;
        }
        /* Once the 4-way handshake is complete, ordinary data from a WPA2 BSS
         * must be protected. Keep the unprotected exception only for EAPOL
         * above; accepting plaintext IPv4/ARP here would both hide a crypto
         * failure and make the proof stack accept spoofable post-association
         * traffic. */
        if(!protected_frame){tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_UNPROTECTED_L3,queue,keyid,0);return;}
        ev=titan_net_rx(&g_net,et,eth_src,payload,payload_end-payload_off-8);
        if(ev!=TITAN_NET_NONE){
            g_net_event=ev;
            tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_ACCEPT,queue,keyid,ev);
        }else{
            tt1424_trace_rx_data(f,len,fc,rs_status,rs_keyix,TT1424_RXD_NET_NONE,queue,keyid,0);
        }
    }
}

static void capture_assoc_response_rates(const u8 *ies,int len)
{
    int pos=0,i,bit;
    u16 legacy=0,basic=0;
    u8 ns=0,ne=0,supp[8],ext[22];
    memset(supp,0,sizeof(supp));memset(ext,0,sizeof(ext));
    if(!ies||len<=0)return;
    while(pos+2<=len){
        int id=ies[pos],ilen=ies[pos+1];const u8 *v;
        pos+=2;if(pos+ilen>len)break;v=ies+pos;
        if(id==1){ns=(u8)(ilen>8?8:ilen);if(ns)memcpy(supp,v,ns);}
        else if(id==50){ne=(u8)(ilen>22?22:ilen);if(ne)memcpy(ext,v,ne);}
        pos+=ilen;
    }
    /* Some rejection frames may legally omit Supported Rates. Keep the last
     * valid response-derived set in that case rather than throwing away the
     * adaptive seed learned from an earlier explicit response. */
    if(!ns)return;
    for(i=0;i<ns;i++){
        bit=legacy_rate_index(supp[i]);
        if(bit>=0){legacy|=(u16)(1u<<bit);if(supp[i]&0x80u)basic|=(u16)(1u<<bit);}
    }
    for(i=0;i<ne;i++){
        bit=legacy_rate_index(ext[i]);
        if(bit>=0){legacy|=(u16)(1u<<bit);if(ext[i]&0x80u)basic|=(u16)(1u<<bit);}
    }
    memset(g_info.assoc_resp_supp_rates,0,sizeof(g_info.assoc_resp_supp_rates));
    memset(g_info.assoc_resp_ext_rates,0,sizeof(g_info.assoc_resp_ext_rates));
    memcpy(g_info.assoc_resp_supp_rates,supp,ns);if(ne)memcpy(g_info.assoc_resp_ext_rates,ext,ne);
    g_info.assoc_resp_supp_rate_count=ns;g_info.assoc_resp_ext_rate_count=ne;
    g_info.assoc_resp_legacy_rate_mask=legacy;g_info.assoc_resp_basic_rate_mask=basic;
    g_info.assoc_adapt_flags=(u8)((g_info.assoc_adapt_flags&~3u)|1u);
    if(ns!=g_target.supp_rate_count||ne!=g_target.ext_rate_count ||
       !bytes_equal(supp,g_target.supp_rates,ns) || !bytes_equal(ext,g_target.ext_rates,ne))
        g_info.assoc_adapt_flags|=2u;
}

static int oracle_rates_supported(const u8 *v,int n)
{
    int i;
    if(!v||n<1)return 0;
    for(i=0;i<n;i++)if(legacy_rate_index(v[i])<0)return 0;
    return 1;
}

static void oracle_capture_request(const u8 *f,int len,int subtype)
{
    int pos,ssid_ok=0;
    TitanAssocOracle o;
    if(!g_oracle.active||!f)return;
    if(subtype!=0 && subtype!=2)return; /* Association/Reassociation Request */
    if(len<(subtype==2?34:28))return;
    if(!bssid_equal(f+4,g_target.bssid)||!bssid_equal(f+16,g_target.bssid))return;
    if(bssid_equal(f+10,g_radio.macaddr))return;
    memset(&o,0,sizeof(o));
    memcpy(o.sta,f+10,6);o.reassoc=(u8)(subtype==2);
    if(o.reassoc)memcpy(o.current_ap,f+28,6);
    else memcpy(o.current_ap,g_target.bssid,6);
    o.capability=rd_le16(f+24);o.listen_interval=rd_le16(f+26);
    pos=o.reassoc?34:28;
    if(len-pos<=TITAN_MLME_MAX_TEMPLATE_IES){
        o.template_ies_len=(u16)(len-pos);
        memcpy(o.template_ies,f+pos,(unsigned int)o.template_ies_len);
    }
    while(pos+2<=len){
        int id=f[pos],ilen=f[pos+1];const u8 *v;
        pos+=2;if(pos+ilen>len)return;v=f+pos;
        if(id==0){
            if(ilen==(int)g_wifi_ssid_len && bytes_equal(v,(const u8 *)g_wifi_ssid,g_wifi_ssid_len))ssid_ok=1;
        }else if(id==1){
            if(ilen<1||ilen>8||!oracle_rates_supported(v,ilen))return;
            o.supp_rate_count=(u8)ilen;memcpy(o.supp_rates,v,ilen);
        }else if(id==50){
            if(ilen<1||ilen>22||!oracle_rates_supported(v,ilen))return;
            o.ext_rate_count=(u8)ilen;memcpy(o.ext_rates,v,ilen);
        }else if(id==48){
            if(ilen+2<=TITAN_WPA2_MAX_RSN_IE){
                o.rsn_ie[0]=48;o.rsn_ie[1]=(u8)ilen;memcpy(o.rsn_ie+2,v,ilen);
                o.rsn_ie_len=(u8)(ilen+2);
            }
        }else if(id==33 && ilen==2){
            o.has_power=1;o.power_min=v[0];o.power_max=v[1];
        }else if(id==45 && ilen==26)o.has_ht=1;
        else if(id==70 && ilen>=5){
            o.has_rrm_ie=1;
            memcpy(o.rrm_caps,v,5);
        }else if(id==221 && ilen>=7 && v[0]==0x00&&v[1]==0x50&&v[2]==0xf2&&v[3]==0x02)o.has_wmm=1;
        pos+=ilen;
    }
    if(!ssid_ok||!o.supp_rate_count)return;
    /* Store the most recent plausible request.  It is not considered an
     * oracle until we later see a SUCCESS Association Response to this STA. */
    o.active=1;o.request_seen=1;
    g_oracle=o;
    g_info.assoc_diag_flags|=0x02u;
}

static void oracle_capture_response(const u8 *f,int len,int subtype)
{
    u16 st;
    /* Association Response is subtype 1; Reassociation Response is subtype 3.
     * Phones commonly use reassociation on a quick Wi-Fi OFF/ON reconnect.
     * V9.6.0 accepted Reassociation Requests (subtype 2) but accidentally
     * ignored the matching subtype-3 response, so ORC could stay 00 even
     * after a completely successful phone reconnect. */
    if(!g_oracle.active||!g_oracle.request_seen||(subtype!=1&&subtype!=3)||len<30)return;
    if(!bssid_equal(f+10,g_target.bssid)||!bssid_equal(f+4,g_oracle.sta))return;
    st=rd_le16(f+26);
    if(st!=0)return;
    g_oracle.accepted=1;
    g_info.assoc_diag_flags|=0x04u;
    if(g_oracle.capability&0x1000u)g_info.assoc_diag_flags|=0x08u;
    if(g_oracle.has_power)g_info.assoc_diag_flags|=0x10u;
    if(g_oracle.has_ht)g_info.assoc_diag_flags|=0x20u;
    if(g_oracle.has_wmm)g_info.assoc_diag_flags|=0x40u;
    if(g_oracle.has_rrm_ie)g_info.assoc_diag_flags|=0x80u;
}

static void parse_management_frame(const u8 *f, int len, int rssi_dbm)
{
    u16 fc; int type,subtype;
    if(len<24)return;
    fc=rd_le16(f);type=(fc>>2)&3;subtype=(fc>>4)&0xf;
    if(type!=0)return;
    if(subtype==8||subtype==5){parse_beacon_probe(f,len,rssi_dbm);return;}
    if(!g_target.found)return;
    /* Air-oracle observes a third-party association exchange only while the
     * explicit oracle window is active.  It never adopts the other STA MAC. */
    if(g_oracle.active){
        oracle_capture_request(f,len,subtype);
        oracle_capture_response(f,len,subtype);
    }
    /* Responses must be directed to our station and originate from the selected AP. */
    if(!bssid_equal(f+4,g_radio.macaddr) || !bssid_equal(f+10,g_target.bssid))return;
    if(subtype==11 && len>=30){
        u16 alg=rd_le16(f+24),trans=rd_le16(f+26),st=rd_le16(f+28);
        if(alg==0 && trans==2){
            titan_fr_record(TITAN_FR_EV_RX_MGMT,0,0,(u32)rd_le16(f),(u32)len,(u32)(s32)rssi_dbm,0,
                            f,(u16)(len>768?768:len),0);
            titan_fr_record(TITAN_FR_EV_AUTH_RESULT,0,(s32)st,(u32)alg,(u32)trans,(u32)st,0,NULL,0,0);
            g_auth_status=st;g_info.auth_status=st;g_auth_seen=1;
            (void)titan_mlme_rx_auth(&g_mlme,alg,trans,st);
        }
    }else if((subtype==1||subtype==3) && len>=30){
        int varlen=len-30,n;
        u16 cap=rd_le16(f+24),st=rd_le16(f+26),aid=rd_le16(f+28)&0x3fff;
        g_info.assoc_resp_capability=cap;
        g_info.assoc_resp_ie_len=(u8)(varlen>255?255:varlen);
        memset(g_info.assoc_resp_preview,0,sizeof(g_info.assoc_resp_preview));
        n=varlen>(int)sizeof(g_info.assoc_resp_preview)?(int)sizeof(g_info.assoc_resp_preview):varlen;
        if(n>0)memcpy(g_info.assoc_resp_preview,f+30,n);
        capture_assoc_response_rates(f+30,varlen);
        titan_fr_record(TITAN_FR_EV_RX_MGMT,1,0,(u32)rd_le16(f),(u32)len,(u32)(s32)rssi_dbm,0,
                        f,(u16)(len>768?768:len),0);
        titan_fr_record(TITAN_FR_EV_ASSOC_RESULT,0,(s32)st,(u32)cap,(u32)st,(u32)aid,(u32)varlen,
                        f,(u16)(len>768?768:len),0);
        g_assoc_status=st;g_assoc_aid=aid;g_info.assoc_status=st;g_info.aid=aid;g_assoc_seen=1;
        (void)titan_mlme_rx_assoc(&g_mlme,st,aid);
        /* TT1424 continues past ASSOC=0. Raw EAPOL payloads are never recorded
         * and key-cache WMI values are redacted before entering the recorder. */
    }
}

static void parse_dot11_frame(const u8 *f,int len,int rssi_dbm,u8 rs_status,u8 rs_keyix)
{
    u16 fc;int type;
    if(len<10)return;
    fc=rd_le16(f);type=(fc>>2)&3;
    if(type==0)parse_management_frame(f,len,rssi_dbm);
    else if(type==2)parse_data_frame(f,len,fc,rs_status,rs_keyix);
}

static void parse_hif_packet(const u8 *body, int pkt_len)
{
    const u8 *payload, *rxs, *dot11;
    int payload_len, dot11_len, rs_datalen, rs_status, rs_keyix, rssi;
    u8 endpoint;
    if (htc_payload_view(body, pkt_len, &endpoint, &payload, &payload_len) < 0) {
        ++g_info.malformed_rx; return;
    }
    /* WMI control uses REG-IN. WLAN RX service endpoints arrive here. Do not
       reinterpret endpoint 0/control or WMI frames as ath9k RX status. */
    if (endpoint == 0 || endpoint == g_wmi_endpoint) return;
    if (payload_len < RX_STATUS_LEN) { ++g_info.malformed_rx; return; }
    rxs = payload;
    dot11 = rxs + RX_STATUS_LEN;
    dot11_len = payload_len - RX_STATUS_LEN;
    rs_datalen = rd_be16(rxs + 8);
    rs_status = rxs[10];
    rssi = (s8)rxs[12];
    rs_keyix = rxs[19];
    if (rs_status & RX_DROP_STATUS) {
        if (dot11_len >= RX_FCS_LEN + 24 && (((rd_le16(dot11)>>2)&3)==2))
            tt1424_trace_rx_data(dot11,dot11_len-RX_FCS_LEN,rd_le16(dot11),(u8)rs_status,(u8)rs_keyix,TT1424_RXD_HIF_STATUS,-1,0,0);
        return;
    }
    if (rs_datalen != dot11_len || rs_datalen < RX_FCS_LEN + 10) { ++g_info.malformed_rx; return; }
    if (rs_keyix >= RX_KEY_MAX && rs_keyix != RX_KEY_INVALID) { ++g_info.malformed_rx; return; }
    ++g_info.rx_80211_frames;
    if (rssi > 0 && rssi < 128) rssi += RX_DEFAULT_NF_DBM;
    else rssi = RX_DEFAULT_NF_DBM;
    parse_dot11_frame(dot11, rs_datalen - RX_FCS_LEN, rssi, (u8)rs_status, (u8)rs_keyix);
}

static void hif_packet_cb(void *ctx, const u8 *packet, int len)
{
    (void)ctx;
    parse_hif_packet(packet, len);
    g_info.hif_packets = g_hif_rx.packets + 1; /* callback runs before parser increments */
}

static void feed_bulk_rx(const u8 *buf, int count)
{
    int n;
    if (count <= 0) return;
    ++g_info.rx_usb_packets;
    g_info.rx_bytes += (unsigned int)count;

    if (g_info.raw_len == 0) {
        n = count > (int)sizeof(g_info.raw_rx) ? (int)sizeof(g_info.raw_rx) : count;
        memcpy(g_info.raw_rx, buf, n);
        g_info.raw_len = (u8)n;
    }

    titan_hif_rx_feed(&g_hif_rx, buf, count);
    g_info.hif_packets = g_hif_rx.packets;
    g_info.hif_partial_packets = g_hif_rx.partial_packets;
    g_info.hif_invalid_packets = g_hif_rx.invalid_packets;
}

static int rx_submit_slot(TitanRxSlot *slot)
{
    int rc;
    if (!slot || g_dev_id < 0 || g_bulk_in_pipe < 0 || slot->armed) return -1;
    slot->done = 0;
    slot->result = 0;
    slot->count = 0;
    slot->armed = 1;
    ++slot->submits;
    ++g_info.rx_resubmits;
    rc = UsbBulkTransfer(g_bulk_in_pipe, slot->buffer, RX_XFER_SIZE, rx_callback, slot);
    if (rc != USB_RC_OK) {
        slot->armed = 0;
        ++g_info.rx_submit_errors;
        return -2;
    }
    return 0;
}

static void rx_update_armed_count(void)
{
    int i, n = 0;
    for (i = 0; i < RX_SLOT_COUNT; ++i) if (g_rx_slots[i].armed) ++n;
    g_info.rx_slots_armed = (u8)n;
    if ((u8)n > g_info.rx_slots_peak) g_info.rx_slots_peak = (u8)n;
    g_rx_armed = n > 0;
}

static void rx_arm_all_available(void)
{
    int i;
    if (g_dev_id < 0 || g_bulk_in_pipe < 0) { g_rx_armed = 0; return; }
    for (i = 0; i < RX_SLOT_COUNT; ++i) {
        if (!g_rx_slots[i].armed && !g_rx_slots[i].done)
            (void)rx_submit_slot(&g_rx_slots[i]);
    }
    rx_update_armed_count();
}

static void rx_thread(void *arg)
{
    int i;
    (void)arg;
    for (;;) {
        if (g_dev_id < 0 || g_bulk_in_pipe < 0) {
            g_rx_armed = 0;
            g_info.rx_slots_armed = 0;
            DelayThread(10000);
            continue;
        }

        rx_arm_all_available();
        if (!g_rx_armed) {
            DelayThread(5000);
            continue;
        }

        WaitSema(g_rx_sema);
        for (i = 0; i < RX_SLOT_COUNT; ++i) {
            TitanRxSlot *slot = &g_rx_slots[i];
            if (!slot->done) continue;
            slot->done = 0;
            ++g_info.rx_processed_completions;
            if (slot->result == USB_RC_OK && slot->count > 0)
                feed_bulk_rx(slot->buffer, slot->count);
            else if (g_dev_id >= 0 && g_bulk_in_pipe >= 0 && slot->result != USB_RC_OK)
                ++g_info.malformed_rx;
            if (g_dev_id >= 0 && g_bulk_in_pipe >= 0)
                (void)rx_submit_slot(slot);
        }
        rx_update_armed_count();
    }
}

/* ---------------- device / bring-up ----------------------------------- */
/*
 * PS2/OHCI workaround: the real AR9271 descriptors expose EP83/EP04 as
 * INTERRUPT endpoints (Linux also uses interrupt URBs for these REG pipes).
 * On the current PS2SDK USBD, however, EP04 failed to allocate as interrupt
 * on this real console. Re-describing the same endpoint addresses as BULK to
 * the host scheduler is a PS2-specific workaround that has already carried
 * real HTC/WMI traffic successfully. The device descriptors themselves are
 * never modified.
 */
static int open_reg_pipes_as_bulk(void)
{
    UsbEndpointDescriptor *ep;
    UsbEndpointDescriptor *reg_in = NULL, *reg_out = NULL;
    UsbEndpointDescriptor in_bulk, out_bulk;
    int i;

    ep = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(g_dev_id, NULL, USB_DT_ENDPOINT);
    for (i = 0; i < 6 && ep; ++i) {
        if (ep->bEndpointAddress == AR9271_REG_IN_EP) reg_in = ep;
        if (ep->bEndpointAddress == AR9271_REG_OUT_EP) reg_out = ep;
        if (ep->bLength == 0) break;
        ep = (UsbEndpointDescriptor *)((u8 *)ep + ep->bLength);
    }
    if (!reg_in || !reg_out) return -1;

    memcpy(&in_bulk, reg_in, sizeof(in_bulk));
    memcpy(&out_bulk, reg_out, sizeof(out_bulk));
    in_bulk.bmAttributes = (in_bulk.bmAttributes & ~USB_ENDPOINT_XFERTYPE_MASK) | USB_ENDPOINT_XFER_BULK;
    out_bulk.bmAttributes = (out_bulk.bmAttributes & ~USB_ENDPOINT_XFERTYPE_MASK) | USB_ENDPOINT_XFER_BULK;
    in_bulk.bInterval = 0;
    out_bulk.bInterval = 0;

    g_int_in_pipe = UsbOpenEndpointAligned(g_dev_id, &in_bulk);
    if (g_int_in_pipe < 0) g_int_in_pipe = UsbOpenEndpoint(g_dev_id, &in_bulk);
    if (g_int_in_pipe < 0) return -2;

    g_int_out_pipe = UsbOpenEndpointAligned(g_dev_id, &out_bulk);
    if (g_int_out_pipe < 0) g_int_out_pipe = UsbOpenEndpoint(g_dev_id, &out_bulk);
    if (g_int_out_pipe < 0) {
        UsbCloseEndpoint(g_int_in_pipe);
        g_int_in_pipe = -1;
        return -3;
    }

    g_info.int_in_pipe = g_int_in_pipe;
    g_info.int_out_pipe = g_int_out_pipe;
    return 0;
}

static int open_bulk_in_pipe(void)
{
    UsbEndpointDescriptor *ep;
    int i;
    ep = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(g_dev_id, NULL, USB_DT_ENDPOINT);
    for (i = 0; i < 6 && ep; ++i) {
        u8 type = ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
        if (ep->bEndpointAddress == AR9271_BULK_IN_EP && type == USB_ENDPOINT_XFER_BULK) {
            /* Important PS2SDK rule: usbmass opens BULK-IN with the normal pipe,
             * explicitly warning that OpenPipeAligned can make some devices
             * incompatible. Our buffers remain 64-byte aligned, but the pipe is
             * intentionally the normal variant. */
            g_bulk_in_pipe = UsbOpenEndpoint(g_dev_id, ep);
            g_info.bulk_in_open_mode = 1;
            if (g_bulk_in_pipe < 0) {
                g_bulk_in_pipe = UsbOpenEndpointAligned(g_dev_id, ep);
                g_info.bulk_in_open_mode = 2;
            }
            g_info.bulk_in_mps = (u16)ep->wMaxPacketSizeLB | ((u16)ep->wMaxPacketSizeHB << 8);
            g_info.bulk_in_pipe = g_bulk_in_pipe;
            return g_bulk_in_pipe >= 0 ? 0 : -2;
        }
        if (ep->bLength == 0) break;
        ep = (UsbEndpointDescriptor *)((u8 *)ep + ep->bLength);
    }
    return -1;
}

static int open_bulk_out_pipe(void)
{
    UsbEndpointDescriptor *ep;
    int i;
    ep = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(g_dev_id, NULL, USB_DT_ENDPOINT);
    for (i = 0; i < 6 && ep; ++i) {
        u8 type = ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
        if (ep->bEndpointAddress == AR9271_BULK_OUT_EP && type == USB_ENDPOINT_XFER_BULK) {
            /* TT1417 FS-BULK-OUT-MPS64-FIX:
             *
             * PS2SDK's non-aligned bulk OUT path clamps an OHCI endpoint whose
             * wMaxPacketSize is 64 to 62 bytes.  The AR9271 full-speed target
             * uses a short USB packet as the end-of-transfer boundary; a 62-byte
             * packet on a device endpoint whose real MPS is 64 is therefore an
             * early terminator for frames that span more than one USB packet.
             *
             * Association is 174 bytes on the USB wire (64 + 64 + 46).  It must
             * be submitted as ONE UsbBulkTransfer through an aligned endpoint so
             * the OHCI ED retains the descriptor's real 64-byte MPS.  Do not
             * fall back to the normal pipe: silently doing so reintroduces the
             * truncation that TT1416 measured (nbuf_len=44).
             */
            g_bulk_out_pipe = UsbOpenEndpointAligned(g_dev_id, ep);
            g_info.bulk_out_open_mode = 2;
            g_info.bulk_out_pipe = g_bulk_out_pipe;
            return g_bulk_out_pipe >= 0 ? 0 : -2;
        }
        if (ep->bLength == 0) break;
        ep = (UsbEndpointDescriptor *)((u8 *)ep + ep->bLength);
    }
    return -1;
}

static int upload_firmware(void)
{
    u32 offset = 0, addr = AR9271_FIRMWARE_ADDR;
    if (size_htc_9271_fw < AR9271_FIRMWARE_MIN_SIZE || size_htc_9271_fw > AR9271_FIRMWARE_MAX_SIZE) return -1;
    g_info.fw_total = size_htc_9271_fw;
    g_info.fw_sent = 0;
    while (offset < size_htc_9271_fw) {
        u32 n = size_htc_9271_fw - offset;
        if (n > AR9271_FW_CHUNK) n = AR9271_FW_CHUNK;
        memcpy(g_fw_stage, htc_9271_fw + offset, n);
        if (sync_control(USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
                         AR9271_REQ_FW_DOWNLOAD, (int)((addr >> 8) & 0xffff), 0,
                         (int)n, g_fw_stage, 3000000) < 0)
            return -2;
        offset += n;
        addr += n;
        g_info.fw_sent = offset;
    }
    if (sync_control(USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
                     AR9271_REQ_FW_COMPLETE,
                     (int)((AR9271_FIRMWARE_TEXT_ADDR >> 8) & 0xffff),
                     0, 0, NULL, 3000000) < 0)
        return -3;
    return 0;
}

static int htc_wmi_handshake(void)
{
    int i, rc;
    u8 endpoint;

    if (sync_reg_in(g_reg_rx, sizeof(g_reg_rx), 5000000) < 0) return -1;
    if (parse_htc_ready() < 0) return -2;
    g_info.state = TITAN_SCAN_HTC_READY;
    set_detail("HTC READY; Linux 7.1.5 service order");

    for (i = 0; i < 9; ++i) {
        endpoint = 0;
        rc = connect_service(g_service_ids[i], &endpoint);
        if (rc < 0) return -10 - i;
        if (g_service_ids[i] == 0x0100) {
            g_wmi_endpoint = endpoint;
            g_info.wmi_endpoint = endpoint;
        } else if (g_service_ids[i] == 0x0104) {
            g_mgmt_endpoint = endpoint;
            g_info.mgmt_endpoint = endpoint;
        } else if (g_service_ids[i] == 0x0107) {
            g_data_be_endpoint = endpoint;
            g_info.data_be_endpoint = endpoint;
        } else if (g_service_ids[i] == 0x0105) {
            g_data_vo_endpoint = endpoint;
        }
    }
    /* Exact endpoint allocation observed in the cold-start capture. */
    if (g_wmi_endpoint != 1 || g_mgmt_endpoint != 5 || g_data_be_endpoint != 6 || g_data_vo_endpoint != 9) return -29;
    if (config_pipe_credits() < 0) return -30;
    if (setup_complete() < 0) return -31;

    /* Do NOT issue GET_FW_VERSION here. Linux's first post-HTC WMI command is
     * REG_READ seq=1. GET_FW_VERSION occurs inside the captured init trace
     * (seq=317), so issuing it here would shift every subsequent WMI sequence. */
    g_wmi_seq = 0;
    return 0;
}

static void update_target_rx_stats(void)
{
    u8 stats[16];
    int n = 0;
    memset(stats, 0, sizeof(stats));
    if (issue_wmi(WMI_RX_STATS_CMDID, NULL, 0, stats, sizeof(stats), &n) < 0)
        return;
    /* struct ath9k_htc_target_rx_stats: three big-endian u32 values. */
    if (n >= 12) {
        g_info.tgt_rx_nobuf = ((u32)stats[0] << 24) | ((u32)stats[1] << 16) |
                              ((u32)stats[2] << 8) | (u32)stats[3];
        g_info.tgt_rx_host_send = ((u32)stats[4] << 24) | ((u32)stats[5] << 16) |
                                  ((u32)stats[6] << 8) | (u32)stats[7];
        g_info.tgt_rx_host_done = ((u32)stats[8] << 24) | ((u32)stats[9] << 16) |
                                  ((u32)stats[10] << 8) | (u32)stats[11];
    }
}

static int wmi_remove_monitor_target(void)
{
    u8 vif[12],idx=0;int n=0;
    memset(vif,0,sizeof(vif));vif[0]=0;memcpy(vif+2,g_radio.macaddr,6);
    if(issue_wmi(WMI_VAP_REMOVE_CMDID,vif,sizeof(vif),NULL,0,&n)<0)return -1;
    if(issue_wmi(WMI_NODE_REMOVE_CMDID,&idx,1,NULL,0,&n)<0)return -2;
    return 0;
}

static int wmi_create_station_vif(void)
{
    u8 vif[12],node[22];int n=0;
    memset(vif,0,sizeof(vif));
    vif[0]=0;vif[1]=HTC_M_STA;memcpy(vif+2,g_radio.macaddr,6);vif[8]=0;wr_be16(vif+9,2304);vif[11]=0;
    if(issue_wmi(WMI_VAP_CREATE_CMDID,vif,sizeof(vif),NULL,0,&n)<0)return -1;
    memset(node,0,sizeof(node));
    memcpy(node,g_radio.macaddr,6);node[12]=0;node[13]=0;node[14]=1;
    wr_be16(node+15,0);wr_be16(node+17,0);wr_be16(node+19,0xffff);node[21]=0;
    if(issue_wmi(WMI_NODE_CREATE_CMDID,node,sizeof(node),NULL,0,&n)<0)return -2;
    return 0;
}

/* V9.8.2: keep the already-working station VAP at index 0 and add the
 * oracle monitor as the second firmware VAP.  This mirrors Linux ath9k_htc's
 * monitor-interface model: a monitor VAP gets its own vif/node slot and can
 * coexist with another active interface.  The AR9271 target supports two VIFs.
 *
 * This deliberately avoids calling titan_ar9271_radio_start_monitor() while RX
 * is already running.  That routine is the cold-start path and begins with
 * WMI_START_RECV; V9.8.0 called it during a live STA->monitor swap, which can
 * fail before the 60-second oracle window is ever opened. */
static int wmi_create_oracle_monitor_vif(void)
{
    u8 vif[12],node[22],cleanup[12];int n=0;
    memset(vif,0,sizeof(vif));
    vif[0]=ORACLE_MON_VIF_IDX;vif[1]=HTC_M_MONITOR;memcpy(vif+2,g_radio.macaddr,6);
    vif[8]=0;wr_be16(vif+9,0);vif[11]=0;
    if(issue_wmi(WMI_VAP_CREATE_CMDID,vif,sizeof(vif),NULL,0,&n)<0)return -1;

    memset(node,0,sizeof(node));
    memcpy(node,g_radio.macaddr,6);
    node[12]=ORACLE_MON_STA_IDX;node[13]=ORACLE_MON_VIF_IDX;node[14]=1;
    wr_be16(node+15,0);wr_be16(node+17,0);wr_be16(node+19,0xffff);node[21]=0;
    if(issue_wmi(WMI_NODE_CREATE_CMDID,node,sizeof(node),NULL,0,&n)<0){
        memset(cleanup,0,sizeof(cleanup));cleanup[0]=ORACLE_MON_VIF_IDX;
        memcpy(cleanup+2,g_radio.macaddr,6);
        (void)issue_wmi(WMI_VAP_REMOVE_CMDID,cleanup,sizeof(cleanup),NULL,0,&n);
        return -2;
    }
    return 0;
}

static int wmi_remove_oracle_monitor_vif(void)
{
    u8 vif[12],idx=ORACLE_MON_STA_IDX;int n=0,vr=0,nr=0;
    /* Preserve Linux ath9k_htc monitor teardown ordering: VAP_REMOVE first,
     * then NODE_REMOVE for the monitor vif-station entry.  Attempt both even
     * if one command fails so the AP-station slot can be reused later. */
    memset(vif,0,sizeof(vif));vif[0]=ORACLE_MON_VIF_IDX;
    memcpy(vif+2,g_radio.macaddr,6);
    if(issue_wmi(WMI_VAP_REMOVE_CMDID,vif,sizeof(vif),NULL,0,&n)<0)vr=-1;
    if(issue_wmi(WMI_NODE_REMOVE_CMDID,&idx,1,NULL,0,&n)<0)nr=-1;
    if(vr<0)return -1;
    if(nr<0)return -2;
    return 0;
}

static int wmi_update_ap_station_rate(void)
{
    /* Linux 5.4 ath9k_htc_setup_rate() subset lives in a pure helper so its
     * WMI target ABI can be host-tested independently of PS2SDK/USBD. */
    u8 legacy[TITAN_ATH9K_RATE_MAX],rate[TITAN_ATH9K_TARGET_RATE_LEN];
    int n=0,i,nr=0,len;

    for(i=0;i<g_mlme.supp_rate_count&&nr<TITAN_ATH9K_RATE_MAX;i++)
        legacy[nr++]=(u8)(g_mlme.supp_rates[i]&0x7f);
    for(i=0;i<g_mlme.ext_rate_count&&nr<TITAN_ATH9K_RATE_MAX;i++)
        legacy[nr++]=(u8)(g_mlme.ext_rates[i]&0x7f);
    if(nr==0){
        nr=g_target.rate_count;if(nr>TITAN_ATH9K_RATE_MAX)nr=TITAN_ATH9K_RATE_MAX;
        for(i=0;i<nr;i++)legacy[i]=(u8)(g_target.rates[i]&0x7f);
    }

    len=titan_ath9k_build_target_rate(rate,1,legacy,(u8)nr,
                                      (u8)(g_mlme.use_ht&&g_target.ht_present),
                                      g_target.ht_present?g_target.ht_cap:NULL);
    if(len<0)return -1;
    if(issue_wmi(WMI_RC_RATE_UPDATE_CMDID,rate,len,NULL,0,&n)<0)return -2;
    return 0;
}

static int wmi_create_ap_station(void)
{
    u8 node[22],ampdu_factor=0;u32 maxampdu;int n=0;
    /* Linux/mac80211 lifecycle: node 0 is the VIF-station entry created with
     * the interface. The AP peer does NOT exist while Auth/Assoc are being sent;
     * mac80211 adds the regular-station AP entry after it receives a successful
     * Association Response. Only then does ath9k_htc create the target peer and
     * initialize its rate table. Titan V10.0/V10.1 had this ordering backwards. */
    memset(node,0,sizeof(node));
    memcpy(node,g_target.bssid,6);memcpy(node+6,g_target.bssid,6);
    node[12]=1;node[13]=0;node[14]=0;wr_be16(node+15,0);wr_be16(node+17,0);
    /* ath9k_htc's peer-node maxampdu is 1 << (13 + peer ampdu_factor), not
     * the 0xffff sentinel used for the local VIF-station node. A non-HT peer
     * therefore starts at 8 KiB; for HT, use the selected AP's factor. The
     * target field is 16-bit just like upstream, so factor 3 intentionally
     * wraps 65536 to 0 in the wire ABI. */
    if(g_mlme.use_ht&&g_target.ht_present)ampdu_factor=(u8)(g_target.ht_cap[2]&3u);
    maxampdu=1u<<(13u+ampdu_factor);
    wr_be16(node+19,(u16)maxampdu);node[21]=0;
    if(issue_wmi(WMI_NODE_CREATE_CMDID,node,sizeof(node),NULL,0,&n)<0)return -1;
    if(wmi_update_ap_station_rate()<0)return -2;
    return 0;
}

static int wait_event(volatile int *flag,int timeout_ms)
{
    int i,steps=timeout_ms/10;if(steps<1)steps=1;
    for(i=0;i<steps;i++){if(*flag)return 1;if(g_dev_id<0)return 0;DelayThread(10000);}return *flag?1:0;
}

static u16 next_seq_ctrl(void)
{
    u16 v=(u16)((g_dot11_seq++ & 0x0fffu)<<4);return v;
}

static u16 next_qos_seq_ctrl(u8 tid)
{
    u16 v;
    tid&=0x0fu;
    v=(u16)((g_qos_seq[tid]&0x0fffu)<<4);
    g_qos_seq[tid]=(u16)((g_qos_seq[tid]+1u)&0x0fffu);
    return v;
}


static void dot11_header(u8 *f,u16 fc,const u8 a1[6],const u8 a2[6],const u8 a3[6])
{
    memset(f,0,24);wr_le16(f,fc);wr_le16(f+2,0);memcpy(f+4,a1,6);memcpy(f+10,a2,6);memcpy(f+16,a3,6);wr_le16(f+22,next_seq_ctrl());
}

static int tx_mgmt_frame(const u8 *f,int len)
{
    int rc,air;u8 cookie;
    if(!f||len<10)return -1;
    g_last_mgmt_air=0;
    titan_fr_record(TITAN_FR_EV_TX_MGMT,0,0,(u32)rd_le16(f),(u32)len,(u32)g_tx.mgmt_sta_idx,(u32)g_tx.vif_idx,
                    f,(u16)(len>768?768:len),0);
    cookie=g_tx.cookie;g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;
    rc=titan_ar9271_tx_mgmt(&g_tx,f,len);g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    if(rc<0)return rc;
    if(!(f[4]&1u)){
        /* V9.2.x often saw TXSTATUS arrive after its 600 ms window. Give the
         * firmware/REG-IN path almost the same grace used by upstream's
         * multi-second TX cleanup; a received Authentication/Association
         * Response still overrides unknown TX telemetry. */
        air=wait_wmi_tx_status(g_tx.last_cookie,g_tx.mgmt_ep,2500000);
        g_last_mgmt_air=(u8)(air>0?2:(air<0?1:0));
        if((rd_le16(f)&0x00fcu)==0x00b0u)g_info.auth_tx_rate=g_tx_status_rate[g_tx.last_cookie];
        else if((rd_le16(f)&0x00fcu)==0x0000u || (rd_le16(f)&0x00fcu)==0x0020u)
            g_info.assoc_tx_rate=g_tx_status_rate[g_tx.last_cookie];
        if(air<0)return -20; /* firmware reports no ACK */
    }else g_last_mgmt_air=2;
    return 0;
}

static int send_deauth_request_nowait(void)
{
    u8 f[26];int rc;u8 cookie;
    dot11_header(f,0x00c0,g_target.bssid,g_radio.macaddr,g_target.bssid);
    wr_le16(f+24,3); /* station is leaving/deauthenticating */
    cookie=g_tx.cookie;g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;
    rc=titan_ar9271_tx_mgmt(&g_tx,f,sizeof(f));
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    return rc;
}

static int tx_data_frame_linux(const u8 *f,int len,u8 key_type,u8 keyix,u8 epid,u8 tid,u32 flags)
{
    int rc,air;u8 cookie;
    if(!f||len<10||!epid)return -1;
    cookie=g_tx.cookie;g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;
    rc=titan_ar9271_tx_data_linux(&g_tx,f,len,key_type,keyix,1,epid,tid,flags);
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    if(rc<0)return rc;
    if(!(f[4]&1u)){
        air=wait_wmi_tx_status(g_tx.last_cookie,epid,2500000);
        if(air<0)return -20;
    }
    return 0;
}

static int tx_data_frame(const u8 *f,int len,u8 key_type,u8 keyix)
{
    return tx_data_frame_linux(f,len,key_type,keyix,g_data_be_endpoint,0,1u);
}

static int assoc_request_selfcheck(const u8 *f,int len);
static int assoc_request_selfcheck_managed(const u8 *f,int len);

static int send_auth_request(void)
{
    u8 f[32];int n;
    titan_mlme_begin_auth(&g_mlme);
    n=titan_mlme_build_auth(&g_mlme,next_seq_ctrl(),f,sizeof(f));
    if(n<0)return -30+n;
    return tx_mgmt_frame(f,n);
}

static void mlme_sync_assoc_telemetry(void)
{
    g_info.assoc_tx_capability=g_mlme.assoc_capability;
    g_info.assoc_listen_interval=g_mlme.listen_interval;
    g_info.assoc_rate_count=g_mlme.rate_count;
    g_info.assoc_supp_rate_count=g_mlme.supp_rate_count;
    g_info.assoc_ext_rate_count=g_mlme.ext_rate_count;
    g_info.assoc_legacy_rate_mask=g_mlme.legacy_rate_mask;
    /* RATES TX describes the station-generated legacy-rate bytes actually
     * emitted. V10.6 keeps the mac80211-derived policy: AP Basic requirements are validated,
     * while the Association Request itself carries no copied Basic markers. */
    g_info.assoc_basic_rate_mask=g_mlme.basic_rate_mask;
    memset(g_info.assoc_supp_rates,0,sizeof(g_info.assoc_supp_rates));
    memset(g_info.assoc_ext_rates,0,sizeof(g_info.assoc_ext_rates));
    if(g_mlme.supp_rate_count)memcpy(g_info.assoc_supp_rates,g_mlme.supp_rates,g_mlme.supp_rate_count);
    if(g_mlme.ext_rate_count)memcpy(g_info.assoc_ext_rates,g_mlme.ext_rates,g_mlme.ext_rate_count);
}

static int send_mlme_assoc_request(void)
{
    u8 f[512];const u8 *rsn;int rsn_len,n,rc,self;
    /* V10.6 has no accepted-station template/oracle path.  Always use the
     * local WPA2 selection and one deterministic managed-station frame. */
    titan_wpa2_use_selected_rsn_ie(&g_wpa);
    g_info.assoc_rsn_mode=0;
    rsn=titan_wpa2_rsn_ie(&g_wpa,&rsn_len);
    g_info.assoc_rsn_ie_len=(u8)(rsn_len>255?255:rsn_len);
    memset(g_info.assoc_rsn_preview,0,sizeof(g_info.assoc_rsn_preview));
    if(rsn&&rsn_len>0){
        int preview=rsn_len>(int)sizeof(g_info.assoc_rsn_preview)?(int)sizeof(g_info.assoc_rsn_preview):rsn_len;
        memcpy(g_info.assoc_rsn_preview,rsn,preview);
    }
    g_info.assoc_wire_len=0;g_info.assoc_wire_hash=0;g_info.online_diag_reserved=0;
    titan_mlme_begin_assoc(&g_mlme);
    n=titan_mlme_build_assoc(&g_mlme,next_seq_ctrl(),rsn,rsn_len,f,sizeof(f));
    if(n<0){g_info.assoc_req_selfcheck=2;return -40+n;}
    mlme_sync_assoc_telemetry();
    self=assoc_request_selfcheck_managed(f,n);
    g_info.assoc_req_selfcheck=(u8)self;
    /* A locally malformed/inconsistent request never reaches the radio. */
    if(self!=1)return -80-self;
    rc=tx_mgmt_frame(f,n);
    g_info.assoc_wire_len=g_tx.last_wire_len;
    g_info.assoc_wire_hash=g_tx.last_wire_hash;
    return rc;
}

static int ap_basic_ht_mcs_supported(void)
{
    int i;
    if(!g_target.ht_op_present)return 1;
    /* struct ieee80211_ht_operation: 1 primary + 1 ht_param + 2 operation
     * mode + 2 stbc + 16-byte Basic HT-MCS Set. AR9271 is 1x1 and ath9k
     * advertises MCS0..7 only, i.e. rx_mask[0]=0xff and the rest zero. */
    for(i=7;i<22;i++)if(g_target.ht_op[i])return 0;
    return 1;
}

static int append_ar9271_ht_cap_policy(u8 *f,int p,int cap,int ht_mode)
{
    /* Linux ath9k AR9271 baseline: 1 spatial stream, MCS0..7,
     * 20/40 + SGI20/40 + DSSS/CCK40, A-MPDU 64K/density 8.
     * ieee80211_mcs_info is 10-byte RX mask, 2-byte rx_highest,
     * 1-byte tx_params, then 3 reserved bytes. tx_params therefore lives
     * at HT-capability payload byte 15 and must contain TX_DEFINED. */
    u8 ht[26]={
        0x6e,0x10, 0x1b,
        0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00, 0x01,
        0x00,0x00,0x00,
        0x00,0x00,
        0x00,0x00,0x00,0x00,
        0x00
    };
    u16 hcap=0x106eu;
    u8 ampdu=0x1bu;
    if(ht_mode==1){                 /* strict 20 MHz + SGI20 */
        hcap=0x102cu;
    }else if(ht_mode==2){           /* conservative HT20, no SGI */
        hcap=0x000cu;
    }else if(ht_mode==3 && g_target.ht_present){
        /* Vendor-strict interoperability profile: advertise only the
         * AR9271 capabilities that the AP itself also advertises. This is
         * intentionally more conservative than normal mac80211. */
        u16 apcap=rd_le16(g_target.ht_cap);
        hcap=(u16)(0x106eu & apcap);
        /* SMPS=disabled is an STA property, not an AP feature intersection. */
        hcap=(u16)((hcap&~0x000cu)|0x000cu);
        /* Never leave SGI40/DSSSCCK40 set when 40 MHz is gone. */
        if(!(hcap&0x0002u))hcap&=(u16)~(0x0040u|0x1000u);
        /* Use the stricter A-MPDU factor and spacing advertised by the AP. */
        {
            u8 apamp=g_target.ht_cap[2];
            u8 factor=(u8)((ampdu&3u)<(apamp&3u)?(ampdu&3u):(apamp&3u));
            u8 dens=(u8)(((ampdu>>2)&7u)>((apamp>>2)&7u)?((ampdu>>2)&7u):((apamp>>2)&7u));
            ampdu=(u8)(factor|(dens<<2));
        }
    }else if(ht_mode==4){           /* very conservative old-driver 20 MHz */
        hcap=0x0020u;               /* SGI20 only; no 40 MHz features */
    }
    wr_le16(ht,hcap);ht[2]=ampdu;
    if(!f||p<0||p+28>cap)return -1;
    f[p++]=45;f[p++]=26;memcpy(f+p,ht,sizeof(ht));return p+(int)sizeof(ht);
}

static int append_qos_capability(u8 *f,int p,int cap)
{
    /* OpenBSD net80211 emits IEEE 802.11e QoS Capability (IE 46) before
     * HT Capabilities when the AP is QoS-capable and the station associates
     * as HT. Keep U-APSD disabled (QoS Info=0), matching our WMM Information
     * element and the capabilities the current data path actually implements. */
    if(!f||p<0||p+3>cap)return -1;
    f[p++]=46;f[p++]=1;f[p++]=0;
    return p;
}

static int append_wmm_info(u8 *f,int p,int cap)
{
    /* WMM Information IE for a non-UAPSD station.  HT association on Wi-Fi
     * gear commonly expects WMM/QoS capability to be advertised as well. */
    static const u8 wmm[9]={0xdd,0x07,0x00,0x50,0xf2,0x02,0x00,0x01,0x00};
    if(!f||p<0||p+(int)sizeof(wmm)>cap)return -1;
    memcpy(f+p,wmm,sizeof(wmm));return p+(int)sizeof(wmm);
}

static int append_power_capability(u8 *f,int p,int cap,u8 min_dbm,u8 max_dbm)
{
    /* mac80211 emits Power Capability whenever Spectrum Management OR Radio
     * Measurement is set in Capability Information. ath9k's 2.4 GHz channel
     * table advertises max_power=20 dBm, so use the same 0..20 dBm range for
     * the AR9271 diagnostic association. */
    if(!f||p<0||p+4>cap)return -1;
    f[p++]=33;f[p++]=2;f[p++]=min_dbm;f[p++]=max_dbm;return p;
}

static int append_rrm_enabled_capabilities(u8 *f,int p,int cap,const u8 *caps5)
{
    /* IE 70 is five bytes. V9.6/V9.7 set the Radio Measurement Capability
     * bit but forgot the companion element, so those so-called RRM profiles
     * were structurally incomplete. For the AIR ORACLE replay we use the
     * exact five bytes from the station that the AP accepted. Legacy manual
     * RRM probes use an all-zero diagnostic body rather than inventing bits. */
    static const u8 none[5]={0,0,0,0,0};
    const u8 *v=caps5?caps5:none;
    if(!f||p<0||p+7>cap)return -1;
    f[p++]=70;f[p++]=5;memcpy(f+p,v,5);return p+5;
}


/* Append Supported Rates / Extended Supported Rates.  V9.3.0 adds two
 * important one-shot probes that V9.2.x could not express: preserve the AP's
 * exact IE-1/IE-50 boundary, and preserve that boundary while clearing Basic
 * markers.  This matters for old/vendor AP parsers that validate the two IEs
 * independently instead of treating them as one logical rate set. */
static int append_assoc_rate_ies_from(u8 *f,int p,int cap,
                                      const u8 *supp,int nsupp,
                                      const u8 *ext,int next,int rate_mode)
{
    TitanAssocRateIes r;int i;
    if(titan_assoc_build_rate_ies(supp,nsupp,ext,next,rate_mode,&r)<0)return -1;
    if(r.supp_len==0 || p+2+r.supp_len>cap)return -1;
    f[p++]=1;f[p++]=r.supp_len;memcpy(f+p,r.supp,r.supp_len);p+=r.supp_len;
    if(r.ext_len){
        if(p+2+r.ext_len>cap)return -1;
        f[p++]=50;f[p++]=r.ext_len;memcpy(f+p,r.ext,r.ext_len);p+=r.ext_len;
    }
    g_info.assoc_rate_count=r.total_len;
    g_info.assoc_supp_rate_count=r.supp_len;
    g_info.assoc_ext_rate_count=r.ext_len;
    memset(g_info.assoc_supp_rates,0,sizeof(g_info.assoc_supp_rates));
    memset(g_info.assoc_ext_rates,0,sizeof(g_info.assoc_ext_rates));
    if(r.supp_len)memcpy(g_info.assoc_supp_rates,r.supp,r.supp_len);
    if(r.ext_len)memcpy(g_info.assoc_ext_rates,r.ext,r.ext_len);
    g_info.assoc_legacy_rate_mask=r.legacy_mask;
    g_info.assoc_basic_rate_mask=r.basic_mask;
    for(i=0;i<r.supp_len;i++)if(legacy_rate_index(r.supp[i])<0)return -2;
    for(i=0;i<r.ext_len;i++)if(legacy_rate_index(r.ext[i])<0)return -2;
    return p;
}

static int append_assoc_rate_ies(u8 *f,int p,int cap,int rate_mode,int response_source)
{
    if(response_source==2){
        if(!g_oracle.accepted||g_oracle.supp_rate_count==0)return -10;
        return append_assoc_rate_ies_from(f,p,cap,
                                          g_oracle.supp_rates,g_oracle.supp_rate_count,
                                          g_oracle.ext_rates,g_oracle.ext_rate_count,
                                          rate_mode);
    }
    if(response_source==1){
        if(!(g_info.assoc_adapt_flags&1u)||g_info.assoc_resp_supp_rate_count==0)return -9;
        return append_assoc_rate_ies_from(f,p,cap,
                                          g_info.assoc_resp_supp_rates,g_info.assoc_resp_supp_rate_count,
                                          g_info.assoc_resp_ext_rates,g_info.assoc_resp_ext_rate_count,
                                          rate_mode);
    }
    return append_assoc_rate_ies_from(f,p,cap,
                                      g_target.supp_rates,g_target.supp_rate_count,
                                      g_target.ext_rates,g_target.ext_rate_count,
                                      rate_mode);
}

static int assoc_request_selfcheck_managed(const u8 *f,int len)
{
    TitanAssocWireResult r;int rc;
    memset(&r,0,sizeof(r));
    rc=titan_assoc_wire_check(f,len,g_target.bssid,g_radio.macaddr,
                              (const u8 *)g_wifi_ssid,g_wifi_ssid_len,
                              g_info.target_basic_rate_mask,g_target.rsn_ie_len?1:0,
                              g_mlme.use_ht?1:0,g_mlme.use_wmm?1:0,&r);
    g_info.assoc_diag_flags=r.flags;
    g_info.online_diag_reserved=r.ie_count;
    /* Cross-check the byte parser against the MLME telemetry.  This catches
     * accidental builder/telemetry divergence before a frame is transmitted. */
    if(rc==1 && (r.legacy_mask!=g_info.assoc_legacy_rate_mask ||
                 r.basic_marker_mask!=g_info.assoc_basic_rate_mask ||
                 r.supp_len!=g_info.assoc_supp_rate_count ||
                 r.ext_len!=g_info.assoc_ext_rate_count))return 26;
    return rc;
}

static int assoc_request_selfcheck(const u8 *f,int len)
{
    int pos,ssid_seen=0,rates_seen=0,reassoc;
    u16 fc;
    if(!f||len<32)return 2;
    fc=rd_le16(f);
    reassoc=((fc&0x00fcu)==0x0020u);
    if((fc&0x00fcu)!=0x0000u && !reassoc)return 3;
    if(!bssid_equal(f+4,g_target.bssid)||!bssid_equal(f+10,g_radio.macaddr)||!bssid_equal(f+16,g_target.bssid))return 4;
    if(reassoc){
        if(len<38)return 15;
        /* Current AP Address is a mandatory fixed field in a Reassociation
         * Request. The oracle path preserves the accepted station's value. */
        if(!bssid_equal(f+28,g_oracle.current_ap))return 16;
        pos=34;
    }else pos=28;
    while(pos+2<=len){
        int id=f[pos],ilen=f[pos+1];const u8 *v;
        pos+=2;if(pos+ilen>len)return 5;v=f+pos;
        if(id==0){
            if(ssid_seen++)return 6;
            if(ilen!=(int)g_wifi_ssid_len||!bytes_equal(v,(const u8 *)g_wifi_ssid,g_wifi_ssid_len))return 7;
        }else if(id==1){
            if(rates_seen++||ilen<1||ilen>8)return 8;
        }else if(id==50 && ilen<1)return 9;
        else if(id==48 && ilen<8)return 10;
        else if(id==45 && ilen!=26)return 11;
        else if(id==46 && ilen!=1)return 17;
        else if(id==70 && ilen!=5)return 14;
        pos+=ilen;
    }
    if(pos!=len)return 12;
    if(!ssid_seen||!rates_seen)return 13;
    return 1;
}

static int send_assoc_request(int profile)
{
    u8 f[256];int p=24,i,rsn_len=0;const u8 *rsn=NULL;u16 cap;
    int use_ht=(profile&1)!=0,use_wmm=0,use_qos=0,ht_mode=0,response_source=0;
    int use_rrm=(profile>=36&&profile<=46),use_power=0,oracle_profile=(profile==44);
    int cap_policy=0; /* 0=normal,1=minimal,2=preamble-only,3=slot-only */
    u16 listen_interval=1;
    int no_rsn=(profile==24||profile==25);
    int oracle_rsn=0;
    int mirror_rsn=(profile==2||profile==3||profile==6||profile==7||profile==10||profile==11||
                    profile==16||profile==17||profile==18||profile==19||profile==42);
    int ap_caps_rsn=(profile==22||profile==23||profile==41);
    int linux_legacy_caps=(profile==20||profile==21);
    int rate_mode=0,n,self;

    if(profile>=4&&profile<=7)rate_mode=TITAN_ASSOC_RATE_MODE_BASIC_ONLY;
    else if(profile>=8&&profile<=11)rate_mode=TITAN_ASSOC_RATE_MODE_AP_COMBINED;
    else if(profile==12||profile==13||profile==16||profile==17)rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;
    else if(profile==14||profile==15||profile==18||profile==19||profile==20||profile==21||
            profile==22||profile==23||profile==24||profile==25)rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;

    /* V9.5 response-driven profiles. */
    if(profile==26||profile==27){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;}
    else if(profile==28||profile==29){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;}
    else if(profile==30||profile==31){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS;}
    else if(profile==32||profile==33){response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS;}
    else if(profile==34){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=0;use_wmm=1;}
    else if(profile==35){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=1;use_wmm=1;ht_mode=1;}
    /* V9.6: all eight V9.5 rate/HT/WMM shapes were explicitly rejected.
     * The screen exposed CAP=1431/0431: bit 0x1000 (Radio Measurement) was
     * the one AP capability never exercised in our request.  36..42 isolate
     * that axis while preserving already-proven full legacy rates. */
    else if(profile==36){response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=0;use_wmm=0;use_power=1;}
    else if(profile==37){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=0;use_wmm=0;use_power=1;}
    else if(profile==38){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=0;use_wmm=1;use_power=1;}
    else if(profile==39){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=1;use_wmm=1;ht_mode=1;use_power=1;}
    else if(profile==40){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=1;use_wmm=1;use_power=1;}
    else if(profile==41||profile==42){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;use_ht=0;use_wmm=0;use_power=1;}
    /* V9.6.0 tested RRM only with Basic markers cleared.  Cross the two
     * dimensions explicitly so a vendor parser that couples Capability
     * Information and rate-marker policy is not left untested. */
    else if(profile==43){response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;use_ht=0;use_wmm=0;use_power=1;}
    else if(profile==45){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;use_ht=0;use_wmm=0;use_power=1;}
    else if(profile==46){response_source=1;rate_mode=TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS;use_ht=0;use_wmm=0;use_power=1;}
    else if(profile==44){response_source=2;rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;
        use_ht=g_oracle.has_ht?1:0;use_wmm=g_oracle.has_wmm?1:0;use_power=g_oracle.has_power?1:0;
        /* Only replay RRM when the accepted request had BOTH the Capability
         * bit and the mandatory-shaped IE 70 that we actually captured. */
        use_rrm=((g_oracle.capability&0x1000u)&&g_oracle.has_rrm_ie)?1:0;
        /* Prefer the exact non-secret RSN selection from the station that the
         * AP accepted. titan_wpa2 validates it against our WPA2-PSK/CCMP scope
         * and falls back to our normal selected RSN if the phone used anything
         * we cannot actually implement (for example SAE or required PMF). */
        oracle_rsn=g_oracle.rsn_ie_len?1:0;
        /* V9.7 captured Listen Interval but accidentally hard-coded 1 here. */
        listen_interval=g_oracle.listen_interval?g_oracle.listen_interval:1;}

    /* V9.7 POLICY SWEEP. These profiles are grouped from the normal Linux
     * station shapes and the common 2.4 GHz compatibility axes: full legacy
     * intersection, HT20/HT40, WMM, AP-exact RSN, Basic markers, conservative
     * Capability Information, and Listen Interval. One ELF tries the lot. */
    if(profile>=47 && profile<=66){
        use_ht=0;use_wmm=0;use_rrm=0;use_power=0;response_source=1;
        rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR;ht_mode=0;cap_policy=0;listen_interval=1;
        mirror_rsn=0;ap_caps_rsn=0;no_rsn=0;
        switch(profile){
            case 47: break;                                      /* Linux legacy */
            case 48: use_ht=1;use_wmm=1;ht_mode=1;break;         /* Linux HT20 */
            case 49: use_ht=1;use_wmm=1;ht_mode=0;break;         /* ath9k full HT */
            case 50: use_ht=1;use_wmm=1;ht_mode=3;break;         /* AP-intersect HT */
            case 51: use_ht=1;use_wmm=1;ht_mode=2;break;         /* conservative HT20 */
            case 52: use_ht=1;use_wmm=1;ht_mode=3;mirror_rsn=1;break;
            case 53: use_wmm=1;break;                            /* legacy + WMM */
            case 54: rate_mode=TITAN_ASSOC_RATE_MODE_AP_EXACT;use_ht=1;use_wmm=1;ht_mode=3;break;
            case 55: cap_policy=1;break;                         /* ESS+privacy only */
            case 56: cap_policy=2;break;                         /* +short preamble */
            case 57: cap_policy=3;break;                         /* +short slot */
            case 58: listen_interval=3;break;
            case 59: listen_interval=10;break;
            case 60: use_rrm=1;use_power=1;break;
            case 61: use_rrm=1;use_power=1;use_ht=1;use_wmm=1;ht_mode=3;break;
            case 62: mirror_rsn=1;break;
            case 63: no_rsn=1;break;                             /* diagnostic only */
            case 64: rate_mode=TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS;break;
            case 65: use_ht=1;use_wmm=1;ht_mode=4;break;         /* old-driver HT20 */
            case 66: use_ht=1;use_wmm=0;ht_mode=1;break;         /* HT20 no WMM probe */
            default:return -12;
        }
    }
    /* V9.9 UPSTREAM-MLME: stop inventing policy permutations. Profiles
     * 90..92 are bounded, deterministic request shapes derived from mature
     * station stacks instead of target-specific guessing:
     *
     *  90 OpenBSD/net80211 AR9271 shape:
     *     - AP-negotiated legacy rates are emitted with their Basic markers;
     *     - RSN -> QoS Capability -> conservative AR9271 HT -> WMM ordering;
     *     - HT capability follows OpenBSD athn's deliberately conservative
     *       baseline (SMPS disabled, MCS0..7; 40 MHz/SGI remain disabled).
     *
     *  91 Linux/mac80211-style HT shape:
     *     - device/AP legacy-rate intersection with Basic markers cleared;
     *     - RSN -> HT -> WMM using the AR9271 capability set.
     *
     *  92 Linux/mac80211-style legacy fallback:
     *     - same mutual legacy-rate construction, no HT/WMM/QoS.
     *
     * This is a production fallback ladder, not a diagnostic policy sweep. */
    if(profile==90){
        response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_AP_COMBINED;
        use_ht=(g_target.ht_present&&g_target.wmm_present)?1:0;
        use_wmm=use_ht;use_qos=use_ht;
        use_rrm=0;use_power=0;ht_mode=2;cap_policy=0;listen_interval=1;
        mirror_rsn=0;ap_caps_rsn=0;no_rsn=0;oracle_rsn=0;
    }else if(profile==91){
        response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_MAC80211;
        use_ht=(g_target.ht_present&&g_target.wmm_present)?1:0;
        use_wmm=use_ht;use_qos=0;
        use_rrm=0;use_power=0;ht_mode=0;cap_policy=0;listen_interval=1;
        mirror_rsn=0;ap_caps_rsn=0;no_rsn=0;oracle_rsn=0;
    }else if(profile==92){
        response_source=0;rate_mode=TITAN_ASSOC_RATE_MODE_MAC80211;
        use_ht=0;use_wmm=0;use_qos=0;use_rrm=0;use_power=0;
        cap_policy=0;listen_interval=1;mirror_rsn=0;ap_caps_rsn=0;
        no_rsn=0;oracle_rsn=0;
    }
    if(use_ht && profile!=66)use_wmm=1;

    g_info.assoc_rsn_mode=0;g_info.assoc_rsn_ie_len=0;g_info.assoc_req_selfcheck=0;
    memset(g_info.assoc_rsn_preview,0,sizeof(g_info.assoc_rsn_preview));
    if(no_rsn){
        g_info.assoc_rsn_mode=3;
    }else if(oracle_rsn){
        if(titan_wpa2_use_station_rsn_ie_compat(&g_wpa,g_oracle.rsn_ie,g_oracle.rsn_ie_len)==0)
            g_info.assoc_rsn_mode=4;
        else{
            titan_wpa2_use_selected_rsn_ie(&g_wpa);
            g_info.assoc_rsn_mode=0;
        }
    }else if(mirror_rsn){
        if(titan_wpa2_use_ap_rsn_ie_compat(&g_wpa)<0)return -2;
        g_info.assoc_rsn_mode=2;
    }else if(ap_caps_rsn){
        if(titan_wpa2_use_selected_ap_caps_rsn_ie_compat(&g_wpa)<0)return -2;
        g_info.assoc_rsn_mode=1;
    }else{
        titan_wpa2_use_selected_rsn_ie(&g_wpa);
        g_info.assoc_rsn_mode=0;
    }
    if(!no_rsn){
        rsn=titan_wpa2_rsn_ie(&g_wpa,&rsn_len);
        if(!rsn||rsn_len<=0)return -1;
        g_info.assoc_rsn_ie_len=(u8)rsn_len;
        n=rsn_len>(int)sizeof(g_info.assoc_rsn_preview)?(int)sizeof(g_info.assoc_rsn_preview):rsn_len;
        memcpy(g_info.assoc_rsn_preview,rsn,n);
    }

    /* AIR ORACLE must replay the same management subtype that the AP accepted.
     * In particular, phones frequently reconnect with a Reassociation Request
     * (subtype 2). V9.8.1 captured those requests but silently converted them
     * to a normal Association Request, defeating the point of the oracle. */
    dot11_header(f,(oracle_profile&&g_oracle.reassoc)?0x0020:0x0000,
                 g_target.bssid,g_radio.macaddr,g_target.bssid);
    if(linux_legacy_caps)
        cap=(u16)(0x0001u | (g_target.capability&0x0010u) | 0x0020u | 0x0400u);
    else
        cap=(u16)(0x0001u | (g_target.capability&(0x0010u|0x0020u|0x0400u)));
    if(cap_policy==1)cap=(u16)(0x0001u|(g_target.capability&0x0010u));
    else if(cap_policy==2)cap=(u16)(0x0001u|(g_target.capability&0x0010u)|0x0020u);
    else if(cap_policy==3)cap=(u16)(0x0001u|(g_target.capability&0x0010u)|0x0400u);
    if(use_rrm)cap|=0x1000u;
    if(oracle_profile){
        /* Never clone the third-party MAC or arbitrary capability bits. Keep
         * only capabilities our AR9271 path can safely represent. RRM is
         * included only when IE 70 was captured and will be emitted below. */
        cap=(u16)(0x0001u | (g_target.capability&0x0010u) |
                  (g_oracle.capability&(0x0020u|0x0400u)));
        if(use_rrm)cap|=0x1000u;
    }
    g_info.assoc_tx_capability=cap;g_info.assoc_listen_interval=listen_interval;
    wr_le16(f+p,cap);p+=2;wr_le16(f+p,listen_interval);p+=2;
    if(oracle_profile&&g_oracle.reassoc){
        if(p+6>(int)sizeof(f))return -1;
        memcpy(f+p,g_oracle.current_ap,6);p+=6;
    }
    f[p++]=0;f[p++]=g_wifi_ssid_len;for(i=0;i<(int)g_wifi_ssid_len;i++)f[p++]=(u8)g_wifi_ssid[i];
    p=append_assoc_rate_ies(f,p,sizeof(f),rate_mode,response_source);if(p<0)return -5;
    /* Match mac80211 ordering: Power Capability is before RSN/HT. */
    if(use_power){
        u8 pmin=oracle_profile&&g_oracle.has_power?g_oracle.power_min:0;
        u8 pmax=oracle_profile&&g_oracle.has_power?g_oracle.power_max:20;
        p=append_power_capability(f,p,sizeof(f),pmin,pmax);if(p<0)return -6;
    }
    if(!no_rsn){
        if(p+rsn_len>(int)sizeof(f))return -1;
        memcpy(f+p,rsn,rsn_len);p+=rsn_len;
    }
    if(use_rrm){
        const u8 *rrm=oracle_profile&&g_oracle.has_rrm_ie?g_oracle.rrm_caps:NULL;
        p=append_rrm_enabled_capabilities(f,p,sizeof(f),rrm);if(p<0)return -7;
    }
    if(use_qos){p=append_qos_capability(f,p,sizeof(f));if(p<0)return -9;}
    if(use_ht){p=append_ar9271_ht_cap_policy(f,p,sizeof(f),ht_mode);if(p<0)return -3;}
    if(use_wmm){p=append_wmm_info(f,p,sizeof(f));if(p<0)return -4;}
    self=assoc_request_selfcheck(f,p);g_info.assoc_req_selfcheck=(u8)self;
    if(self!=1)return -8;
    return tx_mgmt_frame(f,p);
}

static void ccmp_tx_write_iv(u8 out[8],u64 pn)
{
    /* Linux/mac80211 supplies the 8-byte CCMP header to ath9k_htc before the
     * LLC payload. Pairwise traffic uses on-air KeyID 0 even though the AR9271
     * hardware key-cache slot is TITAN_PAIRWISE_KEYIX (4). */
    out[0]=(u8)pn;out[1]=(u8)(pn>>8);out[2]=0;out[3]=0x20u;
    out[4]=(u8)(pn>>16);out[5]=(u8)(pn>>24);out[6]=(u8)(pn>>32);out[7]=(u8)(pn>>40);
}

static int send_llc_payload(const u8 dst[6],u16 ethertype,const u8 *payload,int payload_len,int encrypted)
{
    u8 f[ONLINE_FRAME_MAX];
    int p=28;
    u8 tid=(ethertype==ETH_P_EAPOL)?7u:0u;
    u8 epid=(ethertype==ETH_P_EAPOL)?g_data_vo_endpoint:g_data_be_endpoint;
    static const u8 snap[6]={0xaa,0xaa,0x03,0x00,0x00,0x00};
    if(!dst||!payload||payload_len<0||!epid)return -1;
    if(28+(encrypted?8:0)+8+payload_len>(int)sizeof(f))return -2;

    /* Linux/mac80211 sends QoS Data after association.  AR9271's HTC path
     * keeps the 26-byte 802.11 QoS header 4-byte aligned by inserting two
     * host padding bytes before the crypto/LLC bytes. The successful PCAP
     * shows TID 7 + VO endpoint 9 for clear EAPOL and TID 0 + BE endpoint 6
     * for normal pairwise-CCMP data. */
    memset(f,0,28);
    wr_le16(f,encrypted?0x4188u:0x0188u); /* QoS Data, ToDS, Protected for CCMP */
    memcpy(f+4,g_target.bssid,6);
    memcpy(f+10,g_radio.macaddr,6);
    memcpy(f+16,dst,6);
    wr_le16(f+22,next_qos_seq_ctrl(tid));
    wr_le16(f+24,(u16)tid);
    /* f[26..27] are Linux's alignment pad, not on-air bytes. For encrypted
     * frames Linux then supplies an 8-byte CCMP IV/ExtIV. The target/hardware
     * still performs AES encryption and appends the MIC via key_type/keyix. */
    if(encrypted){
        u64 pn=g_tx.tx_pn;
        if(pn==0 || (pn>>48)!=0)return -3;
        ccmp_tx_write_iv(f+p,pn);p+=8;
        g_tx.tx_pn=pn+1;
    }
    memcpy(f+p,snap,6);p+=6;wr_be16(f+p,ethertype);p+=2;memcpy(f+p,payload,payload_len);p+=payload_len;
    if(ethertype==ETH_P_EAPOL)titan_ar9271_tx_redact_next_wire(&g_tx);
    return tx_data_frame_linux(f,p,encrypted?ATH9K_KEY_TYPE_AES:ATH9K_KEY_TYPE_CLEAR,
                               encrypted?TITAN_PAIRWISE_KEYIX:0xff,epid,tid,1u);
}

static int send_llc_payload_netman(const u8 dst[6],u16 ethertype,const u8 *payload,int payload_len)
{
    u8 f[ONLINE_FRAME_MAX];
    int p=28,rc;
    u8 tid=0u;
    u8 epid=g_data_be_endpoint;
    static const u8 snap[6]={0xaa,0xaa,0x03,0x00,0x00,0x00};
    if(!dst||!payload||payload_len<0||!epid||!g_wpa.secured)return -1;
    if(28+8+8+payload_len>(int)sizeof(f))return -2;
    memset(f,0,28);
    wr_le16(f,0x4188u);
    memcpy(f+4,g_target.bssid,6);
    memcpy(f+10,g_radio.macaddr,6);
    memcpy(f+16,dst,6);
    wr_le16(f+22,next_qos_seq_ctrl(tid));
    wr_le16(f+24,(u16)tid);
    {
        u64 pn=g_tx.tx_pn;
        if(pn==0 || (pn>>48)!=0)return -3;
        ccmp_tx_write_iv(f+p,pn);p+=8;g_tx.tx_pn=pn+1;
    }
    memcpy(f+p,snap,6);p+=6;wr_be16(f+p,ethertype);p+=2;
    memcpy(f+p,payload,payload_len);p+=payload_len;
    /* Socket traffic cannot wait for the tester's 2.5 s WMI TXSTATUS gate
     * on every frame. USB completion transfers ownership; firmware handles
     * the normal 802.11 ACK/retry policy. */
    rc=titan_ar9271_tx_data_linux(&g_tx,f,p,ATH9K_KEY_TYPE_AES,
                                  TITAN_PAIRWISE_KEYIX,1,epid,tid,1u);
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;
    g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;
    g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;
    g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    return rc;
}

static void titan_netman_rx_frame(const u8 dst[6],const u8 src[6],u16 ethertype,
                                  const u8 *payload,int payload_len)
{
    void *packet,*out;
    int total;
    if(!g_netman_bridge_active||g_netman_if_id<0||!payload||payload_len<0)return;
    total=14+payload_len;
    if(total>NETMAN_NETIF_FRAME_SIZE)return;
    packet=NetManNetProtStackAllocRxPacket((unsigned int)total,&out);
    if(!packet||!out)return;
    memcpy(out,dst,6);memcpy((u8*)out+6,src,6);
    ((u8*)out)[12]=(u8)(ethertype>>8);((u8*)out)[13]=(u8)ethertype;
    memcpy((u8*)out+14,payload,payload_len);
    NetManNetProtStackEnQRxPacket(packet);
}

static int titan_netman_init(void){return 0;}
static void titan_netman_deinit(void){g_netman_bridge_active=0;g_netman_link_up=0;}

static void titan_netman_tx_drain(void)
{
    void *frame;
    int len;
    /* NetMan's xmit callback may run from interrupt/SIFCMD context.  All USB
     * waits therefore live here, in a normal IOP worker thread.  Drain the
     * queue even while the bridge is down so a stale packet cannot wedge the
     * protocol stack across reconnect/interface changes. */
    while((len=NetManTxPacketNext(&frame))>0){
        u8 *f=(u8*)frame;
        if(g_netman_bridge_active && g_wpa.secured && f && len>=14){
            u16 et=(u16)(((u16)f[12]<<8)|f[13]);
            (void)send_llc_payload_netman(f,et,f+14,len-14);
        }
        NetManTxPacketDeQ();
    }
}

static void titan_netman_tx_thread(void *arg)
{
    (void)arg;
    for(;;){
        WaitSema(g_netman_tx_sema);
        titan_netman_tx_drain();
    }
}

static void titan_netman_xmit(void)
{
    if(g_netman_tx_sema<0)return;
    if(QueryIntrContext())iSignalSema(g_netman_tx_sema);
    else SignalSema(g_netman_tx_sema);
}

/* Never toggle NetMan link state from the SIF RPC server thread.  Link-up can
 * wake the EE protocol stack, so perform the transition from a normal IOP
 * worker and let the RPC return immediately. */
static void titan_netman_control_thread(void *arg)
{
    (void)arg;
    for(;;){
        int requested;
        WaitSema(g_netman_ctl_sema);
        requested=g_netman_bridge_requested?1:0;
        if(requested){
            g_netman_bridge_active=1;
            titan_netman_set_link(g_info.state==TITAN_SCAN_ONLINE_PASS && g_wpa.secured);
        }else{
            titan_netman_set_link(0);
            g_netman_bridge_active=0;
            /* Dispose of any lwIP packet queued while the interface was down. */
            titan_netman_tx_drain();
        }
    }
}

static int titan_netman_ioctl(unsigned int command,void *args,unsigned int args_len,
                              void *output,unsigned int length)
{
    (void)args;(void)args_len;
    if(command==NETMAN_NETIF_IOCTL_ETH_GET_MAC){
        if(!output||length<6u)return -1;
        memcpy(output,g_netman_mac,6);
        return 0;
    }
    if(command==NETMAN_NETIF_IOCTL_GET_LINK_STATUS)
        return g_netman_link_up?NETMAN_NETIF_ETH_LINK_STATE_UP:NETMAN_NETIF_ETH_LINK_STATE_DOWN;
    if(command==NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE)
        return NETMAN_NETIF_ETH_LINK_MODE_10M_FDX;
    if(command==NETMAN_NETIF_IOCTL_GET_TX_DROPPED_COUNT || command==NETMAN_NETIF_IOCTL_GET_RX_DROPPED_COUNT)
        return 0;
    if(command==NETMAN_NETIF_IOCTL_ETH_GET_STATUS){
        struct NetManEthStatus *st=(struct NetManEthStatus*)output;
        if(!st||length<sizeof(*st))return -1;
        memset(st,0,sizeof(*st));
        st->LinkStatus=(u8)(g_netman_link_up?1:0);
        st->LinkMode=NETMAN_NETIF_ETH_LINK_MODE_10M_FDX;
        return 0;
    }
    return -1;
}

static int titan_netman_register(void)
{
    static struct NetManNetIF device={"WFI",NETMAN_NETIF_ETHERNET,0,
        &titan_netman_init,&titan_netman_deinit,&titan_netman_xmit,
        &titan_netman_ioctl,0};
    if(g_netman_if_id>=0)return 1;
    g_netman_if_id=NetManRegisterNetIF(&device);
    return g_netman_if_id>=0;
}

static void titan_netman_set_link(int up)
{
    int requested=up?1:0;
    if(g_netman_if_id<0)return;
    g_netman_link_up=requested;
    NetManToggleNetIFLinkState(g_netman_if_id,
        (u8)(requested?NETMAN_NETIF_ETH_LINK_STATE_UP:NETMAN_NETIF_ETH_LINK_STATE_DOWN));
}

static int send_eapol(const u8 *eapol,int len)
{
    return send_llc_payload(g_target.bssid,ETH_P_EAPOL,eapol,len,0);
}

static void seed_wpa_entropy(u8 entropy[16])
{
    iop_sys_clock_t t;u32 a,b,c,d;
    memset(&t,0,sizeof(t));GetSystemTime(&t);
    a=t.lo ^ g_info.rx_bytes ^ ((u32)g_info.rx_completions<<1);
    b=t.hi ^ g_info.tgt_rx_host_send ^ ((u32)g_target.channel<<24);
    c=g_info.beacons_seen ^ g_info.hif_packets ^ ((u32)g_target.rssi_dbm<<16);
    d=((u32)g_radio.macaddr[0]<<24)|((u32)g_radio.macaddr[2]<<16)|((u32)g_target.bssid[4]<<8)|g_target.bssid[5];
    entropy[0]=(u8)a;entropy[1]=(u8)(a>>8);entropy[2]=(u8)(a>>16);entropy[3]=(u8)(a>>24);
    entropy[4]=(u8)b;entropy[5]=(u8)(b>>8);entropy[6]=(u8)(b>>16);entropy[7]=(u8)(b>>24);
    entropy[8]=(u8)c;entropy[9]=(u8)(c>>8);entropy[10]=(u8)(c>>16);entropy[11]=(u8)(c>>24);
    entropy[12]=(u8)d;entropy[13]=(u8)(d>>8);entropy[14]=(u8)(d>>16);entropy[15]=(u8)(d>>24);
}

static int station_prepare(void)
{
    int rc;u8 entropy[16];TitanMlmeBssInput bss;
    /* Never render zero-initialized telemetry as a fake AUTH/ASSOC success
     * while the station VAP is only being created. */
    g_info.auth_status=0xffff;g_info.assoc_status=0xffff;g_info.assoc_first_status=0xffff;
    g_info.aid=0;g_info.assoc_profile=0xff;
    g_info.auth_tx_rate=0xff;g_info.assoc_tx_rate=0xff;
    memcpy(g_info.local_mac,g_radio.macaddr,6);
    g_info.state=TITAN_SCAN_STATION_INIT;set_detail("target found; replacing monitor VAP with station VAP");
    if(open_bulk_out_pipe()<0)return -1;
    if(!g_mgmt_endpoint||!g_data_be_endpoint)return -2;
    if(wmi_remove_monitor_target()<0)return -3;
    /* Historical V10.9 path: reproduce the successful Linux lifecycle instead of creating the
     * station VAP after the final reset.  Linux restarts the target, starts RX
     * with 0x0207, sends TARGET_IC_UPDATE, creates VAP/local-node, and only
     * then performs the final channel reset that programs station RX=0x1607. */
    rc=titan_ar9271_radio_restart_station_base(&g_radio,g_target.channel);if(rc<0)return -10+rc;
    if(wmi_create_station_vif()<0)return -30;
    /* PV1412: UL1411 proved that its six-command semantic subset is not
     * sufficient (AUTH=0, explicit ASSOC=18).  Fresh Linux-success usbmon and
     * the historical Linux-success trace both execute the same complete
     * 168-command register phase here.  Run that bounded phase with LIVE WMI
     * sequence numbers/responses, then enter the existing live reset path.
     * Do not insert update_radio_diag() inside this critical gap. */
    set_detail("TT1415 TARGET-TXDBG-SPLIT: 3x32-byte debug events + PR1413 radio path");
    rc=run_linux_postvif_168_live();
    if(rc<0)return -40+rc;

    /* PR1413 controlled A/B: PV1412 still received explicit Association
     * status 18 after the full stable 168-command post-VIF phase.  The fresh
     * successful Linux usbmon path then performs FOUR complete channel
     * reset/start passes before Open-System Authentication, while PV1412 did
     * only one.  Preserve the now-proven POSTVIF-168 phase and reproduce that
     * measured reset history without changing MLME, rates, RSN, HT, WMM,
     * station identity, or the Association Request bytes. */
    {
        int reset_pass;
        for(reset_pass=1;reset_pass<=4;++reset_pass){
            titan_fr_mark(0x52344247u,(u32)reset_pass,g_radio.last_reg,g_radio.last_reg_val); /* R4BG */
            rc=titan_ar9271_radio_start_station(&g_radio,g_target.channel);
            if(rc<0){
                titan_fr_mark(0x52344552u,(u32)reset_pass,(u32)rc,g_radio.last_reg); /* R4ER */
                return -50+rc;
            }
            titan_fr_mark(0x52344f4bu,(u32)reset_pass,g_radio.last_reg,g_radio.last_reg_val); /* R4OK */
        }
    }

    /* V10.13 conclusive live-BSS gate. The final station reset clears the BSSID
     * registers before association, so temporarily widen RX exactly long enough
     * to prove that the selected AP is still arriving through the newly-reset
     * live PHY. No trace-derived context is allowed to satisfy this gate. */
    g_target_live_bss=0;
    g_target_live_gate_pass=0;
    g_target_live_gate_active=1;
    if(titan_ar9271_radio_set_promisc(&g_radio)<0){
        g_target_live_gate_active=0;
        return -71;
    }
    {
        int gate_wait;
        for(gate_wait=0;gate_wait<150 && g_target_live_bss<2u;++gate_wait)DelayThread(10000);
    }
    g_target_live_gate_active=0;
    if(titan_ar9271_radio_set_station_rx(&g_radio)<0)return -72;
    if(g_target_live_bss<2u){
        update_radio_diag();
        return -73;
    }
    g_target_live_gate_pass=1;
    update_radio_diag();
    if(titan_ar9271_tx_init(&g_tx,g_bulk_out_pipe,g_mgmt_endpoint,g_data_be_endpoint)<0)return -31;
    titan_ar9271_tx_set_nodes(&g_tx,0,0,1);
    memcpy(g_info.local_mac,g_radio.macaddr,6);
    seed_wpa_entropy(entropy);titan_wpa2_init(&g_wpa,g_wifi_pmk,g_radio.macaddr,g_target.bssid,entropy);memset(entropy,0,sizeof(entropy));
    if(titan_wpa2_set_ap_rsn_ie(&g_wpa,g_target.rsn_ie,g_target.rsn_ie_len)<0)return -32;

    /* V10.0: join the BSS once and freeze one coherent station view before
     * authentication. This replaces profile-by-profile frame mutation with a
     * real MLME boundary: scan result -> validated BSS -> auth -> assoc. */
    memset(&bss,0,sizeof(bss));
    bss.bssid=g_target.bssid;bss.ssid=(const u8 *)g_wifi_ssid;bss.ssid_len=g_wifi_ssid_len;
    bss.channel=g_target.channel;bss.capability=g_target.capability;
    bss.supp_rates=g_target.supp_rates;bss.supp_rate_count=g_target.supp_rate_count;
    bss.ext_rates=g_target.ext_rates;bss.ext_rate_count=g_target.ext_rate_count;
    bss.ht_present=g_target.ht_present;bss.ht_cap=g_target.ht_cap;
    bss.ht_op_present=g_target.ht_op_present;bss.ht_op=g_target.ht_op;bss.wmm_present=g_target.wmm_present;
    rc=titan_mlme_join(&g_mlme,g_radio.macaddr,&bss);
    if(rc<0)return -40+rc;
    mlme_sync_assoc_telemetry();

    /* Upstream ath9k_htc TX chooses vif_sta_pos[vif] whenever mac80211 has no
     * ieee80211_sta object for the destination. During AUTH and the outgoing
     * ASSOC request that AP sta object does not exist yet, so both frames must
     * use the local VIF station node (node 0). V10.0/V10.1 incorrectly created
     * peer node 1 early and put pre-association management TX on that node. */
    titan_ar9271_tx_set_mgmt_node(&g_tx,0);
    return 0;
}

static int station_authenticate(void)
{
    int attempt,txrc,waited;
    g_info.state=TITAN_SCAN_AUTHENTICATING;g_info.auth_status=0xffff;
    set_detail("802.11 MLME: Open-System authentication");
    for(attempt=0;attempt<TITAN_MLME_MAX_AUTH_TRIES;attempt++){
        g_auth_seen=0;g_auth_status=0xffff;
        /* Linux frees TX slot 0 after the synchronous Auth TXSTATUS and reuses
         * slot/cookie 0 for Association.  Reuse that captured state exactly. */
        g_tx.cookie=0;
        txrc=send_auth_request();
        /* mac80211/net80211-style bounded retry: retry silence/transport
         * uncertainty, but never churn after an explicit AP rejection. */
        if(txrc<0 && g_tx.transport_dead)return -3;
        waited=g_auth_seen?1:0;
        if(!waited && !g_tx.transport_dead)waited=wait_event(&g_auth_seen,1800);
        if(waited){
            g_info.auth_status=g_auth_status;
            return g_auth_status==0?0:-2;
        }
        if(g_tx.transport_dead)return -3;
        if(attempt+1<TITAN_MLME_MAX_AUTH_TRIES)DelayThread(180000);
    }
    return -1;
}

#define ASSOC_HIST_AUTH_FAIL 0xfdu
#define ASSOC_HIST_TX_FAIL   0xfeu
#define ASSOC_HIST_NO_RESP   0xffu
#define ASSOC_HIST_MAX       12

static void assoc_hist_add(u8 profile,u8 result,u8 txair)
{
    int n=g_info.assoc_hist_count,i;
    /* Keep the most recent 12 results. A wide one-shot policy sweep is more
     * useful if the final screen shows the profiles nearest the outcome. */
    if(n>=ASSOC_HIST_MAX){
        for(i=1;i<ASSOC_HIST_MAX;i++){
            g_info.assoc_hist_profile[i-1]=g_info.assoc_hist_profile[i];
            g_info.assoc_hist_result[i-1]=g_info.assoc_hist_result[i];
            g_info.assoc_hist_tx[i-1]=g_info.assoc_hist_tx[i];
        }
        n=ASSOC_HIST_MAX-1;
    }
    g_info.assoc_hist_profile[n]=profile;
    g_info.assoc_hist_result[n]=result;
    g_info.assoc_hist_tx[n]=txair;
    if(g_info.assoc_hist_count<ASSOC_HIST_MAX)++g_info.assoc_hist_count;
}

/* A second Authentication frame is not a true clean slate.  V9.2.3 showed
 * one real rejection followed by four silent probes.  Before every independent
 * fallback we now explicitly deauthenticate, wait for the AP to retire the
 * old station state, then perform a fresh Open-System authentication. */
static int station_fresh_auth_for_assoc(void)
{
    int attempt,txrc;
    (void)send_deauth_request_nowait();
    g_auth_seen=0;g_auth_status=0xffff;g_assoc_seen=0;g_assoc_status=0xffff;g_assoc_aid=0;
    /* V9.3 proved that the target answers clean association attempts, but the
     * 16-profile matrix eventually drove AUTH into a reject/throttle state and
     * accumulated many local USB errors.  V9.4 deliberately cools down longer
     * and allows only two auth transmissions before stopping the matrix. */
    DelayThread(900000);
    for(attempt=0;attempt<2;attempt++){
        g_auth_seen=0;g_auth_status=0xffff;
        txrc=send_auth_request();
        if(g_auth_seen){
            g_info.auth_status=g_auth_status;
            if(g_auth_status==0){DelayThread(220000);return 0;}
        }
        if(g_tx.transport_dead)return -2;
        if(txrc>=0 && wait_event(&g_auth_seen,2500)){
            g_info.auth_status=g_auth_status;
            if(g_auth_status==0){DelayThread(220000);return 0;}
        }
        if(attempt==0)DelayThread(1500000);
    }
    return -1;
}

static int station_air_oracle(void)
{
    int i,rc,accepted,cleanup_rc,rx_rc;
    memset(&g_oracle,0,sizeof(g_oracle));
    g_info.assoc_diag_flags&=0x01u;
    set_detail("AIR ORACLE: adding monitor VAP #1 beside live station VAP #0");

    /* V9.8.0 hot-swapped STA->MON by removing VAP 0 and then calling the
     * cold monitor start path.  On real hardware that path can issue
     * WMI_START_RECV while receive is already active, producing the observed
     * -879 before the oracle window begins.  Keep STA VAP 0 alive instead and
     * use the firmware's second VIF slot for a temporary monitor interface. */
    rc=wmi_create_oracle_monitor_vif();
    if(rc==-1)return -1;
    if(rc==-2)return -2;

    /* A monitor VAP alone does not widen the AR9271 host-side RX filter in our
     * minimal port, so explicitly enable the same filter that already proved
     * passive scanning on the PS2.  No reset/channel switch is required: the
     * station is already locked to g_target.channel. */
    if(titan_ar9271_radio_set_promisc(&g_radio)<0){
        (void)wmi_remove_oracle_monitor_vif();
        (void)titan_ar9271_radio_set_station_rx(&g_radio);
        return -3;
    }
    g_info.current_channel=g_target.channel;
    memset(&g_oracle,0,sizeof(g_oracle));g_oracle.active=1;
    set_detail("FULLASSOC fallback 30s: toggle Wi-Fi OFF/ON once on a phone already connected to this SSID");
    for(i=0;i<300;i++){
        if(g_oracle.accepted)break;
        if(g_dev_id<0)break;
        DelayThread(100000);
    }
    g_oracle.active=0;accepted=g_oracle.accepted?1:0;

    /* Tear down only VIF/node #1; VIF/node #0 remain our station the whole
     * time.  Always restore the narrow station filter, even after teardown
     * errors, so subsequent diagnostics are deterministic. */
    cleanup_rc=wmi_remove_oracle_monitor_vif();
    rx_rc=titan_ar9271_radio_set_station_rx(&g_radio);
    titan_ar9271_tx_set_nodes(&g_tx,0,0,1);
    if(cleanup_rc==-1)return -4;
    if(cleanup_rc==-2)return -5;
    if(rx_rc<0)return -6;

    if(!accepted){
        set_detail("AIR ORACLE saw no accepted Assoc/Reassoc exchange; automatic probes exhausted");
        return 0;
    }
    set_detail("AIR ORACLE learned an AP-accepted station shape; retrying our MAC safely");
    return 1;
}

/* V9.5 target result:
 *  - 8/8 Association Requests received explicit status 18;
 *  - AP/response exact rates, Basic clear/preserved, Basic-only, WMM and HT20
 *    were all rejected;
 *  - transport remained perfect (23/23 air ACK, zero local USB errors);
 *  - the screen exposed CAP=1431/0431, a previously untested 0x1000 Radio
 *    Measurement capability delta. V9.6 attacks that concrete delta first,
 *    then can passively learn the shape of an association that this AP really
 *    accepted from another station, without copying its MAC or credentials. */
/* V9.4 target result:
 *  - 6/6 clean Association Requests received explicit status 18;
 *  - selected RSN, AP-exact RSN, no-RSN, HT and non-HT all received 18;
 *  - USB/TX was completely clean (17 submissions, 17 air ACKs, zero local
 *    transport errors/timeouts).
 * Therefore V9.5 stops using status 18 as a literal instruction to invent
 * another generic legacy-rate permutation. The Association Response itself
 * contains Supported/Extended Rates, so we capture those bytes and drive the
 * remaining compatibility probes from the AP's own rejection frame. */
static int finish_successful_association(void)
{
    int rc;
    rc=titan_ar9271_radio_set_assoc(&g_radio,g_target.bssid,g_assoc_aid);if(rc<0)return -3;
    if(wmi_create_ap_station()<0)return -4;
    titan_ar9271_tx_set_nodes(&g_tx,0,0,1);
    titan_ar9271_tx_set_mgmt_node(&g_tx,1);
    /* The pre-association trace already proved the TT1417 MPS64 fix. Start a
     * fresh recorder here so the full WPA/DHCP path has the entire RAM budget. */
    titan_fr_reset();
    titan_fr_record(TITAN_FR_EV_ASSOC_RESULT,0,0,(u32)g_info.assoc_resp_capability,0u,
                    (u32)g_assoc_aid,(u32)g_info.assoc_resp_ie_len,NULL,0,0);
    titan_fr_mark(0x57504153u,(u32)g_assoc_aid,(u32)g_info.bulk_out_open_mode,(u32)g_info.assoc_wire_len); /* WPAS */
    set_detail("802.11 ASSOCIATED; entering WPA2 4-way handshake");
    return 0;
}

static int station_associate_once(u8 profile,int max_silence_tries)
{
    int attempt,txrc,waited;
    u16 first_status=0xffff;
    g_info.assoc_profile=profile;
    for(attempt=0;attempt<max_silence_tries;attempt++){
        g_assoc_seen=0;g_assoc_status=0xffff;g_assoc_aid=0;
        g_info.assoc_status=0xffff;g_info.aid=0;g_info.assoc_resp_capability=0;
        g_info.assoc_resp_ie_len=0;memset(g_info.assoc_resp_preview,0,sizeof(g_info.assoc_resp_preview));
        ++g_info.assoc_attempts;
        /* Auth already completed and its TXSTATUS has been consumed, so Linux
         * reuses management TX slot/cookie 0 here as seen in the success PCAP. */
        g_tx.cookie=0;
        txrc=send_mlme_assoc_request();
        if(g_info.assoc_req_selfcheck>1)return -8;
        waited=g_assoc_seen?1:0;
        if(!waited && !g_tx.transport_dead)waited=wait_event(&g_assoc_seen,5000);
        if(!waited){
            if(g_tx.transport_dead)return -7;
            assoc_hist_add(profile,txrc<0?ASSOC_HIST_TX_FAIL:ASSOC_HIST_NO_RESP,g_last_mgmt_air);
            ++g_info.assoc_timeouts;
            if(attempt+1<max_silence_tries)DelayThread(200000);
            continue;
        }
        ++g_info.assoc_responses;
        g_info.assoc_status=g_assoc_status;g_info.aid=g_assoc_aid;
        assoc_hist_add(profile,(u8)(g_assoc_status<=0xfcu?g_assoc_status:0xfcu),g_last_mgmt_air);
        if(g_info.assoc_first_status==0xffff){first_status=g_assoc_status;g_info.assoc_first_status=first_status;}
        if(g_assoc_status==0)return finish_successful_association();
        return -2; /* explicit status is authoritative */
    }
    return -1;
}

static int station_associate(void)
{
    int rc;
    const u8 profile_ps2linux=210;

    g_info.state=TITAN_SCAN_ASSOCIATING;g_info.assoc_status=0xffff;
    g_info.assoc_first_status=0xffff;g_info.assoc_profile=profile_ps2linux;g_info.assoc_attempts=0;
    g_info.assoc_responses=0;g_info.assoc_timeouts=0;g_info.assoc_hist_count=0;
    g_info.assoc_diag_flags=0;g_info.assoc_resp_capability=0;g_info.assoc_resp_ie_len=0;
    g_info.assoc_resp_supp_rate_count=0;g_info.assoc_resp_ext_rate_count=0;
    g_info.assoc_resp_legacy_rate_mask=0;g_info.assoc_resp_basic_rate_mask=0;
    g_info.assoc_req_selfcheck=0;g_info.assoc_adapt_flags=0;
    g_info.assoc_wire_len=0;g_info.assoc_wire_hash=0;g_info.online_diag_reserved=0;
    g_info.assoc_tx_rate=0xff;
    g_assoc_template_mode=0;titan_mlme_clear_reassoc(&g_mlme);
    memset(&g_oracle,0,sizeof(g_oracle));
    memset(g_info.assoc_resp_preview,0,sizeof(g_info.assoc_resp_preview));
    memset(g_info.assoc_resp_supp_rates,0,sizeof(g_info.assoc_resp_supp_rates));
    memset(g_info.assoc_resp_ext_rates,0,sizeof(g_info.assoc_resp_ext_rates));
    memset(g_info.assoc_hist_profile,0,sizeof(g_info.assoc_hist_profile));
    memset(g_info.assoc_hist_result,0,sizeof(g_info.assoc_hist_result));
    memset(g_info.assoc_hist_tx,0,sizeof(g_info.assoc_hist_tx));

    /* V10.13 deliberately has no Association matrix.  After the live reset,
     * EEPROM/PLL/PHY/TX-power path and post-reset BSS gate have passed, submit
     * exactly one immutable PS2-MAC Linux-gold request.  A status response is
     * evidence, not a trigger for another byte/IE guess. */
    titan_mlme_clear_reassoc(&g_mlme);
    set_detail("ATH9K-LIVE: one exact PS2-MAC Linux-gold Association attempt");
    rc=station_associate_once(profile_ps2linux,1);
    return rc;
}

/* Linux cold-start capture seq3946..3953, immediately after PTK/GTK
 * programming. These register operations are static (no key material) and are
 * replayed with the live WMI sequence so the post-key target state matches the
 * successful ath9k_htc run. */
static int linux_trace_postkey_sync(void)
{
    static const u8 r803c[4]={0x00,0x00,0x80,0x3c};
    static const u8 r810c[4]={0x00,0x00,0x81,0x0c};
    static const u8 rmw34[12]={0x00,0x00,0x00,0x34, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x10};
    static const u8 wrf[16]={0x00,0x00,0x80,0x3c, 0x00,0x00,0x16,0x07,
                             0x00,0x00,0x81,0x0c, 0x00,0x00,0x00,0x00};
    static const char *const detail[3][4]={
        {"TT1423 post-key R0 S1 READ 803c","TT1423 post-key R0 S2 READ 810c",
         "TT1423 post-key R0 S3 RMW 0034","TT1423 post-key R0 S4 WRITE 803c/810c"},
        {"TT1423 post-key R1 S1 READ 803c","TT1423 post-key R1 S2 READ 810c",
         "TT1423 post-key R1 S3 RMW 0034","TT1423 post-key R1 S4 WRITE 803c/810c"},
        {"TT1423 post-key R2 S1 READ 803c","TT1423 post-key R2 S2 READ 810c",
         "TT1423 post-key R2 S3 RMW 0034","TT1423 post-key R2 S4 WRITE 803c/810c"}
    };
    int round,n=0,rc=0,fail=0;
    u8 rsp[16];
    titan_fr_mark(0x504b5354u,(u32)g_info.assoc_profile,(u32)g_wmi_seq,0u); /* PKST */
    g_postkey_bounded_wmi_wait=1;
    for(round=0;round<3;round++){
#define PK_STEP(STEP,CALL,FAILCODE) do { \
        set_detail(detail[round][(STEP)-1]); \
        titan_fr_mark(0x504b5052u,(u32)round,(u32)(STEP),(u32)(g_wmi_seq+1u)); \
        rc=(CALL); \
        titan_fr_mark(0x504b4f4bu,(u32)round,(u32)(STEP),(u32)rc); \
        if(rc<0){fail=(FAILCODE);goto out;} \
    } while(0)
        PK_STEP(1,issue_wmi(WMI_REG_READ_CMDID,r803c,4,rsp,sizeof(rsp),&n),-1);
        PK_STEP(2,issue_wmi(WMI_REG_READ_CMDID,r810c,4,rsp,sizeof(rsp),&n),-2);
        PK_STEP(3,issue_wmi(WMI_REG_RMW_CMDID,rmw34,12,NULL,0,&n),-3);
        PK_STEP(4,issue_wmi(WMI_REG_WRITE_CMDID,wrf,16,NULL,0,&n),-4);
#undef PK_STEP
    }
    titan_fr_mark(0x504b444eu,3u,(u32)g_wmi_seq,0u); /* PKDN */
    set_detail("TT1423 post-key 3-round gold sync complete; entering secured data path");
out:
    g_postkey_bounded_wmi_wait=0;
    return fail;
}

static int wpa_install_authenticated_keys(void)
{
    u8 keyid=0,gtk_local[32];const u8 *tk,*gtk;
    tk=titan_wpa2_tk(&g_wpa);gtk=titan_wpa2_gtk(&g_wpa,&keyid);
    if(!tk||!gtk){g_info.wpa_last_error=-4;return -4;}
    if(titan_wpa2_ptk_install_needed(&g_wpa)){
        if(titan_ar9271_radio_set_ccmp_key(&g_radio,TITAN_PAIRWISE_KEYIX,tk,g_target.bssid)<0){
            g_info.wpa_last_error=-5;return -5;
        }
        titan_wpa2_mark_ptk_installed(&g_wpa);
        /* mac80211 starts the host-supplied pairwise CCMP PN at 1 for a newly
         * installed PTK. The AR9271 hardware still performs AES/MIC; this PN
         * only fills the CCMP IV/ExtIV bytes carried in the TX skb. */
        g_tx.tx_pn=1;
        g_info.pairwise_installed=1;
    }
    if(keyid>3){g_info.wpa_last_error=-6;return -6;}
    if(titan_wpa2_gtk_install_needed(&g_wpa)){
        if(titan_wpa2_group_cipher(&g_wpa)==TITAN_RSN_CIPHER_TKIP){
            int krc;
            if(titan_wpa2_gtk_len(&g_wpa)!=32||titan_wpa2_build_tkip_local_gtk(&g_wpa,gtk_local)<0){g_info.wpa_last_error=-7;return -7;}
            krc=titan_ar9271_radio_set_tkip_group_key(&g_radio,keyid,gtk_local);
            memset(gtk_local,0,sizeof(gtk_local));
            if(krc<0){g_info.wpa_last_error=-8;return -8;}
        }else{
            if(titan_wpa2_gtk_len(&g_wpa)!=16){g_info.wpa_last_error=-7;return -7;}
            if(titan_ar9271_radio_set_ccmp_key(&g_radio,keyid,gtk,NULL)<0){g_info.wpa_last_error=-8;return -8;}
        }
        titan_wpa2_mark_gtk_installed(&g_wpa);g_info.gtk_installed=1;g_info.gtk_keyid=keyid;
        g_info.gtk_len=(u8)titan_wpa2_gtk_len(&g_wpa);
    }
    if(!g_wpa.secured){g_info.wpa_last_error=-9;return -9;}
    titan_fr_record(TITAN_FR_EV_WPA,0,0,5u,(u32)g_info.pairwise_installed,(u32)g_info.gtk_installed,
                    ((u32)g_info.gtk_keyid<<8)|(u32)g_info.gtk_len,NULL,0,0);
    if(g_info.assoc_profile==190 || g_info.assoc_profile==200 || g_info.assoc_profile==201 || g_info.assoc_profile==202 || g_info.assoc_profile==210){
        int src=linux_trace_postkey_sync();
        if(src<0){g_info.wpa_last_error=-10+src;return -10+src;}
    }
    return 0;
}

static int wpa_mark_secured(void)
{
    if(!g_wpa.secured)return -1;
    g_info.state=TITAN_SCAN_WPA_SECURED;
    titan_fr_record(TITAN_FR_EV_WPA,0,0,6u,(u32)g_info.wpa_m1_seen,(u32)g_info.wpa_m3_seen,
                    ((u32)g_info.wpa_m2_sent<<8)|(u32)g_info.wpa_m4_sent,NULL,0,0);
    set_detail(titan_wpa2_group_cipher(&g_wpa)==TITAN_RSN_CIPHER_TKIP?
               "WPA2-PSK secured: CCMP pairwise + TKIP group; DHCP":
               "WPA2-PSK/CCMP secured; starting DHCP");
    return 0;
}

static int wpa_handshake(void)
{
    u8 reply[TITAN_WPA2_EAPOL_MAX],rx_eapol[EAPOL_EVENT_MAX];
    int rlen,res,loops=0,m4_uncertain=0,m4_uncertain_since=0;
    g_info.state=TITAN_SCAN_WPA_HANDSHAKE;g_info.wpa_message=0;g_info.wpa_last_error=0;
    g_info.wpa_m1_seen=0;g_info.wpa_m2_sent=0;g_info.wpa_m3_seen=0;g_info.wpa_m4_sent=0;g_info.gtk_len=0;
    set_detail("waiting for WPA2 4-way handshake M1");
    while(loops++<2000){
        int len;
        if(g_dev_id<0){g_info.wpa_last_error=-1;return -1;}

        /* The AR9271 firmware TXSTATUS path has already produced false/late
         * no-ACK indications on real hardware.  If M4 was submitted after a
         * cryptographically valid M3 and the keys are installed, keep a short
         * grace window alive. A duplicate M3 causes a fresh M4; no duplicate
         * for ~3 s means the AP most likely accepted the original M4 and DHCP
         * becomes the next end-to-end proof instead of forcing another ELF. */
        if(m4_uncertain && g_wpa.secured && (loops-m4_uncertain_since)>=300){
            return wpa_mark_secured();
        }

        len=take_eapol(rx_eapol,sizeof(rx_eapol));
        if(len>0){
            rlen=0;res=titan_wpa2_rx_key(&g_wpa,rx_eapol,len,reply,sizeof(reply),&rlen);
            if(res==TITAN_WPA2_SEND_M2){
                if(g_info.wpa_m1_seen!=0xff)++g_info.wpa_m1_seen;
                titan_fr_record(TITAN_FR_EV_WPA,0,0,1u,(u32)len,(u32)rlen,(u32)g_info.wpa_m1_seen,NULL,0,0);
                g_info.wpa_message=2;set_detail("WPA2 M1 verified; sending M2");
                {
                    int txrc=send_eapol(reply,rlen);
                    titan_fr_record(TITAN_FR_EV_WPA,0,txrc,2u,(u32)rlen,(u32)g_info.wpa_m1_seen,
                                    (u32)g_info.wpa_m2_sent+1u,NULL,0,0);
                    if(txrc<0 && txrc!=-20){g_info.wpa_last_error=-2;return -2;}
                    /* A no-ACK is retryable: either M2 was actually received
                     * and M3 is already on RX, or the AP will retransmit M1. */
                    if(txrc==-20)g_info.wpa_last_error=-20;
                }
                if(g_info.wpa_m2_sent!=0xff)++g_info.wpa_m2_sent;
                set_detail("WPA2 M2 submitted; waiting for M3/retransmission");
            }else if(res==TITAN_WPA2_SEND_M4){
                int txrc,irc;
                if(g_info.wpa_m3_seen!=0xff)++g_info.wpa_m3_seen;
                titan_fr_record(TITAN_FR_EV_WPA,0,0,3u,(u32)len,(u32)rlen,(u32)g_info.wpa_m3_seen,NULL,0,0);
                g_info.wpa_message=4;set_detail("WPA2 M3 MIC/RSN/GTK valid; sending M4");

                /* M3 is authenticated before titan_wpa2 returns SEND_M4. Install
                 * each genuinely new PTK/GTK exactly once; duplicate M3 frames
                 * only retransmit M4 and never reset PN/RSC state. */
                txrc=send_eapol(reply,rlen);
                titan_fr_record(TITAN_FR_EV_WPA,0,txrc,4u,(u32)rlen,(u32)g_info.wpa_m3_seen,
                                (u32)g_info.wpa_m4_sent+1u,NULL,0,0);
                if(txrc<0 && txrc!=-20){g_info.wpa_last_error=-3;return -3;}
                if(g_info.wpa_m4_sent!=0xff)++g_info.wpa_m4_sent;
                irc=wpa_install_authenticated_keys();if(irc<0)return irc;

                if(txrc==0){
                    m4_uncertain=0;
                    return wpa_mark_secured();
                }

                /* Firmware said no-ACK/late status. Keep listening for a
                 * retransmitted M3 and answer it again instead of prematurely
                 * leaving the WPA state machine. */
                g_info.wpa_last_error=-20;
                m4_uncertain=1;m4_uncertain_since=loops;
                set_detail("WPA2 M4 TXSTATUS uncertain; waiting for duplicate M3/grace");
            }else if(res<0){
                titan_fr_record(TITAN_FR_EV_WPA,0,res,0xffu,(u32)len,(u32)g_info.wpa_message,0,NULL,0,0);
                /* Preserve the exact parser/crypto error for the final screen;
                 * retransmissions remain allowed until the handshake timeout. */
                g_info.wpa_last_error=res;
            }
        }
        DelayThread(10000);
    }
    if(!g_info.wpa_last_error)g_info.wpa_last_error=-8;
    return -8;
}

static void sync_ip_info(void)
{
    memcpy(g_info.ip_addr,g_net.ip,4);memcpy(g_info.netmask,g_net.mask,4);memcpy(g_info.gateway,g_net.gateway,4);memcpy(g_info.dns,g_net.dns,4);memcpy(g_info.gateway_mac,g_net.gateway_mac,6);
}

static int send_net_packet(const u8 dst[6],u16 et,const u8 *p,int len)
{
    return send_llc_payload(dst,et,p,len,1);
}

static int wait_net_event_wanted(int wanted,int timeout_ms,u8 *scratch,int scratch_cap)
{
    int loops=timeout_ms/10;
    while(loops-->0){
        int ev=g_net_event;
        if(ev==wanted)return 1;
        if(ev==TITAN_NET_ARP_REQUEST){
            u8 dst[6];int n;
            g_net_event=TITAN_NET_NONE;
            n=titan_net_arp_reply(&g_net,scratch,scratch_cap,dst);
            if(n>0)send_net_packet(dst,ETH_P_ARP,scratch,n);
        }else if(ev!=TITAN_NET_NONE){
            /* Unrelated asynchronous network events must not terminate the
             * wait for DHCP/ARP/ICMP. */
            g_net_event=TITAN_NET_NONE;
        }
        DelayThread(10000);
    }
    return 0;
}

static int online_ipv4(void)
{
    u8 p[640],bcast[6]={0xff,0xff,0xff,0xff,0xff,0xff};int len,attempt;
    u32 seed=((u32)g_info.rx_bytes<<1)^g_info.tgt_rx_host_done^0x92715449u;
    g_info.dhcp_discovers=0;g_info.dhcp_offers=0;g_info.dhcp_requests=0;g_info.dhcp_acks=0;
    g_info.arp_probes=0;g_info.arp_resolved=0;g_info.online_retries=0;g_info.ping_sent=0;g_info.ping_replies=0;
    titan_net_init(&g_net,g_radio.macaddr,seed);
    g_info.state=TITAN_SCAN_DHCP;g_info.dhcp_state=1;
    titan_fr_record(TITAN_FR_EV_NET,0,0,1u,0,0,0,NULL,0,0);
    set_detail("DHCP DISCOVER over encrypted Wi-Fi");
    for(attempt=0;attempt<5;attempt++){
        g_net_event=TITAN_NET_NONE;len=titan_net_dhcp_discover(&g_net,p,sizeof(p));if(len<0)return -1;
        if(g_info.dhcp_discovers!=0xff)++g_info.dhcp_discovers;
        {int txrc=send_net_packet(bcast,ETH_P_IP,p,len);if(txrc<0&&txrc!=-20)return -2;}
        ++g_info.online_retries;
        if(wait_net_event_wanted(TITAN_NET_DHCP_OFFER,2500,p,sizeof(p))){if(g_info.dhcp_offers!=0xff)++g_info.dhcp_offers;break;}
    }
    if(g_net_event!=TITAN_NET_DHCP_OFFER)return -3;
    g_info.dhcp_state=2;titan_fr_record(TITAN_FR_EV_NET,0,0,2u,(u32)g_info.dhcp_offers,0,0,NULL,0,0);
    set_detail("DHCP OFFER received; requesting lease");
    for(attempt=0;attempt<5;attempt++){
        g_net_event=TITAN_NET_NONE;len=titan_net_dhcp_request(&g_net,p,sizeof(p));if(len<0)return -4;
        if(g_info.dhcp_requests!=0xff)++g_info.dhcp_requests;
        {int txrc=send_net_packet(bcast,ETH_P_IP,p,len);if(txrc<0&&txrc!=-20)return -5;}
        ++g_info.online_retries;
        if(wait_net_event_wanted(TITAN_NET_DHCP_ACK,2500,p,sizeof(p))){if(g_info.dhcp_acks!=0xff)++g_info.dhcp_acks;break;}
    }
    if(g_net_event!=TITAN_NET_DHCP_ACK)return -6;
    g_info.dhcp_state=3;sync_ip_info();
    titan_fr_record(TITAN_FR_EV_NET,0,0,3u,(u32)g_info.dhcp_acks,0,0,NULL,0,0);
    /* Announce the lease and make the station answer ARP requests while the
       proof stack is alive. No reply is expected for gratuitous ARP. */
    len=titan_net_gratuitous_arp(&g_net,p,sizeof(p));
    if(len>0)send_net_packet(bcast,ETH_P_ARP,p,len);

    g_info.state=TITAN_SCAN_ARP_GATEWAY;set_detail("DHCP lease active; resolving default gateway with ARP");
    for(attempt=0;attempt<5;attempt++){
        g_net_event=TITAN_NET_NONE;len=titan_net_arp_gateway(&g_net,p,sizeof(p));if(len<0)return -7;
        if(g_info.arp_probes!=0xff)++g_info.arp_probes;
        {int txrc=send_net_packet(bcast,ETH_P_ARP,p,len);if(txrc<0&&txrc!=-20)return -8;}
        ++g_info.online_retries;
        if(wait_net_event_wanted(TITAN_NET_ARP_GATEWAY,1200,p,sizeof(p))){g_info.arp_resolved=1;break;}
    }
    if(g_net_event!=TITAN_NET_ARP_GATEWAY)return -9;
    sync_ip_info();
    titan_fr_record(TITAN_FR_EV_NET,0,0,4u,(u32)g_info.arp_probes,(u32)g_info.arp_resolved,0,NULL,0,0);

    g_info.state=TITAN_SCAN_PING;set_detail("gateway resolved; ICMP Echo -> Google DNS 8.8.8.8");
    for(attempt=0;attempt<6;attempt++){
        g_net_event=TITAN_NET_NONE;len=titan_net_ping_google_dns(&g_net,p,sizeof(p));if(len<0)return -10;
        ++g_info.ping_sent;{int txrc=send_net_packet(g_net.gateway_mac,ETH_P_IP,p,len);if(txrc<0&&txrc!=-20)return -11;}
        if(wait_net_event_wanted(TITAN_NET_PING_REPLY,2000,p,sizeof(p))){
            ++g_info.ping_replies;sync_ip_info();
            titan_fr_record(TITAN_FR_EV_NET,0,0,5u,(u32)g_info.ping_sent,(u32)g_info.ping_replies,0,NULL,0,0);
            return 0;
        }
    }
    return -12;
}


#if 0 /* Standalone laboratory replay/oracle path; not used by TT1426 live runtime. */
/* -------------------------------------------------------------------------
 * Linux 7.1.5 cold-start trace replay
 *
 * This path deliberately bypasses Titan's hand-translated reset/channel
 * helpers before Association.  Every AR9271 WMI command and management USB
 * payload from the successful Linux cold-start capture is submitted in the
 * same order and byte-for-byte.  The PS2-specific host transport remains
 * necessarily different (OHCI/USBD and 4 KiB EP82 reads), but the protocol
 * stream that reaches the AR9271 target is the captured Linux stream.
 * ------------------------------------------------------------------------- */
static const u8 g_linux_trace_bssid[6]={0x6e,0x34,0x91,0xc0,0xa1,0x74};
static const u8 g_linux_trace_mac[6]={TITAN_PS2_BIOS_MAC_BYTES};

/* V10.12.2: the lockstep trace is bound to one AP/BSSID and already contains
 * the exact Association Request selected by Linux for that BSS.  Do not make
 * the capture replay wait on an opportunistic live Beacon/Probe Response right
 * before Authentication: that inserted up to 1.5 s between WMI seq3912 and the
 * captured Auth frame and could abort at -7321 when EP82 had not delivered a
 * scan frame yet.  Seed the non-secret target context directly from the exact
 * captured Association Request before the trace clock starts. */
static int linux_trace_seed_target_from_capture(void)
{
    unsigned int i;
    for(i=0;i<TITAN_LINUX_TRACE_EVENT_COUNT;i++){
        const TitanLinuxTraceEvent *ev=&titan_linux_trace_events[i];
        const u8 *wire,*f;
        const u8 *rsn_ie=NULL,*ht_cap=NULL,*ht_op=NULL;
        u8 rates[30],supp[8],ext[22];
        int flen,pos,nrates=0,nsupp=0,next=0,rsn_len=0,ht_cap_len=0,ht_op_len=0;
        int ssid_ok=0,has_ht=0,has_wmm=0;
        u8 rsn_group=0,rsn_pair=0,rsn_akm=0,rsn_reject=RSN_REJ_MALFORMED;
        u16 rsn_caps=0,cap;
        if(ev->kind!=TITAN_LINUX_TRACE_KIND_MGMT || ev->id!=0u)continue;
        if(ev->offset+ev->length>titan_linux_trace_blob_size || ev->length<48)return -1;
        wire=titan_linux_trace_blob+ev->offset;
        f=wire+20;flen=(int)ev->length-20;
        if(flen<28)return -2;
        if(memcmp(f+4,g_linux_trace_bssid,6)!=0 || memcmp(f+16,g_linux_trace_bssid,6)!=0)return -3;
        if(memcmp(f+10,g_linux_trace_mac,6)!=0)return -4;
        cap=rd_le16(f+24);
        memset(rates,0,sizeof(rates));memset(supp,0,sizeof(supp));memset(ext,0,sizeof(ext));
        pos=28;
        while(pos+2<=flen){
            int id=f[pos],ilen=f[pos+1],j;
            const u8 *v=f+pos+2;
            if(pos+2+ilen>flen)return -5;
            if(id==0){
                ssid_ok=ssid_is_target((const char*)v,ilen);
            }else if(id==1){
                for(j=0;j<ilen&&nrates<30;j++)rates[nrates++]=v[j];
                for(j=0;j<ilen&&nsupp<8;j++)supp[nsupp++]=v[j];
            }else if(id==50){
                for(j=0;j<ilen&&nrates<30;j++)rates[nrates++]=v[j];
                for(j=0;j<ilen&&next<22;j++)ext[next++]=v[j];
            }else if(id==48){
                rsn_ie=f+pos;rsn_len=ilen+2;
                if(!rsn_analyze(v,ilen,&rsn_group,&rsn_pair,&rsn_akm,&rsn_caps,&rsn_reject))return -6;
            }else if(id==45&&ilen==26){
                ht_cap=v;ht_cap_len=26;has_ht=1;
            }else if(id==61&&ilen==22){
                ht_op=v;ht_op_len=22;
            }else if(id==221&&ilen>=4&&v[0]==0x00&&v[1]==0x50&&v[2]==0xf2&&v[3]==0x02){
                has_wmm=1;
            }
            pos+=2+ilen;
        }
        if(!ssid_ok || !rsn_ie || rsn_len<2)return -7;

        /* remember_target() performs the normal field/telemetry copies.  RSSI
         * is intentionally marked unknown (-127 dBm): no live scan sample is
         * being invented just to satisfy lockstep context setup. */
        g_target_locked=0;
        remember_target(g_linux_trace_bssid,1,-127,cap,rates,nrates,
                        supp,nsupp,ext,next,rsn_ie,rsn_len,
                        ht_cap,ht_cap_len,ht_op,ht_op_len,has_ht,has_wmm);
        if(!g_target.found || memcmp(g_target.bssid,g_linux_trace_bssid,6)!=0)return -8;
        g_info.target_ssid_match=1;
        g_info.target_group_cipher=rsn_group;
        g_info.target_pairwise_flags=rsn_pair;
        g_info.target_akm_flags=rsn_akm;
        g_info.target_rsn_caps=rsn_caps;
        g_info.target_rsn_reject=rsn_reject;
        g_target_locked=1;
        return 0;
    }
    return -9;
}

static int linux_trace_use_captured_station_rsn(void)
{
    unsigned int i;
    for(i=0;i<TITAN_LINUX_TRACE_EVENT_COUNT;i++){
        const TitanLinuxTraceEvent *ev=&titan_linux_trace_events[i];
        const u8 *wire,*f,*ie;
        int flen,pos;
        if(ev->kind!=TITAN_LINUX_TRACE_KIND_MGMT || ev->id!=0u)continue;
        if(ev->offset+ev->length>titan_linux_trace_blob_size || ev->length<48)return -1;
        wire=titan_linux_trace_blob+ev->offset;
        /* HIF4 + HTC8 + tx_mgmt8 = 20, 802.11 MAC24 + assoc fixed4 = 28. */
        f=wire+20;flen=(int)ev->length-20;pos=28;
        while(pos+2<=flen){
            int n=f[pos+1];
            if(pos+2+n>flen)return -2;
            ie=f+pos;
            if(ie[0]==48u){
                int len=n+2;
                if(titan_wpa2_use_station_rsn_ie_compat(&g_wpa,ie,len)<0)return -3;
                g_info.assoc_rsn_mode=4;
                g_info.assoc_rsn_ie_len=(u8)len;
                memset(g_info.assoc_rsn_preview,0,sizeof(g_info.assoc_rsn_preview));
                memcpy(g_info.assoc_rsn_preview,ie,
                       len>(int)sizeof(g_info.assoc_rsn_preview)?(int)sizeof(g_info.assoc_rsn_preview):len);
                return 0;
            }
            pos+=2+n;
        }
        return -4;
    }
    return -5;
}

static int linux_trace_prepare_context(void)
{
    u8 entropy[16];
    int rc;
    /* The lockstep path must not insert a live-scan wait between the captured
     * WMI stream and Authentication.  Build the AP context from the same exact
     * accepted Association Request before the trace timing origin is taken. */
    rc=linux_trace_seed_target_from_capture();
    if(rc<0)return -1;
    if(!g_target.found)return -2;
    if(memcmp(g_target.bssid,g_linux_trace_bssid,6)!=0)return -3;
    if(g_target.channel!=1)return -4;

    memcpy(g_info.local_mac,g_linux_trace_mac,6);
    memcpy(g_radio.macaddr,g_linux_trace_mac,6);
    g_radio.current_channel=1;
    g_radio.opmode=HTC_M_STA;
    g_radio.rxchainmask=1;
    g_radio.txchainmask=1;

    seed_wpa_entropy(entropy);
    titan_wpa2_init(&g_wpa,g_wifi_pmk,g_radio.macaddr,g_target.bssid,entropy);
    memset(entropy,0,sizeof(entropy));
    if(titan_wpa2_set_ap_rsn_ie(&g_wpa,g_target.rsn_ie,g_target.rsn_ie_len)<0)return -5;
    /* Do not reconstruct the station RSN for WPA M2. Use the exact RSN IE
     * carried by the Association Request that this same Linux/AR9271 run had
     * accepted by the AP. */
    if(linux_trace_use_captured_station_rsn()<0)return -6;
    return 0;
}

/* V10.12.4: V10.12.3 proved two different things at once:
 *   - the exact captured Linux Association Request is received and explicitly
 *     rejected with status 18;
 *   - an immediate same-authentication rate-mirror retry is ACKed by firmware
 *     telemetry but receives no Association Response from this AP.
 *
 * Do not interpret that silent second transaction as another rate verdict.
 * Some AP implementations discard/age the authenticated-but-unassociated
 * station context after an Association rejection.  The bounded diagnostic
 * retry below therefore starts a FRESH Open-System Authentication transaction
 * before transmitting the AP-rate-mirrored Association Request.  The control
 * attempt remains the immutable 200/LINUX-LOCKSTEP Linux-gold request.
 *
 * The fresh Auth and mirror Assoc use the next two 802.11 sequence numbers
 * (16 and 17) while keeping the captured USB/HTC/tx_mgmt framing and cookie 0.
 * This isolates the only intended compatibility mutation to the Supported
 * Rates / Extended Supported Rates IEs without testing it in a stale AP MLME
 * context. */
static int linux_trace_assoc_rates_from_wire(const u8 *wire,int wire_len)
{
    const u8 *f;
    int flen,pos,i,bit,seen_supp=0,seen_ext=0;
    u16 legacy=0,basic=0;
    if(!wire||wire_len<48)return -1;
    f=wire+20;flen=wire_len-20;
    if(flen<28 || (rd_le16(f)&0x00fcu)!=0x0000u)return -2;
    memset(g_info.assoc_supp_rates,0,sizeof(g_info.assoc_supp_rates));
    memset(g_info.assoc_ext_rates,0,sizeof(g_info.assoc_ext_rates));
    g_info.assoc_rate_count=0;g_info.assoc_supp_rate_count=0;g_info.assoc_ext_rate_count=0;
    pos=28;
    while(pos+2<=flen){
        int id=f[pos],n=f[pos+1];const u8 *v=f+pos+2;
        if(pos+2+n>flen)return -3;
        if(id==1){
            if(seen_supp++ || n<1 || n>8)return -4;
            g_info.assoc_supp_rate_count=(u8)n;
            memcpy(g_info.assoc_supp_rates,v,n);
        }else if(id==50){
            if(seen_ext++ || n<1 || n>(int)sizeof(g_info.assoc_ext_rates))return -5;
            g_info.assoc_ext_rate_count=(u8)n;
            memcpy(g_info.assoc_ext_rates,v,n);
        }
        pos+=2+n;
    }
    if(pos!=flen || !seen_supp)return -6;
    g_info.assoc_rate_count=(u8)(g_info.assoc_supp_rate_count+g_info.assoc_ext_rate_count);
    for(i=0;i<g_info.assoc_supp_rate_count;i++){
        bit=legacy_rate_index(g_info.assoc_supp_rates[i]);if(bit<0)return -7;
        legacy|=(u16)(1u<<bit);if(g_info.assoc_supp_rates[i]&0x80u)basic|=(u16)(1u<<bit);
    }
    for(i=0;i<g_info.assoc_ext_rate_count;i++){
        bit=legacy_rate_index(g_info.assoc_ext_rates[i]);if(bit<0)return -8;
        legacy|=(u16)(1u<<bit);if(g_info.assoc_ext_rates[i]&0x80u)basic|=(u16)(1u<<bit);
    }
    g_info.assoc_legacy_rate_mask=legacy;g_info.assoc_basic_rate_mask=basic;
    return 0;
}

static int linux_trace_advance_mgmt_seq(u8 *wire,int wire_len,u8 steps)
{
    u8 *f;u16 seq;
    if(!wire||wire_len<44||steps==0)return -1;
    f=wire+20;
    seq=rd_le16(f+22);
    seq=(u16)((seq&0x000fu)|((seq+((u16)steps<<4))&0xfff0u));
    wr_le16(f+22,seq);
    return 0;
}

static int linux_trace_copy_mgmt_wire(u8 subtype,u8 *dst,int dst_cap,int *out_len)
{
    unsigned int i;
    if(!dst||dst_cap<=0||!out_len)return -1;
    for(i=0;i<TITAN_LINUX_TRACE_EVENT_COUNT;i++){
        const TitanLinuxTraceEvent *ev=&titan_linux_trace_events[i];
        if(ev->kind!=TITAN_LINUX_TRACE_KIND_MGMT || ev->id!=subtype)continue;
        if(ev->offset+ev->length>titan_linux_trace_blob_size)return -2;
        if((int)ev->length>dst_cap)return -3;
        memcpy(dst,titan_linux_trace_blob+ev->offset,ev->length);
        *out_len=(int)ev->length;
        return 0;
    }
    return -4;
}

static int linux_trace_make_rate_mirror_assoc(u8 *dst,int dst_cap,
                                               const u8 *gold,int gold_len,
                                               u8 seq_steps)
{
    u8 *f;
    int flen,pos,seen_supp=0,seen_ext=0;
    if(!dst||!gold||gold_len<48||gold_len>dst_cap)return -1;
    if(!(g_info.assoc_adapt_flags&1u) || !g_info.assoc_resp_supp_rate_count)return -2;
    /* The current gold frame and live AP response are both 8+4. Refuse any
     * size-changing mutation so HIF/HTC/tx_mgmt lengths remain byte-identical. */
    if(g_info.assoc_resp_supp_rate_count!=8u || g_info.assoc_resp_ext_rate_count!=4u)return -3;
    if(g_info.assoc_resp_legacy_rate_mask!=0x0fffu)return -4;
    memcpy(dst,gold,gold_len);f=dst+20;flen=gold_len-20;
    if((rd_le16(f)&0x00fcu)!=0x0000u)return -5;
    pos=28;
    while(pos+2<=flen){
        int id=f[pos],n=f[pos+1];
        if(pos+2+n>flen)return -6;
        if(id==1){
            if(seen_supp++ || n!=(int)g_info.assoc_resp_supp_rate_count)return -7;
            memcpy(f+pos+2,g_info.assoc_resp_supp_rates,n);
        }else if(id==50){
            if(seen_ext++ || n!=(int)g_info.assoc_resp_ext_rate_count)return -8;
            memcpy(f+pos+2,g_info.assoc_resp_ext_rates,n);
        }
        pos+=2+n;
    }
    if(pos!=flen || seen_supp!=1 || seen_ext!=1)return -9;
    if(linux_trace_advance_mgmt_seq(dst,gold_len,seq_steps)<0)return -10;
    return linux_trace_assoc_rates_from_wire(dst,gold_len);
}

static int linux_trace_freshauth_rate_retry(const u8 *gold,int gold_len)
{
    u8 retry[256],auth[80],cookie;
    int rc,air,waited,auth_len=0;
    u16 first_status=g_info.assoc_first_status;
    if(gold_len>(int)sizeof(retry))return -1;

    /* Gold Assoc is seq15.  A new Auth consumes seq16 and the compatibility
     * Assoc consumes seq17, hence +2 for the copied Association Request. */
    rc=linux_trace_make_rate_mirror_assoc(retry,sizeof(retry),gold,gold_len,2u);
    if(rc<0)return -10+rc;
    rc=linux_trace_copy_mgmt_wire(11u,auth,sizeof(auth),&auth_len);
    if(rc<0)return -30+rc;
    /* Captured Auth is seq14 -> fresh Auth seq16. */
    if(linux_trace_advance_mgmt_seq(auth,auth_len,2u)<0)return -35;

    g_info.assoc_profile=202;
    g_info.assoc_adapt_flags|=4u;
    set_detail("LINUX-RATE-FRESHAUTH: Auth then AP-rate mirror");

    /* Let the AP and AR9271 finish the explicit rejection transaction before
     * reusing Linux's serialized management slot/cookie 0. */
    DelayThread(100000);

    /* Fresh Open-System Authentication. */
    g_info.auth_status=0xffff;g_auth_seen=0;g_auth_status=0xffff;
    cookie=auth[18];
    g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;
    rc=titan_ar9271_tx_raw_usb(&g_tx,auth,auth_len);
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    if(rc<0){
        assoc_hist_add(202,ASSOC_HIST_AUTH_FAIL,0);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -40;
    }
    air=wait_wmi_tx_status(cookie,g_mgmt_endpoint,2500000);
    g_last_mgmt_air=(u8)(air>0?2:(air<0?1:0));
    g_info.auth_tx_rate=g_tx_status_rate[cookie];
    if(air<0){
        assoc_hist_add(202,ASSOC_HIST_AUTH_FAIL,g_last_mgmt_air);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -41;
    }
    waited=g_auth_seen?1:wait_event(&g_auth_seen,1800);
    if(!waited || g_auth_status!=0u){
        if(waited)g_info.auth_status=g_auth_status;
        assoc_hist_add(202,ASSOC_HIST_AUTH_FAIL,g_last_mgmt_air);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -42;
    }
    g_info.auth_status=0u;

    /* Avoid reusing cookie 0 in the same scheduler turn. Linux serializes
     * these management transactions after TXSTATUS; 10 ms is intentionally
     * tiny versus the old 1.5 s beacon wait but gives the AP a clean MLME
     * boundary for this diagnostic path. */
    DelayThread(10000);

    g_info.assoc_status=0xffff;g_info.aid=0;
    g_assoc_seen=0;g_assoc_status=0xffff;g_assoc_aid=0;
    ++g_info.assoc_attempts;
    g_info.assoc_req_selfcheck=1;
    set_detail("LINUX-RATE-FRESHAUTH: mirrored Association");

    cookie=retry[18];
    g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;
    rc=titan_ar9271_tx_raw_usb(&g_tx,retry,gold_len);
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    if(rc<0){
        assoc_hist_add(202,ASSOC_HIST_TX_FAIL,0);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -50;
    }
    g_info.assoc_wire_len=g_tx.last_wire_len;g_info.assoc_wire_hash=g_tx.last_wire_hash;
    air=wait_wmi_tx_status(cookie,g_mgmt_endpoint,2500000);
    g_last_mgmt_air=(u8)(air>0?2:(air<0?1:0));
    g_info.assoc_tx_rate=g_tx_status_rate[cookie];
    if(air<0){
        assoc_hist_add(202,ASSOC_HIST_TX_FAIL,g_last_mgmt_air);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -51;
    }
    waited=g_assoc_seen?1:wait_event(&g_assoc_seen,5000);
    if(!waited){
        ++g_info.assoc_timeouts;
        assoc_hist_add(202,ASSOC_HIST_NO_RESP,g_last_mgmt_air);
        g_info.assoc_status=first_status;g_assoc_status=first_status;g_assoc_seen=1;
        return -52;
    }
    ++g_info.assoc_responses;
    g_info.assoc_status=g_assoc_status;g_info.aid=g_assoc_aid;
    assoc_hist_add(202,(u8)(g_assoc_status<=0xfcu?g_assoc_status:0xfcu),g_last_mgmt_air);
    if(g_assoc_status!=0)return -53;
    g_info.assoc_adapt_flags|=8u;
    return 0;
}

static int linux_trace_raw_mgmt(const u8 *wire,int wire_len,u8 subtype)
{
    int rc,air,waited;
    u8 cookie;
    const u8 *f;
    int flen;
    if(!wire||wire_len<44)return -1;
    f=wire+20;flen=wire_len-20;
    cookie=wire[18];
    g_tx_status[cookie]=0;g_tx_status_ep[cookie]=0xff;g_tx_status_rate[cookie]=0xff;

    if(subtype==11){
        g_info.state=TITAN_SCAN_AUTHENTICATING;
        g_info.auth_status=0xffff;g_auth_seen=0;g_auth_status=0xffff;
        set_detail("LINUX-LOCKSTEP: captured Authentication");
    }else if(subtype==0){
        g_info.state=TITAN_SCAN_ASSOCIATING;
        g_info.assoc_profile=200;
        g_info.assoc_status=0xffff;g_info.assoc_first_status=0xffff;
        g_assoc_seen=0;g_assoc_status=0xffff;g_assoc_aid=0;
        g_info.assoc_attempts=1;g_info.assoc_responses=0;g_info.assoc_timeouts=0;
        /* Parse the fixed fields directly from the captured frame for screen
         * telemetry; no locally rebuilt Association Request is involved. */
        if(flen>=28){
            g_info.assoc_tx_capability=rd_le16(f+24);
            g_info.assoc_listen_interval=rd_le16(f+26);
        }
        /* Raw replay bypasses the normal Association builder, so explicitly
         * decode the exact on-wire rate IEs for truthful TX telemetry. */
        if(linux_trace_assoc_rates_from_wire(wire,wire_len)<0)return -11;
        set_detail("LINUX-LOCKSTEP: captured Association Request");
    }

    if(g_lockstep_trace_start_us){
        u32 elapsed=trace_clock_us32()-g_lockstep_trace_start_us;
        if(subtype==11)g_info.tgt_rx_nobuf=elapsed;       /* Auth submit us */
        else if(subtype==0)g_info.tgt_rx_host_send=elapsed; /* Assoc submit us */
    }

    rc=titan_ar9271_tx_raw_usb(&g_tx,wire,wire_len);
    g_info.tx_packets=g_tx.submits;g_info.tx_errors=g_tx.errors;g_info.tx_reserved=g_tx.last_error;
    g_info.tx_usb_submit_errs=g_tx.submit_errors;g_info.tx_usb_completion_errs=g_tx.completion_errors;
    g_info.tx_usb_short_errs=g_tx.short_errors;g_info.tx_usb_timeout_errs=g_tx.timeout_errors;
    if(rc<0)return -2;

    if(subtype==11){
        if(g_tx.last_wire_hash!=TITAN_LINUX_TRACE_AUTH_FNV1A32)return -3;
        air=wait_wmi_tx_status(cookie,g_mgmt_endpoint,2500000);
        g_last_mgmt_air=(u8)(air>0?2:(air<0?1:0));
        g_info.auth_tx_rate=g_tx_status_rate[cookie];
        if(air<0)return -4;
        waited=g_auth_seen?1:wait_event(&g_auth_seen,1800);
        if(!waited)return -5;
        g_info.auth_status=g_auth_status;
        if(g_auth_status!=0)return -6;
    }else if(subtype==0){
        if(g_tx.last_wire_hash!=TITAN_LINUX_TRACE_ASSOC_FNV1A32)return -7;
        g_info.assoc_wire_len=g_tx.last_wire_len;
        g_info.assoc_wire_hash=g_tx.last_wire_hash;
        g_info.assoc_req_selfcheck=1; /* exact capture-derived 174-byte USB wire */
        air=wait_wmi_tx_status(cookie,g_mgmt_endpoint,2500000);
        g_last_mgmt_air=(u8)(air>0?2:(air<0?1:0));
        g_info.assoc_tx_rate=g_tx_status_rate[cookie];
        if(air<0)return -8;
        waited=g_assoc_seen?1:wait_event(&g_assoc_seen,5000);
        if(!waited){++g_info.assoc_timeouts;return -9;}
        ++g_info.assoc_responses;
        if(g_lockstep_trace_start_us)g_info.tgt_rx_host_done=trace_clock_us32()-g_lockstep_trace_start_us;
        g_info.assoc_status=g_assoc_status;g_info.assoc_first_status=g_assoc_status;g_info.aid=g_assoc_aid;
        assoc_hist_add(200,(u8)(g_assoc_status<=0xfcu?g_assoc_status:0xfcu),g_last_mgmt_air);
        if(g_assoc_status==18u && LINUX_LOCKSTEP_RATE_RETRY){
            /* Historical V10.12.4 A/B path, deliberately disabled in V10.12.5:
             * hardware already returned explicit status 18 after fresh Auth
             * with the AP-rate-mirrored request. Do not mutate rates again. */
            if(linux_trace_freshauth_rate_retry(wire,wire_len)==0)return 0;
            return -10;
        }
        if(g_assoc_status!=0)return -10;
    }
    return 0;
}

static int linux_trace_replay_postassoc(u16 aid)
{
    unsigned int i;
    u8 patched[REG_IO_BUF_SIZE];
    for(i=0;i<TITAN_LINUX_POSTASSOC_EVENT_COUNT;i++){
        const TitanLinuxTraceEvent *ev=&titan_linux_postassoc_events[i];
        const u8 *wire=titan_linux_trace_blob+ev->offset;
        const u8 *send=wire;
        int len=ev->length;
        if(ev->kind!=TITAN_LINUX_TRACE_KIND_WMI || len<12 || len>(int)sizeof(patched))return -1;
        /* seq3921 NODE_CREATE and seq3923/3924 BSSID/AID writes are the only
         * fields that are logically tied to the live association. BSSID is
         * fixed by this trace; AID is supplied by the actual AP response and
         * therefore patched rather than replaying the captured value 6. */
        if(ev->id==3921 || ev->id==3923 || ev->id==3924){
            memcpy(patched,wire,len);send=patched;
            if(ev->id==3921 && len>=24){
                memcpy(patched+12,g_target.bssid,6);
            }else if(ev->id==3923 && len>=20){
                u32 lo=(u32)g_target.bssid[0]|((u32)g_target.bssid[1]<<8)|
                       ((u32)g_target.bssid[2]<<16)|((u32)g_target.bssid[3]<<24);
                wr_be32(patched+16,lo);
            }else if(ev->id==3924 && len>=20){
                u32 hi=(u32)g_target.bssid[4]|((u32)g_target.bssid[5]<<8)|
                       (((u32)aid&0x3fffu)<<16);
                wr_be32(patched+16,hi);
            }
        }
        if(issue_wmi_wire_exact(send,len)<0)return -2-(int)i;
    }
    /* The captured Linux post-assoc block has now created peer node 1,
     * installed its rate table and programmed BSSID/AID. */
    titan_ar9271_tx_set_nodes(&g_tx,0,0,1);
    titan_ar9271_tx_set_mgmt_node(&g_tx,1);
    g_info.rx_filter=0x1607u;
    return 0;
}

static int linux_trace_replay_to_assoc(void)
{
    unsigned int i;
    int rc;
    u32 trace_start_us;
    u32 trace_target_us=0;
    g_info.assoc_profile=200;
    g_lockstep_bad_seq=0;g_lockstep_bad_kind=0;
    g_lockstep_read_drift_count=0;g_lockstep_read_drift_words=0;
    g_lockstep_read_first_seq=0;g_lockstep_read_first_word_seq=0;
    g_lockstep_read_first_addr=0;g_lockstep_read_first_expected=0;g_lockstep_read_first_actual=0;
    g_lockstep_trace_start_us=0;
    g_info.rtc_status=0;g_info.phy_active=0;g_info.mac_cr=0;
    g_info.diag_sw=0;g_info.synth_control=0;g_info.agc_control=0;
    g_info.auth_status=0xffff;g_info.assoc_status=0xffff;g_info.assoc_first_status=0xffff;
    g_info.auth_tx_rate=0xff;g_info.assoc_tx_rate=0xff;
    g_info.assoc_hist_count=0;g_info.assoc_req_selfcheck=0;
    memset(g_info.assoc_hist_profile,0,sizeof(g_info.assoc_hist_profile));
    memset(g_info.assoc_hist_result,0,sizeof(g_info.assoc_hist_result));
    memset(g_info.assoc_hist_tx,0,sizeof(g_info.assoc_hist_tx));

    /* Context setup is outside the capture clock.  From this point onward the
     * first WMI through Authentication/Association follows only trace-relative
     * deadlines; there is no opportunistic 1.5 s Beacon wait at WSEQ=3912. */
    rc=linux_trace_prepare_context();
    if(rc<0)return -20+rc;
    /* These firmware-stat fields are free in the lockstep screen and carry
     * real submission/response elapsed times without growing the RPC struct. */
    g_info.tgt_rx_nobuf=0;g_info.tgt_rx_host_send=0;g_info.tgt_rx_host_done=0;
    trace_start_us=trace_clock_us32();
    g_lockstep_trace_start_us=trace_start_us;

    for(i=0;i<TITAN_LINUX_TRACE_EVENT_COUNT;i++){
        const TitanLinuxTraceEvent *ev=&titan_linux_trace_events[i];
        const u8 *wire;
        if(ev->offset+ev->length>titan_linux_trace_blob_size)return -10;
        wire=titan_linux_trace_blob+ev->offset;

        /* V10.12 lockstep timing: preserve the COMPLETE Linux submission
         * timeline, including the thousands of 200-500 us gaps. V10.11 kept
         * only >=5 ms pauses and collapsed ~1.17 s of real ath9k/mac80211
         * pacing before Association. */
        trace_target_us+=ev->delta_us;
        trace_wait_deadline(trace_start_us,trace_target_us);

        if(ev->kind==TITAN_LINUX_TRACE_KIND_WMI){
            if(ev->id!=(u16)(g_wmi_seq+1u) || ev->aux!=rd_be16(wire+8))return -11;
            rc=issue_wmi_wire_exact(wire,ev->length);
            if(rc<0)return -1000-(int)i;
            continue;
        }
        if(ev->kind!=TITAN_LINUX_TRACE_KIND_MGMT)return -12;

        rc=linux_trace_raw_mgmt(wire,ev->length,(u8)ev->id);
        if(rc<0)return -2000-(int)i;
        if(ev->id==0){
            rc=linux_trace_replay_postassoc(g_assoc_aid);
            if(rc<0)return -3000+rc;
            return 0;
        }
    }
    return -13;
}

static int run_linux_trace_online(void)
{
    int rc;
    g_info.state=TITAN_SCAN_RADIO_INIT;
    set_detail("LINUX-LOCKSTEP: drift-map + exact Linux-gold Assoc");
    rc=linux_trace_replay_to_assoc();
    if(rc<0){
        if(g_assoc_seen && g_assoc_status!=0xffff)
            fail_scan(-872,"Linux lockstep reached AP; Association rejected");
        else if(g_lockstep_bad_seq){
            g_info.scan_round=g_lockstep_bad_seq;
            if(g_lockstep_bad_kind==1)
                fail_scan(-8101,"LINUX LOCKSTEP: WMI response command diverged");
            else if(g_lockstep_bad_kind==2)
                fail_scan(-8102,"LINUX LOCKSTEP: WMI response length diverged");
            else
                fail_scan(-8103,"LINUX LOCKSTEP: WMI response payload diverged");
        }else
            fail_scan(-7300+rc,"Linux lockstep cold-start replay failed");
        return rc;
    }
    /* Linux reuses TX slot/cookie 0 for the serialized EAPOL/data frames in
     * this capture and mac80211 has independent QoS sequence spaces per TID. */
    g_tx.cookie=0;g_tx.last_cookie=0;
    memset(g_qos_seq,0,sizeof(g_qos_seq));
    set_detail("LINUX-TRACE: ASSOC=0; entering WPA2 4-way handshake");
    rc=wpa_handshake();if(rc<0){fail_scan(-900+rc,"WPA2 4-way handshake failed");return rc;}
    rc=online_ipv4();if(rc<0){fail_scan(-950+rc,"DHCP/ARP/ICMP online proof failed");return rc;}
    g_info.state=TITAN_SCAN_ONLINE_PASS;
    if(g_netman_bridge_active)titan_netman_set_link(1);
    set_detail("LINUX-TRACE ASSOC=0 + WPA2 + DHCP + PING PASS");
    return 0;
}

#endif

static int run_online_target(void)
{
    int rc;
    g_info.state=TITAN_SCAN_TARGET_FOUND;set_detail("configured WPA2 target found; preparing station mode");DelayThread(50000);
    rc=station_prepare();if(rc<0){
        if(rc==-73)fail_scan(-873,"LIVE gate: selected AP absent after final station reset");
        else if(rc==-71||rc==-72)fail_scan(-874,"LIVE gate: RX filter transition failed");
        else fail_scan(-800+rc,"live station/VIF/TX bring-up failed");
        return rc;
    }
    rc=station_authenticate();if(rc<0){
        if(g_tx.transport_dead)fail_scan(-857,"USB bulk-OUT completion timeout during Auth; TX quarantined");
        else fail_scan(-850+rc,"802.11 authentication failed");
        return rc;
    }
    rc=station_associate();if(rc<0){
        if(rc==-8)fail_scan(-878,"local Association Request self-check failed; frame not transmitted");
        else if(rc==-7)fail_scan(-877,"USB bulk-OUT completion timeout; TX quarantined to prevent overlap");
        else if(rc==-6)fail_scan(-876,"no-RSN probe associated; RSN compatibility isolated safely");
        else if(rc==-5)fail_scan(-875,"AP stopped accepting fresh Auth; matrix stopped to avoid churn");
        else if(rc==-10||rc==-30)fail_scan(-880,"AIR ORACLE saw no accepted Assoc/Reassoc exchange");
        else if(rc==-21)fail_scan(-881,"AIR ORACLE dual-VIF: monitor VAP #1 create failed");
        else if(rc==-22)fail_scan(-882,"AIR ORACLE dual-VIF: monitor node #1 create failed");
        else if(rc==-23)fail_scan(-883,"AIR ORACLE dual-VIF: promiscuous RX filter failed");
        else if(rc==-24)fail_scan(-884,"AIR ORACLE dual-VIF: monitor VAP #1 remove failed");
        else if(rc==-25)fail_scan(-885,"AIR ORACLE dual-VIF: monitor node #1 remove failed");
        else if(rc==-26)fail_scan(-886,"AIR ORACLE dual-VIF: station RX filter restore failed");
        else if(rc==-31)fail_scan(-887,"AIR ORACLE accepted exchange had no safe reusable IE template");
        else if(rc==-32)fail_scan(-888,"fresh Auth failed before direct legacy-PHY association retry");
        else if(rc==-33)fail_scan(-889,"fresh Auth failed before direct legacy/no-WMM association retry");
        else if(rc==-34)fail_scan(-890,"fresh Auth failed before accepted-template association retry");
        else if(rc==-35)fail_scan(-891,"accepted association template rejected by local safety validator");
        else if(rc==-36)fail_scan(-892,"fresh Auth failed before bounded reassociation compatibility retry");
        else if(rc==-37)fail_scan(-893,"local reassociation fixed-field construction failed");
        else if(rc==-38)fail_scan(-894,"fresh Auth failed before accepted-template legacy-PHY retry");
        else if(rc==-39)fail_scan(-895,"fresh Auth failed before accepted-template legacy/no-WMM retry");
        else fail_scan(-870+rc,"802.11 association failed");
        return rc;
    }
    rc=wpa_handshake();if(rc<0){fail_scan(-900+rc,"WPA2 4-way handshake failed");return rc;}
    rc=online_ipv4();if(rc<0){fail_scan(-950+rc,"DHCP/ARP/ICMP online proof failed");return rc;}
    g_info.state=TITAN_SCAN_ONLINE_PASS;if(g_netman_bridge_active)titan_netman_set_link(1);set_detail("PING 8.8.8.8: REPLY RECEIVED - Wi-Fi online proof PASS");
    titan_fr_freeze(TITAN_FR_FREEZE_ONLINE_PASS);
    return 0;
}

static int run_passive_scan_phase(int discovery_only)
{
    int rc,round,ci;

    memset(&g_target,0,sizeof(g_target));
    g_target_locked=0;
    g_target_live_bss=0;
    g_target_live_gate_active=0;
    g_target_live_gate_pass=0;
    g_info.target_found=0;
    g_info.target_ssid_match=0;
    g_info.ap_count=0;
    memset(g_info.aps,0,sizeof(g_info.aps));
    g_info.scan_round=0;
    g_info.state=TITAN_SCAN_SCANNING;
    set_detail(g_wifi_configured && !discovery_only
        ? "ATH9K-LIVE: scanning nearby Wi-Fi before reconnect"
        : "ATH9K-LIVE: scanning nearby Wi-Fi networks");

    for(round=0;round<2;++round){
        g_info.scan_round=(u16)(round+1);
        for(ci=0;ci<13;++ci){
            int ch=(int)g_scan_channels[ci];
            if((int)g_radio.current_channel!=ch){
                rc=titan_ar9271_radio_set_channel(&g_radio,ch);
                if(rc<0){fail_scan(-650+rc,"ATH9K-LIVE channel reset failed");return 0;}
            }
            g_info.current_channel=(u8)ch;
            DelayThread(320000);
        }
        if(discovery_only || !g_wifi_configured || g_target.found)break;
    }

    if(discovery_only || !g_wifi_configured){
        update_radio_diag();
        g_discovery_radio_ready=1;
        g_info.state=TITAN_SCAN_WAITING;
        if(g_info.ap_count)
            set_detail("Wi-Fi scan complete; select a network in Settings");
        else
            set_detail("Wi-Fi scan complete; no visible networks found");
        return 1;
    }

    if(!g_target.found){
        update_radio_diag();
        fail_scan(-700,"ATH9K-LIVE scan did not observe configured WPA2 target");
        g_discovery_radio_ready=0;
        return 0;
    }

    g_discovery_radio_ready=0;
    g_target_locked=1;
    if((int)g_radio.current_channel!=(int)g_target.channel){
        rc=titan_ar9271_radio_set_channel(&g_radio,g_target.channel);
        if(rc<0){fail_scan(-701,"ATH9K-LIVE final target retune failed");return 0;}
        g_info.current_channel=g_target.channel;
        DelayThread(120000);
    }
    update_radio_diag();
    (void)run_online_target();
    return 1;
}

static void run_scanner(int discovery_only)
{
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    TitanRadioIo rio;
    int rc,wait;

    if(g_dev_id<0||g_control_pipe<0){fail_scan(-100,"USB control pipe missing");return;}
    device=(UsbDeviceDescriptor*)UsbGetDeviceStaticDescriptor(g_dev_id,NULL,USB_DT_DEVICE);
    config=device?(UsbConfigDescriptor*)UsbGetDeviceStaticDescriptor(g_dev_id,device,USB_DT_CONFIG):NULL;
    if(!config){fail_scan(-101,"USB configuration descriptor missing");return;}
    titan_fr_mark(0x53544152u,(u32)g_dev_id,(u32)device->idVendor,(u32)device->idProduct); /* STAR */

    g_info.state=TITAN_SCAN_CONFIGURING;
    set_detail("IOP-B1311 ATH9K-LIVE: configuring AR9271 USB");
    if(sync_set_configuration(config->bConfigurationValue)<0){fail_scan(-102,"USB SET_CONFIGURATION failed");return;}
    DelayThread(10000);

    g_info.state=TITAN_SCAN_UPLOADING_FW;
    set_detail("ATH9K-LIVE: uploading htc_9271 firmware");
    rc=upload_firmware();
    if(rc<0){fail_scan(-200+rc,"firmware upload/boot failed");return;}

    /* Keep the PS2-proven endpoint allocation/ordering, but from the first WMI
     * register command onward execute the live driver algorithms instead of a
     * capture replay. */
    DelayThread(50000);
    rc=open_reg_pipes_as_bulk();
    if(rc<0){fail_scan(-250+rc,"REG83/04 pipe open failed");return;}
    DelayThread(20000);

    g_info.state=TITAN_SCAN_WAITING_HTC;
    set_detail("ATH9K-LIVE: HTC READY + service negotiation");
    rc=htc_wmi_handshake();
    if(rc<0){
        if(rc==-1)fail_scan(-301,"HTC READY timeout on PS2 REG-IN transport");
        else if(rc==-2)fail_scan(-302,"HTC READY frame malformed/unexpected");
        else fail_scan(-300+rc,"HTC service setup failed");
        return;
    }

    rc=reg_async_start();
    if(rc<0){fail_scan(-399+rc,"REG-IN async pool did not reach 64/64");return;}

    /* EP82 must already have host receive capacity before WMI_START_RECV. */
    if(open_bulk_in_pipe()<0){fail_scan(-400,"EP82 bulk-IN open failed");return;}
    g_rx_armed=0;
    for(wait=0;wait<300&&!g_rx_armed;++wait)DelayThread(5000);
    if(!g_rx_armed){fail_scan(-402,"EP82 readers did not arm");return;}

    memset(&rio,0,sizeof(rio));
    rio.ctx=NULL;
    rio.wmi_cmd=radio_wmi_cmd;
    g_info.state=TITAN_SCAN_RADIO_INIT;
    set_detail("ATH9K-LIVE: register-driven reset + EEPROM/PLL/PHY/TX-power");
    rc=titan_ar9271_radio_init(&g_radio,&rio);
    if(rc<0){
        /* B1311: preserve the exact failing operation BEFORE radio_diag() does
         * any additional REG_READs.  The snapshot below is useful context, but
         * it must never overwrite the WMI/register that actually failed. */
        u16 fail_wmi=g_radio.last_wmi_cmd;
        int fail_wrc=g_radio.last_wmi_rc;
        u32 fail_reg=g_radio.last_reg;
        int fail_init_rc=g_radio.init_rc;
        u8 fail_stage=g_radio.init_stage;
        update_radio_diag();
        g_info.tgt_rx_nobuf=(u32)((u16)fail_init_rc);
        g_info.tgt_rx_host_send=((u32)fail_wmi<<16)|((u16)fail_wrc);
        g_info.tgt_rx_host_done=fail_reg;
        fail_scan(-1300-(int)fail_stage,"IOP-B1311 LIVEINIT failed; stage telemetry below");
        return;
    }
    memcpy(g_info.local_mac,g_radio.macaddr,6);
    memcpy(g_netman_mac,g_radio.macaddr,6);
    g_info.current_channel=1;
    update_radio_diag();

    g_info.state=TITAN_SCAN_RX_START;
    set_detail("ATH9K-LIVE: starting monitor RX for live BSS discovery");
    rc=titan_ar9271_radio_start_monitor(&g_radio);
    if(rc<0){fail_scan(-620+rc,"ATH9K-LIVE monitor start failed");return;}
    g_info.current_channel=1;

    g_discovery_radio_ready=1;
    (void)run_passive_scan_phase(discovery_only);
}

static int ar9271_probe(int dev_id)
{
    UsbDeviceDescriptor *d = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(dev_id, NULL, USB_DT_DEVICE);
    if (!d) return 0;
    return d->idVendor == AR9271_VID && d->idProduct == AR9271_PID;
}

static int ar9271_connect(int dev_id)
{
    UsbDeviceDescriptor *device;

    if (g_dev_id >= 0) return 1;
    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(dev_id, NULL, USB_DT_DEVICE);
    if (!device) return 0;

    titan_fr_reset();
    g_dev_id = dev_id;
    titan_fr_record(TITAN_FR_EV_USB,0,0,(u32)dev_id,AR9271_VID,AR9271_PID,0,NULL,0,0);
    g_control_pipe = UsbOpenEndpoint(dev_id, NULL);
    g_int_in_pipe = g_int_out_pipe = g_bulk_in_pipe = g_bulk_out_pipe = -1;
    titan_hif_rx_init(&g_hif_rx, hif_packet_cb, NULL);
    g_reg_async_active = 0;
    g_reg_rx_pool_ready = 0;
    memset(&g_wmi_mailbox, 0, sizeof(g_wmi_mailbox));
    {
        int si;
        for (si = 0; si < REG_RX_SLOT_COUNT; ++si) {
            memset(&g_reg_rx_slots[si], 0, sizeof(g_reg_rx_slots[si]));
            g_reg_rx_slots[si].index = si;
            g_reg_rx_slots[si].buffer = g_reg_rx_buffers[si];
        }
        for (si = 0; si < REG_TX_SLOT_COUNT; ++si) {
            memset(&g_reg_tx_slots[si], 0, sizeof(g_reg_tx_slots[si]));
            g_reg_tx_slots[si].index = si;
            g_reg_tx_slots[si].buffer = g_reg_tx_buffers[si];
        }
        for (si = 0; si < RX_SLOT_COUNT; ++si) {
            memset(&g_rx_slots[si], 0, sizeof(g_rx_slots[si]));
            g_rx_slots[si].index = si;
            g_rx_slots[si].buffer = g_rx_buffers[si];
        }
    }

    memset(&g_info, 0, sizeof(g_info));
    g_info.magic = TITAN_AR9271_SCAN_MAGIC;
    g_info.state = TITAN_SCAN_USB_DETECTED;
    g_info.dev_id = dev_id;
    g_info.control_pipe = g_control_pipe;
    g_info.int_in_pipe = -1;
    g_info.int_out_pipe = -1;
    g_info.bulk_in_pipe = -1;
    g_info.bulk_out_pipe = -1;
    g_info.fw_total = size_htc_9271_fw;
g_info.wifi_configured = g_wifi_configured ? 1u : 0u;
    if (g_wifi_configured) { int ci; for(ci=0;ci<(int)g_wifi_ssid_len && ci<32;ci++)g_info.target_ssid[ci]=g_wifi_ssid[ci]; g_info.target_ssid[ci]=0; }
    memset(&g_target,0,sizeof(g_target));
    memset(g_tx_status,0,sizeof(g_tx_status));
    memset(g_tx_status_ep,0xff,sizeof(g_tx_status_ep));
    memset(g_tx_status_rate,0xff,sizeof(g_tx_status_rate));
    g_target_locked=0;
    g_target_live_bss=0;
    g_target_live_gate_active=0;
    g_target_live_gate_pass=0;
    g_auth_seen=g_assoc_seen=0;
    /* USB attach runs before RX is armed, so reset the handoff directly; do
     * not block on a semaphore from the USBD driver's attach callback. */
    g_eapol_pending=0;g_eapol_len=0;
    g_net_event=TITAN_NET_NONE;g_dot11_seq=0;
    set_detail("AR9271 detected; firmware + PS2 REG-pipe workaround next");

    if (g_control_pipe < 0) {
        fail_scan(-90, "EP0 control pipe open failed");
        return 0;
    }
    if (g_wifi_configured) {
        g_discovery_requested=0;
        set_detail("AR9271 detected; scanning then reconnecting saved Wi-Fi");
    } else {
        g_discovery_requested=1;
        set_detail("AR9271 detected; automatic Wi-Fi scan starting");
    }
    SignalSema(g_work_sema);
    return 0;
}

static int ar9271_disconnect(int dev_id)
{
    if (g_dev_id == dev_id) {
        int si;
        g_reg_async_active = 0;
        g_reg_rx_pool_ready = 0;
        g_wmi_mailbox.active = 0;
        for (si = 0; si < REG_RX_SLOT_COUNT; ++si) {
            g_reg_rx_slots[si].armed = 0;
            g_reg_rx_slots[si].done = 0;
        }
        g_info.reg_rx_slots_armed = 0;
        SignalSema(g_reg_rx_sema);
        SignalSema(g_wmi_rsp_sema);
        g_rx_armed = 0;
        for (si = 0; si < RX_SLOT_COUNT; ++si) {
            g_rx_slots[si].armed = 0;
            g_rx_slots[si].done = 0;
        }
        g_info.rx_slots_armed = 0;
        g_dev_id = -1;
        if (g_bulk_out_pipe >= 0) UsbCloseEndpoint(g_bulk_out_pipe);
        if (g_bulk_in_pipe >= 0) UsbCloseEndpoint(g_bulk_in_pipe);
        if (g_int_out_pipe >= 0) UsbCloseEndpoint(g_int_out_pipe);
        if (g_int_in_pipe >= 0) UsbCloseEndpoint(g_int_in_pipe);
        if (g_control_pipe >= 0) UsbCloseEndpoint(g_control_pipe);
        g_bulk_out_pipe = g_bulk_in_pipe = g_int_out_pipe = g_int_in_pipe = g_control_pipe = -1;
        g_info.dev_id = -1;
        g_info.bulk_in_pipe = -1;g_info.bulk_out_pipe=-1;
        g_info.state = TITAN_SCAN_DISCONNECTED;
        g_discovery_radio_ready = 0;
        g_discovery_requested = 0;
        g_netman_bridge_requested=0;
        g_netman_bridge_active=0;
        if(g_netman_ctl_sema>=0)SignalSema(g_netman_ctl_sema);
        set_detail("AR9271 disconnected");
        titan_fr_freeze(TITAN_FR_FREEZE_DISCONNECT);
        SignalSema(g_rx_sema);
    }
    return 0;
}

static void worker_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int discovery_only;
        WaitSema(g_work_sema);
        if (g_dev_id < 0) continue;
        discovery_only = g_discovery_requested || !g_wifi_configured;
        g_discovery_requested=0;
        g_worker_running=1;
        if(g_discovery_radio_ready)
            (void)run_passive_scan_phase(discovery_only);
        else
            run_scanner(discovery_only);
        g_worker_running=0;
    }
}

static void *rpc_dispatch(int command, void *data, int size)
{
    if (command == TITAN_AR9271_SCAN_RPC_GET_INFO) {
        memcpy(data, &g_info, sizeof(g_info));
        return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_FR_META){
        TitanFlightRecMeta m; (void)size; titan_fr_get_meta(&m);
        memset(data,0,sizeof(g_rpc_buffer));memcpy(data,&m,sizeof(m));return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_FR_CHUNK){
        TitanFlightRecChunkRequest q; TitanFlightRecChunkReply *r=(TitanFlightRecChunkReply*)data;
        TitanFlightRecMeta m; int n=0;
        if(size<(int)sizeof(q))return data;
        memcpy(&q,data,sizeof(q));titan_fr_get_meta(&m);memset(data,0,sizeof(g_rpc_buffer));
        r->kind=q.kind;r->offset=q.offset;
        if(q.want>TITAN_FR_CHUNK_DATA)q.want=TITAN_FR_CHUNK_DATA;
        if(q.kind==0){r->total=m.record_count*m.record_size;n=titan_fr_copy_records(q.offset,r->data,q.want);}
        else if(q.kind==1){r->total=m.blob_used;n=titan_fr_copy_blob(q.offset,r->data,q.want);}
        r->count=n>0?(u32)n:0;return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_SET_CONFIG){
        TitanAr9271RuntimeConfig cfg;
        int ok=0;
        if(size >= (int)sizeof(cfg)){
            memcpy(&cfg,data,sizeof(cfg));
            if(cfg.version==TITAN_AR9271_RUNTIME_CONFIG_VERSION && cfg.ssid_len>0u && cfg.ssid_len<=32u){
                unsigned int i;
                memset(g_wifi_ssid,0,sizeof(g_wifi_ssid));
                for(i=0u;i<cfg.ssid_len;i++)g_wifi_ssid[i]=cfg.ssid[i];
                memcpy(g_wifi_pmk,cfg.pmk,32u);
                g_wifi_ssid_len=cfg.ssid_len;
                g_wifi_configured=1;
                g_discovery_requested=0;
                g_info.wifi_configured=1;
                g_netman_bridge_requested=0;
                g_netman_bridge_active=0;
                if(g_netman_ctl_sema>=0)SignalSema(g_netman_ctl_sema);
                memset(g_info.target_ssid,0,sizeof(g_info.target_ssid));
                for(i=0u;i<cfg.ssid_len;i++)g_info.target_ssid[i]=cfg.ssid[i];
                if(g_dev_id>=0 && !g_worker_running)SignalSema(g_work_sema);
                set_detail(g_dev_id>=0 ? "Titan Wi-Fi configured; connection starting" : "Titan Wi-Fi configured; waiting for AR9271");
                ok=1;
            }
        }
        memset(data,0,sizeof(g_rpc_buffer));((u32*)data)[0]=(u32)ok;return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_RETRY){
        int ok=(g_dev_id>=0 && g_wifi_configured && !g_worker_running);
        if(ok){
            g_netman_bridge_requested=0;g_netman_bridge_active=0;
            if(g_netman_ctl_sema>=0)SignalSema(g_netman_ctl_sema);
            g_discovery_requested=0;g_discovery_radio_ready=0;SignalSema(g_work_sema);
        }
        memset(data,0,sizeof(g_rpc_buffer));((u32*)data)[0]=(u32)ok;return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_START_SCAN){
        int ok=(g_dev_id>=0 && !g_worker_running && g_info.state!=TITAN_SCAN_ONLINE_PASS);
        if(ok){g_discovery_requested=1;SignalSema(g_work_sema);}
        memset(data,0,sizeof(g_rpc_buffer));((u32*)data)[0]=(u32)ok;return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE){
        u32 enabled=0,ok=0;
        if(size>=(int)sizeof(enabled))memcpy(&enabled,data,sizeof(enabled));
        g_netman_bridge_requested=enabled?1:0;
        ok=(g_netman_if_id>=0 && g_netman_ctl_sema>=0)?1u:0u;
        if(ok)SignalSema(g_netman_ctl_sema);
        memset(data,0,sizeof(g_rpc_buffer));((u32*)data)[0]=ok;return data;
    }
    if(command == TITAN_AR9271_SCAN_RPC_FR_FORCE_FREEZE){
        u32 tag=0; TitanFlightRecMeta m;
        if(size>=(int)sizeof(tag))memcpy(&tag,data,sizeof(tag));
        titan_fr_get_meta(&m);
        if(!m.frozen){
            titan_fr_mark(0x57444721u,tag,(u32)g_info.state,(u32)g_info.wpa_last_error); /* WDG! */
            g_info.error_code=-1421;
            set_detail("TT1423 watchdog: post-key WMI stalled; recorder forced frozen");
            g_info.state=TITAN_SCAN_ERROR;
            titan_fr_record(TITAN_FR_EV_FAIL,0,-1421,(u32)g_info.state,(u32)g_radio.init_stage,
                            (u32)g_radio.last_wmi_cmd,g_radio.last_reg,NULL,0,0);
            titan_fr_freeze(TITAN_FR_FREEZE_MANUAL);
        }
        memset(data,0,sizeof(g_rpc_buffer));
        ((u32*)data)[0]=1u;
        return data;
    }
    return data;
}

static void rpc_thread(void *arg)
{
    int tid = GetThreadId();
    (void)arg;
    SifSetRpcQueue(&g_rpc_queue, tid);
    SifRegisterRpc(&g_rpc_server, TITAN_AR9271_SCAN_RPC_ID, rpc_dispatch,
                   g_rpc_buffer, NULL, NULL, &g_rpc_queue);
    SifRpcLoop(&g_rpc_queue);
}

static void titan_startup_cleanup_semas(void)
{
    int *ids[] = {
        &g_work_sema, &g_ctrl_sema, &g_rx_sema, &g_reg_rx_sema,
        &g_wmi_rsp_sema, &g_netman_tx_sema, &g_netman_ctl_sema,
        &g_eapol_lock
    };
    unsigned int i;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (*ids[i] >= 0) {
            DeleteSema(*ids[i]);
            *ids[i] = -1;
        }
    }
}

static void titan_startup_cleanup_threads(int tids[], unsigned char started[], unsigned int count)
{
    while (count > 0u) {
        count--;
        if (tids[count] >= 0) {
            if (started[count]) TerminateThread(tids[count]);
            DeleteThread(tids[count]);
            tids[count] = -1;
            started[count] = 0u;
        }
    }
}

int _start(int argc, char *argv[])
{
    iop_sema_t sema;
    iop_thread_t thread;
    int tids[6] = {-1, -1, -1, -1, -1, -1};
    unsigned char started[6] = {0, 0, 0, 0, 0, 0};
    int netman_registered = 0;
    int usb_registered = 0;
    (void)argc; (void)argv;

    titan_fr_reset();
    if (!titan_fr_lock_ready()) return MODULE_NO_RESIDENT_END;
    memset(g_wifi_ssid,0,sizeof(g_wifi_ssid)); memset(g_wifi_pmk,0,sizeof(g_wifi_pmk)); g_wifi_ssid_len=0u; g_wifi_configured=0; g_worker_running=0; g_discovery_requested=0; g_discovery_radio_ready=0;
    g_netman_if_id=-1;g_netman_bridge_active=0;g_netman_bridge_requested=0;g_netman_link_up=0;
    memset(&g_info, 0, sizeof(g_info));
    g_info.magic = TITAN_AR9271_SCAN_MAGIC;
    g_info.state = TITAN_SCAN_WAITING;
    g_info.dev_id = -1;
    g_info.control_pipe = g_info.int_in_pipe = g_info.int_out_pipe = g_info.bulk_in_pipe = -1;
    g_info.bulk_out_pipe = -1;
    g_info.wifi_configured = g_wifi_configured ? 1u : 0u;
    if (g_wifi_configured) { int ci; for(ci=0;ci<(int)g_wifi_ssid_len && ci<32;ci++)g_info.target_ssid[ci]=g_wifi_ssid[ci]; g_info.target_ssid[ci]=0; }
    g_info.fw_total = size_htc_9271_fw;
    g_target_locked = 0;
    g_target_live_bss = 0;
    g_target_live_gate_active = 0;
    g_target_live_gate_pass = 0;
    memset(g_tx_status,0,sizeof(g_tx_status));
    memset(g_tx_status_ep,0xff,sizeof(g_tx_status_ep));
    memset(g_tx_status_rate,0xff,sizeof(g_tx_status_rate));
    set_detail("waiting for Qualcomm Atheros AR9271 0cf3:9271");

    sema.attr=0; sema.option=0; sema.initial=0; sema.max=1;
    g_work_sema = CreateSema(&sema);
    g_ctrl_sema = CreateSema(&sema);
    g_rx_sema = CreateSema(&sema);
    g_reg_rx_sema = CreateSema(&sema);
    g_wmi_rsp_sema = CreateSema(&sema);
    g_netman_tx_sema = CreateSema(&sema);
    g_netman_ctl_sema = CreateSema(&sema);
    sema.initial=1; sema.max=1;
    g_eapol_lock = CreateSema(&sema);
    if (g_work_sema < 0 || g_ctrl_sema < 0 || g_rx_sema < 0 ||
        g_reg_rx_sema < 0 || g_wmi_rsp_sema < 0 || g_netman_tx_sema < 0 ||
        g_netman_ctl_sema < 0 || g_eapol_lock < 0) goto fail;

    thread.attr=TH_C; thread.option=0; thread.priority=40; thread.stacksize=0x5000; thread.thread=worker_thread;
    tids[0]=CreateThread(&thread); if(tids[0]<0)goto fail;
    if(StartThread(tids[0],NULL)<0)goto fail;
    started[0]=1u;

    /* Linux keeps REG-IN completion processing ahead of ordinary WLAN RX.
     * PS2 priorities are numerically lower when more urgent; USBD callbacks
     * run at 36, then REG-IN dispatcher 37, WLAN RX 38, worker 40. */
    thread.priority=37; thread.stacksize=0x3000; thread.thread=reg_rx_thread;
    tids[1]=CreateThread(&thread); if(tids[1]<0)goto fail;
    if(StartThread(tids[1],NULL)<0)goto fail;
    started[1]=1u;

    thread.priority=38; thread.stacksize=0x3000; thread.thread=rx_thread;
    tids[2]=CreateThread(&thread); if(tids[2]<0)goto fail;
    if(StartThread(tids[2],NULL)<0)goto fail;
    started[2]=1u;

    /* NetMan xmit() is notification-only; this worker owns all blocking
     * AR9271 USB TX work. Keep it below RX but ahead of RPC service. */
    thread.priority=39; thread.stacksize=0x2000; thread.thread=titan_netman_tx_thread;
    tids[3]=CreateThread(&thread); if(tids[3]<0)goto fail;
    if(StartThread(tids[3],NULL)<0)goto fail;
    started[3]=1u;

    /* Link ownership transitions can wake NetMan/EE callbacks. Keep them out
     * of the SIF RPC dispatcher to avoid cross-stack reentrancy/deadlock. */
    thread.priority=40; thread.stacksize=0x1000; thread.thread=titan_netman_control_thread;
    tids[4]=CreateThread(&thread); if(tids[4]<0)goto fail;
    if(StartThread(tids[4],NULL)<0)goto fail;
    started[4]=1u;

    if(!titan_netman_register()) goto fail;
    netman_registered = 1;
    if (UsbRegisterDriver(&g_usb_driver) != USB_RC_OK) goto fail;
    usb_registered = 1;

    /* RPC is deliberately the final fallible startup step. Once this thread
     * starts it registers a SIF server which has no symmetric teardown path in
     * this module; therefore there must be no later operation that can fail. */
    thread.priority=45; thread.stacksize=0x1800; thread.thread=rpc_thread;
    tids[5]=CreateThread(&thread); if(tids[5]<0)goto fail;
    if(StartThread(tids[5],NULL)<0)goto fail;
    started[5]=1u;
    return MODULE_RESIDENT_END;

fail:
    if (usb_registered) UsbUnregisterDriver(&g_usb_driver);
    if (netman_registered) {
        NetManUnregisterNetIF("WFI");
        g_netman_if_id = -1;
    }
    titan_startup_cleanup_threads(tids, started, sizeof(tids) / sizeof(tids[0]));
    titan_startup_cleanup_semas();
    return MODULE_NO_RESIDENT_END;
}
