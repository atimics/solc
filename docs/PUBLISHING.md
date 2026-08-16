# Package publishing strategy

Status: proposed

Last reviewed: 2026-08-16

This strategy distributes the Solana transaction builder without creating a
second implementation of its protocol rules. The deterministic C kernel is the
source of truth; Rust and future JavaScript packages are progressively safer or
more convenient bindings over that same source.

## Release thesis

Ship the smallest useful package at each boundary:

1. a source-first C/CMake package for the authoritative kernel;
2. raw and safe Rust crates after the current orchestration crate is split;
3. a CLI package after compile, attach-signature, and finalize exist;
4. a scoped JavaScript/WASM package only after the builder API is stable.

GitHub Releases are the canonical release record. Language registries are
distribution mirrors for the exact tagged source, not independent release
trains.

## Current readiness

The repository is not ready for a public package push yet:

- CMake installs the library and headers but does not install a package config
  or version file for `find_package()` consumers;
- there is no pkg-config file or installed-consumer smoke test;
- `solc-orchestrator` mixes the safe Rust API, CLI, process adapter, RPC JSON,
  FFI declarations, and native build;
- the Rust package lacks the description, homepage, readme, keywords,
  categories, and explicit package contents expected for crates.io;
- the builder is not exposed through the safe Rust API or CLI;
- no JavaScript/WASM package exists;
- package names have not been reserved in their registries.

The Pages site and source repository can be published before registry packages.
They describe implemented behavior separately from the next product surface.

## Package map

| Boundary | Proposed artifact | Channel | Timing |
| --- | --- | --- | --- |
| C source and headers | `solc-wire` source bundle | GitHub Release | First |
| CMake consumer config | `solc_wireConfig.cmake` and version file | Inside source/install bundle | First |
| pkg-config consumer metadata | `solc-wire.pc` | Inside source/install bundle | First |
| Raw Rust FFI and C build | `solc-wire-sys` | crates.io | After source-layout split |
| Safe Rust builder API | `solc-wire` | crates.io | After builder API and examples |
| CLI binary crate | `solc-wire-cli` with `solc-wire` binary | crates.io and GitHub Release | After compile/finalize CLI |
| JavaScript builder | organization-scoped name, WASM-backed | npm | After MVP and browser tests |
| OS/community packages | Homebrew tap, vcpkg, or Conan recipes | Respective channels | Demand-led |

All names are proposals until ownership is verified and reserved. In
particular, the project should decide whether `solc-wire` remains an engineering
name before promoting it externally: `solc` is already strongly associated with
the Solidity compiler.

## Versioning

- Use one SemVer version across the C kernel, Rust crates, CLI, and generated
  WASM package while the repository is small.
- Stay on `0.y.z` until the transaction-plan schema, builder ownership model,
  and public C/Rust APIs have survived a real integration.
- During `0.y`, increment the minor version for an incompatible public API,
  ABI, plan-schema, or wire-claim change; increment the patch for compatible
  fixes.
- Tag releases as `vX.Y.Z`; package manifests contain `X.Y.Z` without the `v`.
- Never reuse or move a published release tag.
- Maintain a manually curated `CHANGELOG.md` before the first registry release.

Lockstep versions are operationally simpler than independently versioning thin
bindings. Independent versions should be introduced only if release cadence
actually diverges.

## C and CMake: first distribution

The C package is the canonical first release because it already contains the
builder implementation and has no production dependencies.

Before `v0.1.0`:

1. Install an exported target set with the existing `solc::wire` target name.
2. Generate and install `solc_wireConfig.cmake` and
   `solc_wireConfigVersion.cmake` under a conventional CMake package directory.
3. Generate and install `solc-wire.pc` for non-CMake consumers.
4. Add a clean installed-consumer test that runs:

   ```cmake
   find_package(solc_wire CONFIG REQUIRED)
   target_link_libraries(example PRIVATE solc::wire)
   ```

5. Add a `FetchContent` example pinned to a release tag and commit SHA.
6. Verify headers are self-contained for both C and C++ consumers.
7. Decide and document symbol visibility before offering a shared library.

The first GitHub Release should attach an explicitly built source archive rather
than relying only on GitHub's automatic repository archives. The release bundle
contains:

- `include/`, deterministic C sources, CMake files, and license;
- the public ABI symbol list and compatibility metadata;
- a SHA-256 checksum manifest;
- an SPDX or CycloneDX SBOM;
- build and installed-consumer instructions.

Start source-first. Portable CLI binaries can be attached later for supported
Linux and macOS targets, but prebuilt static libraries multiply compiler, libc,
architecture, and hardening combinations without improving the initial builder
experience.

## Rust: split before publishing

Do not publish the current `solc-orchestrator` package under its internal name.
Split responsibilities first:

### `solc-wire-sys`

- owns raw FFI declarations and native compilation;
- declares `links = "solc_wire"`;
- packages the authoritative C sources and public headers;
- exposes no handwritten transaction semantics;
- documents supported compiler and target requirements.

The repository must contain only one editable copy of the C kernel. Before the
crate is created, reorganize the source tree so the canonical C files are inside
the `-sys` package root and the top-level CMake build consumes those same files.
Do not maintain a manually synchronized vendored copy.

### `solc-wire`

