#include "solana_client.h"

#include <curl/curl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SOLANA_DEFAULT_TIMEOUT_MS 10000L

typedef struct solana_response_buffer {
    char *data;
    size_t length;
} solana_response_buffer_t;

static size_t solana_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    solana_response_buffer_t *buffer = (solana_response_buffer_t *)userp;
    size_t written = 0u;

    if ((buffer != NULL) && (total > 0u))
    {
        char *new_data = (char *)realloc(buffer->data, buffer->length + total + 1u);
        if (new_data != NULL)
        {
            memcpy(new_data + buffer->length, contents, total);
            buffer->data = new_data;
            buffer->length += total;
            buffer->data[buffer->length] = '\0';
            written = total;
        }
    }

    return written;
}

static char *solana_strdup(const char *source)
{
    if (source == NULL)
    {
        return NULL;
    }

    size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

static int solana_ensure_curl_initialized(void)
{
    static int initialized = 0;
    static int init_status = SOLANA_OK;

    if (initialized == 0)
    {
        CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK)
        {
            init_status = SOLANA_ERROR_CURL;
        }
        initialized = 1;
    }

    return init_status;
}

int solana_client_init(solana_client_t *client, const char *rpc_url)
{
    if (client == NULL || rpc_url == NULL || rpc_url[0] == '\0')
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    int init_status = solana_ensure_curl_initialized();
    if (init_status != SOLANA_OK)
    {
        return init_status;
    }

    char *url_copy = solana_strdup(rpc_url);
    if (url_copy == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    client->rpc_url = url_copy;
    client->timeout_ms = SOLANA_DEFAULT_TIMEOUT_MS;
    client->next_request_id = 1u;

    return SOLANA_OK;
}

void solana_client_cleanup(solana_client_t *client)
{
    if (client == NULL)
    {
        return;
    }

    free(client->rpc_url);
    client->rpc_url = NULL;
    client->timeout_ms = SOLANA_DEFAULT_TIMEOUT_MS;
    client->next_request_id = 1u;
}

void solana_client_set_timeout(solana_client_t *client, long timeout_ms)
{
    if (client == NULL)
    {
        return;
    }

    if (timeout_ms <= 0)
    {
        client->timeout_ms = SOLANA_DEFAULT_TIMEOUT_MS;
    }
    else
    {
        client->timeout_ms = timeout_ms;
    }
}

long solana_client_get_timeout(const solana_client_t *client)
{
    if (client == NULL)
    {
        return SOLANA_DEFAULT_TIMEOUT_MS;
    }

    return client->timeout_ms;
}

void solana_client_free_response(char *response)
{
    free(response);
}

static void solana_response_buffer_reset(solana_response_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0u;
}

int solana_client_rpc_request(solana_client_t *client,
                              const char *method,
                              const char *params_json,
                              char **out_response)
{
    int status = SOLANA_ERROR_INVALID_ARGUMENT;
    solana_response_buffer_t buffer;
    const char *params = NULL;
    uint64_t request_id = 0u;
    char *payload = NULL;
    size_t payload_length = 0u;
    int written = 0;
    CURL *curl_handle = NULL;
    struct curl_slist *headers = NULL;
    CURLcode perform_result = CURLE_OK;
    long http_status = 0;

    if ((client == NULL) || (client->rpc_url == NULL) || (method == NULL) || (method[0] == '\0') || (out_response == NULL))
    {
        return status;
    }

    *out_response = NULL;
    memset(&buffer, 0, sizeof(buffer));

    params = ((params_json != NULL) && (params_json[0] != '\0')) ? params_json : "[]";
    request_id = client->next_request_id++;

    payload_length = (size_t)snprintf(NULL, 0,
                                      "{\"jsonrpc\":\"2.0\",\"id\":%" PRIu64 ",\"method\":\"%s\",\"params\":%s}",
                                      request_id,
                                      method,
                                      params);

    payload = (char *)malloc(payload_length + 1u);
    if (payload == NULL)
    {
        status = SOLANA_ERROR_ALLOCATION_FAILED;
    }
    else
    {
        written = snprintf(payload, payload_length + 1u,
                           "{\"jsonrpc\":\"2.0\",\"id\":%" PRIu64 ",\"method\":\"%s\",\"params\":%s}",
                           request_id,
                           method,
                           params);
        if ((written < 0) || ((size_t)written != payload_length))
        {
            status = SOLANA_ERROR_CURL;
        }
        else
        {
            status = SOLANA_OK;
        }
    }

    if (status == SOLANA_OK)
    {
        curl_handle = curl_easy_init();
        if (curl_handle == NULL)
        {
            status = SOLANA_ERROR_CURL;
        }
    }

    if (status == SOLANA_OK)
    {
        headers = curl_slist_append(NULL, "Content-Type: application/json");
        if (headers == NULL)
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
    }

    if (status == SOLANA_OK)
    {
        curl_easy_setopt(curl_handle, CURLOPT_URL, client->rpc_url);
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long)payload_length);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, client->timeout_ms);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, solana_write_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "c_wallet/solana_client");

        perform_result = curl_easy_perform(curl_handle);
        if (perform_result != CURLE_OK)
        {
            status = SOLANA_ERROR_CURL;
        }
        else
        {
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);
        }
    }

    if ((status == SOLANA_OK) && (buffer.data == NULL))
    {
        buffer.data = (char *)malloc(1u);
        if (buffer.data == NULL)
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
        else
        {
            buffer.data[0] = '\0';
            buffer.length = 0u;
        }
    }

    if ((status == SOLANA_OK) && ((http_status < 200) || (http_status >= 300)))
    {
        status = SOLANA_ERROR_HTTP_STATUS;
    }

    if (headers != NULL)
    {
        curl_slist_free_all(headers);
    }

    if (curl_handle != NULL)
    {
        curl_easy_cleanup(curl_handle);
    }

    if (payload != NULL)
    {
        free(payload);
    }

    if ((status == SOLANA_OK) || (status == SOLANA_ERROR_HTTP_STATUS))
    {
        *out_response = buffer.data;
    }
    else
    {
        solana_response_buffer_reset(&buffer);
    }

    return status;
}

