#include <string.h>
#include "titan_assoc_wire.h"

static u16 rd_le16_wire(const u8 *p){return (u16)(p[0]|((u16)p[1]<<8));}
static int eq6(const u8 *a,const u8 *b){return memcmp(a,b,6)==0;}

u32 titan_assoc_fnv1a32(const u8 *data,int len)
{
    u32 h=2166136261u;int i;
    if(!data||len<0)return 0;
    for(i=0;i<len;i++){h^=(u32)data[i];h*=16777619u;}
    return h;
}

/* Encoded IEEE 802.11 legacy rate -> AR9271/Linux 2.4-GHz bit position.
 * The Basic bit is deliberately ignored for identity. */
static int legacy_rate_index(u8 encoded)
{
    static const u8 rates[12]={0x02,0x04,0x0b,0x16,0x0c,0x12,0x18,0x24,0x30,0x48,0x60,0x6c};
    u8 v=(u8)(encoded&0x7fu);int i;
    for(i=0;i<12;i++)if(rates[i]==v)return i;
    return -1;
}

static int parse_rates(const u8 *p,int n,u16 *legacy,u16 *basic)
{
    int i,idx;u16 bit;
    if(!p||n<1)return -1;
    for(i=0;i<n;i++){
        idx=legacy_rate_index(p[i]);if(idx<0)return -2;
        bit=(u16)(1u<<idx);
        if((*legacy)&bit)return -3; /* duplicate rate across IE1/IE50 */
        *legacy|=bit;
        if(p[i]&0x80u)*basic|=bit;
    }
    return 0;
}

int titan_assoc_wire_check(const u8 *f,int len,
                           const u8 bssid[6],const u8 sta[6],
                           const u8 *ssid,int ssid_len,
                           u16 target_basic_mask,
                           int expect_rsn,int expect_ht,int expect_wmm,
                           TitanAssocWireResult *out)
{
    TitanAssocWireResult r;int pos=28,last_rank=0;
    int ssid_seen=0,supp_seen=0,ext_seen=0,power_seen=0,rsn_seen=0,ht_seen=0;
    int extcap_seen=0,rrm_seen=0,opclass_seen=0,wmm_seen=0;
    u16 fc;
    memset(&r,0,sizeof(r));
    if(!f||!bssid||!sta||!ssid||!out||len<32||ssid_len<0||ssid_len>32)return 2;
    r.dot11_hash=titan_assoc_fnv1a32(f,len);
    fc=rd_le16_wire(f);
    /* Active V10.6 path is Association Request only, not reassociation. */
    if((fc&0x00fcu)!=0x0000u)return 3;
    if(!eq6(f+10,sta))return 4;
    r.flags|=TITAN_ASSOC_WIRE_F_STA_ADDR;
    if(!eq6(f+4,bssid)||!eq6(f+16,bssid))return 5;
    r.flags|=TITAN_ASSOC_WIRE_F_BSSID_ADDRS;

    while(pos+2<=len){
        int id=f[pos],ilen=f[pos+1],rank=0;const u8 *v;
        pos+=2;if(ilen<0||pos+ilen>len)return 6;v=f+pos;++r.ie_count;
        if(id==0){
            rank=1;if(ssid_seen++)return 7;
            if(ilen!=ssid_len||memcmp(v,ssid,ssid_len)!=0)return 8;
        }else if(id==1){
            rank=2;if(supp_seen++||ilen<1||ilen>8)return 9;
            r.supp_len=(u8)ilen;
            if(parse_rates(v,ilen,&r.legacy_mask,&r.basic_marker_mask)<0)return 10;
        }else if(id==50){
            rank=3;if(ext_seen++||ilen<1||ilen>22)return 11;
            r.ext_len=(u8)ilen;
            if(parse_rates(v,ilen,&r.legacy_mask,&r.basic_marker_mask)<0)return 12;
        }else if(id==33){
            static const u8 gold_power[2]={0x00,0x14};
            rank=4;if(power_seen++||ilen!=2||memcmp(v,gold_power,2)!=0)return 13;
        }else if(id==48){
            rank=5;if(rsn_seen++||ilen!=20)return 14;
            /* Gold request: WPA2-PSK, TKIP group, CCMP pairwise, RSN caps 0x008c. */
            if(v[18]!=0x8c||v[19]!=0x00)return 15;
        }else if(id==45){
            static const u8 gold_ht[26]={
                0x6e,0x11,0x1b,0xff,0,0,0,0,0,0,0,0,0,0,0,0x01,
                0,0,0,0,0,0,0,0,0,0
            };
            rank=6;if(ht_seen++||ilen!=26||memcmp(v,gold_ht,26)!=0)return 16;
        }else if(id==127){
            static const u8 gold_extcap[10]={0x04,0x00,0x0a,0x02,0x01,0x40,0x00,0x40,0x00,0x01};
            rank=7;if(extcap_seen++||ilen!=10||memcmp(v,gold_extcap,10)!=0)return 17;
        }else if(id==70){
            static const u8 gold_rrm[5]={0x70,0x00,0x00,0x00,0x00};
            rank=8;if(rrm_seen++||ilen!=5||memcmp(v,gold_rrm,5)!=0)return 18;
        }else if(id==59){
            static const u8 gold_opclass[4]={0x51,0x51,0x53,0x54};
            rank=9;if(opclass_seen++||ilen!=4||memcmp(v,gold_opclass,4)!=0)return 19;
        }else if(id==221 && ilen>=7 && v[0]==0x00&&v[1]==0x50&&v[2]==0xf2&&v[3]==0x02){
            rank=10;if(wmm_seen++)return 20;
            /* WMM Information element: subtype 0, version 1, QoS Info 0. */
            if(ilen!=7||v[4]!=0x00||v[5]!=0x01||v[6]!=0x00)return 21;
        }else{
            return 22;
        }
        if(rank<=last_rank)return 23;
        last_rank=rank;pos+=ilen;
    }
    if(pos!=len||!ssid_seen||!supp_seen)return 24;
    if(!power_seen||!extcap_seen||!rrm_seen||!opclass_seen)return 25;
    r.flags|=TITAN_ASSOC_WIRE_F_RATES_VALID;
    if((target_basic_mask & (u16)~r.legacy_mask)!=0)return 26;
    r.flags|=TITAN_ASSOC_WIRE_F_BASIC_SUBSET;
    if(r.basic_marker_mask!=0)return 27;
    r.flags|=TITAN_ASSOC_WIRE_F_BASIC_CLEAR;
    if(expect_rsn){if(!rsn_seen)return 28;r.flags|=TITAN_ASSOC_WIRE_F_RSN_PRESENT;}
    else if(rsn_seen)return 29;
    if((expect_ht?1:0)!=(ht_seen?1:0))return 30;
    r.flags|=TITAN_ASSOC_WIRE_F_HT_MATCH;
    if((expect_wmm?1:0)!=(wmm_seen?1:0))return 31;
    r.flags|=TITAN_ASSOC_WIRE_F_WMM_MATCH;
    *out=r;
    return 1;
}
