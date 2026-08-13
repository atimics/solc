# Native SBF program ABI

This is the byte contract implemented by `solc_sbf_parameters_decode`, pinned
to the C SDK distributed with Agave 2.3.13 and platform-tools v1.48.

## Entrypoint trust boundary

The native loader calls:

```c
uint64_t entrypoint(const uint8_t *input);
```

It does not pass a buffer length. On chain, the loader is responsible for
constructing a valid allocation and the program must select
`SOLC_SBF_LOADER_TRUSTED_INPUT_LEN`. Native tests, fuzzers, captured fixtures,
and external runtimes must pass a real extent; that mode rejects every
truncated prefix and all trailing bytes.

The official C helper reads `uint64_t` values by casting the current byte
pointer. This implementation instead reads every little-endian integer one byte
at a time, checks size arithmetic, validates duplicate indices, and exposes
borrowed pointers only after their complete regions are proven present.

## Serialized parameters

```text
u64 account_count
AccountRecord * account_count
u64 instruction_data_len
u8  instruction_data[instruction_data_len]
u8  current_program_id[32]
```

A unique account record is:

```text
u8  duplicate_marker = 0xff
u8  is_signer
u8  is_writable
u8  executable
u8  padding[4]
u8  key[32]
u8  owner[32]
u64 lamports
u64 original_data_len
u8  data[original_data_len]
u8  permitted_realloc_region[10_240]
u8  alignment_padding[0..7]  // next field at absolute 8-byte alignment
u64 rent_epoch
```

A duplicate is eight bytes: a prior account index followed by seven ignored
padding bytes. Forward and self references are rejected. The decoded duplicate
shares key, owner, lamports, length, and data pointers with its source.

Flag bytes are normalized to zero or one, matching the loader helper's
non-zero boolean semantics. Padding is ignored because the runtime does not
define it as canonical data.

## Checked account access

The helpers in `solc/sbf.h` centralize:

- exact key and owner checks;
- signer and writable requirements;
- minimum account-data lengths;
- little-endian lamport reads/writes;
- writable-only mutable data access;
- realloc length updates capped at the loader-provided 10 KiB region.

This prevents application code from scattering raw pointer casts and flag
checks through business logic.

## PDA seed validation

The limits shared with the current address implementation are 16 seeds and 32
bytes per seed. Empty seeds are valid; a non-empty seed must have a non-null
pointer. `solc_pda_signers_validate` applies the same rule to every signer seed
group before a syscall adapter is allowed to use it.

The validator checks shape and limits only. Curve rejection and the actual PDA
derivation remain responsibilities of the runtime syscall or a separately
reviewed host provider.

## Checked CPI construction

The builder enforces the SDK/runtime limits:

| Resource | Limit |
| --- | ---: |
| instruction data | 10,240 bytes |
| instruction account metas | 255 |
| unique account-info records | 128 |

It refuses writable escalation and refuses signer escalation unless the caller
explicitly marks that signer as PDA-authorized. PDA seed groups must still pass
`solc_pda_signers_validate` before invocation. Repeated metas are preserved,
while the syscall account-info list is deduplicated by public key. The
executable program account is always included at finalization.

The output is a host-neutral instruction model. `solc/sbf_sdk.h` converts it and
validated signer seeds to the SDK's `SolInstruction`, `SolAccountMeta`, and
`SolAccountInfo`, then calls `sol_invoke_signed`. Policy and byte validation
remain testable without an SBF compiler.

## Toolchain and artifact evidence

`compat/sbf-toolchain.toml` pins Agave 2.3.13, platform-tools v1.48, rustc
1.84.1-dev, LLVM 19.1.7, SBPF v2, the five ABI/linker input hashes, and the
stripped reference artifact's size and SHA-256. The reference program uses the
pointer-only trusted decoder exactly as a deployed C entrypoint would.

`scripts/check-sbf-toolchain.sh` rebuilds it with path remapping and checks all
of those values. CI caches the large official compiler bundle but never accepts
a changed SDK header or artifact silently.
