# Application migration slices

M5 deliberately migrates three narrow boundaries, not three applications. The
old sources remain identified by SHA-256 in
`tests/vectors/migrations/provenance.sha256`, while byte and process behavior is
kept in committed fixtures. `scripts/check-migration-fixtures.sh` rejects drift
in the local fixtures and also checks the sibling sources when they are present.

## Signal pack-NFT transaction construction

`solc_legacy_transaction_build` replaces Signal's `tools/packnft/msg.c` account
collector. Callers provide a fee payer, blockhash, signature storage, and
instructions expressed as pubkeys plus signer/writable requirements. The C
builder:

- merges duplicate privileges;
- inserts the fee payer as the first writable signer;
- keeps discovery order within the four canonical privilege groups;
- fails on every invalid pointer, capacity, flag, or signature-count mismatch;
- emits the ordinary `solc_transaction` model, so the same sanitizer and
  canonical encoder remain authoritative.

The fixture `signal-packnft-legacy.hex` is the exact old `test_msg.c` model with
a zero signature prepended. The migration test produces all 206 bytes through
the new builder and compares them byte-for-byte. Actual Ed25519 signing stays at
the explicit provider/orchestrator boundary; private keys do not enter this C
format layer.

## RATi bridge input and CPI

`solc_rati_bridge_instruction_decode` preserves the three deployed attempt
discriminants and byte layouts: initialize, 81-byte attestation, and variable
chain proof. Unlike the old unpacker it requires exact lengths, validates that
the event header's payload length consumes the event exactly, uses byte-wise
little-endian loads, and returns borrowed views instead of casting wire bytes to
C structs. Positive attestation/proof vectors and a payload-length negative
vector are committed.

`solc_rati_mint_checked_cpi` and `solc_rati_burn_checked_cpi` build the two
classic SPL Token checked instructions through the M3 token codec and M2 CPI
builder. They require the exact classic Token program ID, executable program
account, Token-owned writable accounts, and a real signer or explicitly marked
PDA signer. Thus app logic cannot silently substitute a program, invent
writability, or hand-encode tags 14/15.

This slice does not preserve the old bridge's unsafe search for Ed25519 results
inside arbitrary account data or its cast-based state layout. Those were not
valid protocol boundaries; a full bridge should inspect the Instructions sysvar
with a dedicated strict codec before accepting attestations.

## Trebuchet process adapter

`solc-bridge` is a persistent, line-oriented Rust process over the C ABI. Each
request and response is one UTF-8 JSON line using the exact schemas
`solc-process-request/v1` and `solc-process-response/v1`. Requests are capped at
16,384 bytes, reject unknown fields and schema drift, and support only four
network-free operations: inspect, verify, round-trip, and construct an RPC
`sendTransaction` request. The process does not open sockets or load keys.

`migrations/trebuchet/solc-wire-adapter.mjs` is the narrow Node side: it launches
an explicit binary path without a shell, correlates request IDs, bounds response
size, treats stderr as diagnostics, and rejects codec failures with stable code
and offset fields. The golden JSONL exchange and the live Node subprocess test
protect both sides of the boundary.
