# CI and deterministic-core test contract

The test system separates fast merge gates from expensive scheduled evidence.
The production C core is held to a stricter policy than test programs, command
line tools, and Rust orchestration.

## Local profiles

The quickest complete developer gate is:

```sh
make check
```

It runs the deterministic-core policy, native C tests, Rust tests and lints,
migration provenance, and canonical vectors. The deeper local gate adds
cross-compiler determinism, AddressSanitizer/UBSan, ThreadSanitizer, coverage,
and bounded fuzzing:

```sh
make check-full
```

Individual lanes are available as `make policy`, `make determinism`,
`make sanitize`, `make thread-sanitize`, `make coverage`, `make analyze`, and
`make fuzz-smoke`. CMake presets provide matching `dev`, `release`, `sanitize`,
`thread`, and `fuzz` configurations.

## Deterministic-core policy

`ci/kernel-policy.toml` is the reviewed machine-readable boundary for `src/`
and `include/solc/`. `scripts/check-kernel-policy.py` enforces it in four ways:

1. lexical source checks reject floating-point types and literals, mutable
   static storage, host I/O/time/randomness, dynamic allocation, threading,
   locale and environment access, and unreviewed headers;
2. Clang AST checks independently reject floating types and mutable
   static-duration objects;
3. the static-library symbol table may contain only the reviewed deterministic
   libc surface and no writable globals;
4. `compat/public-api.symbols` makes exported C ABI changes explicit.

The checker includes negative self-tests. Adding an exception requires a
reviewed policy change; inline suppressions are not accepted.

## Determinism and host isolation

`solc_determinism_tests` produces a canonical transcript for legacy, v0, and v1
vectors while changing input/output alignment and scratch fill. The matrix
script rebuilds it with GCC and Clang at `-O0` and `-O3`, changes locale,
timezone, current directory, and environment noise, and requires byte-identical
output.

On Linux, `solc_syscall_boundary_tests` loads its fixture before entering a
seccomp sandbox. The C core then executes with every host syscall denied except
process termination. `solc_thread_safety_tests` concurrently exercises shared
read-only transaction bytes through independent decode, encode, hash, and text
codec contexts; the scheduled ThreadSanitizer lane checks the same path for
hidden shared state.

## GitHub Actions topology

Every push and pull request runs independent jobs for:

- deterministic policy, link boundary, ABI, and repository hygiene;
- strict GCC, Clang, and macOS native builds;
- Rust formatting, tests, Clippy, FFI, process adapter, and vectors;
- AddressSanitizer and UndefinedBehaviorSanitizer;
- GCC/Clang optimization-independent transcripts;
- exact-pinned official transaction and program oracles;
- short fuzz campaigns for wire, encoding, SBF, and program decoders.

Rust work no longer runs redundantly in every native compiler job. Workflow
concurrency cancels superseded runs and exact-lockfile cache keys avoid stale
oracle or orchestrator artifacts.

The scheduled workflow adds:

- a 77% deterministic-core line-coverage floor;
- GCC's interprocedural analyzer;
- ThreadSanitizer;
- five-minute campaigns for each fuzz target.

SBF artifact, runtime differential, upstream-drift, mainnet-corpus, and manual
devnet evidence retain separate workflows because their toolchains, network
permissions, and trust boundaries differ from the fast merge gate.

## Adding kernel code

New deterministic code must:

- accept all environmental state through explicit inputs or callbacks;
- use caller-owned scratch/output storage;
- contain no floating-point computation or mutable global state;
- add no external symbol without a policy review;
- add its public entry points to `compat/public-api.symbols` deliberately;
- extend deterministic transcripts or exact-pinned oracle evidence;
- preserve the coverage floor and fuzz the new byte boundary.

Telemetry such as wall time, cache-hit counts, host memory use, or thread IDs
must stay outside canonical execution results.
