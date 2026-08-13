#include "solc/programs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static void put_u32(uint8_t *output, size_t *offset, uint32_t value) {
    output[(*offset)++] = (uint8_t)value;
    output[(*offset)++] = (uint8_t)(value >> 8u);
    output[(*offset)++] = (uint8_t)(value >> 16u);
    output[(*offset)++] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *output, size_t *offset, uint64_t value) {
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        output[(*offset)++] = (uint8_t)(value >> (i * 8u));
    }
}

static void test_system(void) {
    uint8_t bytes[160] = {0u};
    uint8_t encoded[160] = {0u};
    solc_system_instruction instruction;
    solc_error error;
    size_t offset = 0u;
    size_t encoded_len = 0u;
    size_t i;

    put_u32(bytes, &offset, SOLC_SYSTEM_CREATE_ACCOUNT_WITH_SEED);
    memset(bytes + offset, 0x11, 32u);
    offset += 32u;
    put_u64(bytes, &offset, 2u);
    bytes[offset++] = 0xc3u;
    bytes[offset++] = 0xa9u;
    put_u64(bytes, &offset, UINT64_C(0x0102030405060708));
    put_u64(bytes, &offset, UINT64_C(0x1112131415161718));
    memset(bytes + offset, 0x22, 32u);
    offset += 32u;

    CHECK(solc_system_instruction_decode(bytes, offset, &instruction, &error) == SOLC_OK);
    CHECK(instruction.kind == SOLC_SYSTEM_CREATE_ACCOUNT_WITH_SEED);
    CHECK(instruction.base[0] == 0x11u);
    CHECK(instruction.seed.len == 2u);
    CHECK(instruction.lamports == UINT64_C(0x0102030405060708));
    CHECK(instruction.space == UINT64_C(0x1112131415161718));
    CHECK(instruction.owner[31] == 0x22u);
    CHECK(solc_system_instruction_encode(
              &instruction, encoded, sizeof(encoded), &encoded_len, &error) == SOLC_OK);
    CHECK(encoded_len == offset);
    CHECK(memcmp(bytes, encoded, offset) == 0);
    CHECK(strcmp(solc_system_instruction_name(instruction.kind),
                 "CREATE_ACCOUNT_WITH_SEED") == 0);
    for (i = 0u; i < offset; ++i) {
        CHECK(solc_system_instruction_decode(bytes, i, &instruction, &error) != SOLC_OK);
    }
    bytes[44u] = 0xc0u;
    bytes[45u] = 0x80u;
    CHECK(solc_system_instruction_decode(bytes, offset, &instruction, &error) ==
          SOLC_E_INVALID_UTF8);
}

static void test_compute_budget(void) {
    static const uint8_t limit_bytes[] = {2u, 1u, 1u, 0u, 0u};
    solc_compute_budget_instruction decoded;
    solc_compute_budget_instruction instructions[4];
    solc_compute_budget_limits limits;
    solc_v1_config config;
    solc_error error;
    uint8_t output[9];
    size_t output_len;

    CHECK(solc_compute_budget_instruction_decode(
              limit_bytes, sizeof(limit_bytes), &decoded, &error) == SOLC_OK);
    CHECK(decoded.kind == SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_LIMIT);
    CHECK(decoded.value == 257u);
    CHECK(solc_compute_budget_instruction_encode(
              &decoded, output, sizeof(output), &output_len, &error) == SOLC_OK);
    CHECK(output_len == sizeof(limit_bytes));
    CHECK(memcmp(output, limit_bytes, output_len) == 0);
    CHECK(solc_compute_budget_instruction_decode(
              limit_bytes, sizeof(limit_bytes) - 1u, &decoded, &error) == SOLC_E_TRUNCATED);

    instructions[0].kind = SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_LIMIT;
    instructions[0].value = 3u;
    instructions[1].kind = SOLC_COMPUTE_BUDGET_SET_COMPUTE_UNIT_PRICE;
    instructions[1].value = 2000001u;
    instructions[2].kind = SOLC_COMPUTE_BUDGET_REQUEST_HEAP_FRAME;
    instructions[2].value = 65536u;
    instructions[3].kind = SOLC_COMPUTE_BUDGET_SET_LOADED_ACCOUNTS_DATA_SIZE_LIMIT;
    instructions[3].value = 100000u;
    CHECK(solc_compute_budget_collect(instructions, 4u, &limits) == SOLC_OK);
    CHECK(solc_compute_budget_limits_to_v1(&limits, &config) == SOLC_OK);
    CHECK(config.mask == SOLC_V1_CONFIG_KNOWN_BITS);
    CHECK(config.priority_fee == 7u);
    CHECK(config.compute_unit_limit == 3u);
    CHECK(config.heap_size == 65536u);
    CHECK(config.loaded_accounts_data_size_limit == 100000u);
    instructions[3] = instructions[0];
    CHECK(solc_compute_budget_collect(instructions, 4u, &limits) ==
          SOLC_E_DUPLICATE_CONFIG);
    instructions[0].kind = SOLC_COMPUTE_BUDGET_REQUEST_HEAP_FRAME;
    instructions[0].value = 33000u;
    CHECK(solc_compute_budget_collect(instructions, 1u, &limits) == SOLC_E_INVALID_CONFIG);
}

