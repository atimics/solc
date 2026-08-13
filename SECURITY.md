# Security policy

`solc-wire` parses attacker-controlled transaction bytes. Treat any accepted
input as structurally valid only; signature verification, lookup resolution,
program existence, blockhash freshness, and runtime execution are separate.

## Invariants for changes

- No unchecked pointer arithmetic or integer-size arithmetic.
- No decoder allocation and no hidden global state.
- No direct casts from untrusted bytes to C structs.
- Every new field has truncation, malformed, boundary, and round-trip tests.
- Every accepted input re-encodes byte-for-byte.
- New upstream formats are unsupported until their sanitizer rules and negative
  cases are implemented.
- Warnings remain errors in C; Rust formatting and Clippy remain merge gates.

Do not submit private keys, seed phrases, signed unpublished transactions, or
production RPC credentials in an issue or test vector.
