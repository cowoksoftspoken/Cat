# Frontend Safety Status

## Current Safety Coverage

The frontend now enforces these core safety rules:
- ownership move checking for owned values
- definite initialization for locals, reassignments, moves, and current control-flow joins
- lexical `look` / `edit` borrow checking for local bindings
- safe view-return validation
- raw-address fencing in safe code paths
- typed dependency function contracts through `claw.toml`, including `raw` and `safe` external call boundaries
- canonical type layout, aggregate ABI pass classification, and linkage metadata across OIR and LIR
- foreign ABI hardening so non-`claw` safe external contracts only accept FFI-stable boundary types
- explicit drop scheduling for reassignment, block exit, `give`, and loop control exits

Regression coverage exists for:
- move errors
- lexical borrow errors
- uninitialized reads
- moved-out slot reads before reinitialization
- escaping view returns
- invalid raw-address use outside `raw`
- opaque external calls leaking into safe value flow
- typed external contracts that violate their declared raw boundary

## Still Missing Before Rust-Level Claims

The frontend is not yet ready to claim full Rust-level memory safety.

Major remaining gaps:
- richer ownership precision in more complex aliasing cases
- typed raw / FFI contracts for memory operations, effects, and non-function boundaries beyond the current function-signature layer
- concurrency ability checks such as `sendable` / `shareable`
- safety validation for later parallelism and runtime interaction beyond the current single-threaded frontend assumptions

## Verification

Use MSYS2 UCRT64.

```bash
cmake --build build-ucrt64 -j 4
bash test/run_frontend_tests.sh
bash test_backend/run_backend_tests.sh
```

- builtin container generic arity is now enforced for `Span of T`, `Vec of T`, `Table of K, V`, `Set of T`, `Heap of T`, and `Ring of T`, reducing frontend ambiguity before backend lowering.
- OIR and LIR now preserve explicit raw-region tagging across nested control-flow blocks, which keeps unsafe boundaries visible before backend lowering.
- dependency-backed external calls without shared signatures still lower as opaque external results, require an explicit `raw` block, and are only allowed as standalone statements, which keeps untyped foreign boundaries from leaking into safe value flow.
- dependency-backed external calls with shared `claw.toml` contracts now carry typed signatures plus ABI, dependency-root, and symbol metadata through OIR and LIR, so backend lowering can distinguish raw-only boundaries from proven-safe external calls.
- safe external contracts that target non-`claw` ABIs are now forced onto FFI-stable boundary types, so `Text`, `Bytes`, views, and internal handles cannot masquerade as foreign safe values.
- the frontend is now hard enough to support a first LLVM backend pass without inventing core ownership or layout rules mid-backend.
