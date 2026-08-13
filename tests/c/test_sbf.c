#include "solc/sbf.h"

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

typedef struct fixture {
    union {
        uint64_t alignment;
        uint8_t bytes[24000];
    } input;
    size_t len;
    size_t duplicate_offset;
} fixture;

static void put_u64(fixture *value, uint64_t number) {
    size_t i;
    for (i = 0u; i < 8u; ++i) {
        value->input.bytes[value->len++] = (uint8_t)(number >> (i * 8u));
    }
}

static void put_bytes(fixture *value, uint8_t byte, size_t len) {
    memset(value->input.bytes + value->len, byte, len);
    value->len += len;
}

static void align_8(fixture *value) {
    uintptr_t address = (uintptr_t)value->input.bytes + value->len;
    size_t padding = (size_t)((8u - (address & 7u)) & 7u);
    put_bytes(value, 0u, padding);
}

static void put_account(fixture *value,
                        uint8_t key,
                        uint8_t owner,
                        uint64_t lamports,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t signer,
                        uint8_t writable,
                        uint8_t executable) {
    value->input.bytes[value->len++] = UINT8_MAX;
    value->input.bytes[value->len++] = signer;
    value->input.bytes[value->len++] = writable;
    value->input.bytes[value->len++] = executable;
    put_bytes(value, 0xa5u, 4u);
    put_bytes(value, key, 32u);
    put_bytes(value, owner, 32u);
    put_u64(value, lamports);
    put_u64(value, (uint64_t)data_len);
    if (data_len != 0u) {
        memcpy(value->input.bytes + value->len, data, data_len);
        value->len += data_len;
    }
    put_bytes(value, 0u, SOLC_SBF_MAX_PERMITTED_DATA_INCREASE);
    align_8(value);
    put_u64(value, 9u);
}

static void make_fixture(fixture *value) {
    static const uint8_t data[] = {0xdeu, 0xadu, 0xbeu};
    memset(value, 0, sizeof(*value));
    put_u64(value, 3u);
    put_account(value, 0x11u, 0xaau, 42u, data, sizeof(data), 1u, 1u, 0u);
    put_account(value, 0x22u, 0xbbu, 7u, NULL, 0u, 0u, 0u, 1u);
    value->duplicate_offset = value->len;
    value->input.bytes[value->len++] = 0u;
    put_bytes(value, 0xccu, 7u);
    put_u64(value, 2u);
    value->input.bytes[value->len++] = 0xf0u;
    value->input.bytes[value->len++] = 0x0du;
    put_bytes(value, 0x99u, 32u);
}

static void test_decode_and_helpers(void) {
    fixture value;
    solc_sbf_account accounts[3];
    solc_sbf_parameters parameters;
    solc_error error;
    uint8_t *mutable_data = NULL;
    size_t i;

    make_fixture(&value);
    CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                     value.len,
                                     accounts,
                                     3u,
                                     &parameters,
                                     &error) == SOLC_OK);
    CHECK(parameters.account_count == 3u);
    CHECK(parameters.accounts == accounts);
    CHECK(accounts[0].duplicate_of == UINT8_MAX);
    CHECK(accounts[2].duplicate_of == 0u);
    CHECK(accounts[0].key == accounts[2].key);
    CHECK(accounts[0].data == accounts[2].data);
    CHECK(accounts[0].data_len == 3u);
    CHECK(accounts[0].data[0] == 0xdeu);
    CHECK(accounts[0].rent_epoch == 9u);
    CHECK(parameters.instruction_data.len == 2u);
    CHECK(parameters.instruction_data.data[0] == 0xf0u);
    CHECK(parameters.program_id[0] == 0x99u);

    CHECK(solc_sbf_account_require_signer(&accounts[0]) == SOLC_OK);
    CHECK(solc_sbf_account_require_writable(&accounts[0]) == SOLC_OK);
    CHECK(solc_sbf_account_require_signer(&accounts[1]) == SOLC_E_ACCOUNT_NOT_SIGNER);
    CHECK(solc_sbf_account_require_writable(&accounts[1]) ==
          SOLC_E_ACCOUNT_NOT_WRITABLE);
    CHECK(solc_sbf_account_require_key(&accounts[0], accounts[0].key) == SOLC_OK);
    CHECK(solc_sbf_account_require_key(&accounts[0], accounts[1].key) ==
          SOLC_E_ACCOUNT_KEY_MISMATCH);
    CHECK(solc_sbf_account_require_owner(&accounts[0], accounts[0].owner) == SOLC_OK);
    CHECK(solc_sbf_account_require_owner(&accounts[0], accounts[1].owner) ==
          SOLC_E_ACCOUNT_OWNER_MISMATCH);
    CHECK(solc_sbf_account_lamports(&accounts[0]) == 42u);
    CHECK(solc_sbf_account_set_lamports(&accounts[0], 99u) == SOLC_OK);
    CHECK(solc_sbf_account_lamports(&accounts[2]) == 99u);
    CHECK(solc_sbf_account_set_lamports(&accounts[1], 1u) ==
          SOLC_E_ACCOUNT_NOT_WRITABLE);
    CHECK(solc_sbf_account_data_mut(&accounts[0], 3u, &mutable_data) == SOLC_OK);
    CHECK(mutable_data == accounts[0].data);
    CHECK(solc_sbf_account_data_mut(&accounts[1], 0u, &mutable_data) ==
          SOLC_E_ACCOUNT_NOT_WRITABLE);
    CHECK(solc_sbf_account_set_data_len(
              &accounts[0], 3u + SOLC_SBF_MAX_PERMITTED_DATA_INCREASE) == SOLC_OK);
    CHECK(solc_sbf_account_set_data_len(
              &accounts[0], 4u + SOLC_SBF_MAX_PERMITTED_DATA_INCREASE) ==
          SOLC_E_LIMIT_EXCEEDED);
    CHECK(solc_sbf_account_set_data_len(&accounts[0], 3u) == SOLC_OK);

    for (i = 0u; i < value.len; ++i) {
        CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                         i,
                                         accounts,
                                         3u,
                                         &parameters,
                                         &error) != SOLC_OK);
    }
    CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                     SOLC_SBF_LOADER_TRUSTED_INPUT_LEN,
                                     accounts,
                                     3u,
                                     &parameters,
                                     &error) == SOLC_OK);
}

