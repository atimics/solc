# solc-wire Transaction Compiler — Product Requirements

Status: working draft

## Product thesis

`solc-wire` is a small deterministic transaction compiler: describe a Solana
transaction once, receive canonical binary bytes, exact signing payloads, and
precise validation without importing a full Solana SDK into the protected
component.

The primary promise is:

> Describe the transaction. Get the exact bytes to sign and send.

Assurance is a property of construction, not the first product. The same strict
kernel can later inspect externally constructed transactions and emit replayable
evidence, but receipts and live state acquisition do not block the builder MVP.

## Problem

Constructing a Solana transaction from instructions requires applications to
handle details that are easy to get subtly wrong:

- duplicate account privileges must be promoted correctly;
- fee payer and required signers must occupy canonical positions;
- program and account indices depend on the compiled account order;
- message and signature placement differs across transaction versions;
- ShortU16 counts and instruction data must have one canonical encoding;
- the bytes covered by signatures are not always the whole envelope;
- hand-written program instruction tags and integers can drift from upstream.

The usual answer is to import a large language-specific SDK. That is awkward for
C, embedded, agentic, cross-language, offline-signing, and narrow bridge
components. Smaller applications often respond with ad hoc builders that mix
protocol validation, state acquisition, signing, and network submission.

Developers need a portable compiler that owns the byte rules while leaving
keys, networks, and application intent at explicit boundaries.

## Initial user

The first user is a developer constructing Solana transactions in an
SDK-constrained environment and passing them across a signing or forwarding
boundary.

Reference environments include:

- a C or embedded application;
- a bridge component that must minimize its trusted dependencies;
- an offline or HSM-backed signing service;
- a Rust, Node, or agent pipeline that wants deterministic wire output without
  duplicating Solana compilation rules.

Wallets, custody products, auditors, and general assurance systems are later
distribution paths, not simultaneous MVP personas.

## User jobs

1. Express a transaction in terms of payer, lifetime, instructions, pubkeys,
   signer flags, and writable flags—not compiled indices.
2. Obtain the exact ordered signer list and message bytes required by a wallet,
   HSM, or external signer.
3. Attach signatures without rebuilding or changing the message.
4. Emit canonical binary, hexadecimal, or base64 transaction bytes.
5. Inspect and verify the finalized result before it crosses a network boundary.
6. Reproduce the same output across supported compilers and hosts.

## Product principles

### Construction first

The MVP ends in usable transaction bytes. It does not require an evidence graph,
live RPC connection, or full SVM runtime.

### No hidden authority

The compiler operates on explicit inputs. A caller supplies the recent
blockhash, lookup-table state, signatures, and instruction intent. The compiler
does not imply that a blockhash is fresh or supplied state is current.

### Keys stay outside

The product creates signing payloads and attaches returned signatures. It does
not load, discover, derive, store, or transmit private keys.

### One semantic implementation

Canonical ordering, encoding, and sanitizer rules live in deterministic kernels.
Rust and future JavaScript layers provide ownership and ergonomics without
rewriting those rules.

### Refuse partial success

Invalid flags, insufficient capacity, unsupported versions, missing signatures,
malformed instruction data, and inconsistent plans fail with stable errors. The
compiler never emits a partially valid transaction.

## Core workflow

```text
transaction plan
    -> compile
    -> transaction template + ordered signer manifest + exact message bytes
    -> external wallet/HSM signatures
    -> attach signatures
    -> finalize and verify
    -> binary / hex / base64 transaction
```

An illustrative CLI journey is:

```text
solc-wire compile plan.json --out transaction.bundle
solc-wire attach transaction.bundle --signature PUBKEY=SIGNATURE
solc-wire finalize transaction.bundle --encoding base64
```

Command names and the bundle representation remain engineering decisions. The
separation between compilation, external signing, and finalization is required.

## Transaction plan

The versioned plan format contains:

- transaction format selection;
- fee payer public key;
- recent blockhash or other supported lifetime value;
- ordered high-level instructions;
- program public key for every raw instruction;
- ordered account public keys with signer and writable requirements;
- instruction data as typed program input or explicit bytes;
- optional caller-supplied lookup-table context for versioned transactions;
- output encoding preference outside the deterministic plan core.

The plan does not contain compiled account indices. It may contain zeroed
signature slots or external signature references only in the signing bundle,
not in the compilation intent.

Unknown fields and unknown schema versions fail closed. A normalized plan digest
may be added later, but canonical transaction bytes remain the authoritative
build output.

## Build outputs

Compilation produces a signing bundle containing or binding:

- canonical transaction model and unsigned/template wire bytes;
- exact serialized message bytes;
- ordered required signer public keys;
- transaction version and header summary;
- static account order and compiled instruction summary;
- stable build diagnostics;
- compiler and rule-set version;
- digest of the message bytes;
- explicit statements about caller-supplied lifetime and state inputs.

The bundle is operational: it is consumed by attach and finalize. It is not a
passive observability receipt.

## Product surface

### C API

The C API is the authoritative, allocation-free compiler surface. Callers own
all input, scratch, signature, and output storage. Exact sizing calls allow
bounded allocation outside the kernel.

### Safe Rust API

The Rust library owns buffers and lifetimes, exposes ergonomic plan and result
types, and calls the C kernel for protocol semantics. It contains no network
client in the default feature set.

