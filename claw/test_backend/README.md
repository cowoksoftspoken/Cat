# Revised Backend Tests

This suite is intentionally small and only covers revised-language backend lowering that is already supported today.

Recommended runner:
- `bash test_backend/run_backend_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash test_backend/run_backend_tests.sh`

Current fixture:
- `revise_result_llvm.cat`
  Exercises revised `Result[T, E]`, `Ok`, `Fail`, `try`, `try ... else`, integer printing, and string printing through the LLVM backend.

The backend runner checks textual LLVM IR only. Native executable coverage lives in `test_native/`.
