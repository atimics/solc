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

## M6 — transaction compiler product surface (proposed)

- versioned legacy transaction-plan schema;
- safe Rust wrapper over the checked C builder;
- compile, message, attach-signature, and finalize CLI workflow;
- ordered signer manifest and explicit unsigned/partial/final states;
- System, Compute Budget, SPL Token, and raw-instruction construction recipes;
- consumer-ready CMake, Rust, and source-release packaging.

M6 is deliberately offline and legacy-first. V0 construction with supplied
lookup-table context follows only after the compile/sign/finalize path is proven
at a real signing or forwarding boundary. See [PRD.md](PRD.md), [ENG.md](ENG.md),
and [docs/PUBLISHING.md](docs/PUBLISHING.md).

## Architectural direction after the builder MVP

Future work is separated by claim boundary rather than folded into the wire
codec:

- a state kernel may resolve supplied lookup-table snapshots, check account
  predicates, and evaluate blockhash-lifetime context;
- an optional execution kernel may implement deterministic SVM semantics over
  explicit state, feature, syscall, and metering inputs;
- Rust remains the control plane for acquiring and identifying context,
  selecting providers or engines, and packaging evidence;
- Bank authority, fork choice, voting, consensus, and finality remain in an
  external agreement plane.

These are architectural boundaries, not yet committed implementation
milestones. Each future claim must be independently specified, versioned,
tested, and differentially checked before it becomes part of the supported
surface. See [docs/MICROVALIDATORS.md](docs/MICROVALIDATORS.md).
