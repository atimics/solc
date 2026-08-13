#include "solc/crypto.h"

#include <limits.h>
#include <string.h>

typedef struct reader {
    const uint8_t *input;
    size_t len;
    size_t pos;
    solc_error *error;
} reader;

typedef struct writer {
    uint8_t *output;
    size_t capacity;
    size_t pos;
} writer;

static void set_error(solc_error *error, solc_status status, size_t offset) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
    }
}

static solc_status reader_take(reader *r, size_t len, const uint8_t **out) {
    if (len > r->len - r->pos) {
        set_error(r->error, SOLC_E_TRUNCATED, r->pos);
        return SOLC_E_TRUNCATED;
    }
    *out = r->input + r->pos;
    r->pos += len;
    return SOLC_OK;
}

static solc_status reader_u8(reader *r, uint8_t *out) {
    const uint8_t *byte = NULL;
    solc_status status = reader_take(r, 1u, &byte);
    if (status == SOLC_OK) {
        *out = byte[0];
    }
    return status;
}

static solc_status reader_u16_le(reader *r, uint16_t *out) {
    const uint8_t *bytes = NULL;
    solc_status status = reader_take(r, 2u, &bytes);
    if (status == SOLC_OK) {
        *out = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
    }
    return status;
}

static solc_status reader_u32_le(reader *r, uint32_t *out) {
    const uint8_t *bytes = NULL;
    solc_status status = reader_take(r, 4u, &bytes);
    if (status == SOLC_OK) {
        *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
               ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
    }
    return status;
}

static solc_status reader_u64_le(reader *r, uint64_t *out) {
    const uint8_t *bytes = NULL;
    solc_status status = reader_take(r, 8u, &bytes);
    size_t i = 0u;
    uint64_t value = 0u;
    if (status != SOLC_OK) {
        return status;
    }
    for (i = 0u; i < 8u; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    *out = value;
    return SOLC_OK;
}

static solc_status reader_short_u16(reader *r, uint16_t *out) {
    size_t consumed = 0u;
    solc_status status =
        solc_short_u16_decode(r->input + r->pos, r->len - r->pos, out, &consumed);
    if (status != SOLC_OK) {
        set_error(r->error, status, r->pos);
        return status;
    }
    r->pos += consumed;
    return SOLC_OK;
}

static solc_status writer_bytes(writer *w, const uint8_t *data, size_t len) {
    if (len > SIZE_MAX - w->pos) {
        return SOLC_E_OVERFLOW;
    }
    if (w->output != NULL) {
        if (len > w->capacity - w->pos) {
            return SOLC_E_OUTPUT_TOO_SMALL;
        }
        if (len != 0u) {
            memcpy(w->output + w->pos, data, len);
        }
    }
    w->pos += len;
    return SOLC_OK;
}

static solc_status writer_u8(writer *w, uint8_t value) {
    return writer_bytes(w, &value, 1u);
}

static solc_status writer_u16_le(writer *w, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8u)};
    return writer_bytes(w, bytes, sizeof(bytes));
}

static solc_status writer_u32_le(writer *w, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u),
    };
    return writer_bytes(w, bytes, sizeof(bytes));
}

static solc_status writer_u64_le(writer *w, uint64_t value) {
    uint8_t bytes[8];
    size_t i = 0u;
    for (i = 0u; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8u));
    }
    return writer_bytes(w, bytes, sizeof(bytes));
}

static solc_status writer_short_u16(writer *w, uint16_t value) {
    uint8_t bytes[3];
    size_t written = 0u;
    solc_status status = solc_short_u16_encode(value, bytes, &written);
    if (status != SOLC_OK) {
        return status;
    }
    return writer_bytes(w, bytes, written);
}

static int slice_is_valid(solc_slice slice) {
    return slice.len == 0u || slice.data != NULL;
}

solc_status solc_short_u16_decode(const uint8_t *input,
                                  size_t input_len,
                                  uint16_t *value,
                                  size_t *consumed) {
    uint32_t result = 0u;
    size_t i = 0u;

    if (input == NULL || value == NULL || consumed == NULL) {
        return SOLC_E_NULL;
    }
    *consumed = 0u;

    for (i = 0u; i < 3u; ++i) {
        uint8_t byte;
        uint32_t payload;
        if (i >= input_len) {
            return SOLC_E_TRUNCATED;
        }
        byte = input[i];
        payload = (uint32_t)(byte & 0x7fu);

        if (i != 0u && byte == 0u) {
            return SOLC_E_NON_CANONICAL;
        }
        if (i == 2u && (payload > 3u || (byte & 0x80u) != 0u)) {
            return SOLC_E_OVERFLOW;
        }

        result |= payload << (i * 7u);
        if ((byte & 0x80u) == 0u) {
            *value = (uint16_t)result;
            *consumed = i + 1u;
            return SOLC_OK;
        }
    }
    return SOLC_E_OVERFLOW;
}

