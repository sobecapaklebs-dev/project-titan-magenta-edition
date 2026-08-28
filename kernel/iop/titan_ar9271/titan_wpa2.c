/* Minimal RSN/WPA2-Personal supplicant state for the standalone AR9271 tester.
 * Scope: WPA2-PSK, CCMP pairwise + CCMP or TKIP group, EAPOL-Key descriptor v2.
 * State and reinstallation handling follow wpa_supplicant's classic
 * TPTK->PTK model closely enough for this deliberately narrow lab.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>
#include <sysclib.h>
#include "titan_crypto.h"
#include "titan_wpa2.h"

#define EAPOL_TYPE_KEY 3
#define RSN_KEY_DESC 2
#define KI_VER_MASK 0x0007
#define KI_KEY_TYPE 0x0008
#define KI_INSTALL 0x0040
#define KI_ACK 0x0080
#define KI_MIC 0x0100
#define KI_SECURE 0x0200
#define KI_ERROR 0x0400
#define KI_REQUEST 0x0800
#define KI_ENCR 0x1000
#define KEY_FIXED 95
#define EAPOL_FIXED (4 + KEY_FIXED)
#define EAPOL_KEY_RSC_OFF 65

static u16 rbe16(const u8 *p){return ((u16)p[0]<<8)|p[1];}
static void wbe16(u8 *p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static int cmp(const u8*a,const u8*b,int n){int i;for(i=0;i<n;i++){if(a[i]<b[i])return -1;if(a[i]>b[i])return 1;}return 0;}
static int is_zero(const u8 *p,int n){int i;u8 v=0;for(i=0;i<n;i++)v|=p[i];return v==0;}

static void build_rsn(TitanWpa2 *w)
{
    static const u8 ie[22]={
        0x30,0x14, 0x01,0x00,
        0x00,0x0f,0xac,0x04,
        0x01,0x00, 0x00,0x0f,0xac,0x04,
        0x01,0x00, 0x00,0x0f,0xac,0x02,
        /* V10.8 LINUX-GOLD-PCAP: exact RSN Capabilities word from the
         * successful Linux/ath9k_htc Association Request. */
        0x8c,0x00
    };
    memcpy(w->rsn_ie,ie,sizeof(ie));
    /* The station selects the AP's group cipher while retaining CCMP as the
     * pairwise cipher. This is the mixed WPA2 profile used by many legacy APs. */
    w->rsn_ie[7]=w->group_cipher;
    w->rsn_ie_len=sizeof(ie);
}

void titan_wpa2_init(TitanWpa2 *w,const u8 pmk[32],const u8 sta[6],const u8 ap[6],const u8 entropy[16])
{
    memset(w,0,sizeof(*w));
    memcpy(w->pmk,pmk,32);memcpy(w->sta,sta,6);memcpy(w->ap,ap,6);
    if(entropy)memcpy(w->entropy,entropy,16);
    w->group_cipher=TITAN_RSN_CIPHER_CCMP;
    build_rsn(w);
}

int titan_wpa2_set_ap_rsn_ie(TitanWpa2 *w,const u8 *ie,int len)
{
    u8 group;
    if(!w)return -1;
    w->ap_rsn_ie_len=0;
    if(!ie||len==0)return 0;
    if(len<10||len>TITAN_WPA2_MAX_RSN_IE||ie[0]!=0x30||ie[1]+2!=len)return -2;
    if(ie[2]!=0x01||ie[3]!=0x00)return -3;
    if(ie[4]!=0x00||ie[5]!=0x0f||ie[6]!=0xac)return -4;
    group=ie[7];
    if(group!=TITAN_RSN_CIPHER_CCMP&&group!=TITAN_RSN_CIPHER_TKIP)return -5;
    memcpy(w->ap_rsn_ie,ie,len);w->ap_rsn_ie_len=(u8)len;
    w->group_cipher=group;
    build_rsn(w);
    return 0;
}

