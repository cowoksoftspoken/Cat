# Claw

Claw is the compiler for `C@`, a systems language being built around one slogan:

**Fast, Safe, Simple**

- `Fast`: native code generation, explicit cost, no hidden GC path.
- `Safe`: ownership, definite initialization, explicit `raw`, and typed external boundaries.
- `Simple`: a smaller surface model than Rust, with syntax that stays direct and readable.

## Current Status

The compiler is in an active **revised-surface migration**.

The revised surface already alive in the compiler today includes:
- `fn name(...) { ... }`
- `return`
- `val` and `var`
- `ref` and `ref mut`
- `if` / `else`
- `Result[T, E]`, `Ok(...)`, `Fail(...)`
- `try expr`
- `try expr else err { ... }`
- root `main.cat`
- workspace config through `claw.toml`

The compiler still keeps the deeper frontend and backend pipeline in place:
- structured diagnostics
- semantic analysis
- ownership checking
- AIR / OIR / LIR
- LLVM IR emission
- initial native executable generation

## What Is Verified Right Now

Revised-only suites now live in:
- `test/`
- `test_backend/`
- `test_native/`

Verified commands:

```bash
claw check path/to/file.cat
claw validate path/to/workspace
claw air path/to/file.cat
claw llvm path/to/file.cat
claw build path/to/file-or-workspace
```

Validated today:
- revised frontend parsing and semantic checks
- revised workspace loading with `main.cat`, `src/modules.cat`, and `claw.toml`
- revised `Result[T, E]` and `try` lowering through LLVM
- native `.exe` generation for the current revised subset

## Current Native Scope

`claw build ...` is already real, but the supported subset is still intentionally narrow.

Currently green:
- single-file revised programs
- simple revised workspaces with root `main.cat`
- runtime `println(...)`
- direct arithmetic and direct calls in the current subset

Not claimed yet:
- full revised imported workspace native coverage
- richer aggregate construction paths
- the full revised collection and builtin surface
- broader raw / FFI lowering beyond the current typed subset

## Workspace Shape

Current workspace layout:

```text
my-app/
  claw.toml
  main.cat
  src/
    modules.cat
    math.cat
```

Example `claw.toml`:

```toml
[project]
name = "my_app"
version = "0.1.0"
edition = "2025"

[dependencies]
```

`main.cat` is special:
- it is the fixed workspace entry
- it must contain `fn main()`
- it is not importable as a normal module surface

## Build And Test

Use MSYS2 UCRT64.

Build:

```bash
cmake -S . -B build-ucrt64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build-ucrt64 -j 4
```

Run suites:

```bash
bash test/run_frontend_tests.sh
bash test_backend/run_backend_tests.sh
bash test_native/run_native_tests.sh
```

## Repository Layout

- `src/`
  Compiler source.
- src/codegen/
  LLVM/backend emission and native codegen boundary.
- src/workspace/
  Workspace loading, config parsing, and module graph resolution.
- `test/`
  Revised frontend regression suite.
- `test_backend/`
  Revised LLVM/backend regression suite.
- `test_native/`
  Revised native executable integration suite.
- `docs/`
  Internal status notes and next-wave planning.

## Next Wave

The next wave is still about finishing the revised language surface cleanly before widening backend claims again.

Immediate order:
1. finish the revised type and collection surface migration
2. keep replacing remaining old surface terminology in sema, IR text, and docs
3. prepare receiver-first builtin method dispatch for the revised collection model
4. then continue widening LLVM and native coverage on top of that cleaner surface
