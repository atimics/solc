#include "solc/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SOLC_SOURCE_DIR
#error "SOLC_SOURCE_DIR must identify the repository root"
#endif

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

static int load_hex(const char *name, uint8_t *output, size_t capacity, size_t *output_len) {
    char path[1024];
    FILE *file;
    int high = -1;
    int comment = 0;
    int value;
    size_t length = 0u;

    if (snprintf(path, sizeof(path), "%s/tests/vectors/%s.hex", SOLC_SOURCE_DIR, name) < 0) {
        return 0;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    while ((value = fgetc(file)) != EOF) {
        int nibble;
        if (comment != 0) {
            if (value == '\n') {
                comment = 0;
            }
            continue;
        }
        if (value == '#') {
            comment = 1;
            continue;
        }
        nibble = hex_nibble(value);
        if (nibble < 0) {
            continue;
        }
        if (high < 0) {
            high = nibble;
            continue;
        }
        if (length == capacity) {
            fclose(file);
            return 0;
        }
        output[length++] = (uint8_t)((unsigned int)high << 4u | (unsigned int)nibble);
        high = -1;
    }
    if (fclose(file) != 0 || high >= 0) {
        return 0;
    }
    *output_len = length;
    return 1;
}

static void print_hex(const uint8_t *bytes, size_t length) {
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0u; index < length; ++index) {
        putchar(digits[bytes[index] >> 4u]);
        putchar(digits[bytes[index] & 0x0fu]);
    }
}

static int check_vector(const char *name) {
    uint8_t source[SOLC_V1_MAX_TRANSACTION_BYTES];
    uint8_t input_arena[SOLC_V1_MAX_TRANSACTION_BYTES + 32u];
    uint8_t output_arena[SOLC_V1_MAX_TRANSACTION_BYTES + 32u];
    uint8_t expected_digest[SOLC_HASH_BYTES];
    size_t source_len = 0u;
    size_t alignment;
    solc_message_version version = SOLC_MESSAGE_LEGACY;

    if (!load_hex(name, source, sizeof(source), &source_len)) {
        fprintf(stderr, "failed to load vector %s\n", name);
        return 0;
    }
    memset(expected_digest, 0, sizeof(expected_digest));
    for (alignment = 0u; alignment < 32u; ++alignment) {
        solc_compiled_instruction instructions[SOLC_MAX_DECODE_INSTRUCTIONS];
        solc_address_table_lookup lookups[SOLC_MAX_DECODE_LOOKUPS];
        solc_decode_scratch scratch = {
            instructions,
            SOLC_MAX_DECODE_INSTRUCTIONS,
            lookups,
            SOLC_MAX_DECODE_LOOKUPS,
        };
        solc_transaction transaction;
        solc_error error;
        uint8_t digest[SOLC_HASH_BYTES];
        uint8_t *input = input_arena + alignment;
        uint8_t *output = output_arena + (31u - alignment);
        size_t output_len = 0u;

        memset(input_arena, (int)(0x40u + alignment), sizeof(input_arena));
        memset(output_arena, (int)(0xa0u + alignment), sizeof(output_arena));
        memset(instructions, (int)(0x10u + alignment), sizeof(instructions));
        memset(lookups, (int)(0x20u + alignment), sizeof(lookups));
        memcpy(input, source, source_len);
        if (solc_transaction_decode(
                input, source_len, &scratch, &transaction, &error) != SOLC_OK) {
            fprintf(stderr, "%s decode failed at alignment %zu\n", name, alignment);
            return 0;
        }
        if (solc_transaction_encode(&transaction,
                                    output,
                                    SOLC_V1_MAX_TRANSACTION_BYTES,
                                    &output_len,
                                    &error) != SOLC_OK ||
            output_len != source_len || memcmp(output, source, source_len) != 0) {
            fprintf(stderr, "%s round trip failed at alignment %zu\n", name, alignment);
            return 0;
        }
        if (solc_sha256_builtin(NULL, output, output_len, digest) != SOLC_OK) {
            fprintf(stderr, "%s digest failed\n", name);
            return 0;
        }
        if (alignment == 0u) {
            memcpy(expected_digest, digest, sizeof(expected_digest));
            version = transaction.message.version;
        } else if (memcmp(expected_digest, digest, sizeof(expected_digest)) != 0 ||
                   transaction.message.version != version) {
            fprintf(stderr, "%s transcript changed at alignment %zu\n", name, alignment);
            return 0;
        }
    }

    printf("%s %s %zu ", name, solc_version_string(version), source_len);
    print_hex(expected_digest, sizeof(expected_digest));
    putchar('\n');
    return 1;
}

int main(void) {
    if (!check_vector("legacy") || !check_vector("v0") || !check_vector("v1")) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
