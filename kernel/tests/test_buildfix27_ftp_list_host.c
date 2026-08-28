#include "titan_ftp_service.h"
#include "titan_network_stream.h"
#include "titan_storage_io.h"
#include "titan_network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *cmds[]={"USER a\r\n","PASS b\r\n","EPSV\r\n","LIST\r\n","QUIT\r\n"};static unsigned ci;static char control[2048];static unsigned cu;static char data[1024];static unsigned du;static int accepted_control,accepted_data;static TitanNetworkStatus ns;
int TitanNetworkStream_Listen(TitanNetworkStreamOwner o,unsigned short p,unsigned int b,TitanNetworkStreamHandle*out){assert(o==TITAN_NETWORK_STREAM_OWNER_FTP);(void)b;if(p==2121u)*out=1u;else{assert(p>=50000u&&p<50032u);*out=3u;}return 1;}
int TitanNetworkStream_Accept(TitanNetworkStreamHandle l,unsigned int d,TitanNetworkStreamHandle*out){assert(d==1u);if(l==1u){if(accepted_control)return 0;accepted_control=1;*out=2u;return 1;}if(l==3u){if(accepted_data)return 0;accepted_data=1;*out=4u;return 1;}return 0;}
int TitanNetworkStream_Read(TitanNetworkStreamHandle s,void*b,unsigned int cap,unsigned int d,unsigned int*out){const char*c;unsigned n;assert(s==2u&&d==1u);if(ci>=sizeof(cmds)/sizeof(cmds[0])){*out=0;return 0;}c=cmds[ci++];n=(unsigned)strlen(c);assert(n<=cap);memcpy(b,c,n);*out=n;return 1;}
int TitanNetworkStream_Write(TitanNetworkStreamHandle s,const void*b,unsigned int n,unsigned int d,unsigned int*out){assert(d==1000u);if(s==2u){assert(cu+n<sizeof(control));memcpy(control+cu,b,n);cu+=n;control[cu]=0;}else{assert(s==4u);assert(du+n<sizeof(data));memcpy(data+du,b,n);du+=n;data[du]=0;}*out=n;return 1;}
void TitanNetworkStream_Close(TitanNetworkStreamHandle h){(void)h;}void TitanNetworkStream_CancelOwner(TitanNetworkStreamOwner o){assert(o==TITAN_NETWORK_STREAM_OWNER_FTP);}
const TitanNetworkStatus *TitanNetwork_GetStatus(void){strcpy(ns.ip,"192.168.1.10");return &ns;}
int TitanStorageIo_PathInfo64(const char*p,int*d,TitanStorageIoSize*z){(void)p;*d=1;*z=0;return 1;}
int TitanStorageIo_ListDirectory(const char*p,unsigned int i,TitanStorageIoEntry*e,unsigned int cap){assert(strcmp(p,"mass:/")==0&&cap==1u);if(i==0){strcpy(e->name,"games");e->size=0;e->flags=TITAN_STORAGE_IO_ENTRY_DIRECTORY;return 1;}if(i==1){strcpy(e->name,"readme.txt");e->size=12;e->flags=0;return 1;}return 0;}
int TitanStorageIo_Open(const char *p,int f){(void)p;(void)f;return -1;}
int TitanStorageIo_Close(int fd){(void)fd;return 1;}
TitanStorageIoResult TitanStorageIo_ReadSome(int fd,void *b,size_t n){(void)fd;(void)b;(void)n;return -1;}
int TitanStorageIo_WriteExact(int fd,const void *b,size_t n){(void)fd;(void)b;(void)n;return 0;}
int TitanStorageIo_AtomicReplace(const char *t,const char *d,const char *b){(void)t;(void)d;(void)b;return 0;}
int TitanStorageIo_RemoveAny(const char *p){(void)p;return 1;}
TitanNetworkStreamIoStatus TitanNetworkStream_ReadStatus(TitanNetworkStreamHandle s,void *b,unsigned int c,unsigned int d,unsigned int *o){(void)s;(void)b;(void)c;(void)d;if(o)*o=0;return TITAN_NETWORK_STREAM_IO_ERROR;}

int main(void){unsigned i;assert(TitanFtpService_Start("mass:/",2121));for(i=0;i<30;i++)TitanFtpService_Tick();assert(strstr(control,"229 Entering Extended Passive Mode")!=0);assert(strstr(control,"150 Opening passive data connection")!=0);assert(strstr(control,"226 Directory transfer complete")!=0);assert(strstr(data,"games")!=0);assert(strstr(data,"readme.txt")!=0);assert(strstr(control,"221 Goodbye")!=0);TitanFtpService_Stop();puts("buildfix27-ftp-list-host: OK passive LIST paged over Titan VFS");return 0;}
