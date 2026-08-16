#include "solc/wire.h"

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
    uint8_t signatures[64];
    uint8_t keys[64];
    uint8_t hash[32];
    uint8_t instruction_accounts[2];
    uint8_t instruction_data[4];
    uint8_t lookup_key[32];
    uint8_t writable_indices[1];
    uint8_t readonly_indices[1];
    solc_compiled_instruction instruction;
    solc_address_table_lookup lookup;
    solc_transaction transaction;
} fixture;

static solc_slice slice(const uint8_t *data, size_t len) {
    solc_slice result = {data, len};
    return result;
}

static void fill(uint8_t *data, size_t len, uint8_t value) {
    memset(data, value, len);
}

static void make_fixture(fixture *f, solc_message_version version) {
    uint8_t signature_byte = version == SOLC_MESSAGE_LEGACY ? 0xa1u
                             : version == SOLC_MESSAGE_V0   ? 0xa2u
                                                           : 0xa3u;
    memset(f, 0, sizeof(*f));
    fill(f->signatures, sizeof(f->signatures), signature_byte);
    fill(f->keys, 32u, 0x11u);
    fill(f->keys + 32u, 32u, 0x22u);
    fill(f->hash, sizeof(f->hash), version == SOLC_MESSAGE_LEGACY ? 0x33u
                                  : version == SOLC_MESSAGE_V0   ? 0x34u
                                                                : 0x35u);
    fill(f->lookup_key, sizeof(f->lookup_key), 0x44u);

    f->instruction_accounts[0] = 0u;
    f->instruction_accounts[1] = 2u;
    if (version == SOLC_MESSAGE_LEGACY) {
        f->instruction_data[0] = 0xdeu;
        f->instruction_data[1] = 0xadu;
        f->instruction_data[2] = 0xbeu;
        f->instruction_data[3] = 0xefu;
    } else if (version == SOLC_MESSAGE_V0) {
        f->instruction_data[0] = 1u;
        f->instruction_data[1] = 2u;
        f->instruction_data[2] = 3u;
    } else {
        f->instruction_data[0] = 0xcau;
        f->instruction_data[1] = 0xfeu;
    }

    f->instruction.program_id_index = 1u;
    f->instruction.account_indices =
        slice(f->instruction_accounts, version == SOLC_MESSAGE_V0 ? 2u : 1u);
    f->instruction.data = slice(f->instruction_data, version == SOLC_MESSAGE_LEGACY ? 4u
                                                        : version == SOLC_MESSAGE_V0 ? 3u
                                                                                     : 2u);
    f->lookup.account_key = slice(f->lookup_key, sizeof(f->lookup_key));
    f->writable_indices[0] = 7u;
    f->readonly_indices[0] = 9u;
    f->lookup.writable_indices = slice(f->writable_indices, 1u);
    f->lookup.readonly_indices = slice(f->readonly_indices, 1u);

    f->transaction.signatures = slice(f->signatures, sizeof(f->signatures));
    f->transaction.message.version = version;
    f->transaction.message.num_required_signatures = 1u;
    f->transaction.message.num_readonly_signed_accounts = 0u;
    f->transaction.message.num_readonly_unsigned_accounts = 1u;
    f->transaction.message.static_account_keys = slice(f->keys, sizeof(f->keys));
    f->transaction.message.lifetime_specifier = slice(f->hash, sizeof(f->hash));
    f->transaction.message.instructions = &f->instruction;
    f->transaction.message.instruction_count = 1u;
    if (version == SOLC_MESSAGE_V0) {
        f->transaction.message.address_table_lookups = &f->lookup;
        f->transaction.message.address_table_lookup_count = 1u;
    }
    if (version == SOLC_MESSAGE_V1) {
        f->transaction.message.v1_config.mask =
            SOLC_V1_CONFIG_PRIORITY_FEE | SOLC_V1_CONFIG_COMPUTE_UNIT_LIMIT;
        f->transaction.message.v1_config.priority_fee = UINT64_C(0x0102030405060708);
        f->transaction.message.v1_config.compute_unit_limit = UINT32_C(0x11223344);
    }
}

static size_t encode_fixture(fixture *f, uint8_t *encoded, size_t capacity) {
    size_t len = 0u;
    solc_error error;
    solc_status status =
        solc_transaction_encode(&f->transaction, encoded, capacity, &len, &error);
    CHECK(status == SOLC_OK);
    return len;
}

