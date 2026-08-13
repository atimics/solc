# Runtime differential evidence

The reference C program is tested in two execution paths on the same ELF:

1. `mollusk-svm = 0.3.0`, which is built against the Agave 2.3 runtime crates;
2. `solana-test-validator = 2.3.13`, with the ELF installed as a genesis BPF
   program and invoked by a signed transaction over RPC.

The isolated Rust workspace in `tests/runtime-oracle` owns all official runtime
dependencies. It does not enter the library dependency graph. Both paths report
JSON evidence containing the runtime version, program address, artifact size and
SHA-256, success status, compute units, and return-data size. The validator path
also reports the genesis hash and confirmed transaction signature.

`scripts/check-runtime-differential.sh` rebuilds the pinned SBF artifact, starts
an ephemeral local validator, runs both paths, and validates their evidence with
`scripts/compare-runtime-evidence.py`. The current reference consumes 66 compute
units in both runtimes. `compat/runtime-budgets.toml` permits at most 9,216 bytes
and 80 compute units. These are regression ceilings, not targets: changes that
cross them require source and generated-artifact review.

## Network evidence

`scripts/capture-mainnet-corpus.py OUTPUT` can capture current finalized wire
bytes. It has a code-level allowlist containing only `getGenesisHash`,
`getSignaturesForAddress`, and `getTransaction`; it refuses any endpoint whose
genesis hash is not mainnet-beta. Every captured transaction must pass strict
inspection, signature verification, and byte-for-byte round trip before the
manifest is written. The scheduled workflow publishes the corpus as an
ephemeral CI artifact, so network observations do not silently become fixtures.

Devnet is deliberately separate and disabled by default. The manual workflow
only proceeds when both keypair secrets exist. `scripts/devnet-evidence.sh`
requires `SOLC_DEVNET_ENABLE=1`, exact Agave 2.3.13, both explicit keypair paths,
and the full devnet genesis hash before deployment. It then deploys the pinned
artifact, simulates an invocation, sends it, confirms it, and writes combined
deployment/execution evidence. It never creates, discovers, or prints keypair
material.
