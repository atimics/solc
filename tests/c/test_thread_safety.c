#include "solc/crypto.h"
#include "solc/encoding.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SOLC_SOURCE_DIR
#error "SOLC_SOURCE_DIR must identify the repository root"
#endif

enum {
    THREAD_COUNT = 8,
    ITERATION_COUNT = 2000,
};

typedef struct thread_context {
    const uint8_t *transaction;
    size_t transaction_len;
    unsigned int identity;
    int failed;
} thread_context;

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

static int load_legacy(uint8_t *output, size_t capacity, size_t *output_len) {
    const char path[] = SOLC_SOURCE_DIR "/tests/vectors/legacy.hex";
    FILE *file = fopen(path, "r");
    int high = -1;
    int comment = 0;
    int value;
    size_t length = 0u;
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
        } else {
            if (length == capacity) {
                fclose(file);
                return 0;
            }
            output[length++] = (uint8_t)((unsigned int)high << 4u | (unsigned int)nibble);
            high = -1;
        }
    }
    if (fclose(file) != 0 || high >= 0) {
        return 0;
    }
    *output_len = length;
    return 1;
}

static void *exercise(void *opaque) {
    thread_context *context = opaque;
    unsigned int iteration;
    for (iteration = 0u; iteration < ITERATION_COUNT; ++iteration) {
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
        uint8_t encoded[SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES];
        uint8_t digest[SOLC_HASH_BYTES];
        char base64[64];
        uint8_t decoded_digest[SOLC_HASH_BYTES];
        size_t encoded_len = 0u;
        size_t base64_len = 0u;
        size_t decoded_len = 0u;

        if (solc_transaction_decode(context->transaction,
                                    context->transaction_len,
                                    &scratch,
                                    &transaction,
                                    &error) != SOLC_OK ||
            solc_transaction_encode(&transaction,
                                    encoded,
                                    sizeof(encoded),
                                    &encoded_len,
                                    &error) != SOLC_OK ||
            encoded_len != context->transaction_len ||
            memcmp(encoded, context->transaction, encoded_len) != 0 ||
            solc_sha256_builtin(NULL, encoded, encoded_len, digest) != SOLC_OK ||
            solc_base64_encode(
                digest, sizeof(digest), base64, sizeof(base64), &base64_len) != SOLC_OK ||
            solc_base64_decode(base64,
                               base64_len,
                               decoded_digest,
                               sizeof(decoded_digest),
                               &decoded_len) != SOLC_OK ||
            decoded_len != sizeof(digest) || memcmp(digest, decoded_digest, decoded_len) != 0) {
            context->failed = 1;
            return NULL;
        }
        digest[0] ^= (uint8_t)(context->identity + iteration);
    }
    return NULL;
}

int main(void) {
    uint8_t legacy[SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES];
    size_t legacy_len = 0u;
    pthread_t threads[THREAD_COUNT];
    thread_context contexts[THREAD_COUNT];
    unsigned int index;

    if (!load_legacy(legacy, sizeof(legacy), &legacy_len)) {
        fputs("failed to load thread-safety fixture\n", stderr);
        return EXIT_FAILURE;
    }
    for (index = 0u; index < THREAD_COUNT; ++index) {
        contexts[index].transaction = legacy;
        contexts[index].transaction_len = legacy_len;
        contexts[index].identity = index;
        contexts[index].failed = 0;
        if (pthread_create(&threads[index], NULL, exercise, &contexts[index]) != 0) {
            fputs("pthread_create failed\n", stderr);
            return EXIT_FAILURE;
        }
    }
    for (index = 0u; index < THREAD_COUNT; ++index) {
        if (pthread_join(threads[index], NULL) != 0 || contexts[index].failed != 0) {
            fputs("concurrent deterministic-core execution failed\n", stderr);
            return EXIT_FAILURE;
        }
    }
    puts("solc_thread_safety_tests: ok");
    return EXIT_SUCCESS;
}
