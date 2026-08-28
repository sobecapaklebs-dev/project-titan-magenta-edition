#include <sysclib.h>
#include "titan_ath9k_rate.h"

#define IEEE80211_HT_CAP_SGI_20   0x0020u
#define IEEE80211_HT_CAP_RX_STBC  0x0300u

static void put_be32(u8 *p,u32 v)
{
    p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;
}

int titan_ath9k_build_target_rate(u8 out[TITAN_ATH9K_TARGET_RATE_LEN],
                                  u8 sta_index,
                                  const u8 *legacy,u8 legacy_count,
                                  u8 use_ht,const u8 *ap_ht_cap)
{
    u32 caps=0;
    u16 peer_cap=0;
    int i,nr=0,nh=0;

    if(!out)return -1;
    if(legacy_count && !legacy)return -2;
    if(legacy_count>TITAN_ATH9K_RATE_MAX)legacy_count=TITAN_ATH9K_RATE_MAX;

    memset(out,0,TITAN_ATH9K_TARGET_RATE_LEN);
    out[0]=sta_index;
    out[1]=1; /* isnew: same initial peer-rate update as ath9k_htc. */

    for(i=0;i<legacy_count && nr<TITAN_ATH9K_RATE_MAX;i++)
        out[9+nr++]=(u8)(legacy[i]&0x7fu);
    out[8]=(u8)nr;

    if(use_ht && ap_ht_cap){
        peer_cap=(u16)(ap_ht_cap[0]|((u16)ap_ht_cap[1]<<8));

        /* HT Capability payload byte 3 is RX MCS mask byte 0. AR9271 is 1SS,
         * therefore Linux's effective local/peer intersection is MCS0..7. */
        for(i=0;i<8 && nh<TITAN_ATH9K_RATE_MAX;i++)
            if(ap_ht_cap[3]&(1u<<i))out[40+nh++]=(u8)i;
        out[39]=(u8)nh;

        if(nh){
            caps|=TITAN_WLAN_RC_HT_FLAG;
            if(peer_cap&IEEE80211_HT_CAP_SGI_20)caps|=TITAN_WLAN_RC_SGI_FLAG;
            if(peer_cap&IEEE80211_HT_CAP_RX_STBC)caps|=TITAN_ATH_RC_TX_STBC_FLAG;
        }
    }

    put_be32(out+4,caps);
    return TITAN_ATH9K_TARGET_RATE_LEN;
}
