#include <types.h>
#include <sysclib.h>
#include "titan_mlme.h"
#include "titan_assoc_rates.h"
#include "titan_full_assoc.h"

/* V10.5 PS2LINUX-MANAGED
 * ------------------------
 * Direct managed-station subset ported from the Linux 5.4 mac80211 + ath9k
 * behavior used by the PS2 Linux tree.  This is intentionally not an IE/profile
 * guessing engine: the default Association Request is constructed from the
 * exact AR9271/mac80211 policy that matters for a 2.4 GHz WPA2 station.
 */

#define IEEE80211_FC_ASSOC_REQ   0x0000u
#define IEEE80211_FC_REASSOC_REQ 0x0020u
#define IEEE80211_FC_AUTH      0x00b0u
#define IEEE80211_CAP_ESS            0x0001u
#define IEEE80211_CAP_PRIVACY        0x0010u
#define IEEE80211_CAP_SHORT_PREAMBLE 0x0020u
#define IEEE80211_CAP_SHORT_SLOT     0x0400u
#define IEEE80211_CAP_RADIO_MEASURE 0x1000u

/* AR9271/ath9k 2.4 GHz local legacy rate table, encoded in 500 kbit/s. */
static const u8 titan_local_rates_24g[12]={
    0x02,0x04,0x0b,0x16,0x0c,0x12,0x18,0x24,0x30,0x48,0x60,0x6c
};

static void wr16(u8 *p,u16 v){p[0]=(u8)v;p[1]=(u8)(v>>8);}

static int rate_index(u8 encoded)
{
    int i;u8 r=(u8)(encoded&0x7fu);
    for(i=0;i<(int)sizeof(titan_local_rates_24g);i++)
        if(titan_local_rates_24g[i]==r)return i;
    return -1;
}

static int validate_basic_legacy(const TitanMlmeBssInput *b)
{
    int i;
    for(i=0;i<b->supp_rate_count;i++)
        if((b->supp_rates[i]&0x80u) && rate_index(b->supp_rates[i])<0)return -1;
    for(i=0;i<b->ext_rate_count;i++)
        if((b->ext_rates[i]&0x80u) && rate_index(b->ext_rates[i])<0)return -1;
    return 0;
}

static int build_negotiated_rates(TitanMlme *m,const TitanMlmeBssInput *b)
{
    TitanAssocRateIes ri;int rc;
    if(validate_basic_legacy(b)<0)return -3;

    /* Port the actual mac80211 association behavior instead of replaying the
     * AP's Basic bits. ieee80211_assoc_add_rates() passes basic_rates=0 to the
     * rate-IE constructor and emits the device/AP intersection in device order.
     * The AP's Basic set is a JOIN requirement, not a request-side marker mask. */
    rc=titan_assoc_build_rate_ies(b->supp_rates,b->supp_rate_count,
                                  b->ext_rates,b->ext_rate_count,
                                  TITAN_ASSOC_RATE_MODE_MAC80211,&ri);
    if(rc<0||!ri.supp_len)return -5;
    m->rate_count=ri.total_len;m->supp_rate_count=ri.supp_len;m->ext_rate_count=ri.ext_len;
    m->legacy_rate_mask=ri.legacy_mask;m->basic_rate_mask=ri.basic_mask;
    memcpy(m->supp_rates,ri.supp,ri.supp_len);
    if(ri.ext_len)memcpy(m->ext_rates,ri.ext,ri.ext_len);
    memcpy(m->rates,ri.supp,ri.supp_len);
    if(ri.ext_len)memcpy(m->rates+ri.supp_len,ri.ext,ri.ext_len);
    return 0;
}

static int ht_basic_mcs_compatible(const TitanMlmeBssInput *b)
{
    int i;
    if(!b->ht_op_present||!b->ht_op)return 0;
    /* HT Operation payload bytes 6..21 are Basic HT-MCS Set. AR9271 is 1x1
     * and supports MCS0..7, so byte 6 may contain any bits but bytes 7..21
     * must be zero for a strict one-stream membership check. */
    for(i=7;i<22;i++)if(b->ht_op[i])return 0;
    return 1;
}

