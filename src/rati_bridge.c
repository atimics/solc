#include "solc/rati_bridge.h"

#include <string.h>

static const uint8_t TOKEN_PROGRAM_ID[SOLC_PUBKEY_BYTES] = {
    0x06u, 0xddu, 0xf6u, 0xe1u, 0xd7u, 0x65u, 0xa1u, 0x93u,
    0xd9u, 0xcbu, 0xe1u, 0x46u, 0xceu, 0xbeu, 0xb7u, 0x9au,
    0xc1u, 0xcbu, 0x48u, 0x5eu, 0xd5u, 0xf5u, 0xb3u, 0x79u,
    0x13u, 0xa8u, 0xcfu, 0x58u, 0x57u, 0xefu, 0xf0u, 0x0au,
};

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint64_t read_u64(const uint8_t *bytes) {
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value;
}

static solc_status fail(solc_error *error, solc_status status, size_t offset) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
    }
    return status;
}

solc_status solc_rati_bridge_instruction_decode(
    const uint8_t *input,
    size_t input_len,
    solc_rati_bridge_instruction *out,
    solc_error *error) {
    size_t exact_len;
    uint16_t event_len;
    uint16_t payload_len;
    if (input == NULL || out == NULL) {
        return fail(error, SOLC_E_NULL, 0u);
    }
    if (input_len == 0u) {
        return fail(error, SOLC_E_TRUNCATED, 0u);
    }
    memset(out, 0, sizeof(*out));
    out->kind = input[0];
    if (out->kind == SOLC_RATI_BRIDGE_INIT) {
        if (input_len != 1u) {
            return fail(error, SOLC_E_TRAILING_BYTES, 1u);
        }
    } else if (out->kind == SOLC_RATI_BRIDGE_ATTESTATION) {
        if (input_len < 81u) {
            return fail(error, SOLC_E_TRUNCATED, input_len);
        }
        if (input_len != 81u) {
            return fail(error, SOLC_E_TRAILING_BYTES, 81u);
        }
        out->station_pubkey = input + 1u;
        out->epoch = read_u64(input + 33u);
        out->amount = read_u64(input + 41u);
        out->target = input + 49u;
    } else if (out->kind == SOLC_RATI_BRIDGE_CHAIN_PROOF) {
        if (input_len < 35u) {
            return fail(error, SOLC_E_TRUNCATED, input_len);
        }
        event_len = read_u16(input + 33u);
        if ((size_t)event_len > input_len - 35u) {
            return fail(error, SOLC_E_TRUNCATED, input_len);
        }
        exact_len = 35u + (size_t)event_len;
        if (SOLC_RATI_ED25519_SIGNATURE_BYTES > input_len - exact_len) {
            return fail(error, SOLC_E_TRUNCATED, input_len);
        }
        exact_len += SOLC_RATI_ED25519_SIGNATURE_BYTES;
        if (input_len != exact_len) {
            return fail(error, SOLC_E_TRAILING_BYTES, exact_len);
        }
        if (event_len < SOLC_RATI_CHAIN_EVENT_HEADER_BYTES) {
            return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 35u);
        }
        payload_len = read_u16(input + 35u + 49u);
        if ((size_t)payload_len !=
            (size_t)event_len - SOLC_RATI_CHAIN_EVENT_HEADER_BYTES) {
            return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 35u + 49u);
        }
        out->station_pubkey = input + 1u;
        out->event.data = input + 35u;
        out->event.len = event_len;
        out->signature = input + 35u + event_len;
        out->event_id = read_u64(out->event.data);
        out->event_timestamp = read_u64(out->event.data + 40u);
        out->event_type = out->event.data[48u];
        out->event_payload.data = out->event.data + SOLC_RATI_CHAIN_EVENT_HEADER_BYTES;
        out->event_payload.len = payload_len;
    } else {
        return fail(error, SOLC_E_INVALID_PROGRAM_DATA, 0u);
    }
    if (error != NULL) {
        error->status = SOLC_OK;
        error->offset = input_len;
    }
    return SOLC_OK;
}