- provides safe owned Rust inputs and outputs;
- owns builder ergonomics, capacity allocation, and lifetime containment;
- delegates all protocol acceptance and encoding rules through `-sys`;
- contains no CLI, process protocol, or RPC client;
- includes runnable construction, signing-provider, and round-trip examples.

### `solc-wire-cli`

- provides the `solc-wire` binary;
- implements compile, message, attach-signature, finalize, inspect, and verify;
- may own JSON transaction-plan and signing-bundle presentation schemas;
- keeps network acquisition behind optional, explicit features or a later
  package.

Keep the Trebuchet process adapter and test oracles unpublished unless an actual
consumer requires them.

Every crate release runs, from the packaged tarball rather than the worktree:

```sh
cargo package --list
cargo publish --dry-run --locked
```

The release gate also builds a fresh consumer crate against the generated
`.crate` files. Registry publication is permanent, so the tag, changelog, and
GitHub Release must identify the same version and commit.

## JavaScript: WASM after the builder MVP

Do not publish the current Node subprocess adapter as the JavaScript transaction
builder. It is a migration boundary, not the intended developer experience.

The first npm package should:

- use an organization scope chosen before release;
- compile or embed the same deterministic kernel as WASM;
- provide ESM and TypeScript declarations;
- accept and return ordinary owned byte arrays;
- work in current Node and evergreen browsers without shelling out;
- keep Ed25519 and network access behind explicit providers;
- expose the same plan, message, and finalize semantics as Rust;
- include the WASM binary, license, type declarations, and source mapping in
  `npm pack --dry-run` output.

Test the packed tarball in clean Node and browser fixtures. Publish through npm
trusted publishing from GitHub Actions so short-lived OIDC credentials and npm
provenance are used instead of a long-lived repository token.

## Release process

Publishing must be a deliberate release action, never a side effect of pushing
to `main`.

### 1. Release pull request

- update versions in every shipping manifest;
- update `CHANGELOG.md` and compatibility metadata;
- run `make check-full` and all package dry-runs;
- build the Pages site and all installed/packed consumer fixtures;
- verify the tree is clean after generation;
- review the exact package file lists.

### 2. Release candidate

- create signed tag `vX.Y.Z-rc.N` when external testing is needed;
- build source and binary artifacts only from the tagged commit;
- record checksums, toolchain versions, and SBOMs;
- run the packaged artifacts in clean environments.

### 3. Immutable GitHub Release

- create the final `vX.Y.Z` release as a draft;
- attach the explicit source archive, checksums, SBOM, and any supported CLI
  binaries;
- publish only after every asset is present;
- enable GitHub immutable releases so the tag and assets cannot be replaced and
  receive release attestations.

### 4. Registry publication

- publish from the immutable tag through a protected `release` environment;
- require a maintainer approval at the environment boundary;
- use registry trusted publishing/OIDC wherever supported;
- publish dependency order: `-sys`, safe library, CLI, then npm/WASM;
- verify registry metadata and install each published package in a clean smoke
  project;
- never retry a partial publish by reusing a version that a registry accepted.

Registry workflows should be added only when the corresponding packages exist.
Until then, CI should implement package dry-runs without credentials.

## Supply-chain requirements

- The release commit passes the deterministic-core policy, sanitizer, fuzz,
  oracle, migration, and canonical-vector gates appropriate to the change.
- Workflows pin third-party actions to reviewed major versions initially and to
  full commit SHAs before registry publication.
- Package workflows receive only `contents: read` and `id-token: write` unless a
  specific release step requires more.
- Long-lived registry tokens are avoided; any unavoidable bootstrap token is
  stored in a protected environment and removed after trusted publishing works.
- GitHub Release assets include checksums and an SBOM.
- Generated C, Rust, or WASM packages embed the source tag and commit identity
  in package metadata, not in deterministic transaction results.
- A release is reproducible from its tag using documented toolchain versions.

## Channel policy

- GitHub Pages documents `main` and clearly labels implemented versus proposed
  behavior.
- GitHub prereleases distribute release candidates.
- crates.io and npm receive stable releases only; do not use disposable version
  numbers as a substitute for testing packed artifacts.
- Homebrew, vcpkg, Conan, Linux distribution packages, and other community
  channels follow demand and consume immutable release artifacts.
- The repository does not operate a custom package registry for public
  artifacts.

## Go/no-go checklist for the first package

- [ ] Product-facing name and package ownership are decided.
- [ ] Legacy compile/sign/finalize workflow exists through one public surface.
- [ ] CMake config, pkg-config, and clean consumer tests pass.
- [ ] Public API and ABI compatibility policy is documented.
- [ ] Changelog and support matrix exist.
- [ ] Explicit source archive, checksums, and SBOM are reproducible.
- [ ] Release environment and maintainer approval are configured.
- [ ] Immutable releases are enabled.
- [ ] All packaged-file lists and dry-runs are reviewed.
- [ ] Installation examples use the actual packaged artifacts.

## Primary references

- [GitHub Pages custom workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [GitHub immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
- [Cargo publishing](https://doc.rust-lang.org/cargo/reference/publishing.html)
- [npm trusted publishing](https://docs.npmjs.com/trusted-publishers/)
- [CMake package discovery](https://cmake.org/cmake/help/latest/command/find_package.html)