int solana_client_get_latest_blockhash(solana_client_t *client, char **out_response)
{
    return solana_client_rpc_request(client,
                                     "getLatestBlockhash",
                                     "[{\"commitment\":\"finalized\"}]",
                                     out_response);
}

int solana_client_request_airdrop(solana_client_t *client,
                                  const char *public_key_base58,
                                  uint64_t lamports,
                                  char **out_response)
{
    int status = SOLANA_ERROR_INVALID_ARGUMENT;
    size_t params_length = 0u;
    char *params = NULL;
    int written = 0;

    if (out_response != NULL)
    {
        *out_response = NULL;
    }

    if ((client != NULL) && (public_key_base58 != NULL) && (public_key_base58[0] != '\0') && (out_response != NULL))
    {
        params_length = (size_t)snprintf(NULL, 0, "[\"%s\",%" PRIu64 "]", public_key_base58, lamports);
        params = (char *)malloc(params_length + 1u);
        if (params != NULL)
        {
            written = snprintf(params, params_length + 1u, "[\"%s\",%" PRIu64 "]", public_key_base58, lamports);
            if ((written < 0) || ((size_t)written != params_length))
            {
                status = SOLANA_ERROR_CURL;
            }
            else
            {
                status = solana_client_rpc_request(client, "requestAirdrop", params, out_response);
            }
        }
        else
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
    }

    if (params != NULL)
    {
        free(params);
    }

    return status;
}

int solana_client_get_balance(solana_client_t *client,
                              const char *public_key_base58,
                              char **out_response)
{
    int status = SOLANA_ERROR_INVALID_ARGUMENT;
    size_t params_length = 0u;
    char *params = NULL;
    int written = 0;

    if (out_response != NULL)
    {
        *out_response = NULL;
    }

    if ((client != NULL) && (public_key_base58 != NULL) && (public_key_base58[0] != '\0') && (out_response != NULL))
    {
        params_length = (size_t)snprintf(NULL, 0, "[\"%s\",{\"commitment\":\"confirmed\"}]", public_key_base58);
        params = (char *)malloc(params_length + 1u);
        if (params != NULL)
        {
            written = snprintf(params, params_length + 1u, "[\"%s\",{\"commitment\":\"confirmed\"}]", public_key_base58);
            if ((written < 0) || ((size_t)written != params_length))
            {
                status = SOLANA_ERROR_CURL;
            }
            else
            {
                status = solana_client_rpc_request(client, "getBalance", params, out_response);
            }
        }
        else
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
    }

    if (params != NULL)
    {
        free(params);
    }

    return status;
}

int solana_client_get_signature_status(solana_client_t *client,
                                       const char *signature,
                                       char **out_response)
{
    int status = SOLANA_ERROR_INVALID_ARGUMENT;
    size_t params_length = 0u;
    char *params = NULL;
    int written = 0;

    if (out_response != NULL)
    {
        *out_response = NULL;
    }

    if ((client != NULL) && (signature != NULL) && (signature[0] != '\0') && (out_response != NULL))
    {
        params_length = (size_t)snprintf(NULL, 0, "[[\"%s\"],{\"searchTransactionHistory\":true}]", signature);
        params = (char *)malloc(params_length + 1u);
        if (params != NULL)
        {
            written = snprintf(params, params_length + 1u, "[[\"%s\"],{\"searchTransactionHistory\":true}]", signature);
            if ((written < 0) || ((size_t)written != params_length))
            {
                status = SOLANA_ERROR_CURL;
            }
            else
            {
                status = solana_client_rpc_request(client, "getSignatureStatuses", params, out_response);
            }
        }
        else
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
    }

    if (params != NULL)
    {
        free(params);
    }

    return status;
}

int solana_client_send_transaction(solana_client_t *client,
                                   const char *signed_transaction_base64,
                                   char **out_response)
{
    int status = SOLANA_ERROR_INVALID_ARGUMENT;
    static const char *params_template = "[\"%s\",{\"encoding\":\"base64\"}]";
    size_t params_length = 0u;
    char *params = NULL;
    int written = 0;

    if (out_response != NULL)
    {
        *out_response = NULL;
    }

    if ((client != NULL) && (signed_transaction_base64 != NULL) && (signed_transaction_base64[0] != '\0') && (out_response != NULL))
    {
        params_length = (size_t)snprintf(NULL, 0, params_template, signed_transaction_base64);
        params = (char *)malloc(params_length + 1u);
        if (params != NULL)
        {
            written = snprintf(params, params_length + 1u, params_template, signed_transaction_base64);
            if ((written < 0) || ((size_t)written != params_length))
            {
                status = SOLANA_ERROR_CURL;
            }
            else
            {
                status = solana_client_rpc_request(client, "sendTransaction", params, out_response);
            }
        }
        else
        {
            status = SOLANA_ERROR_ALLOCATION_FAILED;
        }
    }

    if (params != NULL)
    {
        free(params);
    }

    return status;
}
