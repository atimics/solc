# Transaction wire format

This document describes the bytes accepted by `solc-wire` at compatibility
baseline `anza-xyz/solana-sdk@5190ff456079d17b64669bcb5eeac48dd595b91e`.
All integers are unsigned. Multi-byte fixed integers are little-endian.

## ShortU16

ShortU16 is a one-to-one base-128 representation of a `u16`:

| Value | Bytes |
| --- | --- |
| `0..127` | `0xxxxxxx` |
| `128..16383` | `1xxxxxxx 0yyyyyyy` |
| `16384..65535` | `1xxxxxxx 1yyyyyyy 000000zz` |

The final byte may not be zero unless it is the first byte. The third byte may
only use its low two bits and may not carry a continuation bit. These rules make
the encoding canonical; for example, `80 00` is not an alternate spelling of
zero.

## Transaction discriminator

The first byte determines the outer transaction layout:

| First byte | Layout |
| --- | --- |
| high bit clear | legacy/v0 envelope; byte is signature count |
| `0x81` | v1 envelope and message prefix |
| any other high-bit value | unsupported |

Historically the legacy/v0 signature count is described as ShortU16. The
current versioned-transaction reader uses the first byte as a discriminator and
therefore requires the feasible signature count to be below 128. The 1,232-byte
transaction cap makes a larger Ed25519 signature list impossible anyway.

## Legacy transaction

```text
u8             signature_count
[64] * count   signatures
u8             num_required_signatures
u8             num_readonly_signed_accounts
u8             num_readonly_unsigned_accounts
ShortU16       static_account_key_count
[32] * count   static_account_keys
[32]           recent_blockhash
ShortU16       instruction_count
Instruction * count
```

The first byte of the legacy message is the required-signature header byte; it
does not have a message-version prefix.

## V0 transaction

The outer signature list is identical to legacy. The message is:

```text
u8             version_prefix = 0x80
u8             num_required_signatures
u8             num_readonly_signed_accounts
u8             num_readonly_unsigned_accounts
ShortU16       static_account_key_count
[32] * count   static_account_keys
[32]           recent_blockhash
ShortU16       instruction_count
Instruction * count
ShortU16       lookup_count
AddressTableLookup * count
```

An address-table lookup is:

```text
[32]           lookup_table_account_key
ShortU16       writable_index_count
u8 * count     writable_indices
ShortU16       readonly_index_count
u8 * count     readonly_indices
```

Each lookup must load at least one address. Runtime account order is static
keys, all loaded writable keys, then all loaded readonly keys. A program ID must
refer to a static key; instruction account indices may refer to any resulting
key. The combined account count cannot exceed 256.

## Legacy/v0 instruction

```text
u8             program_id_index
ShortU16       account_index_count
u8 * count     account_indices
ShortU16       data_length
u8 * length    opaque_data
```

## V1 transaction (SIMD-0385)

V1 is deliberately different. The message prefix comes first and the fixed
number of signatures implied by the header is appended at the end:

```text
u8             version_prefix = 0x81
u8             num_required_signatures (max 12)
u8             num_readonly_signed_accounts
u8             num_readonly_unsigned_accounts
u32            transaction_config_mask
[32]           lifetime_specifier
u8             instruction_count (max 64)
u8             address_count (max 64)
[32] * count   static_account_keys
ConfigValues   fields selected by config mask
IxHeader * N   all instruction headers
IxPayload * N  all instruction payloads
[64] * required_signatures
```

Each v1 instruction header is four bytes:

```text
u8             program_id_index
u8             account_index_count
u16            data_length
```

Each payload then contains `account_index_count` one-byte indices followed by
`data_length` opaque bytes. V1 has no address lookup tables.

### V1 transaction config

Values are emitted in table order when their mask bits are present:

| Mask | Field | Width |
| --- | --- | --- |
| `0x03` | priority fee | `u64` |
| `0x04` | compute-unit limit | `u32` |
| `0x08` | loaded account-data size limit | `u32` |
| `0x10` | heap size | `u32` |

Unknown bits and partial priority-fee masks (`0x01` or `0x02`) are invalid. A
specified heap size must be a multiple of 1,024 in the inclusive range 32–256
KiB.

## Shared sanitizer rules

- The signature count equals `num_required_signatures`.
- At least one writable signer exists; this is the fee payer at account index 0.
- Required signatures plus readonly unsigned accounts fit in the static key
  list.
- Program index 0 is forbidden and every program ID is a static account.
- Every instruction account index is in range.
- Legacy/v0 transactions are at most 1,232 bytes; v1 is at most 4,096 bytes.
- V1 static addresses are unique.

Parsing and signature verification are separate operations. Acceptance by this
codec means structurally canonical and sanitizer-valid, not cryptographically
authorized or executable against current on-chain state.
