#include "wallet_crypto.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ed25519.h"
#include "sha512.h"

#define SHA512_BLOCK_SIZE 128u
#define SHA512_DIGEST_LENGTH 64u
#define PBKDF2_ITERATIONS 200000u

static void hmac_sha512(const uint8_t *key,
                        size_t key_len,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t *out_digest);
static int pbkdf2_hmac_sha512(const uint8_t *password,
                              size_t password_len,
                              const uint8_t *salt,
                              size_t salt_len,
                              uint32_t iterations,
                              uint8_t *output,
                              size_t output_len);
static void derive_stream_key(const uint8_t *master_key,
                              const uint8_t *nonce,
                              size_t nonce_len,
                              const char *label,
                              uint8_t *derived,
                              size_t derived_len);
static int constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len);

int wallet_random_bytes(uint8_t *buffer, size_t length)
{
    int status = APP_ERR_IO;

    if (buffer != NULL)
    {
        size_t offset = 0u;

        while (offset < length)
        {
            uint8_t seed[WALLET_SEED_LEN];
            size_t copy_len = length - offset;

            if (ed25519_create_seed(seed) != 0)
            {
                wallet_secure_zero(seed, sizeof(seed));
                status = APP_ERR_IO;
                break;
            }

            if (copy_len > sizeof(seed))
            {
                copy_len = sizeof(seed);
            }

            memcpy(buffer + offset, seed, copy_len);
            wallet_secure_zero(seed, sizeof(seed));
            offset += copy_len;
        }

        if (offset >= length)
        {
            status = APP_OK;
        }
    }

    return status;
}

void wallet_secure_zero(void *ptr, size_t length)
{
    if (ptr == NULL)
    {
        return;
    }

    volatile uint8_t *volatile bytes = (volatile uint8_t *volatile)ptr;
    for (size_t i = 0; i < length; i++)
    {
        bytes[i] = 0u;
    }
}

