#ifndef SOLC_BUILDER_H
#define SOLC_BUILDER_H

#include "solc/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_BUILDER_MAX_ACCOUNTS 256u

typedef struct solc_transaction_account_meta {
    const uint8_t *pubkey;
    uint8_t is_signer;
    uint8_t is_writable;
} solc_transaction_account_meta;

typedef struct solc_transaction_instruction {
    const uint8_t *program_id;
    const solc_transaction_account_meta *accounts;
    size_t account_count;
    solc_slice data;
} solc_transaction_instruction;

/*
 * Caller-owned storage for compiling ergonomic account metadata to the wire
 * model. Account capacities are measured in accounts; account_index_capacity
 * is measured in bytes and must cover the sum of instruction account counts.
 */
typedef struct solc_legacy_builder_scratch {
    uint8_t *discovered_account_keys;
    uint8_t *ordered_account_keys;
    uint8_t *account_flags;
    uint8_t *old_to_new;
    size_t account_capacity;
    solc_compiled_instruction *compiled_instructions;
    size_t instruction_capacity;
    uint8_t *account_indices;
    size_t account_index_capacity;
} solc_legacy_builder_scratch;

/*
 * Build a legacy transaction model using Solana's four stable account groups:
 * writable signers, readonly signers, writable non-signers, then readonly
 * non-signers. Discovery order is retained within each group. The fee payer is
 * inserted first and forced to be a writable signer. Duplicate accounts merge
 * privileges. `out` borrows all inputs and scratch storage.
 */
solc_status solc_legacy_transaction_build(
    const uint8_t fee_payer[SOLC_PUBKEY_BYTES],
    const uint8_t recent_blockhash[SOLC_HASH_BYTES],
    solc_slice signatures,
    const solc_transaction_instruction *instructions,
    size_t instruction_count,
    solc_legacy_builder_scratch *scratch,
    solc_transaction *out,
    solc_error *error);

#ifdef __cplusplus
}
#endif

#endif
