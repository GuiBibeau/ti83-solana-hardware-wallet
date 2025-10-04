#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <time.h>
#include <termios.h>
#include <unistd.h>
#endif
#include "ticables.h"
#include "ticalcs.h"
#include "ticonv.h"
#include "tifiles.h"
#include "ed25519.h"
#include "calc_session.h"
#include "calc_string_store.h"
#include "wallet_crypto.h"
#include "solana_encoding.h"
#include "solana_client.h"

#define MENU_OPTION_EXIT 0
#define MENU_OPTION_CREATE 1
#define MENU_OPTION_LOAD 2
#define MENU_OPTION_AIRDROP 3
#define MENU_OPTION_BALANCE 4
#define MENU_OPTION_SEND 5

#define STRING_VAR_NAME_LENGTH 4
#define STRING_VAR_BUFFER_LENGTH 5

#define PASSWORD_BUFFER_LENGTH 256
#define SEED_LENGTH 32u
#define STORED_KEY_PAYLOAD_LEN (WALLET_PUBLIC_KEY_LEN + WALLET_BLOB_LEN)
#define SOLANA_LAMPORTS_PER_SOL 1000000000ULL
#define SOLANA_DEFAULT_RPC_URL "https://api.devnet.solana.com"
#define SOLANA_SIGNATURE_POLL_INTERVAL_MS 1000u
#define SOLANA_AIRDROP_TIMEOUT_SECONDS 30u
#define SOLANA_TRANSFER_TIMEOUT_SECONDS 60u
#define SOLANA_MAX_MESSAGE_LEN 512u
#define SOLANA_MAX_TRANSACTION_LEN 1024u
#define SOLANA_MAX_MEMO_LENGTH 120u
#define SOLANA_DEFAULT_MEMO "sent from my ti83+"

static const uint8_t SOLANA_SYSTEM_PROGRAM_ID[WALLET_PUBLIC_KEY_LEN] = {0};
static const char SOLANA_MEMO_PROGRAM_BASE58[] = "MemoSq4gqABAXKb96qnH8TysNcWxMyWCqXgDLGmfcHr";

static void print_menu(void)
{
    printf("\nCalculator Menu\n");
    printf(" %d) Create encrypted keypair\n", MENU_OPTION_CREATE);
    printf(" %d) Load encrypted keypair\n", MENU_OPTION_LOAD);
    printf(" %d) Request SOL airdrop\n", MENU_OPTION_AIRDROP);
    printf(" %d) Fetch balance\n", MENU_OPTION_BALANCE);
    printf(" %d) Send SOL transfer\n", MENU_OPTION_SEND);
    printf(" %d) Exit\n", MENU_OPTION_EXIT);
}

static int read_line(char *buffer, size_t size)
{
    int status = 0;

    if ((buffer != NULL) && (size > 0u))
    {
        if (fgets(buffer, (int)size, stdin) != NULL)
        {
            size_t len = strlen(buffer);
            if ((len > 0u) && (buffer[len - 1u] == '\n'))
            {
                buffer[len - 1u] = '\0';
            }
            status = 1;
        }
    }

    return status;
}

static int prompt_string_slot(char *out_name, size_t size)
{
    int status = 0;

    if ((out_name != NULL) && (size >= STRING_VAR_BUFFER_LENGTH))
    {
        char input_buffer[32];
        int continue_selection = 1;

        while (continue_selection == 1)
        {
            int index = 0;

            printf("\nSelect target string slot\n");
            for (index = 0; index <= 9; index++)
            {
                printf(" %2d) Str%d\n", index + 1, index);
            }
            printf("  0) Cancel\n");
            printf("Choice: ");

            if (read_line(input_buffer, sizeof(input_buffer)) == 0)
            {
                printf("Input unavailable, cancelling.\n");
                continue_selection = 0;
            }
            else
            {
                char *endptr = NULL;
                long choice = 0;

                errno = 0;
                choice = strtol(input_buffer, &endptr, 10);
                if ((errno != 0) || (endptr == input_buffer))
                {
                    printf("Invalid selection. Please enter a number.\n");
                }
                else if (choice == 0)
                {
                    printf("Operation cancelled.\n");
                    continue_selection = 0;
                }
                else if ((choice >= 1) && (choice <= 10))
                {
                    (void)snprintf(out_name, size, "Str%ld", choice - 1);
                    status = 1;
                    continue_selection = 0;
                }
                else
                {
                    printf("Unknown option. Please try again.\n");
                }
            }
        }
    }

    return status;
}

static int read_password_input(char *buffer, size_t size)
{
    size_t length = 0u;
    int ch = 0;

    if ((buffer == NULL) || (size == 0u))
    {
        return 0;
    }

#ifdef _WIN32
    while (1)
    {
        ch = _getch();
        if ((ch == '\r') || (ch == '\n'))
        {
            putchar('\n');
            break;
        }
        else if ((ch == '\b') || (ch == 127))
        {
            if (length > 0u)
            {
                length--;
                buffer[length] = '\0';
                fputs("\b \b", stdout);
            }
        }
        else if ((ch == 0) || (ch == 0xe0))
        {
            (void)_getch();
        }
        else if ((ch >= 32) && (ch <= 126))
        {
            if (length < (size - 1u))
            {
                buffer[length++] = (char)ch;
                putchar('*');
            }
        }
        else if (ch == 3)
        {
            return 0;
        }
    }
#else
    {
        struct termios oldt;
        struct termios newt;
        if (tcgetattr(STDIN_FILENO, &oldt) != 0)
        {
            return 0;
        }

        newt = oldt;
        newt.c_lflag &= ~(ECHO | ICANON);
        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0)
        {
            return 0;
        }

        while (1)
        {
            ch = getchar();
            if ((ch == '\r') || (ch == '\n'))
            {
                putchar('\n');
                break;
            }
            else if ((ch == 127) || (ch == '\b'))
            {
                if (length > 0u)
                {
                    length--;
                    buffer[length] = '\0';
                    fputs("\b \b", stdout);
                }
            }
            else if (ch == EOF)
            {
                length = 0u;
                break;
            }
            else if ((ch >= 32) && (ch <= 126))
            {
                if (length < (size - 1u))
                {
                    buffer[length++] = (char)ch;
                    putchar('*');
                }
            }
        }

        (void)tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
#endif

    buffer[length] = '\0';
    return (int)length;
}

static int prompt_password(char *buffer, size_t size, const char *prompt)
{
    int status = 0;

    if ((buffer != NULL) && (size > 0u))
    {
        int continue_prompt = 1;

        while (continue_prompt == 1)
        {
            printf("%s", prompt);
            fflush(stdout);

            if (read_password_input(buffer, size) == 0)
            {
                continue_prompt = 0;
            }
            else if (buffer[0] == '\0')
            {
                printf("Password cannot be empty.\n");
            }
            else
            {
                status = 1;
                continue_prompt = 0;
            }
        }
    }

    return status;
}

