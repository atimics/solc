# Architecture

## One source of truth

The C library is the protocol implementation. The Rust crate is an orchestrator
over the exported ABI; it does not carry a second handwritten Solana parser.
This avoids the common failure mode where two local implementations agree with
each other but disagree with the network.

```text
untrusted bytes
    |
    v
C cursor parser -> structural model -> sanitizer -> canonical encoder
    |                                      |
    +---------- borrowed slices -----------+
                       |
                       v
                safe Rust summary/CLI
```

The only `unsafe` Rust is the narrow FFI module. Callers receive owned summaries
or re-encoded bytes, never raw borrowed C pointers.

## Memory model

`solc_transaction_decode` performs no allocation:

- signature, key, hash, account-index, data, and lookup slices point into the
  immutable input buffer;
- instruction and lookup records are written into caller-owned scratch arrays;
- the caller must keep both alive while using the decoded model.

The public `SOLC_MAX_DECODE_INSTRUCTIONS` and `SOLC_MAX_DECODE_LOOKUPS`
constants size scratch for every transaction that fits the supported packet
limits. Smaller application-specific arenas are allowed and fail explicitly
when exhausted.

The same model is also an encoding API. A caller may construct slices and
records directly, run `solc_transaction_encode` once with a null output to
obtain the exact size, then encode into a correctly sized buffer.

## Failure semantics

Every public operation returns a stable status code. Decode and encode calls can
also return a byte offset. The decoder is fail-closed: aliases, unknown versions,
unknown v1 config bits, incomplete data, extra data, invalid indices, and
capacity exhaustion are errors.

## Compatibility strategy

The repository separates four kinds of tests:

1. Local invariants: C unit, negative, truncation, and deterministic mutation
   tests.
2. Cross-language invariants: the Rust wrapper decodes and byte-round-trips the
   committed corpus through the C ABI.
3. Official oracle: an isolated CI-only crate asks exact-pinned Anza crates to
   sanitize and byte-round-trip the same corpus.
4. Upstream drift: scheduled CI hashes only the official source files that
   define transaction serialization and sanitization.

An upstream hash change is not automatically adopted. It opens a review:

1. inspect the official diff and relevant SIMD;
2. add or change a golden vector;
3. implement C behavior and negative tests;
4. update the byte specification;
5. refresh the pinned hashes in one commit.

## Transport and cryptography layer

M1 adds canonical base58/base64 and portable SHA-256 in C. Ed25519 crosses an
explicit provider table; the Rust orchestrator supplies the default strict
provider. Transaction message encoding remains owned by C, including the
different location of v1 signatures. RPC JSON stays in Rust and feeds decoded
bytes back through the C sanitizer.

See [ENCODING_CRYPTO.md](ENCODING_CRYPTO.md) for the executable contract.

## Microvalidator layers

The transaction codec is one deterministic validation kernel, not a miniature
network validator. Future state or execution work follows a three-way placement
rule: reproducible decisions over explicit inputs belong in a deterministic
kernel; acquisition of current context belongs in Rust orchestration; and
selection of the network's accepted history belongs in an external runtime or
consensus system.

This keeps lookup resolution, account predicates, and blockhash-lifetime rules
testable without giving the C core network authority. It also prevents Rust
orchestration from becoming a second handwritten protocol implementation.
Full SVM execution, if implemented locally, is a separate deterministic kernel;
fork choice and consensus remain an external agreement plane.

See [MICROVALIDATORS.md](MICROVALIDATORS.md) for the claim model, responsibility
matrix, package boundaries, and supported research hypotheses.

## Remaining non-goals

- private-key loading and Ed25519 signing;
- an HTTP/TLS network client;
- lookup-table account resolution;
- extension-specific Token-2022 payload semantics beyond the top-level envelope;
- a production in-process SVM runtime or validator (Mollusk and Agave remain
  isolated test oracles);
- claiming that SDK-master features are active on every cluster.

Those belong in the explicit state, execution, control, or agreement layers
described in [MICROVALIDATORS.md](MICROVALIDATORS.md), not in the transaction
wire core.