### CLI

The CLI exposes compile, message, attach-signature, finalize, inspect, verify,
and encoding operations. It accepts files and standard input, emits stable
machine-readable errors, and never loads private keys.

### JavaScript/WASM

A scoped browser and Node package follows the MVP. It wraps the same kernel via
WASM rather than spawning the migration process adapter or implementing a new
TypeScript compiler.

## Delivery scope

### Foundation — implemented

- strict legacy, v0, and v1 decode, sanitize, and byte-exact encode;
- checked legacy transaction builder with privilege merging and canonical
  account order;
- System, Compute Budget, SPL Token/Token-2022 base, and Address Lookup Table
  codecs;
- exact signing-message extraction and signature verification;
- base58/base64, process, and network-free RPC-format adapters;
- deterministic, sanitizer, fuzz, oracle, migration, and runtime evidence.

The missing product layer is an ergonomic public compile/sign/finalize workflow.

### v0.1 MVP — legacy compiler

- versioned transaction-plan schema;
- safe Rust wrapper over the existing legacy builder;
- compile command producing a signing bundle;
- message export and ordered signer manifest;
- attach-signature and finalize commands;
- System transfer, Compute Budget, and core SPL Token construction examples;
- binary, hex, and base64 output;
- clean C/CMake and Rust package-consumer tests;
- GitHub source release and package dry-runs.

### v0.2 — versioned transaction construction

- v0 transaction builder;
- caller-supplied offline lookup-table context;
- deterministic static-versus-loaded account compilation;
- exact-pinned differential lookup fixtures;
- optional context acquisition in Rust, clearly labeled as provenance;
- v1 construction only when network support justifies a user-facing promise.

### Later — assurance and execution evidence

- claim-based receipts for externally constructed transactions;
- offline replayable context bundles;
- account preconditions and blockhash-lifetime checks over supplied context;
- external simulation or execution evidence bound to engine versions;
- upstream-drift and multi-provider monitoring.

## Explicit non-goals for v0.1

- private-key management or signing;
- choosing application intent on the user's behalf;
- fetching a recent blockhash or current account state;
- address-lookup optimization or live lookup-table resolution;
- submitting transactions over HTTP/TLS;
- full SVM simulation or execution;
- fork choice, consensus, inclusion, confirmation, or finality;
- replacing a full SDK for account discovery and application workflows;
- presenting zero signature placeholders as cryptographic authorization.

## MVP acceptance criteria

The legacy compiler MVP is complete when:

1. one supported plan compiles through the CLI and safe Rust API to the same
   canonical transaction bytes;
2. callers never specify compiled account indices or canonical account order;
3. duplicate privileges, fee-payer placement, signer ordering, and signature
   count are handled by the C builder;
4. the signing bundle identifies every required signer in wire order and
   exports the exact message bytes;
5. attaching a valid external signature does not modify the message bytes;
6. finalization rejects missing, duplicate, unknown, malformed, or invalid
   signatures;
7. finalized output passes strict decode, sanitizer, signature verification,
   and byte-exact round trip;
8. committed builder examples match exact-pinned official Rust output;
9. outputs are deterministic across the supported compiler and optimization
   matrix;
10. clean consumers build from the exact C source bundle and Rust `.crate`
    artifacts intended for release;
11. existing policy, ABI, fuzz, sanitizer, migration, and vector gates remain
    green.

## Success measures

Technical success:

- no reference integration contains handwritten account-index ordering;
- all committed construction plans reproduce their canonical bytes;
- no divergence from the exact-pinned oracle on the declared builder corpus;
- package consumers require no production Solana SDK dependency;
- protocol drift fails a visible compatibility gate rather than being adopted
  silently.

Product success:

- a new user can produce their first canonical legacy transaction and signing
  payload from one documented example;
- the compiler is used at a real signing or forwarding boundary;
- errors identify the exact construction invariant that failed;
- users adopt the safe library or CLI without copying the C builder internally.

## Principal risks

### Ergonomics remain too close to the wire

The current C builder is intentionally explicit and caller-owned. The safe API,
plan format, examples, and capacity management must remove manual scratch and
index work without hiding protocol inputs.

### The product expands back into a validator

State acquisition, full execution, and consensus would delay the builder users
actually want. New subsystems must be justified by a concrete construction or
signing-boundary job.

### Signing bundles create security ambiguity

Zero signature slots, message bytes, and finalized transactions must have
different types and presentation. Finalization must prove every required slot
contains a valid signature.

### Language wrappers drift

Rust and JavaScript convenience code may be tempted to compile accounts itself.
Differential tests and ownership rules must keep protocol semantics in one
kernel.

### Name collision

`solc` is strongly associated with the Solidity compiler, while `solc-wire`
sounds narrower than the eventual developer product. Keep the repository name
for now, but decide the external product and registry names before promotion.

## Open product decisions

- Which existing bridge or signing flow becomes the reference integration?
- How much typed instruction construction belongs in v0.1 versus raw instruction
  data plus recipes?
- Should signing bundles be directories, one JSON file with detached binary, or
  a single binary container?
- Which product name can be owned consistently across GitHub, crates.io, npm,
  and documentation?

Engineering ownership and sequencing are specified in [ENG.md](ENG.md).
Package channels and release gates are specified in
[docs/PUBLISHING.md](docs/PUBLISHING.md).