int wallet_encrypt_private_key(const char *password,
                               const uint8_t *private_key,
                               size_t private_key_len,
                               uint8_t *out_blob,
                               size_t out_blob_len)
{
    int status = APP_ERR_IO;
    size_t password_len = 0u;
    uint8_t salt[WALLET_SALT_LEN];
    uint8_t nonce[WALLET_NONCE_LEN];
    uint8_t master_key[SHA512_DIGEST_LENGTH];
    uint8_t keystream[WALLET_PRIVATE_KEY_LEN];
    uint8_t mac_key[SHA512_DIGEST_LENGTH];
    uint8_t ciphertext[WALLET_PRIVATE_KEY_LEN];
    uint8_t auth_input[WALLET_NONCE_LEN + WALLET_PRIVATE_KEY_LEN];
    uint8_t mac_full[SHA512_DIGEST_LENGTH];
    uint8_t mac_truncated[WALLET_MAC_LEN];

    memset(master_key, 0, sizeof(master_key));
    memset(keystream, 0, sizeof(keystream));
    memset(mac_key, 0, sizeof(mac_key));
    memset(ciphertext, 0, sizeof(ciphertext));
    memset(auth_input, 0, sizeof(auth_input));
    memset(mac_full, 0, sizeof(mac_full));
    memset(mac_truncated, 0, sizeof(mac_truncated));

    if ((password != NULL) && (private_key != NULL) && (out_blob != NULL))
    {
        if ((private_key_len == WALLET_PRIVATE_KEY_LEN) && (out_blob_len >= WALLET_BLOB_LEN))
        {
            password_len = strlen(password);
            if (password_len > 0u)
            {
                int salt_status = wallet_random_bytes(salt, sizeof(salt));
                int nonce_status = wallet_random_bytes(nonce, sizeof(nonce));
                if ((salt_status == APP_OK) && (nonce_status == APP_OK))
                {
                    status = pbkdf2_hmac_sha512((const uint8_t *)password,
                                                password_len,
                                                salt,
                                                sizeof(salt),
                                                PBKDF2_ITERATIONS,
                                                master_key,
                                                sizeof(master_key));
                    if (status == APP_OK)
                    {
                        derive_stream_key(master_key, nonce, sizeof(nonce), "ENC", keystream, sizeof(keystream));
                        derive_stream_key(master_key, nonce, sizeof(nonce), "MAC", mac_key, sizeof(mac_key));

                        for (size_t index = 0u; index < WALLET_PRIVATE_KEY_LEN; index++)
                        {
                            ciphertext[index] = private_key[index] ^ keystream[index];
                        }

                        memcpy(auth_input, nonce, sizeof(nonce));
                        memcpy(auth_input + sizeof(nonce), ciphertext, sizeof(ciphertext));

                        hmac_sha512(mac_key, sizeof(mac_key), auth_input, sizeof(auth_input), mac_full);
                        memcpy(mac_truncated, mac_full, sizeof(mac_truncated));

                        out_blob[0] = WALLET_BLOB_VERSION;
                        memcpy(out_blob + 1u, salt, sizeof(salt));
                        memcpy(out_blob + 1u + sizeof(salt), nonce, sizeof(nonce));
                        memcpy(out_blob + 1u + sizeof(salt) + sizeof(nonce), ciphertext, sizeof(ciphertext));
                        memcpy(out_blob + 1u + sizeof(salt) + sizeof(nonce) + sizeof(ciphertext), mac_truncated, sizeof(mac_truncated));

                        status = APP_OK;
                    }
                    else
                    {
                        status = APP_ERR_CRYPTO;
                    }
                }
            }
        }
    }

    wallet_secure_zero(master_key, sizeof(master_key));
    wallet_secure_zero(keystream, sizeof(keystream));
    wallet_secure_zero(mac_key, sizeof(mac_key));
    wallet_secure_zero(mac_full, sizeof(mac_full));
    wallet_secure_zero(mac_truncated, sizeof(mac_truncated));
    wallet_secure_zero(ciphertext, sizeof(ciphertext));
    wallet_secure_zero(auth_input, sizeof(auth_input));
    wallet_secure_zero(salt, sizeof(salt));
    wallet_secure_zero(nonce, sizeof(nonce));

    return status;
}