/* Compatibility escape hatch for APs that reject the standards-normalized
 * single-pairwise RSN IE.  This is used only after an explicit association
 * rejection and makes the Association Request and EAPOL M2 echo the AP's
 * exact RSN IE.  The normal profile remains preferred. */
int titan_wpa2_use_ap_rsn_ie_compat(TitanWpa2 *w)
{
    if(!w||w->ap_rsn_ie_len<10||w->ap_rsn_ie_len>TITAN_WPA2_MAX_RSN_IE)return -1;
    memcpy(w->rsn_ie,w->ap_rsn_ie,w->ap_rsn_ie_len);
    w->rsn_ie_len=w->ap_rsn_ie_len;
    return 0;
}

static int rsn_suite(const u8 *p,u8 type)
{
    return p&&p[0]==0x00&&p[1]==0x0f&&p[2]==0xac&&p[3]==type;
}

int titan_wpa2_use_station_rsn_ie_compat(TitanWpa2 *w,const u8 *ie,int len)
{
    int pos,count;u16 caps=0;
    if(!w||!ie||len<20||len>TITAN_WPA2_MAX_RSN_IE)return -1;
    if(ie[0]!=0x30||ie[1]+2!=len)return -2;
    if(ie[2]!=0x01||ie[3]!=0x00)return -3;
    pos=4;
    if(pos+4>len||!rsn_suite(ie+pos,w->group_cipher))return -4;
    pos+=4;
    if(pos+2>len)return -5;
    count=(int)ie[pos]|((int)ie[pos+1]<<8);pos+=2;
    /* The standalone data/key path implements CCMP pairwise only. */
    if(count!=1||pos+4>len||!rsn_suite(ie+pos,TITAN_RSN_CIPHER_CCMP))return -6;
    pos+=4;
    if(pos+2>len)return -7;
    count=(int)ie[pos]|((int)ie[pos+1]<<8);pos+=2;
    /* AKM suite type 2 is WPA2-PSK. Do not replay SAE/802.1X from a phone. */
    if(count!=1||pos+4>len||!rsn_suite(ie+pos,2))return -8;
    pos+=4;
    if(pos+2<=len){
        caps=(u16)ie[pos]|((u16)ie[pos+1]<<8);
        /* RSN Capabilities bit 6 = Management Frame Protection Required. */
        if(caps&0x0040u)return -9;
    }
    memcpy(w->rsn_ie,ie,len);w->rsn_ie_len=(u8)len;
    return 0;
}

static int ap_rsn_caps(const TitanWpa2 *w,u16 *caps)
{
    int pos,len,count;
    const u8 *ie;
    if(caps)*caps=0;
    if(!w||!caps||w->ap_rsn_ie_len<10)return -1;
    ie=w->ap_rsn_ie;len=w->ap_rsn_ie_len;pos=2;
    if(pos+2+4+2>len)return -2;
    pos+=2; /* version */
    pos+=4; /* group cipher */
    count=(int)ie[pos]|((int)ie[pos+1]<<8);pos+=2;
    if(count<=0||count>16||pos+4*count+2>len)return -3;
    pos+=4*count;
    count=(int)ie[pos]|((int)ie[pos+1]<<8);pos+=2;
    if(count<=0||count>16||pos+4*count>len)return -4;
    pos+=4*count;
    if(pos+2<=len)*caps=(u16)ie[pos]|((u16)ie[pos+1]<<8);
    return 0;
}

int titan_wpa2_use_selected_ap_caps_rsn_ie_compat(TitanWpa2 *w)
{
    u16 caps=0;
    if(!w)return -1;
    if(ap_rsn_caps(w,&caps)<0)return -2;
    build_rsn(w);
    if(w->rsn_ie_len<22)return -3;
    /* The target scan already rejects MFPR, so copying this word cannot turn
     * an unsupported mandatory-PMF BSS into an apparently usable one. */
    w->rsn_ie[20]=(u8)caps;w->rsn_ie[21]=(u8)(caps>>8);
    return 0;
}

