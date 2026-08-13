#include "solc/builder.h"
#include "solc/programs.h"
#include "solc/rati_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static int hex_nibble(int value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static size_t load_hex_fixture(const char *relative_path,
                               uint8_t *output,
                               size_t output_capacity) {
    char path[1024];
    FILE *file;
    int high = -1;
    int character;
    int comment = 0;
    size_t length = 0u;
    if (snprintf(path, sizeof(path), "%s/%s", SOLC_SOURCE_DIR, relative_path) < 0) {
        return 0u;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0u;
    }
    while ((character = fgetc(file)) != EOF) {
        int nibble;
        if (comment != 0) {
            if (character == '\n') {
                comment = 0;
            }
            continue;
        }
        if (character == '#') {
            comment = 1;
            continue;
        }
        nibble = hex_nibble(character);
        if (nibble < 0) {
            continue;
        }
        if (high < 0) {
            high = nibble;
        } else {
            if (length >= output_capacity) {
                fclose(file);
                return 0u;
            }
            output[length++] = (uint8_t)((unsigned int)high << 4u |
                                         (unsigned int)nibble);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 ? length : 0u;
}

static void test_signal_packnft_transaction_parity(void) {
    uint8_t payer[SOLC_PUBKEY_BYTES];
    uint8_t program[SOLC_PUBKEY_BYTES];
    uint8_t writable[SOLC_PUBKEY_BYTES];
    uint8_t blockhash[SOLC_HASH_BYTES];
    uint8_t signatures[SOLC_SIGNATURE_BYTES] = {0u};
    static const uint8_t data[] = {0xdeu, 0xadu, 0xbeu, 0xefu};
    solc_transaction_account_meta account;
    solc_transaction_instruction instruction;
    uint8_t discovered[4u * SOLC_PUBKEY_BYTES];
    uint8_t ordered[4u * SOLC_PUBKEY_BYTES];
    uint8_t flags[4];
    uint8_t old_to_new[4];
    solc_compiled_instruction compiled[1];
    uint8_t indices[1];
    solc_legacy_builder_scratch scratch = {
        discovered, ordered, flags, old_to_new, 4u, compiled, 1u, indices, 1u,
    };
    solc_transaction transaction;
    solc_error error;
    uint8_t encoded[512];
    uint8_t expected[512];
    size_t encoded_len = 0u;
    size_t expected_len;

    memset(payer, 0x01, sizeof(payer));
    memset(program, 0x02, sizeof(program));
    memset(writable, 0x03, sizeof(writable));
    memset(blockhash, 0xab, sizeof(blockhash));
    account.pubkey = writable;
    account.is_signer = 0u;
    account.is_writable = 1u;
    instruction.program_id = program;
    instruction.accounts = &account;
    instruction.account_count = 1u;
    instruction.data.data = data;
    instruction.data.len = sizeof(data);

    CHECK(solc_legacy_transaction_build(payer,
                                        blockhash,
                                        (solc_slice){signatures, sizeof(signatures)},
                                        &instruction,
                                        1u,
                                        &scratch,
                                        &transaction,
                                        &error) == SOLC_OK);
    CHECK(transaction.message.num_required_signatures == 1u);
    CHECK(transaction.message.num_readonly_unsigned_accounts == 1u);
    CHECK(transaction.message.static_account_keys.len == 96u);
    CHECK(transaction.message.static_account_keys.data[0] == 0x01u);
    CHECK(transaction.message.static_account_keys.data[32u] == 0x03u);
    CHECK(transaction.message.static_account_keys.data[64u] == 0x02u);
    CHECK(transaction.message.instructions[0].program_id_index == 2u);
    CHECK(transaction.message.instructions[0].account_indices.data[0] == 1u);
    CHECK(solc_transaction_encode(&transaction,
                                  encoded,
                                  sizeof(encoded),
                                  &encoded_len,
                                  &error) == SOLC_OK);
    expected_len = load_hex_fixture(
        "tests/vectors/migrations/signal-packnft-legacy.hex",
        expected,
        sizeof(expected));
    CHECK(expected_len == 206u);
    CHECK(encoded_len == expected_len);
    CHECK(memcmp(encoded, expected, expected_len) == 0);

    CHECK(solc_legacy_transaction_build(payer,
                                        blockhash,
                                        (solc_slice){NULL, 0u},
                                        &instruction,
                                        1u,
                                        &scratch,
                                        &transaction,
                                        &error) == SOLC_E_SIGNATURE_MISMATCH);

    instruction.accounts = NULL;
    instruction.account_count = 0u;
    scratch.account_indices = NULL;
    scratch.account_index_capacity = 0u;
    CHECK(solc_legacy_transaction_build(payer,
                                        blockhash,
                                        (solc_slice){signatures, sizeof(signatures)},
                                        &instruction,
                                        1u,
                                        &scratch,
                                        &transaction,
                                        &error) == SOLC_OK);
    CHECK(transaction.message.instructions[0].account_indices.data == NULL);
    CHECK(transaction.message.instructions[0].account_indices.len == 0u);
}

static void test_rati_instruction_views(void) {
    uint8_t init[] = {SOLC_RATI_BRIDGE_INIT};
    uint8_t attestation[256];
    uint8_t proof[256];
    uint8_t invalid_proof[256];
    size_t attestation_len;
    size_t proof_len;
    size_t invalid_proof_len;
    solc_rati_bridge_instruction instruction;
    solc_error error;

    CHECK(solc_rati_bridge_instruction_decode(
              init, sizeof(init), &instruction, &error) == SOLC_OK);
    CHECK(instruction.kind == SOLC_RATI_BRIDGE_INIT);
    CHECK(solc_rati_bridge_instruction_decode(
              (uint8_t[]){0u, 0u}, 2u, &instruction, &error) ==
          SOLC_E_TRAILING_BYTES);

    attestation_len = load_hex_fixture(
        "tests/vectors/migrations/rati-attestation.hex",
        attestation,
        sizeof(attestation));
    CHECK(attestation_len == 81u);
    CHECK(solc_rati_bridge_instruction_decode(attestation,
                                              attestation_len,
                                              &instruction,
                                              &error) == SOLC_OK);
    CHECK(instruction.epoch == 42u);
    CHECK(instruction.amount == UINT64_C(1000000000000));
    CHECK(instruction.station_pubkey[0] == 0x11u);
    CHECK(instruction.target[0] == 0x22u);
    CHECK(solc_rati_bridge_instruction_decode(attestation,
                                              attestation_len - 1u,
                                              &instruction,
                                              &error) == SOLC_E_TRUNCATED);

    proof_len = load_hex_fixture("tests/vectors/migrations/rati-chain-proof.hex",
                                 proof,
                                 sizeof(proof));
    CHECK(proof_len == 230u);
    CHECK(solc_rati_bridge_instruction_decode(
              proof, proof_len, &instruction, &error) == SOLC_OK);
    CHECK(instruction.kind == SOLC_RATI_BRIDGE_CHAIN_PROOF);
    CHECK(instruction.event.len == 131u);
    CHECK(instruction.event_payload.len == 80u);
    CHECK(instruction.event_payload.data[64u] == 7u);
    CHECK(instruction.event_id == 99u);
    CHECK(instruction.event_timestamp == 123456u);
    CHECK(instruction.event_type == 1u);
    CHECK(instruction.signature[0] == 0x44u);
    CHECK(solc_rati_bridge_instruction_decode(
              proof, proof_len - 1u, &instruction, &error) == SOLC_E_TRUNCATED);
    invalid_proof_len = load_hex_fixture(
        "tests/vectors/migrations/rati-chain-proof-payload-mismatch.invalid.hex",
        invalid_proof,
        sizeof(invalid_proof));
    CHECK(invalid_proof_len == proof_len);
    CHECK(solc_rati_bridge_instruction_decode(invalid_proof,
                                              invalid_proof_len,
                                              &instruction,
                                              &error) ==
          SOLC_E_INVALID_PROGRAM_DATA);
}

static void test_rati_checked_cpi(void) {
    static const uint8_t token_key[SOLC_PUBKEY_BYTES] = {
        0x06u, 0xddu, 0xf6u, 0xe1u, 0xd7u, 0x65u, 0xa1u, 0x93u,
        0xd9u, 0xcbu, 0xe1u, 0x46u, 0xceu, 0xbeu, 0xb7u, 0x9au,
        0xc1u, 0xcbu, 0x48u, 0x5eu, 0xd5u, 0xf5u, 0xb3u, 0x79u,
        0x13u, 0xa8u, 0xcfu, 0x58u, 0x57u, 0xefu, 0xf0u, 0x0au,
    };
    uint8_t mint_key[32] = {1u};
    uint8_t destination_key[32] = {2u};
    uint8_t authority_key[32] = {3u};
    solc_sbf_account program = {0};
    solc_sbf_account mint = {0};
    solc_sbf_account destination = {0};
    solc_sbf_account authority = {0};
    solc_rati_token_cpi cpi;
    solc_token_instruction decoded;
    solc_error error;

    program.key = token_key;
    program.executable = 1u;
    mint.key = mint_key;
    mint.owner = (uint8_t *)token_key;
    mint.is_writable = 1u;
    destination.key = destination_key;
    destination.owner = (uint8_t *)token_key;
    destination.is_writable = 1u;
    authority.key = authority_key;

    CHECK(solc_rati_mint_checked_cpi(&program,
                                     &mint,
                                     &destination,
                                     &authority,
                                     UINT64_C(999000000000),
                                     1,
                                     &cpi) == SOLC_OK);
    CHECK(cpi.data[0] == 14u);
    CHECK(cpi.data[9] == SOLC_RATI_TOKEN_DECIMALS);
    CHECK(cpi.instruction.account_count == 3u);
    CHECK(cpi.instruction.accounts[0].pubkey == mint_key);
    CHECK(cpi.instruction.accounts[1].pubkey == destination_key);
    CHECK(cpi.instruction.accounts[2].is_signer == 1u);
    CHECK(cpi.account_info_count == 4u);
    CHECK(cpi.account_infos[3] == &program);
    CHECK(solc_token_instruction_decode(SOLC_TOKEN_CLASSIC,
                                        cpi.data,
                                        sizeof(cpi.data),
                                        &decoded,
                                        &error) == SOLC_OK);
    CHECK(decoded.kind == 14u);
    CHECK(decoded.amount == UINT64_C(999000000000));
    CHECK(decoded.decimals == SOLC_RATI_TOKEN_DECIMALS);

    CHECK(solc_rati_burn_checked_cpi(&program,
                                     &destination,
                                     &mint,
                                     &authority,
                                     UINT64_C(1000000000),
                                     1,
                                     &cpi) == SOLC_OK);
    CHECK(cpi.data[0] == 15u);
    destination.is_writable = 0u;
    CHECK(solc_rati_mint_checked_cpi(&program,
                                     &mint,
                                     &destination,
                                     &authority,
                                     1u,
                                     1,
                                     &cpi) == SOLC_E_CPI_PRIVILEGE_ESCALATION);
    destination.is_writable = 1u;
    CHECK(solc_rati_mint_checked_cpi(&program,
                                     &mint,
                                     &destination,
                                     &authority,
                                     1u,
                                     0,
                                     &cpi) == SOLC_E_CPI_PRIVILEGE_ESCALATION);
}

int main(void) {
    test_signal_packnft_transaction_parity();
    test_rati_instruction_views();
    test_rati_checked_cpi();
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("solc_migration_tests: ok");
    return EXIT_SUCCESS;
}
