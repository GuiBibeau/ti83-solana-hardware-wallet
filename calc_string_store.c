#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "calc_string_store.h"

#include "tifiles.h"
#include "ticonv.h"

static int build_string_entry(CalcSession *session, const char *var_name, const char *payload, FileContent **out_content);
static int build_binary_entry(CalcSession *session, const char *var_name, const uint8_t *payload, size_t payload_len, FileContent **out_content);

int calc_store_persistent_string(CalcSession *session, const char *var_name, const char *payload)
{
    FileContent *content = NULL;
    int status = APP_ERR_NO_CALC;

    if ((session != NULL) && (session->calc != NULL))
    {
        if ((var_name != NULL) && (payload != NULL))
        {
            status = build_string_entry(session, var_name, payload, &content);
            if (status == APP_OK)
            {
                int transfer_result = ticalcs_calc_send_var(session->calc, MODE_NORMAL, content);
                if (transfer_result != 0)
                {
                    char *error_text = NULL;
                    if ((ticalcs_error_get(transfer_result, &error_text) == 0) && (error_text != NULL))
                    {
                        fprintf(stderr, "ticalcs_calc_send_var failed: %s\n", error_text);
                        ticalcs_error_free(error_text);
                    }
                    else
                    {
                        fprintf(stderr, "ticalcs_calc_send_var failed with code %d\n", transfer_result);
                    }

                    status = APP_ERR_IO;
                }
            }
        }
        else
        {
            status = APP_ERR_IO;
        }
    }

    if (content != NULL)
    {
        tifiles_content_delete_regular(content);
    }

    return status;
}

int calc_fetch_string(CalcSession *session, const char *var_name, FileContent **out_content)
{
    char *tokenized_name = NULL;
    FileContent *content = NULL;
    VarEntry request;
    uint8_t string_type = 0u;
    int status = APP_ERR_NO_CALC;

    if ((session != NULL) && (session->calc != NULL))
    {
        status = APP_ERR_IO;
        if ((var_name != NULL) && (out_content != NULL))
        {
            *out_content = NULL;
            string_type = tifiles_string2vartype(session->calc_model, "String");
            if (string_type != 0u)
            {
                tokenized_name = ticonv_varname_tokenize(session->calc_model, var_name, string_type);
                if (tokenized_name != NULL)
                {
                    content = tifiles_content_create_regular(session->calc_model);
                    if (content == NULL)
                    {
                        status = APP_ERR_ALLOC;
                    }
                    else
                    {
                        memset(&request, 0, sizeof(request));
                        request.type = string_type;
                        strncpy(request.name, tokenized_name, sizeof(request.name) - 1u);

                        if (ticalcs_calc_recv_var(session->calc, MODE_NORMAL, content, &request) == 0)
                        {
                            *out_content = content;
                            status = APP_OK;
                            content = NULL;
                        }
                    }
                }
            }
        }
    }

    if (content != NULL)
    {
        tifiles_content_delete_regular(content);
    }

    if (tokenized_name != NULL)
    {
        ticonv_varname_free(tokenized_name);
    }

    return status;
}

int calc_store_binary_string(CalcSession *session, const char *var_name, const uint8_t *payload, size_t payload_len)
{
    FileContent *content = NULL;
    int status = APP_ERR_NO_CALC;

    if ((session != NULL) && (session->calc != NULL))
    {
        status = APP_ERR_IO;
        if ((var_name != NULL) && (payload != NULL))
        {
            status = build_binary_entry(session, var_name, payload, payload_len, &content);
            if (status == APP_OK)
            {
                int transfer_result = ticalcs_calc_send_var(session->calc, MODE_NORMAL, content);
                if (transfer_result != 0)
                {
                    char *error_text = NULL;
                    if ((ticalcs_error_get(transfer_result, &error_text) == 0) && (error_text != NULL))
                    {
                        fprintf(stderr, "ticalcs_calc_send_var failed: %s\n", error_text);
                        ticalcs_error_free(error_text);
                    }
                    else
                    {
                        fprintf(stderr, "ticalcs_calc_send_var failed with code %d\n", transfer_result);
                    }

                    status = APP_ERR_IO;
                }
            }
        }
    }

    if (content != NULL)
    {
        tifiles_content_delete_regular(content);
    }

    return status;
}

