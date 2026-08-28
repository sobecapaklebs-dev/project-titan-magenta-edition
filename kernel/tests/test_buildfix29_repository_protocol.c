#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "titan_repository_protocol.h"
int main(int argc,char**argv){FILE*f;long n;char buf[4096];TitanRepositoryCatalog c;const TitanRepositoryPackage*p;assert(argc==2);f=fopen(argv[1],"rb");assert(f);n=fread(buf,1,sizeof(buf),f);fclose(f);assert(n>0);assert(TitanRepository_Parse(buf,(size_t)n,&c)==TITAN_REPOSITORY_PARSE_OK);assert(c.package_count==2);p=TitanRepository_FindPackage(&c,"TITNAPP01");assert(p);assert(p->package_size==123456u);assert(!strcmp(p->platform,"ps2"));assert(!strcmp(p->architecture,"mips-r5900"));puts("titan-repository-protocol-host: OK");return 0;}
