/*
 * Project Titan AR9271 tester - compact cryptographic primitives used by
 * WPA2-PSK only. Implemented from the SHA-1, HMAC, WPA PRF and AES/RFC3394
 * specifications so the standalone tester does not depend on libc/OpenSSL.
 *
 * This is laboratory code for the standalone PS2 tester. It is intentionally
 * limited to the algorithms needed by WPA2-Personal/CCMP.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>
#include <sysclib.h>
#include "titan_crypto.h"

static u32 rol32(u32 v, unsigned int n) { return (v << n) | (v >> (32 - n)); }
static u32 be32_load(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void be32_store(u8 *p, u32 v) {
    p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v;
}

static void sha1_transform(TitanSha1Ctx *ctx, const u8 block[64])
{
    u32 w[80], a,b,c,d,e,f,k,t;
    int i;
    for (i=0;i<16;i++) w[i]=be32_load(block+i*4);
    for (i=16;i<80;i++) w[i]=rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=ctx->h[0]; b=ctx->h[1]; c=ctx->h[2]; d=ctx->h[3]; e=ctx->h[4];
    for (i=0;i<80;i++) {
        if (i<20) { f=(b&c)|((~b)&d); k=0x5a827999u; }
        else if (i<40) { f=b^c^d; k=0x6ed9eba1u; }
        else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcu; }
        else { f=b^c^d; k=0xca62c1d6u; }
        t=rol32(a,5)+f+e+k+w[i]; e=d; d=c; c=rol32(b,30); b=a; a=t;
    }
    ctx->h[0]+=a; ctx->h[1]+=b; ctx->h[2]+=c; ctx->h[3]+=d; ctx->h[4]+=e;
}

void titan_sha1_init(TitanSha1Ctx *ctx)
{
    memset(ctx,0,sizeof(*ctx));
    ctx->h[0]=0x67452301u; ctx->h[1]=0xefcdab89u; ctx->h[2]=0x98badcfeu;
    ctx->h[3]=0x10325476u; ctx->h[4]=0xc3d2e1f0u;
}

void titan_sha1_update(TitanSha1Ctx *ctx, const void *vdata, u32 len)
{
    const u8 *data=(const u8*)vdata;
    ctx->total_bits += (u64)len * 8u;
    while (len) {
        u32 n=64u-ctx->block_used;
        if (n>len) n=len;
        memcpy(ctx->block+ctx->block_used,data,n);
        ctx->block_used+=n; data+=n; len-=n;
        if (ctx->block_used==64) {
            sha1_transform(ctx,ctx->block);
            ctx->block_used=0;
        }
    }
}

void titan_sha1_final(TitanSha1Ctx *ctx, u8 out[20])
{
    u64 bits=ctx->total_bits;
    int i;
    ctx->block[ctx->block_used++]=0x80;
    if (ctx->block_used>56) {
        while (ctx->block_used<64) ctx->block[ctx->block_used++]=0;
        sha1_transform(ctx,ctx->block); ctx->block_used=0;
    }
    while (ctx->block_used<56) ctx->block[ctx->block_used++]=0;
    for (i=0;i<8;i++) ctx->block[63-i]=(u8)(bits>>(i*8));
    sha1_transform(ctx,ctx->block);
    for (i=0;i<5;i++) be32_store(out+i*4,ctx->h[i]);
    memset(ctx,0,sizeof(*ctx));
}

void titan_hmac_sha1(const u8 *key, u32 key_len,
                     const u8 *data, u32 data_len, u8 out[20])
{
    u8 k0[64], ipad[64], opad[64], inner[20];
    TitanSha1Ctx c;
    u32 i;
    memset(k0,0,sizeof(k0));
    if (key_len>64) {
        titan_sha1_init(&c); titan_sha1_update(&c,key,key_len); titan_sha1_final(&c,k0);
    } else if (key_len) memcpy(k0,key,key_len);
    for (i=0;i<64;i++) { ipad[i]=(u8)(k0[i]^0x36); opad[i]=(u8)(k0[i]^0x5c); }
    titan_sha1_init(&c); titan_sha1_update(&c,ipad,64); titan_sha1_update(&c,data,data_len); titan_sha1_final(&c,inner);
    titan_sha1_init(&c); titan_sha1_update(&c,opad,64); titan_sha1_update(&c,inner,20); titan_sha1_final(&c,out);
    memset(k0,0,sizeof(k0)); memset(ipad,0,sizeof(ipad)); memset(opad,0,sizeof(opad)); memset(inner,0,sizeof(inner));
}

void titan_wpa_prf512(const u8 pmk[32], const u8 *label, u32 label_len,
                      const u8 *data, u32 data_len, u8 out[64])
{
    u8 buf[160], digest[20];
    u32 pos=0, take, i=0;
    if (label_len + 1 + data_len + 1 > sizeof(buf)) { memset(out,0,64); return; }
    memcpy(buf,label,label_len); buf[label_len]=0;
    memcpy(buf+label_len+1,data,data_len);
    while (pos<64) {
        buf[label_len+1+data_len]=(u8)i++;
        titan_hmac_sha1(pmk,32,buf,label_len+1+data_len+1,digest);
        take=64-pos; if (take>20) take=20;
        memcpy(out+pos,digest,take); pos+=take;
    }
    memset(buf,0,sizeof(buf)); memset(digest,0,sizeof(digest));
}

int titan_consttime_eq(const u8 *a, const u8 *b, u32 len)
{
    u8 d=0; u32 i; for(i=0;i<len;i++) d|=(u8)(a[i]^b[i]); return d==0;
}

/* ---------------- TKIP Michael MIC -------------------------------------
 * This follows the mac80211 Michael construction: initialize L/R from the
 * 8-byte Michael key, process DA/SA/priority as the 16-byte pseudo-header,
 * process the MSDU in little-endian 32-bit words, then append 0x5a and zero
 * padding followed by one all-zero word. */
