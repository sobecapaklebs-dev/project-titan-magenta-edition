#ifndef PROJECT_TITAN_SHA256_H
#define PROJECT_TITAN_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct TitanSha256Context { uint32_t state[8]; uint64_t total; unsigned char block[64]; unsigned int used; } TitanSha256Context;
void TitanSha256_Init(TitanSha256Context *ctx);
void TitanSha256_Update(TitanSha256Context *ctx,const void *data,size_t size);
void TitanSha256_Final(TitanSha256Context *ctx,unsigned char digest[32]);
void TitanSha256_Hex(const unsigned char digest[32],char hex[65]);
int TitanSha256_HexEqual(const unsigned char digest[32],const char *expected_hex);
#endif