static void assert_roundtrip(solc_message_version version, size_t expected_len) {
    fixture f;
    uint8_t encoded[4096];
    uint8_t reencoded[4096];
    solc_compiled_instruction instructions[64];
    solc_address_table_lookup lookups[32];
    solc_decode_scratch scratch = {instructions, 64u, lookups, 32u};
    solc_transaction decoded;
    solc_error error;
    size_t encoded_len;
    size_t reencoded_len = 0u;
    solc_status status;

    make_fixture(&f, version);
    encoded_len = encode_fixture(&f, encoded, sizeof(encoded));
    if (encoded_len != expected_len) {
        fprintf(stderr,
                "length mismatch for %s: got %zu, expected %zu\n",
                solc_version_string(version),
                encoded_len,
                expected_len);
        ++failures;
    }
    status = solc_transaction_decode(encoded, encoded_len, &scratch, &decoded, &error);
    CHECK(status == SOLC_OK);
    CHECK(decoded.message.version == version);
    CHECK(decoded.message.instruction_count == 1u);
    CHECK(decoded.message.static_account_keys.len == 64u);
    CHECK(decoded.signatures.len == 64u);
    status = solc_transaction_encode(
        &decoded, reencoded, sizeof(reencoded), &reencoded_len, &error);
    CHECK(status == SOLC_OK);
    CHECK(reencoded_len == encoded_len);
    CHECK(memcmp(encoded, reencoded, encoded_len) == 0);

    if (version == SOLC_MESSAGE_LEGACY) {
        CHECK(encoded[0] == 1u);
        CHECK(encoded[65] == 1u);
    } else if (version == SOLC_MESSAGE_V0) {
        CHECK(encoded[0] == 1u);
        CHECK(encoded[65] == 0x80u);
        CHECK(decoded.message.address_table_lookup_count == 1u);
    } else {
        CHECK(encoded[0] == 0x81u);
        CHECK(encoded[encoded_len - 64u] == 0xa3u);
        CHECK(decoded.message.v1_config.priority_fee == UINT64_C(0x0102030405060708));
        CHECK(decoded.message.v1_config.compute_unit_limit == UINT32_C(0x11223344));
    }
}

static void test_short_u16(void) {
    static const struct {
        uint16_t value;
        uint8_t bytes[3];
        size_t len;
    } cases[] = {
        {0x0000u, {0x00u, 0u, 0u}, 1u},
        {0x007fu, {0x7fu, 0u, 0u}, 1u},
        {0x0080u, {0x80u, 0x01u, 0u}, 2u},
        {0x00ffu, {0xffu, 0x01u, 0u}, 2u},
        {0x0100u, {0x80u, 0x02u, 0u}, 2u},
        {0x3fffu, {0xffu, 0x7fu, 0u}, 2u},
        {0x4000u, {0x80u, 0x80u, 0x01u}, 3u},
        {0xffffu, {0xffu, 0xffu, 0x03u}, 3u},
    };
    static const uint8_t alias_zero[] = {0x80u, 0x00u};
    static const uint8_t alias_127[] = {0xffu, 0x00u};
    static const uint8_t overflow[] = {0xffu, 0xffu, 0x04u};
    static const uint8_t continues[] = {0x80u, 0x80u, 0x80u};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t encoded[3] = {0u, 0u, 0u};
        uint16_t decoded = 0u;
        size_t len = 0u;
        CHECK(solc_short_u16_encode(cases[i].value, encoded, &len) == SOLC_OK);
        CHECK(len == cases[i].len);
        CHECK(memcmp(encoded, cases[i].bytes, len) == 0);
        CHECK(solc_short_u16_decode(encoded, len, &decoded, &len) == SOLC_OK);
        CHECK(decoded == cases[i].value);
        CHECK(len == cases[i].len);
    }
    {
        uint16_t value = 0u;
        size_t consumed = 0u;
        CHECK(solc_short_u16_decode(alias_zero, sizeof(alias_zero), &value, &consumed) ==
              SOLC_E_NON_CANONICAL);
        CHECK(solc_short_u16_decode(alias_127, sizeof(alias_127), &value, &consumed) ==
              SOLC_E_NON_CANONICAL);
        CHECK(solc_short_u16_decode(overflow, sizeof(overflow), &value, &consumed) ==
              SOLC_E_OVERFLOW);
        CHECK(solc_short_u16_decode(continues, sizeof(continues), &value, &consumed) ==
              SOLC_E_OVERFLOW);
    }
}

