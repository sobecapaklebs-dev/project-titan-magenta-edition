#include "titan_applications.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void le16(unsigned char *d,unsigned int v){d[0]=(unsigned char)v;d[1]=(unsigned char)(v>>8);}
static void le32(unsigned char *d,unsigned int v){d[0]=(unsigned char)v;d[1]=(unsigned char)(v>>8);d[2]=(unsigned char)(v>>16);d[3]=(unsigned char)(v>>24);}
static void elf(const char *p){unsigned char b[0x104];FILE*f;memset(b,0,sizeof(b));b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';b[4]=1;b[5]=1;b[6]=1;le16(b+16,2);le16(b+18,8);le32(b+20,1);le32(b+24,0x00100000);le32(b+28,52);le16(b+40,52);le16(b+42,32);le16(b+44,1);le32(b+52,1);le32(b+56,0x100);le32(b+60,0x00100000);le32(b+64,0x00100000);le32(b+68,4);le32(b+72,4);le32(b+76,5);le32(b+80,0x100);f=fopen(p,"wb");assert(f);assert(fwrite(b,1,sizeof(b),f)==sizeof(b));fclose(f);}
static void text(const char*p,const char*s){FILE*f=fopen(p,"wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));fclose(f);}
static const TitanApplication *find_title(const char *title){unsigned int i;for(i=0;i<TitanApplications_GetCount();i++){const TitanApplication*a=TitanApplications_Get(i);if(a&&strcmp(a->title,title)==0)return a;}return 0;}

int main(void){const TitanApplication*a;
 assert(system("rm -rf /tmp/bf24_sas_layouts") == 0);assert(mkdir("/tmp/bf24_sas_layouts",0700)==0);assert(chdir("/tmp/bf24_sas_layouts")==0);assert(mkdir("mc0:",0700)==0);
 assert(mkdir("mc0:/APP_CLASSIC",0700)==0);elf("mc0:/APP_CLASSIC/CLASSIC.ELF");text("mc0:/APP_CLASSIC/title.cfg","title=Classic SAS\nboot=CLASSIC.ELF\nauthor=TnA Test\ncredits=Community\nsource=https://example.invalid/source\n");
 assert(mkdir("mc0:/APP_EXT",0700)==0);assert(mkdir("mc0:/APP_EXT/assets",0700)==0);elf("mc0:/APP_EXT/EXT.ELF");text("mc0:/APP_EXT/title.cfg","title=Extended SAS\nboot=EXT.ELF\n");
 assert(mkdir("mc0:/XYZ_TOOL",0700)==0);elf("mc0:/XYZ_TOOL/TOOL.ELF");text("mc0:/XYZ_TOOL/title.cfg","title=Unknown Prefix\nboot=TOOL.ELF\n");
 assert(mkdir("mc0:/APP_FALLBACK",0700)==0);elf("mc0:/APP_FALLBACK/FALLBACK.ELF");text("mc0:/APP_FALLBACK/app.cfg","title=Titan Fallback\nexecutable=FALLBACK.ELF\n");
 assert(mkdir("mc0:/APP_PRECEDENCE",0700)==0);elf("mc0:/APP_PRECEDENCE/PRE.ELF");text("mc0:/APP_PRECEDENCE/app.cfg","title=Wrong AppCfg\nexecutable=PRE.ELF\n");text("mc0:/APP_PRECEDENCE/title.cfg","title=Canonical TitleCfg\nboot=PRE.ELF\n");
 assert(mkdir("mc0:/PLAIN",0700)==0);elf("mc0:/PLAIN/PLAIN.ELF");text("mc0:/PLAIN/INSTALL.PBT","NOT A SAS REQUIREMENT\n");
 TitanApplications_Initialize(); assert(TitanApplications_RefreshFromRoot("mc0:")==6u);
 a=find_title("Classic SAS");assert(a);assert(a->sas_compatible==1u&&a->sas_compliant==1u);assert(a->sas_profile==TITAN_SAS_PROFILE_CLASSIC);assert(a->sas_osdsys_manageable==1u);assert(a->sas_copyability==TITAN_SAS_OSDSYS_COPY_SAFE);assert(a->sas_prefix_known==1u);assert(strcmp(a->credits,"Community")==0);assert(strstr(a->source,"example.invalid")!=0);
 a=find_title("Extended SAS");assert(a);assert(a->sas_compatible==1u&&a->sas_compliant==0u);assert(a->sas_has_subfolders==1u);assert(a->sas_profile==TITAN_SAS_PROFILE_EXTENDED_COMPAT);assert(a->sas_copyability==TITAN_SAS_OSDSYS_COPY_UNSAFE);assert(a->sas_osdsys_manageable==0u);
 a=find_title("Unknown Prefix");assert(a);assert(a->sas_compatible==1u&&a->sas_compliant==0u);assert(a->sas_prefix_known==0u);
 a=find_title("Titan Fallback");assert(a);assert(a->sas_compatible==1u&&a->sas_compliant==0u);assert(a->sas_profile==TITAN_SAS_PROFILE_TITAN_FALLBACK);assert(a->metadata_source==TITAN_APPLICATION_METADATA_APP_CFG);
 a=find_title("Canonical TitleCfg");assert(a);assert(a->metadata_source==TITAN_APPLICATION_METADATA_TITLE_CFG);assert(a->sas_compliant==1u);
 a=find_title("PLAIN");assert(a);assert(a->sas_compatible==0u&&a->sas_compliant==0u);assert(a->classification==TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY);
 puts("titan-sas-layout-contracts: OK classic/extended/prefix/copyability/titlecfg/appcfg/PBT");return 0;}