static void test_token_instructions(void) {
    uint8_t mint[67] = {0u};
    uint8_t output[80] = {0u};
    uint8_t extensions[] = {1u, 0u, 28u, 0u};
    uint8_t envelope[] = {26u, 3u, 9u, 8u};
    uint8_t invalid_utf8[] = {24u, 0xc0u, 0x80u};
    solc_token_instruction instruction;
    solc_error error;
    size_t output_len;

    mint[0] = 0u;
    mint[1] = 9u;
    memset(mint + 2u, 0x11, 32u);
    mint[34u] = 1u;
    memset(mint + 35u, 0x22, 32u);
    CHECK(solc_token_instruction_decode(
              SOLC_TOKEN_CLASSIC, mint, sizeof(mint), &instruction, &error) == SOLC_OK);
    CHECK(instruction.kind == 0u);
    CHECK(instruction.decimals == 9u);
    CHECK(instruction.optional_pubkey.present == 1u);
    CHECK(instruction.optional_pubkey.key[0] == 0x22u);
    CHECK(solc_token_instruction_encode(SOLC_TOKEN_CLASSIC,
                                        &instruction,
                                        output,
                                        sizeof(output),
                                        &output_len,
                                        &error) == SOLC_OK);
    CHECK(output_len == sizeof(mint));
    CHECK(memcmp(output, mint, sizeof(mint)) == 0);
    mint[67u - 1u] = 0x22u;
    CHECK(solc_token_instruction_decode(
              SOLC_TOKEN_CLASSIC, mint, sizeof(mint) - 1u, &instruction, &error) ==
          SOLC_E_TRUNCATED);

    output[0] = 21u;
    memcpy(output + 1u, extensions, sizeof(extensions));
    CHECK(solc_token_instruction_decode(SOLC_TOKEN_2022,
                                        output,
                                        1u + sizeof(extensions),
                                        &instruction,
                                        &error) == SOLC_OK);
    CHECK(instruction.extension_types.len == sizeof(extensions));
    output[4u] = 29u;
    CHECK(solc_token_instruction_decode(SOLC_TOKEN_2022,
                                        output,
                                        1u + sizeof(extensions),
                                        &instruction,
                                        &error) == SOLC_E_INVALID_PROGRAM_DATA);
    CHECK(solc_token_instruction_decode(SOLC_TOKEN_2022,
                                        envelope,
                                        sizeof(envelope),
                                        &instruction,
                                        &error) == SOLC_OK);
    CHECK(instruction.data.len == 3u);
    CHECK(instruction.data.data[2] == 8u);
    CHECK(solc_token_instruction_decode(SOLC_TOKEN_CLASSIC,
                                        invalid_utf8,
                                        sizeof(invalid_utf8),
                                        &instruction,
                                        &error) == SOLC_E_INVALID_UTF8);
    CHECK(strcmp(solc_token_instruction_name(SOLC_TOKEN_2022, 44u),
                 "PAUSABLE_EXTENSION") == 0);
}

