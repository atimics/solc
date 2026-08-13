# Encoding, signing, and transport boundary

This document specifies the M1 interfaces layered above the transaction wire
model. The C implementation remains allocation-free and has no JSON, socket,
TLS, operating-system crypto, or Rust dependency.

## Canonical text encodings

`solc_base58_encode` and `solc_base58_decode` use the Bitcoin/Solana alphabet:

```text
123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz
```

Leading zero bytes map one-to-one to leading `1` characters. Whitespace and the
excluded characters `0`, `O`, `I`, and `l` are errors. Every accepted string
decodes and re-encodes byte-for-byte to itself.

`solc_base64_encode` and `solc_base64_decode` implement padded RFC 4648 base64
with the `+/` alphabet. The decoder requires a multiple-of-four input length,
padding in the final quartet only, the exact required padding, and zero unused
bits. It rejects whitespace, URL-safe `-_`, missing padding, excess padding,
and aliases such as `Zh==` for the byte `66`.

Encoders do not append a NUL byte. They return an explicit length. Neither
decoder silently ignores input.

## Exact bytes covered by signatures

An Ed25519 signature covers the serialized message, not the entire transaction
envelope:

| Transaction | Signed byte range |
| --- | --- |
| legacy | header byte immediately after the signature list through end |
| v0 | `0x80` version prefix immediately after the signature list through end |
| v1 | initial `0x81` version prefix through the byte before appended signatures |

`solc_transaction_message_encode` derives these bytes from the validated C
model. It does not rely on offsets supplied by an application. A null output
performs exact sizing, allowing the caller to supply its own arena.

The `message` CLI command prints these bytes as hexadecimal. The three
`signed-*.hex` fixtures use a deterministic test key and are accepted by both
the strict Rust provider and the exact-pinned Anza verifier.

## Cryptographic providers

`solc_crypto_provider` is an explicit table containing a context pointer and
SHA-256/Ed25519 callbacks. Missing callbacks return
`SOLC_E_CRYPTO_UNAVAILABLE`; unexpected provider results are normalized to
`SOLC_E_PROVIDER_FAILURE`; a validly executed verification that rejects a
signature returns `SOLC_E_SIGNATURE_INVALID` and its signer index.

The C library includes a portable, allocation-free SHA-256 provider with NIST
known-answer tests. It intentionally does not ship a new handwritten elliptic
curve implementation. The Rust orchestrator installs `ed25519-dalek` 2.2.0 and
uses strict verification, matching the strict verification policy used by the
official Solana signature crate. Other hosts can supply a reviewed platform,
HSM, or embedded provider without changing transaction parsing.

The SHA-256 reported by the CLI is explicitly named `messageSha256`; it is a
diagnostic digest and is not presented as Solana's Blake3 message hash or as a
transaction signature.

## RPC JSON isolation

RPC request and response adapters live in Rust under `rpc.rs`. They:

- request and emit base64 transaction payloads;
- set `maxSupportedTransactionVersion` to 1 for `getTransaction` requests;
- validate transaction bytes through the C codec before sending and after
  receiving;
- require exact 64-byte canonical base58 transaction signatures;
- surface JSON-RPC errors rather than confusing them with missing results.

No network client is included. Endpoint selection, TLS, retries,
authentication, rate limits, and secret handling belong to an application or
the later differential harness.

## Regression gates

- RFC 4648 and Bitcoin base58 known-answer tests;
- exhaustive deterministic round trips for input lengths 0 through 256;
- negative padding, alphabet, whitespace, and capacity cases;
- NIST SHA-256 known-answer tests;
- fake-provider tests proving message boundaries and signer/key ordering;
- real strict Ed25519 positive and bit-corruption tests for every transaction
  layout;
- official Anza verification of committed signed fixtures;
- a bounded canonical-decoder fuzz target asserting decode/encode identity.