static u32 le16_load(const u8 *p){ return (u32)p[0] | ((u32)p[1]<<8); }
static u32 le32_load(const u8 *p){ return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static void le32_store(u8 *p,u32 v){p[0]=(u8)v;p[1]=(u8)(v>>8);p[2]=(u8)(v>>16);p[3]=(u8)(v>>24);}
static u32 ror32(u32 v,unsigned int n){return (v>>n)|(v<<(32-n));}
static void michael_block(u32 *l,u32 *r,u32 val)
{
    *l ^= val;
    *r ^= rol32(*l,17); *l += *r;
    *r ^= ((*l & 0xff00ff00u)>>8) | ((*l & 0x00ff00ffu)<<8); *l += *r;
    *r ^= rol32(*l,3); *l += *r;
    *r ^= ror32(*l,2); *l += *r;
}

void titan_michael_mic(const u8 key[8], const u8 da[6], const u8 sa[6],
                       u8 priority, const u8 *data, u32 data_len, u8 mic[8])
{
    u32 l,r,blocks,left,i,val;
    if(!mic)return;
    if(!key||!da||!sa||(!data&&data_len)){memset(mic,0,8);return;}
    l=le32_load(key);r=le32_load(key+4);
    michael_block(&l,&r,le32_load(da));
    michael_block(&l,&r,le16_load(da+4)|(le16_load(sa)<<16));
    michael_block(&l,&r,le32_load(sa+2));
    michael_block(&l,&r,(u32)(priority&0x0fu));
    blocks=data_len/4u;left=data_len%4u;
    for(i=0;i<blocks;i++)michael_block(&l,&r,le32_load(data+i*4u));
    val=0x5au;
    while(left>0){val<<=8;left--;val|=data[blocks*4u+left];}
    michael_block(&l,&r,val);michael_block(&l,&r,0);
    le32_store(mic,l);le32_store(mic+4,r);
}

/* ---------------- AES-128 decrypt, only what RFC3394 unwrap needs ------- */
static const u8 sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static u8 invs[256]; static int inv_ready=0;
static u8 xt(u8 x){ return (u8)((x<<1)^((x&0x80)?0x1b:0)); }
static u8 gm(u8 a,u8 b){u8 r=0;while(b){if(b&1)r^=a;a=xt(a);b>>=1;}return r;}
static void aes_inv_init(void){int i;if(inv_ready)return;for(i=0;i<256;i++)invs[sbox[i]]=(u8)i;inv_ready=1;}
static void aes_add(u8 s[16],const u8 *rk){int i;for(i=0;i<16;i++)s[i]^=rk[i];}
static void aes_inv_sub(u8 s[16]){int i;aes_inv_init();for(i=0;i<16;i++)s[i]=invs[s[i]];}
static void aes_inv_shift(u8 s[16]){
    u8 t;
    t=s[13];s[13]=s[9];s[9]=s[5];s[5]=s[1];s[1]=t;
    t=s[2];s[2]=s[10];s[10]=t;t=s[6];s[6]=s[14];s[14]=t;
    t=s[3];s[3]=s[7];s[7]=s[11];s[11]=s[15];s[15]=t;
}
static void aes_inv_mix(u8 s[16]){
    int c; for(c=0;c<4;c++){u8 *p=s+c*4,a=p[0],b=p[1],d=p[2],e=p[3];
        p[0]=gm(a,14)^gm(b,11)^gm(d,13)^gm(e,9);
        p[1]=gm(a,9)^gm(b,14)^gm(d,11)^gm(e,13);
        p[2]=gm(a,13)^gm(b,9)^gm(d,14)^gm(e,11);
        p[3]=gm(a,11)^gm(b,13)^gm(d,9)^gm(e,14);}
}
static void aes_key_expand(const u8 key[16],u8 rk[176]){
    static const u8 rcon[10]={1,2,4,8,16,32,64,128,27,54};
    int bytes=16,rc=0,i;u8 t[4];memcpy(rk,key,16);
    while(bytes<176){for(i=0;i<4;i++)t[i]=rk[bytes-4+i];if((bytes&15)==0){u8 x=t[0];t[0]=sbox[t[1]]^rcon[rc++];t[1]=sbox[t[2]];t[2]=sbox[t[3]];t[3]=sbox[x];}for(i=0;i<4;i++){rk[bytes]=rk[bytes-16]^t[i];bytes++;}}
}
static void aes128_decrypt(const u8 key[16],const u8 in[16],u8 out[16]){
    u8 s[16],rk[176];int r;memcpy(s,in,16);aes_key_expand(key,rk);aes_add(s,rk+160);
    for(r=9;r>0;r--){aes_inv_shift(s);aes_inv_sub(s);aes_add(s,rk+r*16);aes_inv_mix(s);}aes_inv_shift(s);aes_inv_sub(s);aes_add(s,rk);memcpy(out,s,16);memset(rk,0,sizeof(rk));memset(s,0,sizeof(s));
}

int titan_aes_key_unwrap(const u8 kek[16], const u8 *in, u32 in_len,
                         u8 *out, u32 out_cap, u32 *out_len)
{
    u8 a[8], b[16]; u32 n,i,j; u64 t;
    static const u8 iv[8]={0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6};
    if (!in || !out || in_len<24 || (in_len&7)) return -1;
    n=in_len/8-1; if(out_cap<n*8)return -2;
    memcpy(a,in,8); memcpy(out,in+8,n*8);
    for(j=6;j>0;j--){
        for(i=n;i>0;i--){
            t=(u64)(n*(j-1)+i);
            memcpy(b,a,8);
            b[0]^=(u8)(t>>56);b[1]^=(u8)(t>>48);b[2]^=(u8)(t>>40);b[3]^=(u8)(t>>32);
            b[4]^=(u8)(t>>24);b[5]^=(u8)(t>>16);b[6]^=(u8)(t>>8);b[7]^=(u8)t;
            memcpy(b+8,out+(i-1)*8,8);
            aes128_decrypt(kek,b,b);
            memcpy(a,b,8); memcpy(out+(i-1)*8,b+8,8);
        }
    }
    if(!titan_consttime_eq(a,iv,8)){memset(out,0,n*8);return -3;}
    if(out_len)*out_len=n*8;
    return 0;
}
