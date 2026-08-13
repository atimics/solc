#include "solc/sbf.h"

typedef struct sbf_reader {
    uint8_t *input;
    size_t len;
    size_t pos;
    solc_error *error;
} sbf_reader;

static void zero_bytes(void *output, size_t len) {
    uint8_t *bytes = (uint8_t *)output;
    size_t i;
    for (i = 0u; i < len; ++i) {
        bytes[i] = 0u;
    }
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t len) {
    size_t i;
    for (i = 0u; i < len; ++i) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static void sbf_set_error(solc_error *error, solc_status status, size_t offset) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
    }
}

static solc_status sbf_take(sbf_reader *reader, size_t len, uint8_t **out) {
    if (len > SIZE_MAX - reader->pos ||
        (reader->len != SOLC_SBF_LOADER_TRUSTED_INPUT_LEN &&
         len > reader->len - reader->pos)) {
        sbf_set_error(reader->error, SOLC_E_TRUNCATED, reader->pos);
        return SOLC_E_TRUNCATED;
    }
    *out = reader->input + reader->pos;
    reader->pos += len;
    return SOLC_OK;
}

static uint64_t read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value;
}

static void write_u64_le(uint8_t *bytes, uint64_t value) {
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8u));
    }
}

static solc_status sbf_read_u64(sbf_reader *reader,
                                uint64_t *value,
                                uint8_t **storage) {
    uint8_t *bytes = NULL;
    solc_status status = sbf_take(reader, 8u, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    *value = read_u64_le(bytes);
    if (storage != NULL) {
        *storage = bytes;
    }
    return SOLC_OK;
}

static solc_status sbf_align_8(sbf_reader *reader) {
    uintptr_t address;
    size_t padding;
    uint8_t *ignored = NULL;
    if (reader->pos > UINTPTR_MAX - (uintptr_t)reader->input) {
        sbf_set_error(reader->error, SOLC_E_OVERFLOW, reader->pos);
        return SOLC_E_OVERFLOW;
    }
    address = (uintptr_t)reader->input + reader->pos;
    padding = (size_t)((8u - (address & 7u)) & 7u);
    return sbf_take(reader, padding, &ignored);
}

static solc_status size_from_u64(uint64_t value, size_t *out) {
    size_t converted = (size_t)value;
    if ((uint64_t)converted != value) {
        return SOLC_E_OVERFLOW;
    }
    *out = converted;
    return SOLC_OK;
}

solc_status solc_sbf_parameters_decode(uint8_t *input,
                                       size_t input_len,
                                       solc_sbf_account *account_scratch,
                                       size_t account_capacity,
                                       solc_sbf_parameters *out,
                                       solc_error *error) {
    sbf_reader reader;
    uint64_t raw_count = 0u;
    size_t account_count = 0u;
    size_t i;
    solc_status status;

    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = 0u;
    }
    if (input == NULL || out == NULL ||
        (account_capacity != 0u && account_scratch == NULL)) {
        sbf_set_error(error, SOLC_E_NULL, 0u);
        return SOLC_E_NULL;
    }
    zero_bytes(out, sizeof(*out));
    reader.input = input;
    reader.len = input_len;
    reader.pos = 0u;
    reader.error = error;
    status = sbf_read_u64(&reader, &raw_count, NULL);
    if (status == SOLC_OK) {
        status = size_from_u64(raw_count, &account_count);
    }
    if (status != SOLC_OK) {
        sbf_set_error(error, status, reader.pos);
        return status;
    }
    if (account_count > account_capacity) {
        sbf_set_error(error, SOLC_E_SCRATCH_TOO_SMALL, reader.pos);
        return SOLC_E_SCRATCH_TOO_SMALL;
    }

    for (i = 0u; i < account_count; ++i) {
        solc_sbf_account *account = &account_scratch[i];
        uint8_t *duplicate = NULL;
        uint8_t *fields = NULL;
        uint64_t raw_data_len = 0u;
        uint64_t rent_epoch = 0u;
        zero_bytes(account, sizeof(*account));
        status = sbf_take(&reader, 1u, &duplicate);
        if (status != SOLC_OK) {
            return status;
        }
        if (duplicate[0] != UINT8_MAX) {
            if ((size_t)duplicate[0] >= i) {
                sbf_set_error(error, SOLC_E_INVALID_DUPLICATE, reader.pos - 1u);
                return SOLC_E_INVALID_DUPLICATE;
            }
            status = sbf_take(&reader, 7u, &fields);
            if (status != SOLC_OK) {
                return status;
            }
            *account = account_scratch[duplicate[0]];
            account->duplicate_of = duplicate[0];
            continue;
        }

        account->duplicate_of = UINT8_MAX;
        status = sbf_take(&reader, 7u, &fields);
        if (status == SOLC_OK) {
            account->is_signer = fields[0] != 0u;
            account->is_writable = fields[1] != 0u;
            account->executable = fields[2] != 0u;
            status = sbf_take(&reader, SOLC_PUBKEY_BYTES, &fields);
        }
        if (status == SOLC_OK) {
            account->key = fields;
            status = sbf_take(&reader, SOLC_PUBKEY_BYTES, &account->owner);
        }
        if (status == SOLC_OK) {
            status = sbf_take(&reader, 8u, &account->lamports_le);
        }
        if (status == SOLC_OK) {
            status = sbf_read_u64(&reader, &raw_data_len, &account->data_len_le);
        }
        if (status == SOLC_OK) {
            status = size_from_u64(raw_data_len, &account->data_len);
        }
        if (status == SOLC_OK) {
            account->original_data_len = account->data_len;
            status = sbf_take(&reader, account->data_len, &account->data);
        }
        if (status == SOLC_OK) {
            status = sbf_take(
                &reader, SOLC_SBF_MAX_PERMITTED_DATA_INCREASE, &fields);
        }
        if (status == SOLC_OK) {
            status = sbf_align_8(&reader);
        }
        if (status == SOLC_OK) {
            status = sbf_read_u64(&reader, &rent_epoch, NULL);
        }
        if (status != SOLC_OK) {
            return status;
        }
        account->rent_epoch = rent_epoch;
    }

    {
        uint64_t raw_instruction_len = 0u;
        size_t instruction_len = 0u;
        uint8_t *bytes = NULL;
        status = sbf_read_u64(&reader, &raw_instruction_len, NULL);
        if (status == SOLC_OK) {
            status = size_from_u64(raw_instruction_len, &instruction_len);
        }
        if (status == SOLC_OK) {
            status = sbf_take(&reader, instruction_len, &bytes);
        }
        if (status != SOLC_OK) {
            return status;
        }
        out->instruction_data.data = bytes;
        out->instruction_data.len = instruction_len;
        status = sbf_take(&reader, SOLC_PUBKEY_BYTES, &bytes);
        if (status != SOLC_OK) {
            return status;
        }
        out->program_id = bytes;
    }
    if (input_len != SOLC_SBF_LOADER_TRUSTED_INPUT_LEN && reader.pos != input_len) {
        sbf_set_error(error, SOLC_E_TRAILING_BYTES, reader.pos);
        return SOLC_E_TRAILING_BYTES;
    }
    out->accounts = account_scratch;
    out->account_count = account_count;
    return SOLC_OK;
}

