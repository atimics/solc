# Contributing

Run the complete local gate:

```sh
make check
```

Changes to deterministic C execution boundaries should also run:

```sh
make check-full
```

The individual policy, determinism, sanitizer, coverage, analyzer, and fuzz
lanes are documented in `docs/CI.md`. Do not suppress a kernel policy finding
inside source; update `ci/kernel-policy.toml` with an explicit reviewable
exception only when the dependency is part of the deterministic contract.

Protocol changes must include:

- the primary upstream source or SIMD that motivated the change;
- an updated byte-level section in `docs/WIRE_FORMAT.md`;
- at least one canonical vector;
- negative cases for truncation, invalid values, and boundaries;
- C and Rust round-trip coverage;
- refreshed upstream hashes only after the behavior is reviewed.

Keep program-specific logic, RPC clients, and signing providers out of
`src/wire.c`. That file is the small protocol truth surface.
