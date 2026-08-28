#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "titan_storage_pfs_iop.h"

typedef struct Fake { int resident[4]; int calls[4]; int ready; } Fake;
static const char *names[4] = {"dev9","atad","hdd_driver","pfs_driver"};
static int idx(const char *n){int i;for(i=0;i<4;i++)if(strcmp(n,names[i])==0)return i;return -1;}
static int resident(void *c,const char*n){Fake*f=c;int i=idx(n);return i>=0?f->resident[i]:0;}
static int execm(void *c,const unsigned char*d,uint32_t sz){Fake*f=c;int i=(int)d[0]-1;if(sz==0||i<0||i>3)return 0;f->calls[i]++;f->resident[i]=1;if(i==3)f->ready=1;return 1;}
static int ready(void*c){return ((Fake*)c)->ready;}
int main(void){
 unsigned char d0[]={1},d1[]={2},d2[]={3},d3[]={4};
 TitanStoragePfsIopImage im[4]={{"dev9",d0,1},{"atad",d1,1},{"hdd_driver",d2,1},{"pfs_driver",d3,1}};
 TitanStoragePfsIopBackend b={resident,execm,ready}; Fake f={{1,0,0,0},{0,0,0,0},0};
 assert(TitanStoragePfsIop_Ensure(&b,&f,im));
 assert(f.calls[0]==0 && f.calls[1]==1 && f.calls[2]==1 && f.calls[3]==1);
 assert(TitanStoragePfsIop_Ensure(&b,&f,im));
 assert(f.calls[1]==1 && f.calls[2]==1 && f.calls[3]==1);
 puts("buildfix27-pfs-iop-loader-host: OK dependency order + resident idempotence"); return 0;
}
