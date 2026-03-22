# C@

C@ is a systems programming language designed around one slogan:

**Fast, Safe, Simple**

- **Fast** means native performance, predictable cost, no hidden GC, and no invisible allocation model.
- **Safe** means memory safety is the default in safe code through ownership, views, definite initialization, explicit failure handling, and explicit `raw` boundaries.
- **Simple** means the surface syntax stays smaller and easier to reason about than Rust. The language avoids lifetime syntax, avoids punctuation-heavy rituals, and keeps control flow explicit.

This README describes the intended **language surface**. The compiler implementation lives in [`claw/`](./claw).

## Status

The syntax below is the current language direction.

Some parts are already implemented in the compiler frontend and initial LLVM backend. Some parts are still design targets. This document describes the intended user-facing language, not only the subset already compiled today.

## Core Ideas

C@ is built around a few rules:

- one obvious function syntax: `fn name(args) -> Type { ... }`
- one obvious block syntax: braces
- `hold` for immutable local bindings
- `slot` for mutable storage
- `look` and `edit` for safe views
- `pick` for tagged-union branching
- `lift` for explicit success-or-failure extraction
- `raw` for unsafe or foreign boundaries
- `realm` plus explicit `import` for module structure

## File Model

Every source file is a `.cat` file and belongs to one `realm`.

```cat
realm app.main
```

A workspace has a fixed root entry file:

```text
main.cat
claw.toml
```

`main.cat` is special. It is the workspace entry source and the root workspace does not need `modules.cat`.

## Comments

Single-line comments use `//`.

```cat
// this is a comment
hold port: Int32 = 8080
```

## Bindings And Assignment

### Immutable binding

```cat
hold host: Text = "127.0.0.1"
hold limit = 64
```

### Mutable storage

```cat
slot total: Int32 = 0
slot index = 0
```

### Assignment

```cat
total = total + 1
index = index + 1
```

Rules:
- `hold` creates an immutable local binding.
- `slot` creates mutable storage.
- assignment targets mutable storage, not immutable bindings.
- a moved-out `slot` must be reinitialized before it can be read again.

## Functions

### Function declaration

```cat
fn add(left: Int32, right: Int32) -> Int32 {
    give left + right
}
```

### Unit-returning function

```cat
fn greet(name: look Text) {
    println(name)
}
```

### Return

```cat
give value
give
```

Rules:
- `fn` is mandatory.
- parameters use `name: Type`.
- the return type is optional only for `Unit`.
- the current design keeps function syntax explicit and compact.

## Conditionals

```cat
when size > 0 {
    give size
} otherwise {
    give 0
}
```

Rules:
- `when` is the conditional form.
- `otherwise` is the alternative branch.
- conditions are explicit expressions.
- there is no hidden propagation or implicit control transfer.

## Loops

### Conditional loop

```cat
loop index < limit {
    index = index + 1
}
```

### Infinite loop

```cat
loop {
    work()
}
```

### Iteration

```cat
scan item over data {
    total = total + item
}
```

### Loop control

```cat
stop
skip
```

Rules:
- `loop condition {}` is the while-form.
- `loop {}` is the infinite form.
- `scan` is the iteration form.
- `stop` exits the nearest loop.
- `skip` advances to the next iteration.

## Shapes

A `shape` is a nominal record type.

```cat
shape Config {
    share host: Text
    share port: Int32
}
```

Rules:
- `shape` defines a record-like data type.
- `share` exposes a field or item outside the defining realm.
- mutability is controlled by the view used to access the value, not by special field mutability syntax.

### Shape construction

```cat
fn default_config() -> Config {
    give Config(host: "127.0.0.1", port: 8080)
}
```

### Field access

```cat
fn inspect(cfg: Config) -> Int32 {
    give cfg.port
}
```

## Choices

A `choice` is a tagged union.

```cat
choice Maybe of T {
    none
    some(value: T)
}
```

```cat
choice Signal {
    ready
    failed(code: Int32)
}
```

Rules:
- each variant has a unique tag.
- payload names are local to matching branches and constructors.
- generic choices use `of`.

## Variant Dispatch With `pick`

```cat
fn show(value: Maybe of Int32) {
    pick value {
        none {
            println("empty")
        }
        some(v) {
            print(v)
        }
    }
}
```

```cat
fn unwrap(signal: Signal) -> Int32 {
    pick signal {
        ready {
            give 0
        }
        failed(code) {
            give code
        }
    }
}
```

Rules:
- `pick` is the variant-dispatch form.
- patterns are explicit and local.
- exhaustiveness is required for closed choices.

## Error Handling With `Outcome`

Recoverable failure is expressed with `Outcome of T, E`.

```cat
choice Outcome of T, E {
    ok(value: T)
    fail(cause: E)
}
```

