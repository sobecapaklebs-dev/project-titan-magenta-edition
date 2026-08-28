#ifndef TITAN_WIFI_CRYPTO_H
#define TITAN_WIFI_CRYPTO_H

#include <types.h>

typedef struct TitanSha1Ctx {
    u32 h[5];
    u64 total_bits;
    u8 block[64];
    u32 block_used;
} TitanSha1Ctx;

void titan_sha1_init(TitanSha1Ctx *ctx);
void titan_sha1_update(TitanSha1Ctx *ctx, const void *data, u32 len);
void titan_sha1_final(TitanSha1Ctx *ctx, u8 out[20]);
void titan_hmac_sha1(const u8 *key, u32 key_len,
                     const u8 *data, u32 data_len, u8 out[20]);
void titan_wpa_prf512(const u8 pmk[32], const u8 *label, u32 label_len,
                      const u8 *data, u32 data_len, u8 out[64]);
int titan_consttime_eq(const u8 *a, const u8 *b, u32 len);

/* TKIP Michael MIC over the 802.11 Michael pseudo-header
 * (DA || SA || priority || 0,0,0) followed by MSDU bytes. The caller must
 * supply the 8-byte RX or TX Michael key appropriate for the direction. */
void titan_michael_mic(const u8 key[8], const u8 da[6], const u8 sa[6],
                       u8 priority, const u8 *data, u32 data_len, u8 mic[8]);

/* RFC 3394 AES Key Unwrap. out_cap must be >= in_len - 8. */
int titan_aes_key_unwrap(const u8 kek[16], const u8 *in, u32 in_len,
                         u8 *out, u32 out_cap, u32 *out_len);

#endif
