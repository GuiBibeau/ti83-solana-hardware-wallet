#ifndef SOLANA_ENCODING_H
#define SOLANA_ENCODING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int solana_base58_encode(const uint8_t *data,
                         size_t data_len,
                         char *out,
                         size_t out_size);

int solana_base58_decode(const char *input,
                         uint8_t *out,
                         size_t out_size);

size_t solana_base64_encode(const uint8_t *data,
                            size_t data_len,
                            char *out,
                            size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