### Explicit lift

```cat
lift expression as value fail issue {
    failure_block
}
```

Meaning:
- if the result is `ok(v)`, bind `value = v` and continue.
- if the result is `fail(e)`, bind `issue = e`, run the fail block, and the fail path must leave the current flow explicitly.

Example:

```cat
fn read_text(path: look Text) -> Outcome of Text, ReadFault {
    lift fs.open_text(path) as file fail issue {
        give fail(map_fault(issue))
    }

    give ok(fs.read_all(file))
}
```

This keeps failure flow explicit, typed, and easy to audit.

## Ownership, Views, And Mutation

C@ distinguishes owned values from safe views.

### Read-only view

```cat
fn size(text: look Text) -> USize {
    give text.len()
}
```

### Mutable view

```cat
fn clear_bytes(bytes: edit Bytes) {
    bytes.clear()
}
```

Rules:
- `look T` is a read-only view.
- `edit T` is a mutable view.
- safe code must respect borrow rules.
- moving an owned value while it is still viewed is rejected.
- returning invalid escaping views is rejected.

## Builtin Method Dispatch

Builtin methods are resolved statically and remain cost-visible.

Examples:

```cat
text.len()
text.is_empty()
text.byte_at(0)
text.slice(0, 2)
bytes.reserve(64)
bytes.clear()
items.capacity()
```

The intent is compile-time method dispatch on known core types, not hidden dynamic dispatch.

## `raw` Blocks

Unsafe or foreign operations must be isolated inside `raw`.

```cat
raw {
    emit("boot")
}
```

Rules:
- safe code is the default.
- raw pointers, foreign calls, and unsafe boundaries must be explicit.
- `raw` is meant to be auditable and contained.

## Prelude Output

`print` and `println` are prelude builtins.

```cat
print(value)
println("hello")
```

No explicit import is required for them.

## Realms, Modules, And Imports

This is the core modularization model of C@.

### What a `realm` is

A `realm` is the canonical module identity of a source file.

It has four jobs at once:
- it gives the file its logical name
- it defines the namespace for the items declared inside the file
- it acts as a visibility boundary
- it gives the compiler a stable unit for import resolution and dependency graphs

Examples:

```cat
realm main
realm src.api
realm src.runtime
```

In a structured workspace, the realm should match the file path:
- `main.cat` -> `realm main`
- `src/api.cat` -> `realm src.api`
- `src/runtime.cat` -> `realm src.runtime`

This is intentional. It keeps the module graph explicit, deterministic, and easy to navigate.

### What `share` does

Items are private to their realm by default.

`share` exports an item from that realm so other realms may import it.

```cat
realm src.api

share shape Config {
    share port: Int32
}

share fn add(base: Int32, extra: Int32) -> Int32 {
    give base + extra
}

fn helper() -> Int32 {
    give 7
}
```

In that example:
- `Config` is public to other realms
- `add` is public to other realms
- `helper` stays private to `src.api`

So `share` is item-level visibility.

### What `modules.cat` does

`modules.cat` is not the same as `share`.

- `share` publishes items from one file/realm.
- `modules.cat` publishes which files or child folders in a non-root directory may be reached from outside that directory.

Example:

```cat
pub modules {api, runtime, util}
```

If this file lives at `src/modules.cat`, it means outer code is allowed to reach:
- `src.api`
- `src.runtime`
- `src.util`

Without that `modules.cat`, or without those names listed, code outside `src/` should not be able to traverse into those modules even if the files physically exist.

So `modules.cat` is folder-level publishing.

### Root workspace rule

The root workspace is special.

It uses:
- `main.cat` as the fixed entry source
- `fn main()` or `fn main() -> Int32` as the entry function

The root workspace does **not** need `modules.cat`.

That means:
- root entry is always clear
- the top-level package stays structured without extra boilerplate
- folder publishing is only needed when crossing non-root directory boundaries

### Import forms

C@ keeps imports explicit and small.

#### Import shared items from a realm

```cat
import src.api.{Config, Signal, add}
```

Use this when you want specific public items from another realm.

#### Import a module name

```cat
import src.{runtime}
```

Use this when you want to call shared items through the module path, for example:

```cat
runtime.banner()
```

#### Import a sibling module

```cat
import super.{util}
```

This is the short form for importing a sibling module from the same folder module space.

Its job is to avoid repeating the full realm path when two files live side by side.

Example idea:
- current file is in the same folder as `util.cat`
- `import super.{util}` brings that sibling module into scope
- later code can call `util.answer()`

#### Import from a dependency root

```cat
import term.{emit}
```

This is for external package roots declared in `claw.toml`.

The dependency root must exist in `[dependencies]`, and its imported surface is defined by the package contract. For typed external functions, that contract may also carry ABI and safety information.

