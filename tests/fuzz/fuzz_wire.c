#include "solc/wire.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
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

    if (solc_transaction_decode(data, size, &scratch, &transaction, &error) == SOLC_OK) {
        size_t encoded_len = 0u;
        uint8_t *encoded;
        if (solc_transaction_encode(
                &transaction, NULL, 0u, &encoded_len, &error) != SOLC_OK) {
            abort();
        }
        encoded = (uint8_t *)malloc(encoded_len == 0u ? 1u : encoded_len);
        if (encoded == NULL) {
            return 0;
        }
        if (solc_transaction_encode(
                &transaction, encoded, encoded_len, &encoded_len, &error) != SOLC_OK ||
            encoded_len != size || memcmp(encoded, data, size) != 0) {
            free(encoded);
            abort();
        }
        free(encoded);
    }
    return 0;
}
