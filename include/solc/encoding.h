#ifndef SOLC_ENCODING_H
#define SOLC_ENCODING_H

#include <stddef.h>
#include <stdint.h>

#include "solc/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_BASE58_PUBKEY_MAX_CHARS 44u
#define SOLC_BASE58_SIGNATURE_MAX_CHARS 88u
#define SOLC_BASE64_LEGACY_V0_MAX_CHARS 1644u
#define SOLC_BASE64_V1_MAX_CHARS 5464u

/*
 * Encoders write text without a trailing NUL. Decoders reject whitespace,
 * alternate alphabets, omitted/extra base64 padding, and non-zero unused bits.
 * `output_len` is always set on success. Base58 callers should provide at least
 * (input_len * 138 / 100) + 2 bytes; base64 needs 4 * ceil(input_len / 3).
 */
solc_status solc_base58_encode(const uint8_t *input,
                               size_t input_len,
                               char *output,
                               size_t output_capacity,
                               size_t *output_len);
solc_status solc_base58_decode(const char *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_capacity,
                               size_t *output_len);

solc_status solc_base64_encode(const uint8_t *input,
                               size_t input_len,
                               char *output,
                               size_t output_capacity,
                               size_t *output_len);
solc_status solc_base64_decode(const char *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_capacity,
                               size_t *output_len);

#ifdef __cplusplus
}
#endif

#endif