solc_status solc_short_u16_encode(uint16_t value,
                                  uint8_t output[3],
                                  size_t *written) {
    uint16_t remaining = value;
    size_t pos = 0u;
    if (output == NULL || written == NULL) {
        return SOLC_E_NULL;
    }
    do {
        uint8_t byte = (uint8_t)(remaining & 0x7fu);
        remaining >>= 7u;
        if (remaining != 0u) {
            byte |= 0x80u;
        }
        output[pos++] = byte;
    } while (remaining != 0u);
    *written = pos;
    return SOLC_OK;
}

static solc_status validate_config_mask(uint32_t mask) {
    uint32_t priority_bits = mask & SOLC_V1_CONFIG_PRIORITY_FEE;
    if ((mask & ~SOLC_V1_CONFIG_KNOWN_BITS) != 0u ||
        (priority_bits != 0u && priority_bits != SOLC_V1_CONFIG_PRIORITY_FEE)) {
        return SOLC_E_INVALID_CONFIG;
    }
    return SOLC_OK;
}

static solc_status validate_config(const solc_v1_config *config) {
    if (validate_config_mask(config->mask) != SOLC_OK) {
        return SOLC_E_INVALID_CONFIG;
    }
    if ((config->mask & SOLC_V1_CONFIG_HEAP_SIZE) != 0u) {
        if (config->heap_size < 32768u || config->heap_size > 262144u ||
            (config->heap_size % 1024u) != 0u) {
            return SOLC_E_INVALID_CONFIG;
        }
    }
    return SOLC_OK;
}

static solc_status validate_transaction(const solc_transaction *transaction) {
    const solc_message *message;
    size_t static_count;
    size_t signature_count;
    size_t loaded_count = 0u;
    size_t total_count;
    size_t i;

    if (transaction == NULL) {
        return SOLC_E_NULL;
    }
    message = &transaction->message;
    if (!slice_is_valid(transaction->signatures) ||
        !slice_is_valid(message->static_account_keys) ||
        !slice_is_valid(message->lifetime_specifier) ||
        message->lifetime_specifier.len != SOLC_HASH_BYTES ||
        (message->static_account_keys.len % SOLC_PUBKEY_BYTES) != 0u ||
        (transaction->signatures.len % SOLC_SIGNATURE_BYTES) != 0u ||
        (message->instruction_count != 0u && message->instructions == NULL) ||
        (message->address_table_lookup_count != 0u &&
         message->address_table_lookups == NULL)) {
        return SOLC_E_INVALID_MODEL;
    }

    static_count = message->static_account_keys.len / SOLC_PUBKEY_BYTES;
    signature_count = transaction->signatures.len / SOLC_SIGNATURE_BYTES;

    if (signature_count != (size_t)message->num_required_signatures) {
        return SOLC_E_SIGNATURE_MISMATCH;
    }
    if (message->num_readonly_signed_accounts >= message->num_required_signatures ||
        (size_t)message->num_required_signatures +
                (size_t)message->num_readonly_unsigned_accounts >
            static_count) {
        return SOLC_E_INVALID_HEADER;
    }

    if (message->version == SOLC_MESSAGE_LEGACY) {
        if (message->address_table_lookup_count != 0u || static_count > 256u ||
            message->instruction_count > UINT16_MAX || signature_count >= 128u) {
            return SOLC_E_INVALID_MODEL;
        }
    } else if (message->version == SOLC_MESSAGE_V0) {
        if (static_count > 256u || message->instruction_count > UINT16_MAX ||
            signature_count >= 128u) {
            return SOLC_E_INVALID_MODEL;
        }
        for (i = 0u; i < message->address_table_lookup_count; ++i) {
            const solc_address_table_lookup *lookup = &message->address_table_lookups[i];
            if (!slice_is_valid(lookup->account_key) ||
                !slice_is_valid(lookup->writable_indices) ||
                !slice_is_valid(lookup->readonly_indices) ||
                lookup->account_key.len != SOLC_PUBKEY_BYTES ||
                lookup->writable_indices.len > UINT16_MAX ||
                lookup->readonly_indices.len > UINT16_MAX ||
                (lookup->writable_indices.len == 0u && lookup->readonly_indices.len == 0u)) {
                return SOLC_E_INVALID_MODEL;
            }
            if (lookup->writable_indices.len > SIZE_MAX - loaded_count ||
                lookup->readonly_indices.len >
                    SIZE_MAX - loaded_count - lookup->writable_indices.len) {
                return SOLC_E_OVERFLOW;
            }
            loaded_count += lookup->writable_indices.len + lookup->readonly_indices.len;
        }
        if (message->address_table_lookup_count > UINT16_MAX) {
            return SOLC_E_INVALID_MODEL;
        }
    } else if (message->version == SOLC_MESSAGE_V1) {
        solc_status config_status;
        if (message->address_table_lookup_count != 0u ||
            static_count > SOLC_V1_MAX_ADDRESSES ||
            message->instruction_count > SOLC_V1_MAX_INSTRUCTIONS ||
            signature_count > SOLC_V1_MAX_SIGNATURES) {
            return SOLC_E_LIMIT_EXCEEDED;
        }
        config_status = validate_config(&message->v1_config);
        if (config_status != SOLC_OK) {
            return config_status;
        }
        for (i = 0u; i < static_count; ++i) {
            size_t j;
            const uint8_t *left = message->static_account_keys.data + i * SOLC_PUBKEY_BYTES;
            for (j = i + 1u; j < static_count; ++j) {
                const uint8_t *right =
                    message->static_account_keys.data + j * SOLC_PUBKEY_BYTES;
                if (memcmp(left, right, SOLC_PUBKEY_BYTES) == 0) {
                    return SOLC_E_DUPLICATE_ADDRESS;
                }
            }
        }
    } else {
        return SOLC_E_UNSUPPORTED_VERSION;
    }

    if (loaded_count > SIZE_MAX - static_count) {
        return SOLC_E_OVERFLOW;
    }
    total_count = static_count + loaded_count;
    if (total_count == 0u || total_count > 256u) {
        return SOLC_E_LIMIT_EXCEEDED;
    }

    for (i = 0u; i < message->instruction_count; ++i) {
        const solc_compiled_instruction *instruction = &message->instructions[i];
        size_t j;
        if (!slice_is_valid(instruction->account_indices) ||
            !slice_is_valid(instruction->data)) {
            return SOLC_E_INVALID_MODEL;
        }
        if (instruction->program_id_index == 0u ||
            (size_t)instruction->program_id_index >= static_count) {
            return SOLC_E_INVALID_INDEX;
        }
        if (message->version == SOLC_MESSAGE_V1) {
            if (instruction->account_indices.len > UINT8_MAX ||
                instruction->data.len > UINT16_MAX) {
                return SOLC_E_LIMIT_EXCEEDED;
            }
        } else if (instruction->account_indices.len > UINT16_MAX ||
                   instruction->data.len > UINT16_MAX) {
            return SOLC_E_LIMIT_EXCEEDED;
        }
        for (j = 0u; j < instruction->account_indices.len; ++j) {
            if ((size_t)instruction->account_indices.data[j] >= total_count) {
                return SOLC_E_INVALID_INDEX;
            }
        }
    }
    return SOLC_OK;
}

