/*
 * AR9271 HTC/HIF transmit path for the standalone Project Titan tester.
 * Framing follows Linux ath9k_htc: tx_mgmt_hdr/tx_frame_hdr -> HTC header ->
 * ath9k_htc USB stream header (tag 0x697e) -> WLAN bulk-OUT pipe.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>
#include <sysclib.h>
#include <thsemap.h>
#include <thbase.h>
#include <usbd.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <usbd_macro.h>
#pragma GCC diagnostic pop
#include "ar9271_tx.h"
#include "titan_assoc_wire.h"
#include "titan_flightrec.h"

#define ATH_USB_TX_STREAM_TAG 0x697eu
#define HTC_HDR_LEN 8
#define TX_MGMT_HDR_LEN 8
#define TX_FRAME_HDR_LEN 12
#define TX_BUF_SIZE 2048
#define ATH9K_KEY_TYPE_CLEAR 0
#define ATH9K_TXKEYIX_INVALID 0xff

static u8 g_txbuf[TX_BUF_SIZE] __attribute__((aligned(64)));
static TitanAr9271Tx *g_active_tx;

static void wr_le16(u8 *p,u16 v){p[0]=(u8)v;p[1]=(u8)(v>>8);}
static void wr_be16(u8 *p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void wr_be32(u8 *p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}

static void tx_cb(int result,int count,void *arg)
{
    TitanAr9271Tx *tx=(TitanAr9271Tx*)arg;
    if(!tx)tx=g_active_tx;
    if(!tx)return;
    tx->result=result;tx->count=count;tx->done=1;
    SignalSema(tx->sema);
}

static int tx_wait(TitanAr9271Tx *tx,int total)
{
    int rc,i;
    if(tx->transport_dead){tx->last_error=4;return -5;}
    while(PollSema(tx->sema)==0) { }
    tx->done=0;tx->result=0;tx->count=0;tx->last_error=0;g_active_tx=tx;
    rc=UsbBulkTransfer(tx->bulk_out_pipe,g_txbuf,total,tx_cb,tx);
    if(rc!=USB_RC_OK){
        tx->last_error=1;
        if(tx->submit_errors!=0xff) ++tx->submit_errors;
        ++tx->errors;
        DelayThread(20000);
        return -1;
    }
    ++tx->submits;
    /* A local USB completion is transport telemetry, not the 802.11 verdict.
     * Keep the bounded wait, classify failures, and let higher layers prefer a
     * real AUTH/ASSOC response when one already arrived over the air. */
    for(i=0;i<10000;i++){
        if(PollSema(tx->sema)==0)break;
        DelayThread(1000);
    }
    if(i==10000){
        /* There is no safe transfer-generation cookie in the PS2 USBD bulk
         * callback. If a completion is genuinely lost, submitting another
         * transfer could let a late callback satisfy the wrong waiter. Quarantine
         * the TX path for this run rather than manufacture a cascade of errors. */
        tx->last_error=4;tx->transport_dead=1;
        if(tx->timeout_errors!=0xff) ++tx->timeout_errors;
        ++tx->errors;
        return -4;
    }
    if(!tx->done || tx->result!=USB_RC_OK){
        tx->last_error=2;
        if(tx->completion_errors!=0xff) ++tx->completion_errors;
        ++tx->errors;
        DelayThread(20000);
        return -2;
    }
    if(tx->count!=total){
        tx->last_error=3;
        if(tx->short_errors!=0xff) ++tx->short_errors;
        ++tx->errors;
        DelayThread(20000);
        return -3;
    }
    return 0;
}

int titan_ar9271_tx_init(TitanAr9271Tx *tx,int pipe,u8 mgmt_ep,u8 data_ep)
{
    iop_sema_t s;
    if(!tx || pipe<0 || !mgmt_ep || !data_ep)return -1;
    memset(tx,0,sizeof(*tx));tx->bulk_out_pipe=pipe;tx->mgmt_ep=mgmt_ep;tx->data_be_ep=data_ep;
    tx->vif_idx=0;tx->vif_sta_idx=0;tx->ap_sta_idx=1;tx->mgmt_sta_idx=0;tx->tx_pn=1;
    s.attr=0;s.option=0;s.initial=0;s.max=1;tx->sema=CreateSema(&s);
    return tx->sema>=0?0:-2;
}
void titan_ar9271_tx_set_nodes(TitanAr9271Tx *tx,u8 vif,u8 vsta,u8 apsta){if(tx){tx->vif_idx=vif;tx->vif_sta_idx=vsta;tx->ap_sta_idx=apsta;}}
void titan_ar9271_tx_set_mgmt_node(TitanAr9271Tx *tx,u8 sta_idx){if(tx)tx->mgmt_sta_idx=sta_idx;}
void titan_ar9271_tx_redact_next_wire(TitanAr9271Tx *tx){if(tx)tx->redact_next_wire=1;}