static int prompt_yes_no(const char *prompt)
{
    char input_buffer[8] = {0};
    int result = 0;

    if (prompt == NULL)
    {
        return 0;
    }

    printf("%s", prompt);
    if (read_line(input_buffer, sizeof(input_buffer)) != 0)
    {
        if (input_buffer[0] != '\0')
        {
            int response = tolower((unsigned char)input_buffer[0]);
            if ((response == 'y') || (response == '1'))
            {
                result = 1;
            }
        }
    }

    return result;
}

static void print_hex(const char *label, const uint8_t *data, size_t length)
{
    size_t index = 0;

    printf("%s", label);
    if (data != NULL)
    {
        for (index = 0; index < length; index++)
        {
            printf("%02x", data[index]);
        }
    }
    printf("\n");
}

static void print_base58(const char *label, const uint8_t *data, size_t length)
{
    char buffer[96];
    int encoded = solana_base58_encode(data, length, buffer, sizeof(buffer));

    if (encoded > 0)
    {
        printf("%s%s\n", label, buffer);
    }
    else
    {
        print_hex(label, data, length);
    }
}

static const char *solana_resolve_rpc_url(void)
{
    const char *rpc_url = getenv("SOLANA_RPC_URL");

    if ((rpc_url == NULL) || (rpc_url[0] == '\0'))
    {
        rpc_url = SOLANA_DEFAULT_RPC_URL;
    }

    if ((strncmp(rpc_url, "https://", 8) != 0))
    {
        fprintf(stderr, "Insecure SOLANA_RPC_URL detected. Falling back to default devnet endpoint.\n");
        rpc_url = SOLANA_DEFAULT_RPC_URL;
    }

    return rpc_url;
}

static int parse_json_string_field(const char *json, const char *field, char *out_value, size_t out_size)
{
    int status = 0;

    if ((json != NULL) && (field != NULL) && (out_value != NULL) && (out_size > 0u))
    {
        char pattern[64];
        size_t written = snprintf(pattern, sizeof(pattern), "\"%s\":\"", field);
        if ((written > 0u) && (written < sizeof(pattern)))
        {
            const char *start = strstr(json, pattern);
            if (start != NULL)
            {
                const char *value_start = start + written;
                const char *value_end = strchr(value_start, '\"');
                if ((value_end != NULL) && (value_end > value_start))
                {
                    size_t copy_len = (size_t)(value_end - value_start);
                    if (copy_len < out_size)
                    {
                        memcpy(out_value, value_start, copy_len);
                        out_value[copy_len] = '\0';
                        status = 1;
                    }
                }
            }
        }
    }

    return status;
}

static int parse_balance_response(const char *json, uint64_t *lamports)
{
    int status = 0;

    if ((json != NULL) && (lamports != NULL))
    {
        const char *value_ptr = strstr(json, "\"value\"");
        if (value_ptr != NULL)
        {
            value_ptr = strchr(value_ptr, ':');
            if (value_ptr != NULL)
            {
                char *endptr = NULL;
                unsigned long long parsed = 0ull;

                value_ptr++;
                while ((*value_ptr != '\0') && isspace((unsigned char)*value_ptr))
                {
                    value_ptr++;
                }

                errno = 0;
                parsed = strtoull(value_ptr, &endptr, 10);
                if ((errno == 0) && (endptr != value_ptr))
                {
                    *lamports = (uint64_t)parsed;
                    status = 1;
                }
            }
        }
    }

    return status;
}

static void sleep_milliseconds(unsigned int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(milliseconds / 1000u);
    ts.tv_nsec = (long)((milliseconds % 1000u) * 1000000L);
    (void)nanosleep(&ts, NULL);
#endif
}

static int is_signature_error(const char *json)
{
    const char *err_ptr = NULL;

    if (json == NULL)
    {
        return 0;
    }

    err_ptr = strstr(json, "\"err\":");
    if (err_ptr != NULL)
    {
        err_ptr += 6;
        while ((*err_ptr != '\0') && isspace((unsigned char)*err_ptr))
        {
            err_ptr++;
        }

        if (strncmp(err_ptr, "null", 4) != 0)
        {
            return 1;
        }
    }

    return 0;
}

static int is_signature_finalized(const char *json)
{
    if (json == NULL)
    {
        return 0;
    }

    if (strstr(json, "\"confirmationStatus\":\"finalized\"") != NULL)
    {
        return 1;
    }

    if (strstr(json, "\"confirmationStatus\":\"confirmed\"") != NULL)
    {
        return 1;
    }

    if ((strstr(json, "\"confirmationStatus\"") == NULL) &&
        (strstr(json, "\"confirmations\":null") != NULL) &&
        (strstr(json, "\"err\":null") != NULL))
    {
        return 1;
    }

    return 0;
}

static int wait_for_signature_confirmation(solana_client_t *client,
                                           const char *signature,
                                           unsigned int timeout_seconds)
{
    unsigned int max_attempts = 1u;

    if ((client == NULL) || (signature == NULL) || (signature[0] == '\0'))
    {
        return -1;
    }

    if (timeout_seconds > 0u)
    {
        unsigned int total_ms = timeout_seconds * 1000u;
        max_attempts = total_ms / SOLANA_SIGNATURE_POLL_INTERVAL_MS;
        if ((total_ms % SOLANA_SIGNATURE_POLL_INTERVAL_MS) != 0u)
        {
            max_attempts++;
        }
        if (max_attempts == 0u)
        {
            max_attempts = 1u;
        }
    }

    unsigned int attempt = 0u;

    for (attempt = 0u; attempt < max_attempts; attempt++)
    {
        char *status_response = NULL;
        int rpc_status = solana_client_get_signature_status(client, signature, &status_response);

        if ((rpc_status == SOLANA_OK) && (status_response != NULL))
        {
            if (is_signature_error(status_response) != 0)
            {
                solana_client_free_response(status_response);
                return -1;
            }

            if (is_signature_finalized(status_response) != 0)
            {
                solana_client_free_response(status_response);
                return 1;
            }
        }

        if (status_response != NULL)
        {
            solana_client_free_response(status_response);
        }

        if ((timeout_seconds == 0u) || (attempt + 1u >= max_attempts))
        {
            break;
        }

        sleep_milliseconds(SOLANA_SIGNATURE_POLL_INTERVAL_MS);
    }

    return 0;
}

static void print_solscan_link(const char *signature, const char *rpc_url)
{
    const char *cluster = NULL;

    if (signature == NULL)
    {
        return;
    }

    if ((rpc_url != NULL) && (strstr(rpc_url, "devnet") != NULL))
    {
        cluster = "devnet";
    }
    else if ((rpc_url != NULL) && (strstr(rpc_url, "testnet") != NULL))
    {
        cluster = "testnet";
    }
    else if ((rpc_url != NULL) && (strstr(rpc_url, "mainnet") != NULL))
    {
        cluster = NULL;
    }
    else if (rpc_url != NULL)
    {
        cluster = "custom";
    }
    else
    {
        cluster = "devnet";
    }

    printf("View on Solscan: https://solscan.io/tx/%s", signature);
    if (cluster != NULL)
    {
        printf("?cluster=%s", cluster);
    }
    printf("\n");
}

