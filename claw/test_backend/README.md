# Revised Backend Tests

This suite is intentionally small and only covers revised-language backend lowering that is already supported today.

Recommended runner:
- `bash test_backend/run_backend_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash test_backend/run_backend_tests.sh`

Artifacts:
- Generated LLVM IR is left in `test_backend/artifacts/` so it can be inspected after the runner finishes.

Current fixtures:
- `revise_result_llvm.cat`
  Exercises revised `Result[T, E]`, `Ok`, `Fail`, `try`, `try ... else`, integer printing, and string printing through the LLVM backend.
- `revise_maybe.cat`
  Verifies revised `Maybe[T]` lowering can still produce valid LLVM IR.

The backend runner checks textual LLVM IR and validates it with `llvm-as`. Native executable coverage lives in `test_native/`.
