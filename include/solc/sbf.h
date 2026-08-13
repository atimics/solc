#ifndef SOLC_SBF_H
#define SOLC_SBF_H

#include <stddef.h>
#include <stdint.h>

#include "solc/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_SBF_MAX_PERMITTED_DATA_INCREASE 10240u
#define SOLC_PDA_MAX_SEEDS 16u
#define SOLC_PDA_MAX_SEED_BYTES 32u
#define SOLC_CPI_MAX_INSTRUCTION_DATA_BYTES 10240u
#define SOLC_CPI_MAX_INSTRUCTION_ACCOUNTS 255u
#define SOLC_CPI_MAX_ACCOUNT_INFOS 128u

/* The loader entrypoint supplies a trusted pointer but no byte extent. */
#define SOLC_SBF_LOADER_TRUSTED_INPUT_LEN SIZE_MAX

typedef struct solc_sbf_account {
    const uint8_t *key;
    uint8_t *owner;
    uint8_t *lamports_le;
    uint8_t *data_len_le;
    uint8_t *data;
    size_t data_len;
    size_t original_data_len;
    uint64_t rent_epoch;
    uint8_t is_signer;
    uint8_t is_writable;
    uint8_t executable;
    uint8_t duplicate_of;
} solc_sbf_account;

typedef struct solc_sbf_parameters {
    solc_sbf_account *accounts;
    size_t account_count;
    solc_slice instruction_data;
    const uint8_t *program_id;
} solc_sbf_parameters;

/*
 * Decode the SBF loader entrypoint buffer byte-by-byte. A real input length
 * gives fail-closed host/fuzz behavior. SIZE_MAX selects the loader-trusted
 * on-chain mode required by the pointer-only entrypoint ABI.
 */
solc_status solc_sbf_parameters_decode(uint8_t *input,
                                       size_t input_len,
                                       solc_sbf_account *account_scratch,
                                       size_t account_capacity,
                                       solc_sbf_parameters *out,
                                       solc_error *error);

int solc_pubkey_equal(const uint8_t left[SOLC_PUBKEY_BYTES],
                      const uint8_t right[SOLC_PUBKEY_BYTES]);
solc_status solc_sbf_account_require_key(
    const solc_sbf_account *account,
    const uint8_t expected[SOLC_PUBKEY_BYTES]);
solc_status solc_sbf_account_require_owner(
    const solc_sbf_account *account,
    const uint8_t expected[SOLC_PUBKEY_BYTES]);
solc_status solc_sbf_account_require_signer(const solc_sbf_account *account);
solc_status solc_sbf_account_require_writable(const solc_sbf_account *account);
solc_status solc_sbf_account_require_data_len(const solc_sbf_account *account,
                                              size_t minimum);
uint64_t solc_sbf_account_lamports(const solc_sbf_account *account);
solc_status solc_sbf_account_set_lamports(solc_sbf_account *account,
                                          uint64_t lamports);
solc_status solc_sbf_account_set_data_len(solc_sbf_account *account,
                                          size_t data_len);
solc_status solc_sbf_account_data_mut(solc_sbf_account *account,
                                      size_t minimum,
                                      uint8_t **data);

typedef struct solc_seed {
    const uint8_t *data;
    size_t len;
} solc_seed;

typedef struct solc_signer_seeds {
    const solc_seed *seeds;
    size_t seed_count;
} solc_signer_seeds;

solc_status solc_pda_seeds_validate(const solc_seed *seeds, size_t seed_count);
solc_status solc_pda_signers_validate(const solc_signer_seeds *signers,
                                      size_t signer_count);

typedef struct solc_cpi_account_meta {
    const uint8_t *pubkey;
    uint8_t is_writable;
    uint8_t is_signer;
} solc_cpi_account_meta;

typedef struct solc_cpi_instruction {
    const uint8_t *program_id;
    const solc_cpi_account_meta *accounts;
    size_t account_count;
    solc_slice data;
} solc_cpi_instruction;

typedef struct solc_cpi_builder {
    const solc_sbf_account *program_account;
    solc_cpi_account_meta *metas;
    size_t meta_count;
    size_t meta_capacity;
    const solc_sbf_account **account_infos;
    size_t account_info_count;
    size_t account_info_capacity;
    solc_slice data;
    uint8_t finalized;
} solc_cpi_builder;

solc_status solc_cpi_builder_init(solc_cpi_builder *builder,
                                  const solc_sbf_account *program_account,
                                  solc_slice data,
                                  solc_cpi_account_meta *meta_scratch,
                                  size_t meta_capacity,
                                  const solc_sbf_account **account_info_scratch,
                                  size_t account_info_capacity);
solc_status solc_cpi_builder_add_account(solc_cpi_builder *builder,
                                         const solc_sbf_account *account,
                                         int request_writable,
                                         int request_signer,
                                         int signer_via_pda);
solc_status solc_cpi_builder_finish(solc_cpi_builder *builder,
                                    solc_cpi_instruction *instruction,
                                    const solc_sbf_account ***account_infos,
                                    size_t *account_info_count);

#ifdef __cplusplus
}
#endif

#endif
