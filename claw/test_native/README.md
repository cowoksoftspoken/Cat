# Revised Native Tests

This suite covers revised-language native executable generation for the currently supported subset.

Recommended runner:
- `bash test_native/run_native_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash test_native/run_native_tests.sh`

Current fixtures:
- `revise_single_file.cat`
  A direct single-file build that proves revised surface syntax can build and run as a native executable.
- `revise_workspace/`
  A workspace build with `claw.toml` and root `main.cat`, proving revised project loading also reaches native execution.

This suite intentionally avoids revised imported workspaces for now, because native lowering for that path is not complete yet.