### What an `import` really does

An import is not just a text include.

Its job is to say:
- which external realm or module boundary this file depends on
- which public names become available in this scope
- which package or folder boundary is being crossed
- what symbols the compiler must resolve and type-check

This explicitness matters for all three goals:
- `Fast`: the compiler gets a stable, cacheable dependency graph
- `Safe`: boundaries are visible and auditable
- `Simple`: there is one obvious import model instead of hidden discovery rules

### Visibility and traversal rules

The intended model is:
- private by default
- `share` exports items from a realm
- imports only see shared items
- `modules.cat` publishes non-root child modules outward
- root `main.cat` is special and does not need `modules.cat`
- sibling imports use `super.{...}`
- dependency imports come from roots declared in `claw.toml`

### End-to-end example

Project tree:

```text
main.cat
claw.toml
src/
  modules.cat
  api.cat
  runtime.cat
  util.cat
```

`src/modules.cat`

```cat
pub modules {api, runtime, util}
```

`src/api.cat`

```cat
realm src.api

share shape Config {
    share port: Int32
}

share choice Signal {
    ready
    failed(code: Int32)
}

share fn add(base: Int32, extra: Int32) -> Int32 {
    give base + extra
}
```

`main.cat`

```cat
realm main

import src.{util, runtime}
import src.api.{Config, Signal, add}
import term.{emit}

fn main() -> Int32 {
    raw {
        emit("boot")
    }
    runtime.banner()
    give add(util.answer(), 1)
}
```

What happens here:
- `main.cat` is the workspace entry
- `src/modules.cat` allows outer code to traverse into `src.api`, `src.runtime`, and `src.util`
- `share` makes `Config`, `Signal`, and `add` visible outside `src.api`
- `import src.api.{...}` brings selected shared items into scope
- `import src.{runtime}` brings the module path into scope
- `import term.{emit}` crosses into a declared dependency root

## Workspace Structure

A typical workspace looks like this:

```text
main.cat
claw.toml
src/
  modules.cat
  api.cat
  runtime.cat
  util.cat
```

Root file:

```cat
realm main

import src.{util, runtime}
import src.api.{Config, Signal, add}
import term.{emit}

fn main() -> Int32 {
    raw {
        emit("boot")
    }
    runtime.banner()
    give add(util.answer(), 1)
}
```

Non-root folder index:

```cat
pub modules {api, util, runtime}
```

Rules:
- `main.cat` is the fixed workspace entry file.
- root `main.cat` is entry-only and cannot be imported as a module.
- the root workspace does not need `modules.cat`.
- non-root folders use `modules.cat` to publish modules outward.
- `share` controls item visibility inside a realm.
- `modules.cat` controls folder-level traversal from outside the folder.
- the source path and `realm` should match in a structured workspace.


## Project Configuration

The intended config shape is:

```toml
[project]
name = "TICTACTOE"
version = "0.1.0"
edition = "2025"

[dependencies]
term = { version = "1.0.0", abi = "claw", emit = "raw fn emit(message: look Text) -> Unit {}" }
```

Rules:
- `main.cat` is the entry; config should not manually redefine entry.
- dependencies are explicit.
- dependency contracts can describe typed external functions.

## Numeric Literals

Examples:

```cat
1
1.5
42_UInt16
1.5_Float32
```

Current direction:
- explicit suffixes are preferred where precision matters.
- no hidden numeric widening rules that obscure cost or safety.
- size-related types such as `USize` and `ISize` are part of the core model.

## Core Data Type Direction

The language is not intended to be `Int32`-only.

The planned core surface includes families such as:
- `Byte`
- `Bool`
- `Rune`
- `Int8`, `Int16`, `Int32`, `Int64`, `Int128`
- `UInt8`, `UInt16`, `UInt32`, `UInt64`, `UInt128`
- `Bits8`, `Bits16`, `Bits32`, `Bits64`, `Bits128`
- `Float32`, `Float64`
- `USize`, `ISize`
- `Text`, `Bytes`
- `Span of T`, `Vec of T`, `Table of K, V`, `Set of T`, `Ring of T`, `Heap of T`

## Small End-To-End Example

```cat
realm main

choice Signal {
    ready
    failed(code: Int32)
}

fn handle(signal: Signal) -> Int32 {
    pick signal {
        ready {
            println("ready")
            give 0
        }
        failed(code) {
            print(code)
            give code
        }
    }
}

fn main() -> Int32 {
    hold title: Text = "C@"
    print(title.len())
    give handle(ready)
}
```

## Design Summary

C@ aims to be:
- **Fast** enough to feel native like C
- **Safe** enough that memory-safety mistakes are pushed out of safe code
- **Simple** enough that the syntax and mental model stay lighter than Rust

That is the language direction this repository is building toward.
