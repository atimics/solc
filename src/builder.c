#include "solc/builder.h"

#include <string.h>

#define ACCOUNT_SIGNER 0x01u
#define ACCOUNT_WRITABLE 0x02u

static solc_status fail(solc_error *error, solc_status status, size_t offset) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
    }
    return status;
}

static void succeed(solc_error *error) {
    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = 0u;
    }
}

static int same_key(const uint8_t *left, const uint8_t *right) {
    return memcmp(left, right, SOLC_PUBKEY_BYTES) == 0;
}

static solc_status add_account(solc_legacy_builder_scratch *scratch,
                               size_t *account_count,
                               const uint8_t *pubkey,
                               uint8_t is_signer,
                               uint8_t is_writable,
                               size_t *account_index) {
    size_t i;
    uint8_t flags;
    if (pubkey == NULL || is_signer > 1u || is_writable > 1u) {
        return SOLC_E_INVALID_MODEL;
    }
    flags = (is_signer != 0u ? ACCOUNT_SIGNER : 0u) |
            (is_writable != 0u ? ACCOUNT_WRITABLE : 0u);
    for (i = 0u; i < *account_count; ++i) {
        if (same_key(scratch->discovered_account_keys + i * SOLC_PUBKEY_BYTES,
                     pubkey)) {
            scratch->account_flags[i] |= flags;
            *account_index = i;
            return SOLC_OK;
        }
    }
    if (*account_count >= scratch->account_capacity ||
        *account_count >= SOLC_BUILDER_MAX_ACCOUNTS) {
        return SOLC_E_SCRATCH_TOO_SMALL;
    }
    memcpy(scratch->discovered_account_keys + *account_count * SOLC_PUBKEY_BYTES,
           pubkey,
           SOLC_PUBKEY_BYTES);
    scratch->account_flags[*account_count] = flags;
    *account_index = *account_count;
    ++*account_count;
    return SOLC_OK;
}

static unsigned int account_group(uint8_t flags) {
    if ((flags & ACCOUNT_SIGNER) != 0u) {
        return (flags & ACCOUNT_WRITABLE) != 0u ? 0u : 1u;
    }
    return (flags & ACCOUNT_WRITABLE) != 0u ? 2u : 3u;
}

