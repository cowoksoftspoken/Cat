# C@

C@ is an in-progress systems programming language and compiler built around one design promise:

**Fast, Safe, Simple**

- **Fast**: native code generation, explicit costs, no hidden GC, and predictable control over data layout and ownership.
- **Safe**: ownership, borrowing, typed error handling, explicit unsafe boundaries, and a compiler that aims to prevent memory misuse in safe code.
- **Simple**: a smaller, clearer surface than traditional systems languages, with familiar syntax and fewer user-facing rules.

This repository contains the language project at the workspace root and the compiler implementation in [`claw/`](./claw).

## Project Status

C@ is under active language and compiler development. The current compiler already supports a meaningful revised surface and can:

- parse, type-check, and borrow-check revised C@ source
- lower to internal AIR, OIR, and LIR stages
- emit LLVM IR
- build native `.exe` programs for the currently supported subset
- run revised frontend, backend, and native test suites

The project is **not feature-complete yet**. The language surface below reflects the intended revised direction, with emphasis on the parts that are already implemented or actively being hardened.

## Design Direction

C@ is targeting production-grade systems programming with these principles:

- default-safe ownership and borrowing
- explicit escape hatches such as `raw` and `foreign c`
- strong module boundaries and deterministic workspaces
- readable, familiar syntax with minimal ceremony
- an implementation strategy that stays compatible with native code generation and LLVM-backed compilation

## Current Surface Overview

### Bindings

```cat
val host = "127.0.0.1"
var total = 0
```

- `val` creates an immutable binding.
- `var` creates a mutable binding.

### Functions

```cat
fn add(left: Int32, right: Int32) -> Int32 {
    left + right
}

fn greet(name: ref Str) {
    println(name)
}
```

- `fn` declares a function.
- The last expression in a block can be used as the return value.
- `return` is still available when explicit early exit is needed.
- `fn main()` may return `Unit` implicitly or explicitly, and `fn main() -> Int32` is also allowed for OS exit codes.

### Borrowing

```cat
fn inspect(text: ref Str) {
    println(text)
}

fn clear(items: ref mut Vec[Int32]) {
    items.clear()
}
```

- `ref T` is a read-only borrow.
- `ref mut T` is a mutable borrow.
- Lifetimes are implicit; diagnostics are intended to teach without exposing lifetime syntax.

### Control Flow

```cat
if total > 0 {
    println("positive")
} else {
    println("zero")
}

loop {
    stop
}

scan item over values {
    println(item)
}
```

- `if` / `else` handle branching.
- `loop` supports conditional and unconditional looping.
- `scan` iterates over collections and other iterable values.
- `stop` and `skip` control loop flow.

### Data Types

Current revised direction:

- Integers: `Int8`, `Int16`, `Int32`, `Int64`, `Int128`
- Unsigned: `UInt8`, `UInt16`, `UInt32`, `UInt64`, `UInt128`
- Floating point: `Float32`, `Float64`, `Float128`
- Aliases: `Int`, `UInt`, `Float`
- Other core types: `Bool`, `Char`, `Str`, `USize`, `Unit`

### Structured Data

```cat
shape Config {
    share host: Str
    share port: Int32
}

choice Result[T, E] {
    Ok(value: T)
    Fail(cause: E)
}
```

- `shape` defines nominal record types.
- normal `shape` values cannot store borrowed fields such as `ref T`, `ref mut T`, or nested borrowed storage like `Maybe[ref T]`. Use owned fields, `Anchor[T]`, or `view shape[s]` when the aggregate itself is intentionally scope-bound.
- `choice` defines tagged unions.
- Generics use `[]`.

### Pattern Dispatch

```cat
pick result {
    Ok(value) {
        println(value)
    }
    Fail(err) {
        println(err)
    }
}
```

`pick` is the primary form for explicit choice dispatch.

### Error Handling

```cat
val file = try fs.open(path)

val text = try fs.read(path) else err {
    println(err)
    return Fail(err)
}
```

- `Result[T, E]` models recoverable failure.
- `Maybe[T]` models optional values.
- `Result[T, E]` and `Maybe[T]` are must-use in the current compiler. Ignoring them as bare statements is rejected; handle them, bind them, or explicitly discard them.
- `try expr` propagates failure when the current function also returns `Result`.
- `try expr else err { ... }` handles failure locally.

### Workspace Model

A revised workspace uses a fixed root entry file and explicit published modules.

