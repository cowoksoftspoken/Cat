# Frontend Test Corpus

Current corpus files are plain source and workspace fixtures intended for the compiler driver.

Recommended checks:
- `claw check test\syntax.cat`
- `claw check test\types.cat`
- `claw check test\control_flow.cat`
- `claw check test\scope_isolation.cat`
- `claw check test\prelude_prints.cat`
- `claw check test\generic_instantiation.cat`
- `claw check test\view_safety.cat`
  This file is expected to pass and locks in lexical view scope release plus safe passthrough view returns.
- `claw check test\ownership.cat`
  This file is expected to fail ownership checking because it moves the same `Text` twice.
- `claw check test\view_borrow_ownership.cat`
  This file is expected to fail ownership checking because a lexical `look` borrow blocks both moving and `edit` reborrowing of the owner.
- `claw check test\parse_diagnostics.cat`
  This file is expected to fail in the parser and should show `file:line:column` plus a caret highlight.
- `claw check test\parse_recovery.cat`
  This file is expected to fail with multiple parser diagnostics in a single run.
- `claw check test\semantic_diagnostics.cat`
  This file is expected to fail in semantic analysis and should show `file:line:column` plus a caret highlight.
- `claw check test\safety_diagnostics.cat`
  This file is expected to fail semantic safety validation for escaping views, `edit` view returns, and raw-address usage outside `raw`.
- `claw check test\type_arity_diagnostics.cat`
  This file is expected to fail semantic validation for missing and extra generic type arguments.

Structured workspace fixtures:
- `claw check test\pkg_demo`
- `claw build test\pkg_demo`
- `claw build test\pkg_demo\claw.toml`
- Covers root `main.cat`, workspace config parsing, dependency-backed external imports, folder-level `modules.cat`, and whole-project OIR.
- `claw check test\pkg_rootless_demo`
- `claw build test\pkg_rootless_demo`
- Proves the root workspace does not need `modules.cat`; only non-root folders use it.
- `claw check test\pkg_bad_import`
- This fixture is expected to fail and proves that shared function imports across files keep their real types.
- `claw check test\pkg_missing_dep`
- This fixture is expected to fail because an external dependency root is not declared in `[dependencies]`.
- `claw build test\pkg_bad_config`
- This fixture is expected to fail because root `main.cat` is the fixed entry and config may not redeclare it.

Driver diagnostics:
- `claw check test\does_not_exist.cat`
- The error should include the requested path, current working directory, and per-path status details.

IR snapshots:
- `claw emit-air test\emit_air.cat`
- Compare the output with `test\emit_air.expect`
- `claw emit-oir test\emit_air.cat`
- Compare the output with `test\emit_oir.expect`
- `claw emit-oir test\pkg_demo`
- Compare the output with `test\pkg_demo\emit_oir.expect` to validate project-level OIR emission.

MSYS2 UCRT64 runner:
- `bash test/run_frontend_tests.sh`
- Override executable path if needed with `CLAW_EXE=/path/to/claw.exe bash test/run_frontend_tests.sh`

Coverage focus:
- Surface syntax and declarations
- Parser recovery with multiple diagnostics per file
- Type and shape / choice validation
- Generic instantiation in field access and `pick`
- Generic type arity checking
- Scope isolation
- Ownership moves on owned values
- Lexical view borrows, safe view returns, and raw fences
- Structured parser, semantic, and ownership diagnostics
- Prelude output builtins `print` / `println`
- Clear missing-file diagnostics
- Typed cross-unit imports for functions, shapes, and choices
- Workspace config parsing and dependency-gated external import roots
- Typed AIR emission baseline
- Whole-project OIR emission closer to backend