int solc_pubkey_equal(const uint8_t left[32], const uint8_t right[32]) {
    return left != NULL && right != NULL && bytes_equal(left, right, 32u);
}

solc_status solc_sbf_account_require_key(const solc_sbf_account *account,
                                         const uint8_t expected[32]) {
    if (account == NULL || expected == NULL) {
        return SOLC_E_NULL;
    }
    return solc_pubkey_equal(account->key, expected) ? SOLC_OK
                                                     : SOLC_E_ACCOUNT_KEY_MISMATCH;
}

solc_status solc_sbf_account_require_owner(const solc_sbf_account *account,
                                           const uint8_t expected[32]) {
    if (account == NULL || expected == NULL) {
        return SOLC_E_NULL;
    }
    return solc_pubkey_equal(account->owner, expected) ? SOLC_OK
                                                       : SOLC_E_ACCOUNT_OWNER_MISMATCH;
}

solc_status solc_sbf_account_require_signer(const solc_sbf_account *account) {
    if (account == NULL) {
        return SOLC_E_NULL;
    }
    return account->is_signer != 0u ? SOLC_OK : SOLC_E_ACCOUNT_NOT_SIGNER;
}

solc_status solc_sbf_account_require_writable(const solc_sbf_account *account) {
    if (account == NULL) {
        return SOLC_E_NULL;
    }
    return account->is_writable != 0u ? SOLC_OK : SOLC_E_ACCOUNT_NOT_WRITABLE;
}

