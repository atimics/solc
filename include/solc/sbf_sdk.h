#ifndef SOLC_SBF_SDK_H
#define SOLC_SBF_SDK_H

/* This adapter is compiled only for the official C SBF SDK target. */
#include <sol/cpi.h>

#include "solc/sbf.h"

static inline void solc_sbf_sdk_account_info(const solc_sbf_account *source,
                                             SolAccountInfo *destination) {
    destination->key = (SolPubkey *)(uintptr_t)source->key;
    destination->lamports = (uint64_t *)(void *)source->lamports_le;
    destination->data_len = (uint64_t)source->data_len;
    destination->data = source->data;
    destination->owner = (SolPubkey *)(void *)source->owner;
    destination->rent_epoch = source->rent_epoch;
    destination->is_signer = source->is_signer != 0u;
    destination->is_writable = source->is_writable != 0u;
    destination->executable = source->executable != 0u;
}

/*
 * Convert a finalized, checked host-neutral CPI model and invoke the official
 * C syscall. All scratch arrays are caller-owned SBF stack/arena storage.
 */
static inline uint64_t solc_sbf_sdk_invoke_checked(
    const solc_cpi_instruction *instruction,
    const solc_sbf_account *const *account_infos,
    size_t account_info_count,
    const solc_signer_seeds *signers,
    size_t signer_count,
    SolAccountMeta *sdk_meta_scratch,
    size_t sdk_meta_capacity,
    SolAccountInfo *sdk_info_scratch,
    size_t sdk_info_capacity,
    SolSignerSeed *sdk_seed_scratch,
    size_t sdk_seed_capacity,
    SolSignerSeeds *sdk_signer_scratch,
    size_t sdk_signer_capacity) {
    SolInstruction sdk_instruction;
    size_t seed_pos = 0u;
    size_t i;
    solc_status status;

    if (instruction == NULL ||
        (instruction->account_count != 0u && sdk_meta_scratch == NULL) ||
        (account_info_count != 0u &&
         (account_infos == NULL || sdk_info_scratch == NULL)) ||
        (signer_count != 0u &&
         (signers == NULL || sdk_signer_scratch == NULL))) {
        return (uint64_t)SOLC_E_NULL;
    }
    if (instruction->account_count > sdk_meta_capacity ||
        account_info_count > sdk_info_capacity || signer_count > sdk_signer_capacity ||
        instruction->account_count > SOLC_CPI_MAX_INSTRUCTION_ACCOUNTS ||
        account_info_count > SOLC_CPI_MAX_ACCOUNT_INFOS ||
        account_info_count > (size_t)INT32_MAX || signer_count > (size_t)INT32_MAX) {
        return (uint64_t)SOLC_E_LIMIT_EXCEEDED;
    }
    status = solc_pda_signers_validate(signers, signer_count);
    if (status != SOLC_OK) {
        return (uint64_t)status;
    }
    for (i = 0u; i < instruction->account_count; ++i) {
        sdk_meta_scratch[i].pubkey =
            (SolPubkey *)(uintptr_t)instruction->accounts[i].pubkey;
        sdk_meta_scratch[i].is_writable = instruction->accounts[i].is_writable != 0u;
        sdk_meta_scratch[i].is_signer = instruction->accounts[i].is_signer != 0u;
    }
    for (i = 0u; i < account_info_count; ++i) {
        solc_sbf_sdk_account_info(account_infos[i], &sdk_info_scratch[i]);
    }
    for (i = 0u; i < signer_count; ++i) {
        size_t j;
        if (signers[i].seed_count > sdk_seed_capacity - seed_pos) {
            return (uint64_t)SOLC_E_SCRATCH_TOO_SMALL;
        }
        sdk_signer_scratch[i].addr = &sdk_seed_scratch[seed_pos];
        sdk_signer_scratch[i].len = (uint64_t)signers[i].seed_count;
        for (j = 0u; j < signers[i].seed_count; ++j) {
            sdk_seed_scratch[seed_pos].addr = signers[i].seeds[j].data;
            sdk_seed_scratch[seed_pos].len = (uint64_t)signers[i].seeds[j].len;
            ++seed_pos;
        }
    }
    sdk_instruction.program_id = (SolPubkey *)(uintptr_t)instruction->program_id;
    sdk_instruction.accounts = sdk_meta_scratch;
    sdk_instruction.account_len = (uint64_t)instruction->account_count;
    sdk_instruction.data = (uint8_t *)(uintptr_t)instruction->data.data;
    sdk_instruction.data_len = (uint64_t)instruction->data.len;
    return sol_invoke_signed(&sdk_instruction,
                             sdk_info_scratch,
                             (int)account_info_count,
                             sdk_signer_scratch,
                             (int)signer_count);
}

#endif
