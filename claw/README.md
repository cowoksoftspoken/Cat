# Claw

Claw is an experimental systems programming language and compiler project built around one core slogan:

**Fast, Safe, Simple**

- **Fast**: native code generation, predictable execution, no hidden GC, no implicit heap cost.
- **Safe**: ownership, definite initialization, lexical views, typed external boundaries, and explicit `raw` regions are meant to keep safe code memory-safe by default.
- **Simple**: the surface model stays smaller and easier to reason about than Rust. Users think in owned values, `look`, `edit`, `raw`, and explicit control over cost.

## Current Status

Claw is currently at the **hardened frontend + first LLVM backend entry** stage.

Implemented today:
- lexer and parser for the current Claw syntax direction
- structured diagnostics with file, line, column, and caret highlights
- nominal types for `shape` and `choice`
- basic generics support in semantic analysis
- semantic checking for bindings, assignments, calls, `pick`, `lift`, and control flow
- ownership checking for moves on owned values
- definite initialization checking for locals, reassignment, moves, and current control-flow joins
- lexical borrow checking for local `look` / `edit` views
- safe view-return validation, including builtin methods that return borrows from their receiver
- raw-address fencing for safe code paths
- explicit drop scheduling at reassignment, scope exit, and early control-flow exits in OIR-backed ownership lowering
- contextual integer-literal typing across Byte / Int* / UInt* / Bits* / USize / ISize without implicit numeric widening
- stable `native64` frontend rules for `USize` / `ISize`, independent of the compiler host width
- bare, suffixed, and exponent float literals with contextual fitting into Float32 / Float64
- static builtin method dispatch on core receivers such as `Text.len()`, `Text.slice(start, len)`, `Text.find_byte(b)`, `Bytes.byte_at(i)`, and mutable container methods like `reserve()`, `truncate()`, `shrink_to_fit()`, and `clear()`
- builtin container generic arity enforcement for forms such as `Span of T`, `Vec of T`, `Table of K, V`, `Set of T`, `Heap of T`, and `Ring of T`
- OIR as a structured in-memory model (`Program/Realm/Decl/Function/Block/Inst`) with textual printing layered on top
- LIR as a backend-facing structured model derived from OIR, with CFG blocks, explicit stack objects, phi-like control joins, explicit builtin bounds checks with defect edges, explicit `lift` success/fail edges, explicit raw-region tagging, typed external call ABI/linkage metadata, explicit unsafe-boundary tagging, call kinds, and textual inspection via `emit-lir`
- canonical type layout, aggregate ABI pass classification, and symbol linkage metadata for named types and functions across OIR and LIR
- typed dependency function contracts in `claw.toml`, including `raw` and `safe` external function signatures that flow into sema, OIR, and LIR
- hardened foreign-ABI safety rules so non-`claw` safe external contracts only admit FFI-stable boundary types
- workspace loading with root `main.cat`
- folder module publishing through `modules.cat`
- project config loading from `claw.toml`
- AIR, OIR, LIR, and initial LLVM IR textual emission for inspected lowering stages
- separate frontend and backend regression suites

Validated environment:
- MSYS2 UCRT64
- CMake + g++ build
- LLVM toolchain available through MSYS2 UCRT64
- frontend regression runner in `test/run_frontend_tests.sh`
- backend regression runner in `test_backend/run_backend_tests.sh`

## What Works Right Now

The frontend already rejects several important bug classes:
- use-after-move
- moving an owned value while it is borrowed
- reading a value before it is definitely initialized
- reading a moved-out `slot` before reinitialization
- creating an `edit` borrow while a `look` borrow is still alive
- returning a view that escapes a local owner
- returning `edit` views from safe functions
- raw-address usage outside explicit `raw` boundaries
- using dependency-backed external calls without a shared signature as ordinary values; opaque external results are statement-only and require an explicit `raw` block
- using typed dependency external calls outside their declared safety boundary; `raw` contracts require explicit `raw`, while `safe` contracts can participate in typed value flow
- non-exhaustive or malformed `pick` branches
- various typed import and workspace resolution failures with useful diagnostics

