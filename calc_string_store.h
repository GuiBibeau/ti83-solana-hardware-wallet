#ifndef CALC_STRING_STORE_H
#define CALC_STRING_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "calc_session.h"

#ifdef __cplusplus
extern "C" {
#endif

int calc_store_persistent_string(CalcSession *session, const char *var_name, const char *payload);
int calc_fetch_string(CalcSession *session, const char *var_name, FileContent **out_content);
int calc_store_binary_string(CalcSession *session, const char *var_name, const uint8_t *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif
