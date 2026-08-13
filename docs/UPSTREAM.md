# Upstream compatibility baseline

Checked: 2026-08-13

Repository: `anza-xyz/solana-sdk`

Baseline commit: `5190ff456079d17b64669bcb5eeac48dd595b91e`

The exact source hashes are in `compat/upstream-source.sha256`. They cover only
files that define ShortU16, legacy/v0/v1 message bytes, transaction envelope
bytes, configuration masks, and sanitizer behavior.

## Important finding

At this baseline, SDK master contains `VersionedMessage::V1` and the SIMD-0385
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
