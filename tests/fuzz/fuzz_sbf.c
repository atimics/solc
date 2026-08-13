#include "solc/sbf.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static int slice_inside(const uint8_t *base,
                        size_t input_len,
                        const uint8_t *pointer,
                        size_t len) {
    uintptr_t start = (uintptr_t)base;
    uintptr_t end = start + input_len;
    uintptr_t value = (uintptr_t)pointer;
    return end >= start && value >= start && value <= end && len <= end - value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    solc_sbf_account accounts[32];
    solc_sbf_parameters parameters;
    solc_error error;
    size_t i;
    solc_status status;

    if (size > 65536u) {
        return 0;
    }
    status = solc_sbf_parameters_decode(
        (uint8_t *)data, size, accounts, 32u, &parameters, &error);
    if (status != SOLC_OK) {
        return 0;
    }
    if (!slice_inside(data,
                      size,
                      parameters.instruction_data.data,
                      parameters.instruction_data.len) ||
        !slice_inside(data, size, parameters.program_id, SOLC_PUBKEY_BYTES)) {
        abort();
    }
    for (i = 0u; i < parameters.account_count; ++i) {
        const solc_sbf_account *account = &parameters.accounts[i];
        if (!slice_inside(data, size, account->key, SOLC_PUBKEY_BYTES) ||
            !slice_inside(data, size, account->owner, SOLC_PUBKEY_BYTES) ||
            !slice_inside(data, size, account->lamports_le, 8u) ||
            !slice_inside(data, size, account->data_len_le, 8u) ||
            !slice_inside(data, size, account->data, account->original_data_len) ||
            (account->duplicate_of != UINT8_MAX &&
             (size_t)account->duplicate_of >= i)) {
            abort();
        }
    }
    return 0;
}