static int prompt_lamports(uint64_t *out_lamports)
{
    int status = 0;
    char input_buffer[64];
    int continue_prompt = 1;
    double parsed_double = 0.0;
    unsigned long long parsed_value = 0ull;
    char *endptr = NULL;
    char *input_ptr = NULL;
    size_t length = 0u;
    int treat_as_sol = 0;

    if (out_lamports != NULL)
    {
        while (continue_prompt == 1)
        {
            printf("Enter amount to request (lamports or SOL, e.g. 200000 or 0.0002): ");
            if (read_line(input_buffer, sizeof(input_buffer)) == 0)
            {
                continue_prompt = 0;
            }
            else
            {
                parsed_double = 0.0;
                parsed_value = 0ull;
                endptr = NULL;
                input_ptr = NULL;
                length = 0u;
                treat_as_sol = 0;

                input_ptr = input_buffer;

                while ((*input_ptr != '\0') && isspace((unsigned char)*input_ptr))
                {
                    input_ptr++;
                }

                length = strlen(input_ptr);
                while ((length > 0u) && isspace((unsigned char)input_ptr[length - 1u]))
                {
                    input_ptr[length - 1u] = '\0';
                    length--;
                }

                if (length == 0u)
                {
                    printf("Amount cannot be empty.\n");
                    continue;
                }

                if ((length >= 3u) &&
                    ((input_ptr[length - 1u] == 'l') || (input_ptr[length - 1u] == 'L')) &&
                    ((input_ptr[length - 2u] == 'o') || (input_ptr[length - 2u] == 'O')) &&
                    ((input_ptr[length - 3u] == 's') || (input_ptr[length - 3u] == 'S')))
                {
                    treat_as_sol = 1;
                    input_ptr[length - 3u] = '\0';
                    length -= 3u;
                    while ((length > 0u) && isspace((unsigned char)input_ptr[length - 1u]))
                    {
                        input_ptr[length - 1u] = '\0';
                        length--;
                    }
                }

                if (strpbrk(input_ptr, ".eE") != NULL)
                {
                    treat_as_sol = 1;
                }

                if (treat_as_sol != 0)
                {
                    errno = 0;
                    parsed_double = strtod(input_ptr, &endptr);
                    if ((errno != 0) || (endptr == input_ptr))
                    {
                        printf("Invalid SOL amount.\n");
                        continue;
                    }

                    if ((parsed_double <= 0.0) || (!isfinite(parsed_double)))
                    {
                        printf("Amount must be greater than zero.\n");
                        continue;
                    }

                    if (parsed_double > ((double)UINT64_MAX / (double)SOLANA_LAMPORTS_PER_SOL))
                    {
                        printf("Amount exceeds maximum supported size.\n");
                        continue;
                    }

                    parsed_value = (unsigned long long)(parsed_double * (double)SOLANA_LAMPORTS_PER_SOL + 0.5);
                    if (parsed_value == 0ull)
                    {
                        printf("Amount too small after conversion to lamports.\n");
                        continue;
                    }
                }
                else
                {
                    errno = 0;
                    parsed_value = strtoull(input_ptr, &endptr, 10);
                    if ((errno != 0) || (endptr == input_ptr) || (*endptr != '\0'))
                    {
                        printf("Invalid lamport amount.\n");
                        continue;
                    }

                    if (parsed_value == 0ull)
                    {
                        printf("Amount must be greater than zero.\n");
                        continue;
                    }
                }

                *out_lamports = (uint64_t)parsed_value;
                continue_prompt = 0;
                status = 1;
            }
        }
    }

    return status;
}

static int solana_append_u8(uint8_t *buffer, size_t buffer_size, size_t *offset, uint8_t value)
{
    if ((buffer == NULL) || (offset == NULL))
    {
        return 0;
    }

    if (*offset >= buffer_size)
    {
        return 0;
    }

    buffer[*offset] = value;
    (*offset)++;
    return 1;
}

static int solana_append_bytes(uint8_t *buffer, size_t buffer_size, size_t *offset, const uint8_t *data, size_t data_len)
{
    if ((buffer == NULL) || (offset == NULL) || (data == NULL))
    {
        return 0;
    }

    if ((buffer_size - *offset) < data_len)
    {
        return 0;
    }

    memcpy(buffer + *offset, data, data_len);
    *offset += data_len;
    return 1;
}

static int solana_append_shortvec(uint8_t *buffer, size_t buffer_size, size_t *offset, size_t value)
{
    size_t current_offset = 0u;

    if ((buffer == NULL) || (offset == NULL))
    {
        return 0;
    }

    current_offset = *offset;

    do
    {
        uint8_t byte = (uint8_t)(value & 0x7fu);
        value >>= 7;
        if (value != 0u)
        {
            byte |= 0x80u;
        }

        if (current_offset >= buffer_size)
        {
            return 0;
        }

        buffer[current_offset++] = byte;
    }
    while (value != 0u);

    *offset = current_offset;
    return 1;
}

static int prompt_base58_public_key(const char *prompt,
                                    char *out_base58,
                                    size_t base58_size,
                                    uint8_t *out_bytes,
                                    size_t bytes_len)
{
    int status = 0;

    if ((prompt == NULL) || (out_base58 == NULL) || (out_bytes == NULL) || (base58_size == 0u))
    {
        return 0;
    }

    while (status == 0)
    {
        char input_buffer[128];
        int decoded_length = 0;

        printf("%s", prompt);
        if (read_line(input_buffer, sizeof(input_buffer)) == 0)
        {
            break;
        }

        if (strlen(input_buffer) >= base58_size)
        {
            printf("Input too long.\n");
            continue;
        }

        decoded_length = solana_base58_decode(input_buffer, out_bytes, bytes_len);
        if ((decoded_length <= 0) || ((size_t)decoded_length != bytes_len))
        {
            printf("Invalid base58 public key.\n");
            continue;
        }

        (void)strncpy(out_base58, input_buffer, base58_size);
        out_base58[base58_size - 1u] = '\0';
        status = 1;
    }

    return status;
}