static int build_string_entry(CalcSession *session, const char *var_name, const char *payload, FileContent **out_content)
{
    FileContent *content = NULL;
    VarEntry *entry = NULL;
    uint8_t *data = NULL;
    unsigned short *utf16_payload = NULL;
    char *ti_payload = NULL;
    char *tokenized_name = NULL;
    size_t ti_length = 0u;
    size_t utf16_len = 0u;
    size_t ti_capacity = 0u;
    size_t entry_size = 0u;
    size_t name_len = 0u;
    uint8_t string_type = 0u;
    int status = APP_ERR_IO;

    if (out_content == NULL)
    {
        return APP_ERR_IO;
    }

    *out_content = NULL;

    if ((session == NULL) || (session->calc == NULL))
    {
        return APP_ERR_NO_CALC;
    }

    if ((var_name == NULL) || (payload == NULL))
    {
        return APP_ERR_IO;
    }

    string_type = tifiles_string2vartype(session->calc_model, "String");
    if (string_type == 0u)
    {
        fprintf(stderr, "String vartype lookup failed for model %s\n", ticalcs_model_to_string(session->calc_model));
        return APP_ERR_IO;
    }

    if ((strncmp(var_name, "Str", 3) != 0) || (strlen(var_name) != 4u) || (!isdigit((unsigned char)var_name[3])))
    {
        fprintf(stderr, "Unsupported string variable name '%s'\n", var_name);
        return APP_ERR_IO;
    }

    tokenized_name = ticonv_varname_tokenize(session->calc_model, var_name, string_type);
    if (tokenized_name == NULL)
    {
        fprintf(stderr, "Failed to tokenize variable name %s\n", var_name);
        return APP_ERR_IO;
    }

    utf16_payload = ticonv_utf8_to_utf16(payload);
    if (utf16_payload == NULL)
    {
        status = APP_ERR_ALLOC;
    }
    else
    {
        utf16_len = ticonv_utf16_strlen(utf16_payload);
        ti_capacity = (utf16_len * 4u) + 1u;
        ti_payload = (char *)calloc(ti_capacity, sizeof(char));
        if (ti_payload == NULL)
        {
            status = APP_ERR_ALLOC;
        }
        else if (ticonv_charset_utf16_to_ti_s(session->calc_model, utf16_payload, ti_payload) == NULL)
        {
            status = APP_ERR_IO;
        }
        else
        {
            ti_length = strlen(ti_payload);
            if (ti_length == 0u)
            {
                fprintf(stderr, "Converted payload is empty\n");
            }
            else if (ti_length > 255u)
            {
                fprintf(stderr, "TI string length %zu exceeds 255-byte limit\n", ti_length);
            }
            else
            {
                entry_size = ti_length + 1u;
                data = (uint8_t *)tifiles_ve_alloc_data(entry_size);
                if (data == NULL)
                {
                    status = APP_ERR_ALLOC;
                }
                else
                {
                    data[0] = (uint8_t)ti_length;
                    memcpy(data + 1u, ti_payload, ti_length);

                    content = tifiles_content_create_regular(session->calc_model);
                    if (content == NULL)
                    {
                        status = APP_ERR_ALLOC;
                    }
                    else
                    {
                        entry = tifiles_ve_create();
                        if (entry == NULL)
                        {
                            status = APP_ERR_ALLOC;
                        }
                        else
                        {
                            memset(entry, 0, sizeof(*entry));
                            entry->type = string_type;
                            entry->attr = ATTRB_NONE;
                            entry->version = 0;
                            entry->size = (uint32_t)entry_size;
                            entry->data = data;
                            entry->action = 0;

                            name_len = strlen(tokenized_name);
                            if (name_len > (sizeof(entry->name) - 1u))
                            {
                                name_len = sizeof(entry->name) - 1u;
                            }
                            memcpy(entry->name, tokenized_name, name_len);
                            entry->name[name_len] = '\0';

                            content->model = session->calc_model;
                            content->model_dst = session->calc_model;
                            (void)snprintf(content->comment, sizeof(content->comment), "Pushed from c_wallet");

                            content->entries = tifiles_ve_create_array(1);
                            if (content->entries == NULL)
                            {
                                status = APP_ERR_ALLOC;
                            }
                            else
                            {
                                content->entries[0] = entry;
                                content->num_entries = 1;
                                *out_content = content;
                                status = APP_OK;
                            }
                        }
                    }
                }
            }
        }
    }

    if (status != APP_OK)
    {
        if (content != NULL)
        {
            tifiles_content_delete_regular(content);
        }
        else if (entry != NULL)
        {
            tifiles_ve_delete(entry);
        }
        else if (data != NULL)
        {
            tifiles_ve_free_data(data);
        }
    }

    if (ti_payload != NULL)
    {
        free(ti_payload);
    }

    if (utf16_payload != NULL)
    {
        ticonv_utf16_free(utf16_payload);
    }

    if (tokenized_name != NULL)
    {
        ticonv_varname_free(tokenized_name);
    }

    return status;
}