int wallet_decrypt_private_key(const char *password,
                               const uint8_t *blob,
                               size_t blob_len,
                               uint8_t *out_private_key,
                               size_t private_key_len)
{
    int status = APP_ERR_IO;
    size_t password_len = 0u;
    const uint8_t *salt = NULL;
    const uint8_t *nonce = NULL;
    const uint8_t *ciphertext = NULL;
    const uint8_t *mac = NULL;
    uint8_t master_key[SHA512_DIGEST_LENGTH];
    uint8_t mac_key[SHA512_DIGEST_LENGTH];
    uint8_t keystream[WALLET_PRIVATE_KEY_LEN];
    uint8_t auth_input[WALLET_NONCE_LEN + WALLET_PRIVATE_KEY_LEN];
    uint8_t mac_full[SHA512_DIGEST_LENGTH];

    memset(master_key, 0, sizeof(master_key));
    memset(mac_key, 0, sizeof(mac_key));
    memset(keystream, 0, sizeof(keystream));
    memset(auth_input, 0, sizeof(auth_input));
    memset(mac_full, 0, sizeof(mac_full));

    if ((password != NULL) && (blob != NULL) && (out_private_key != NULL))
    {
        if ((blob_len >= WALLET_BLOB_LEN) && (private_key_len == WALLET_PRIVATE_KEY_LEN))
        {
            password_len = strlen(password);
            if (password_len > 0u)
            {
                if (blob[0] == WALLET_BLOB_VERSION)
                {
                    salt = blob + 1u;
                    nonce = salt + WALLET_SALT_LEN;
                    ciphertext = nonce + WALLET_NONCE_LEN;
                    mac = ciphertext + WALLET_PRIVATE_KEY_LEN;

                    status = pbkdf2_hmac_sha512((const uint8_t *)password,
                                                password_len,
                                                salt,
                                                WALLET_SALT_LEN,
                                                PBKDF2_ITERATIONS,
                                                master_key,
                                                sizeof(master_key));
                    if (status == APP_OK)
                    {
                        int mac_mismatch = 0;

                        derive_stream_key(master_key, nonce, WALLET_NONCE_LEN, "MAC", mac_key, sizeof(mac_key));
                        memcpy(auth_input, nonce, WALLET_NONCE_LEN);
                        memcpy(auth_input + WALLET_NONCE_LEN, ciphertext, WALLET_PRIVATE_KEY_LEN);
                        hmac_sha512(mac_key, sizeof(mac_key), auth_input, sizeof(auth_input), mac_full);
                        mac_mismatch = constant_time_compare(mac, mac_full, WALLET_MAC_LEN);

                        if (mac_mismatch == 0)
                        {
                            derive_stream_key(master_key, nonce, WALLET_NONCE_LEN, "ENC", keystream, sizeof(keystream));

                            for (size_t index = 0u; index < WALLET_PRIVATE_KEY_LEN; index++)
                            {
                                out_private_key[index] = ciphertext[index] ^ keystream[index];
                            }

                            status = APP_OK;
                        }
                        else
                        {
                            status = APP_ERR_CRYPTO;
                        }
                    }
                    else
                    {
                        status = APP_ERR_CRYPTO;
                    }
                }
                else
                {
                    status = APP_ERR_CRYPTO;
                }
            }
        }
    }

    if ((status != APP_OK) && (out_private_key != NULL) && (private_key_len == WALLET_PRIVATE_KEY_LEN))
    {
        wallet_secure_zero(out_private_key, private_key_len);
    }

    wallet_secure_zero(master_key, sizeof(master_key));
    wallet_secure_zero(mac_key, sizeof(mac_key));
    wallet_secure_zero(mac_full, sizeof(mac_full));
    wallet_secure_zero(auth_input, sizeof(auth_input));
    wallet_secure_zero(keystream, sizeof(keystream));

    return status;
}

static void hmac_sha512(const uint8_t *key,
                        size_t key_len,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t *out_digest)
{
    uint8_t kopad[SHA512_BLOCK_SIZE];
    uint8_t kipad[SHA512_BLOCK_SIZE];
    uint8_t temp_key[SHA512_DIGEST_LENGTH];

    if (key_len > SHA512_BLOCK_SIZE)
    {
        sha512(key, key_len, temp_key);
        key = temp_key;
        key_len = SHA512_DIGEST_LENGTH;
    }

    memset(kopad, 0, sizeof(kopad));
    memset(kipad, 0, sizeof(kipad));
    memcpy(kopad, key, key_len);
    memcpy(kipad, key, key_len);

    for (size_t i = 0; i < SHA512_BLOCK_SIZE; i++)
    {
        kopad[i] ^= 0x5c;
        kipad[i] ^= 0x36;
    }

    uint8_t inner_digest[SHA512_DIGEST_LENGTH];
    sha512_context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, kipad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, data, data_len);
    sha512_final(&ctx, inner_digest);

    sha512_init(&ctx);
    sha512_update(&ctx, kopad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, inner_digest, SHA512_DIGEST_LENGTH);
    sha512_final(&ctx, out_digest);

    wallet_secure_zero(inner_digest, sizeof(inner_digest));
    wallet_secure_zero(kopad, sizeof(kopad));
    wallet_secure_zero(kipad, sizeof(kipad));
    wallet_secure_zero(temp_key, sizeof(temp_key));
}

