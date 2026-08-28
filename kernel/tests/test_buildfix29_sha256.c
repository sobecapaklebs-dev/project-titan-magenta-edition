#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "titan_sha256.h"
int main(void){TitanSha256Context c;unsigned char d[32];char h[65];TitanSha256_Init(&c);TitanSha256_Update(&c,"abc",3);TitanSha256_Final(&c,d);TitanSha256_Hex(d,h);assert(!strcmp(h,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));assert(TitanSha256_HexEqual(d,"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));puts("titan-sha256-host: OK");return 0;}