static int ht_channel_compatible(const TitanMlmeBssInput *b)
{
    if(!b->ht_op_present||!b->ht_op)return 0;
    return b->ht_op[0]==b->channel;
}

static void build_local_ht_cap(TitanMlme *m,const TitanMlmeBssInput *b)
{
    /* V10.8 LINUX-GOLD-PCAP: exact HT Capability payload observed from this
     * same AR9271 under Linux/ath9k_htc in the Association Request that the
     * target AP accepted. The BSS is currently 20 MHz, but Linux advertises
     * the radio's 20/40 capability here: cap=0x116e, A-MPDU=0x1b, MCS0..7. */
    static const u8 local_ht[26]={
        0x6e,0x11,0x1b,
        0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x01,
        0x00,0x00,0x00,
        0x00,0x00,
        0x00,0x00,0x00,0x00,
        0x00
    };
    (void)b;
    memcpy(m->ht_cap_ie,local_ht,sizeof(local_ht));
    m->use_ht40=1;
}

int titan_mlme_join(TitanMlme *m,const u8 local_mac[6],const TitanMlmeBssInput *b)
{
    int rc;
    if(!m||!local_mac||!b||!b->bssid||!b->ssid||!b->supp_rates)return -1;
    if(!b->ssid_len||b->ssid_len>TITAN_MLME_MAX_SSID||b->supp_rate_count>8||b->ext_rate_count>22)return -2;
    memset(m,0,sizeof(*m));
    memcpy(m->local_mac,local_mac,6);memcpy(m->bssid,b->bssid,6);
    memcpy(m->ssid,b->ssid,b->ssid_len);m->ssid_len=b->ssid_len;
    m->channel=b->channel;m->ap_capability=b->capability;

    rc=build_negotiated_rates(m,b);if(rc<0){m->state=TITAN_MLME_FAILED;return rc;}

    /* mac80211 does not mirror the AP's Radio Measurement bit.  It sets RRM
     * only when the managed-station feature is explicitly enabled.  Titan's
     * proof client implements no RRM measurement engine, so do not advertise
     * a capability we cannot service.  The WPA2 AP's Privacy bit is copied,
     * while the normal 2.4 GHz ESS/short-preamble/short-slot bits are local. */
    /* Exact capability word from the successful Linux/AR9271 association:
     * ESS + Privacy + Short Preamble + Short Slot + Radio Measurement. */
    m->assoc_capability=(u16)(IEEE80211_CAP_ESS|IEEE80211_CAP_SHORT_PREAMBLE|
                              IEEE80211_CAP_SHORT_SLOT|IEEE80211_CAP_RADIO_MEASURE);
    if(b->capability&IEEE80211_CAP_PRIVACY)m->assoc_capability|=IEEE80211_CAP_PRIVACY;
    m->use_rrm=1;
    /* Exact RRM Enabled Capabilities IE payload captured in the accepted request. */
    m->rrm_caps[0]=0x70;m->rrm_caps[1]=0x00;m->rrm_caps[2]=0x00;
    m->rrm_caps[3]=0x00;m->rrm_caps[4]=0x00;
    m->listen_interval=TITAN_MLME_LISTEN_INTERVAL;

    m->use_wmm=(u8)(b->wmm_present?1:0);
    m->use_ht=(u8)((b->ht_present&&m->use_wmm&&
                    ht_channel_compatible(b)&&ht_basic_mcs_compatible(b))?1:0);
    m->use_ht40=0;
    m->use_qos=0; /* WMM Information is sufficient; don't duplicate IE 46 by default. */
    if(m->use_ht)build_local_ht_cap(m,b);

    m->auth_status=0xffffu;m->assoc_status=0xffffu;
    m->state=TITAN_MLME_JOINED;
    return 0;
}

