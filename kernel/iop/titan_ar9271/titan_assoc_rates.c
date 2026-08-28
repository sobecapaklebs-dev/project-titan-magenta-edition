#include <types.h>
#include <sysclib.h>
#include "titan_assoc_rates.h"

static const u8 legacy_rates_24g[12] = {
    0x02,0x04,0x0b,0x16,0x0c,0x12,0x18,0x24,0x30,0x48,0x60,0x6c
};

int titan_assoc_legacy_rate_index(u8 encoded)
{
    int i;u8 r=(u8)(encoded&0x7fu);
    for(i=0;i<(int)sizeof(legacy_rates_24g);i++)if(legacy_rates_24g[i]==r)return i;
    return -1;
}

static int ap_contains(const u8 *supp,int nsupp,const u8 *ext,int next,u8 rate)
{
    int i;
    for(i=0;i<nsupp;i++)if((supp[i]&0x7fu)==rate)return 1;
    for(i=0;i<next;i++)if((ext[i]&0x7fu)==rate)return 1;
    return 0;
}

static int output_contains(const TitanAssocRateIes *o,u8 rate)
{
    int i;
    for(i=0;i<o->supp_len;i++)if((o->supp[i]&0x7fu)==rate)return 1;
    for(i=0;i<o->ext_len;i++)if((o->ext[i]&0x7fu)==rate)return 1;
    return 0;
}

static void account(TitanAssocRateIes *o,u8 v)
{
    int bit=titan_assoc_legacy_rate_index(v);
    ++o->total_len;
    if(bit>=0){
        o->legacy_mask|=(u16)(1u<<bit);
        if(v&0x80u)o->basic_mask|=(u16)(1u<<bit);
    }
}

static int append_linear(TitanAssocRateIes *o,u8 v)
{
    if(o->supp_len<8){o->supp[o->supp_len++]=v;account(o,v);return 0;}
    if(o->ext_len<22){o->ext[o->ext_len++]=v;account(o,v);return 0;}
    return -1;
}

int titan_assoc_build_rate_ies(const u8 *ap_supp,int nsupp,
                               const u8 *ap_ext,int next,
                               int mode,TitanAssocRateIes *o)
{
    int i;u8 v,r;
    static const u8 fallback[4]={0x02,0x04,0x0b,0x16};
    if(!o)return -1;
    memset(o,0,sizeof(*o));
    if(!ap_supp||nsupp<0)nsupp=0;
    if(nsupp>8)nsupp=8;
    if(!ap_ext||next<0)next=0;
    if(next>22)next=22;

    if(mode==TITAN_ASSOC_RATE_MODE_AP_EXACT || mode==TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR){
        int clear=(mode==TITAN_ASSOC_RATE_MODE_AP_EXACT_CLR);
        for(i=0;i<nsupp;i++){
            v=ap_supp[i];if(titan_assoc_legacy_rate_index(v)<0)continue;
            if(clear)v&=0x7fu;
            o->supp[o->supp_len++]=v;account(o,v);
        }
        for(i=0;i<next;i++){
            v=ap_ext[i];if(titan_assoc_legacy_rate_index(v)<0)continue;
            if(clear)v&=0x7fu;
            o->ext[o->ext_len++]=v;account(o,v);
        }
        if(o->supp_len>0)return 0;
        memset(o,0,sizeof(*o));
        mode=TITAN_ASSOC_RATE_MODE_MAC80211;
    }

    if(mode==TITAN_ASSOC_RATE_MODE_BASIC_ONLY){
        for(i=0;i<nsupp;i++){
            v=ap_supp[i];if(!(v&0x80u))continue;r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,r)<0)return -2;
        }
        for(i=0;i<next;i++){
            v=ap_ext[i];if(!(v&0x80u))continue;r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,r)<0)return -2;
        }
        if(o->total_len>0)return 0;
        memset(o,0,sizeof(*o));
        mode=TITAN_ASSOC_RATE_MODE_MAC80211;
    }

    if(mode==TITAN_ASSOC_RATE_MODE_BASIC_ONLY_BITS){
        /* Diagnostic/vendor-compat form: advertise only rates that the AP
         * marked Basic, while preserving the Basic marker itself. This is
         * intentionally separate from the mac80211-style BASIC_ONLY mode,
         * which clears bit 7 in a station Association Request. */
        for(i=0;i<nsupp;i++){
            v=ap_supp[i];if(!(v&0x80u))continue;r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,v)<0)return -2;
        }
        for(i=0;i<next;i++){
            v=ap_ext[i];if(!(v&0x80u))continue;r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,v)<0)return -2;
        }
        if(o->total_len>0)return 0;
        memset(o,0,sizeof(*o));
        mode=TITAN_ASSOC_RATE_MODE_MAC80211;
    }

    if(mode==TITAN_ASSOC_RATE_MODE_MAC80211){
        for(i=0;i<(int)sizeof(legacy_rates_24g);i++){
            r=legacy_rates_24g[i];
            if(ap_contains(ap_supp,nsupp,ap_ext,next,r) && append_linear(o,r)<0)return -2;
        }
    }else if(mode==TITAN_ASSOC_RATE_MODE_AP_COMBINED){
        for(i=0;i<nsupp;i++){
            v=ap_supp[i];r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,v)<0)return -2;
        }
        for(i=0;i<next;i++){
            v=ap_ext[i];r=(u8)(v&0x7fu);
            if(titan_assoc_legacy_rate_index(r)<0||output_contains(o,r))continue;
            if(append_linear(o,v)<0)return -2;
        }
    }else return -3;

    if(o->supp_len==0){
        memset(o,0,sizeof(*o));
        for(i=0;i<4;i++)if(append_linear(o,fallback[i])<0)return -2;
    }
    return 0;
}
