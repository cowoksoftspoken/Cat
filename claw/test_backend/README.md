# LLVM Backend Tests

This folder is intentionally separate from `test/` so backend bring-up does not get mixed into the frontend corpus.

Current scope:
- direct function calls and integer arithmetic
- branch / compare lowering
- runtime `print` / `println` declarations and string constants
- typed safe external scalar calls
- checked lowering for core `Text` builtins such as `len`, `is_empty`, `byte_at`, and `slice`
- explicit LLVM lowering of the current `bounds_check` path into branch-to-defect flow
- concrete choice `pick` lowering with tag-based `switch`, payload extraction, and defect blocks
- initial `lift` lowering for concrete `Outcome of T, E` values
- first indirect aggregate ABI lowering for direct Claw calls, including hidden return slots, pointer-based aggregate parameters, and concrete shape field load/store lowering
- iterator lowering for the current slice-like iterable subset plus direct LLVM lowering for `break` / `continue` loop control
- explicit negative coverage for raw-only external calls that still have no supported LLVM contract

Primary runner:
- `bash test_backend/run_backend_tests.sh`

Current validation strategy:
- emit LLVM IR with `claw emit-llvm ...`
- assemble it with `llvm-as`
- lower it to an object with `llc`
- compare stable textual snapshots for the current subset

This suite is meant for backend bring-up. It should stay separate from `test/` until backend coverage is broad enough that mixing both suites would not blur frontend failures with backend failures.