static int pbkdf2_hmac_sha512(const uint8_t *password,
                              size_t password_len,
                              const uint8_t *salt,
                              size_t salt_len,
                              uint32_t iterations,
                              uint8_t *output,
                              size_t output_len)
{
    int status = APP_ERR_IO;
    uint32_t block_index = 0u;
    uint8_t u[SHA512_DIGEST_LENGTH];
    uint8_t t[SHA512_DIGEST_LENGTH];
    uint8_t first_input[WALLET_SALT_LEN + 4u];

    if ((password == NULL) || (salt == NULL) || (output == NULL) || (iterations == 0u))
    {
        return APP_ERR_IO;
    }

    if (salt_len > WALLET_SALT_LEN)
    {
        return APP_ERR_IO;
    }

    memset(u, 0, sizeof(u));
    memset(t, 0, sizeof(t));
    memset(first_input, 0, sizeof(first_input));

    for (block_index = 1u; block_index <= (uint32_t)((output_len + SHA512_DIGEST_LENGTH - 1u) / SHA512_DIGEST_LENGTH); block_index++)
    {
        uint32_t iteration_index = 0u;
        size_t offset = (size_t)(block_index - 1u) * SHA512_DIGEST_LENGTH;
        size_t copy_len = output_len - offset;
        size_t xor_index = 0u;

        memset(first_input, 0, sizeof(first_input));

        memcpy(first_input, salt, salt_len);
        first_input[salt_len + 0u] = (uint8_t)((block_index >> 24u) & 0xffu);
        first_input[salt_len + 1u] = (uint8_t)((block_index >> 16u) & 0xffu);
        first_input[salt_len + 2u] = (uint8_t)((block_index >> 8u) & 0xffu);
        first_input[salt_len + 3u] = (uint8_t)(block_index & 0xffu);

        hmac_sha512(password, password_len, first_input, salt_len + 4u, u);
        memcpy(t, u, sizeof(t));

        for (iteration_index = 1u; iteration_index < iterations; iteration_index++)
        {
            hmac_sha512(password, password_len, u, sizeof(u), u);
            for (xor_index = 0u; xor_index < sizeof(t); xor_index++)
            {
                t[xor_index] ^= u[xor_index];
            }
        }

        if (copy_len > SHA512_DIGEST_LENGTH)
        {
            copy_len = SHA512_DIGEST_LENGTH;
        }

        memcpy(output + offset, t, copy_len);
    }

    wallet_secure_zero(u, sizeof(u));
    wallet_secure_zero(t, sizeof(t));
    wallet_secure_zero(first_input, sizeof(first_input));

    status = APP_OK;
    return status;
}

static void derive_stream_key(const uint8_t *master_key,
                              const uint8_t *nonce,
                              size_t nonce_len,
                              const char *label,
                              uint8_t *derived,
                              size_t derived_len)
{
    uint8_t info[16u + WALLET_NONCE_LEN];
    uint8_t digest[SHA512_DIGEST_LENGTH];
    size_t label_len = 0u;
    size_t copy_len = 0u;

    if ((master_key == NULL) || (nonce == NULL) || (label == NULL) || (derived == NULL))
    {
        return;
    }

    if (nonce_len > WALLET_NONCE_LEN)
    {
        return;
    }

    memset(info, 0, sizeof(info));
    memset(digest, 0, sizeof(digest));

    label_len = strlen(label);
    if (label_len > sizeof(info))
    {
        label_len = sizeof(info);
    }

    memcpy(info, label, label_len);
    memcpy(info + label_len, nonce, nonce_len);

    hmac_sha512(master_key, SHA512_DIGEST_LENGTH, info, label_len + nonce_len, digest);

    copy_len = derived_len;
    if (copy_len > SHA512_DIGEST_LENGTH)
    {
        copy_len = SHA512_DIGEST_LENGTH;
    }

    memcpy(derived, digest, copy_len);
    wallet_secure_zero(digest, sizeof(digest));
    wallet_secure_zero(info, sizeof(info));
}

static int constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0u;
    size_t index = 0u;

    if ((a == NULL) || (b == NULL))
    {
        return 1;
    }

    for (index = 0u; index < len; index++)
    {
        diff |= (uint8_t)(a[index] ^ b[index]);
    }

    return (diff != 0u);
}