static void test_short_u16_exhaustive(void) {
    uint32_t candidate;
    for (candidate = 0u; candidate <= UINT16_MAX; ++candidate) {
        uint8_t encoded[3] = {0u, 0u, 0u};
        uint16_t decoded = 0u;
        size_t encoded_len = 0u;
        size_t consumed = 0u;
        size_t expected_len = candidate < 128u ? 1u : candidate < 16384u ? 2u : 3u;
        CHECK(solc_short_u16_encode((uint16_t)candidate, encoded, &encoded_len) == SOLC_OK);
        CHECK(encoded_len == expected_len);
        CHECK(solc_short_u16_decode(encoded, encoded_len, &decoded, &consumed) == SOLC_OK);
        CHECK(decoded == (uint16_t)candidate);
        CHECK(consumed == encoded_len);
    }
}

static void test_truncation_and_canonical_mutations(solc_message_version version) {
    fixture f;
    uint8_t encoded[4096];
    uint8_t mutated[4097];
    uint8_t reencoded[4096];
    solc_compiled_instruction instructions[64];
    solc_address_table_lookup lookups[32];
    solc_decode_scratch scratch = {instructions, 64u, lookups, 32u};
    solc_transaction decoded;
    solc_error error;
    size_t len;
    size_t i;

    make_fixture(&f, version);
    len = encode_fixture(&f, encoded, sizeof(encoded));
    for (i = 0u; i < len; ++i) {
        CHECK(solc_transaction_decode(encoded, i, &scratch, &decoded, &error) != SOLC_OK);
    }
    memcpy(mutated, encoded, len);
    mutated[len] = 0u;
    CHECK(solc_transaction_decode(mutated, len + 1u, &scratch, &decoded, &error) ==
          SOLC_E_TRAILING_BYTES);

    for (i = 0u; i < len * 8u; ++i) {
        size_t output_len = 0u;
        memcpy(mutated, encoded, len);
        mutated[i / 8u] ^= (uint8_t)(1u << (i % 8u));
        if (solc_transaction_decode(mutated, len, &scratch, &decoded, &error) == SOLC_OK) {
            CHECK(solc_transaction_encode(
                      &decoded, reencoded, sizeof(reencoded), &output_len, &error) == SOLC_OK);
            CHECK(output_len == len);
            CHECK(memcmp(mutated, reencoded, len) == 0);
        }
    }
}

