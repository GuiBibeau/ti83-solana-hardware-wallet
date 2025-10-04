#ifndef SOLANA_CLIENT_H
#define SOLANA_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOLANA_OK 0
#define SOLANA_ERROR_INVALID_ARGUMENT -1
#define SOLANA_ERROR_ALLOCATION_FAILED -2
#define SOLANA_ERROR_CURL -3
#define SOLANA_ERROR_HTTP_STATUS -4

typedef struct solana_client {
    char *rpc_url;
    long timeout_ms;
    uint64_t next_request_id;
} solana_client_t;

int solana_client_init(solana_client_t *client, const char *rpc_url);
void solana_client_cleanup(solana_client_t *client);
void solana_client_set_timeout(solana_client_t *client, long timeout_ms);
long solana_client_get_timeout(const solana_client_t *client);

int solana_client_rpc_request(solana_client_t *client,
                              const char *method,
                              const char *params_json,
                              char **out_response);

int solana_client_get_latest_blockhash(solana_client_t *client, char **out_response);

int solana_client_request_airdrop(solana_client_t *client,
                                  const char *public_key_base58,
                                  uint64_t lamports,
                                  char **out_response);

int solana_client_get_balance(solana_client_t *client,
                              const char *public_key_base58,
                              char **out_response);

int solana_client_get_signature_status(solana_client_t *client,
                                       const char *signature,
                                       char **out_response);

int solana_client_send_transaction(solana_client_t *client,
                                    const char *signed_transaction_base64,
                                    char **out_response);

void solana_client_free_response(char *response);

#ifdef __cplusplus
}
#endif

#endif
