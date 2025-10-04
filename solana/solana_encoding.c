#include "solana_encoding.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char solana_base58_alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static int solana_base58_value(char c)
{
    if ((c >= '1') && (c <= '9'))
    {
        return (int)(c - '1');
    }
    if ((c >= 'A') && (c <= 'H'))
    {
        return (int)(c - 'A') + 9;
    }
    if ((c >= 'J') && (c <= 'N'))
    {
        return (int)(c - 'J') + 17;
    }
    if ((c >= 'P') && (c <= 'Z'))
    {
        return (int)(c - 'P') + 22;
    }
    if ((c >= 'a') && (c <= 'k'))
    {
        return (int)(c - 'a') + 33;
    }
    if ((c >= 'm') && (c <= 'z'))
    {
        return (int)(c - 'm') + 44;
    }
    return -1;
}

int solana_base58_encode(const uint8_t *data,
                         size_t data_len,
                         char *out,
                         size_t out_size)
{
    size_t zeros = 0u;
    size_t size = 0u;
    size_t idx = 0u;
    size_t out_index = 0u;
    uint8_t *buffer = NULL;
    int result = 0;

    if ((data == NULL) || (out == NULL) || (out_size == 0u))
    {
        return 0;
    }

    while ((zeros < data_len) && (data[zeros] == 0u))
    {
        zeros++;
    }

    if (data_len == zeros)
    {
        if (out_size < (zeros + 1u))
        {
            return 0;
        }

        for (idx = 0u; idx < zeros; idx++)
        {
            out[idx] = solana_base58_alphabet[0];
        }
        out[zeros] = '\0';
        return (int)zeros;
    }

    size = ((data_len - zeros) * 138u) / 100u + 1u;
    buffer = (uint8_t *)calloc(size, sizeof(uint8_t));
    if (buffer == NULL)
    {
        return 0;
    }

    for (idx = zeros; idx < data_len; idx++)
    {
        int carry = 0;
        size_t j = 0u;

        carry = data[idx];
        j = size;

        while (j > 0u)
        {
            carry += 256 * buffer[j - 1u];
            buffer[j - 1u] = (uint8_t)(carry % 58);
            carry /= 58;
            j--;
        }
    }

    idx = 0u;
    while ((idx < size) && (buffer[idx] == 0u))
    {
        idx++;
    }

    if ((zeros + (size - idx) + 1u) > out_size)
    {
        free(buffer);
        return 0;
    }

    for (out_index = 0u; out_index < zeros; out_index++)
    {
        out[out_index] = solana_base58_alphabet[0];
    }

    while (idx < size)
    {
        out[out_index++] = solana_base58_alphabet[buffer[idx++]];
    }

    out[out_index] = '\0';
    result = (int)out_index;
    free(buffer);
    return result;
}

int solana_base58_decode(const char *input,
                         uint8_t *out,
                         size_t out_size)
{
    size_t input_len = 0u;
    size_t zeros = 0u;
    size_t size = 0u;
    uint8_t *buffer = NULL;
    size_t input_index = 0u;
    size_t output_index = 0u;

    if ((input == NULL) || (out == NULL) || (out_size == 0u))
    {
        return 0;
    }

    input_len = strlen(input);
    if (input_len == 0u)
    {
        return 0;
    }

    while ((zeros < input_len) && (input[zeros] == solana_base58_alphabet[0]))
    {
        zeros++;
    }

    size = ((input_len - zeros) * 733u) / 1000u + 1u;
    buffer = (uint8_t *)calloc(size, sizeof(uint8_t));
    if (buffer == NULL)
    {
        return 0;
    }

    for (input_index = zeros; input_index < input_len; input_index++)
    {
        int carry = 0;
        size_t j = 0u;

        carry = solana_base58_value(input[input_index]);
        j = size;

        if (carry < 0)
        {
            free(buffer);
            return 0;
        }

        while (j > 0u)
        {
            carry += 58 * buffer[j - 1u];
            buffer[j - 1u] = (uint8_t)(carry % 256);
            carry /= 256;
            j--;
        }
    }

    input_index = 0u;
    while ((input_index < size) && (buffer[input_index] == 0u))
    {
        input_index++;
    }

    if ((zeros + (size - input_index)) > out_size)
    {
        free(buffer);
        return 0;
    }

    memset(out, 0, zeros);
    output_index = zeros;

    while (input_index < size)
    {
        out[output_index++] = buffer[input_index++];
    }

    free(buffer);
    return (int)output_index;
}

size_t solana_base64_encode(const uint8_t *data,
                            size_t data_len,
                            char *out,
                            size_t out_size)
{
    static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t encoded_len = 4u * ((data_len + 2u) / 3u);
    size_t input_index = 0u;
    size_t output_index = 0u;

    if ((data == NULL) || (out == NULL))
    {
        return 0u;
    }

    if (out_size < (encoded_len + 1u))
    {
        return 0u;
    }

    while (input_index < data_len)
    {
        size_t remaining = 0u;
        uint32_t octet_a = 0u;
        uint32_t octet_b = 0u;
        uint32_t octet_c = 0u;
        uint32_t triple = 0u;

        remaining = data_len - input_index;
        octet_a = data[input_index++];
        octet_b = (remaining > 1u) ? data[input_index++] : 0u;
        octet_c = (remaining > 2u) ? data[input_index++] : 0u;
        triple = (octet_a << 16u) | (octet_b << 8u) | octet_c;

        out[output_index++] = base64_table[(triple >> 18u) & 0x3fu];
        out[output_index++] = base64_table[(triple >> 12u) & 0x3fu];
        out[output_index++] = (remaining > 1u) ? base64_table[(triple >> 6u) & 0x3fu] : '=';
        out[output_index++] = (remaining > 2u) ? base64_table[triple & 0x3fu] : '=';
    }

    out[encoded_len] = '\0';
    return encoded_len;
}
