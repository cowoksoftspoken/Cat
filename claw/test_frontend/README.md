# Revised Frontend Tests

This folder now contains only revised-language frontend fixtures.

Recommended runner:
- `bash test/run_frontend_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash test/run_frontend_tests.sh`

Current fixtures:
- `revise_surface.cat`
  Locks in the revised surface basics: `val` / `var`, `ref`, `if` / `else`, file modules without `realm`, and generic `[]`.
- `revise_pkg/`
  A revised workspace fixture covering root `main.cat`, `src/modules.cat`, and revised import alias syntax.
- `revise_bad_main_return/`
  Expected to fail validation because root `main` must take no parameters and return `Unit` implicitly or explicitly.
- `revise_error_handling.cat`
  Locks in `Result[T, E]`, `Ok(...)`, `Fail(...)`, `try`, and `try ... else ...`.
- `revise_bad_try_return.cat`
  Expected to fail because shorthand `try` requires the current function to return `Result[T, E]`.
- `revise_bad_try_var.cat`
  Expected to fail because `try` bindings are currently restricted to `val`.

Coverage focus:
- revised parser surface
- revised module/workspace rules
- revised AIR printing
- revised `Result` / `try` semantic validation
- Anchor stable-storage semantics and scoped/borrow diagnostics
- revised diagnostics for entry and `try` misuse

Backend and native integration suites live separately:
- `bash test_backend/run_backend_tests.sh`
- `bash test_native/run_native_tests.sh`

