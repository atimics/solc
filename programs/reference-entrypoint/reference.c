#include "solc/sbf.h"
#include "solc/sbf_sdk.h"

#define REFERENCE_MAX_ACCOUNTS 32u

uint64_t entrypoint(const uint8_t *input) {
    solc_sbf_account accounts[REFERENCE_MAX_ACCOUNTS];
    solc_sbf_parameters parameters;
    solc_error error;
    solc_status status = solc_sbf_parameters_decode(
        (uint8_t *)input,
        SOLC_SBF_LOADER_TRUSTED_INPUT_LEN,
        accounts,
        REFERENCE_MAX_ACCOUNTS,
        &parameters,
        &error);
    if (status != SOLC_OK) {
        return (uint64_t)status;
    }
    return parameters.program_id == NULL ? (uint64_t)SOLC_E_INVALID_MODEL : 0u;
}
