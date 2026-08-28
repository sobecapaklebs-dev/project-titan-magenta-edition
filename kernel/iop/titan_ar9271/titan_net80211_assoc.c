/*-
 * Portions of the association-frame construction in this file are adapted
 * from OpenBSD sys/net80211/ieee80211_output.c.
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Damien Bergamini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <types.h>
#include <sysclib.h>
#include "titan_net80211_assoc.h"

#define IEEE80211_RATE_SIZE 8
#define ELEMID_SSID         0
#define ELEMID_RATES        1
#define ELEMID_QOS_CAP      46
#define ELEMID_XRATES       50
#define ELEMID_HTCAPS       45
#define ELEMID_VENDOR       221

static void le16enc(u8 *p,u16 v){p[0]=(u8)v;p[1]=(u8)(v>>8);}

static int add_ie(u8 *out,int p,int cap,u8 id,const u8 *body,int len)
{
    if(!out||len<0||len>255||p<0||p+2+len>cap)return -1;
    out[p++]=id;out[p++]=(u8)len;
    if(len){memcpy(out+p,body,(unsigned int)len);p+=len;}
    return p;
}

static int add_ssid(u8 *out,int p,int cap,const u8 *ssid,u8 len)
{
    return add_ie(out,p,cap,ELEMID_SSID,ssid,len);
}

/* net80211's ieee80211_add_rates()/ieee80211_add_xrates() copy the negotiated
 * node-rate bytes themselves.  The high (Basic) marker is therefore retained
 * in the on-air Supported Rates IE instead of being reconstructed later. */
static int add_rates(u8 *out,int p,int cap,const u8 *rates,u8 nrates)
{
    int n=nrates>IEEE80211_RATE_SIZE?IEEE80211_RATE_SIZE:nrates;
    return add_ie(out,p,cap,ELEMID_RATES,rates,n);
}

static int add_xrates(u8 *out,int p,int cap,const u8 *rates,u8 nrates)
{
    if(nrates<=IEEE80211_RATE_SIZE)return p;
    return add_ie(out,p,cap,ELEMID_XRATES,rates+IEEE80211_RATE_SIZE,
                  nrates-IEEE80211_RATE_SIZE);
}

static int add_qos_capability(u8 *out,int p,int cap)
{
    static const u8 qosinfo=0; /* U-APSD disabled. */
    return add_ie(out,p,cap,ELEMID_QOS_CAP,&qosinfo,1);
}

static int add_htcaps(u8 *out,int p,int cap,const u8 *ht)
{
    return add_ie(out,p,cap,ELEMID_HTCAPS,ht,26);
}

static int add_wme_info(u8 *out,int p,int cap)
{
    /* OUI 00:50:f2, WME type 2, information subtype 0, version 1,
     * station QoS info 0 (no U-APSD). This is the net80211 STA WME-info shape. */
    static const u8 wme[7]={0x00,0x50,0xf2,0x02,0x00,0x01,0x00};
    return add_ie(out,p,cap,ELEMID_VENDOR,wme,sizeof(wme));
}

int titan_net80211_build_assoc_body(u16 capability,u16 listen_interval,
                                    const u8 *ssid,u8 ssid_len,
                                    const u8 *rates,u8 rate_count,
                                    const u8 *rsn_ie,int rsn_ie_len,
                                    u8 add_qos,const u8 *ht_cap,u8 add_ht,
                                    u8 add_wmm,
                                    u8 *out,int out_cap)
{
    int p=0;
    if(!ssid||!ssid_len||!rates||!rate_count||!out||out_cap<4)return -1;
    if(!rsn_ie||rsn_ie_len<2||rsn_ie_len>66||rsn_ie[0]!=48 ||
       (int)rsn_ie[1]+2!=rsn_ie_len)return -2;
    if(add_ht&&!ht_cap)return -3;

    le16enc(out+p,capability);p+=2;
    le16enc(out+p,listen_interval);p+=2;

    /* OpenBSD net80211 ieee80211_get_assoc_req() order for the subset Titan
     * needs on AR9271/2.4 GHz WPA2:
     * SSID -> Rates -> XRates -> RSN -> QoS Capability -> HT Caps -> WME Info. */
    p=add_ssid(out,p,out_cap,ssid,ssid_len);if(p<0)return -4;
    p=add_rates(out,p,out_cap,rates,rate_count);if(p<0)return -4;
    p=add_xrates(out,p,out_cap,rates,rate_count);if(p<0)return -4;
    if(p+rsn_ie_len>out_cap)return -4;
    memcpy(out+p,rsn_ie,(unsigned int)rsn_ie_len);p+=rsn_ie_len;
    if(add_qos){p=add_qos_capability(out,p,out_cap);if(p<0)return -4;}
    if(add_ht){p=add_htcaps(out,p,out_cap,ht_cap);if(p<0)return -4;}
    if(add_wmm){p=add_wme_info(out,p,out_cap);if(p<0)return -4;}
    return p;
}