static solc_status build_checked_cpi(
    uint8_t kind,
    const solc_sbf_account *token_program,
    const solc_sbf_account *first,
    const solc_sbf_account *second,
    const solc_sbf_account *authority,
    uint64_t amount,
    int authority_via_pda,
    solc_rati_token_cpi *out) {
    solc_token_instruction token_instruction;
    solc_cpi_builder builder;
    solc_error error;
    const solc_sbf_account **account_infos;
    size_t data_len = 0u;
    solc_status status;

    if (token_program == NULL || first == NULL || second == NULL ||
        authority == NULL || out == NULL) {
        return SOLC_E_NULL;
    }
    if (amount == 0u || (kind != 14u && kind != 15u)) {
        return SOLC_E_INVALID_MODEL;
    }
    status = solc_sbf_account_require_key(token_program, TOKEN_PROGRAM_ID);
    if (status != SOLC_OK || token_program->executable == 0u) {
        return status != SOLC_OK ? status : SOLC_E_INVALID_MODEL;
    }
    status = solc_sbf_account_require_owner(first, TOKEN_PROGRAM_ID);
    if (status != SOLC_OK) {
        return status;
    }
    status = solc_sbf_account_require_owner(second, TOKEN_PROGRAM_ID);
    if (status != SOLC_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    memset(&token_instruction, 0, sizeof(token_instruction));
    token_instruction.kind = kind;
    token_instruction.amount = amount;
    token_instruction.decimals = SOLC_RATI_TOKEN_DECIMALS;
    status = solc_token_instruction_encode(SOLC_TOKEN_CLASSIC,
                                           &token_instruction,
                                           out->data,
                                           sizeof(out->data),
                                           &data_len,
                                           &error);
    if (status != SOLC_OK) {
        return status;
    }
    status = solc_cpi_builder_init(&builder,
                                   token_program,
                                   (solc_slice){out->data, data_len},
                                   out->metas,
                                   3u,
                                   out->account_infos,
                                   4u);
    if (status == SOLC_OK) {
        status = solc_cpi_builder_add_account(&builder, first, 1, 0, 0);
    }
    if (status == SOLC_OK) {
        status = solc_cpi_builder_add_account(&builder, second, 1, 0, 0);
    }
    if (status == SOLC_OK) {
        status = solc_cpi_builder_add_account(
            &builder, authority, 0, 1, authority_via_pda);
    }
    if (status == SOLC_OK) {
        status = solc_cpi_builder_finish(
            &builder, &out->instruction, &account_infos, &out->account_info_count);
    }
    if (status != SOLC_OK) {
        memset(out, 0, sizeof(*out));
        return status;
    }
    if (account_infos != out->account_infos) {
        memset(out, 0, sizeof(*out));
        return SOLC_E_INVALID_MODEL;
    }
    return SOLC_OK;
}

solc_status solc_rati_mint_checked_cpi(
    const solc_sbf_account *token_program,
    const solc_sbf_account *mint,
    const solc_sbf_account *destination,
    const solc_sbf_account *authority,
    uint64_t amount,
    int authority_via_pda,
    solc_rati_token_cpi *out) {
    return build_checked_cpi(14u,
                             token_program,
                             mint,
                             destination,
                             authority,
                             amount,
                             authority_via_pda,
                             out);
}

solc_status solc_rati_burn_checked_cpi(
    const solc_sbf_account *token_program,
    const solc_sbf_account *token_account,
    const solc_sbf_account *mint,
    const solc_sbf_account *authority,
    uint64_t amount,
    int authority_via_pda,
    solc_rati_token_cpi *out) {
    return build_checked_cpi(15u,
                             token_program,
                             token_account,
                             mint,
                             authority,
                             amount,
                             authority_via_pda,
                             out);
}
