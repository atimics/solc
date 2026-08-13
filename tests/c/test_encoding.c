#include "solc/encoding.h"

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

static void test_base58_vectors(void) {
    static const struct {
        const uint8_t *bytes;
        size_t byte_len;
        const char *text;
    } cases[] = {
        {(const uint8_t *)"", 0u, ""},
        {(const uint8_t *)"\0", 1u, "1"},
        {(const uint8_t *)"\0\0\1", 3u, "112"},
        {(const uint8_t *)"Hello World", 11u, "JxF12TrwUP45BMd"},
        {(const uint8_t *)"Hello World!", 12u, "2NEpo7TZRRrLZSi2U"},
    };
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char encoded[128];
        uint8_t decoded[128];
        size_t encoded_len = 0u;
        size_t decoded_len = 0u;
        CHECK(solc_base58_encode(cases[i].bytes,
                                 cases[i].byte_len,
                                 encoded,
                                 sizeof(encoded),
                                 &encoded_len) == SOLC_OK);
        CHECK(encoded_len == strlen(cases[i].text));
        CHECK(memcmp(encoded, cases[i].text, encoded_len) == 0);
        CHECK(solc_base58_decode(cases[i].text,
                                 strlen(cases[i].text),
                                 decoded,
                                 sizeof(decoded),
                                 &decoded_len) == SOLC_OK);
        CHECK(decoded_len == cases[i].byte_len);
        CHECK(memcmp(decoded, cases[i].bytes, decoded_len) == 0);
    }
}

static void test_base58_roundtrip_and_rejections(void) {
    uint8_t bytes[256];
    uint8_t decoded[256];
    char encoded[360];
    static const char invalid[] = {'0', 'O', 'I', 'l', ' ', '\n', '\0'};
    size_t len;
    size_t i;
    for (i = 0u; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)((i * 73u + 19u) & 0xffu);
    }
    for (len = 0u; len <= sizeof(bytes); ++len) {
        size_t encoded_len = 0u;
        size_t decoded_len = 0u;
        CHECK(solc_base58_encode(
                  bytes, len, encoded, sizeof(encoded), &encoded_len) == SOLC_OK);
        CHECK(solc_base58_decode(encoded,
                                 encoded_len,
                                 decoded,
                                 sizeof(decoded),
                                 &decoded_len) == SOLC_OK);
        CHECK(decoded_len == len);
        CHECK(memcmp(decoded, bytes, len) == 0);
    }
    for (i = 0u; i < sizeof(invalid); ++i) {
        size_t output_len = 0u;
        CHECK(solc_base58_decode(
                  &invalid[i], 1u, decoded, sizeof(decoded), &output_len) ==
              SOLC_E_INVALID_ENCODING);
    }
    {
        size_t output_len = 0u;
        CHECK(solc_base58_encode(bytes, sizeof(bytes), encoded, 1u, &output_len) ==
              SOLC_E_OUTPUT_TOO_SMALL);
        CHECK(solc_base58_decode("111", 3u, decoded, 2u, &output_len) ==
              SOLC_E_OUTPUT_TOO_SMALL);
    }
}

static void test_base64_vectors(void) {
    static const struct {
        const char *plain;
        const char *encoded;
    } cases[] = {
        {"", ""},       {"f", "Zg=="},     {"fo", "Zm8="},
        {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char encoded[32];
        uint8_t decoded[32];
        size_t encoded_len = 0u;
        size_t decoded_len = 0u;
        CHECK(solc_base64_encode((const uint8_t *)cases[i].plain,
                                 strlen(cases[i].plain),
                                 encoded,
                                 sizeof(encoded),
                                 &encoded_len) == SOLC_OK);
        CHECK(encoded_len == strlen(cases[i].encoded));
        CHECK(memcmp(encoded, cases[i].encoded, encoded_len) == 0);
        CHECK(solc_base64_decode(cases[i].encoded,
                                 strlen(cases[i].encoded),
                                 decoded,
                                 sizeof(decoded),
                                 &decoded_len) == SOLC_OK);
        CHECK(decoded_len == strlen(cases[i].plain));
        CHECK(memcmp(decoded, cases[i].plain, decoded_len) == 0);
    }
}

static void test_base64_rejections(void) {
    static const char *noncanonical[] = {
        "Zg=", "Zg===", "Zh==", "Zm9=", "Zm=9", "Zg==AAAA",
    };
    static const char *invalid[] = {"Zg-_", "Z g==", "Zg\n==", "!!!!"};
    uint8_t output[32];
    size_t output_len = 0u;
    size_t i;
    for (i = 0u; i < sizeof(noncanonical) / sizeof(noncanonical[0]); ++i) {
        solc_status status = solc_base64_decode(noncanonical[i],
                                                strlen(noncanonical[i]),
                                                output,
                                                sizeof(output),
                                                &output_len);
        CHECK(status == SOLC_E_NON_CANONICAL || status == SOLC_E_INVALID_ENCODING);
    }
    for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        solc_status status = solc_base64_decode(invalid[i],
                                                strlen(invalid[i]),
                                                output,
                                                sizeof(output),
                                                &output_len);
        CHECK(status == SOLC_E_INVALID_ENCODING || status == SOLC_E_NON_CANONICAL);
    }
    CHECK(solc_base64_decode("Zm9v", 4u, output, 2u, &output_len) ==
          SOLC_E_OUTPUT_TOO_SMALL);
}

int main(void) {
    test_base58_vectors();
    test_base58_roundtrip_and_rejections();
    test_base64_vectors();
    test_base64_rejections();
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("solc_encoding_tests: ok");
    return EXIT_SUCCESS;
}