static void test_decode_rejections(void) {
    fixture value;
    solc_sbf_account accounts[3];
    solc_sbf_parameters parameters;
    solc_error error;
    make_fixture(&value);
    CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                     value.len,
                                     accounts,
                                     2u,
                                     &parameters,
                                     &error) == SOLC_E_SCRATCH_TOO_SMALL);
    value.input.bytes[value.duplicate_offset] = 2u;
    CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                     value.len,
                                     accounts,
                                     3u,
                                     &parameters,
                                     &error) == SOLC_E_INVALID_DUPLICATE);
    make_fixture(&value);
    value.input.bytes[value.len++] = 0u;
    CHECK(solc_sbf_parameters_decode(value.input.bytes,
                                     value.len,
                                     accounts,
                                     3u,
                                     &parameters,
                                     &error) == SOLC_E_TRAILING_BYTES);
}

static void test_seeds(void) {
    uint8_t bytes[33] = {0u};
    solc_seed seeds[17];
    solc_signer_seeds signer;
    size_t i;
    for (i = 0u; i < 17u; ++i) {
        seeds[i].data = bytes;
        seeds[i].len = i == 0u ? 0u : 32u;
    }
    CHECK(solc_pda_seeds_validate(seeds, 16u) == SOLC_OK);
    CHECK(solc_pda_seeds_validate(seeds, 17u) == SOLC_E_INVALID_SEEDS);
    seeds[4].len = 33u;
    CHECK(solc_pda_seeds_validate(seeds, 16u) == SOLC_E_INVALID_SEEDS);
    seeds[4].len = 32u;
    seeds[4].data = NULL;
    CHECK(solc_pda_seeds_validate(seeds, 16u) == SOLC_E_INVALID_SEEDS);
    seeds[4].data = bytes;
    signer.seeds = seeds;
    signer.seed_count = 16u;
    CHECK(solc_pda_signers_validate(&signer, 1u) == SOLC_OK);
}

static void test_cpi_builder(void) {
    uint8_t account_key[32] = {1u};
    uint8_t program_key[32] = {2u};
    uint8_t data[4] = {9u, 8u, 7u, 6u};
    solc_sbf_account account = {0};
    solc_sbf_account program = {0};
    solc_cpi_account_meta metas[3];
    const solc_sbf_account *infos[3];
    const solc_sbf_account **finished_infos = NULL;
    size_t finished_info_count = 0u;
    solc_cpi_builder builder;
    solc_cpi_instruction instruction;
    solc_slice instruction_data = {data, sizeof(data)};

    account.key = account_key;
    account.is_signer = 1u;
    account.is_writable = 1u;
    program.key = program_key;
    program.executable = 1u;
    CHECK(solc_cpi_builder_init(&builder,
                                &program,
                                instruction_data,
                                metas,
                                3u,
                                infos,
                                3u) == SOLC_OK);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 1, 1, 0) == SOLC_OK);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 0, 1, 0) == SOLC_OK);
    CHECK(solc_cpi_builder_finish(
              &builder, &instruction, &finished_infos, &finished_info_count) == SOLC_OK);
    CHECK(instruction.program_id == program_key);
    CHECK(instruction.account_count == 2u);
    CHECK(instruction.accounts[0].is_writable == 1u);
    CHECK(instruction.accounts[1].is_writable == 0u);
    CHECK(finished_infos == infos);
    CHECK(finished_info_count == 2u);
    CHECK(infos[0] == &account);
    CHECK(infos[1] == &program);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 0, 0, 0) ==
          SOLC_E_INVALID_MODEL);

    account.is_signer = 0u;
    account.is_writable = 0u;
    CHECK(solc_cpi_builder_init(&builder,
                                &program,
                                instruction_data,
                                metas,
                                3u,
                                infos,
                                3u) == SOLC_OK);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 1, 0, 0) ==
          SOLC_E_CPI_PRIVILEGE_ESCALATION);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 0, 1, 0) ==
          SOLC_E_CPI_PRIVILEGE_ESCALATION);
    CHECK(solc_cpi_builder_add_account(&builder, &account, 0, 1, 1) == SOLC_OK);
}

int main(void) {
    test_decode_and_helpers();
    test_decode_rejections();
    test_seeds();
    test_cpi_builder();
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("solc_sbf_tests: ok");
    return EXIT_SUCCESS;
}