void titan_wpa2_use_selected_rsn_ie(TitanWpa2 *w)
{
    if(w)build_rsn(w);
}

const u8 *titan_wpa2_rsn_ie(const TitanWpa2 *w,int *len){if(len)*len=w?w->rsn_ie_len:0;return w?w->rsn_ie:NULL;}
const u8 *titan_wpa2_tk(const TitanWpa2 *w){return (w&&w->have_ptk)?w->ptk+32:NULL;}
const u8 *titan_wpa2_gtk(const TitanWpa2 *w,u8 *keyid){if(keyid)*keyid=w?w->gtk_keyid:0;return (w&&w->have_gtk)?w->gtk:NULL;}
int titan_wpa2_gtk_len(const TitanWpa2 *w){return (w&&w->have_gtk)?w->gtk_len:0;}
int titan_wpa2_build_tkip_local_gtk(const TitanWpa2 *w,u8 out[32])
{
    if(!w||!out||!w->have_gtk||w->group_cipher!=TITAN_RSN_CIPHER_TKIP||w->gtk_len!=32)return -1;
    memcpy(out,w->gtk,16);
    /* GTK KDE MIC subkeys are from the authenticator perspective. A station
     * receives group traffic, so AP-RX becomes local-TX and AP-TX becomes
     * local-RX, matching the key layout expected by mac80211/ath. */
    memcpy(out+16,w->gtk+24,8);
    memcpy(out+24,w->gtk+16,8);
    return 0;
}
const u8 *titan_wpa2_installed_tkip_rx_mic(const TitanWpa2 *w)
{
    if(!w||!w->gtk_hw_installed||w->group_cipher!=TITAN_RSN_CIPHER_TKIP||w->installed_gtk_len!=32)return NULL;
    /* installed_gtk stores raw KDE bytes: authenticator TX-MIC is local RX. */
    return w->installed_gtk+16;
}
u8 titan_wpa2_group_cipher(const TitanWpa2 *w){return w?w->group_cipher:0;}
const u8 *titan_wpa2_gtk_rsc(const TitanWpa2 *w){return (w&&w->have_gtk)?w->gtk_rsc:NULL;}
int titan_wpa2_ptk_install_needed(const TitanWpa2 *w){return w&&w->ptk_install_pending;}
int titan_wpa2_gtk_install_needed(const TitanWpa2 *w){return w&&w->gtk_install_pending;}

static u64 rsc_to_pn(const u8 rsc[8])
{
    return (u64)rsc[0]|((u64)rsc[1]<<8)|((u64)rsc[2]<<16)|
           ((u64)rsc[3]<<24)|((u64)rsc[4]<<32)|((u64)rsc[5]<<40);
}

void titan_wpa2_mark_ptk_installed(TitanWpa2 *w)
{
    if(!w||!w->have_ptk||!w->ptk_install_pending)return;
    /* Only an actual new-key install resets pairwise replay state. Duplicate
     * M3 frames never reach this function, so their packet numbers survive. */
    memcpy(w->installed_ptk,w->ptk,sizeof(w->installed_ptk));
    memset(w->pairwise_rx_pn,0,sizeof(w->pairwise_rx_pn));
    w->ptk_hw_installed=1;w->ptk_install_pending=0;
    if(w->gtk_hw_installed)w->secured=1;
}

void titan_wpa2_mark_gtk_installed(TitanWpa2 *w)
{
    int i;u64 pn;
    if(!w||!w->have_gtk)return;
    memset(w->installed_gtk,0,sizeof(w->installed_gtk));
    memcpy(w->installed_gtk,w->gtk,w->gtk_len);w->installed_gtk_keyid=w->gtk_keyid;w->installed_gtk_len=w->gtk_len;
    w->gtk_hw_installed=1;w->gtk_install_pending=0;
    pn=rsc_to_pn(w->gtk_rsc);
    for(i=0;i<17;i++)w->gtk_rx_pn[i]=pn;
    if(w->ptk_hw_installed)w->secured=1;
}