static int build_binary_entry(CalcSession *session, const char *var_name, const uint8_t *payload, size_t payload_len, FileContent **out_content)
{
    FileContent *content = NULL;
    VarEntry *entry = NULL;
    uint8_t *data = NULL;
    char *tokenized_name = NULL;
    size_t entry_size = 0u;
    size_t name_len = 0u;
    uint8_t string_type = 0u;
    int status = APP_ERR_IO;

    if (out_content == NULL)
    {
        return APP_ERR_IO;
    }

    *out_content = NULL;

    if ((session == NULL) || (session->calc == NULL))
    {
        return APP_ERR_NO_CALC;
    }

    if ((var_name == NULL) || (payload == NULL))
    {
        return APP_ERR_IO;
    }

    if (payload_len > 255u)
    {
        fprintf(stderr, "Binary payload exceeds 255 byte limit (%zu)\n", payload_len);
        return APP_ERR_IO;
    }

    string_type = tifiles_string2vartype(session->calc_model, "String");
    if (string_type == 0u)
    {
        fprintf(stderr, "String vartype lookup failed for model %s\n", ticalcs_model_to_string(session->calc_model));
        return APP_ERR_IO;
    }

    if ((strncmp(var_name, "Str", 3) != 0) || (strlen(var_name) != 4u) || (!isdigit((unsigned char)var_name[3])))
    {
        fprintf(stderr, "Unsupported string variable name '%s'\n", var_name);
        return APP_ERR_IO;
    }

    tokenized_name = ticonv_varname_tokenize(session->calc_model, var_name, string_type);
    if (tokenized_name == NULL)
    {
        fprintf(stderr, "Failed to tokenize variable name %s\n", var_name);
        return APP_ERR_IO;
    }

    entry_size = payload_len + 1u;
    data = (uint8_t *)tifiles_ve_alloc_data(entry_size);
    if (data == NULL)
    {
        status = APP_ERR_ALLOC;
    }
    else
    {
        data[0] = (uint8_t)payload_len;
        if (payload_len > 0u)
        {
            memcpy(data + 1u, payload, payload_len);
        }

        content = tifiles_content_create_regular(session->calc_model);
        if (content == NULL)
        {
            status = APP_ERR_ALLOC;
        }
        else
        {
            entry = tifiles_ve_create();
            if (entry == NULL)
            {
                status = APP_ERR_ALLOC;
            }
            else
            {
                memset(entry, 0, sizeof(*entry));
                entry->type = string_type;
                entry->attr = ATTRB_NONE;
                entry->version = 0;
                entry->size = (uint32_t)entry_size;
                entry->data = data;
                entry->action = 0;

                name_len = strlen(tokenized_name);
                if (name_len > (sizeof(entry->name) - 1u))
                {
                    name_len = sizeof(entry->name) - 1u;
                }
                memcpy(entry->name, tokenized_name, name_len);
                entry->name[name_len] = '\0';

                content->model = session->calc_model;
                content->model_dst = session->calc_model;
                (void)snprintf(content->comment, sizeof(content->comment), "Pushed from c_wallet");

                content->entries = tifiles_ve_create_array(1);
                if (content->entries == NULL)
                {
                    status = APP_ERR_ALLOC;
                }
                else
                {
                    content->entries[0] = entry;
                    content->num_entries = 1;
                    *out_content = content;
                    status = APP_OK;
                }
            }
        }
    }

    if (status != APP_OK)
    {
        if (content != NULL)
        {
            tifiles_content_delete_regular(content);
        }
        else if (entry != NULL)
        {
            tifiles_ve_delete(entry);
        }
        else if (data != NULL)
        {
            tifiles_ve_free_data(data);
        }
    }

    if (tokenized_name != NULL)
    {
        ticonv_varname_free(tokenized_name);
    }

    return status;
}
