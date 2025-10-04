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

    if (total == 0 || buffer == NULL)
    {
        return 0;
    }

    char *new_data = (char *)realloc(buffer->data, buffer->length + total + 1u);
    if (new_data == NULL)
    {
        return 0;
    }

    memcpy(new_data + buffer->length, contents, total);
    buffer->data = new_data;
    buffer->length += total;
    buffer->data[buffer->length] = '\0';

    return total;
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

    if (!initialized)
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
    if (client == NULL || client->rpc_url == NULL || method == NULL || method[0] == '\0' || out_response == NULL)
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    *out_response = NULL;

    solana_response_buffer_t buffer = {0};
    const char *params = (params_json != NULL && params_json[0] != '\0') ? params_json : "[]";
    uint64_t request_id = client->next_request_id++;

    static const char *payload_template =
        "{\"jsonrpc\":\"2.0\",\"id\":%" PRIu64 ",\"method\":\"%s\",\"params\":%s}";

    size_t payload_length = (size_t)snprintf(NULL, 0, payload_template, request_id, method, params);
    char *payload = (char *)malloc(payload_length + 1u);
    if (payload == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    int written = snprintf(payload, payload_length + 1u, payload_template, request_id, method, params);
    if (written < 0 || (size_t)written != payload_length)
    {
        free(payload);
        return SOLANA_ERROR_CURL;
    }

    CURL *curl_handle = curl_easy_init();
    if (curl_handle == NULL)
    {
        free(payload);
        return SOLANA_ERROR_CURL;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers == NULL)
    {
        curl_easy_cleanup(curl_handle);
        free(payload);
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, client->rpc_url);
    curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long)payload_length);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, client->timeout_ms);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, solana_write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "c_wallet/solana_client");

    CURLcode perform_result = curl_easy_perform(curl_handle);

    long http_status = 0;
    if (perform_result == CURLE_OK)
    {
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);
    free(payload);

    if (perform_result != CURLE_OK)
    {
        solana_response_buffer_reset(&buffer);
        return SOLANA_ERROR_CURL;
    }

    if (buffer.data == NULL)
    {
        buffer.data = (char *)malloc(1u);
        if (buffer.data == NULL)
        {
            return SOLANA_ERROR_ALLOCATION_FAILED;
        }
        buffer.data[0] = '\0';
        buffer.length = 0u;
    }

    if (http_status >= 200 && http_status < 300)
    {
        *out_response = buffer.data;
        return SOLANA_OK;
    }

    *out_response = buffer.data;
    return SOLANA_ERROR_HTTP_STATUS;
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
    if (client == NULL || public_key_base58 == NULL || public_key_base58[0] == '\0' || out_response == NULL)
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    size_t params_length = (size_t)snprintf(NULL, 0, "[\"%s\",%" PRIu64 "]", public_key_base58, lamports);
    char *params = (char *)malloc(params_length + 1u);
    if (params == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    int written = snprintf(params, params_length + 1u, "[\"%s\",%" PRIu64 "]", public_key_base58, lamports);
    if (written < 0 || (size_t)written != params_length)
    {
        free(params);
        return SOLANA_ERROR_CURL;
    }

    int status = solana_client_rpc_request(client, "requestAirdrop", params, out_response);
    free(params);
    return status;
}

int solana_client_get_balance(solana_client_t *client,
                              const char *public_key_base58,
                              char **out_response)
                              {
    if (client == NULL || public_key_base58 == NULL || public_key_base58[0] == '\0' || out_response == NULL)
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    size_t params_length = (size_t)snprintf(NULL, 0, "[\"%s\",{\"commitment\":\"confirmed\"}]", public_key_base58);
    char *params = (char *)malloc(params_length + 1u);
    if (params == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    int written = snprintf(params, params_length + 1u, "[\"%s\",{\"commitment\":\"confirmed\"}]", public_key_base58);
    if (written < 0 || (size_t)written != params_length)
    {
        free(params);
        return SOLANA_ERROR_CURL;
    }

    int status = solana_client_rpc_request(client, "getBalance", params, out_response);
    free(params);
    return status;
}

int solana_client_get_signature_status(solana_client_t *client,
                                       const char *signature,
                                       char **out_response)
                                       {
    if (client == NULL || signature == NULL || signature[0] == '\0' || out_response == NULL)
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    size_t params_length = (size_t)snprintf(NULL, 0, "[[\"%s\"],{\"searchTransactionHistory\":true}]", signature);
    char *params = (char *)malloc(params_length + 1u);
    if (params == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    int written = snprintf(params, params_length + 1u, "[[\"%s\"],{\"searchTransactionHistory\":true}]", signature);
    if (written < 0 || (size_t)written != params_length)
    {
        free(params);
        return SOLANA_ERROR_CURL;
    }

    int status = solana_client_rpc_request(client, "getSignatureStatuses", params, out_response);
    free(params);
    return status;
}

int solana_client_send_transaction(solana_client_t *client,
                                    const char *signed_transaction_base64,
                                    char **out_response)
                                    {
    if (signed_transaction_base64 == NULL || signed_transaction_base64[0] == '\0')
    {
        return SOLANA_ERROR_INVALID_ARGUMENT;
    }

    static const char *params_template = "[\"%s\",{\"encoding\":\"base64\"}]";
    size_t params_length = (size_t)snprintf(NULL, 0, params_template, signed_transaction_base64);
    char *params = (char *)malloc(params_length + 1u);
    if (params == NULL)
    {
        return SOLANA_ERROR_ALLOCATION_FAILED;
    }

    int written = snprintf(params, params_length + 1u, params_template, signed_transaction_base64);
    if (written < 0 || (size_t)written != params_length)
    {
        free(params);
        return SOLANA_ERROR_CURL;
    }

    int status = solana_client_rpc_request(client, "sendTransaction", params, out_response);
    free(params);
    return status;
}
