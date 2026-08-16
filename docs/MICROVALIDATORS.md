# Microvalidator architecture

The product requirements are defined in [../PRD.md](../PRD.md), and the
engineering ownership, receipt, and delivery contracts are defined in
[../ENG.md](../ENG.md).

## Thesis

A validator is not the smallest useful unit of trust. A **microvalidator** is a
deterministic, versioned component that establishes one narrowly stated claim
about an artifact from finite, explicit inputs. It either rejects with a stable
error or accepts with reproducible evidence. Larger validity decisions are a
composition of those claims, not an expansion of what any one component is
allowed to assert.

For this repository, the intended claim chain is:

```text
canonical bytes
    -> structurally valid transaction
    -> cryptographically authorized transaction
    -> transaction resolved against an identified state snapshot
    -> valid program invocation under that snapshot
    -> deterministic state transition under a named runtime configuration
    -> inclusion in a network-agreed history
```

The current implementation covers the first claim, the signature portion of
the second, checked pieces of program invocation, and differential evidence for
a small reference-program execution. It does not collapse those results into a
claim about current network acceptance.

## Placement rule

The architectural boundary is not C versus Rust. It is pure validation versus
context acquisition versus distributed agreement:

- If a decision can be reproduced from finite, explicit inputs, it belongs in
  a deterministic validation kernel.
- If an operation discovers which inputs are current or obtains them from a
  host, it belongs in orchestration.
- If an operation decides which history a network adopts, it belongs in a
  runtime or consensus system.

The existing language split follows that rule: reviewed protocol semantics are
implemented in C, while Rust owns safe process, cryptography-provider, JSON-RPC,
and test-orchestration boundaries. A future kernel does not have to be written
in C, but each rule needs one authoritative implementation. Rust orchestration
must not grow a second handwritten version of a rule already owned by C.

## Planes and responsibilities

| Concern | Deterministic kernel | Rust control plane | Runtime or agreement plane |
| --- | --- | --- | --- |
| Address lookup tables | Decode a supplied table snapshot, check lifecycle context, and resolve indices | Acquire and identify the table and slot context | Establish which state snapshot is canonical |
| Account state | Check owner, layout, privileges, and instruction preconditions against supplied account bytes | Acquire, pin, cache, and label the snapshot | Supply the Bank state or a separately verifiable state commitment |
| Blockhash lifetime | Check a transaction against a supplied, versioned lifetime context | Obtain and freeze that context | Decide whether the hash is admissible in the active Bank |
| SVM execution | Execute explicit accounts and instructions under an explicit feature/syscall configuration | Select an engine, supply inputs, and package evidence | Provide the production runtime, when local execution is not authoritative |
| Fork choice | At most, evaluate a pure rule over a complete supplied fork view | Ingest votes, persist observations, and coordinate peers | Run the dedicated fork-choice subsystem |
| Consensus | Verify individual votes, certificates, or evidence objects | Communicate with a consensus client | Decide and maintain the network-agreed history |

This creates a one-way authority flow:

```text
external authority or Rust adapter obtains and identifies context
                              |
                              v
              deterministic kernel evaluates one claim
                              |
                              v
                 Rust packages and routes evidence
```

Orchestration may report the provenance of a snapshot. Provenance alone is not
proof that the snapshot is canonical. That stronger assertion must come from a
trusted Bank, a consensus client, or a verified state proof.

## Claim contract

Conceptually, every microvalidator has the form:

```text
V(rule_version, artifact, explicit_context)
    -> reject(stable_code, location)
     | accept(scoped_claim, witness)
```

This is a model, not a commitment to one public result struct. Existing APIs
already express parts of it through stable status codes, byte offsets,
canonical re-encoding, message bytes, digests, and runtime evidence.

Every new microvalidator must document:

1. the exact proposition it establishes;
2. all artifact and context bytes on which the proposition depends;
3. the rule, feature-set, and format versions used;
4. the deterministic accept and reject conditions;
5. the witness or transcript that allows the decision to be replayed;
6. facts explicitly not established by acceptance.

An accepted claim may be consumed by a later validator only when the later
validator names it as a prerequisite and binds the same artifact, context, and
rule versions. A claim about snapshot `S` cannot be silently reused for
snapshot `S + 1`, and structural acceptance cannot stand in for signature or
execution acceptance.

### Example: lookup resolution

The kernel must not say, "this lookup table is current." It may say:

> Given transaction `T`, lookup-table account bytes `A`, and lifecycle context
> `C` under rule version `R`, the lookup indices resolve to address sequence
> `W` and all checked structural and lifecycle predicates hold.

Rust records how `A` and `C` were obtained. The runtime or agreement plane
establishes whether that context belongs to the accepted history.

### Example: current account state

"Current" is not a property of account bytes alone. A state validator can
establish instruction preconditions relative to an identified snapshot. It
cannot establish that the snapshot is current without consuming an authority
claim from outside the deterministic kernel.

## Package boundaries

The architecture can grow as four local packages plus one external plane:

1. **Wire kernel (implemented):** canonical transaction and program byte
   formats, signature-message extraction, and strict sanitization.
2. **State kernel (future):** lookup resolution, account-state predicates,
   instruction preconditions, and blockhash-lifetime checking over supplied
   context. This should be a separate module rather than an expansion of
   `wire.c` into a network-aware component.
3. **Execution kernel (optional future):** deterministic SVM semantics with
   explicit accounts, feature activation, syscalls, metering, and output-state
   evidence. It should remain separate from both the wire and state kernels.
4. **Rust control plane:** RPC and process I/O, snapshot acquisition,
   provenance, caching, cryptographic providers, engine selection, and evidence
   assembly. It coordinates semantic kernels but does not duplicate them.
5. **Agreement plane (external):** Bank authority, fork choice, voting,
   consensus, finality, and peer-to-peer operation.

Mollusk and Agave currently act as isolated execution oracles. That is evidence
about compatibility, not a local implementation of the execution or agreement
planes.

## Supported and unsupported claims

The repository currently provides evidence for these microvalidator
properties:

- canonical decode/encode identity for accepted wire inputs;
- deterministic results across supported compilers, optimization levels, and
  host noise;
- execution without ambient host syscalls in the tested C boundary;
- explicit cryptographic-provider and RPC boundaries;
- agreement with exact-pinned format oracles on the committed corpus;
- runtime differential evidence for the same reference ELF;
- narrow migration fixtures demonstrating replacement of ambiguous application
  boundaries with checked ones.

It does not yet prove protocol completeness, semantic equivalence for the full
SVM, snapshot canonicality, blockhash freshness on a live Bank, correct fork
choice, Byzantine fault tolerance, or network consensus. Those claims require
new kernels, new evidence, or an external authority as described above.

## Research hypotheses

This architecture supports falsifiable rather than purely rhetorical claims:

- independently built kernels produce identical decisions for identical
  versioned inputs;
- canonical witnesses prevent multiple accepted encodings of the same modeled
  object;
- an upstream rule change invalidates only the microvalidators that depend on
  it;
- replacing legacy application boundaries preserves the accepted valid corpus
  while rejecting ambiguous or malformed inputs;
- composed claims agree with an exact-pinned reference system on a declared
  corpus and runtime configuration;
- failures can be localized to the first unsatisfied claim boundary instead of
  being reported as undifferentiated transaction rejection.

These hypotheses require corpus coverage, negative and mutation tests,
differential oracles, explicit versioning, and replayable evidence for every
new claim boundary.
