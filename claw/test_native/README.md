# Native Integration Tests

`test_native` is isolated from `test` and `test_backend`.

It validates the full native pipeline:
- `claw build-native ...`
- LLVM IR compilation through `clang`
- runtime linkage
- `.exe` execution with output and exit-code checks
