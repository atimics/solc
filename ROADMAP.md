# Roadmap

## M0 — transaction wire foundation (implemented)

- strict ShortU16;
- legacy, v0, and v1 decode/sanitize/encode;
- stable zero-allocation C ABI;
- safe Rust inspection and round-trip CLI;
- golden/negative/mutation/fuzz tests;
- Linux/macOS, sanitizer, and upstream-drift CI.

## M1 — transport and cryptographic primitives (implemented)

- base58 and base64 with canonical decoders;
- SHA-256 and Ed25519 verification behind explicit provider interfaces;
- message-signing byte extraction and signature verification;
- RPC JSON adapters kept outside the C protocol core.

## M2 — native program ABI (implemented)

- documented SBF entrypoint input decoder without struct casts;
- account ownership/signer/writable helpers;
- PDA seed validation;
- checked CPI instruction builder;
- pinned C SBF toolchain manifest and reproducible artifact metadata.

## M3 — core program codecs (implemented)

- System Program;
- SPL Token and Token-2022 base instructions/accounts;
- Compute Budget compatibility for legacy/v0 and inline v1 config;
- Address Lookup Table account-state decoding;
- program-specific codecs generated from small declarative schemas where that
  does not weaken reviewability.

## M4 — runtime differential harness (implemented)

- exact-pinned Anza Rust oracle in an isolated test workspace;
- Mollusk/Agave execution comparison for C programs;
- read-only mainnet corpus capture;
- secret-gated devnet deploy/simulate/send evidence with genesis checks;
- compute-unit and artifact-size regression budgets.

## M5 — migrate real programs (implemented)

- port the Signal pack-NFT transaction path to this codec;
- rebuild the RATi bridge input/CPI path on checked primitives;
- connect Trebuchet through a narrow Rust process/FFI adapter;
- retain old implementations as differential fixtures until parity is proven.

Every milestone adds executable format prose, positive vectors, negative
vectors, and an upstream compatibility gate before application features.
