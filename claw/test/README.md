# Frontend Test Corpus

Current corpus files are plain source and snapshot fixtures intended for the compiler driver.

Recommended checks:
- `claw check test\syntax.cat`
- `claw check test\types.cat`
- `claw check test\control_flow.cat`
- `claw check test\scope_isolation.cat`
- `claw check test\prelude_prints.cat`
- `claw check test\generic_instantiation.cat`
- `claw check test\ownership.cat`
  This file is expected to fail ownership checking because it moves the same `Text` twice.
- `claw check test\parse_diagnostics.cat`
  This file is expected to fail in the parser and should show `file:line:column` plus a caret highlight.
- `claw check test\parse_recovery.cat`
  This file is expected to fail with multiple parser diagnostics in a single run.
- `claw check test\semantic_diagnostics.cat`
  This file is expected to fail in semantic analysis and should show `file:line:column` plus a caret highlight.
- `claw check test\type_arity_diagnostics.cat`
  This file is expected to fail semantic validation for missing and extra generic type arguments.

Driver diagnostics:
- `claw check test\does_not_exist.cat`
- The error should include the requested path, current working directory, and per-path status details.

IR snapshots:
- `claw emit-air test\emit_air.cat`
- Compare the output with `test\emit_air.expect`
- `claw emit-oir test\emit_air.cat`
- Compare the output with `test\emit_oir.expect`

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
- Structured parser, semantic, and ownership diagnostics
- Prelude output builtins `print` / `println`
- Clear missing-file diagnostics
- Typed AIR emission baseline
- Lowered OIR emission baseline closer to backend
Structured package fixture:
- `claw check test\pkg_demo\modules.cat`
- `claw build test\pkg_demo\modules.cat`
- Covers `modules.cat`, `pub modules`, `pub entry`, cross-folder resolution, and `import super.{...}`.