#include "solc/programs.h"

#include <limits.h>
#include <string.h>

typedef struct program_cursor {
    const uint8_t *data;
    size_t len;
    size_t offset;
} program_cursor;

typedef struct program_writer {
    uint8_t *data;
    size_t capacity;
    size_t offset;
} program_writer;

static solc_status fail(solc_error *error, solc_status status, size_t offset) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
    }
    return status;
}

static void succeed(solc_error *error, size_t offset) {
    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = offset;
    }
}

static uint16_t read_u16_at(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t read_u32_at(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static uint64_t read_u64_at(const uint8_t *data) {
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        value |= (uint64_t)data[i] << (i * 8u);
    }
    return value;
}

static solc_status take(program_cursor *cursor, size_t len, const uint8_t **out) {
    if (len > cursor->len - cursor->offset) {
        return SOLC_E_TRUNCATED;
    }
    *out = cursor->data + cursor->offset;
    cursor->offset += len;
    return SOLC_OK;
}

static solc_status take_u8(program_cursor *cursor, uint8_t *out) {
    const uint8_t *bytes;
    solc_status status = take(cursor, 1u, &bytes);
    if (status == SOLC_OK) {
        *out = bytes[0];
    }
    return status;
}

static solc_status take_u32(program_cursor *cursor, uint32_t *out) {
    const uint8_t *bytes;
    solc_status status = take(cursor, 4u, &bytes);
    if (status == SOLC_OK) {
        *out = read_u32_at(bytes);
    }
    return status;
}

static solc_status take_u64(program_cursor *cursor, uint64_t *out) {
    const uint8_t *bytes;
    solc_status status = take(cursor, 8u, &bytes);
    if (status == SOLC_OK) {
        *out = read_u64_at(bytes);
    }
    return status;
}

static solc_status write_bytes(program_writer *writer, const uint8_t *data, size_t len) {
    if (len != 0u && data == NULL) {
        return SOLC_E_INVALID_MODEL;
    }
    if (len > SIZE_MAX - writer->offset) {
        return SOLC_E_OVERFLOW;
    }
    if (writer->data != NULL) {
        if (len > writer->capacity - writer->offset) {
            return SOLC_E_OUTPUT_TOO_SMALL;
        }
        if (len != 0u) {
            memcpy(writer->data + writer->offset, data, len);
        }
    }
    writer->offset += len;
    return SOLC_OK;
}

static solc_status write_u8(program_writer *writer, uint8_t value) {
    return write_bytes(writer, &value, 1u);
}

static solc_status write_u32(program_writer *writer, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
    return write_bytes(writer, bytes, sizeof(bytes));
}

static solc_status write_u64(program_writer *writer, uint64_t value) {
    uint8_t bytes[8];
    size_t i;
    for (i = 0u; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8u));
    }
    return write_bytes(writer, bytes, sizeof(bytes));
}

static int valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0u;
    if (len != 0u && data == NULL) {
        return 0;
    }
    while (i < len) {
        uint8_t first = data[i++];
        uint32_t codepoint;
        size_t continuation;
        size_t j;
        if (first <= 0x7fu) {
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = (uint32_t)(first & 0x1fu);
            continuation = 1u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = (uint32_t)(first & 0x0fu);
            continuation = 2u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = (uint32_t)(first & 0x07u);
            continuation = 3u;
        } else {
            return 0;
        }
        if (continuation > len - i) {
            return 0;
        }
        for (j = 0u; j < continuation; ++j) {
            uint8_t next = data[i++];
            if ((next & 0xc0u) != 0x80u) {
                return 0;
            }
            codepoint = (codepoint << 6u) | (uint32_t)(next & 0x3fu);
        }
        if ((continuation == 2u && codepoint < 0x800u) ||
            (continuation == 3u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) || codepoint > 0x10ffffu) {
            return 0;
        }
    }
    return 1;
}

static solc_status finish_decode(program_cursor *cursor, solc_error *error) {
    if (cursor->offset != cursor->len) {
        return fail(error, SOLC_E_TRAILING_BYTES, cursor->offset);
    }
    succeed(error, cursor->offset);
    return SOLC_OK;
}

static solc_status system_seed(program_cursor *cursor,
                               solc_slice *seed,
                               solc_error *error) {
    uint64_t length;
    const uint8_t *data;
    solc_status status = take_u64(cursor, &length);
    if (status != SOLC_OK) {
        return fail(error, status, cursor->offset);
    }
    if (length > SOLC_SYSTEM_MAX_SEED_BYTES) {
        return fail(error, SOLC_E_LIMIT_EXCEEDED, cursor->offset - 8u);
    }
    status = take(cursor, (size_t)length, &data);
    if (status != SOLC_OK) {
        return fail(error, status, cursor->offset);
    }
    if (!valid_utf8(data, (size_t)length)) {
        return fail(error, SOLC_E_INVALID_UTF8, cursor->offset - (size_t)length);
    }
    seed->data = data;
    seed->len = (size_t)length;
    return SOLC_OK;
}

