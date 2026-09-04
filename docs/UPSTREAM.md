# Upstream compatibility baseline

Checked: 2026-09-04

Repository: `anza-xyz/solana-sdk`

Baseline commit: `5ec913f7826c3c7ccbfc8adacc69605e90b3d6fa`

The exact source hashes are in `compat/upstream-source.sha256`. They cover only
files that define ShortU16, legacy/v0/v1 message bytes, transaction envelope
bytes, configuration masks, and sanitizer behavior.

## September 4 review

The [source comparison](https://github.com/anza-xyz/solana-sdk/compare/5190ff456079d17b64669bcb5eeac48dd595b91e...5ec913f7826c3c7ccbfc8adacc69605e90b3d6fa)
covers seven changed files among the nine watched files.

| Source | Reviewed change |
| --- | --- |
| `transaction/src/lib.rs` | Legacy signature counts must match the message header exactly. Verification sanitizes first and uses the shared signature verifier. |
| `transaction/src/versioned/mod.rs` | Verification sanitizes first. `verify_and_hash_message` replaces the removed `verify_with_results` API. |
| `message/src/legacy.rs`, `message/src/versions/mod.rs`, `message/src/versions/v0/mod.rs` | Deprecated writability helpers and their tests were removed. The replacement helpers remain. |
| `message/src/versions/v1/message.rs` | Deprecated writability and serialization helpers were removed. V1 construction uses `try_compile_with_config` with explicit transaction configuration. |
| `message/src/versions/v1/mod.rs` | `WireInstructionHeader` replaces the removed deprecated tuple alias. |

ShortU16 and v1 configuration-mask source hashes match the earlier baseline.
The reviewed changes preserve the serialized fields and codec implementations.

The isolated oracle pins the SDK Git revision above. It compares all six
canonical vectors with the C encoder and the official encoder. It also checks
valid signatures, missing signatures, extra signatures, missing account keys,
and invalid signatures across legacy, v0, and v1. For legacy messages it checks
both `Transaction` and `VersionedTransaction` verification methods. These cases
produce matching C and SDK accept/reject results.

The native C crypto tests also check that malformed signature counts and missing
keys are rejected before the crypto provider runs. The existing C model checks
already enforce this order. CI checks the recorded source hashes at the exact
SDK revision and runs the oracle with its lockfile.

## Original format finding

At the August 13 baseline, SDK master contained `VersionedMessage::V1` and the SIMD-0385
wire implementation. The public transaction pages inspected on the same date
still described legacy and v0 as the supported formats. This project therefore
treats primary SDK source and tests as the byte-level oracle and treats prose
documentation as explanatory, not normative.

This does not assert that v1 is activated on mainnet or supported by every RPC
provider. `solc-wire` implements the current serialized model so the change is
visible and testable.

## Review policy

Run the networked drift check explicitly:

```sh
scripts/check-upstream-drift.sh
```

Scheduled CI runs it against `master`. A failure means “review upstream,” not
“blindly update checksums.” The compatible behavior, vectors, docs, and hashes
must move together.