```text
my-app/
  main.cat
  claw.toml
  src/
    modules.cat
    math.cat
    util.cat
```

Rules:

- `main.cat` is required at the workspace root.
- exactly one `fn main` is required inside `main.cat`.
- `main.cat` is entry-only and cannot be imported as a normal module.
- `fn main` is not a normal callable function surface for user code.
- non-root folders use `modules.cat` to publish outward-facing modules.
- imports are path-based and explicit.

Example:

```cat
import src.math.{ add }
import super.{ util }
```

### Unsafe and Foreign Boundaries

```cat
raw {
    emit("boot")
}

foreign c {
    fn puts(text: ref Str) -> Int32
}
```

- safe code is the default.
- `raw` isolates unsafe operations.
- `foreign c` is the intended foreign boundary for C interoperability.

### Stable Reference Helpers

The revised model also includes explicit stabilization tools for complex ownership scenarios.

```cat
val stable = Anchor.new("hello")
println(stable.get())
```

Current implementation status:

- `Anchor.new(value)` is implemented for owned payloads with stable ownership.
- `anchor.get()` yields `ref T`.
- `scope` and scoped refs are implemented in the current compiler wave.
- local borrowed aggregate carriers via `view shape Name[s] { ... }` are implemented, including scope-bound construction and escape checks.
- `Arena` is still part of the design direction and is not yet fully implemented.
- broader scoped-type propagation in arbitrary signatures and more advanced lifetime helpers are still being hardened.

## Example Program

```cat
import src.demo.{ word_count }

fn main() -> Int32 {
    val words = Vec[Str]["hello", "world", "hello"]
    val counts = word_count(ref words)
    println(counts.len())
    0
}
```

## Compiler Layout

The compiler implementation lives in [`claw/`](./claw).

Key areas:

- `claw/src/parser/` - lexer, parser, AST construction
- `claw/src/analysis/` - semantic analysis, type resolution, ownership and borrow checking
- `claw/src/ir/` - AIR, OIR, and LIR lowering
- `claw/src/backend/` - LLVM IR emission and native build flow
- `claw/src/driver/` - CLI entry points and build commands
- `claw/runtime/` - native runtime support used by generated programs

## Compiler Commands

From the compiler directory, the main commands are:

- `check` - parse, type-check, and borrow-check source or workspace
- `validate` - validate workspace graph and entry rules
- `build` - produce a native executable for the supported subset
- `air` - print AIR
- `oir` - print OIR
- `lir` - print LIR
- `llvm` - print LLVM IR

## Testing

The active revised test suites are separated by layer:

- `claw/test/` - frontend and semantic checks
- `claw/test_backend/` - LLVM IR regression checks
- `claw/test_native/` - native executable integration tests

On the current MSYS2 UCRT64 setup, the project is typically built and tested with:

```bash
C:/msys64/ucrt64/bin/cmake.exe --build claw/build-ucrt64-clang --target claw -- -j 4
bash claw/test/run_frontend_tests.sh
bash claw/test_backend/run_backend_tests.sh
bash claw/test_native/run_native_tests.sh
```

The backend and native suites intentionally keep generated `.ll` artifacts so they can be inspected after a run.

## What Is Already Working

Broadly, the current compiler already covers:

- revised `val` / `var` bindings
- revised `ref` / `ref mut` borrowing
- `Result[T, E]`, `Maybe[T]`, `try`, and explicit `pick`
- workspace entry validation for `main.cat`
- borrow checking for path-based field overlap and `Vec` to `Span` rules
- rejection of borrowed fields in normal `shape` declarations
- must-use enforcement for `Result[T, E]` and `Maybe[T]` statement values
- scoped references and `Anchor`
- LLVM IR emission for the supported subset
- native `.exe` generation for the supported subset

## What Is Still In Progress

Major areas still being matured include:

- the remaining type-surface cleanup against the revised PRDs
- broader builtin method dispatch in receiver-first form
- `Arena` and other advanced ownership helpers
- deeper borrow-checker maturity for more complex escape and aggregate scenarios
- broader native/backend coverage beyond the current supported subset
- ongoing documentation and ergonomics refinement

## Language Governance In This Repo

This repository is intentionally PRD-driven.

The language direction is defined by the revised design documents in [`revise/`](./revise), and implementation work is expected to track those revisions carefully and explicitly.

## License

This project is licensed under the terms in [`LICENSE`](./LICENSE).