static int solana_build_transfer_transaction(const uint8_t *from_public_key,
                                             const uint8_t *to_public_key,
                                             uint64_t lamports,
                                             const uint8_t *recent_blockhash,
                                             const uint8_t *private_key,
                                             const char *memo,
                                             size_t memo_len,
                                             char *out_base64,
                                             size_t out_base64_size,
                                             char *out_signature_base58,
                                             size_t signature_base58_size)
{
    uint8_t message[SOLANA_MAX_MESSAGE_LEN];
    uint8_t transaction[SOLANA_MAX_TRANSACTION_LEN];
    uint8_t signature[64];
    uint8_t instruction_data[12];
    uint8_t memo_program_id[WALLET_PUBLIC_KEY_LEN];
    size_t message_len = 0u;
    size_t transaction_len = 0u;
    size_t account_key_count = 3u;
    size_t instruction_count = 1u;
    uint8_t readonly_unsigned = 1u;
    int include_memo = ((memo != NULL) && (memo_len > 0u));
    int encoded_signature_len = 0;
    int decoded = 0;
    uint8_t memo_program_index = 0u;
    int status = APP_ERR_IO;

    if ((from_public_key == NULL) || (to_public_key == NULL) || (recent_blockhash == NULL) ||
        (private_key == NULL) || (out_base64 == NULL))
    {
        return APP_ERR_IO;
    }

    if ((memo_len > SOLANA_MAX_MEMO_LENGTH) || ((memo_len > 0u) && (memo == NULL)))
    {
        return APP_ERR_IO;
    }

    memset(message, 0, sizeof(message));
    memset(transaction, 0, sizeof(transaction));
    memset(signature, 0, sizeof(signature));
    memset(instruction_data, 0, sizeof(instruction_data));
    memset(memo_program_id, 0, sizeof(memo_program_id));

    status = APP_OK;

    if ((status == APP_OK) && (include_memo != 0))
    {
        decoded = solana_base58_decode(SOLANA_MEMO_PROGRAM_BASE58, memo_program_id, sizeof(memo_program_id));
        if (decoded != (int)sizeof(memo_program_id))
        {
            status = APP_ERR_IO;
        }
        else
        {
            account_key_count++;
            instruction_count++;
            readonly_unsigned = 2u;
        }
    }

    if (status == APP_OK)
    {
        int append_status = 0;
        append_status = solana_append_u8(message, sizeof(message), &message_len, 1u);
        append_status &= solana_append_u8(message, sizeof(message), &message_len, 0u);
        append_status &= solana_append_u8(message, sizeof(message), &message_len, readonly_unsigned);
        append_status &= solana_append_shortvec(message, sizeof(message), &message_len, account_key_count);
        append_status &= solana_append_bytes(message, sizeof(message), &message_len, from_public_key, WALLET_PUBLIC_KEY_LEN);
        append_status &= solana_append_bytes(message, sizeof(message), &message_len, to_public_key, WALLET_PUBLIC_KEY_LEN);
        append_status &= solana_append_bytes(message, sizeof(message), &message_len, SOLANA_SYSTEM_PROGRAM_ID, WALLET_PUBLIC_KEY_LEN);
        if ((include_memo != 0) && (append_status != 0))
        {
            append_status &= solana_append_bytes(message, sizeof(message), &message_len, memo_program_id, sizeof(memo_program_id));
        }
        append_status &= solana_append_bytes(message, sizeof(message), &message_len, recent_blockhash, WALLET_PUBLIC_KEY_LEN);
        append_status &= solana_append_shortvec(message, sizeof(message), &message_len, instruction_count);

        if (append_status == 0)
        {
            status = APP_ERR_IO;
        }
    }

    if (status == APP_OK)
    {
        int header_status = 0;
        header_status = solana_append_u8(message, sizeof(message), &message_len, 2u);
        header_status &= solana_append_shortvec(message, sizeof(message), &message_len, 2u);
        header_status &= solana_append_u8(message, sizeof(message), &message_len, 0u);
        header_status &= solana_append_u8(message, sizeof(message), &message_len, 1u);
        if (header_status == 0)
        {
            status = APP_ERR_IO;
        }
    }

    if (status == APP_OK)
    {
        instruction_data[0] = 2u;
        instruction_data[1] = 0u;
        instruction_data[2] = 0u;
        instruction_data[3] = 0u;
        instruction_data[4] = (uint8_t)(lamports & 0xffu);
        instruction_data[5] = (uint8_t)((lamports >> 8u) & 0xffu);
        instruction_data[6] = (uint8_t)((lamports >> 16u) & 0xffu);
        instruction_data[7] = (uint8_t)((lamports >> 24u) & 0xffu);
        instruction_data[8] = (uint8_t)((lamports >> 32u) & 0xffu);
        instruction_data[9] = (uint8_t)((lamports >> 40u) & 0xffu);
        instruction_data[10] = (uint8_t)((lamports >> 48u) & 0xffu);
        instruction_data[11] = (uint8_t)((lamports >> 56u) & 0xffu);

        if ((solana_append_shortvec(message, sizeof(message), &message_len, sizeof(instruction_data)) == 0) ||
            (solana_append_bytes(message, sizeof(message), &message_len, instruction_data, sizeof(instruction_data)) == 0))
        {
            status = APP_ERR_IO;
        }
    }

    if ((status == APP_OK) && (include_memo != 0))
    {
        int memo_status = 0;
        memo_program_index = (uint8_t)(account_key_count - 1u);
        memo_status = solana_append_u8(message, sizeof(message), &message_len, memo_program_index);
        memo_status &= solana_append_shortvec(message, sizeof(message), &message_len, 0u);
        memo_status &= solana_append_shortvec(message, sizeof(message), &message_len, memo_len);
        memo_status &= solana_append_bytes(message, sizeof(message), &message_len, (const uint8_t *)memo, memo_len);
        if (memo_status == 0)
        {
            status = APP_ERR_IO;
        }
    }

    if (status == APP_OK)
    {
        ed25519_sign(signature, message, message_len, from_public_key, private_key);
        if ((solana_append_shortvec(transaction, sizeof(transaction), &transaction_len, 1u) == 0) ||
            (solana_append_bytes(transaction, sizeof(transaction), &transaction_len, signature, sizeof(signature)) == 0) ||
            (solana_append_bytes(transaction, sizeof(transaction), &transaction_len, message, message_len) == 0))
        {
            status = APP_ERR_IO;
        }
    }

    if (status == APP_OK)
    {
        if (solana_base64_encode(transaction, transaction_len, out_base64, out_base64_size) == 0u)
        {
            status = APP_ERR_IO;
        }
    }

    if ((status == APP_OK) && (out_signature_base58 != NULL) && (signature_base58_size > 0u))
    {
        encoded_signature_len = solana_base58_encode(signature, sizeof(signature), out_signature_base58, signature_base58_size);
        if (encoded_signature_len <= 0)
        {
            status = APP_ERR_IO;
        }
    }

    wallet_secure_zero(signature, sizeof(signature));
    wallet_secure_zero(message, sizeof(message));
    wallet_secure_zero(transaction, sizeof(transaction));
    wallet_secure_zero(instruction_data, sizeof(instruction_data));
    wallet_secure_zero(memo_program_id, sizeof(memo_program_id));

    return status;
}

