# Frontend Safety Status

## Current Safety Coverage

The frontend currently enforces these core rules:
- ownership move checking for owned values
- definite initialization for locals, reassignments, moves, and current control-flow joins
- lexical `ref` / `ref mut` borrow checking for local bindings
- validation for safe borrow returns
- raw-address fencing in safe code paths
- typed dependency function contracts through `claw.toml`, including `raw` and `safe` external call boundaries
- canonical type layout, aggregate ABI pass classification, and linkage metadata across OIR and LIR
- foreign ABI hardening so non-`claw` safe external contracts only accept FFI-stable boundary types
- explicit drop scheduling for reassignment, block exit, `return`, and loop-control exits

Regression coverage now exists for:
- move errors
- lexical borrow errors
- uninitialized reads
- moved-out reads before reinitialization
- escaping borrow returns
- invalid raw-address use outside `raw`
- opaque external calls leaking into safe value flow
- typed external contracts that violate their declared raw boundary
- revised entry-point validation
- revised `Result[T, E]` and `try` diagnostics

## Still Missing Before Rust-Level Claims

The frontend is not ready to claim full Rust-level memory safety yet.

Major remaining gaps:
- richer ownership precision in more complex aliasing cases
- typed raw / FFI contracts for memory operations, effects, and non-function boundaries beyond the current function-signature layer
- concurrency ability checks such as `sendable` / `shareable`
- safety validation for later parallelism and runtime interaction beyond the current single-threaded assumptions
- full revised type-surface cleanup so safety docs and implementation names match everywhere

## Verification

Use MSYS2 UCRT64.

```bash
cmake --build build-ucrt64 -j 4
bash test/run_frontend_tests.sh
bash test_backend/run_backend_tests.sh
bash test_native/run_native_tests.sh
```

## Current Notes

- OIR and LIR preserve explicit raw-region tagging across nested control-flow blocks, so unsafe boundaries remain visible before backend lowering.
- dependency-backed external calls without shared signatures still lower as opaque external results, require explicit `raw`, and are only allowed as standalone statements.
- dependency-backed external calls with shared `claw.toml` contracts carry typed signatures plus ABI, dependency-root, and symbol metadata through OIR and LIR.
- safe external contracts that target non-`claw` ABIs are forced onto FFI-stable boundary types, so internal string/view types and handles cannot masquerade as foreign safe values.
- the frontend is strong enough to support the current first LLVM backend path, but the revised surface migration still needs more cleanup before we call that story settled.
