#ifndef SOLC_PROGRAMS_H
#define SOLC_PROGRAMS_H

#include "solc/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_SYSTEM_MAX_SEED_BYTES 32u
#define SOLC_TOKEN_MINT_BYTES 82u
#define SOLC_TOKEN_ACCOUNT_BYTES 165u
#define SOLC_TOKEN_MULTISIG_BYTES 355u
#define SOLC_TOKEN_MAX_SIGNERS 11u
#define SOLC_ALT_META_BYTES 56u
#define SOLC_ALT_MAX_ADDRESSES 256u
#define SOLC_TOKEN2022_MAX_EXTENSION_TYPE 28u
#define SOLC_COMPUTE_BUDGET_MIN_HEAP_BYTES (32u * 1024u)
#define SOLC_COMPUTE_BUDGET_MAX_HEAP_BYTES (256u * 1024u)
#define SOLC_MICRO_LAMPORTS_PER_LAMPORT 1000000u

typedef struct solc_optional_pubkey {
    uint8_t present;
    const uint8_t *key;
} solc_optional_pubkey;

typedef struct solc_optional_u64 {
    uint8_t present;
    uint64_t value;
} solc_optional_u64;

typedef enum solc_system_instruction_kind {
    SOLC_SYSTEM_CREATE_ACCOUNT = 0,
    SOLC_SYSTEM_ASSIGN = 1,
    SOLC_SYSTEM_TRANSFER = 2,
    SOLC_SYSTEM_CREATE_ACCOUNT_WITH_SEED = 3,
    SOLC_SYSTEM_ADVANCE_NONCE_ACCOUNT = 4,
    SOLC_SYSTEM_WITHDRAW_NONCE_ACCOUNT = 5,
    SOLC_SYSTEM_INITIALIZE_NONCE_ACCOUNT = 6,
    SOLC_SYSTEM_AUTHORIZE_NONCE_ACCOUNT = 7,
    SOLC_SYSTEM_ALLOCATE = 8,
    SOLC_SYSTEM_ALLOCATE_WITH_SEED = 9,
    SOLC_SYSTEM_ASSIGN_WITH_SEED = 10,
    SOLC_SYSTEM_TRANSFER_WITH_SEED = 11,
    SOLC_SYSTEM_UPGRADE_NONCE_ACCOUNT = 12
} solc_system_instruction_kind;

/* Flat zero-allocation representation; unused fields are zero/null. */
typedef struct solc_system_instruction {
    uint32_t kind;
    uint64_t lamports;
    uint64_t space;
    const uint8_t *base;
    const uint8_t *owner;
    const uint8_t *authority;
    solc_slice seed;
} solc_system_instruction;

solc_status solc_system_instruction_decode(const uint8_t *input,
                                           size_t input_len,
                                           solc_system_instruction *out,
                                           solc_error *error);
solc_status solc_system_instruction_encode(const solc_system_instruction *instruction,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_len,
                                           solc_error *error);
const char *solc_system_instruction_name(uint32_t kind);

typedef enum solc_compute_budget_kind {
    SOLC_COMPUTE_BUDGET_UNUSED = 0,
    SOLC_COMPUTE_BUDGET_REQUEST_HEAP_FRAME = 1,
    SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_LIMIT = 2,
    SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_PRICE = 3,
    SOLC_COMPUTE_BUDGET_SET_LOADED_ACCOUNTS_DATA_SIZE_LIMIT = 4
} solc_compute_budget_kind;

typedef struct solc_compute_budget_instruction {
    uint8_t kind;
    uint64_t value;
} solc_compute_budget_instruction;

typedef struct solc_compute_budget_limits {
    uint32_t mask;
    uint64_t compute_unit_price_micro_lamports;
    uint32_t compute_unit_limit;
    uint32_t loaded_accounts_data_size_limit;
    uint32_t heap_size;
} solc_compute_budget_limits;

#define SOLC_COMPUTE_BUDGET_HAS_PRICE 0x01u
#define SOLC_COMPUTE_BUDGET_HAS_COMPUTE_UNIT_LIMIT 0x02u
#define SOLC_COMPUTE_BUDGET_HAS_LOADED_DATA_LIMIT 0x04u
#define SOLC_COMPUTE_BUDGET_HAS_HEAP_SIZE 0x08u

solc_status solc_compute_budget_instruction_decode(
    const uint8_t *input,
    size_t input_len,
    solc_compute_budget_instruction *out,
    solc_error *error);
solc_status solc_compute_budget_instruction_encode(
    const solc_compute_budget_instruction *instruction,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_len,
    solc_error *error);
solc_status solc_compute_budget_collect(
    const solc_compute_budget_instruction *instructions,
    size_t instruction_count,
    solc_compute_budget_limits *out);
