#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/services/titan_datetime.h"
int main(void){
 int y,m,d,h,mi,s; uint64_t e,out; unsigned char p[48]={0};
 assert(TitanDateTime_ValidateCivil(2024,2,29,23,59,59));
 assert(!TitanDateTime_ValidateCivil(2023,2,29,0,0,0));
 e=TitanDateTime_CivilToUnix(2026,8,20,12,34,56); assert(e>0);
 assert(TitanDateTime_UnixToCivil(e,&y,&m,&d,&h,&mi,&s));
 assert(y==2026&&m==8&&d==20&&h==12&&mi==34&&s==56);
 p[0]=0x24; p[1]=2; p[28]=0x12;p[29]=0x34;p[30]=0x56;p[31]=0x78;
 {uint64_t ntp=e+2208988800ull;p[40]=(unsigned char)(ntp>>24);p[41]=(unsigned char)(ntp>>16);p[42]=(unsigned char)(ntp>>8);p[43]=(unsigned char)ntp;}
 assert(TitanDateTime_ParseSntpResponse(p,48,0x12345678u,&out));assert(out==e);
 p[1]=0;assert(!TitanDateTime_ParseSntpResponse(p,48,0x12345678u,&out));
 p[1]=2;p[0]=0xE4;assert(!TitanDateTime_ParseSntpResponse(p,48,0x12345678u,&out));
 puts("buildfix30-datetime-host: OK");return 0;
}
