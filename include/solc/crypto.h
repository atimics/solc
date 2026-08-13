#ifndef SOLC_CRYPTO_H
#define SOLC_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "solc/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef solc_status (*solc_sha256_provider_fn)(void *context,
                                               const uint8_t *message,
                                               size_t message_len,
                                               uint8_t digest[SOLC_HASH_BYTES]);

typedef solc_status (*solc_ed25519_verify_provider_fn)(
    void *context,
    const uint8_t public_key[SOLC_PUBKEY_BYTES],
    const uint8_t signature[SOLC_SIGNATURE_BYTES],
    const uint8_t *message,
    size_t message_len);

typedef struct solc_crypto_provider {
    void *context;
    solc_sha256_provider_fn sha256;
    solc_ed25519_verify_provider_fn ed25519_verify;
} solc_crypto_provider;

/* Portable, allocation-free SHA-256 provider shipped by the C core. */
solc_status solc_sha256_builtin(void *context,
                                const uint8_t *message,
                                size_t message_len,
                                uint8_t digest[SOLC_HASH_BYTES]);

solc_status solc_sha256(const solc_crypto_provider *provider,
                        const uint8_t *message,
                        size_t message_len,
                        uint8_t digest[SOLC_HASH_BYTES]);

solc_status solc_ed25519_verify(const solc_crypto_provider *provider,
                                const uint8_t public_key[SOLC_PUBKEY_BYTES],
                                const uint8_t signature[SOLC_SIGNATURE_BYTES],
                                const uint8_t *message,
                                size_t message_len);

/*
 * Encode the exact message bytes covered by every transaction signature.
 * The caller owns `message_buffer`; a NULL buffer performs exact sizing.
 */
solc_status solc_transaction_message_encode(const solc_transaction *transaction,
                                            uint8_t *message_buffer,
                                            size_t message_capacity,
                                            size_t *message_len,
                                            solc_error *error);

/*
 * Verify each signature against the corresponding leading static account key.
 * The provider decides which Ed25519 implementation crosses the trust boundary.
 * `failed_signature` receives SIZE_MAX on provider/setup failures and the signer
 * index on a cryptographic rejection.
 */
solc_status solc_transaction_verify_signatures(
    const solc_transaction *transaction,
    const solc_crypto_provider *provider,
    uint8_t *message_buffer,
    size_t message_capacity,
    size_t *failed_signature,
    solc_error *error);

#ifdef __cplusplus
}
#endif

#endif