int titan_mlme_apply_accepted_template(TitanMlme *m,
                                       u16 capability,u16 listen_interval,
                                       const u8 *supp_rates,u8 supp_rate_count,
                                       const u8 *ext_rates,u8 ext_rate_count,
                                       const u8 *template_ies,u16 template_ies_len,
                                       u8 has_ht,u8 has_wmm,
                                       const u8 rrm_caps[5],u8 has_rrm)
{
    TitanMlmeBssInput b;u8 saved_ht[26];u8 saved_use_ht,saved_use_wmm;int rc;
    if(!m||!supp_rates||!supp_rate_count||supp_rate_count>8||ext_rate_count>22||
       !template_ies||!template_ies_len||template_ies_len>TITAN_MLME_MAX_TEMPLATE_IES)return -1;
    memcpy(saved_ht,m->ht_cap_ie,sizeof(saved_ht));saved_use_ht=m->use_ht;saved_use_wmm=m->use_wmm;
    memset(&b,0,sizeof(b));b.supp_rates=supp_rates;b.supp_rate_count=supp_rate_count;
    b.ext_rates=ext_rates;b.ext_rate_count=ext_rate_count;
    rc=build_negotiated_rates(m,&b);if(rc<0)return rc;
    memcpy(m->template_ies,template_ies,template_ies_len);m->template_ies_len=template_ies_len;
    m->assoc_capability=(u16)(IEEE80211_CAP_ESS|(m->ap_capability&IEEE80211_CAP_PRIVACY)|
                              (capability&(IEEE80211_CAP_SHORT_PREAMBLE|IEEE80211_CAP_SHORT_SLOT)));
    m->use_rrm=(u8)(has_rrm?1:0);if(m->use_rrm)m->assoc_capability|=0x1000u;
    memset(m->rrm_caps,0,sizeof(m->rrm_caps));if(has_rrm&&rrm_caps)memcpy(m->rrm_caps,rrm_caps,5);
    m->listen_interval=listen_interval?listen_interval:TITAN_MLME_LISTEN_INTERVAL;
    m->use_wmm=(u8)(saved_use_wmm&&has_wmm);
    m->use_ht=(u8)(saved_use_ht&&has_ht&&m->use_wmm);m->use_qos=0;
    memcpy(m->ht_cap_ie,saved_ht,sizeof(saved_ht));
    return 0;
}

int titan_mlme_set_reassoc(TitanMlme *m,const u8 current_ap[6])
{
    if(!m||!current_ap)return -1;
    memcpy(m->current_ap,current_ap,6);m->reassoc=1;
    return 0;
}

void titan_mlme_clear_reassoc(TitanMlme *m)
{
    if(!m)return;
    m->reassoc=0;memset(m->current_ap,0,sizeof(m->current_ap));
}

void titan_mlme_set_phy_mode(TitanMlme *m,u8 use_ht,u8 use_wmm)
{
    if(!m)return;
    m->use_wmm=(u8)(use_wmm?1:0);
    m->use_ht=(u8)((use_ht&&m->use_wmm)?1:0);
    if(!m->use_ht)m->use_ht40=0;
}

static int mgmt_header(TitanMlme *m,u8 *f,int cap,u16 fc,u16 seq_ctrl)
{
    if(!m||!f||cap<24)return -1;
    memset(f,0,24);wr16(f,fc);wr16(f+2,0);
    memcpy(f+4,m->bssid,6);memcpy(f+10,m->local_mac,6);memcpy(f+16,m->bssid,6);
    wr16(f+22,seq_ctrl);return 24;
}

void titan_mlme_begin_auth(TitanMlme *m)
{
    if(!m)return;
    m->state=TITAN_MLME_AUTHENTICATING;
    m->auth_status=0xffffu;
    if(m->auth_tries<255)m->auth_tries++;
}