static int fetch_wallet_payload(CalcSession *session,
                                char *var_buffer,
                                size_t var_buffer_len,
                                uint8_t *public_key,
                                uint8_t *blob,
                                size_t blob_len)
{
    int status = APP_ERR_IO;
    FileContent *content = NULL;
    VarEntry *entry = NULL;
    uint8_t payload_length = 0u;

    if ((session == NULL) || (var_buffer == NULL) || (public_key == NULL) || (blob == NULL))
    {
        return APP_ERR_IO;
    }

    if ((var_buffer_len < STRING_VAR_BUFFER_LENGTH) || (blob_len < WALLET_BLOB_LEN))
    {
        return APP_ERR_IO;
    }

    memset(var_buffer, 0, var_buffer_len);

    if (prompt_string_slot(var_buffer, var_buffer_len) == 0)
    {
        return APP_ERR_IO;
    }

    status = calc_fetch_string(session, var_buffer, &content);
    if (status != APP_OK)
    {
        fprintf(stderr, "Failed to fetch %s (error %d).\n", var_buffer, status);
    }

    if (status == APP_OK)
    {
        if ((content == NULL) || (content->num_entries == 0) || (content->entries[0] == NULL))
        {
            fprintf(stderr, "%s is empty or missing.\n", var_buffer);
            status = APP_ERR_IO;
        }
        else
        {
            entry = content->entries[0];
            if ((entry->data == NULL) || (entry->size < (STORED_KEY_PAYLOAD_LEN + 1u)))
            {
                fprintf(stderr, "%s does not contain an encrypted key.\n", var_buffer);
                status = APP_ERR_IO;
            }
            else
            {
                payload_length = entry->data[0];
                if ((payload_length != STORED_KEY_PAYLOAD_LEN) || (payload_length > (entry->size - 1u)))
                {
                    fprintf(stderr, "Stored data size is invalid (%u bytes).\n", payload_length);
                    status = APP_ERR_IO;
                }
                else
                {
                    memcpy(public_key, entry->data + 1u, WALLET_PUBLIC_KEY_LEN);
                    memcpy(blob, entry->data + 1u + WALLET_PUBLIC_KEY_LEN, WALLET_BLOB_LEN);
                    status = APP_OK;
                }
            }
        }
    }

    if ((status != APP_OK) || (entry == NULL))
    {
        wallet_secure_zero(public_key, WALLET_PUBLIC_KEY_LEN);
        wallet_secure_zero(blob, blob_len);
    }

    if ((entry != NULL) && (entry->data != NULL) && (entry->size > 0u))
    {
        wallet_secure_zero(entry->data, entry->size);
    }

    if (content != NULL)
    {
        tifiles_content_delete_regular(content);
    }

    return status;
}

