# Revised Native Tests

This suite covers revised-language native executable generation for the currently supported subset.

Recommended runner:
- `bash test_native/run_native_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash test_native/run_native_tests.sh`

Artifacts:
- Built executables and generated LLVM IR are left in `test_native/artifacts/` so both `.exe` and `.ll` outputs can be inspected after the runner finishes.

Current fixtures:
- `revise_single_file.cat`
  A direct single-file build that proves revised surface syntax can build and run as a native executable.
- `revise_workspace/`
  A workspace build with `claw.toml` and root `main.cat`, proving revised project loading also reaches native execution.
- `revise_maybe.cat`
  Confirms the current revised `Maybe[T]` subset still builds and runs natively.
- `revise_exit_code.cat`
  Verifies `fn main() -> Int32` returns the expected OS exit code.

This suite intentionally avoids revised imported workspaces for now, because native lowering for that path is not complete yet.
