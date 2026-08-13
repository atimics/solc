#include "solc/crypto.h"

#include <limits.h>
#include <string.h>

static const uint32_t SHA256_ROUND_CONSTANTS[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
    UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
    UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
    UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
    UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
    UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
    UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
    UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
    UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
    UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
    UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
    UINT32_C(0xc67178f2),
};

static uint32_t rotate_right(uint32_t value, unsigned int shift) {
    return (value >> shift) | (value << (32u - shift));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t i;

    for (i = 0u; i < 16u; ++i) {
        size_t offset = i * 4u;
        words[i] = ((uint32_t)block[offset] << 24u) |
                   ((uint32_t)block[offset + 1u] << 16u) |
                   ((uint32_t)block[offset + 2u] << 8u) |
                   (uint32_t)block[offset + 3u];
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotate_right(words[i - 15u], 7u) ^
                      rotate_right(words[i - 15u], 18u) ^ (words[i - 15u] >> 3u);
        uint32_t s1 = rotate_right(words[i - 2u], 17u) ^
                      rotate_right(words[i - 2u], 19u) ^ (words[i - 2u] >> 10u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    for (i = 0u; i < 64u; ++i) {
        uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^
                        rotate_right(e, 25u);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 =
            h + sum1 + choice + SHA256_ROUND_CONSTANTS[i] + words[i];
        uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^
                        rotate_right(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

solc_status solc_sha256_builtin(void *context,
                                const uint8_t *message,
                                size_t message_len,
                                uint8_t digest[SOLC_HASH_BYTES]) {
    uint32_t state[8] = {
        UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
        UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
        UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
    };
    uint8_t final_blocks[128];
    size_t full_blocks;
    size_t remaining;
    size_t final_len;
    uint64_t bit_len;
    size_t i;

    (void)context;
    if ((message_len != 0u && message == NULL) || digest == NULL) {
        return SOLC_E_NULL;
    }
    full_blocks = message_len / 64u;
    for (i = 0u; i < full_blocks; ++i) {
        sha256_transform(state, message + i * 64u);
    }
    remaining = message_len % 64u;
    memset(final_blocks, 0, sizeof(final_blocks));
    if (remaining != 0u) {
        memcpy(final_blocks, message + full_blocks * 64u, remaining);
    }
    final_blocks[remaining] = 0x80u;
    final_len = remaining < 56u ? 64u : 128u;
    bit_len = (uint64_t)message_len * UINT64_C(8);
    for (i = 0u; i < 8u; ++i) {
        final_blocks[final_len - 1u - i] = (uint8_t)(bit_len >> (i * 8u));
    }
    sha256_transform(state, final_blocks);
    if (final_len == 128u) {
        sha256_transform(state, final_blocks + 64u);
    }
    for (i = 0u; i < 8u; ++i) {
        digest[i * 4u] = (uint8_t)(state[i] >> 24u);
        digest[i * 4u + 1u] = (uint8_t)(state[i] >> 16u);
        digest[i * 4u + 2u] = (uint8_t)(state[i] >> 8u);
        digest[i * 4u + 3u] = (uint8_t)state[i];
    }
    return SOLC_OK;
}

solc_status solc_sha256(const solc_crypto_provider *provider,
                        const uint8_t *message,
                        size_t message_len,
                        uint8_t digest[SOLC_HASH_BYTES]) {
    solc_status status;
    if ((message_len != 0u && message == NULL) || digest == NULL) {
        return SOLC_E_NULL;
    }
    if (provider == NULL || provider->sha256 == NULL) {
        return SOLC_E_CRYPTO_UNAVAILABLE;
    }
    status = provider->sha256(provider->context, message, message_len, digest);
    return status == SOLC_OK ? SOLC_OK : SOLC_E_PROVIDER_FAILURE;
}

solc_status solc_ed25519_verify(const solc_crypto_provider *provider,
                                const uint8_t public_key[SOLC_PUBKEY_BYTES],
                                const uint8_t signature[SOLC_SIGNATURE_BYTES],
                                const uint8_t *message,
                                size_t message_len) {
    solc_status status;
    if (public_key == NULL || signature == NULL ||
        (message_len != 0u && message == NULL)) {
        return SOLC_E_NULL;
    }
    if (provider == NULL || provider->ed25519_verify == NULL) {
        return SOLC_E_CRYPTO_UNAVAILABLE;
    }
    status = provider->ed25519_verify(
        provider->context, public_key, signature, message, message_len);
    if (status == SOLC_OK || status == SOLC_E_SIGNATURE_INVALID) {
        return status;
    }
    return SOLC_E_PROVIDER_FAILURE;
}

solc_status solc_transaction_verify_signatures(
    const solc_transaction *transaction,
    const solc_crypto_provider *provider,
    uint8_t *message_buffer,
    size_t message_capacity,
    size_t *failed_signature,
    solc_error *error) {
    size_t message_len = 0u;
    size_t signature_count;
    size_t i;
    solc_status status;

    if (failed_signature != NULL) {
        *failed_signature = SIZE_MAX;
    }
    if (transaction == NULL || provider == NULL || failed_signature == NULL) {
        if (error != NULL) {
            error->status = SOLC_E_NULL;
            error->offset = 0u;
        }
        return SOLC_E_NULL;
    }
    if (provider->ed25519_verify == NULL) {
        if (error != NULL) {
            error->status = SOLC_E_CRYPTO_UNAVAILABLE;
            error->offset = 0u;
        }
        return SOLC_E_CRYPTO_UNAVAILABLE;
    }
    status = solc_transaction_message_encode(
        transaction, NULL, 0u, &message_len, error);
    if (status != SOLC_OK) {
        return status;
    }
    if (message_buffer == NULL) {
        if (error != NULL) {
            error->status = SOLC_E_NULL;
            error->offset = 0u;
        }
        return SOLC_E_NULL;
    }
    status = solc_transaction_message_encode(
        transaction, message_buffer, message_capacity, &message_len, error);
    if (status != SOLC_OK) {
        return status;
    }

    signature_count = transaction->signatures.len / SOLC_SIGNATURE_BYTES;
    for (i = 0u; i < signature_count; ++i) {
        const uint8_t *public_key =
            transaction->message.static_account_keys.data + i * SOLC_PUBKEY_BYTES;
        const uint8_t *signature =
            transaction->signatures.data + i * SOLC_SIGNATURE_BYTES;
        status = solc_ed25519_verify(
            provider, public_key, signature, message_buffer, message_len);
        if (status != SOLC_OK) {
            if (status == SOLC_E_SIGNATURE_INVALID) {
                *failed_signature = i;
            }
            if (error != NULL) {
                error->status = status;
                error->offset = i;
            }
            return status;
        }
    }
    return SOLC_OK;
}
