#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'src/services/titan_asset_service.c'
with tempfile.TemporaryDirectory(prefix='titan_asset_syntax_') as td0:
    td = Path(td0)
    (td/'delaythread.h').write_text('int DelayThread(unsigned int);\n')
    (td/'kernel.h').write_text(r'''
typedef struct { int init_count,max_count,option; } ee_sema_t;
typedef struct { void (*func)(void*); void *stack; unsigned int stack_size; void *gp_reg; int initial_priority; } ee_thread_t;
int CreateSema(ee_sema_t*); int WaitSema(int); int SignalSema(int);
int CreateThread(ee_thread_t*); int StartThread(int,void*); void ExitDeleteThread(void);
''')
    (td/'netdb.h').write_text(r'''
struct hostent { char **h_addr_list; };
struct hostent *gethostbyname(const char*);
''')
    (td/'ps2ip.h').write_text(r'''
#ifndef PS2IP_H
#define PS2IP_H
#define AF_INET 2
#define PF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
typedef unsigned int socklen_t;
struct in_addr { unsigned int s_addr; };
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
struct sockaddr_in { unsigned short sin_family; unsigned short sin_port; struct in_addr sin_addr; };
unsigned short htons(unsigned short);
int socket(int,int,int); int connect(int,const struct sockaddr*,socklen_t);
int send(int,const void*,int,int); int recv(int,void*,int,int); int lwip_close(int);
#endif
''')
    cmd=['cc','-std=gnu99','-fsyntax-only','-Wall','-Wextra','-Werror',
         '-Werror=implicit-function-declaration','-Werror=misleading-indentation','-D__EE__',
         f'-I{td}',f'-I{root/"include"}',f'-I{root/"src/services"}',f'-I{root/"src/core"}',
         f'-I{root/"src/graphics"}',f'-I{root/"src/ui"}',f'-I{root/"src/platform/ps2"}',
         str(src)]
    subprocess.run(cmd, check=True)
print('Titan Asset Service EE syntax guard: PASS')