static void clear_transient_handshake(TitanWpa2 *w)
{
    /* A new M1 can be a PTK rekey. Keep the currently installed PTK/GTK and
     * their replay counters alive until the new M3 has been authenticated.
     * Only the candidate/handshake state is discarded here. */
    memset(w->tptk,0,sizeof(w->tptk));
    memset(w->anonce,0,sizeof(w->anonce));
    memset(w->m3_replay,0,sizeof(w->m3_replay));
    w->have_tptk=0;w->have_m1=0;w->have_m3=0;
    w->ptk_install_pending=0;w->gtk_install_pending=0;
}

static void make_snonce(TitanWpa2 *w)
{
    u8 seed[32+6+6+16+2],d1[20],d2[20];
    memcpy(seed,w->anonce,32);memcpy(seed+32,w->sta,6);memcpy(seed+38,w->ap,6);
    memcpy(seed+44,w->entropy,16);seed[60]=0x54;seed[61]=0x31;
    titan_hmac_sha1(w->pmk,32,seed,sizeof(seed),d1);seed[61]=0x32;
    titan_hmac_sha1(w->pmk,32,seed,sizeof(seed),d2);
    memcpy(w->snonce,d1,20);memcpy(w->snonce+20,d2,12);
    memset(seed,0,sizeof(seed));memset(d1,0,sizeof(d1));memset(d2,0,sizeof(d2));
}

static void derive_tptk(TitanWpa2 *w)
{
    u8 data[76],*p=data;const u8 *m1,*m2,*n1,*n2;
    if(cmp(w->sta,w->ap,6)<0){m1=w->sta;m2=w->ap;}else{m1=w->ap;m2=w->sta;}
    if(cmp(w->snonce,w->anonce,32)<0){n1=w->snonce;n2=w->anonce;}else{n1=w->anonce;n2=w->snonce;}
    memcpy(p,m1,6);p+=6;memcpy(p,m2,6);p+=6;memcpy(p,n1,32);p+=32;memcpy(p,n2,32);
    titan_wpa_prf512(w->pmk,(const u8*)"Pairwise key expansion",22,data,sizeof(data),w->tptk);
    w->have_tptk=1;memset(data,0,sizeof(data));
}

static void mic16(const u8 kck[16],u8 *eapol,int len,u8 out[16])
{u8 d[20];titan_hmac_sha1(kck,16,eapol,(u32)len,d);memcpy(out,d,16);memset(d,0,sizeof(d));}

static int build_reply(TitanWpa2 *w,const u8 *rx,int rxlen,int message,const u8 ptk[64],u8*out,int cap,int*outlen)
{
    u16 ver=rbe16(rx+5)&KI_VER_MASK,ki;int kdlen=(message==2)?w->rsn_ie_len:0,total=EAPOL_FIXED+kdlen;u8 m[16];
    if(!ptk||cap<total||rxlen<EAPOL_FIXED)return -1;
    memset(out,0,total);out[0]=rx[0];out[1]=EAPOL_TYPE_KEY;wbe16(out+2,(u16)(KEY_FIXED+kdlen));out[4]=RSN_KEY_DESC;
    ki=ver|KI_KEY_TYPE|KI_MIC;if(message==4&&(rbe16(rx+5)&KI_SECURE))ki|=KI_SECURE;wbe16(out+5,ki);wbe16(out+7,0);
    memcpy(out+9,rx+9,8);if(message==2)memcpy(out+17,w->snonce,32);wbe16(out+97,(u16)kdlen);
    if(kdlen)memcpy(out+99,w->rsn_ie,kdlen);
    memset(out+81,0,16);mic16(ptk,out,total,m);memcpy(out+81,m,16);
    memset(m,0,sizeof(m));if(outlen)*outlen=total;return 0;
}