static int build_outer(TitanAr9271Tx *tx,u8 epid,const u8 *inner,int inner_len)
{
    int htc_len=HTC_HDR_LEN+inner_len,total=4+htc_len;
    if(total>TX_BUF_SIZE)return -1;
    wr_le16(g_txbuf,(u16)htc_len);wr_le16(g_txbuf+2,ATH_USB_TX_STREAM_TAG);
    g_txbuf[4]=epid;g_txbuf[5]=0;wr_be16(g_txbuf+6,(u16)inner_len);memset(g_txbuf+8,0,4);
    memcpy(g_txbuf+12,inner,inner_len);
    tx->last_wire_len=(u16)total;
    tx->last_wire_hash=titan_assoc_fnv1a32(g_txbuf,total);
    if(tx->redact_next_wire){
        titan_fr_record(TITAN_FR_EV_TX_WIRE,TITAN_FR_F_SECRET_REDACTED,0,(u32)epid,(u32)total,
                        (u32)tx->last_cookie,tx->last_wire_hash,NULL,0,0);
        tx->redact_next_wire=0;
    }else{
        titan_fr_record(TITAN_FR_EV_TX_WIRE,0,0,(u32)epid,(u32)total,(u32)tx->last_cookie,tx->last_wire_hash,
                        g_txbuf,(u16)(total>768?768:total),0);
    }
    return tx_wait(tx,total);
}

int titan_ar9271_tx_raw_usb(TitanAr9271Tx *tx,const u8 *wire,int wire_len)
{
    if(!tx||!wire||wire_len<20||wire_len>TX_BUF_SIZE)return -1;
    /* Exact ath9k_htc USB-stream validation: LE payload length + 0x697e tag,
     * HTC endpoint/length, then the 8-byte tx_mgmt_hdr. */
    if((u16)(wire[0]|((u16)wire[1]<<8))!=(u16)(wire_len-4))return -2;
    if((u16)(wire[2]|((u16)wire[3]<<8))!=ATH_USB_TX_STREAM_TAG)return -3;
    if(wire[4]!=tx->mgmt_ep)return -4;
    if(((u16)wire[6]<<8|wire[7])!=(u16)(wire_len-12))return -5;
    memcpy(g_txbuf,wire,wire_len);
    tx->last_cookie=wire[18];
    tx->cookie=(u8)(wire[18]+1u);
    tx->last_wire_len=(u16)wire_len;
    tx->last_wire_hash=titan_assoc_fnv1a32(g_txbuf,wire_len);
    titan_fr_record(TITAN_FR_EV_TX_WIRE,1,0,(u32)wire[4],(u32)wire_len,(u32)tx->last_cookie,tx->last_wire_hash,
                    g_txbuf,(u16)(wire_len>768?768:wire_len),0);
    return tx_wait(tx,wire_len);
}

int titan_ar9271_tx_mgmt(TitanAr9271Tx *tx,const u8 *frame,int frame_len)
{
    u8 inner[TX_BUF_SIZE-12];int n=0;
    if(!tx||!frame||frame_len<24||frame_len>(int)sizeof(inner)-TX_MGMT_HDR_LEN)return -1;
    memset(inner,0,TX_MGMT_HDR_LEN);
    /* ath9k_htc uses the peer station index only when mac80211 supplies a STA
     * object. Before association there is no AP STA object, so upstream falls
     * back to vif_sta_pos[vif] (Titan node 0). Titan switches to AP peer node 1
     * only after a successful Association Response creates that peer. */
    inner[0]=tx->mgmt_sta_idx;inner[1]=tx->vif_idx;inner[2]=0;inner[3]=0;
    inner[4]=ATH9K_KEY_TYPE_CLEAR;inner[5]=ATH9K_TXKEYIX_INVALID;tx->last_cookie=tx->cookie;inner[6]=tx->cookie++;inner[7]=0;
    n=TX_MGMT_HDR_LEN;memcpy(inner+n,frame,frame_len);n+=frame_len;
    return build_outer(tx,tx->mgmt_ep,inner,n);
}

int titan_ar9271_tx_data_linux(TitanAr9271Tx *tx,const u8 *frame,int frame_len,u8 key_type,u8 keyix,int use_ap_node,u8 endpoint,u8 tid,u32 flags)
{
    u8 inner[TX_BUF_SIZE-12];int n=0;
    if(!tx||!frame||frame_len<24||frame_len>(int)sizeof(inner)-TX_FRAME_HDR_LEN||!endpoint)return -1;
    memset(inner,0,TX_FRAME_HDR_LEN);
    inner[0]=2; /* ATH9K_HTC_NORMAL */
    inner[1]=use_ap_node?tx->ap_sta_idx:tx->vif_sta_idx;
    inner[2]=tx->vif_idx;inner[3]=(u8)(tid&0x0fu);wr_be32(inner+4,flags);
    inner[8]=key_type;inner[9]=(key_type==ATH9K_KEY_TYPE_CLEAR)?ATH9K_TXKEYIX_INVALID:keyix;
    /* The successful Linux trace serializes these packets through TX slot 0;
     * TXSTATUS frees slot 0 before the next packet, so every EAPOL/BE frame in
     * the reference capture carries cookie 0. */
    tx->last_cookie=0;tx->cookie=0;inner[10]=0;inner[11]=0;
    n=TX_FRAME_HDR_LEN;memcpy(inner+n,frame,frame_len);n+=frame_len;
    return build_outer(tx,endpoint,inner,n);
}

int titan_ar9271_tx_data(TitanAr9271Tx *tx,const u8 *frame,int frame_len,u8 key_type,u8 keyix,int use_ap_node)
{
    return titan_ar9271_tx_data_linux(tx,frame,frame_len,key_type,keyix,use_ap_node,
                                      tx?tx->data_be_ep:0,0,0);
}
