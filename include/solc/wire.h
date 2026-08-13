#ifndef SOLC_WIRE_H
#define SOLC_WIRE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_PUBKEY_BYTES 32u
#define SOLC_HASH_BYTES 32u
#define SOLC_SIGNATURE_BYTES 64u
#define SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES 1232u
#define SOLC_V1_MAX_TRANSACTION_BYTES 4096u
#define SOLC_V1_MAX_SIGNATURES 12u
#define SOLC_V1_MAX_ADDRESSES 64u
#define SOLC_V1_MAX_INSTRUCTIONS 64u
/* Sufficient scratch for every packet-valid legacy/v0/v1 transaction. */
#define SOLC_MAX_DECODE_INSTRUCTIONS 410u
#define SOLC_MAX_DECODE_LOOKUPS 36u

#define SOLC_V1_CONFIG_PRIORITY_FEE 0x00000003u
#define SOLC_V1_CONFIG_COMPUTE_UNIT_LIMIT 0x00000004u
#define SOLC_V1_CONFIG_LOADED_ACCOUNTS_DATA_SIZE_LIMIT 0x00000008u
#define SOLC_V1_CONFIG_HEAP_SIZE 0x00000010u
#define SOLC_V1_CONFIG_KNOWN_BITS 0x0000001fu

typedef enum solc_status {
    SOLC_OK = 0,
    SOLC_E_NULL = 1,
    SOLC_E_TRUNCATED = 2,
    SOLC_E_NON_CANONICAL = 3,
    SOLC_E_OVERFLOW = 4,
    SOLC_E_UNSUPPORTED_VERSION = 5,
    SOLC_E_TRAILING_BYTES = 6,
    SOLC_E_LIMIT_EXCEEDED = 7,
    SOLC_E_INVALID_HEADER = 8,
    SOLC_E_SIGNATURE_MISMATCH = 9,
    SOLC_E_INVALID_INDEX = 10,
    SOLC_E_SCRATCH_TOO_SMALL = 11,
    SOLC_E_OUTPUT_TOO_SMALL = 12,
    SOLC_E_INVALID_MODEL = 13,
    SOLC_E_INVALID_CONFIG = 14,
    SOLC_E_DUPLICATE_ADDRESS = 15,
    SOLC_E_INVALID_ENCODING = 16,
    SOLC_E_CRYPTO_UNAVAILABLE = 17,
    SOLC_E_SIGNATURE_INVALID = 18,
    SOLC_E_PROVIDER_FAILURE = 19,
    SOLC_E_INVALID_DUPLICATE = 20,
    SOLC_E_ACCOUNT_NOT_SIGNER = 21,
    SOLC_E_ACCOUNT_NOT_WRITABLE = 22,
    SOLC_E_ACCOUNT_OWNER_MISMATCH = 23,
    SOLC_E_ACCOUNT_KEY_MISMATCH = 24,
    SOLC_E_INVALID_SEEDS = 25,
    SOLC_E_CPI_PRIVILEGE_ESCALATION = 26,
    SOLC_E_INVALID_PROGRAM_DATA = 27,
    SOLC_E_INVALID_UTF8 = 28,
    SOLC_E_DUPLICATE_CONFIG = 29,
    SOLC_E_UNINITIALIZED_ACCOUNT = 30
} solc_status;

typedef enum solc_message_version {
    SOLC_MESSAGE_LEGACY = -1,
    SOLC_MESSAGE_V0 = 0,
    SOLC_MESSAGE_V1 = 1
} solc_message_version;

typedef struct solc_slice {
    const uint8_t *data;
    size_t len;
} solc_slice;

typedef struct solc_error {
    solc_status status;
    size_t offset;
} solc_error;

typedef struct solc_compiled_instruction {
    uint8_t program_id_index;
    solc_slice account_indices;
    solc_slice data;
} solc_compiled_instruction;

typedef struct solc_address_table_lookup {
    solc_slice account_key;
    solc_slice writable_indices;
    solc_slice readonly_indices;
} solc_address_table_lookup;

typedef struct solc_v1_config {
    uint32_t mask;
    uint64_t priority_fee;
    uint32_t compute_unit_limit;
    uint32_t loaded_accounts_data_size_limit;
    uint32_t heap_size;
} solc_v1_config;

typedef struct solc_message {
    int32_t version;
    uint8_t num_required_signatures;
    uint8_t num_readonly_signed_accounts;
    uint8_t num_readonly_unsigned_accounts;
    solc_slice static_account_keys;
    solc_slice lifetime_specifier;
    const solc_compiled_instruction *instructions;
    size_t instruction_count;
    const solc_address_table_lookup *address_table_lookups;
    size_t address_table_lookup_count;
    solc_v1_config v1_config;
} solc_message;

typedef struct solc_transaction {
    solc_slice signatures;
    solc_message message;
} solc_transaction;

/* Caller-owned storage used by the zero-allocation decoder. */
typedef struct solc_decode_scratch {
    solc_compiled_instruction *instructions;
    size_t instruction_capacity;
    solc_address_table_lookup *address_table_lookups;
    size_t address_table_lookup_capacity;
} solc_decode_scratch;

/* Strict one-to-one ShortU16. Aliases and overflow encodings are rejected. */
solc_status solc_short_u16_decode(const uint8_t *input,
                                  size_t input_len,
                                  uint16_t *value,
                                  size_t *consumed);
solc_status solc_short_u16_encode(uint16_t value,
                                  uint8_t output[3],
                                  size_t *written);

/*
 * Decode and sanitize one complete transaction. All slices in `out` borrow
 * from `input`; instruction/lookup arrays borrow from `scratch`.
 */
solc_status solc_transaction_decode(const uint8_t *input,
                                    size_t input_len,
                                    solc_decode_scratch *scratch,
                                    solc_transaction *out,
                                    solc_error *error);

/*
 * Validate and encode a transaction model. With output == NULL, returns the
 * exact required length in `output_len`. The model may be decoder output or a
 * caller-built model using the same structures.
 */
solc_status solc_transaction_encode(const solc_transaction *transaction,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_len,
                                    solc_error *error);

const char *solc_status_string(solc_status status);
const char *solc_version_string(solc_message_version version);

#ifdef __cplusplus
}
#endif

#endif
