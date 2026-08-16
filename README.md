# solc-wire

A deterministic Solana transaction compiler and dependency-free C reference for
wire formats, with a small Rust orchestration layer and CI designed to expose
upstream drift.

The implemented foundation provides strict decode, validation, and byte-exact
encode for:

- legacy transactions;
- v0 transactions and address lookup table references;
- the v1 transaction format from SIMD-0385 currently present in Anza's SDK
  master branch.

It also provides canonical base58/base64, portable SHA-256, exact signing-byte
extraction, strict Ed25519 verification through an explicit provider boundary,
network-free Rust JSON-RPC adapters, and a checked native SBF entrypoint/CPI
foundation.
It also owns strict System, Compute Budget, SPL Token/Token-2022 base, and
Address Lookup Table account codecs, plus a checked legacy transaction builder.

The current C builder compiles explicit instructions and account requirements
into a canonical legacy transaction model. The next product layer exposes that
foundation as an ergonomic compile, external-sign, and finalize workflow. See
[PRD.md](PRD.md) for the product contract and [ENG.md](ENG.md) for its delivery
sequence.

This is intentionally not another application built on a large Solana SDK. The
C library owns the format and invariants. Rust provides a safe CLI boundary and
test orchestration without depending on Solana crates. Official SDK source and
an exact-pinned, CI-only Rust decoder are external compatibility oracles, not
production dependencies.

## Quick start

```sh
make check
cargo run -p solc-orchestrator --bin solc-wire -- \
  inspect @tests/vectors/v1.hex
```

The CLI accepts inline hexadecimal, `@file.hex`, raw `@file`, or raw stdin via
`@-`:

```sh
solc-wire inspect 0100...
solc-wire roundtrip @transaction.bin
solc-wire message @transaction.bin
solc-wire verify @signed-transaction.bin
solc-wire encode base64 @transaction.bin
solc-wire rpc-send-request @transaction.bin 7
solc-wire check-vectors
```

## What is enforced

- strict one-to-one ShortU16 encoding, including rejection of aliases, overflow,
  and a continuing third byte;
- complete-input parsing with no trailing bytes;
- current legacy, v0, and v1 size and count limits;
- signature/header agreement;
- fee-payer, readonly, program-ID, and account-index invariants;
- v0 lookup-table cardinality and combined 256-account limit;
- v1 config-mask, heap-size, address uniqueness, and fixed-width instruction
  rules;
- decode → encode byte identity for every accepted input.

The structural transaction decoder allocates no memory. Parsed slices borrow from the input and parsed
instruction records use caller-owned scratch space. It does not verify Ed25519
signatures automatically; use the explicit verification API. It does not fetch
or resolve lookup-table accounts.

## Repository map

```text
PRD.md                            Product thesis, users, scope, and acceptance
ENG.md                            Transaction compiler architecture and delivery
site/                             Dependency-free GitHub Pages source
include/solc/wire.h               Stable C ABI and wire model
include/solc/encoding.h           Canonical base58/base64 ABI
include/solc/crypto.h             SHA-256 and crypto-provider ABI
include/solc/sbf.h                Entrypoint, account, PDA, and CPI ABI
include/solc/sbf_sdk.h            Thin official C SDK syscall adapter
include/solc/programs.h           Core program instruction/account ABI
include/solc/builder.h            Checked ergonomic legacy transaction builder
include/solc/rati_bridge.h        Strict RATi input and checked CPI migration
src/wire.c                        Zero-allocation codec and sanitizer
src/encoding.c                    Allocation-free canonical text codecs
src/crypto.c                      SHA-256, message bytes, provider dispatch
src/sbf.c                         Cast-free loader decoder and checked helpers
src/programs.c                    Strict System/Token/Compute/ALT codecs
src/builder.c                     Privilege merging and canonical account order
src/rati_bridge.c                 RATi borrowed views and SPL Token CPIs
crates/solc-orchestrator/         Safe Rust wrapper and CLI
migrations/trebuchet/             Narrow Node-to-Rust process adapter
tests/c/                          Golden, negative, and mutation tests
tests/fuzz/                       libFuzzer round-trip invariant
tests/vectors/                    Cross-language canonical vectors
tests/upstream-oracle/            Isolated exact-pinned official Rust check
tests/program-oracle/             Exact-pinned official program-format check
tests/runtime-oracle/             Exact-pinned Mollusk/Agave execution check
schemas/program_instructions.def  Reviewable instruction discriminant schema
compat/                           Pinned upstream source hashes
docs/WIRE_FORMAT.md               Byte-level format specification
docs/ENCODING_CRYPTO.md           M1 signing/encoding/RPC contract
docs/SBF_ABI.md                    M2 native loader and CPI contract
docs/PROGRAM_FORMATS.md            M3 core program byte contract
docs/RUNTIME_DIFFERENTIAL.md       M4 execution and network evidence contract
docs/MIGRATIONS.md                 M5 Signal/RATi/Trebuchet parity boundaries
docs/ARCHITECTURE.md              Trust boundaries and extension rules
docs/MICROVALIDATORS.md           Composable validation and authority layers
docs/PUBLISHING.md                Package channels and release gates
docs/ARCHAEOLOGY.md               What survived from Signal/RATi/Trebuchet
docs/CI.md                        Local/hosted deterministic-core test contract
```

## CI contract

Every push and pull request builds and tests C and Rust on Linux and macOS,
runs a machine-readable deterministic-core policy, GCC/Clang transcript
comparison, strict compiler warnings, Clippy, formatting, sanitizers, exact
oracles, the canonical vector gate, and bounded fuzzing. Scheduled checks add
coverage, ThreadSanitizer, GCC analysis, extended fuzzing, and upstream drift.
See [docs/CI.md](docs/CI.md) for the local commands and enforced boundaries.

See [ROADMAP.md](ROADMAP.md) for the path from transaction wire compatibility to
a broader C SVM reference.

## Sources

The compatibility baseline is recorded in
[docs/UPSTREAM.md](docs/UPSTREAM.md). Primary references include the
[official transaction structure documentation](https://solana.com/docs/core/transactions/transaction-structure),
the [Anza ShortU16 implementation](https://github.com/anza-xyz/solana-sdk/blob/master/short-vec/src/lib.rs),
and the [Anza message implementation](https://github.com/anza-xyz/solana-sdk/tree/master/message/src/versions).

Licensed under Apache-2.0.
