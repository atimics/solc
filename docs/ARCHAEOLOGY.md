# Prior-work archaeology

This repository started by examining the existing local Signal, RATi, and
Trebuchet attempts. The goal was to preserve proven ideas without importing
their application coupling.

## Signal

Useful material:

- `tools/packnft/msg.c` and `txn.c` established a sensible primitives → message
  → transaction layering and proved that useful Solana transaction construction
  can remain small C.
- `programs/burn-to-mint/onchain-c` proved a C SBF artifact can be the active
  candidate while Rust remains a compact native reference.
- its Rust-generated golden-vector gate is the strongest reusable testing idea.
- `docs/c_safety_policy.md` and the broader C test suite demonstrate the value
  of compiler warnings, sanitizers, deterministic replay, and narrow ownership.

Why the old transaction codec was not copied:

- legacy messages only;
- encode-only, so it could not inspect or round-trip network bytes;
- fixed application caps rather than wire-derived limits;
- ShortU16 decode accepted aliases and overflow in the third byte;
- account/instruction builder failures could be ignored before writes;
- protocol validation and application building were mixed together.

## RATi

The `rati-bridge` history includes a pure-C Solana entrypoint and a disposable
devnet deployment lineage. That is valuable evidence that C is not merely a
paper target for this toolchain. The burn-to-mint project also keeps C SBF,
native Rust logic, account-order documents, size gates, and generated vectors in
separate review surfaces.

The bridge source also shows why a foundation layer is needed: program logic,
raw buffer casts, fixed rent assumptions, signature-precompile discovery, token
instruction construction, and state layout all sit in one file. This repository
will supply checked primitives so future programs do not re-invent those pieces
inside business logic.

## Trebuchet

Trebuchet contributes the operational testing model:

- network-free fakes for ordinary CI;
- explicit read-only mainnet smoke tests;
- secret-gated devnet transaction tests;
- genesis-hash checks before irreversible writes;
- recovery-aware multi-transaction workflows.

Its transaction construction still depends on JavaScript Solana SDKs. The M5
process adapter now makes the Rust orchestrator and C codec an executable
boundary, while Trebuchet-style network gates remain the operational shape.

## Migrated evidence

The inspected sibling source hashes, Signal byte fixture, RATi positive and
negative layouts, and Trebuchet JSONL exchange are pinned under
`tests/vectors/migrations`. See [MIGRATIONS.md](MIGRATIONS.md) for the exact
replacement boundaries and the behavior intentionally rejected rather than
preserved.

## Resulting design

The retained pattern is:

```text
C protocol truth
  + Rust orchestration/reference-vector generation
  + hermetic local tests
  + optional official-SDK differential tests
  + secret-gated devnet evidence
```

This project changes the layer boundary, not the language dogma. C owns stable,
auditable byte and runtime primitives. Rust remains useful for orchestration,
toolchain integration, and external differential checks. Application UI and
RPC policy stay outside both.
