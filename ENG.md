# solc-wire Transaction Compiler — Engineering Design

Status: working draft

This document translates [PRD.md](PRD.md) into component ownership, data
contracts, and a delivery sequence. Existing byte behavior remains governed by
the executable format documents under `docs/`.

## Engineering objective

Compile explicit transaction intent into a canonical Solana transaction model,
export the exact signing message and ordered signer list, attach externally
produced signatures without changing the message, and finalize strict binary
wire bytes.

The first supported product path is offline legacy construction. Versioned
transactions, state acquisition, assurance receipts, and execution evidence are
separate later slices.

## System flow

```text
plan JSON / safe Rust types / C instruction inputs
                         |
                         v
                 Rust ownership layer
             parse, bound, allocate, present
                         |
                         v
           deterministic C builder and codecs
       merge privileges, order keys, compile indices
                         |
                         v
     transaction template + message + signer manifest
                         |
                         v
              external wallet / HSM / signer
                         |
                         v
             attach -> verify -> final encode
                         |
                         v
                   binary / hex / base64
```

Network submission is downstream of this system. Private-key access is upstream
or external to it.

## Architectural placement rule

- Reproducible protocol decisions over finite inputs belong in deterministic
  kernels.
- Buffer ownership, JSON, files, process I/O, and provider coordination belong
  in Rust.
- Current-state acquisition belongs in an explicit adapter.
- Bank authority, execution, fork choice, and consensus remain external.

The rule is not C for its own sake. It ensures every protocol decision has one
authoritative implementation and no ambient authority.

## Existing foundation

### C kernel

`src/builder.c` and `include/solc/builder.h` already:

- accept instructions expressed as program pubkeys, account pubkeys, and
  signer/writable requirements;
- insert the fee payer as first writable signer;
- merge duplicate account privileges;
- preserve discovery order inside Solana's four account groups;
- compile program and instruction account indices;
- require signature-slot count to equal required signer count;
- emit the ordinary `solc_transaction` model.

The wire encoder then sizes and emits canonical legacy transaction bytes.
Message extraction and strict Ed25519 verification use the same validated model.

### Rust orchestration

`crates/solc-orchestrator` currently owns safe decode/inspect/verify operations,
text encoding, process integration, and RPC JSON construction. It does not
expose the legacy builder through safe Rust and does not have compile, attach,
or finalize commands.

### Program codecs

System, Compute Budget, and SPL Token instruction encoders can produce
instruction data, but the product still needs convenience constructors that pair
the data with the correct program and account metadata.

## Required product components

### Transaction plan schema

The CLI plan schema is a versioned, strict transport model. It is not the
internal C ABI.

Required properties:

- explicit schema identifier;
- explicit transaction version;
- canonical base58 public keys and blockhash input;
- ordered instruction list;
- typed instruction objects for supported recipes;
- raw instruction escape hatch with program, accounts, and base64 data;
- strict unknown-field rejection;
- bounded request, instruction, account, and data sizes;
- no private key or seed fields;
- no compiled index fields.

The parser decodes text and performs transport-shape checks in Rust. C remains
authoritative for instruction encoding, account compilation, sanitizer rules,
and wire encoding.

### Safe builder API

Introduce owned public Rust types distinct from the current decoded summaries:

```text
LegacyTransactionPlan
InstructionPlan
AccountRequirement
CompiledTransaction
SigningBundle
FinalizedTransaction
```

The exact names are provisional. The type flow must make invalid transitions
hard:

- a plan has no signatures;
- a compiled transaction has stable message bytes and required signers;
- an attached-signature set is keyed by public key, not caller-provided slot;
- a finalized transaction exists only after all required signatures verify.

Rust owns the C scratch arrays and preserves all borrowed inputs for the duration
of build and encode calls. Public results are owned Rust bytes and summaries,
never raw FFI pointers.

### Signing bundle

The bundle carries the immutable output of compilation across an external signer
boundary. It contains or binds:

- schema and compiler version;
- transaction version;
- canonical message bytes;
- message digest with named algorithm;
- ordered signer public keys;
- transaction template or reconstruction inputs;
- static account and instruction summary;
- caller-supplied lifetime value;
- signature entries collected so far.

Attaching a signature must recompute the message from the canonical model and
verify that it matches the bundled bytes before accepting the signature. The
public key selects the required slot; callers cannot inject a positional index.

The deterministic bundle core contains no timestamp, endpoint, hostname,
current directory, or unrelated environment value.

### CLI

Proposed commands:

```text
solc-wire compile <plan> --out <bundle>
solc-wire message <bundle-or-transaction>
solc-wire attach <bundle> --signature <pubkey>=<signature>
solc-wire finalize <bundle> [--encoding raw|hex|base64]
solc-wire inspect <transaction>
solc-wire verify <transaction>
```

`attach` writes a new bundle by default rather than modifying the only copy in
place. Atomic file-replacement behavior and standard-output modes must be
specified before implementation.

Machine output uses versioned JSON schemas and stable error codes. Human output
never truncates a value that is required for signing or verification.

### Instruction recipes

The MVP should provide a small set of high-value recipes over existing codecs:

- System transfer;
- Compute Budget unit limit and unit price;
- SPL Token transfer-checked, mint-to-checked, and burn-checked.

Each recipe owns the exact program ID, account order, signer/writable flags, and
instruction fields for its pinned rule version. Raw instructions remain
available for programs outside the reviewed set.

Typed recipes must be tested against exact-pinned official program encoders.

## C kernel work for v0.1

The legacy builder is structurally sufficient but needs a product review:

1. Confirm sizing and capacity errors distinguish invalid plans from small
   caller arenas.
2. Add an API or safe-wrapper method that derives required signer order before
   non-placeholder signatures are present.
3. Decide whether the C builder should accept signature storage separately from
   signature contents so an unsigned template is not confused with a signed
   transaction.
4. Add builder vectors for multiple signers, duplicate promotion, repeated
   accounts, empty instruction account lists, and every capacity boundary.
5. Add instruction recipe helpers without putting application policy in
   `wire.c`.

No new C API is public until its ownership, scratch, lifetime, and sizing
contract is documented in the corresponding header.

## Signature lifecycle

The builder must distinguish three states:

```text
compiled          message fixed; signature slots required but empty
partially signed  message fixed; some verified signatures attached
finalized         all required signatures present and strictly verified
```

Zero-filled 64-byte slots are serialization placeholders, never evidence of a
signature. A compile result may serialize a template for inspection, but the
CLI must not label it a finalized transaction or send request.

Every attach operation:

1. decodes the signer pubkey and signature canonically;
2. proves the pubkey is a required signer;
3. rejects duplicate replacement unless an explicit safe workflow is designed;
4. verifies the signature over the exact stored/recomputed message;
5. records it in the signer-defined slot;
6. proves the message bytes did not change.

Finalization reruns structural sanitization, every signature verification, and
byte-exact encode/decode identity.

## Versioned transactions

v0 construction is a separate release because account placement becomes a
state-relative compilation problem.

The future v0 compiler consumes explicit lookup-table account bytes and all
lifecycle context required by pinned upstream rules. A deterministic state
kernel resolves visible addresses and selects only caller-authorized table
entries. Rust may acquire the bytes later, but acquisition provenance cannot
upgrade them into canonical state.

The selection policy—whether callers nominate tables, the compiler optimizes
placement, or both—must be specified before implementation. Do not silently
move an account into a lookup table if that changes a user's signing or audit
expectations.

V1 decoding remains supported. User-facing v1 construction waits until its
network availability and feature rules support an honest product promise.

## Packaging architecture

The package split is defined in [docs/PUBLISHING.md](docs/PUBLISHING.md):

