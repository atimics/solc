# Core program formats

This layer decodes program-owned bytes without importing an SDK into the C
library. All integers are little-endian. Accepted instructions consume the
complete input; the official SPL decoders often ignore suffix bytes, but this
reference rejects them unless the suffix belongs to a documented variable
payload.

## System Program

System instructions use bincode's fixed-width enum representation: a `u32`
variant followed by its fields in declaration order. Public keys are 32 bytes,
integers are fixed-width, and strings are a `u64` byte count followed by UTF-8.
Seeds are limited to 32 bytes.

| Tag | Payload |
|---:|---|
| 0 | `lamports:u64`, `space:u64`, `owner:[32]` |
| 1 | `owner:[32]` |
| 2 | `lamports:u64` |
| 3 | `base:[32]`, `seed:string`, `lamports:u64`, `space:u64`, `owner:[32]` |
| 4 | empty |
| 5 | `lamports:u64` |
| 6, 7 | `authority:[32]` |
| 8 | `space:u64` |
| 9 | `base:[32]`, `seed:string`, `space:u64`, `owner:[32]` |
| 10 | `base:[32]`, `seed:string`, `owner:[32]` |
| 11 | `lamports:u64`, `seed:string`, `owner:[32]` |
| 12 | empty |

## Compute Budget and v1

Legacy/v0 Compute Budget instructions use a one-byte tag. Tags 1, 2, and 4
carry `u32`; tag 3 carries a `u64`; tag 0 is deprecated and rejected when
collecting a transaction configuration. Duplicate settings are rejected. Heap
requests must be 32–256 KiB and a multiple of 1024.

Legacy tag 3 is a price in micro-lamports per compute unit. A v1 transaction
instead carries a total priority fee in lamports. The compatibility adapter
therefore requires an explicit compute-unit limit and computes:

```text
priority_fee = ceil(compute_unit_price_micro_lamports * compute_unit_limit / 1_000_000)
```

The multiplication and rounding are overflow checked. No inverse adapter is
provided because the conversion loses information.

## Token instructions

Classic SPL Token tags 0–24 and the Token-2022 superset tags 0–44 are exposed.
The fixed base payloads are parsed into fields. Compact instruction options are
`u8 0` or `u8 1 + pubkey`. Tags 21 and 29 carry a sequence of known `u16`
Token-2022 extension types. Tag 24 owns the remainder as UTF-8.

Token-2022 extension-family tags are envelopes: the top-level tag is decoded
and the extension subinstruction remains a borrowed byte slice. That boundary
is deliberate—the subprogram family owns its next discriminant, while this
layer still preserves exact round trips.

The names and discriminants are emitted from
`schemas/program_instructions.def`, a small X-macro schema that is also readable
as a wire table. Payload logic remains handwritten and reviewed.

## Token accounts

The fixed base layouts are:

- Mint: 82 bytes;
- token account: 165 bytes;
- multisig: 355 bytes with 11 public keys.

Account `COption` values use a `u32` tag and fixed-size body. The canonical
decoder requires a zero body when the tag is `None`, validates booleans and
account-state ordinals, and borrows all public keys.

An extended Token-2022 mint pads bytes 82–164 with zero; both extended mints
and accounts put `AccountType` at byte 165 and TLV at byte 166. Each TLV entry
is `extension_type:u16`, `length:u16`, then value. The decoder checks bounds,
known type, mint/account applicability, duplicate types, zero padding, and the
special non-extensible 355-byte multisig collision.

## Address Lookup Table accounts

The fixed metadata region is 56 bytes:

```text
u32 state (=1)
u64 deactivation_slot
u64 last_extended_slot
u8  last_extended_slot_start_index
u8  authority_option
[32] authority when present
u16 zero padding
zero fill to byte 56
[32] addresses...
```

An uninitialized state (`0`) has its own error. The address tail must be a
multiple of 32 and contain at most 256 entries. The visibility helper applies
the official same-slot extension rule. It intentionally does not claim to
decide deactivation, which also requires the runtime's `SlotHashes` sysvar.

## Compatibility evidence

`tests/program-oracle` uses exact-pinned official crates to regenerate every
committed program vector. `compat/program-source.sha256` additionally pins the
specific source files reviewed for these layouts. C unit, truncation, negative,
sanitizer, and fuzz tests exercise the same boundaries.
