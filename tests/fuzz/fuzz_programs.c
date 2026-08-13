#include "solc/programs.h"
#include "solc/rati_bridge.h"

#include <stdint.h>
#include <stdlib.h>

static void roundtrip_system(const uint8_t *data, size_t size) {
    solc_system_instruction instruction;
    solc_error error;
    uint8_t output[160];
    size_t output_len = 0u;
    if (solc_system_instruction_decode(data, size, &instruction, &error) == SOLC_OK) {
        if (solc_system_instruction_encode(
                &instruction, output, sizeof(output), &output_len, &error) != SOLC_OK ||
            output_len != size) {
            abort();
        }
        for (size_t i = 0u; i < size; ++i) {
            if (output[i] != data[i]) {
                abort();
            }
        }
    }
}

static void roundtrip_compute(const uint8_t *data, size_t size) {
    solc_compute_budget_instruction instruction;
    solc_error error;
    uint8_t output[9];
    size_t output_len = 0u;
    if (solc_compute_budget_instruction_decode(data, size, &instruction, &error) == SOLC_OK) {
        if (solc_compute_budget_instruction_encode(
                &instruction, output, sizeof(output), &output_len, &error) != SOLC_OK ||
            output_len != size) {
            abort();
        }
        for (size_t i = 0u; i < size; ++i) {
            if (output[i] != data[i]) {
                abort();
            }
        }
    }
}

static void roundtrip_token(solc_token_program_flavor flavor,
                            const uint8_t *data,
                            size_t size) {
    solc_token_instruction instruction;
    solc_error error;
    uint8_t output[4096];
    size_t output_len = 0u;
    if (size > sizeof(output)) {
        return;
    }
    if (solc_token_instruction_decode(flavor, data, size, &instruction, &error) == SOLC_OK) {
        if (solc_token_instruction_encode(
                flavor, &instruction, output, sizeof(output), &output_len, &error) != SOLC_OK ||
            output_len != size) {
            abort();
        }
        for (size_t i = 0u; i < size; ++i) {
            if (output[i] != data[i]) {
                abort();
            }
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    solc_token_mint mint;
    solc_token_account account;
    solc_token_multisig multisig;
    solc_token2022_state extended;
    solc_address_lookup_table lookup;
    solc_error error;
    if (size == 0u) {
        return 0;
    }
    switch (data[0] % 10u) {
        case 0u: roundtrip_system(data + 1u, size - 1u); break;
        case 1u: roundtrip_compute(data + 1u, size - 1u); break;
        case 2u: roundtrip_token(SOLC_TOKEN_CLASSIC, data + 1u, size - 1u); break;
        case 3u: roundtrip_token(SOLC_TOKEN_2022, data + 1u, size - 1u); break;
        case 4u: (void)solc_token_mint_decode(data + 1u, size - 1u, &mint); break;
        case 5u: (void)solc_token_account_decode(data + 1u, size - 1u, &account); break;
        case 6u: (void)solc_token_multisig_decode(data + 1u, size - 1u, &multisig); break;
        case 7u:
            (void)solc_token2022_state_decode(
                SOLC_TOKEN2022_MINT, data + 1u, size - 1u, &extended, &error);
            (void)solc_token2022_state_decode(
                SOLC_TOKEN2022_ACCOUNT, data + 1u, size - 1u, &extended, &error);
            break;
        case 8u:
            (void)solc_address_lookup_table_decode(
                data + 1u, size - 1u, &lookup, &error);
            break;
        default: {
            solc_rati_bridge_instruction instruction;
            (void)solc_rati_bridge_instruction_decode(
                data + 1u, size - 1u, &instruction, &error);
            break;
        }
    }
    return 0;
}
