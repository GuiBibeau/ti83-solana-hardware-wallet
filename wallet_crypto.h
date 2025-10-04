#ifndef WALLET_CRYPTO_H
#define WALLET_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "calc_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WALLET_BLOB_VERSION 1u
#define WALLET_SALT_LEN 16u
#define WALLET_NONCE_LEN 12u
#define WALLET_MAC_LEN 32u
#define WALLET_PUBLIC_KEY_LEN 32u
#define WALLET_PRIVATE_KEY_LEN 64u
#define WALLET_SEED_LEN 32u
#define WALLET_BLOB_LEN (1u + WALLET_SALT_LEN + WALLET_NONCE_LEN + WALLET_PRIVATE_KEY_LEN + WALLET_MAC_LEN)

int wallet_random_bytes(uint8_t *buffer, size_t length);
void wallet_secure_zero(void *ptr, size_t length);
int wallet_encrypt_private_key(const char *password,
                               const uint8_t *private_key,
                               size_t private_key_len,
                               uint8_t *out_blob,
                               size_t out_blob_len);
int wallet_decrypt_private_key(const char *password,
                               const uint8_t *blob,
                               size_t blob_len,
                               uint8_t *out_private_key,
                               size_t private_key_len);

#ifdef __cplusplus
}
#endif

#endif