static void test_token_accounts(void) {
    uint8_t mint[SOLC_TOKEN_MINT_BYTES] = {0u};
    uint8_t account[SOLC_TOKEN_ACCOUNT_BYTES] = {0u};
    uint8_t multisig[SOLC_TOKEN_MULTISIG_BYTES] = {0u};
    uint8_t extended[205] = {0u};
    solc_token_mint decoded_mint;
    solc_token_account decoded_account;
    solc_token_multisig decoded_multisig;
    solc_token2022_state state;
    solc_token2022_tlv_iterator iterator;
    solc_token2022_tlv_entry entry;
    solc_error error;
    uint8_t has_entry;
    size_t offset;

    put_u32(mint, &(size_t){0u}, 1u);
    memset(mint + 4u, 0x11, 32u);
    offset = 36u;
    put_u64(mint, &offset, 99u);
    mint[44u] = 6u;
    mint[45u] = 1u;
    CHECK(solc_token_mint_decode(mint, sizeof(mint), &decoded_mint) == SOLC_OK);
    CHECK(decoded_mint.mint_authority.present == 1u);
    CHECK(decoded_mint.supply == 99u);
    CHECK(decoded_mint.is_initialized == 1u);

    memset(account, 0x22, 32u);
    memset(account + 32u, 0x33, 32u);
    offset = 64u;
    put_u64(account, &offset, 77u);
    account[108u] = 1u;
    CHECK(solc_token_account_decode(account, sizeof(account), &decoded_account) == SOLC_OK);
    CHECK(decoded_account.amount == 77u);
    CHECK(decoded_account.state == 1u);

    multisig[0u] = 2u;
    multisig[1u] = 3u;
    multisig[2u] = 1u;
    memset(multisig + 3u, 0x44, 32u);
    CHECK(solc_token_multisig_decode(multisig, sizeof(multisig), &decoded_multisig) ==
          SOLC_OK);
    CHECK(decoded_multisig.signers.len == 352u);

    memcpy(extended, mint, sizeof(mint));
    extended[165u] = SOLC_TOKEN2022_MINT;
    extended[166u] = 3u;
    extended[167u] = 0u;
    extended[168u] = 32u;
    extended[169u] = 0u;
    memset(extended + 170u, 0xaau, 32u);
    CHECK(solc_token2022_state_decode(
              SOLC_TOKEN2022_MINT, extended, sizeof(extended), &state, &error) == SOLC_OK);
    CHECK(state.extension_count == 1u);
    CHECK(state.tlv_used_len == 36u);
    solc_token2022_tlv_iterator_init(&state, &iterator);
    CHECK(solc_token2022_tlv_next(&iterator, &entry, &has_entry) == SOLC_OK);
    CHECK(has_entry == 1u);
    CHECK(entry.extension_type == 3u);
    CHECK(entry.value.len == 32u);
    CHECK(entry.value.data[0] == 0xaau);
    CHECK(solc_token2022_tlv_next(&iterator, &entry, &has_entry) == SOLC_OK);
    CHECK(has_entry == 0u);
    extended[82u] = 1u;
    CHECK(solc_token2022_state_decode(SOLC_TOKEN2022_MINT,
                                      extended,
                                      sizeof(extended),
                                      &state,
                                      &error) == SOLC_E_NON_CANONICAL);
    CHECK(solc_token2022_state_decode(SOLC_TOKEN2022_ACCOUNT,
                                      mint,
                                      sizeof(mint),
                                      &state,
                                      &error) == SOLC_E_TRUNCATED);
}

static void test_lookup_table(void) {
    uint8_t bytes[SOLC_ALT_META_BYTES + 64u] = {0u};
    solc_address_lookup_table table;
    solc_error error;
    const uint8_t *address = NULL;
    size_t offset = 0u;

    put_u32(bytes, &offset, 1u);
    put_u64(bytes, &offset, UINT64_MAX);
    put_u64(bytes, &offset, 42u);
    bytes[offset++] = 1u;
    bytes[offset++] = 1u;
    memset(bytes + offset, 0x11, 32u);
    offset += 32u;
    bytes[offset++] = 0u;
    bytes[offset++] = 0u;
    CHECK(offset == SOLC_ALT_META_BYTES);
    memset(bytes + offset, 0x22, 32u);
    memset(bytes + offset + 32u, 0x33, 32u);
    CHECK(solc_address_lookup_table_decode(bytes, sizeof(bytes), &table, &error) == SOLC_OK);
    CHECK(table.address_count == 2u);
    CHECK(table.authority.present == 1u);
    CHECK(table.authority.key[0] == 0x11u);
    CHECK(solc_address_lookup_table_visible_len(&table, 42u) == 1u);
    CHECK(solc_address_lookup_table_visible_len(&table, 43u) == 2u);
    CHECK(solc_address_lookup_table_address(&table, 1u, &address) == SOLC_OK);
    CHECK(address[0] == 0x33u);
    CHECK(solc_address_lookup_table_address(&table, 2u, &address) == SOLC_E_INVALID_INDEX);
    bytes[0u] = 0u;
    CHECK(solc_address_lookup_table_decode(bytes, sizeof(bytes), &table, &error) ==
          SOLC_E_UNINITIALIZED_ACCOUNT);
}

int main(void) {
    test_system();
    test_compute_budget();
    test_token_instructions();
    test_token_accounts();
    test_lookup_table();
    if (failures != 0) {
        fprintf(stderr, "%d program codec test(s) failed\n", failures);
        return 1;
    }
    puts("program codec tests passed");
    return 0;
}
