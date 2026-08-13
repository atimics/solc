#include "solc/encoding.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t decoded[4096];
    char reencoded[5700];
    size_t decoded_len = 0u;
    size_t reencoded_len = 0u;
    solc_status status;

    if (size > sizeof(reencoded)) {
        return 0;
    }
    status = solc_base58_decode((const char *)data,
                                size,
                                decoded,
                                sizeof(decoded),
                                &decoded_len);
    if (status == SOLC_OK) {
        if (solc_base58_encode(decoded,
                               decoded_len,
                               reencoded,
                               sizeof(reencoded),
                               &reencoded_len) != SOLC_OK ||
            reencoded_len != size || memcmp(reencoded, data, size) != 0) {
            abort();
        }
    }

    status = solc_base64_decode((const char *)data,
                                size,
                                decoded,
                                sizeof(decoded),
                                &decoded_len);
    if (status == SOLC_OK) {
        if (solc_base64_encode(decoded,
                               decoded_len,
                               reencoded,
                               sizeof(reencoded),
                               &reencoded_len) != SOLC_OK ||
            reencoded_len != size || memcmp(reencoded, data, size) != 0) {
            abort();
        }
    }
    return 0;
}
