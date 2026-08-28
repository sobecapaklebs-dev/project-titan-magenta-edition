#ifndef TITAN_BLE_CRYPTO_H
#define TITAN_BLE_CRYPTO_H
#include <stdint.h>
void titan_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void titan_aes_cmac(const uint8_t key[16], const uint8_t *data, unsigned len, uint8_t out[16]);
void titan_ble_sc_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z, uint8_t out[16]);
void titan_ble_sc_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7], const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16]);
void titan_ble_sc_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16], const uint8_t iocap[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16]);
#endif