solc_status solc_system_instruction_decode(const uint8_t *input,
                                           size_t input_len,
                                           solc_system_instruction *out,
                                           solc_error *error) {
    program_cursor cursor = {input, input_len, 0u};
    solc_status status;
    if ((input == NULL && input_len != 0u) || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    memset(out, 0, sizeof(*out));
    status = take_u32(&cursor, &out->kind);
    if (status != SOLC_OK) {
        return fail(error, status, cursor.offset);
    }
    if (out->kind > SOLC_SYSTEM_UPGRADE_NONCE_ACCOUNT) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    switch (out->kind) {
        case SOLC_SYSTEM_CREATE_ACCOUNT:
            if ((status = take_u64(&cursor, &out->lamports)) != SOLC_OK ||
                (status = take_u64(&cursor, &out->space)) != SOLC_OK ||
                (status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner)) != SOLC_OK) {
                return fail(error, status, cursor.offset);
            }
            break;
        case SOLC_SYSTEM_ASSIGN:
            status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner);
            break;
        case SOLC_SYSTEM_TRANSFER:
        case SOLC_SYSTEM_WITHDRAW_NONCE_ACCOUNT:
            status = take_u64(&cursor, &out->lamports);
            break;
        case SOLC_SYSTEM_CREATE_ACCOUNT_WITH_SEED:
            if ((status = take(&cursor, SOLC_PUBKEY_BYTES, &out->base)) != SOLC_OK ||
                (status = system_seed(&cursor, &out->seed, error)) != SOLC_OK ||
                (status = take_u64(&cursor, &out->lamports)) != SOLC_OK ||
                (status = take_u64(&cursor, &out->space)) != SOLC_OK ||
                (status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner)) != SOLC_OK) {
                return status == SOLC_OK ? SOLC_OK : fail(error, status, cursor.offset);
            }
            break;
        case SOLC_SYSTEM_ADVANCE_NONCE_ACCOUNT:
        case SOLC_SYSTEM_UPGRADE_NONCE_ACCOUNT:
            break;
        case SOLC_SYSTEM_INITIALIZE_NONCE_ACCOUNT:
        case SOLC_SYSTEM_AUTHORIZE_NONCE_ACCOUNT:
            status = take(&cursor, SOLC_PUBKEY_BYTES, &out->authority);
            break;
        case SOLC_SYSTEM_ALLOCATE:
            status = take_u64(&cursor, &out->space);
            break;
        case SOLC_SYSTEM_ALLOCATE_WITH_SEED:
            if ((status = take(&cursor, SOLC_PUBKEY_BYTES, &out->base)) != SOLC_OK ||
                (status = system_seed(&cursor, &out->seed, error)) != SOLC_OK ||
                (status = take_u64(&cursor, &out->space)) != SOLC_OK ||
                (status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner)) != SOLC_OK) {
                return status == SOLC_OK ? SOLC_OK : fail(error, status, cursor.offset);
            }
            break;
        case SOLC_SYSTEM_ASSIGN_WITH_SEED:
            if ((status = take(&cursor, SOLC_PUBKEY_BYTES, &out->base)) != SOLC_OK ||
                (status = system_seed(&cursor, &out->seed, error)) != SOLC_OK ||
                (status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner)) != SOLC_OK) {
                return status == SOLC_OK ? SOLC_OK : fail(error, status, cursor.offset);
            }
            break;
        case SOLC_SYSTEM_TRANSFER_WITH_SEED:
            if ((status = take_u64(&cursor, &out->lamports)) != SOLC_OK ||
                (status = system_seed(&cursor, &out->seed, error)) != SOLC_OK ||
                (status = take(&cursor, SOLC_PUBKEY_BYTES, &out->owner)) != SOLC_OK) {
                return status == SOLC_OK ? SOLC_OK : fail(error, status, cursor.offset);
            }
            break;
        default: return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    if (status != SOLC_OK) {
        return fail(error, status, cursor.offset);
    }
    return finish_decode(&cursor, error);
}

static solc_status write_system_seed(program_writer *writer, solc_slice seed) {
    solc_status status;
    if (seed.len > SOLC_SYSTEM_MAX_SEED_BYTES) {
        return SOLC_E_LIMIT_EXCEEDED;
    }
    if (!valid_utf8(seed.data, seed.len)) {
        return SOLC_E_INVALID_UTF8;
    }
    status = write_u64(writer, (uint64_t)seed.len);
    return status == SOLC_OK ? write_bytes(writer, seed.data, seed.len) : status;
}