solc_status solc_legacy_transaction_build(
    const uint8_t fee_payer[SOLC_PUBKEY_BYTES],
    const uint8_t recent_blockhash[SOLC_HASH_BYTES],
    solc_slice signatures,
    const solc_transaction_instruction *instructions,
    size_t instruction_count,
    solc_legacy_builder_scratch *scratch,
    solc_transaction *out,
    solc_error *error) {
    size_t account_count = 0u;
    size_t account_index_offset = 0u;
    size_t ignored_index;
    size_t i;
    size_t j;
    size_t ordered_count = 0u;
    size_t signer_count = 0u;
    size_t readonly_signer_count = 0u;
    size_t readonly_unsigned_count = 0u;
    solc_status status;

    if (fee_payer == NULL || recent_blockhash == NULL || scratch == NULL ||
        out == NULL || (instruction_count != 0u && instructions == NULL) ||
        (signatures.len != 0u && signatures.data == NULL) ||
        scratch->discovered_account_keys == NULL ||
        scratch->ordered_account_keys == NULL || scratch->account_flags == NULL ||
        scratch->old_to_new == NULL ||
        (scratch->instruction_capacity != 0u &&
         scratch->compiled_instructions == NULL) ||
        (scratch->account_index_capacity != 0u && scratch->account_indices == NULL)) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (scratch->account_capacity == 0u ||
        scratch->account_capacity > SOLC_BUILDER_MAX_ACCOUNTS ||
        instruction_count > scratch->instruction_capacity ||
        signatures.len % SOLC_SIGNATURE_BYTES != 0u) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    memset(out, 0, sizeof(*out));
    status = add_account(scratch, &account_count, fee_payer, 1u, 1u, &ignored_index);
    if (status != SOLC_OK) {
        return fail(error, status, 0u);
    }

    for (i = 0u; i < instruction_count; ++i) {
        const solc_transaction_instruction *source = &instructions[i];
        solc_compiled_instruction *compiled = &scratch->compiled_instructions[i];
        size_t program_index;
        if (source->program_id == NULL ||
            (source->account_count != 0u && source->accounts == NULL) ||
            (source->data.len != 0u && source->data.data == NULL)) {
            return fail(error, SOLC_E_NULL, i);
        }
        if (source->account_count > scratch->account_index_capacity -
                                        account_index_offset) {
            return fail(error, SOLC_E_SCRATCH_TOO_SMALL, i);
        }
        status = add_account(
            scratch, &account_count, source->program_id, 0u, 0u, &program_index);
        if (status != SOLC_OK) {
            return fail(error, status, i);
        }
        compiled->program_id_index = (uint8_t)program_index;
        compiled->account_indices.data = source->account_count == 0u
                                             ? NULL
                                             : scratch->account_indices +
                                                   account_index_offset;
        compiled->account_indices.len = source->account_count;
        compiled->data = source->data;
        for (j = 0u; j < source->account_count; ++j) {
            size_t index;
            status = add_account(scratch,
                                 &account_count,
                                 source->accounts[j].pubkey,
                                 source->accounts[j].is_signer,
                                 source->accounts[j].is_writable,
                                 &index);
            if (status != SOLC_OK) {
                return fail(error, status, i);
            }
            scratch->account_indices[account_index_offset + j] = (uint8_t)index;
        }
        account_index_offset += source->account_count;
    }

    for (i = 0u; i < 4u; ++i) {
        for (j = 0u; j < account_count; ++j) {
            if (account_group(scratch->account_flags[j]) == i) {
                memcpy(scratch->ordered_account_keys +
                           ordered_count * SOLC_PUBKEY_BYTES,
                       scratch->discovered_account_keys + j * SOLC_PUBKEY_BYTES,
                       SOLC_PUBKEY_BYTES);
                scratch->old_to_new[j] = (uint8_t)ordered_count;
                ++ordered_count;
            }
        }
    }
    for (i = 0u; i < account_count; ++i) {
        uint8_t flags = scratch->account_flags[i];
        if ((flags & ACCOUNT_SIGNER) != 0u) {
            ++signer_count;
            if ((flags & ACCOUNT_WRITABLE) == 0u) {
                ++readonly_signer_count;
            }
        } else if ((flags & ACCOUNT_WRITABLE) == 0u) {
            ++readonly_unsigned_count;
        }
    }
    if (signer_count > UINT8_MAX || readonly_signer_count > UINT8_MAX ||
        readonly_unsigned_count > UINT8_MAX ||
        signatures.len / SOLC_SIGNATURE_BYTES != signer_count) {
        return fail(error, SOLC_E_SIGNATURE_MISMATCH, 0u);
    }
    account_index_offset = 0u;
    for (i = 0u; i < instruction_count; ++i) {
        solc_compiled_instruction *compiled = &scratch->compiled_instructions[i];
        compiled->program_id_index = scratch->old_to_new[compiled->program_id_index];
        for (j = 0u; j < compiled->account_indices.len; ++j) {
            uint8_t old_index = scratch->account_indices[account_index_offset + j];
            scratch->account_indices[account_index_offset + j] =
                scratch->old_to_new[old_index];
        }
        account_index_offset += compiled->account_indices.len;
    }

    out->signatures = signatures;
    out->message.version = SOLC_MESSAGE_LEGACY;
    out->message.num_required_signatures = (uint8_t)signer_count;
    out->message.num_readonly_signed_accounts = (uint8_t)readonly_signer_count;
    out->message.num_readonly_unsigned_accounts = (uint8_t)readonly_unsigned_count;
    out->message.static_account_keys.data = scratch->ordered_account_keys;
    out->message.static_account_keys.len = account_count * SOLC_PUBKEY_BYTES;
    out->message.lifetime_specifier.data = recent_blockhash;
    out->message.lifetime_specifier.len = SOLC_HASH_BYTES;
    out->message.instructions = scratch->compiled_instructions;
    out->message.instruction_count = instruction_count;
    succeed(error);
    return SOLC_OK;
}