solc_status solc_sbf_account_require_data_len(const solc_sbf_account *account,
                                              size_t minimum) {
    if (account == NULL) {
        return SOLC_E_NULL;
    }
    return account->data_len >= minimum ? SOLC_OK : SOLC_E_TRUNCATED;
}

uint64_t solc_sbf_account_lamports(const solc_sbf_account *account) {
    return account == NULL || account->lamports_le == NULL
               ? 0u
               : read_u64_le(account->lamports_le);
}

solc_status solc_sbf_account_set_lamports(solc_sbf_account *account,
                                          uint64_t lamports) {
    solc_status status = solc_sbf_account_require_writable(account);
    if (status != SOLC_OK) {
        return status;
    }
    if (account->lamports_le == NULL) {
        return SOLC_E_INVALID_MODEL;
    }
    write_u64_le(account->lamports_le, lamports);
    return SOLC_OK;
}

solc_status solc_sbf_account_set_data_len(solc_sbf_account *account,
                                          size_t data_len) {
    solc_status status = solc_sbf_account_require_writable(account);
    if (status != SOLC_OK) {
        return status;
    }
    if (account->data_len_le == NULL ||
        account->original_data_len > SIZE_MAX - SOLC_SBF_MAX_PERMITTED_DATA_INCREASE ||
        data_len > account->original_data_len + SOLC_SBF_MAX_PERMITTED_DATA_INCREASE) {
        return SOLC_E_LIMIT_EXCEEDED;
    }
    account->data_len = data_len;
    write_u64_le(account->data_len_le, (uint64_t)data_len);
    return SOLC_OK;
}

solc_status solc_sbf_account_data_mut(solc_sbf_account *account,
                                      size_t minimum,
                                      uint8_t **data) {
    solc_status status;
    if (data == NULL) {
        return SOLC_E_NULL;
    }
    status = solc_sbf_account_require_writable(account);
    if (status != SOLC_OK) {
        return status;
    }
    status = solc_sbf_account_require_data_len(account, minimum);
    if (status != SOLC_OK) {
        return status;
    }
    *data = account->data;
    return SOLC_OK;
}

solc_status solc_pda_seeds_validate(const solc_seed *seeds, size_t seed_count) {
    size_t i;
    if (seed_count > SOLC_PDA_MAX_SEEDS) {
        return SOLC_E_INVALID_SEEDS;
    }
    if (seed_count != 0u && seeds == NULL) {
        return SOLC_E_NULL;
    }
    for (i = 0u; i < seed_count; ++i) {
        if (seeds[i].len > SOLC_PDA_MAX_SEED_BYTES ||
            (seeds[i].len != 0u && seeds[i].data == NULL)) {
            return SOLC_E_INVALID_SEEDS;
        }
    }
    return SOLC_OK;
}

solc_status solc_pda_signers_validate(const solc_signer_seeds *signers,
                                      size_t signer_count) {
    size_t i;
    if (signer_count > (size_t)INT32_MAX) {
        return SOLC_E_LIMIT_EXCEEDED;
    }
    if (signer_count != 0u && signers == NULL) {
        return SOLC_E_NULL;
    }
    for (i = 0u; i < signer_count; ++i) {
        solc_status status =
            solc_pda_seeds_validate(signers[i].seeds, signers[i].seed_count);
        if (status != SOLC_OK) {
            return status;
        }
    }
    return SOLC_OK;
}