static solc_status decode_legacy_or_v0(reader *r,
                                       uint8_t signature_count,
                                       solc_decode_scratch *scratch,
                                       solc_transaction *out) {
    const uint8_t *bytes = NULL;
    uint16_t count = 0u;
    size_t i;
    solc_status status;

    status = reader_take(r, (size_t)signature_count * SOLC_SIGNATURE_BYTES, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->signatures.data = bytes;
    out->signatures.len = (size_t)signature_count * SOLC_SIGNATURE_BYTES;

    if (r->pos >= r->len) {
        set_error(r->error, SOLC_E_TRUNCATED, r->pos);
        return SOLC_E_TRUNCATED;
    }
    if ((r->input[r->pos] & 0x80u) != 0u) {
        uint8_t prefix = 0u;
        status = reader_u8(r, &prefix);
        if (status != SOLC_OK) {
            return status;
        }
        if (prefix != 0x80u) {
            set_error(r->error, SOLC_E_UNSUPPORTED_VERSION, r->pos - 1u);
            return SOLC_E_UNSUPPORTED_VERSION;
        }
        out->message.version = SOLC_MESSAGE_V0;
    } else {
        out->message.version = SOLC_MESSAGE_LEGACY;
    }

    status = reader_u8(r, &out->message.num_required_signatures);
    if (status == SOLC_OK) {
        status = reader_u8(r, &out->message.num_readonly_signed_accounts);
    }
    if (status == SOLC_OK) {
        status = reader_u8(r, &out->message.num_readonly_unsigned_accounts);
    }
    if (status != SOLC_OK) {
        return status;
    }

    status = reader_short_u16(r, &count);
    if (status != SOLC_OK) {
        return status;
    }
    status = reader_take(r, (size_t)count * SOLC_PUBKEY_BYTES, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->message.static_account_keys.data = bytes;
    out->message.static_account_keys.len = (size_t)count * SOLC_PUBKEY_BYTES;

    status = reader_take(r, SOLC_HASH_BYTES, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->message.lifetime_specifier.data = bytes;
    out->message.lifetime_specifier.len = SOLC_HASH_BYTES;

    status = reader_short_u16(r, &count);
    if (status != SOLC_OK) {
        return status;
    }
    if ((size_t)count > scratch->instruction_capacity) {
        set_error(r->error, SOLC_E_SCRATCH_TOO_SMALL, r->pos);
        return SOLC_E_SCRATCH_TOO_SMALL;
    }
    out->message.instructions = scratch->instructions;
    out->message.instruction_count = count;

    for (i = 0u; i < (size_t)count; ++i) {
        solc_compiled_instruction *instruction = &scratch->instructions[i];
        uint16_t item_len = 0u;
        status = reader_u8(r, &instruction->program_id_index);
        if (status == SOLC_OK) {
            status = reader_short_u16(r, &item_len);
        }
        if (status == SOLC_OK) {
            status = reader_take(r, item_len, &bytes);
        }
        if (status != SOLC_OK) {
            return status;
        }
        instruction->account_indices.data = bytes;
        instruction->account_indices.len = item_len;

        status = reader_short_u16(r, &item_len);
        if (status == SOLC_OK) {
            status = reader_take(r, item_len, &bytes);
        }
        if (status != SOLC_OK) {
            return status;
        }
        instruction->data.data = bytes;
        instruction->data.len = item_len;
    }

    if (out->message.version == SOLC_MESSAGE_V0) {
        status = reader_short_u16(r, &count);
        if (status != SOLC_OK) {
            return status;
        }
        if ((size_t)count > scratch->address_table_lookup_capacity) {
            set_error(r->error, SOLC_E_SCRATCH_TOO_SMALL, r->pos);
            return SOLC_E_SCRATCH_TOO_SMALL;
        }
        out->message.address_table_lookups = scratch->address_table_lookups;
        out->message.address_table_lookup_count = count;
        for (i = 0u; i < (size_t)count; ++i) {
            solc_address_table_lookup *lookup = &scratch->address_table_lookups[i];
            uint16_t item_len = 0u;
            status = reader_take(r, SOLC_PUBKEY_BYTES, &bytes);
            if (status != SOLC_OK) {
                return status;
            }
            lookup->account_key.data = bytes;
            lookup->account_key.len = SOLC_PUBKEY_BYTES;

            status = reader_short_u16(r, &item_len);
            if (status == SOLC_OK) {
                status = reader_take(r, item_len, &bytes);
            }
            if (status != SOLC_OK) {
                return status;
            }
            lookup->writable_indices.data = bytes;
            lookup->writable_indices.len = item_len;

            status = reader_short_u16(r, &item_len);
            if (status == SOLC_OK) {
                status = reader_take(r, item_len, &bytes);
            }
            if (status != SOLC_OK) {
                return status;
            }
            lookup->readonly_indices.data = bytes;
            lookup->readonly_indices.len = item_len;
        }
    }
    return SOLC_OK;
}

static solc_status decode_v1(reader *r,
                             solc_decode_scratch *scratch,
                             solc_transaction *out) {
    const uint8_t *bytes = NULL;
    uint8_t instruction_count = 0u;
    uint8_t address_count = 0u;
    size_t i;
    solc_status status;

    out->message.version = SOLC_MESSAGE_V1;
    status = reader_u8(r, &out->message.num_required_signatures);
    if (status == SOLC_OK) {
        status = reader_u8(r, &out->message.num_readonly_signed_accounts);
    }
    if (status == SOLC_OK) {
        status = reader_u8(r, &out->message.num_readonly_unsigned_accounts);
    }
    if (status == SOLC_OK) {
        status = reader_u32_le(r, &out->message.v1_config.mask);
    }
    if (status != SOLC_OK) {
        return status;
    }
    status = validate_config_mask(out->message.v1_config.mask);
    if (status != SOLC_OK) {
        set_error(r->error, status, r->pos - 4u);
        return status;
    }

    status = reader_take(r, SOLC_HASH_BYTES, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->message.lifetime_specifier.data = bytes;
    out->message.lifetime_specifier.len = SOLC_HASH_BYTES;
    status = reader_u8(r, &instruction_count);
    if (status == SOLC_OK) {
        status = reader_u8(r, &address_count);
    }
    if (status != SOLC_OK) {
        return status;
    }
    if (instruction_count > SOLC_V1_MAX_INSTRUCTIONS ||
        address_count > SOLC_V1_MAX_ADDRESSES) {
        set_error(r->error, SOLC_E_LIMIT_EXCEEDED, r->pos - 2u);
        return SOLC_E_LIMIT_EXCEEDED;
    }
    if ((size_t)instruction_count > scratch->instruction_capacity) {
        set_error(r->error, SOLC_E_SCRATCH_TOO_SMALL, r->pos - 2u);
        return SOLC_E_SCRATCH_TOO_SMALL;
    }

    status = reader_take(r, (size_t)address_count * SOLC_PUBKEY_BYTES, &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->message.static_account_keys.data = bytes;
    out->message.static_account_keys.len = (size_t)address_count * SOLC_PUBKEY_BYTES;

    if ((out->message.v1_config.mask & SOLC_V1_CONFIG_PRIORITY_FEE) ==
        SOLC_V1_CONFIG_PRIORITY_FEE) {
        status = reader_u64_le(r, &out->message.v1_config.priority_fee);
    }
    if (status == SOLC_OK &&
        (out->message.v1_config.mask & SOLC_V1_CONFIG_COMPUTE_UNIT_LIMIT) != 0u) {
        status = reader_u32_le(r, &out->message.v1_config.compute_unit_limit);
    }
    if (status == SOLC_OK &&
        (out->message.v1_config.mask &
         SOLC_V1_CONFIG_LOADED_ACCOUNTS_DATA_SIZE_LIMIT) != 0u) {
        status =
            reader_u32_le(r, &out->message.v1_config.loaded_accounts_data_size_limit);
    }
    if (status == SOLC_OK &&
        (out->message.v1_config.mask & SOLC_V1_CONFIG_HEAP_SIZE) != 0u) {
        status = reader_u32_le(r, &out->message.v1_config.heap_size);
    }
    if (status != SOLC_OK) {
        return status;
    }
    status = validate_config(&out->message.v1_config);
    if (status != SOLC_OK) {
        set_error(r->error, status, r->pos);
        return status;
    }

    out->message.instructions = scratch->instructions;
    out->message.instruction_count = instruction_count;
    for (i = 0u; i < (size_t)instruction_count; ++i) {
        solc_compiled_instruction *instruction = &scratch->instructions[i];
        uint8_t account_len = 0u;
        uint16_t data_len = 0u;
        status = reader_u8(r, &instruction->program_id_index);
        if (status == SOLC_OK) {
            status = reader_u8(r, &account_len);
        }
        if (status == SOLC_OK) {
            status = reader_u16_le(r, &data_len);
        }
        if (status != SOLC_OK) {
            return status;
        }
        instruction->account_indices.data = NULL;
        instruction->account_indices.len = account_len;
        instruction->data.data = NULL;
        instruction->data.len = data_len;
    }
    for (i = 0u; i < (size_t)instruction_count; ++i) {
        solc_compiled_instruction *instruction = &scratch->instructions[i];
        status = reader_take(r, instruction->account_indices.len, &bytes);
        if (status != SOLC_OK) {
            return status;
        }
        instruction->account_indices.data = bytes;
        status = reader_take(r, instruction->data.len, &bytes);
        if (status != SOLC_OK) {
            return status;
        }
        instruction->data.data = bytes;
    }

    status = reader_take(r,
                         (size_t)out->message.num_required_signatures * SOLC_SIGNATURE_BYTES,
                         &bytes);
    if (status != SOLC_OK) {
        return status;
    }
    out->signatures.data = bytes;
    out->signatures.len =
        (size_t)out->message.num_required_signatures * SOLC_SIGNATURE_BYTES;
    return SOLC_OK;
}

solc_status solc_transaction_decode(const uint8_t *input,
                                    size_t input_len,
                                    solc_decode_scratch *scratch,
                                    solc_transaction *out,
                                    solc_error *error) {
    reader r;
    uint8_t discriminator;
    solc_status status;

    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = 0u;
    }
    if (input == NULL || scratch == NULL || out == NULL ||
        (scratch->instruction_capacity != 0u && scratch->instructions == NULL) ||
        (scratch->address_table_lookup_capacity != 0u &&
         scratch->address_table_lookups == NULL)) {
        set_error(error, SOLC_E_NULL, 0u);
        return SOLC_E_NULL;
    }
    memset(out, 0, sizeof(*out));
    r.input = input;
    r.len = input_len;
    r.pos = 0u;
    r.error = error;

    status = reader_u8(&r, &discriminator);
    if (status != SOLC_OK) {
        return status;
    }
    if ((discriminator & 0x80u) == 0u) {
        if (input_len > SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES) {
            set_error(error, SOLC_E_LIMIT_EXCEEDED, 0u);
            return SOLC_E_LIMIT_EXCEEDED;
        }
        status = decode_legacy_or_v0(&r, discriminator, scratch, out);
    } else if (discriminator == 0x81u) {
        if (input_len > SOLC_V1_MAX_TRANSACTION_BYTES) {
            set_error(error, SOLC_E_LIMIT_EXCEEDED, 0u);
            return SOLC_E_LIMIT_EXCEEDED;
        }
        status = decode_v1(&r, scratch, out);
    } else {
        set_error(error, SOLC_E_UNSUPPORTED_VERSION, 0u);
        return SOLC_E_UNSUPPORTED_VERSION;
    }
    if (status != SOLC_OK) {
        return status;
    }
    if (r.pos != r.len) {
        set_error(error, SOLC_E_TRAILING_BYTES, r.pos);
        return SOLC_E_TRAILING_BYTES;
    }
    status = validate_transaction(out);
    if (status != SOLC_OK) {
        set_error(error, status, r.pos);
        return status;
    }
    return SOLC_OK;
}

static solc_status encode_message_impl(const solc_transaction *transaction, writer *w) {
    const solc_message *message = &transaction->message;
    size_t static_count = message->static_account_keys.len / SOLC_PUBKEY_BYTES;
    size_t i;
    solc_status status;

#define WRITE_OR_RETURN(expression)       \
    do {                                  \
        status = (expression);            \
        if (status != SOLC_OK) {          \
            return status;                \
        }                                 \
    } while (0)

    if (message->version == SOLC_MESSAGE_LEGACY ||
        message->version == SOLC_MESSAGE_V0) {
        if (message->version == SOLC_MESSAGE_V0) {
            WRITE_OR_RETURN(writer_u8(w, 0x80u));
        }
        WRITE_OR_RETURN(writer_u8(w, message->num_required_signatures));
        WRITE_OR_RETURN(writer_u8(w, message->num_readonly_signed_accounts));
        WRITE_OR_RETURN(writer_u8(w, message->num_readonly_unsigned_accounts));
        WRITE_OR_RETURN(writer_short_u16(w, (uint16_t)static_count));
        WRITE_OR_RETURN(writer_bytes(w, message->static_account_keys.data,
                                     message->static_account_keys.len));
        WRITE_OR_RETURN(writer_bytes(w, message->lifetime_specifier.data,
                                     message->lifetime_specifier.len));
        WRITE_OR_RETURN(writer_short_u16(w, (uint16_t)message->instruction_count));
        for (i = 0u; i < message->instruction_count; ++i) {
            const solc_compiled_instruction *instruction = &message->instructions[i];
            WRITE_OR_RETURN(writer_u8(w, instruction->program_id_index));
            WRITE_OR_RETURN(
                writer_short_u16(w, (uint16_t)instruction->account_indices.len));
            WRITE_OR_RETURN(writer_bytes(w, instruction->account_indices.data,
                                         instruction->account_indices.len));
            WRITE_OR_RETURN(writer_short_u16(w, (uint16_t)instruction->data.len));
            WRITE_OR_RETURN(writer_bytes(w, instruction->data.data, instruction->data.len));
        }
        if (message->version == SOLC_MESSAGE_V0) {
            WRITE_OR_RETURN(
                writer_short_u16(w, (uint16_t)message->address_table_lookup_count));
            for (i = 0u; i < message->address_table_lookup_count; ++i) {
                const solc_address_table_lookup *lookup = &message->address_table_lookups[i];
                WRITE_OR_RETURN(writer_bytes(w, lookup->account_key.data,
                                             lookup->account_key.len));
                WRITE_OR_RETURN(
                    writer_short_u16(w, (uint16_t)lookup->writable_indices.len));
                WRITE_OR_RETURN(writer_bytes(w, lookup->writable_indices.data,
                                             lookup->writable_indices.len));
                WRITE_OR_RETURN(
                    writer_short_u16(w, (uint16_t)lookup->readonly_indices.len));
                WRITE_OR_RETURN(writer_bytes(w, lookup->readonly_indices.data,
                                             lookup->readonly_indices.len));
            }
        }
    } else {
        const solc_v1_config *config = &message->v1_config;
        WRITE_OR_RETURN(writer_u8(w, 0x81u));
        WRITE_OR_RETURN(writer_u8(w, message->num_required_signatures));
        WRITE_OR_RETURN(writer_u8(w, message->num_readonly_signed_accounts));
        WRITE_OR_RETURN(writer_u8(w, message->num_readonly_unsigned_accounts));
        WRITE_OR_RETURN(writer_u32_le(w, config->mask));
        WRITE_OR_RETURN(writer_bytes(w, message->lifetime_specifier.data,
                                     message->lifetime_specifier.len));
        WRITE_OR_RETURN(writer_u8(w, (uint8_t)message->instruction_count));
        WRITE_OR_RETURN(writer_u8(w, (uint8_t)static_count));
        WRITE_OR_RETURN(writer_bytes(w, message->static_account_keys.data,
                                     message->static_account_keys.len));
        if ((config->mask & SOLC_V1_CONFIG_PRIORITY_FEE) ==
            SOLC_V1_CONFIG_PRIORITY_FEE) {
            WRITE_OR_RETURN(writer_u64_le(w, config->priority_fee));
        }
        if ((config->mask & SOLC_V1_CONFIG_COMPUTE_UNIT_LIMIT) != 0u) {
            WRITE_OR_RETURN(writer_u32_le(w, config->compute_unit_limit));
        }
        if ((config->mask & SOLC_V1_CONFIG_LOADED_ACCOUNTS_DATA_SIZE_LIMIT) != 0u) {
            WRITE_OR_RETURN(writer_u32_le(w, config->loaded_accounts_data_size_limit));
        }
        if ((config->mask & SOLC_V1_CONFIG_HEAP_SIZE) != 0u) {
            WRITE_OR_RETURN(writer_u32_le(w, config->heap_size));
        }
        for (i = 0u; i < message->instruction_count; ++i) {
            const solc_compiled_instruction *instruction = &message->instructions[i];
            WRITE_OR_RETURN(writer_u8(w, instruction->program_id_index));
            WRITE_OR_RETURN(writer_u8(w, (uint8_t)instruction->account_indices.len));
            WRITE_OR_RETURN(writer_u16_le(w, (uint16_t)instruction->data.len));
        }
        for (i = 0u; i < message->instruction_count; ++i) {
            const solc_compiled_instruction *instruction = &message->instructions[i];
            WRITE_OR_RETURN(writer_bytes(w, instruction->account_indices.data,
                                         instruction->account_indices.len));
            WRITE_OR_RETURN(writer_bytes(w, instruction->data.data, instruction->data.len));
        }
    }
#undef WRITE_OR_RETURN
    return SOLC_OK;
}

static solc_status encode_impl(const solc_transaction *transaction, writer *w) {
    const solc_message *message = &transaction->message;
    size_t signature_count = transaction->signatures.len / SOLC_SIGNATURE_BYTES;
    solc_status status;

    if (message->version == SOLC_MESSAGE_LEGACY ||
        message->version == SOLC_MESSAGE_V0) {
        status = writer_short_u16(w, (uint16_t)signature_count);
        if (status == SOLC_OK) {
            status = writer_bytes(
                w, transaction->signatures.data, transaction->signatures.len);
        }
        if (status != SOLC_OK) {
            return status;
        }
        return encode_message_impl(transaction, w);
    }

    status = encode_message_impl(transaction, w);
    if (status != SOLC_OK) {
        return status;
    }
    return writer_bytes(w, transaction->signatures.data, transaction->signatures.len);
}

solc_status solc_transaction_message_encode(const solc_transaction *transaction,
                                            uint8_t *message_buffer,
                                            size_t message_capacity,
                                            size_t *message_len,
                                            solc_error *error) {
    writer sizing = {NULL, 0u, 0u};
    writer transaction_sizing = {NULL, 0u, 0u};
    writer actual;
    size_t limit;
    solc_status status;

    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = 0u;
    }
    if (transaction == NULL || message_len == NULL) {
        set_error(error, SOLC_E_NULL, 0u);
        return SOLC_E_NULL;
    }
    status = validate_transaction(transaction);
    if (status != SOLC_OK) {
        set_error(error, status, 0u);
        return status;
    }
    status = encode_impl(transaction, &transaction_sizing);
    if (status != SOLC_OK) {
        set_error(error, status, transaction_sizing.pos);
        return status;
    }
    limit = transaction->message.version == SOLC_MESSAGE_V1
                ? SOLC_V1_MAX_TRANSACTION_BYTES
                : SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES;
    if (transaction_sizing.pos > limit) {
        set_error(error, SOLC_E_LIMIT_EXCEEDED, transaction_sizing.pos);
        return SOLC_E_LIMIT_EXCEEDED;
    }
    status = encode_message_impl(transaction, &sizing);
    if (status != SOLC_OK) {
        set_error(error, status, sizing.pos);
        return status;
    }
    *message_len = sizing.pos;
    if (message_buffer == NULL) {
        return SOLC_OK;
    }
    if (message_capacity < sizing.pos) {
        set_error(error, SOLC_E_OUTPUT_TOO_SMALL, message_capacity);
        return SOLC_E_OUTPUT_TOO_SMALL;
    }
    actual.output = message_buffer;
    actual.capacity = message_capacity;
    actual.pos = 0u;
    status = encode_message_impl(transaction, &actual);
    if (status != SOLC_OK) {
        set_error(error, status, actual.pos);
        return status;
    }
    return SOLC_OK;
}

solc_status solc_transaction_encode(const solc_transaction *transaction,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_len,
                                    solc_error *error) {
    writer sizing = {NULL, 0u, 0u};
    writer actual;
    solc_status status;
    size_t limit;

    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = 0u;
    }
    if (transaction == NULL || output_len == NULL) {
        set_error(error, SOLC_E_NULL, 0u);
        return SOLC_E_NULL;
    }
    status = validate_transaction(transaction);
    if (status != SOLC_OK) {
        set_error(error, status, 0u);
        return status;
    }
    status = encode_impl(transaction, &sizing);
    if (status != SOLC_OK) {
        set_error(error, status, sizing.pos);
        return status;
    }
    *output_len = sizing.pos;
    limit = transaction->message.version == SOLC_MESSAGE_V1
                ? SOLC_V1_MAX_TRANSACTION_BYTES
                : SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES;
    if (sizing.pos > limit) {
        set_error(error, SOLC_E_LIMIT_EXCEEDED, sizing.pos);
        return SOLC_E_LIMIT_EXCEEDED;
    }
    if (output == NULL) {
        return SOLC_OK;
    }
    if (output_capacity < sizing.pos) {
        set_error(error, SOLC_E_OUTPUT_TOO_SMALL, output_capacity);
        return SOLC_E_OUTPUT_TOO_SMALL;
    }
    actual.output = output;
    actual.capacity = output_capacity;
    actual.pos = 0u;
    status = encode_impl(transaction, &actual);
    if (status != SOLC_OK) {
        set_error(error, status, actual.pos);
        return status;
    }
    return SOLC_OK;
}

const char *solc_status_string(solc_status status) {
    switch (status) {
        case SOLC_OK: return "ok";
        case SOLC_E_NULL: return "null argument";
        case SOLC_E_TRUNCATED: return "truncated input";
        case SOLC_E_NON_CANONICAL: return "non-canonical encoding";
        case SOLC_E_OVERFLOW: return "integer or size overflow";
        case SOLC_E_UNSUPPORTED_VERSION: return "unsupported transaction version";
        case SOLC_E_TRAILING_BYTES: return "trailing bytes";
        case SOLC_E_LIMIT_EXCEEDED: return "protocol limit exceeded";
        case SOLC_E_INVALID_HEADER: return "invalid message header";
        case SOLC_E_SIGNATURE_MISMATCH: return "signature count mismatch";
        case SOLC_E_INVALID_INDEX: return "invalid account or program index";
        case SOLC_E_SCRATCH_TOO_SMALL: return "decoder scratch space too small";
        case SOLC_E_OUTPUT_TOO_SMALL: return "output buffer too small";
        case SOLC_E_INVALID_MODEL: return "invalid transaction model";
        case SOLC_E_INVALID_CONFIG: return "invalid v1 transaction config";
        case SOLC_E_DUPLICATE_ADDRESS: return "duplicate v1 address";
        case SOLC_E_INVALID_ENCODING: return "invalid text encoding";
        case SOLC_E_CRYPTO_UNAVAILABLE: return "cryptographic provider unavailable";
        case SOLC_E_SIGNATURE_INVALID: return "invalid Ed25519 signature";
        case SOLC_E_PROVIDER_FAILURE: return "cryptographic provider failure";
        case SOLC_E_INVALID_DUPLICATE: return "invalid duplicate account reference";
        case SOLC_E_ACCOUNT_NOT_SIGNER: return "account is not a signer";
        case SOLC_E_ACCOUNT_NOT_WRITABLE: return "account is not writable";
        case SOLC_E_ACCOUNT_OWNER_MISMATCH: return "account owner mismatch";
        case SOLC_E_ACCOUNT_KEY_MISMATCH: return "account key mismatch";
        case SOLC_E_INVALID_SEEDS: return "invalid PDA seeds";
        case SOLC_E_CPI_PRIVILEGE_ESCALATION: return "CPI privilege escalation";
        case SOLC_E_INVALID_PROGRAM_DATA: return "invalid program data";
        case SOLC_E_INVALID_UTF8: return "invalid UTF-8";
        case SOLC_E_DUPLICATE_CONFIG: return "duplicate configuration";
        case SOLC_E_UNINITIALIZED_ACCOUNT: return "uninitialized account";
        default: return "unknown error";
    }
}

const char *solc_version_string(solc_message_version version) {
    switch (version) {
        case SOLC_MESSAGE_LEGACY: return "legacy";
        case SOLC_MESSAGE_V0: return "v0";
        case SOLC_MESSAGE_V1: return "v1";
        default: return "unknown";
    }
}
