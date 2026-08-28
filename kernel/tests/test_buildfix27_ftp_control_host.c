#include "titan_ftp_service.h"
#include "titan_network_stream.h"
#include "titan_storage_io.h"
#include "titan_network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *g_commands[] = {"USER anonymous\r\n","PASS x\r\n","PWD\r\n","CWD games\r\n","PWD\r\n","LIST\r\n","QUIT\r\n"};
static unsigned int g_command_index;
static char g_output[1024];
static unsigned int g_output_used;
static int g_connected;

int TitanNetworkStream_Listen(TitanNetworkStreamOwner owner,unsigned short port,unsigned int backlog,TitanNetworkStreamHandle *out){assert(owner==TITAN_NETWORK_STREAM_OWNER_FTP);assert(port==2121u);assert(backlog==2u);*out=1u;return 1;}
int TitanNetworkStream_Accept(TitanNetworkStreamHandle listener,unsigned int deadline,TitanNetworkStreamHandle *out){assert(listener==1u);assert(deadline==1u);if(g_connected)return 0;g_connected=1;*out=2u;return 1;}
int TitanNetworkStream_Read(TitanNetworkStreamHandle stream,void *buffer,unsigned int cap,unsigned int deadline,unsigned int *out){const char *cmd;unsigned int n;assert(stream==2u);assert(deadline==1u);if(g_command_index>=sizeof(g_commands)/sizeof(g_commands[0])){*out=0u;return 0;}cmd=g_commands[g_command_index++];n=(unsigned int)strlen(cmd);assert(n<=cap);memcpy(buffer,cmd,n);*out=n;return 1;}
int TitanNetworkStream_Write(TitanNetworkStreamHandle stream,const void *buffer,unsigned int size,unsigned int deadline,unsigned int *out){assert(stream==2u);assert(deadline==1000u);assert(g_output_used+size<sizeof(g_output));memcpy(g_output+g_output_used,buffer,size);g_output_used+=size;g_output[g_output_used]='\0';*out=size;return 1;}
void TitanNetworkStream_Close(TitanNetworkStreamHandle handle){(void)handle;}
void TitanNetworkStream_CancelOwner(TitanNetworkStreamOwner owner){assert(owner==TITAN_NETWORK_STREAM_OWNER_FTP);}
int TitanStorageIo_PathInfo64(const char *path,int *is_directory,TitanStorageIoSize *size){assert(strcmp(path,"mass:/games")==0);*is_directory=1;*size=0;return 1;}

int TitanStorageIo_Open(const char *p,int f){(void)p;(void)f;return -1;}
int TitanStorageIo_Close(int fd){(void)fd;return 1;}
TitanStorageIoResult TitanStorageIo_ReadSome(int fd,void *b,size_t n){(void)fd;(void)b;(void)n;return -1;}
int TitanStorageIo_WriteExact(int fd,const void *b,size_t n){(void)fd;(void)b;(void)n;return 0;}
int TitanStorageIo_AtomicReplace(const char *t,const char *d,const char *b){(void)t;(void)d;(void)b;return 0;}
int TitanStorageIo_RemoveAny(const char *p){(void)p;return 1;}
TitanNetworkStreamIoStatus TitanNetworkStream_ReadStatus(TitanNetworkStreamHandle s,void *b,unsigned int c,unsigned int d,unsigned int *o){(void)s;(void)b;(void)c;(void)d;if(o)*o=0;return TITAN_NETWORK_STREAM_IO_ERROR;}

static TitanNetworkStatus g_net_status;
const TitanNetworkStatus *TitanNetwork_GetStatus(void){strcpy(g_net_status.ip,"192.168.0.2");return &g_net_status;}
int TitanStorageIo_ListDirectory(const char *p,unsigned int i,TitanStorageIoEntry *e,unsigned int c){(void)p;(void)i;(void)e;(void)c;return 0;}

int main(void){TitanFtpServiceSnapshot snap;unsigned int i;assert(TitanFtpService_Start("mass:/",2121u));for(i=0;i<20u;i++)TitanFtpService_Tick();TitanFtpService_GetSnapshot(&snap);assert(snap.sessions_accepted==1u);assert(snap.commands_processed==7u);assert(snap.commands_rejected==1u);assert(strstr(g_output,"220 Project Titan FTP service ready")!=0);assert(strstr(g_output,"230 Project Titan FTP login successful")!=0);assert(strstr(g_output,"257 \"/\"")!=0);assert(strstr(g_output,"250 Directory changed")!=0);assert(strstr(g_output,"257 \"/games\"")!=0);assert(strstr(g_output,"425 Use PASV or EPSV first")!=0);assert(strstr(g_output,"221 Goodbye")!=0);TitanFtpService_Stop();puts("buildfix27-ftp-control-host: OK cooperative authenticated control plane");return 0;}