int titan_mlme_build_auth(TitanMlme *m,u16 seq_ctrl,u8 *out,int out_cap)
{
    int p;
    if(!m||m->state==TITAN_MLME_FAILED)return -1;
    p=mgmt_header(m,out,out_cap,IEEE80211_FC_AUTH,seq_ctrl);if(p<0||p+6>out_cap)return -2;
    wr16(out+p,0);p+=2;      /* Open System */
    wr16(out+p,1);p+=2;      /* transaction 1 */
    wr16(out+p,0);p+=2;
    return p;
}

int titan_mlme_rx_auth(TitanMlme *m,u16 algorithm,u16 transaction,u16 status)
{
    if(!m||m->state!=TITAN_MLME_AUTHENTICATING||algorithm!=0||transaction!=2)return 0;
    m->auth_status=status;
    if(status==0){m->state=TITAN_MLME_AUTHENTICATED;return 1;}
    m->state=TITAN_MLME_FAILED;return -1;
}

void titan_mlme_begin_assoc(TitanMlme *m)
{
    if(!m)return;
    m->state=TITAN_MLME_ASSOCIATING;
    m->assoc_status=0xffffu;
    m->aid=0;
    if(m->assoc_tries<255)m->assoc_tries++;
}

int titan_mlme_build_assoc(TitanMlme *m,u16 seq_ctrl,
                           const u8 *rsn_ie,int rsn_ie_len,
                           u8 *out,int out_cap)
{
    int p,n;
    if(!m||(m->state!=TITAN_MLME_AUTHENTICATED&&m->state!=TITAN_MLME_ASSOCIATING)||!out)return -1;
    p=mgmt_header(m,out,out_cap,m->reassoc?IEEE80211_FC_REASSOC_REQ:IEEE80211_FC_ASSOC_REQ,seq_ctrl);
    if(p<0)return -3;

    if(!m->reassoc){
        n=titan_full_build_assoc_body(m->assoc_capability,m->listen_interval,
                                      m->ssid,m->ssid_len,
                                      m->supp_rates,m->supp_rate_count,
                                      m->ext_rates,m->ext_rate_count,
                                      rsn_ie,rsn_ie_len,
                                      m->channel,m->use_rrm,m->rrm_caps,
                                      m->ht_cap_ie,m->use_ht,m->use_wmm,
                                      m->template_ies,m->template_ies_len,
                                      out+p,out_cap-p);
        if(n<0)return -4;
        return p+n;
    }else{
        int i;
        /* Reserve six bytes for Current AP while letting the regular body
         * builder use the caller's real frame capacity. This avoids the old
         * fixed-size temporary-body ceiling when an accepted safe template is
         * relatively large. */
        if(out_cap-p<10)return -5;
        n=titan_full_build_assoc_body(m->assoc_capability,m->listen_interval,
                                      m->ssid,m->ssid_len,
                                      m->supp_rates,m->supp_rate_count,
                                      m->ext_rates,m->ext_rate_count,
                                      rsn_ie,rsn_ie_len,
                                      m->channel,m->use_rrm,m->rrm_caps,
                                      m->ht_cap_ie,m->use_ht,m->use_wmm,
                                      m->template_ies,m->template_ies_len,
                                      out+p,out_cap-p-6);
        if(n<4)return -4;
        /* Reassociation has one extra fixed field after capability/listen:
         * Current AP Address. Shift only the IE tail, backwards for overlap. */
        for(i=n-1;i>=4;i--)out[p+i+6]=out[p+i];
        memcpy(out+p+4,m->current_ap,6);
        return p+n+6;
    }
}

int titan_mlme_rx_assoc(TitanMlme *m,u16 status,u16 aid)
{
    if(!m||m->state!=TITAN_MLME_ASSOCIATING)return 0;
    m->assoc_status=status;m->aid=(u16)(aid&0x3fffu);
    if(status==0){m->state=TITAN_MLME_ASSOCIATED;return 1;}
    m->state=TITAN_MLME_FAILED;return -1;
}