static void test_rejections(void) {
    fixture legacy;
    fixture v0;
    fixture v1;
    uint8_t encoded[4096];
    solc_compiled_instruction instructions[64];
    solc_address_table_lookup lookups[32];
    solc_decode_scratch scratch = {instructions, 64u, lookups, 32u};
    solc_decode_scratch no_scratch = {NULL, 0u, NULL, 0u};
    solc_transaction decoded;
    solc_error error;
    size_t len;
    size_t required = 0u;
    uint8_t unknown_v0[] = {0x80u};
    uint8_t unknown_v2[] = {0x82u};

    make_fixture(&legacy, SOLC_MESSAGE_LEGACY);
    make_fixture(&v0, SOLC_MESSAGE_V0);
    make_fixture(&v1, SOLC_MESSAGE_V1);

    CHECK(solc_transaction_decode(unknown_v0, sizeof(unknown_v0), &scratch, &decoded, &error) ==
          SOLC_E_UNSUPPORTED_VERSION);
    CHECK(solc_transaction_decode(unknown_v2, sizeof(unknown_v2), &scratch, &decoded, &error) ==
          SOLC_E_UNSUPPORTED_VERSION);

    len = encode_fixture(&legacy, encoded, sizeof(encoded));
    CHECK(solc_transaction_decode(encoded, len, &no_scratch, &decoded, &error) ==
          SOLC_E_SCRATCH_TOO_SMALL);
    CHECK(solc_transaction_encode(&legacy.transaction, encoded, len - 1u, &required, &error) ==
          SOLC_E_OUTPUT_TOO_SMALL);
    CHECK(required == len);

    legacy.transaction.message.num_required_signatures = 2u;
    CHECK(solc_transaction_encode(&legacy.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_SIGNATURE_MISMATCH);

    v0.lookup.writable_indices.len = 0u;
    v0.lookup.readonly_indices.len = 0u;
    CHECK(solc_transaction_encode(&v0.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_INVALID_MODEL);

    memcpy(v1.keys + 32u, v1.keys, 32u);
    CHECK(solc_transaction_encode(&v1.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_DUPLICATE_ADDRESS);
    make_fixture(&v1, SOLC_MESSAGE_V1);
    v1.transaction.message.v1_config.mask = 1u;
    CHECK(solc_transaction_encode(&v1.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_INVALID_CONFIG);
    v1.transaction.message.v1_config.mask = SOLC_V1_CONFIG_HEAP_SIZE;
    v1.transaction.message.v1_config.heap_size = 1025u;
    CHECK(solc_transaction_encode(&v1.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_INVALID_CONFIG);
}

static void test_size_boundaries(void) {
    fixture legacy;
    fixture v0;
    fixture v1;
    uint8_t legacy_data[1062];
    uint8_t v1_data[3910];
    uint8_t loaded_indices[255];
    uint8_t oversized_legacy[1233] = {0u};
    uint8_t oversized_v1[4097] = {0u};
    solc_compiled_instruction instructions[SOLC_MAX_DECODE_INSTRUCTIONS];
    solc_address_table_lookup lookups[SOLC_MAX_DECODE_LOOKUPS];
    solc_decode_scratch scratch = {
        instructions,
        SOLC_MAX_DECODE_INSTRUCTIONS,
        lookups,
        SOLC_MAX_DECODE_LOOKUPS,
    };
    solc_transaction decoded;
    solc_error error;
    size_t required = 0u;

    memset(legacy_data, 0x5au, sizeof(legacy_data));
    make_fixture(&legacy, SOLC_MESSAGE_LEGACY);
    legacy.instruction.data = slice(legacy_data, 1061u);
    CHECK(solc_transaction_encode(&legacy.transaction, NULL, 0u, &required, &error) == SOLC_OK);
    CHECK(required == SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES);
    legacy.instruction.data.len = 1062u;
    CHECK(solc_transaction_encode(&legacy.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_LIMIT_EXCEEDED);

    memset(v1_data, 0x6bu, sizeof(v1_data));
    make_fixture(&v1, SOLC_MESSAGE_V1);
    v1.instruction.data = slice(v1_data, 3909u);
    CHECK(solc_transaction_encode(&v1.transaction, NULL, 0u, &required, &error) == SOLC_OK);
    CHECK(required == SOLC_V1_MAX_TRANSACTION_BYTES);
    v1.instruction.data.len = 3910u;
    CHECK(solc_transaction_encode(&v1.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_LIMIT_EXCEEDED);

    memset(loaded_indices, 0, sizeof(loaded_indices));
    make_fixture(&v0, SOLC_MESSAGE_V0);
    v0.lookup.writable_indices = slice(loaded_indices, sizeof(loaded_indices));
    v0.lookup.readonly_indices.len = 0u;
    CHECK(solc_transaction_encode(&v0.transaction, NULL, 0u, &required, &error) ==
          SOLC_E_LIMIT_EXCEEDED);

    oversized_v1[0] = 0x81u;
    CHECK(solc_transaction_decode(oversized_legacy,
                                  sizeof(oversized_legacy),
                                  &scratch,
                                  &decoded,
                                  &error) == SOLC_E_LIMIT_EXCEEDED);
    CHECK(solc_transaction_decode(
              oversized_v1, sizeof(oversized_v1), &scratch, &decoded, &error) ==
          SOLC_E_LIMIT_EXCEEDED);
}

int main(void) {
    test_short_u16();
    test_short_u16_exhaustive();
    assert_roundtrip(SOLC_MESSAGE_LEGACY, 174u);
    assert_roundtrip(SOLC_MESSAGE_V0, 212u);
    assert_roundtrip(SOLC_MESSAGE_V1, 189u);
    test_truncation_and_canonical_mutations(SOLC_MESSAGE_LEGACY);
    test_truncation_and_canonical_mutations(SOLC_MESSAGE_V0);
    test_truncation_and_canonical_mutations(SOLC_MESSAGE_V1);
    test_rejections();
    test_size_boundaries();
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("solc_wire_tests: ok");
    return EXIT_SUCCESS;
}