static solc_status cpi_add_account_info(solc_cpi_builder *builder,
                                        const solc_sbf_account *account) {
    size_t i;
    for (i = 0u; i < builder->account_info_count; ++i) {
        if (solc_pubkey_equal(builder->account_infos[i]->key, account->key)) {
            return SOLC_OK;
        }
    }
    if (builder->account_info_count >= builder->account_info_capacity ||
        builder->account_info_count >= SOLC_CPI_MAX_ACCOUNT_INFOS) {
        return SOLC_E_SCRATCH_TOO_SMALL;
    }
    builder->account_infos[builder->account_info_count++] = account;
    return SOLC_OK;
}

solc_status solc_cpi_builder_init(solc_cpi_builder *builder,
                                  const solc_sbf_account *program_account,
                                  solc_slice data,
                                  solc_cpi_account_meta *meta_scratch,
                                  size_t meta_capacity,
                                  const solc_sbf_account **account_info_scratch,
                                  size_t account_info_capacity) {
    if (builder == NULL || program_account == NULL || program_account->key == NULL ||
        (data.len != 0u && data.data == NULL) ||
        (meta_capacity != 0u && meta_scratch == NULL) ||
        (account_info_capacity != 0u && account_info_scratch == NULL)) {
        return SOLC_E_NULL;
    }
    if (data.len > SOLC_CPI_MAX_INSTRUCTION_DATA_BYTES ||
        meta_capacity > SOLC_CPI_MAX_INSTRUCTION_ACCOUNTS ||
        account_info_capacity > SOLC_CPI_MAX_ACCOUNT_INFOS) {
        return SOLC_E_LIMIT_EXCEEDED;
    }
    if (program_account->executable == 0u) {
        return SOLC_E_INVALID_MODEL;
    }
    zero_bytes(builder, sizeof(*builder));
    builder->program_account = program_account;
    builder->metas = meta_scratch;
    builder->meta_capacity = meta_capacity;
    builder->account_infos = account_info_scratch;
    builder->account_info_capacity = account_info_capacity;
    builder->data = data;
    return SOLC_OK;
}

solc_status solc_cpi_builder_add_account(solc_cpi_builder *builder,
                                         const solc_sbf_account *account,
                                         int request_writable,
                                         int request_signer,
                                         int signer_via_pda) {
    solc_cpi_account_meta *meta;
    solc_status status;
    if (builder == NULL || account == NULL || account->key == NULL) {
        return SOLC_E_NULL;
    }
    if (builder->finalized != 0u) {
        return SOLC_E_INVALID_MODEL;
    }
    if ((request_writable != 0 && account->is_writable == 0u) ||
        (request_signer != 0 && account->is_signer == 0u && signer_via_pda == 0)) {
        return SOLC_E_CPI_PRIVILEGE_ESCALATION;
    }
    if (builder->meta_count >= builder->meta_capacity ||
        builder->meta_count >= SOLC_CPI_MAX_INSTRUCTION_ACCOUNTS) {
        return SOLC_E_SCRATCH_TOO_SMALL;
    }
    status = cpi_add_account_info(builder, account);
    if (status != SOLC_OK) {
        return status;
    }
    meta = &builder->metas[builder->meta_count++];
    meta->pubkey = account->key;
    meta->is_writable = request_writable != 0;
    meta->is_signer = request_signer != 0;
    return SOLC_OK;
}

solc_status solc_cpi_builder_finish(solc_cpi_builder *builder,
                                    solc_cpi_instruction *instruction,
                                    const solc_sbf_account ***account_infos,
                                    size_t *account_info_count) {
    solc_status status;
    if (builder == NULL || instruction == NULL || account_infos == NULL ||
        account_info_count == NULL) {
        return SOLC_E_NULL;
    }
    if (builder->finalized != 0u) {
        return SOLC_E_INVALID_MODEL;
    }
    status = cpi_add_account_info(builder, builder->program_account);
    if (status != SOLC_OK) {
        return status;
    }
    instruction->program_id = builder->program_account->key;
    instruction->accounts = builder->metas;
    instruction->account_count = builder->meta_count;
    instruction->data = builder->data;
    *account_infos = builder->account_infos;
    *account_info_count = builder->account_info_count;
    builder->finalized = 1u;
    return SOLC_OK;
}
