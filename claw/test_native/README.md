# Native Integration Tests

`test_native` is isolated from `test` and `test_backend`.

It validates the full native pipeline:
- `claw build ...`
- LLVM IR compilation through `clang`
- runtime linkage
- `.exe` execution with output and exit-code checks
- current end-to-end coverage for runtime printing, direct imports, loop control, and Text-byte scanning
