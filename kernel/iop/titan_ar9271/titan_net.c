/* Tiny IPv4 proof stack for the standalone AR9271 tester.
 * Only DHCPv4, ARP and ICMP Echo are implemented. This deliberately avoids
 * coupling the hardware bring-up proof to ps2ip/lwIP; the eventual Titan
 * integration can expose the same Wi-Fi data path as a normal netif.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>
#include <sysclib.h>
#include "titan_net.h"

static u16 rb16(const u8*p){return ((u16)p[0]<<8)|p[1];}
static u32 rb32(const u8*p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static void wb16(u8*p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void wb32(u8*p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static int eq4(const u8*a,const u8*b){return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3];}
static u16 csum(const u8 *p,int len){u32 s=0;while(len>1){s+=((u16)p[0]<<8)|p[1];p+=2;len-=2;}if(len)s+=(u16)p[0]<<8;while(s>>16)s=(s&0xffff)+(s>>16);return (u16)~s;}

void titan_net_init(TitanNet*n,const u8 mac[6],u32 seed){memset(n,0,sizeof(*n));memcpy(n->mac,mac,6);n->xid=seed?seed:0x5449544eu;n->ip_id=(u16)(seed^(seed>>16));n->ping_id=(u16)(0x9271u^(seed>>16));n->ping_seq=1;}

static int ipv4(TitanNet*n,u8*out,int cap,const u8 src[4],const u8 dst[4],u8 proto,const u8*body,int blen)
{
    int total=20+blen;if(cap<total)return -1;memset(out,0,20);out[0]=0x45;wb16(out+2,(u16)total);wb16(out+4,++n->ip_id);wb16(out+6,0);out[8]=64;out[9]=proto;memcpy(out+12,src,4);memcpy(out+16,dst,4);wb16(out+10,csum(out,20));memcpy(out+20,body,blen);return total;
}

static int dhcp(TitanNet*n,int type,u8*out,int cap)
{
    u8 bootp[548],udp[556],zero[4]={0,0,0,0},bcast[4]={255,255,255,255};int p=240,ulen;
    memset(bootp,0,sizeof(bootp));bootp[0]=1;bootp[1]=1;bootp[2]=6;wb32(bootp+4,n->xid);wb16(bootp+10,0x8000);memcpy(bootp+28,n->mac,6);
    bootp[236]=99;bootp[237]=130;bootp[238]=83;bootp[239]=99;
    bootp[p++]=53;bootp[p++]=1;bootp[p++]=(u8)type;
    bootp[p++]=61;bootp[p++]=7;bootp[p++]=1;memcpy(bootp+p,n->mac,6);p+=6;
    if(type==3){bootp[p++]=50;bootp[p++]=4;memcpy(bootp+p,n->offered_ip,4);p+=4;bootp[p++]=54;bootp[p++]=4;memcpy(bootp+p,n->server_id,4);p+=4;}
    bootp[p++]=55;bootp[p++]=5;bootp[p++]=1;bootp[p++]=3;bootp[p++]=6;bootp[p++]=51;bootp[p++]=54;bootp[p++]=255;
    /* Widely deployed DHCP servers expect the historical minimum BOOTP/DHCP
       message size. The buffer is already zeroed, so pad to 300 bytes. */
    if(p<300)p=300;
    memset(udp,0,8);wb16(udp,68);wb16(udp+2,67);wb16(udp+4,(u16)(8+p));memcpy(udp+8,bootp,p);ulen=8+p;
    return ipv4(n,out,cap,zero,bcast,17,udp,ulen);
}
int titan_net_dhcp_discover(TitanNet*n,u8*out,int cap){return dhcp(n,1,out,cap);}
int titan_net_dhcp_request(TitanNet*n,u8*out,int cap){return dhcp(n,3,out,cap);}

