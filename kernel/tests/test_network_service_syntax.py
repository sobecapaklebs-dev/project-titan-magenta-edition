#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
src=root/'src/services/titan_network.c'
with tempfile.TemporaryDirectory(prefix='titan_network_syntax_') as td0:
    td=Path(td0); (td/'lwip').mkdir()
    (td/'kernel.h').write_text('#define UNCACHED_SEG(x) (x)\n')
    (td/'loadfile.h').write_text('int SifExecModuleBuffer(void*,unsigned int,int,const char*,int*); int SifSearchModuleByName(const char*);\n')
    (td/'netman.h').write_text(r'''#ifndef NETMAN_H
#define NETMAN_H
#define NETMAN_NETIF_IOCTL_ETH_GET_MAC 0x1000
#define NETMAN_NETIF_IOCTL_GET_LINK_STATUS 0x3000
#define NETMAN_NETIF_ETH_LINK_STATE_UP 1
int NetManInit(void); int NetManSetMainIF(const char*); int NetManIoctl(unsigned int,void*,unsigned int,void*,unsigned int);
#endif
''')
    (td/'ps2ip.h').write_text(r'''#ifndef PS2IP_H
#define PS2IP_H
struct ip4_addr { unsigned int addr; };
#define IP4_ADDR(ip,a,b,c,d) do{ (ip)->addr=((unsigned int)(a))|((unsigned int)(b)<<8)|((unsigned int)(c)<<16)|((unsigned int)(d)<<24); }while(0)
int ps2ipInit(struct ip4_addr*,struct ip4_addr*,struct ip4_addr*);
#endif
''')
    (td/'ps2ips.h').write_text(r'''#ifndef PS2IPS_H
#define PS2IPS_H
typedef struct { unsigned int s_addr; } stub_in_addr;
typedef struct { char netif_name[8]; stub_in_addr ipaddr,netmask,gw; unsigned char hw_addr[6]; int dhcp_enabled; int dhcp_status; } t_ip_info;
int ps2ip_getconfig(char*,t_ip_info*); int ps2ip_setconfig(const t_ip_info*);
#endif
''')
    (td/'tcpip.h').write_text(r'''#ifndef TCPIP_H
#define TCPIP_H
struct netif { unsigned char hwaddr[6]; unsigned char hwaddr_len; };
struct netif *netif_find(const char*);
#endif
''')
    (td/'sifrpc.h').write_text(r'''#ifndef SIFRPC_H
#define SIFRPC_H
typedef struct { void *server; int dummy; } SifRpcClientData_t;
#define SIF_RPC_M_NOWAIT 1
int SifBindRpc(SifRpcClientData_t*,unsigned int,int); int SifCallRpc(SifRpcClientData_t*,int,int,void*,int,void*,int,void*,void*);
#endif
''')
    subprocess.run(['cc','-std=gnu99','-fsyntax-only','-Wall','-Wextra','-Werror=implicit-function-declaration','-Werror=misleading-indentation','-D__EE__',f'-I{td}',f'-I{root/"src/services"}',f'-I{root/"iop/titan_ar9271"}',str(src)],check=True)
print('Titan Network EE service syntax guard: PASS')