static int create_encrypted_keypair(CalcSession *session)
{
    int status = APP_ERR_NO_CALC;
    char var_buffer[STRING_VAR_BUFFER_LENGTH] = {0};
    char password[PASSWORD_BUFFER_LENGTH] = {0};
    char password_confirm[PASSWORD_BUFFER_LENGTH] = {0};
    uint8_t seed[SEED_LENGTH];
    uint8_t private_key[WALLET_PRIVATE_KEY_LEN];
    uint8_t public_key[WALLET_PUBLIC_KEY_LEN];
    uint8_t blob[WALLET_BLOB_LEN];
    uint8_t storage_payload[STORED_KEY_PAYLOAD_LEN];

    if (session != NULL)
    {
        int flow_status = APP_OK;

        if (prompt_string_slot(var_buffer, sizeof(var_buffer)) == 0)
        {
            flow_status = APP_ERR_IO;
        }

        if ((flow_status == APP_OK) && (prompt_password(password, sizeof(password), "Enter password: ") == 0))
        {
            flow_status = APP_ERR_IO;
        }

        if ((flow_status == APP_OK) && (prompt_password(password_confirm, sizeof(password_confirm), "Confirm password: ") == 0))
        {
            flow_status = APP_ERR_IO;
        }

        if (flow_status == APP_OK)
        {
            if (strcmp(password, password_confirm) != 0)
            {
                printf("Passwords do not match.\n");
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            if (ed25519_create_seed(seed) != 0)
            {
                fprintf(stderr, "Failed to generate secure seed.\n");
                flow_status = APP_ERR_CRYPTO;
            }
            else
            {
                ed25519_create_keypair(public_key, private_key, seed);
                wallet_secure_zero(seed, sizeof(seed));

                flow_status = wallet_encrypt_private_key(password, private_key, sizeof(private_key), blob, sizeof(blob));
                if (flow_status != APP_OK)
                {
                    fprintf(stderr, "Failed to encrypt private key (error %d).\n", flow_status);
                }
            }
        }

        if (flow_status == APP_OK)
        {
            memcpy(storage_payload, public_key, sizeof(public_key));
            memcpy(storage_payload + sizeof(public_key), blob, sizeof(blob));

            flow_status = calc_store_binary_string(session, var_buffer, storage_payload, sizeof(storage_payload));
            if (flow_status != APP_OK)
            {
                fprintf(stderr, "Failed to store encrypted key (error %d).\n", flow_status);
            }
            else
            {
                printf("Encrypted keypair stored in %s.\n", var_buffer);
                print_base58("Public key (base58): ", public_key, sizeof(public_key));
            }
        }

        status = flow_status;
    }

    wallet_secure_zero(seed, sizeof(seed));
    wallet_secure_zero(password, sizeof(password));
    wallet_secure_zero(password_confirm, sizeof(password_confirm));
    wallet_secure_zero(private_key, sizeof(private_key));
    wallet_secure_zero(blob, sizeof(blob));
    wallet_secure_zero(storage_payload, sizeof(storage_payload));
    wallet_secure_zero(public_key, sizeof(public_key));

    return status;
}

static int load_encrypted_keypair(CalcSession *session)
{
    int status = APP_ERR_NO_CALC;
    char var_buffer[STRING_VAR_BUFFER_LENGTH] = {0};
    char password[PASSWORD_BUFFER_LENGTH] = {0};
    uint8_t blob[WALLET_BLOB_LEN];
    uint8_t private_key[WALLET_PRIVATE_KEY_LEN];
    uint8_t public_key[WALLET_PUBLIC_KEY_LEN];

    if (session != NULL)
    {
        int flow_status = fetch_wallet_payload(session, var_buffer, sizeof(var_buffer), public_key, blob, sizeof(blob));
        if (flow_status == APP_OK)
        {
            print_base58("Stored public key (base58): ", public_key, sizeof(public_key));

            if (prompt_yes_no("Decrypt private key for verification? (y/N): ") == 1)
            {
                if (prompt_password(password, sizeof(password), "Enter password: ") == 0)
                {
                    flow_status = APP_ERR_IO;
                }
                else
                {
                    flow_status = wallet_decrypt_private_key(password, blob, sizeof(blob), private_key, sizeof(private_key));
                    if (flow_status != APP_OK)
                    {
                        fprintf(stderr, "Unable to decrypt private key (error %d).\n", flow_status);
                    }
                    else
                    {
                        uint8_t derived_public_key[WALLET_PUBLIC_KEY_LEN];
                        ed25519_derive_public_key(derived_public_key, private_key);
                        if (memcmp(derived_public_key, public_key, sizeof(public_key)) != 0)
                        {
                            print_base58("Derived public key (base58 mismatch): ", derived_public_key, sizeof(derived_public_key));
                        }
                        wallet_secure_zero(derived_public_key, sizeof(derived_public_key));
                    }
                }
            }
        }

        status = flow_status;
    }

    wallet_secure_zero(blob, sizeof(blob));
    wallet_secure_zero(private_key, sizeof(private_key));
    wallet_secure_zero(public_key, sizeof(public_key));
    wallet_secure_zero(password, sizeof(password));
    wallet_secure_zero(var_buffer, sizeof(var_buffer));

    return status;
}

static int airdrop_to_public_key(CalcSession *session)
{
    int status = APP_ERR_NO_CALC;
    char var_buffer[STRING_VAR_BUFFER_LENGTH] = {0};
    uint8_t blob[WALLET_BLOB_LEN];
    uint8_t public_key[WALLET_PUBLIC_KEY_LEN];
    char public_key_base58[96];
    char signature[192];
    solana_client_t client;
    int client_initialized = 0;
    char *response = NULL;
    uint64_t lamports = 0u;
    const char *rpc_url = NULL;

    memset(&client, 0, sizeof(client));
    memset(blob, 0, sizeof(blob));
    memset(public_key, 0, sizeof(public_key));
    memset(public_key_base58, 0, sizeof(public_key_base58));
    memset(signature, 0, sizeof(signature));

    if (session != NULL)
    {
        int flow_status = fetch_wallet_payload(session, var_buffer, sizeof(var_buffer), public_key, blob, sizeof(blob));
        if (flow_status == APP_OK)
        {
            if (solana_base58_encode(public_key, sizeof(public_key), public_key_base58, sizeof(public_key_base58)) <= 0)
            {
                fprintf(stderr, "Failed to encode public key to base58.\n");
                flow_status = APP_ERR_CRYPTO;
            }
        }

        if (flow_status == APP_OK)
        {
            printf("Using wallet stored in %s.\n", var_buffer);
            printf("Public key: %s\n", public_key_base58);

            if (prompt_lamports(&lamports) == 0)
            {
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            rpc_url = solana_resolve_rpc_url();
            if (solana_client_init(&client, rpc_url) != SOLANA_OK)
            {
                fprintf(stderr, "Failed to initialize Solana client.\n");
                flow_status = APP_ERR_IO;
            }
            else
            {
                client_initialized = 1;
            }
        }

        if (flow_status == APP_OK)
        {
            if (solana_client_request_airdrop(&client, public_key_base58, lamports, &response) != SOLANA_OK)
            {
                fprintf(stderr, "requestAirdrop RPC call failed.\n");
                if (response != NULL)
                {
                    fprintf(stderr, "%s\n", response);
                }
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            double sol_amount = (double)lamports / (double)SOLANA_LAMPORTS_PER_SOL;
            printf("Requested %" PRIu64 " lamports (%.9f SOL).\n", lamports, sol_amount);

            if ((response == NULL) ||
                (parse_json_string_field(response, "result", signature, sizeof(signature)) == 0))
            {
                printf("requestAirdrop response: %s\n", (response != NULL) ? response : "(null)");
                flow_status = APP_ERR_IO;
            }
            else
            {
                printf("Transaction signature: %s\n", signature);
            }
        }

        if (flow_status == APP_OK)
        {
            if (response != NULL)
            {
                solana_client_free_response(response);
                response = NULL;
            }

            printf("Waiting for airdrop confirmation...\n");
            {
                int confirmation = wait_for_signature_confirmation(&client, signature, SOLANA_AIRDROP_TIMEOUT_SECONDS);
                if (confirmation == 1)
                {
                    printf("Airdrop confirmed.\n");
                    print_solscan_link(signature, rpc_url);
                }
                else if (confirmation < 0)
                {
                    fprintf(stderr, "Airdrop confirmation returned an error.\n");
                    flow_status = APP_ERR_IO;
                }
                else
                {
                    fprintf(stderr, "Timed out waiting for airdrop confirmation.\n");
                    flow_status = APP_ERR_IO;
                }
            }
        }

        status = flow_status;
    }

    if (response != NULL)
    {
        solana_client_free_response(response);
    }

    if (client_initialized == 1)
    {
        solana_client_cleanup(&client);
    }

    wallet_secure_zero(blob, sizeof(blob));
    wallet_secure_zero(public_key, sizeof(public_key));
    wallet_secure_zero(var_buffer, sizeof(var_buffer));

    memset(public_key_base58, 0, sizeof(public_key_base58));
    memset(signature, 0, sizeof(signature));

    return status;
}

static int show_public_key_balance(CalcSession *session)
{
    int status = APP_ERR_NO_CALC;
    char var_buffer[STRING_VAR_BUFFER_LENGTH] = {0};
    uint8_t blob[WALLET_BLOB_LEN];
    uint8_t public_key[WALLET_PUBLIC_KEY_LEN];
    char public_key_base58[96];
    solana_client_t client;
    int client_initialized = 0;
    char *response = NULL;

    memset(&client, 0, sizeof(client));
    memset(public_key_base58, 0, sizeof(public_key_base58));

    if (session != NULL)
    {
        int flow_status = fetch_wallet_payload(session, var_buffer, sizeof(var_buffer), public_key, blob, sizeof(blob));
        if (flow_status == APP_OK)
        {
            if (solana_base58_encode(public_key, sizeof(public_key), public_key_base58, sizeof(public_key_base58)) <= 0)
            {
                fprintf(stderr, "Failed to encode public key to base58.\n");
                flow_status = APP_ERR_CRYPTO;
            }
        }

        if (flow_status == APP_OK)
        {
            printf("Using wallet stored in %s.\n", var_buffer);
            printf("Public key: %s\n", public_key_base58);

            if (solana_client_init(&client, solana_resolve_rpc_url()) != SOLANA_OK)
            {
                fprintf(stderr, "Failed to initialize Solana client.\n");
                flow_status = APP_ERR_IO;
            }
            else
            {
                client_initialized = 1;
            }
        }

        if (flow_status == APP_OK)
        {
            if (solana_client_get_balance(&client, public_key_base58, &response) != SOLANA_OK)
            {
                fprintf(stderr, "getBalance RPC call failed.\n");
                if (response != NULL)
                {
                    fprintf(stderr, "%s\n", response);
                }
                flow_status = APP_ERR_IO;
            }
        }

        if ((flow_status == APP_OK) && (response != NULL))
        {
            uint64_t lamports = 0u;
            if (parse_balance_response(response, &lamports) == 1)
            {
                double sol_amount = (double)lamports / (double)SOLANA_LAMPORTS_PER_SOL;
                printf("Balance: %" PRIu64 " lamports (%.9f SOL).\n", lamports, sol_amount);
            }
            else
            {
                printf("getBalance response: %s\n", response);
            }
        }

        status = flow_status;
    }

    if (response != NULL)
    {
        solana_client_free_response(response);
    }

    if (client_initialized == 1)
    {
        solana_client_cleanup(&client);
    }

    wallet_secure_zero(blob, sizeof(blob));
    wallet_secure_zero(public_key, sizeof(public_key));
    wallet_secure_zero(var_buffer, sizeof(var_buffer));

    memset(public_key_base58, 0, sizeof(public_key_base58));

    return status;
}

static int send_sol_transaction(CalcSession *session)
{
    int status = APP_ERR_NO_CALC;
    char var_buffer[STRING_VAR_BUFFER_LENGTH] = {0};
    uint8_t blob[WALLET_BLOB_LEN];
    uint8_t public_key[WALLET_PUBLIC_KEY_LEN];
    uint8_t recipient_public_key[WALLET_PUBLIC_KEY_LEN];
    char public_key_base58[96];
    char recipient_base58[96];
    char password[PASSWORD_BUFFER_LENGTH];
    uint8_t private_key[WALLET_PRIVATE_KEY_LEN];
    char transaction_base64[512];
    char signature_base58[128];
    char blockhash_base58[96];
    uint8_t recent_blockhash[WALLET_PUBLIC_KEY_LEN];
    char memo_buffer[SOLANA_MAX_MEMO_LENGTH + 1u];
    solana_client_t client;
    int client_initialized = 0;
    char *blockhash_response = NULL;
    char *send_response = NULL;
    const char *rpc_url = NULL;
    uint64_t lamports = 0u;
    size_t memo_len = 0u;
    size_t default_memo_len = strlen(SOLANA_DEFAULT_MEMO);
    size_t memo_index = 0u;
    int ascii_ok = 1;

    memset(&client, 0, sizeof(client));
    memset(blob, 0, sizeof(blob));
    memset(public_key, 0, sizeof(public_key));
    memset(recipient_public_key, 0, sizeof(recipient_public_key));
    memset(public_key_base58, 0, sizeof(public_key_base58));
    memset(recipient_base58, 0, sizeof(recipient_base58));
    memset(password, 0, sizeof(password));
    memset(private_key, 0, sizeof(private_key));
    memset(transaction_base64, 0, sizeof(transaction_base64));
    memset(signature_base58, 0, sizeof(signature_base58));
    memset(blockhash_base58, 0, sizeof(blockhash_base58));
    memset(recent_blockhash, 0, sizeof(recent_blockhash));
    memset(memo_buffer, 0, sizeof(memo_buffer));
    if (default_memo_len < sizeof(memo_buffer))
    {
        memcpy(memo_buffer, SOLANA_DEFAULT_MEMO, default_memo_len);
        memo_len = default_memo_len;
    }

    if (session != NULL)
    {
        int flow_status = fetch_wallet_payload(session, var_buffer, sizeof(var_buffer), public_key, blob, sizeof(blob));
        if (flow_status == APP_OK)
        {
            if (solana_base58_encode(public_key, sizeof(public_key), public_key_base58, sizeof(public_key_base58)) <= 0)
            {
                fprintf(stderr, "Failed to encode wallet public key.\n");
                flow_status = APP_ERR_CRYPTO;
            }
        }

        if (flow_status == APP_OK)
        {
            printf("Using wallet stored in %s.\n", var_buffer);
            printf("Sender public key: %s\n", public_key_base58);

            if (prompt_lamports(&lamports) == 0)
            {
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            if (prompt_base58_public_key("Enter recipient public key: ",
                                         recipient_base58,
                                         sizeof(recipient_base58),
                                         recipient_public_key,
                                         sizeof(recipient_public_key)) == 0)
            {
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            char input_buffer[SOLANA_MAX_MEMO_LENGTH + 16u];
            int memo_valid = 0;

            printf("Default memo: '%s'\n", SOLANA_DEFAULT_MEMO);

            while ((memo_valid == 0) && (flow_status == APP_OK))
            {
                printf("Enter memo override (Leave blank to use default): ");
                if (read_line(input_buffer, sizeof(input_buffer)) == 0)
                {
                    memo_valid = 1;
                }
                else
                {
                    size_t input_len = strlen(input_buffer);
                    if (input_len == 0u)
                    {
                        memo_valid = 1;
                    }
                    else if (input_len > SOLANA_MAX_MEMO_LENGTH)
                    {
                        printf("Memo too long (max %u characters).\n", (unsigned)SOLANA_MAX_MEMO_LENGTH);
                    }
                    else
                    {
                        memo_index = 0u;
                        ascii_ok = 1;
                        for (memo_index = 0u; memo_index < input_len; memo_index++)
                        {
                            unsigned char c = (unsigned char)input_buffer[memo_index];
                            if ((c < 32u) || (c > 126u))
                            {
                                ascii_ok = 0;
                                break;
                            }
                        }

                        if (ascii_ok == 0)
                        {
                            printf("Memo must contain printable ASCII characters only.\n");
                        }
                        else
                        {
                            memcpy(memo_buffer, input_buffer, input_len);
                            memo_buffer[input_len] = '\0';
                            memo_len = input_len;
                            memo_valid = 1;
                        }
                    }
                }
            }

            if (memo_valid == 0)
            {
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            if (prompt_password(password, sizeof(password), "Enter password to decrypt wallet: ") == 0)
            {
                flow_status = APP_ERR_IO;
            }
            else
            {
                flow_status = wallet_decrypt_private_key(password, blob, sizeof(blob), private_key, sizeof(private_key));
                if (flow_status != APP_OK)
                {
                    fprintf(stderr, "Unable to decrypt private key (error %d).\n", flow_status);
                }
            }
        }

        wallet_secure_zero(password, sizeof(password));

        if (flow_status == APP_OK)
        {
            rpc_url = solana_resolve_rpc_url();
            if (solana_client_init(&client, rpc_url) != SOLANA_OK)
            {
                fprintf(stderr, "Failed to initialize Solana client.\n");
                flow_status = APP_ERR_IO;
            }
            else
            {
                client_initialized = 1;
            }
        }

        if (flow_status == APP_OK)
        {
            if (solana_client_get_latest_blockhash(&client, &blockhash_response) != SOLANA_OK)
            {
                fprintf(stderr, "getLatestBlockhash RPC call failed.\n");
                if (blockhash_response != NULL)
                {
                    fprintf(stderr, "%s\n", blockhash_response);
                }
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            if ((blockhash_response == NULL) ||
                (parse_json_string_field(blockhash_response, "blockhash", blockhash_base58, sizeof(blockhash_base58)) == 0) ||
                (solana_base58_decode(blockhash_base58, recent_blockhash, sizeof(recent_blockhash)) != (int)sizeof(recent_blockhash)))
            {
                fprintf(stderr, "Failed to parse recent blockhash.\n");
                flow_status = APP_ERR_IO;
            }
        }

        if (flow_status == APP_OK)
        {
            flow_status = solana_build_transfer_transaction(public_key,
                                                            recipient_public_key,
                                                            lamports,
                                                            recent_blockhash,
                                                            private_key,
                                                            (memo_len > 0u) ? memo_buffer : NULL,
                                                            memo_len,
                                                            transaction_base64,
                                                            sizeof(transaction_base64),
                                                            signature_base58,
                                                            sizeof(signature_base58));
            if (flow_status != APP_OK)
            {
                fprintf(stderr, "Failed to build transfer transaction.\n");
            }
        }

        wallet_secure_zero(private_key, sizeof(private_key));

        if (flow_status == APP_OK)
        {
            if (solana_client_send_transaction(&client, transaction_base64, &send_response) != SOLANA_OK)
            {
                fprintf(stderr, "sendTransaction RPC call failed.\n");
                if (send_response != NULL)
                {
                    fprintf(stderr, "%s\n", send_response);
                }
                flow_status = APP_ERR_IO;
            }
            else
            {
                wallet_secure_zero(transaction_base64, sizeof(transaction_base64));
                printf("Transaction submitted. Signature: %s\n", signature_base58);
                print_solscan_link(signature_base58, rpc_url);
                printf("Waiting for transfer confirmation...\n");

                {
                    int confirmation = wait_for_signature_confirmation(&client,
                                                                        signature_base58,
                                                                        SOLANA_TRANSFER_TIMEOUT_SECONDS);
                    if (confirmation == 1)
                    {
                        printf("Transfer confirmed.\n");
                    }
                    else if (confirmation < 0)
                    {
                        fprintf(stderr, "Transfer confirmation returned an error.\n");
                        flow_status = APP_ERR_IO;
                    }
                    else
                    {
                        fprintf(stderr, "Timed out waiting for transfer confirmation.\n");
                        flow_status = APP_ERR_IO;
                    }
                }
            }
        }

        status = flow_status;
    }

    if (send_response != NULL)
    {
        size_t response_len = strlen(send_response);
        if (response_len > 0u)
        {
            wallet_secure_zero(send_response, response_len);
        }
        solana_client_free_response(send_response);
    }

    if (blockhash_response != NULL)
    {
        size_t blockhash_len = strlen(blockhash_response);
        if (blockhash_len > 0u)
        {
            wallet_secure_zero(blockhash_response, blockhash_len);
        }
        solana_client_free_response(blockhash_response);
    }

    if (client_initialized == 1)
    {
        solana_client_cleanup(&client);
    }

    wallet_secure_zero(password, sizeof(password));
    wallet_secure_zero(private_key, sizeof(private_key));
    wallet_secure_zero(blob, sizeof(blob));
    wallet_secure_zero(public_key, sizeof(public_key));
    wallet_secure_zero(recipient_public_key, sizeof(recipient_public_key));
    wallet_secure_zero(recent_blockhash, sizeof(recent_blockhash));
    wallet_secure_zero(var_buffer, sizeof(var_buffer));
    wallet_secure_zero(transaction_base64, sizeof(transaction_base64));
    wallet_secure_zero(memo_buffer, sizeof(memo_buffer));

    memset(public_key_base58, 0, sizeof(public_key_base58));
    memset(recipient_base58, 0, sizeof(recipient_base58));
    memset(signature_base58, 0, sizeof(signature_base58));
    memset(blockhash_base58, 0, sizeof(blockhash_base58));

    return status;
}

int main(void)
{
    int err = APP_OK;
    CalcSession session;
    memset(&session, 0, sizeof(session));
    session.port_number = PORT_1;

    ticables_library_init();
    tifiles_library_init();
    ticalcs_library_init();

    err = calc_session_open(&session);
    if (err == APP_OK)
    {
        err = calc_session_start_polling(&session, 1000);
        if (err == APP_OK)
        {
            if (ticalcs_calc_isready(session.calc) == 0)
            {
                char input_buffer[32];
                int exit_menu = 0;
                printf("Calculator responded to RDY ping\n");
                calc_session_stop_polling(&session);

                while (exit_menu == 0)
                {
                    char *endptr = NULL;
                    long choice = 0;

                    print_menu();
                    printf("Select an option: ");

                    if (read_line(input_buffer, sizeof(input_buffer)) == 0)
                    {
                        printf("Input unavailable, exiting.\n");
                        exit_menu = 1;
                    }
                    else
                    {
                        errno = 0;
                        choice = strtol(input_buffer, &endptr, 10);
                        if ((errno != 0) || (endptr == input_buffer))
                        {
                            printf("Invalid selection. Please enter a number.\n");
                        }
                        else
                        {
                            switch (choice)
                            {
                                case MENU_OPTION_CREATE:
                                {
                                    int create_status = create_encrypted_keypair(&session);
                                    if (create_status != APP_OK)
                                    {
                                        fprintf(stderr, "Keypair creation failed (error %d).\n", create_status);
                                    }
                                    break;
                                }
                                case MENU_OPTION_LOAD:
                                {
                                    int load_status = load_encrypted_keypair(&session);
                                    if (load_status != APP_OK)
                                    {
                                        fprintf(stderr, "Keypair load failed (error %d).\n", load_status);
                                    }
                                    break;
                                }
                                case MENU_OPTION_AIRDROP:
                                {
                                    int airdrop_status = airdrop_to_public_key(&session);
                                    if (airdrop_status != APP_OK)
                                    {
                                        fprintf(stderr, "Airdrop request failed (error %d).\n", airdrop_status);
                                    }
                                    break;
                                }
                                case MENU_OPTION_BALANCE:
                                {
                                    int balance_status = show_public_key_balance(&session);
                                    if (balance_status != APP_OK)
                                    {
                                        fprintf(stderr, "Balance fetch failed (error %d).\n", balance_status);
                                    }
                                    break;
                                }
                                case MENU_OPTION_SEND:
                                {
                                    int send_status = send_sol_transaction(&session);
                                    if (send_status != APP_OK)
                                    {
                                        fprintf(stderr, "Send transaction failed (error %d).\n", send_status);
                                    }
                                    break;
                                }
                                case MENU_OPTION_EXIT:
                                {
                                    printf("Exiting menu.\n");
                                    exit_menu = 1;
                                    break;
                                }
                                default:
                                {
                                    printf("Unknown option. Please try again.\n");
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                fprintf(stderr, "Calculator did not respond to RDY ping\n");
                err = APP_ERR_NOT_READY;
            }
        }
    }
    
    calc_session_stop_polling(&session);
    calc_session_cleanup(&session);
    ticalcs_library_exit();
    tifiles_library_exit();
    ticables_library_exit();

    return err;
}