The initial LLVM backend is already alive for a strict subset:
- direct function definitions and direct calls
- integer arithmetic and integer / float comparisons
- branch and goto lowering
- stack locals through `alloca`, `store`, and `load`
- runtime `print` / `println` lowering
- string constant lowering to module globals
- safe typed external scalar calls
- checked lowering for core `Text` builtins such as `len`, `is_empty`, `byte_at`, `first_byte`, `last_byte`, and `slice`
- explicit LLVM lowering for the current `bounds_check` path into branch-to-defect flow
- textual LLVM IR emission through `claw emit-llvm ...`

Backend bring-up is validated today by assembling emitted IR with `llvm-as` and lowering it with `llc` for the current backend fixtures.

## What Is Not Finished Yet

Claw is not yet a finished production compiler.

Important missing pieces:
- broader LLVM lowering coverage for `pick`, `lift`, richer aggregate operations, more builtin dispatch cases, and deeper checked container paths beyond the current `Text` subset
- fuller typed raw / FFI contracts for memory operations, effects, and non-function boundaries
- richer ownership precision across more complex aliasing cases
- concurrency-related safety checks such as `sendable` / `shareable`
- optimization passes, executable linking flow, and runtime integration beyond the current subset

So while the frontend is hard and the first LLVM path is now alive, the language should still not yet be described as fully Rust-level safe or production-complete.

## Language Direction

Claw is being designed as a structured systems language for production use.

Current design direction includes:
- `main.cat` as the root workspace entry file
- `fn main()` or `fn main() -> Int32` as the validated entry point
- `modules.cat` for publishing non-root folder modules to outer scopes
- `claw.toml` with project metadata and dependencies
- explicit ownership and borrowing through value categories instead of hidden behavior
- a stable frontend `native64` data model for `USize` / `ISize` until explicit target selection is added later

Example project config shape:

```toml
[project]
name = "TICTACTOE"
version = "0.1.0"
edition = "2025"

[dependencies]
term = { version = "1.0.0", abi = "claw", emit = "raw fn emit(message: look Text) -> Unit {}" }
```

## Immediate Next Plan

The correct order from here is:

1. Expand LLVM lowering coverage on top of the current LIR contract.
2. Lower canonical layouts, linkage, drops, bounds checks, `pick`, and `lift` more completely into LLVM IR.
3. Tighten typed raw / FFI contracts for memory operations, effects, and pointer-oriented boundaries in parallel with backend work.
4. Expand builtin method coverage only where dispatch stays compile-time, borrow soundness remains intact, and cost remains explicit.
5. Add concurrency-oriented safety layers and later optimizations after the first end-to-end native pipeline is alive.

## Additional Planned Type Work

One upcoming language task is to expand data type support so the language does not feel centered only around `Int32`.

Planned direction:
- keep widening numeric coverage consistent with the current integer, float, exponent, and size-type literal rules
- strengthen support for the wider integer families already present in the design
- add more range-aware diagnostics where numeric targets still have weak spots
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

Run the backend LLVM bring-up suite:

```bash
bash test_backend/run_backend_tests.sh
```

Inspect lowering stages manually:

```bash
claw emit-air path/to/file.cat
claw emit-oir path/to/file.cat
claw emit-lir path/to/file.cat
claw emit-llvm path/to/file.cat
```

## Repository Layout

- `src/`
  Compiler source code.
- `src/backend/`
  Initial LLVM backend emission.
- `test/`
  Frontend regression corpus and workspace fixtures.
- `test_backend/`
  Separate LLVM/backend bring-up fixtures.
- `docs/`
  Internal design status and roadmap notes.

## Summary

Claw now has a hard frontend foundation: parser, diagnostics, semantic analysis, ownership checks, definite initialization checks, workspace structure, typed external boundaries, canonical layout metadata, and backend-facing OIR/LIR.

The project has also crossed the first backend threshold: LLVM emission exists, backend fixtures are separated from frontend fixtures, and the current subset already assembles through LLVM tools. The next job is to deepen that backend coverage without weakening the `Fast, Safe, Simple` contract.