typedef struct TitanKeyDataParsed {u8 gtk[32];u8 gtk_keyid;u8 gtk_len;u8 have_gtk;u8 rsn_ie[TITAN_WPA2_MAX_RSN_IE];u8 rsn_ie_len;} TitanKeyDataParsed;

static int parse_key_data(const u8 *data,int len,TitanKeyDataParsed *o)
{
    int p=0;if(!o)return -1;memset(o,0,sizeof(*o));
    while(p+2<=len){int id=data[p],n=data[p+1];const u8 *v=data+p+2;if(p+2+n>len)return -2;
        if(id==0x30){int full=n+2;if(full>TITAN_WPA2_MAX_RSN_IE)return -3;memcpy(o->rsn_ie,data+p,full);o->rsn_ie_len=(u8)full;}
        else if(id==0xdd&&n>=22&&v[0]==0x00&&v[1]==0x0f&&v[2]==0xac&&v[3]==0x01){int glen=n-6;if(glen!=16&&glen!=32)return -5;o->gtk_keyid=v[4]&3;o->gtk_len=(u8)glen;memcpy(o->gtk,v+6,glen);o->have_gtk=1;}
        /* RFC3394 plaintext is often padded with a 0xdd element or zero bytes. */
        p+=2+n;
    }
    while(p<len&&data[p]==0)p++;
    return p==len?0:-4;
}

static int validate_m3_rsn(const TitanWpa2 *w,const TitanKeyDataParsed *kd)
{
    if(!w->ap_rsn_ie_len)return 0;
    if(!kd->rsn_ie_len)return -1;
    if(kd->rsn_ie_len!=w->ap_rsn_ie_len)return -2;
    return cmp(kd->rsn_ie,w->ap_rsn_ie,w->ap_rsn_ie_len)==0?0:-3;
}

