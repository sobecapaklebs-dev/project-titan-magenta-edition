#include <types.h>
#include <sysclib.h>
#include "titan_full_assoc.h"

/* IDs used by the 2.4 GHz WPA2/HT managed-station path. */
#define EID_SSID                 0
#define EID_RATES                1
#define EID_POWER_CAP           33
#define EID_SUPPORTED_CHANNELS  36
#define EID_HT_CAP              45
#define EID_QOS_CAP             46
#define EID_RSN                 48
#define EID_XRATES              50
#define EID_SUPPORTED_OPCLASS   59
#define EID_RRM_CAP             70
#define EID_BSS_COEX            72
#define EID_EXT_CAP             127
#define EID_EXTENSION           255
#define EID_VENDOR              221

static void le16enc(u8 *p,u16 v){p[0]=(u8)v;p[1]=(u8)(v>>8);}

static int add_ie(u8 *out,int p,int cap,u8 id,const u8 *body,int len)
{
    if(!out||p<0||len<0||len>255||p+2+len>cap)return -1;
    out[p++]=id;out[p++]=(u8)len;
    if(len){memcpy(out+p,body,(unsigned int)len);p+=len;}
    return p;
}

static int add_rsn(u8 *out,int p,int cap,const u8 *rsn,int rsn_len)
{
    if(!rsn||rsn_len<2||rsn_len>66||rsn[0]!=EID_RSN||
       (int)rsn[1]+2!=rsn_len||p+rsn_len>cap)return -1;
    memcpy(out+p,rsn,(unsigned int)rsn_len);return p+rsn_len;
}

static int add_wmm_info(u8 *out,int p,int cap)
{
    static const u8 wmm[7]={0x00,0x50,0xf2,0x02,0x00,0x01,0x00};
    return add_ie(out,p,cap,EID_VENDOR,wmm,sizeof(wmm));
}

/* Exact non-secret compatibility IEs captured from the successful Linux
 * Association Request of this same AR9271 on BlueWeb-SOMOSLUZ 2.4. */
static int add_linux_gold_compat(u8 *out,int p,int cap,int stage)
{
    static const u8 power[2]={0x00,0x14};
    static const u8 extcap[10]={0x04,0x00,0x0a,0x02,0x01,0x40,0x00,0x40,0x00,0x01};
    static const u8 rrm[5]={0x70,0x00,0x00,0x00,0x00};
    static const u8 opclass[4]={0x51,0x51,0x53,0x54};
    if(stage==0)return add_ie(out,p,cap,EID_POWER_CAP,power,sizeof(power));
    p=add_ie(out,p,cap,EID_EXT_CAP,extcap,sizeof(extcap));if(p<0)return p;
    p=add_ie(out,p,cap,EID_RRM_CAP,rrm,sizeof(rrm));if(p<0)return p;
    return add_ie(out,p,cap,EID_SUPPORTED_OPCLASS,opclass,sizeof(opclass));
}

static int is_wmm_info(const u8 *v,int n)
{
    return v&&n>=7&&v[0]==0x00&&v[1]==0x50&&v[2]==0xf2&&v[3]==0x02;
}

/* Linux 5.4 mac80211 generates Power Capability/Supported Channels only when
 * the station itself enables Spectrum Management or RRM.  The Titan proof
 * client enables neither, so the direct managed path intentionally does not
 * manufacture Power, Supported Operating Class, RRM, or zero Extended-Cap IEs.
 * Supplicant-owned RSN is kept before HT; WMM Information is generated after
 * HT, matching ieee80211_send_assoc(). */

/* Whitelist compatibility IEs from a proven accepted station. We deliberately
 * do not clone VHT/HE/EHT or arbitrary vendor/device IEs onto an AR9271. */
static int safe_template_ie(u8 id,const u8 *v,int len)
{
    (void)v;
    switch(id){
        case EID_POWER_CAP:          return len==2;
        case EID_SUPPORTED_CHANNELS: return len>=2 && !(len&1);
        case EID_QOS_CAP:            return len==1;
        case EID_SUPPORTED_OPCLASS:  return len>=1;
        case EID_RRM_CAP:            return len==5;
        case EID_BSS_COEX:           return len==1;
        case EID_EXT_CAP:            return len>=1 && len<=16;
        default:                     return 0;
    }
}

