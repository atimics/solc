# Contributing

Run the complete local gate:

```sh
make check
```

Protocol changes must include:

- the primary upstream source or SIMD that motivated the change;
- an updated byte-level section in `docs/WIRE_FORMAT.md`;
- at least one canonical vector;
- negative cases for truncation, invalid values, and boundaries;
- C and Rust round-trip coverage;
- refreshed upstream hashes only after the behavior is reviewed.

Keep program-specific logic, RPC clients, and signing providers out of
`src/wire.c`. That file is the small protocol truth surface.
