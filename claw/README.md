# Claw

Claw is an experimental systems programming language and compiler frontend designed around one core slogan:

**Fast, Safe, Simple**

- **Fast**: native performance, predictable execution, no hidden GC, no implicit heap cost.
- **Safe**: ownership, lexical views, and explicit raw boundaries are intended to make safe code memory-safe by default.
- **Simple**: the surface model is deliberately smaller and easier to reason about than Rust. Users think in terms of owned values, `look`, `edit`, and `raw`, without lifetime syntax.

This repository currently contains the **frontend compiler work** for Claw. It is not yet a full production compiler, but it already has a meaningful and testable semantic core.

## Current Status

Claw is currently at the **frontend validation stage**.

Implemented today:
- lexer and parser for the current Claw syntax direction
- structured diagnostics with file, line, column, and caret highlights
- nominal types for `shape` and `choice`
- basic generics support in semantic analysis
- semantic checking for bindings, assignments, calls, `pick`, `lift`, and control flow
- ownership checking for moves on owned values
- lexical borrow checking for local `look` / `edit` views
- safe view-return validation
- raw-address fencing for safe code paths
- workspace loading with root `main.cat`
- folder module publishing through `modules.cat`
- project config loading from `claw.toml`
- AIR and OIR textual emission for inspected lowering stages
- regression corpus covering both passing and expected-failing cases

Validated environment:
- MSYS2 UCRT64
- CMake + g++ build
- frontend regression runner in `test/run_frontend_tests.sh`

## What Works Right Now

The frontend already rejects several important bug classes:
- use-after-move
- moving an owned value while it is borrowed
- creating an `edit` borrow while a `look` borrow is still alive
- returning a view that escapes a local owner
- returning `edit` views from safe functions
- raw-address usage outside explicit `raw` boundaries
- non-exhaustive or malformed `pick` branches
- various typed import and workspace resolution failures with useful diagnostics

In practice, this means Claw is already more than a parser prototype. It has real semantic enforcement.

## What Is Not Finished Yet

Claw is **not ready for LLVM yet**.

Important missing pieces:
- definite initialization analysis across all control-flow joins
- explicit drop / destruction model in the frontend IR contract
- a richer ownership model across more complex aliasing cases
- full raw / FFI safety contracts
- concurrency-related safety checks such as `sendable` / `shareable`
- OIR as a real in-memory IR model instead of a primarily printed lowered form
- LLVM backend and native code generation

So while the project is already strongly safety-oriented, it should not yet be described as fully Rust-level safe.

## Language Direction

Claw is being designed as a structured systems language for production use.

Current design direction includes:
- `main.cat` as the root workspace entry file
- `fn main()` or `fn main() -> Int32` as the validated entry point
- `modules.cat` for publishing non-root folder modules to outer scopes
- `claw.toml` with project metadata and dependencies
- explicit ownership and borrowing through value categories instead of hidden behavior

Example project config shape:

```toml
[project]
name = "TICTACTOE"
version = "0.1.0"
edition = "2025"

[dependencies]
```

## Immediate Next Plan

The next work is not "jump to backend no matter what". The correct order is:

1. Close the remaining frontend safety gaps.
2. Turn OIR into a real IR data model.
3. Make whole-project lowering deterministic and backend-ready.
4. Start the LLVM backend only after the frontend ownership model is stable enough.

## Additional Planned Type Work

One upcoming language task is to expand data type support so the language does not feel centered only around `Int32`.

Planned direction:
- improve scalar coverage and default literal typing behavior
- strengthen support for the wider integer families already present in the design
- make type behavior feel more complete across arithmetic, generics, diagnostics, and lowering
- keep the model explicit and simple rather than adding implicit widening rules that hide cost or risk

The goal is to make Claw feel like a serious production systems language, not a toy frontend with one convenient numeric default.

## Build And Test

Build with MSYS2 UCRT64:

```bash
cmake -S . -B build-ucrt64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build-ucrt64 -j 4
```

Run the frontend regression suite:

```bash
bash test/run_frontend_tests.sh
```

## Repository Layout

- `src/`
  Compiler frontend source code.
- `test/`
  Regression corpus and workspace fixtures.
- `docs/`
  Internal design status and roadmap notes.

## Summary

Claw already has a real frontend foundation: parser, diagnostics, semantic analysis, ownership checks, workspace structure, and IR emission.

The current milestone is to make the frontend safety model complete and backend-ready. After that, the project can move into LLVM with a much stronger foundation.