int titan_net_arp_gateway(TitanNet*n,u8*out,int cap)
{
    if(cap<28||!n->configured)return -1;
    memset(out,0,28);wb16(out,1);wb16(out+2,0x0800);out[4]=6;out[5]=4;wb16(out+6,1);
    memcpy(out+8,n->mac,6);memcpy(out+14,n->ip,4);memcpy(out+24,n->gateway,4);
    return 28;
}
int titan_net_gratuitous_arp(TitanNet*n,u8*out,int cap)
{
    if(cap<28||!n->configured)return -1;
    memset(out,0,28);wb16(out,1);wb16(out+2,0x0800);out[4]=6;out[5]=4;wb16(out+6,1);
    memcpy(out+8,n->mac,6);memcpy(out+14,n->ip,4);memcpy(out+24,n->ip,4);
    return 28;
}
int titan_net_arp_reply(TitanNet*n,u8*out,int cap,u8 dst_mac[6])
{
    if(cap<28||!n->configured||!n->arp_reply_pending)return -1;
    memset(out,0,28);wb16(out,1);wb16(out+2,0x0800);out[4]=6;out[5]=4;wb16(out+6,2);
    memcpy(out+8,n->mac,6);memcpy(out+14,n->ip,4);
    memcpy(out+18,n->arp_peer_mac,6);memcpy(out+24,n->arp_peer_ip,4);
    if(dst_mac)memcpy(dst_mac,n->arp_peer_mac,6);
    n->arp_reply_pending=0;return 28;
}
int titan_net_ping_google_dns(TitanNet*n,u8*out,int cap)
{
    u8 icmp[64],dst[4]={8,8,8,8};const char msg[]="Project Titan AR9271";int m=(int)sizeof(msg)-1,ilen=8+m;
    if(!n->configured||cap<20+ilen)return -1;
    memset(icmp,0,ilen);icmp[0]=8;icmp[1]=0;wb16(icmp+4,n->ping_id);wb16(icmp+6,n->ping_seq++);
    memcpy(icmp+8,msg,m);wb16(icmp+2,csum(icmp,ilen));
    return ipv4(n,out,cap,n->ip,dst,1,icmp,ilen);
}

static void dhcp_opts(TitanNet*n,const u8*p,int len,int *msg)
{
    int i=0;*msg=0;while(i<len){int id=p[i++],l;if(id==255)break;if(id==0)continue;if(i>=len)break;l=p[i++];if(i+l>len)break;
        if(id==53&&l>=1)*msg=p[i];else if(id==54&&l>=4)memcpy(n->server_id,p+i,4);else if(id==1&&l>=4)memcpy(n->mask,p+i,4);else if(id==3&&l>=4)memcpy(n->gateway,p+i,4);else if(id==6&&l>=4)memcpy(n->dns,p+i,4);i+=l;}
}
static int rx_dhcp(TitanNet*n,const u8*p,int len)
{
    int ihl,udplen,blen,msg;const u8 *u,*b;if(len<20||(p[0]>>4)!=4||p[9]!=17)return TITAN_NET_NONE;ihl=(p[0]&15)*4;if(ihl<20||len<ihl+8)return TITAN_NET_NONE;u=p+ihl;if(rb16(u)!=67||rb16(u+2)!=68)return TITAN_NET_NONE;udplen=rb16(u+4);if(udplen<8+240||ihl+udplen>len)return TITAN_NET_NONE;b=u+8;blen=udplen-8;if(b[0]!=2||rb32(b+4)!=n->xid||b[236]!=99||b[237]!=130||b[238]!=83||b[239]!=99)return TITAN_NET_NONE;dhcp_opts(n,b+240,blen-240,&msg);
    if(msg==2){memcpy(n->offered_ip,b+16,4);return TITAN_NET_DHCP_OFFER;}
    if(msg==5){memcpy(n->ip,b+16,4);n->configured=1;return TITAN_NET_DHCP_ACK;}return TITAN_NET_NONE;
}
int titan_net_rx(TitanNet*n,u16 et,const u8*src,const u8*p,int len)
{
    static const u8 google[4]={8,8,8,8};
    if(et==0x0806){
        if(len<28||rb16(p)!=1||rb16(p+2)!=0x0800||p[4]!=6||p[5]!=4)return TITAN_NET_NONE;
        if(rb16(p+6)==2&&eq4(p+14,n->gateway)&&eq4(p+24,n->ip)){
            memcpy(n->gateway_mac,p+8,6);n->have_gateway_mac=1;return TITAN_NET_ARP_GATEWAY;
        }
        if(rb16(p+6)==1&&n->configured&&eq4(p+24,n->ip)){
            memcpy(n->arp_peer_mac,p+8,6);memcpy(n->arp_peer_ip,p+14,4);n->arp_reply_pending=1;
            return TITAN_NET_ARP_REQUEST;
        }
        return TITAN_NET_NONE;
    }
    if(et==0x0800){int e=rx_dhcp(n,p,len);if(e)return e;if(len>=28&&(p[0]>>4)==4&&p[9]==1&&eq4(p+12,google)&&eq4(p+16,n->ip)){int h=(p[0]&15)*4;if(len>=h+8&&p[h]==0&&rb16(p+h+4)==n->ping_id)return TITAN_NET_PING_REPLY;} }
    (void)src;return TITAN_NET_NONE;
}