solc_status solc_system_instruction_encode(const solc_system_instruction *instruction,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_len,
                                           solc_error *error) {
    program_writer writer = {output, output_capacity, 0u};
    solc_status status;
#define WRITE(expression)                         \
    do {                                          \
        status = (expression);                    \
        if (status != SOLC_OK) {                  \
            return fail(error, status, writer.offset); \
        }                                         \
    } while (0)
    if (instruction == NULL || output_len == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (instruction->kind > SOLC_SYSTEM_UPGRADE_NONCE_ACCOUNT) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    WRITE(write_u32(&writer, instruction->kind));
    switch (instruction->kind) {
        case SOLC_SYSTEM_CREATE_ACCOUNT:
            WRITE(write_u64(&writer, instruction->lamports));
            WRITE(write_u64(&writer, instruction->space));
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_ASSIGN:
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_TRANSFER:
        case SOLC_SYSTEM_WITHDRAW_NONCE_ACCOUNT:
            WRITE(write_u64(&writer, instruction->lamports));
            break;
        case SOLC_SYSTEM_CREATE_ACCOUNT_WITH_SEED:
            WRITE(write_bytes(&writer, instruction->base, SOLC_PUBKEY_BYTES));
            WRITE(write_system_seed(&writer, instruction->seed));
            WRITE(write_u64(&writer, instruction->lamports));
            WRITE(write_u64(&writer, instruction->space));
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_INITIALIZE_NONCE_ACCOUNT:
        case SOLC_SYSTEM_AUTHORIZE_NONCE_ACCOUNT:
            WRITE(write_bytes(&writer, instruction->authority, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_ALLOCATE:
            WRITE(write_u64(&writer, instruction->space));
            break;
        case SOLC_SYSTEM_ALLOCATE_WITH_SEED:
            WRITE(write_bytes(&writer, instruction->base, SOLC_PUBKEY_BYTES));
            WRITE(write_system_seed(&writer, instruction->seed));
            WRITE(write_u64(&writer, instruction->space));
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_ASSIGN_WITH_SEED:
            WRITE(write_bytes(&writer, instruction->base, SOLC_PUBKEY_BYTES));
            WRITE(write_system_seed(&writer, instruction->seed));
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_TRANSFER_WITH_SEED:
            WRITE(write_u64(&writer, instruction->lamports));
            WRITE(write_system_seed(&writer, instruction->seed));
            WRITE(write_bytes(&writer, instruction->owner, SOLC_PUBKEY_BYTES));
            break;
        case SOLC_SYSTEM_ADVANCE_NONCE_ACCOUNT:
        case SOLC_SYSTEM_UPGRADE_NONCE_ACCOUNT: break;
        default: return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    *output_len = writer.offset;
    succeed(error, writer.offset);
    return SOLC_OK;
#undef WRITE
}

#define SOLC_SYSTEM_SCHEMA(name, tag) [tag] = #name,
static const char *const system_names[] = {
#include "../schemas/program_instructions.def"
};
#undef SOLC_SYSTEM_SCHEMA

const char *solc_system_instruction_name(uint32_t kind) {
    return kind < sizeof(system_names) / sizeof(system_names[0]) ? system_names[kind]
                                                                 : "UNKNOWN";
}

solc_status solc_compute_budget_instruction_decode(
    const uint8_t *input,
    size_t input_len,
    solc_compute_budget_instruction *out,
    solc_error *error) {
    size_t expected;
    if ((input == NULL && input_len != 0u) || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (input_len == 0u) {
        return fail(error, SOLC_E_TRUNCATED, 0u);
    }
    memset(out, 0, sizeof(*out));
    out->kind = input[0];
    if (out->kind > SOLC_COMPUTE_BUDGET_SET_LOADED_ACCOUNTS_DATA_SIZE_LIMIT) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    expected = out->kind == SOLC_COMPUTE_BUDGET_UNUSED
                   ? 1u
                   : (out->kind == SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_PRICE ? 9u : 5u);
    if (input_len < expected) {
        return fail(error, SOLC_E_TRUNCATED, input_len);
    }
    if (input_len > expected) {
        return fail(error, SOLC_E_TRAILING_BYTES, expected);
    }
    if (expected == 5u) {
        out->value = read_u32_at(input + 1u);
    } else if (expected == 9u) {
        out->value = read_u64_at(input + 1u);
    }
    succeed(error, expected);
    return SOLC_OK;
}

solc_status solc_compute_budget_instruction_encode(
    const solc_compute_budget_instruction *instruction,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_len,
    solc_error *error) {
    program_writer writer = {output, output_capacity, 0u};
    solc_status status;
    if (instruction == NULL || output_len == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (instruction->kind > SOLC_COMPUTE_BUDGET_SET_LOADED_ACCOUNTS_DATA_SIZE_LIMIT) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    status = write_u8(&writer, instruction->kind);
    if (status == SOLC_OK && instruction->kind != SOLC_COMPUTE_BUDGET_UNUSED) {
        if (instruction->kind == SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_PRICE) {
            status = write_u64(&writer, instruction->value);
        } else if (instruction->value > UINT32_MAX) {
            status = SOLC_E_OVERFLOW;
        } else {
            status = write_u32(&writer, (uint32_t)instruction->value);
        }
    }
    if (status != SOLC_OK) {
        return fail(error, status, writer.offset);
    }
    *output_len = writer.offset;
    succeed(error, writer.offset);
    return SOLC_OK;
}

solc_status solc_compute_budget_collect(
    const solc_compute_budget_instruction *instructions,
    size_t instruction_count,
    solc_compute_budget_limits *out) {
    size_t i;
    if ((instructions == NULL && instruction_count != 0u) || out == NULL) {
        return SOLC_E_NULL;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0u; i < instruction_count; ++i) {
        const solc_compute_budget_instruction *instruction = &instructions[i];
        uint32_t bit;
        switch (instruction->kind) {
            case SOLC_COMPUTE_BUDGET_REQUEST_HEAP_FRAME:
                bit = SOLC_COMPUTE_BUDGET_HAS_HEAP_SIZE;
                if (instruction->value < SOLC_COMPUTE_BUDGET_MIN_HEAP_BYTES ||
                    instruction->value > SOLC_COMPUTE_BUDGET_MAX_HEAP_BYTES ||
                    instruction->value % 1024u != 0u) {
                    return SOLC_E_INVALID_CONFIG;
                }
                out->heap_size = (uint32_t)instruction->value;
                break;
            case SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_LIMIT:
                bit = SOLC_COMPUTE_BUDGET_HAS_COMPUTE_UNIT_LIMIT;
                if (instruction->value > UINT32_MAX) {
                    return SOLC_E_OVERFLOW;
                }
                out->compute_unit_limit = (uint32_t)instruction->value;
                break;
            case SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_PRICE:
                bit = SOLC_COMPUTE_BUDGET_HAS_PRICE;
                out->compute_unit_price_micro_lamports = instruction->value;
                break;
            case SOLC_COMPUTE_BUDGET_SET_LOADED_ACCOUNTS_DATA_SIZE_LIMIT:
                bit = SOLC_COMPUTE_BUDGET_HAS_LOADED_DATA_LIMIT;
                if (instruction->value > UINT32_MAX) {
                    return SOLC_E_OVERFLOW;
                }
                out->loaded_accounts_data_size_limit = (uint32_t)instruction->value;
                break;
            case SOLC_COMPUTE_BUDGET_UNUSED:
            default: return SOLC_E_INVALID_CONFIG;
        }
        if ((out->mask & bit) != 0u) {
            return SOLC_E_DUPLICATE_CONFIG;
        }
        out->mask |= bit;
    }
    return SOLC_OK;
}

solc_status solc_compute_budget_limits_to_v1(const solc_compute_budget_limits *limits,
                                             solc_v1_config *out) {
    uint64_t quotient;
    uint64_t remainder;
    uint64_t fee;
    uint64_t tail;
    if (limits == NULL || out == NULL) {
        return SOLC_E_NULL;
    }
    if ((limits->mask & ~(SOLC_COMPUTE_BUDGET_HAS_PRICE |
                          SOLC_COMPUTE_BUDGET_HAS_COMPUTE_UNIT_LIMIT |
                          SOLC_COMPUTE_BUDGET_HAS_LOADED_DATA_LIMIT |
                          SOLC_COMPUTE_BUDGET_HAS_HEAP_SIZE)) != 0u) {
        return SOLC_E_INVALID_CONFIG;
    }
    memset(out, 0, sizeof(*out));
    if ((limits->mask & SOLC_COMPUTE_BUDGET_HAS_PRICE) != 0u) {
        if ((limits->mask & SOLC_COMPUTE_BUDGET_HAS_COMPUTE_UNIT_LIMIT) == 0u) {
            return SOLC_E_INVALID_CONFIG;
        }
        quotient = limits->compute_unit_price_micro_lamports /
                   SOLC_MICRO_LAMPORTS_PER_LAMPORT;
        remainder = limits->compute_unit_price_micro_lamports %
                    SOLC_MICRO_LAMPORTS_PER_LAMPORT;
        if (quotient != 0u && limits->compute_unit_limit > UINT64_MAX / quotient) {
            return SOLC_E_OVERFLOW;
        }
        fee = quotient * limits->compute_unit_limit;
        tail = remainder * limits->compute_unit_limit;
        tail = tail / SOLC_MICRO_LAMPORTS_PER_LAMPORT +
               (tail % SOLC_MICRO_LAMPORTS_PER_LAMPORT != 0u ? 1u : 0u);
        if (tail > UINT64_MAX - fee) {
            return SOLC_E_OVERFLOW;
        }
        out->mask |= SOLC_V1_CONFIG_PRIORITY_FEE;
        out->priority_fee = fee + tail;
    }
    if ((limits->mask & SOLC_COMPUTE_BUDGET_HAS_COMPUTE_UNIT_LIMIT) != 0u) {
        out->mask |= SOLC_V1_CONFIG_COMPUTE_UNIT_LIMIT;
        out->compute_unit_limit = limits->compute_unit_limit;
    }
    if ((limits->mask & SOLC_COMPUTE_BUDGET_HAS_LOADED_DATA_LIMIT) != 0u) {
        out->mask |= SOLC_V1_CONFIG_LOADED_ACCOUNTS_DATA_SIZE_LIMIT;
        out->loaded_accounts_data_size_limit = limits->loaded_accounts_data_size_limit;
    }
    if ((limits->mask & SOLC_COMPUTE_BUDGET_HAS_HEAP_SIZE) != 0u) {
        if (limits->heap_size < SOLC_COMPUTE_BUDGET_MIN_HEAP_BYTES ||
            limits->heap_size > SOLC_COMPUTE_BUDGET_MAX_HEAP_BYTES ||
            limits->heap_size % 1024u != 0u) {
            return SOLC_E_INVALID_CONFIG;
        }
        out->mask |= SOLC_V1_CONFIG_HEAP_SIZE;
        out->heap_size = limits->heap_size;
    }
    return SOLC_OK;
}

#define SOLC_COMPUTE_BUDGET_SCHEMA(name, tag) [tag] = #name,
static const char *const compute_budget_names[] = {
#include "../schemas/program_instructions.def"
};
#undef SOLC_COMPUTE_BUDGET_SCHEMA

const char *solc_compute_budget_instruction_name(uint8_t kind) {
    return kind < sizeof(compute_budget_names) / sizeof(compute_budget_names[0])
               ? compute_budget_names[kind]
               : "UNKNOWN";
}

static solc_status decode_compact_pubkey_option(program_cursor *cursor,
                                                solc_optional_pubkey *out) {
    uint8_t tag;
    solc_status status = take_u8(cursor, &tag);
    if (status != SOLC_OK) {
        return status;
    }
    if (tag > 1u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    out->present = tag;
    out->key = NULL;
    return tag == 0u ? SOLC_OK : take(cursor, SOLC_PUBKEY_BYTES, &out->key);
}

static solc_status write_compact_pubkey_option(program_writer *writer,
                                               solc_optional_pubkey value) {
    solc_status status;
    if (value.present > 1u) {
        return SOLC_E_INVALID_MODEL;
    }
    status = write_u8(writer, value.present);
    if (status == SOLC_OK && value.present != 0u) {
        status = write_bytes(writer, value.key, SOLC_PUBKEY_BYTES);
    }
    return status;
}

static int token_2022_envelope(uint8_t kind) {
    return kind == 26u || kind == 27u || kind == 28u || kind == 30u || kind == 33u ||
           kind == 34u || kind == 36u || kind == 37u || kind == 39u || kind == 40u ||
           kind == 41u || kind == 42u || kind == 43u || kind == 44u;
}

static solc_status validate_extension_type_list(solc_slice list) {
    size_t i;
    if (list.len != 0u && list.data == NULL) {
        return SOLC_E_NULL;
    }
    if (list.len % 2u != 0u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    for (i = 0u; i < list.len; i += 2u) {
        if (read_u16_at(list.data + i) > SOLC_TOKEN2022_MAX_EXTENSION_TYPE) {
            return SOLC_E_INVALID_PROGRAM_DATA;
        }
    }
    return SOLC_OK;
}

solc_status solc_token_instruction_decode(solc_token_program_flavor flavor,
                                          const uint8_t *input,
                                          size_t input_len,
                                          solc_token_instruction *out,
                                          solc_error *error) {
    program_cursor cursor = {input, input_len, 0u};
    solc_status status;
    const uint8_t *bytes;
    if ((input == NULL && input_len != 0u) || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (flavor != SOLC_TOKEN_CLASSIC && flavor != SOLC_TOKEN_2022) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    memset(out, 0, sizeof(*out));
    status = take_u8(&cursor, &out->kind);
    if (status != SOLC_OK) {
        return fail(error, status, 0u);
    }
    if ((flavor == SOLC_TOKEN_CLASSIC && out->kind > 24u) ||
        (flavor == SOLC_TOKEN_2022 && out->kind > 44u)) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    switch (out->kind) {
        case 0u:
        case 20u:
            if ((status = take_u8(&cursor, &out->decimals)) == SOLC_OK) {
                status = take(&cursor, SOLC_PUBKEY_BYTES, &out->pubkey);
            }
            if (status == SOLC_OK) {
                status = decode_compact_pubkey_option(&cursor, &out->optional_pubkey);
            }
            break;
        case 1u:
        case 5u:
        case 9u:
        case 10u:
        case 11u:
        case 17u:
        case 22u: break;
        case 2u:
        case 19u:
            status = take_u8(&cursor, &out->m);
            if (status == SOLC_OK && (out->m == 0u || out->m > SOLC_TOKEN_MAX_SIGNERS)) {
                status = SOLC_E_INVALID_PROGRAM_DATA;
            }
            break;
        case 3u:
        case 4u:
        case 7u:
        case 8u:
        case 23u: status = take_u64(&cursor, &out->amount); break;
        case 6u:
            status = take_u8(&cursor, &out->authority_type);
            if (status == SOLC_OK && out->authority_type > 3u) {
                status = SOLC_E_INVALID_PROGRAM_DATA;
            }
            if (status == SOLC_OK) {
                status = decode_compact_pubkey_option(&cursor, &out->optional_pubkey);
            }
            break;
        case 12u:
        case 13u:
        case 14u:
        case 15u:
            status = take_u64(&cursor, &out->amount);
            if (status == SOLC_OK) {
                status = take_u8(&cursor, &out->decimals);
            }
            break;
        case 16u:
        case 18u:
            status = take(&cursor, SOLC_PUBKEY_BYTES, &out->pubkey);
            break;
        case 21u:
            if (flavor == SOLC_TOKEN_2022) {
                out->extension_types.data = cursor.data + cursor.offset;
                out->extension_types.len = cursor.len - cursor.offset;
                status = validate_extension_type_list(out->extension_types);
                cursor.offset = cursor.len;
            }
            break;
        case 24u:
            out->data.data = cursor.data + cursor.offset;
            out->data.len = cursor.len - cursor.offset;
            if (!valid_utf8(out->data.data, out->data.len)) {
                status = SOLC_E_INVALID_UTF8;
            }
            cursor.offset = cursor.len;
            break;
        case 25u:
            if (flavor != SOLC_TOKEN_2022) {
                status = SOLC_E_INVALID_PROGRAM_DATA;
            } else {
                status = decode_compact_pubkey_option(&cursor, &out->optional_pubkey);
            }
            break;
        case 29u:
            out->extension_types.data = cursor.data + cursor.offset;
            out->extension_types.len = cursor.len - cursor.offset;
            status = validate_extension_type_list(out->extension_types);
            cursor.offset = cursor.len;
            break;
        case 31u:
        case 32u:
        case 38u: break;
        case 35u:
            status = take(&cursor, SOLC_PUBKEY_BYTES, &out->pubkey);
            break;
        default:
            if (!token_2022_envelope(out->kind)) {
                status = SOLC_E_INVALID_PROGRAM_DATA;
                break;
            }
            status = take(&cursor, cursor.len - cursor.offset, &bytes);
            if (status == SOLC_OK) {
                out->data.data = bytes;
                out->data.len = cursor.len - 1u;
            }
            break;
    }
    if (status != SOLC_OK) {
        return fail(error, status, cursor.offset);
    }
    return finish_decode(&cursor, error);
}

solc_status solc_token_instruction_encode(solc_token_program_flavor flavor,
                                          const solc_token_instruction *instruction,
                                          uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_len,
                                          solc_error *error) {
    program_writer writer = {output, output_capacity, 0u};
    solc_status status;
#define TOKEN_WRITE(expression)                    \
    do {                                           \
        status = (expression);                     \
        if (status != SOLC_OK) {                   \
            return fail(error, status, writer.offset); \
        }                                          \
    } while (0)
    if (instruction == NULL || output_len == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if ((flavor == SOLC_TOKEN_CLASSIC && instruction->kind > 24u) ||
        (flavor == SOLC_TOKEN_2022 && instruction->kind > 44u) ||
        (flavor != SOLC_TOKEN_CLASSIC && flavor != SOLC_TOKEN_2022)) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    TOKEN_WRITE(write_u8(&writer, instruction->kind));
    switch (instruction->kind) {
        case 0u:
        case 20u:
            TOKEN_WRITE(write_u8(&writer, instruction->decimals));
            TOKEN_WRITE(write_bytes(&writer, instruction->pubkey, SOLC_PUBKEY_BYTES));
            TOKEN_WRITE(write_compact_pubkey_option(&writer, instruction->optional_pubkey));
            break;
        case 1u:
        case 5u:
        case 9u:
        case 10u:
        case 11u:
        case 17u:
        case 22u: break;
        case 2u:
        case 19u:
            if (instruction->m == 0u || instruction->m > SOLC_TOKEN_MAX_SIGNERS) {
                return fail(error, SOLC_E_INVALID_MODEL, writer.offset);
            }
            TOKEN_WRITE(write_u8(&writer, instruction->m));
            break;
        case 3u:
        case 4u:
        case 7u:
        case 8u:
        case 23u: TOKEN_WRITE(write_u64(&writer, instruction->amount)); break;
        case 6u:
            if (instruction->authority_type > 3u) {
                return fail(error, SOLC_E_INVALID_MODEL, writer.offset);
            }
            TOKEN_WRITE(write_u8(&writer, instruction->authority_type));
            TOKEN_WRITE(write_compact_pubkey_option(&writer, instruction->optional_pubkey));
            break;
        case 12u:
        case 13u:
        case 14u:
        case 15u:
            TOKEN_WRITE(write_u64(&writer, instruction->amount));
            TOKEN_WRITE(write_u8(&writer, instruction->decimals));
            break;
        case 16u:
        case 18u:
        case 35u:
            TOKEN_WRITE(write_bytes(&writer, instruction->pubkey, SOLC_PUBKEY_BYTES));
            break;
        case 21u:
            if (flavor == SOLC_TOKEN_2022) {
                TOKEN_WRITE(validate_extension_type_list(instruction->extension_types));
                TOKEN_WRITE(write_bytes(&writer,
                                        instruction->extension_types.data,
                                        instruction->extension_types.len));
            } else if (instruction->extension_types.len != 0u) {
                return fail(error, SOLC_E_INVALID_MODEL, writer.offset);
            }
            break;
        case 24u:
            if (!valid_utf8(instruction->data.data, instruction->data.len)) {
                return fail(error, SOLC_E_INVALID_UTF8, writer.offset);
            }
            TOKEN_WRITE(write_bytes(&writer, instruction->data.data, instruction->data.len));
            break;
        case 25u:
            TOKEN_WRITE(write_compact_pubkey_option(&writer, instruction->optional_pubkey));
            break;
        case 29u:
            TOKEN_WRITE(validate_extension_type_list(instruction->extension_types));
            TOKEN_WRITE(write_bytes(&writer,
                                    instruction->extension_types.data,
                                    instruction->extension_types.len));
            break;
        case 31u:
        case 32u:
        case 38u: break;
        default:
            if (!token_2022_envelope(instruction->kind)) {
                return fail(error, SOLC_E_INVALID_MODEL, writer.offset);
            }
            TOKEN_WRITE(write_bytes(&writer, instruction->data.data, instruction->data.len));
            break;
    }
    *output_len = writer.offset;
    succeed(error, writer.offset);
    return SOLC_OK;
#undef TOKEN_WRITE
}

#define SOLC_TOKEN_SCHEMA(name, tag) [tag] = #name,
#define SOLC_TOKEN_2022_SCHEMA(name, tag) [tag] = #name,
static const char *const token_names[] = {
#include "../schemas/program_instructions.def"
};
#undef SOLC_TOKEN_SCHEMA
#undef SOLC_TOKEN_2022_SCHEMA

const char *solc_token_instruction_name(solc_token_program_flavor flavor, uint8_t kind) {
    if (flavor == SOLC_TOKEN_CLASSIC && kind > 24u) {
        return "UNKNOWN";
    }
    return kind < sizeof(token_names) / sizeof(token_names[0]) ? token_names[kind]
                                                               : "UNKNOWN";
}

static solc_status decode_coption_pubkey(const uint8_t *data,
                                        solc_optional_pubkey *out) {
    uint32_t tag = read_u32_at(data);
    size_t i;
    if (tag > 1u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    if (tag == 0u) {
        for (i = 4u; i < 36u; ++i) {
            if (data[i] != 0u) {
                return SOLC_E_NON_CANONICAL;
            }
        }
    }
    out->present = (uint8_t)tag;
    out->key = tag == 0u ? NULL : data + 4u;
    return SOLC_OK;
}

static solc_status decode_coption_u64(const uint8_t *data, solc_optional_u64 *out) {
    uint32_t tag = read_u32_at(data);
    size_t i;
    if (tag > 1u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    if (tag == 0u) {
        for (i = 4u; i < 12u; ++i) {
            if (data[i] != 0u) {
                return SOLC_E_NON_CANONICAL;
            }
        }
    }
    out->present = (uint8_t)tag;
    out->value = tag == 0u ? 0u : read_u64_at(data + 4u);
    return SOLC_OK;
}

solc_status solc_token_mint_decode(const uint8_t *input,
                                   size_t input_len,
                                   solc_token_mint *out) {
    solc_status status;
    if (input == NULL || out == NULL) {
        return SOLC_E_NULL;
    }
    if (input_len != SOLC_TOKEN_MINT_BYTES) {
        return input_len < SOLC_TOKEN_MINT_BYTES ? SOLC_E_TRUNCATED : SOLC_E_TRAILING_BYTES;
    }
    memset(out, 0, sizeof(*out));
    status = decode_coption_pubkey(input, &out->mint_authority);
    if (status != SOLC_OK) {
        return status;
    }
    out->supply = read_u64_at(input + 36u);
    out->decimals = input[44u];
    if (input[45u] > 1u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    out->is_initialized = input[45u];
    return decode_coption_pubkey(input + 46u, &out->freeze_authority);
}

solc_status solc_token_account_decode(const uint8_t *input,
                                      size_t input_len,
                                      solc_token_account *out) {
    solc_status status;
    if (input == NULL || out == NULL) {
        return SOLC_E_NULL;
    }
    if (input_len != SOLC_TOKEN_ACCOUNT_BYTES) {
        return input_len < SOLC_TOKEN_ACCOUNT_BYTES ? SOLC_E_TRUNCATED : SOLC_E_TRAILING_BYTES;
    }
    memset(out, 0, sizeof(*out));
    out->mint = input;
    out->owner = input + 32u;
    out->amount = read_u64_at(input + 64u);
    status = decode_coption_pubkey(input + 72u, &out->delegate);
    if (status != SOLC_OK) {
        return status;
    }
    if (input[108u] > 2u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    out->state = input[108u];
    status = decode_coption_u64(input + 109u, &out->is_native);
    if (status != SOLC_OK) {
        return status;
    }
    out->delegated_amount = read_u64_at(input + 121u);
    return decode_coption_pubkey(input + 129u, &out->close_authority);
}

solc_status solc_token_multisig_decode(const uint8_t *input,
                                       size_t input_len,
                                       solc_token_multisig *out) {
    if (input == NULL || out == NULL) {
        return SOLC_E_NULL;
    }
    if (input_len != SOLC_TOKEN_MULTISIG_BYTES) {
        return input_len < SOLC_TOKEN_MULTISIG_BYTES ? SOLC_E_TRUNCATED
                                                     : SOLC_E_TRAILING_BYTES;
    }
    if (input[2u] > 1u) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    out->m = input[0u];
    out->n = input[1u];
    out->is_initialized = input[2u];
    out->signers.data = input + 3u;
    out->signers.len = SOLC_TOKEN_MAX_SIGNERS * SOLC_PUBKEY_BYTES;
    return SOLC_OK;
}

static int extension_matches_account(uint16_t extension_type, uint8_t account_kind) {
    static const uint32_t account_extensions =
        (UINT32_C(1) << 2u) | (UINT32_C(1) << 5u) | (UINT32_C(1) << 7u) |
        (UINT32_C(1) << 8u) | (UINT32_C(1) << 11u) | (UINT32_C(1) << 13u) |
        (UINT32_C(1) << 15u) | (UINT32_C(1) << 17u) | (UINT32_C(1) << 27u);
    uint32_t bit = UINT32_C(1) << extension_type;
    return account_kind == SOLC_TOKEN2022_ACCOUNT ? (account_extensions & bit) != 0u
                                                   : (account_extensions & bit) == 0u;
}

static solc_status scan_token_tlv(uint8_t account_kind,
                                  solc_slice tlv,
                                  size_t *used_len,
                                  size_t *extension_count,
                                  solc_error *error,
                                  size_t base_offset) {
    size_t offset = 0u;
    size_t count = 0u;
    uint32_t seen = 0u;
    while (offset < tlv.len) {
        uint16_t extension_type;
        uint16_t value_len;
        size_t i;
        if (tlv.len - offset < 2u) {
            for (i = offset; i < tlv.len; ++i) {
                if (tlv.data[i] != 0u) {
                    return fail(error, SOLC_E_INVALID_PROGRAM_DATA, base_offset + i);
                }
            }
            break;
        }
        extension_type = read_u16_at(tlv.data + offset);
        if (extension_type == 0u) {
            for (i = offset; i < tlv.len; ++i) {
                if (tlv.data[i] != 0u) {
                    return fail(error, SOLC_E_INVALID_PROGRAM_DATA, base_offset + i);
                }
            }
            break;
        }
        if (extension_type > SOLC_TOKEN2022_MAX_EXTENSION_TYPE) {
            return fail(error, SOLC_E_INVALID_PROGRAM_DATA, base_offset + offset);
        }
        if (!extension_matches_account(extension_type, account_kind)) {
            return fail(error, SOLC_E_INVALID_PROGRAM_DATA, base_offset + offset);
        }
        if ((seen & (UINT32_C(1) << extension_type)) != 0u) {
            return fail(error, SOLC_E_NON_CANONICAL, base_offset + offset);
        }
        seen |= UINT32_C(1) << extension_type;
        if (tlv.len - offset < 4u) {
            return fail(error, SOLC_E_TRUNCATED, base_offset + tlv.len);
        }
        value_len = read_u16_at(tlv.data + offset + 2u);
        if ((size_t)value_len > tlv.len - offset - 4u) {
            return fail(error, SOLC_E_TRUNCATED, base_offset + offset + 4u);
        }
        offset += 4u + (size_t)value_len;
        ++count;
    }
    *used_len = offset;
    *extension_count = count;
    return SOLC_OK;
}

solc_status solc_token2022_state_decode(uint8_t expected_account_kind,
                                        const uint8_t *input,
                                        size_t input_len,
                                        solc_token2022_state *out,
                                        solc_error *error) {
    size_t base_len;
    size_t i;
    solc_status status;
    solc_token_mint mint;
    solc_token_account account;
    if (input == NULL || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (expected_account_kind != SOLC_TOKEN2022_MINT &&
        expected_account_kind != SOLC_TOKEN2022_ACCOUNT) {
        return fail(error, SOLC_E_INVALID_MODEL, 0u);
    }
    base_len = expected_account_kind == SOLC_TOKEN2022_MINT ? SOLC_TOKEN_MINT_BYTES
                                                            : SOLC_TOKEN_ACCOUNT_BYTES;
    if (input_len < base_len) {
        return fail(error, SOLC_E_TRUNCATED, input_len);
    }
    status = expected_account_kind == SOLC_TOKEN2022_MINT
                 ? solc_token_mint_decode(input, base_len, &mint)
                 : solc_token_account_decode(input, base_len, &account);
    if (status != SOLC_OK) {
        return fail(error, status, 0u);
    }
    if (input_len == base_len) {
        out->account_kind = expected_account_kind;
        out->base.data = input;
        out->base.len = base_len;
        out->tlv.data = NULL;
        out->tlv.len = 0u;
        out->tlv_used_len = 0u;
        out->extension_count = 0u;
        succeed(error, input_len);
        return SOLC_OK;
    }
    if (input_len < SOLC_TOKEN_ACCOUNT_BYTES + 2u ||
        input_len == SOLC_TOKEN_MULTISIG_BYTES) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, input_len);
    }
    for (i = base_len; i < SOLC_TOKEN_ACCOUNT_BYTES; ++i) {
        if (input[i] != 0u) {
            return fail(error, SOLC_E_NON_CANONICAL, i);
        }
    }
    if (input[SOLC_TOKEN_ACCOUNT_BYTES] != expected_account_kind) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, SOLC_TOKEN_ACCOUNT_BYTES);
    }
    out->account_kind = expected_account_kind;
    out->base.data = input;
    out->base.len = base_len;
    out->tlv.data = input + SOLC_TOKEN_ACCOUNT_BYTES + 1u;
    out->tlv.len = input_len - SOLC_TOKEN_ACCOUNT_BYTES - 1u;
    status = scan_token_tlv(expected_account_kind,
                            out->tlv,
                            &out->tlv_used_len,
                            &out->extension_count,
                            error,
                            SOLC_TOKEN_ACCOUNT_BYTES + 1u);
    if (status == SOLC_OK) {
        succeed(error, input_len);
    }
    return status;
}

void solc_token2022_tlv_iterator_init(const solc_token2022_state *state,
                                      solc_token2022_tlv_iterator *iterator) {
    if (iterator != NULL) {
        memset(iterator, 0, sizeof(*iterator));
        if (state != NULL) {
            iterator->tlv = state->tlv;
        }
    }
}

solc_status solc_token2022_tlv_next(solc_token2022_tlv_iterator *iterator,
                                    solc_token2022_tlv_entry *entry,
                                    uint8_t *has_entry) {
    uint16_t value_len;
    if (iterator == NULL || entry == NULL || has_entry == NULL) {
        return SOLC_E_NULL;
    }
    *has_entry = 0u;
    if (iterator->finished != 0u || iterator->offset >= iterator->tlv.len) {
        return SOLC_OK;
    }
    if (iterator->tlv.len - iterator->offset < 2u ||
        read_u16_at(iterator->tlv.data + iterator->offset) == 0u) {
        iterator->finished = 1u;
        return SOLC_OK;
    }
    if (iterator->tlv.len - iterator->offset < 4u) {
        return SOLC_E_TRUNCATED;
    }
    entry->extension_type = read_u16_at(iterator->tlv.data + iterator->offset);
    if (entry->extension_type > SOLC_TOKEN2022_MAX_EXTENSION_TYPE) {
        return SOLC_E_INVALID_PROGRAM_DATA;
    }
    value_len = read_u16_at(iterator->tlv.data + iterator->offset + 2u);
    if ((size_t)value_len > iterator->tlv.len - iterator->offset - 4u) {
        return SOLC_E_TRUNCATED;
    }
    entry->value.data = iterator->tlv.data + iterator->offset + 4u;
    entry->value.len = value_len;
    iterator->offset += 4u + value_len;
    *has_entry = 1u;
    return SOLC_OK;
}

solc_status solc_address_lookup_table_decode(const uint8_t *input,
                                             size_t input_len,
                                             solc_address_lookup_table *out,
                                             solc_error *error) {
    uint32_t state;
    uint8_t authority_tag;
    size_t metadata_used;
    size_t i;
    if (input == NULL || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (input_len < SOLC_ALT_META_BYTES) {
        return fail(error, SOLC_E_TRUNCATED, input_len);
    }
    state = read_u32_at(input);
    if (state == 0u) {
        return fail(error, SOLC_E_UNINITIALIZED_ACCOUNT, 0u);
    }
    if (state != 1u) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    if ((input_len - SOLC_ALT_META_BYTES) % SOLC_PUBKEY_BYTES != 0u) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, SOLC_ALT_META_BYTES);
    }
    memset(out, 0, sizeof(*out));
    out->deactivation_slot = read_u64_at(input + 4u);
    out->last_extended_slot = read_u64_at(input + 12u);
    out->last_extended_slot_start_index = input[20u];
    authority_tag = input[21u];
    if (authority_tag > 1u) {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 21u);
    }
    out->authority.present = authority_tag;
    out->authority.key = authority_tag == 0u ? NULL : input + 22u;
    metadata_used = authority_tag == 0u ? 24u : 56u;
    if (input[metadata_used - 2u] != 0u || input[metadata_used - 1u] != 0u) {
        return fail(error, SOLC_E_NON_CANONICAL, metadata_used - 2u);
    }
    for (i = metadata_used; i < SOLC_ALT_META_BYTES; ++i) {
        if (input[i] != 0u) {
            return fail(error, SOLC_E_NON_CANONICAL, i);
        }
    }
    out->addresses.data = input + SOLC_ALT_META_BYTES;
    out->addresses.len = input_len - SOLC_ALT_META_BYTES;
    out->address_count = out->addresses.len / SOLC_PUBKEY_BYTES;
    if (out->address_count > SOLC_ALT_MAX_ADDRESSES ||
        out->last_extended_slot_start_index > out->address_count) {
        return fail(error, SOLC_E_LIMIT_EXCEEDED, 20u);
    }
    succeed(error, input_len);
    return SOLC_OK;
}

size_t solc_address_lookup_table_visible_len(const solc_address_lookup_table *table,
                                             uint64_t current_slot) {
    if (table == NULL) {
        return 0u;
    }
    return current_slot > table->last_extended_slot
               ? table->address_count
               : table->last_extended_slot_start_index;
}

solc_status solc_address_lookup_table_address(const solc_address_lookup_table *table,
                                              uint8_t index,
                                              const uint8_t **address) {
    if (table == NULL || address == NULL) {
        return SOLC_E_NULL;
    }
    if ((size_t)index >= table->address_count) {
        return SOLC_E_INVALID_INDEX;
    }
    *address = table->addresses.data + (size_t)index * SOLC_PUBKEY_BYTES;
    return SOLC_OK;
}
