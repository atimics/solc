#include "solc/encoding.h"

#include <limits.h>
#include <string.h>

static const char BASE58_ALPHABET[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base58_value(unsigned char character) {
    const char *found;
    if (character == 0u || character > 0x7fu) {
        return -1;
    }
    found = strchr(BASE58_ALPHABET, (int)character);
    return found == NULL ? -1 : (int)(found - BASE58_ALPHABET);
}

solc_status solc_base58_encode(const uint8_t *input,
                               size_t input_len,
                               char *output,
                               size_t output_capacity,
                               size_t *output_len) {
    uint8_t *digits = (uint8_t *)output;
    size_t zero_count = 0u;
    size_t digit_count = 0u;
    size_t i;

    if ((input_len != 0u && input == NULL) || output_len == NULL ||
        (output_capacity != 0u && output == NULL)) {
        return SOLC_E_NULL;
    }
    *output_len = 0u;
    while (zero_count < input_len && input[zero_count] == 0u) {
        ++zero_count;
    }
    if (zero_count > output_capacity) {
        return SOLC_E_OUTPUT_TOO_SMALL;
    }

    for (i = zero_count; i < input_len; ++i) {
        unsigned int carry = input[i];
        size_t j;
        for (j = 0u; j < digit_count; ++j) {
            carry += (unsigned int)digits[j] << 8u;
            digits[j] = (uint8_t)(carry % 58u);
            carry /= 58u;
        }
        while (carry != 0u) {
            if (digit_count >= output_capacity - zero_count) {
                return SOLC_E_OUTPUT_TOO_SMALL;
            }
            digits[digit_count++] = (uint8_t)(carry % 58u);
            carry /= 58u;
        }
    }

    for (i = 0u; i < digit_count / 2u; ++i) {
        uint8_t temporary = digits[i];
        digits[i] = digits[digit_count - i - 1u];
        digits[digit_count - i - 1u] = temporary;
    }
    if (zero_count != 0u && digit_count != 0u) {
        memmove(digits + zero_count, digits, digit_count);
    }
    if (zero_count != 0u) {
        memset(digits, '1', zero_count);
    }
    for (i = zero_count; i < zero_count + digit_count; ++i) {
        digits[i] = (uint8_t)BASE58_ALPHABET[digits[i]];
    }
    *output_len = zero_count + digit_count;
    return SOLC_OK;
}

solc_status solc_base58_decode(const char *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_capacity,
                               size_t *output_len) {
    size_t zero_count = 0u;
    size_t byte_count = 0u;
    size_t i;

    if ((input_len != 0u && input == NULL) || output_len == NULL ||
        (output_capacity != 0u && output == NULL)) {
        return SOLC_E_NULL;
    }
    *output_len = 0u;
    while (zero_count < input_len && input[zero_count] == '1') {
        ++zero_count;
    }
    if (zero_count > output_capacity) {
        return SOLC_E_OUTPUT_TOO_SMALL;
    }

    for (i = zero_count; i < input_len; ++i) {
        int decoded = base58_value((unsigned char)input[i]);
        unsigned int carry;
        size_t j;
        if (decoded < 0) {
            return SOLC_E_INVALID_ENCODING;
        }
        carry = (unsigned int)decoded;
        for (j = 0u; j < byte_count; ++j) {
            carry += (unsigned int)output[j] * 58u;
            output[j] = (uint8_t)(carry & 0xffu);
            carry >>= 8u;
        }
        while (carry != 0u) {
            if (byte_count >= output_capacity - zero_count) {
                return SOLC_E_OUTPUT_TOO_SMALL;
            }
            output[byte_count++] = (uint8_t)(carry & 0xffu);
            carry >>= 8u;
        }
    }

    for (i = 0u; i < byte_count / 2u; ++i) {
        uint8_t temporary = output[i];
        output[i] = output[byte_count - i - 1u];
        output[byte_count - i - 1u] = temporary;
    }
    if (zero_count != 0u && byte_count != 0u) {
        memmove(output + zero_count, output, byte_count);
    }
    if (zero_count != 0u) {
        memset(output, 0, zero_count);
    }
    *output_len = zero_count + byte_count;
    return SOLC_OK;
}

static int base64_value(unsigned char character) {
    if (character >= 'A' && character <= 'Z') {
        return (int)(character - 'A');
    }
    if (character >= 'a' && character <= 'z') {
        return (int)(character - 'a') + 26;
    }
    if (character >= '0' && character <= '9') {
        return (int)(character - '0') + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

solc_status solc_base64_encode(const uint8_t *input,
                               size_t input_len,
                               char *output,
                               size_t output_capacity,
                               size_t *output_len) {
    size_t required;
    size_t input_pos = 0u;
    size_t output_pos = 0u;

    if ((input_len != 0u && input == NULL) || output_len == NULL) {
        return SOLC_E_NULL;
    }
    if (input_len > (SIZE_MAX - 2u) / 4u * 3u) {
        return SOLC_E_OVERFLOW;
    }
    required = ((input_len + 2u) / 3u) * 4u;
    *output_len = required;
    if (output == NULL) {
        return SOLC_OK;
    }
    if (output_capacity < required) {
        return SOLC_E_OUTPUT_TOO_SMALL;
    }

    while (input_len - input_pos >= 3u) {
        uint32_t triple = ((uint32_t)input[input_pos] << 16u) |
                          ((uint32_t)input[input_pos + 1u] << 8u) |
                          (uint32_t)input[input_pos + 2u];
        output[output_pos] = BASE64_ALPHABET[(triple >> 18u) & 0x3fu];
        output[output_pos + 1u] = BASE64_ALPHABET[(triple >> 12u) & 0x3fu];
        output[output_pos + 2u] = BASE64_ALPHABET[(triple >> 6u) & 0x3fu];
        output[output_pos + 3u] = BASE64_ALPHABET[triple & 0x3fu];
        input_pos += 3u;
        output_pos += 4u;
    }
    if (input_pos < input_len) {
        uint32_t triple = (uint32_t)input[input_pos] << 16u;
        size_t remaining = input_len - input_pos;
        if (remaining == 2u) {
            triple |= (uint32_t)input[input_pos + 1u] << 8u;
        }
        output[output_pos] = BASE64_ALPHABET[(triple >> 18u) & 0x3fu];
        output[output_pos + 1u] = BASE64_ALPHABET[(triple >> 12u) & 0x3fu];
        output[output_pos + 2u] =
            remaining == 2u ? BASE64_ALPHABET[(triple >> 6u) & 0x3fu] : '=';
        output[output_pos + 3u] = '=';
    }
    return SOLC_OK;
}

solc_status solc_base64_decode(const char *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_capacity,
                               size_t *output_len) {
    size_t padding = 0u;
    size_t required;
    size_t input_pos;
    size_t output_pos = 0u;

    if ((input_len != 0u && input == NULL) || output_len == NULL ||
        (output_capacity != 0u && output == NULL)) {
        return SOLC_E_NULL;
    }
    *output_len = 0u;
    if ((input_len % 4u) != 0u) {
        return SOLC_E_NON_CANONICAL;
    }
    if (input_len != 0u && input[input_len - 1u] == '=') {
        padding = 1u;
        if (input_len >= 2u && input[input_len - 2u] == '=') {
            padding = 2u;
        }
    }
    required = (input_len / 4u) * 3u - padding;
    if (required > output_capacity) {
        return SOLC_E_OUTPUT_TOO_SMALL;
    }

    for (input_pos = 0u; input_pos < input_len; input_pos += 4u) {
        int a = base64_value((unsigned char)input[input_pos]);
        int b = base64_value((unsigned char)input[input_pos + 1u]);
        int c = input[input_pos + 2u] == '='
                    ? -2
                    : base64_value((unsigned char)input[input_pos + 2u]);
        int d = input[input_pos + 3u] == '='
                    ? -2
                    : base64_value((unsigned char)input[input_pos + 3u]);
        int final_quartet = input_pos + 4u == input_len;
        uint32_t triple;

        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (!final_quartet && (c < 0 || d < 0)) ||
            (c == -2 && d != -2) || (c == -2 && (b & 0x0f) != 0) ||
            (d == -2 && c >= 0 && (c & 0x03) != 0)) {
            return c == -2 || d == -2 ? SOLC_E_NON_CANONICAL
                                      : SOLC_E_INVALID_ENCODING;
        }
        triple = ((uint32_t)a << 18u) | ((uint32_t)b << 12u);
        if (c >= 0) {
            triple |= (uint32_t)c << 6u;
        }
        if (d >= 0) {
            triple |= (uint32_t)d;
        }
        if (output_pos < required) {
            output[output_pos++] = (uint8_t)(triple >> 16u);
        }
        if (output_pos < required) {
            output[output_pos++] = (uint8_t)(triple >> 8u);
        }
        if (output_pos < required) {
            output[output_pos++] = (uint8_t)triple;
        }
    }
    *output_len = required;
    return SOLC_OK;
}