int titan_wpa2_rx_key(TitanWpa2 *w,const u8 *e,int len,u8*out,int cap,int*outlen)
{
    u16 eplen,ki,keylen,kdlen;const u8 *nonce,*kd,*mic_key;u8 mic_rx[16],mic_calc[16],tmp[TITAN_WPA2_EAPOL_MAX],plain[256];
    u32 plain_len=0;int total,ver,rc,replay_cmp,expected_gtk_len;TitanKeyDataParsed parsed;
    if(outlen)*outlen=0;
    if(!w||!e||len<EAPOL_FIXED||e[1]!=EAPOL_TYPE_KEY||e[4]!=RSN_KEY_DESC)return TITAN_WPA2_IGNORE;
    eplen=rbe16(e+2);total=4+(int)eplen;if(total>len||total>EAPOL_FIXED+256||total>(int)sizeof(tmp))return -10;
    ki=rbe16(e+5);ver=ki&KI_VER_MASK;keylen=rbe16(e+7);nonce=e+17;kdlen=rbe16(e+97);kd=e+99;if(99+(int)kdlen>total)return -11;
    if(ver!=2||!(ki&KI_KEY_TYPE)||(ki&(KI_ERROR|KI_REQUEST)))return TITAN_WPA2_IGNORE;

    /* Message 1/4. A retransmission with identical replay+ANonce reuses the
       same SNonce/TPTK; a genuinely newer M1 starts a fresh handshake. */
    if((ki&KI_ACK)&&!(ki&KI_MIC)){
        if(keylen!=16||(ki&(KI_INSTALL|KI_SECURE|KI_ENCR)))return -12;
        if(w->have_m1){
            replay_cmp=cmp(e+9,w->replay,8);
            if(replay_cmp<0)return -13;
            if(replay_cmp==0){if(cmp(nonce,w->anonce,32)!=0)return -14;if(!w->have_tptk)return -15;
                return build_reply(w,e,total,2,w->tptk,out,cap,outlen)<0?-16:TITAN_WPA2_SEND_M2;}
            clear_transient_handshake(w);
        }
        if(is_zero(nonce,32))return -17;
        memcpy(w->anonce,nonce,32);memcpy(w->replay,e+9,8);w->descriptor_version=(u8)ver;w->eapol_version=e[0];
        make_snonce(w);derive_tptk(w);w->have_m1=1;
        if(build_reply(w,e,total,2,w->tptk,out,cap,outlen)<0)return -18;
        return TITAN_WPA2_SEND_M2;
    }

    /* Message 3/4. Verify MIC with TPTK before promotion, validate AP RSN IE,
       retain Key RSC, and never mark keys installed merely because M3 arrived. */
    if((ki&KI_ACK)&&(ki&KI_MIC)){
        if(!w->have_m1||(!w->have_tptk&&!w->have_ptk)||keylen!=16||cmp(w->anonce,nonce,32)!=0)return -20;
        if(!(ki&KI_INSTALL)||!(ki&KI_SECURE))return -21;
        replay_cmp=cmp(e+9,w->replay,8);
        if(replay_cmp<0)return -22;
        if(!w->have_m3&&replay_cmp==0)return -23; /* first M3 must advance M1 replay */
        /* During a PTK rekey the old PTK may still be installed. M3 must be
           authenticated with the newly derived temporary PTK, exactly like
           wpa_supplicant does; use the active PTK only for an M3 duplicate
           after the temporary key has already been promoted. */
        mic_key=w->have_tptk?w->tptk:w->ptk;
        memcpy(tmp,e,total);memcpy(mic_rx,tmp+81,16);memset(tmp+81,0,16);mic16(mic_key,tmp,total,mic_calc);
        if(!titan_consttime_eq(mic_rx,mic_calc,16))return -24;
        if(kdlen){
            if(!(ki&KI_ENCR))return -25;
            if(titan_aes_key_unwrap(mic_key+16,kd,kdlen,plain,sizeof(plain),&plain_len)<0)return -26;
            rc=parse_key_data(plain,(int)plain_len,&parsed);if(rc<0)return -27;
        }else memset(&parsed,0,sizeof(parsed));
        if(validate_m3_rsn(w,&parsed)<0)return -28;
        if(!parsed.have_gtk)return -29;
        expected_gtk_len=(w->group_cipher==TITAN_RSN_CIPHER_TKIP)?32:16;
        if(parsed.gtk_len!=expected_gtk_len)return -31;

        if(w->have_tptk){
            memcpy(w->ptk,w->tptk,64);w->have_ptk=1;
            /* Do not reinstall an already active PTK. For a real rekey the
             * candidate differs from installed_ptk, so hardware overwrite is
             * requested exactly once. */
            w->ptk_install_pending=!w->ptk_hw_installed||cmp(w->installed_ptk,w->ptk,64)!=0;
            memset(w->tptk,0,sizeof(w->tptk));w->have_tptk=0;
        }else if(w->have_ptk){
            w->ptk_install_pending=!w->ptk_hw_installed||cmp(w->installed_ptk,w->ptk,64)!=0;
        }

        /* Preserve RSC and only request GTK installation when it is new. */
        memcpy(w->gtk_rsc,e+EAPOL_KEY_RSC_OFF,8);
        memset(w->gtk,0,sizeof(w->gtk));memcpy(w->gtk,parsed.gtk,parsed.gtk_len);
        w->gtk_len=parsed.gtk_len;w->gtk_keyid=parsed.gtk_keyid;w->have_gtk=1;
        if(!w->gtk_hw_installed||w->installed_gtk_keyid!=w->gtk_keyid||w->installed_gtk_len!=w->gtk_len||cmp(w->installed_gtk,w->gtk,w->gtk_len)!=0)w->gtk_install_pending=1;
        else w->gtk_install_pending=0;
        memcpy(w->m3_replay,e+9,8);memcpy(w->replay,e+9,8);w->have_m3=1;
        if(build_reply(w,e,total,4,w->ptk,out,cap,outlen)<0)return -30;
        return TITAN_WPA2_SEND_M4;
    }
    return TITAN_WPA2_IGNORE;
}