static int build_from_template(u16 capability,u16 listen_interval,
                               const u8 *ssid,u8 ssid_len,
                               const u8 *supp,u8 nsupp,const u8 *ext,u8 next,
                               const u8 *rsn,int rsn_len,
                               const u8 *ht,u8 add_ht,u8 add_wmm,
                               const u8 *tpl,int tpl_len,u8 *out,int out_cap)
{
    int p=0,t=0,ssid_done=0,supp_done=0,ext_done=0,rsn_done=0,ht_done=0,wmm_done=0;
    if(out_cap<4)return -1;
    le16enc(out+p,capability);p+=2;le16enc(out+p,listen_interval);p+=2;
    while(t+2<=tpl_len){
        int id=tpl[t],n=tpl[t+1];const u8 *v;
        t+=2;if(t+n>tpl_len)return -2;v=tpl+t;
        if(id==EID_SSID){if(!ssid_done){p=add_ie(out,p,out_cap,EID_SSID,ssid,ssid_len);ssid_done=1;}}
        else if(id==EID_RATES){if(!supp_done){p=add_ie(out,p,out_cap,EID_RATES,supp,nsupp);supp_done=1;}}
        else if(id==EID_XRATES){if(next&&!ext_done){p=add_ie(out,p,out_cap,EID_XRATES,ext,next);ext_done=1;}}
        else if(id==EID_RSN){if(!rsn_done){p=add_rsn(out,p,out_cap,rsn,rsn_len);rsn_done=1;}}
        else if(id==EID_HT_CAP){if(add_ht&&!ht_done){p=add_ie(out,p,out_cap,EID_HT_CAP,ht,26);ht_done=1;}}
        else if(id==EID_VENDOR && is_wmm_info(v,n)){if(add_wmm&&!wmm_done){p=add_wmm_info(out,p,out_cap);wmm_done=1;}}
        else if(id!=EID_EXTENSION && safe_template_ie((u8)id,v,n)){
            /* Power is clamped to AR9271's proof-stack ceiling. */
            if(id==EID_POWER_CAP){u8 power[2]={v[0],v[1]>20?20:v[1]};p=add_ie(out,p,out_cap,(u8)id,power,2);}
            else p=add_ie(out,p,out_cap,(u8)id,v,n);
        }
        if(p<0)return -3;
        t+=n;
    }
    if(!ssid_done){p=add_ie(out,p,out_cap,EID_SSID,ssid,ssid_len);if(p<0)return -3;}
    if(!supp_done){p=add_ie(out,p,out_cap,EID_RATES,supp,nsupp);if(p<0)return -3;}
    if(next&&!ext_done){p=add_ie(out,p,out_cap,EID_XRATES,ext,next);if(p<0)return -3;}
    if(!rsn_done){p=add_rsn(out,p,out_cap,rsn,rsn_len);if(p<0)return -3;}
    if(add_ht&&!ht_done){p=add_ie(out,p,out_cap,EID_HT_CAP,ht,26);if(p<0)return -3;}
    if(add_wmm&&!wmm_done){p=add_wmm_info(out,p,out_cap);if(p<0)return -3;}
    return p;
}

int titan_full_build_assoc_body(u16 capability,u16 listen_interval,
                                const u8 *ssid,u8 ssid_len,
                                const u8 *supp_rates,u8 supp_rate_count,
                                const u8 *ext_rates,u8 ext_rate_count,
                                const u8 *rsn_ie,int rsn_ie_len,
                                u8 channel,u8 use_rrm,const u8 rrm_caps[5],
                                const u8 *ht_cap,u8 add_ht,u8 add_wmm,
                                const u8 *template_ies,int template_ies_len,
                                u8 *out,int out_cap)
{
    int p=0;
    if(!ssid||!ssid_len||!supp_rates||!supp_rate_count||supp_rate_count>8||
       ext_rate_count>22||!out||out_cap<4)return -1;
    if(add_ht&&!ht_cap)return -2;
    if(template_ies&&template_ies_len>0)
        return build_from_template(capability,listen_interval,ssid,ssid_len,
                                   supp_rates,supp_rate_count,ext_rates,ext_rate_count,
                                   rsn_ie,rsn_ie_len,ht_cap,add_ht,add_wmm,
                                   template_ies,template_ies_len,out,out_cap);

    le16enc(out+p,capability);p+=2;le16enc(out+p,listen_interval);p+=2;
    p=add_ie(out,p,out_cap,EID_SSID,ssid,ssid_len);if(p<0)return -3;
    p=add_ie(out,p,out_cap,EID_RATES,supp_rates,supp_rate_count);if(p<0)return -3;
    if(ext_rate_count){p=add_ie(out,p,out_cap,EID_XRATES,ext_rates,ext_rate_count);if(p<0)return -3;}
    /* V10.8 gold order, observed byte-for-byte on the accepted Linux request:
     * Rates -> Extended Rates -> Power -> RSN -> HT -> Extended Capabilities
     * -> RRM Enabled Capabilities -> Supported Operating Classes -> WMM. */
    (void)channel;(void)use_rrm;(void)rrm_caps;
    p=add_linux_gold_compat(out,p,out_cap,0);if(p<0)return -3;
    p=add_rsn(out,p,out_cap,rsn_ie,rsn_ie_len);if(p<0)return -3;
    if(add_ht){p=add_ie(out,p,out_cap,EID_HT_CAP,ht_cap,26);if(p<0)return -3;}
    p=add_linux_gold_compat(out,p,out_cap,1);if(p<0)return -3;
    if(add_wmm){p=add_wmm_info(out,p,out_cap);if(p<0)return -3;}
    return p;
}
