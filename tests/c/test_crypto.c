#include "solc/crypto.h"

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
    uint8_t signature[64];
    uint8_t keys[64];
    uint8_t lifetime[32];
    uint8_t accounts[1];
    uint8_t data[3];
    solc_compiled_instruction instruction;
    solc_transaction transaction;
} fixture;

typedef struct fake_context {
    const uint8_t *expected_message;
    size_t expected_message_len;
    size_t calls;
    int reject;
} fake_context;

static solc_slice slice(const uint8_t *data, size_t len) {
    solc_slice result = {data, len};
    return result;
}

static void make_fixture(fixture *value, solc_message_version version) {
    memset(value, 0, sizeof(*value));
    memset(value->signature, 0xabu, sizeof(value->signature));
    memset(value->keys, 0x11u, 32u);
    memset(value->keys + 32u, 0x22u, 32u);
    memset(value->lifetime, 0x33u, sizeof(value->lifetime));
    value->accounts[0] = 0u;
    value->data[0] = 1u;
    value->data[1] = 2u;
    value->data[2] = 3u;
    value->instruction.program_id_index = 1u;
    value->instruction.account_indices = slice(value->accounts, 1u);
    value->instruction.data = slice(value->data, sizeof(value->data));
    value->transaction.signatures = slice(value->signature, sizeof(value->signature));
    value->transaction.message.version = version;
    value->transaction.message.num_required_signatures = 1u;
    value->transaction.message.num_readonly_unsigned_accounts = 1u;
    value->transaction.message.static_account_keys = slice(value->keys, sizeof(value->keys));
    value->transaction.message.lifetime_specifier =
        slice(value->lifetime, sizeof(value->lifetime));
    value->transaction.message.instructions = &value->instruction;
    value->transaction.message.instruction_count = 1u;
}

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    return value - 'a' + 10;
}

static void decode_digest(const char *text, uint8_t output[32]) {
    size_t i;
    for (i = 0u; i < 32u; ++i) {
        output[i] = (uint8_t)((hex_nibble(text[i * 2u]) << 4) |
                              hex_nibble(text[i * 2u + 1u]));
    }
}

static void test_sha256(void) {
    static const struct {
        const char *message;
        const char *digest;
    } cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"The quick brown fox jumps over the lazy dog",
         "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
    };
    solc_crypto_provider provider = {NULL, solc_sha256_builtin, NULL};
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t actual[32];
        uint8_t expected[32];
        decode_digest(cases[i].digest, expected);
        CHECK(solc_sha256(&provider,
                          (const uint8_t *)cases[i].message,
                          strlen(cases[i].message),
                          actual) == SOLC_OK);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    }
    CHECK(solc_sha256(NULL, (const uint8_t *)"", 0u, (uint8_t[32]){0u}) ==
          SOLC_E_CRYPTO_UNAVAILABLE);
}

static solc_status fake_verify(void *opaque,
                               const uint8_t public_key[32],
                               const uint8_t signature[64],
                               const uint8_t *message,
                               size_t message_len) {
    fake_context *context = (fake_context *)opaque;
    ++context->calls;
    CHECK(public_key[0] == 0x11u);
    CHECK(signature[0] == 0xabu);
    CHECK(message_len == context->expected_message_len);
    CHECK(memcmp(message, context->expected_message, message_len) == 0);
    return context->reject != 0 ? SOLC_E_SIGNATURE_INVALID : SOLC_OK;
}

static void test_message_bytes_and_provider(solc_message_version version) {
    fixture value;
    uint8_t transaction_bytes[512];
    uint8_t message_bytes[512];
    size_t transaction_len = 0u;
    size_t message_len = 0u;
    size_t failed_signature = 0u;
    solc_error error;
    fake_context context;
    solc_crypto_provider provider = {&context, solc_sha256_builtin, fake_verify};
    const uint8_t *expected;
    size_t expected_len;

    make_fixture(&value, version);
    CHECK(solc_transaction_encode(&value.transaction,
                                  transaction_bytes,
                                  sizeof(transaction_bytes),
                                  &transaction_len,
                                  &error) == SOLC_OK);
    CHECK(solc_transaction_message_encode(&value.transaction,
                                          message_bytes,
                                          sizeof(message_bytes),
                                          &message_len,
                                          &error) == SOLC_OK);
    if (version == SOLC_MESSAGE_V1) {
        expected = transaction_bytes;
        expected_len = transaction_len - SOLC_SIGNATURE_BYTES;
        CHECK(message_bytes[0] == 0x81u);
    } else {
        expected = transaction_bytes + 1u + SOLC_SIGNATURE_BYTES;
        expected_len = transaction_len - 1u - SOLC_SIGNATURE_BYTES;
        if (version == SOLC_MESSAGE_V0) {
            CHECK(message_bytes[0] == 0x80u);
        }
    }
    CHECK(message_len == expected_len);
    CHECK(memcmp(message_bytes, expected, expected_len) == 0);

    context.expected_message = expected;
    context.expected_message_len = expected_len;
    context.calls = 0u;
    context.reject = 0;
    CHECK(solc_transaction_verify_signatures(&value.transaction,
                                             &provider,
                                             message_bytes,
                                             sizeof(message_bytes),
                                             &failed_signature,
                                             &error) == SOLC_OK);
    CHECK(context.calls == 1u);
    CHECK(failed_signature == SIZE_MAX);
    context.reject = 1;
    CHECK(solc_transaction_verify_signatures(&value.transaction,
                                             &provider,
                                             message_bytes,
                                             sizeof(message_bytes),
                                             &failed_signature,
                                             &error) == SOLC_E_SIGNATURE_INVALID);
    CHECK(failed_signature == 0u);
}

int main(void) {
    test_sha256();
    test_message_bytes_and_provider(SOLC_MESSAGE_LEGACY);
    test_message_bytes_and_provider(SOLC_MESSAGE_V0);
    test_message_bytes_and_provider(SOLC_MESSAGE_V1);
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("solc_crypto_tests: ok");
    return EXIT_SUCCESS;
}
