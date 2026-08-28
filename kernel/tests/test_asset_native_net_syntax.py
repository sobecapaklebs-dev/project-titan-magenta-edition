#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='titan_http_syntax_') as td0:
 td=Path(td0)
 (td/'delaythread.h').write_text('int DelayThread(unsigned int);\n')
 (td/'libcdvd.h').write_text('typedef struct { unsigned char stat,second,minute,hour,pad,day,month,year; } sceCdCLOCK; int sceCdReadClock(sceCdCLOCK*);\n')
 (td/'timer.h').write_text('unsigned long long GetTimerSystemTime(void);\n')
 (td/'netdb.h').write_text('struct hostent { char **h_addr_list; }; struct hostent *gethostbyname(const char*);\n')
 (td/'ps2ip.h').write_text('''#define AF_INET 2
#define PF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
typedef unsigned int socklen_t; struct in_addr { unsigned int s_addr; }; struct sockaddr { unsigned short sa_family; char sa_data[14]; }; struct sockaddr_in { unsigned short sin_family; unsigned short sin_port; struct in_addr sin_addr; }; unsigned short htons(unsigned short); int socket(int,int,int); int connect(int,const struct sockaddr*,socklen_t); int send(int,const void*,int,int); int recv(int,void*,int,int); int lwip_close(int);
''')
 (td/'bearssl.h').write_text('''#include <stddef.h>\n#include <stdint.h>\n#define BR_SSL_BUFSIZE_BIDI 33178\n#define BR_ERR_OK 0\ntypedef struct { int dummy; } br_ssl_engine_context; typedef struct { br_ssl_engine_context eng; } br_ssl_client_context; typedef struct { int dummy; } br_x509_minimal_context; typedef struct { int dummy; } br_x509_trust_anchor; typedef struct { int dummy; } br_sslio_context;\nvoid br_ssl_client_init_full(br_ssl_client_context*,br_x509_minimal_context*,const br_x509_trust_anchor*,size_t); void br_x509_minimal_set_time(br_x509_minimal_context*,uint32_t,uint32_t); void br_ssl_engine_inject_entropy(br_ssl_engine_context*,const void*,size_t); void br_ssl_engine_set_buffer(br_ssl_engine_context*,void*,size_t,int); void br_sslio_init(br_sslio_context*,br_ssl_engine_context*,int(*)(void*,unsigned char*,size_t),void*,int(*)(void*,const unsigned char*,size_t),void*); int br_ssl_client_reset(br_ssl_client_context*,const char*,int); int br_sslio_flush(br_sslio_context*); int br_sslio_write_all(br_sslio_context*,const void*,size_t); int br_sslio_read(br_sslio_context*,void*,size_t); int br_ssl_engine_last_error(const br_ssl_engine_context*);\n''')
 subprocess.run(['cc','-std=gnu99','-fsyntax-only','-Wall','-Wextra','-Werror','-Werror=implicit-function-declaration','-D__EE__','-DPROJECT_TITAN_TLS_TEST_STUB',f'-I{td}',f'-I{root/"include"}',f'-I{root/"src/services"}',f'-I{root/"src/core"}',str(root/'src/services/titan_http_client.c')],check=True)
 subprocess.run(['cc','-std=gnu99','-fsyntax-only','-Wall','-Wextra','-Werror',f'-I{root/"include"}',f'-I{root/"src/services"}',str(root/'src/services/titan_asset_image.c')],check=True)
print('Titan native HTTPS + image decoder syntax guard: PASS')
