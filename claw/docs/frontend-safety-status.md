# Frontend Safety Status

## Current Safety Coverage

The frontend now enforces these core safety rules:
- ownership move checking for owned values
- definite initialization for locals, reassignments, moves, and current control-flow joins
- lexical `look` / `edit` borrow checking for local bindings
- safe view-return validation
- raw-address fencing in safe code paths
- explicit drop scheduling for reassignment, block exit, `give`, and loop control exits

Regression coverage exists for:
- move errors
- lexical borrow errors
- uninitialized reads
- moved-out slot reads before reinitialization
- escaping view returns
- invalid raw-address use outside `raw`

## Still Missing Before Rust-Level Claims

The frontend is not yet ready to claim full Rust-level memory safety.

Major remaining gaps:
- richer ownership precision in more complex aliasing cases
- fuller raw / FFI contracts
- concurrency ability checks such as `sendable` / `shareable`
- ownership-aware IR data model for backend lowering

## Verification

Use MSYS2 UCRT64.

```bash
cmake --build build-ucrt64 -j 4
bash test/run_frontend_tests.sh
```