- C/CMake source bundle first;
- `solc-wire-sys` for raw FFI and the one C source copy;
- `solc-wire` for safe construction;
- `solc-wire-cli` for command-line workflows;
- organization-scoped JavaScript/WASM after the MVP.

The current `solc-orchestrator` remains unpublished until those boundaries are
implemented. Registry publication workflows are not added before package
dry-runs and clean consumer tests exist.

## Deterministic constraints

All C builder work inherits `ci/kernel-policy.toml`:

- explicit inputs and caller-owned storage;
- checked size and pointer arithmetic;
- no allocation or mutable global state;
- no host I/O, time, randomness, environment, locale, or threading;
- no direct casts from untrusted bytes to C structs;
- stable errors and exact locations where meaningful;
- deterministic output across supported compilers and optimization levels;
- deliberate public ABI changes through `compat/public-api.symbols`.

Rust transport code may allocate but must bound all untrusted inputs before FFI.

## Verification strategy

### Kernel tests

- positive and negative builder unit tests;
- signer/account ordering and privilege-promotion matrices;
- null, capacity, overflow, and boundary cases;
- mutation and fuzzing over instruction/account metadata;
- encode/decode identity for every generated transaction;
- GCC/Clang and optimization-independent transcripts.

### Oracle tests

- generate the same transaction intent with exact-pinned official Rust crates;
- compare message bytes, signer order, account order, compiled indices, and full
  wire bytes;
- pin only the source files that define the compared compilation behavior;
- require reviewed drift before fixture refresh.

### Signing tests

- deterministic test keys for every supported transaction layout;
- valid multi-signer attachment in different arrival orders;
- corrupted signature, wrong pubkey, duplicate, missing signer, and changed
  message failures;
- provider unavailable/failure distinct from invalid signature.

### Product and package tests

- golden plan-to-bundle and bundle-to-wire fixtures;
- CLI and safe Rust semantic equality;
- standard input/output and bounded malformed JSON tests;
- CMake install and `find_package()` consumer build;
- Cargo test from generated `.crate` artifacts;
- website claims checked against an explicit implemented/next inventory.

## Delivery sequence

### E0 — plan and safe builder

- specify strict legacy plan and signing-bundle schemas;
- expose the legacy C builder through owned Rust types;
- add System transfer and raw-instruction construction;
- produce exact message bytes and signer manifest;
- add official-oracle builder fixtures.

### E1 — sign and finalize

- implement pubkey-addressed signature attachment;
- add compiled, partially signed, and finalized type states;
- add strict final verification and binary/hex/base64 output;
- complete CLI compile, attach, and finalize journey;
- migrate one real signing or forwarding path.

### E2 — first packages

- complete CMake config, pkg-config, and consumer tests;
- split sys, safe Rust, and CLI packages;
- add package dry-runs and explicit file-list review;
- publish an immutable source-first GitHub prerelease;
- publish registries only after the prerelease artifacts pass clean installs.

### E3 — v0 and lookup tables

- pin lookup compilation and lifecycle sources;
- add offline state-relative lookup resolution;
- define explicit table-selection policy;
- differentially compare compiled v0 messages and wire bytes;
- add optional Rust acquisition only after offline replay works.

Assurance receipts, blockhash freshness, current account claims, full execution,
and consensus remain later claim boundaries described in
[docs/MICROVALIDATORS.md](docs/MICROVALIDATORS.md).

## Change review checklist

Before adding a builder feature:

- Is this construction semantics, transport ergonomics, context acquisition, or
  external authority?
- Does one kernel own the protocol rule?
- Can the entire build replay without network access?
- Are unsigned, partially signed, and finalized states unambiguous?
- Does the caller avoid compiled indices and hidden account reordering?
- Are all new input lengths, counts, capacities, and errors bounded?
- Are message bytes proven unchanged across signature attachment?
- Does an exact-pinned oracle compare the same semantic surface?
- Do packaged consumers exercise the files that will actually ship?
