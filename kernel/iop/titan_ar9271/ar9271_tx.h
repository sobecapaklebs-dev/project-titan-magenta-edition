#ifndef TITAN_AR9271_TX_H
#define TITAN_AR9271_TX_H
#include <types.h>

typedef struct TitanAr9271Tx {
    int bulk_out_pipe;
    int sema;
    volatile int done;
    volatile int result;
    volatile int count;
    u8 mgmt_ep;
    u8 data_be_ep;
    u8 vif_idx;
    u8 vif_sta_idx;
    u8 ap_sta_idx;
    u8 mgmt_sta_idx;
    u8 cookie;
    u8 last_cookie;
    u64 tx_pn; /* next host-supplied pairwise CCMP PN; starts at 1 per new PTK */
    u32 submits;
    u32 errors;
    u16 last_wire_len; /* HIF+HTC+ath9k tx header+802.11 bytes submitted to USB */
    u32 last_wire_hash; /* FNV-1a over the exact USB bulk payload */
    u8 last_error; /* 0=none,1=submit,2=completion,3=short,4=timeout */
    u8 submit_errors;
    u8 completion_errors;
    u8 short_errors;
    u8 timeout_errors;
    u8 transport_dead; /* set after a lost USB completion; never overlap a later transfer */
    u8 redact_next_wire; /* TT1424: metadata-only recorder entry for EAPOL TX */
} TitanAr9271Tx;

int titan_ar9271_tx_init(TitanAr9271Tx *tx, int bulk_out_pipe,
                         u8 mgmt_ep, u8 data_be_ep);
void titan_ar9271_tx_set_nodes(TitanAr9271Tx *tx, u8 vif_idx,
                               u8 vif_sta_idx, u8 ap_sta_idx);
void titan_ar9271_tx_set_mgmt_node(TitanAr9271Tx *tx, u8 sta_idx);
void titan_ar9271_tx_redact_next_wire(TitanAr9271Tx *tx);
int titan_ar9271_tx_mgmt(TitanAr9271Tx *tx, const u8 *frame, int frame_len);
/* Submit one capture-derived HIF+HTC+ath9k management USB payload byte-for-byte. */
int titan_ar9271_tx_raw_usb(TitanAr9271Tx *tx, const u8 *wire, int wire_len);
int titan_ar9271_tx_data(TitanAr9271Tx *tx, const u8 *frame, int frame_len,
                         u8 key_type, u8 keyix, int use_ap_node);
int titan_ar9271_tx_data_linux(TitanAr9271Tx *tx, const u8 *frame, int frame_len,
                               u8 key_type, u8 keyix, int use_ap_node,
                               u8 endpoint, u8 tid, u32 flags);

#endif
