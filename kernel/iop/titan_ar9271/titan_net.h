#ifndef TITAN_WIFI_NET_H
#define TITAN_WIFI_NET_H
#include <types.h>

enum TitanNetEvent {
    TITAN_NET_NONE=0,
    TITAN_NET_DHCP_OFFER=1,
    TITAN_NET_DHCP_ACK=2,
    TITAN_NET_ARP_GATEWAY=3,
    TITAN_NET_PING_REPLY=4,
    TITAN_NET_ARP_REQUEST=5
};

typedef struct TitanNet {
    u8 mac[6];
    u32 xid;
    u8 offered_ip[4];
    u8 server_id[4];
    u8 ip[4],mask[4],gateway[4],dns[4],gateway_mac[6];
    u8 arp_peer_mac[6],arp_peer_ip[4];
    u16 ip_id;
    u16 ping_id;
    u16 ping_seq;
    u8 configured;
    u8 have_gateway_mac;
    u8 arp_reply_pending;
} TitanNet;

void titan_net_init(TitanNet *n,const u8 mac[6],u32 seed);
int titan_net_dhcp_discover(TitanNet *n,u8 *out,int cap);
int titan_net_dhcp_request(TitanNet *n,u8 *out,int cap);
int titan_net_arp_gateway(TitanNet *n,u8 *out,int cap);
int titan_net_gratuitous_arp(TitanNet *n,u8 *out,int cap);
int titan_net_arp_reply(TitanNet *n,u8 *out,int cap,u8 dst_mac[6]);
int titan_net_ping_google_dns(TitanNet *n,u8 *out,int cap);
int titan_net_rx(TitanNet *n,u16 ethertype,const u8 *src_mac,const u8 *payload,int len);

#endif