/* Converts legacy CU-price semantics to v1's total lamport priority fee. */
solc_status solc_compute_budget_limits_to_v1(const solc_compute_budget_limits *limits,
                                             solc_v1_config *out);
const char *solc_compute_budget_instruction_name(uint8_t kind);

typedef enum solc_token_program_flavor {
    SOLC_TOKEN_CLASSIC = 0,
    SOLC_TOKEN_2022 = 1
} solc_token_program_flavor;

typedef struct solc_token_instruction {
    uint8_t kind;
    uint8_t decimals;
    uint8_t m;
    uint8_t authority_type;
    uint64_t amount;
    const uint8_t *pubkey;
    solc_optional_pubkey optional_pubkey;
    /* Raw u16-LE list for tags 21 and 29. */
    solc_slice extension_types;
    /* UTF-8 bytes for tag 24; extension subinstruction bytes otherwise. */
    solc_slice data;
} solc_token_instruction;

solc_status solc_token_instruction_decode(solc_token_program_flavor flavor,
                                          const uint8_t *input,
                                          size_t input_len,
                                          solc_token_instruction *out,
                                          solc_error *error);
solc_status solc_token_instruction_encode(solc_token_program_flavor flavor,
                                          const solc_token_instruction *instruction,
                                          uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_len,
                                          solc_error *error);
const char *solc_token_instruction_name(solc_token_program_flavor flavor, uint8_t kind);

typedef struct solc_token_mint {
    solc_optional_pubkey mint_authority;
    uint64_t supply;
    uint8_t decimals;
    uint8_t is_initialized;
    solc_optional_pubkey freeze_authority;
} solc_token_mint;

typedef struct solc_token_account {
    const uint8_t *mint;
    const uint8_t *owner;
    uint64_t amount;
    solc_optional_pubkey delegate;
    uint8_t state;
    solc_optional_u64 is_native;
    uint64_t delegated_amount;
    solc_optional_pubkey close_authority;
} solc_token_account;

typedef struct solc_token_multisig {
    uint8_t m;
    uint8_t n;
    uint8_t is_initialized;
    solc_slice signers;
} solc_token_multisig;

solc_status solc_token_mint_decode(const uint8_t *input,
                                   size_t input_len,
                                   solc_token_mint *out);
solc_status solc_token_account_decode(const uint8_t *input,
                                      size_t input_len,
                                      solc_token_account *out);
solc_status solc_token_multisig_decode(const uint8_t *input,
                                       size_t input_len,
                                       solc_token_multisig *out);

typedef enum solc_token2022_account_kind {
    SOLC_TOKEN2022_MINT = 1,
    SOLC_TOKEN2022_ACCOUNT = 2
} solc_token2022_account_kind;

typedef struct solc_token2022_state {
    uint8_t account_kind;
    solc_slice base;
    solc_slice tlv;
    size_t tlv_used_len;
    size_t extension_count;
} solc_token2022_state;

typedef struct solc_token2022_tlv_entry {
    uint16_t extension_type;
    solc_slice value;
} solc_token2022_tlv_entry;

typedef struct solc_token2022_tlv_iterator {
    solc_slice tlv;
    size_t offset;
    uint8_t finished;
} solc_token2022_tlv_iterator;

solc_status solc_token2022_state_decode(uint8_t expected_account_kind,
                                        const uint8_t *input,
                                        size_t input_len,
                                        solc_token2022_state *out,
                                        solc_error *error);
void solc_token2022_tlv_iterator_init(const solc_token2022_state *state,
                                      solc_token2022_tlv_iterator *iterator);
solc_status solc_token2022_tlv_next(solc_token2022_tlv_iterator *iterator,
                                    solc_token2022_tlv_entry *entry,
                                    uint8_t *has_entry);

typedef struct solc_address_lookup_table {
    uint64_t deactivation_slot;
    uint64_t last_extended_slot;
    uint8_t last_extended_slot_start_index;
    solc_optional_pubkey authority;
    solc_slice addresses;
    size_t address_count;
} solc_address_lookup_table;

solc_status solc_address_lookup_table_decode(const uint8_t *input,
                                             size_t input_len,
                                             solc_address_lookup_table *out,
                                             solc_error *error);
/* Applies the same-slot extension visibility rule; deactivation needs SlotHashes. */
size_t solc_address_lookup_table_visible_len(const solc_address_lookup_table *table,
                                             uint64_t current_slot);
solc_status solc_address_lookup_table_address(const solc_address_lookup_table *table,
                                              uint8_t index,
                                              const uint8_t **address);

#ifdef __cplusplus
}
#endif

#endif
