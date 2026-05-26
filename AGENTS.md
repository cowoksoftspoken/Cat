# AGENTS.md

This repository is the language project for **C@** and the compiler implementation in `claw/`.

## Primary Rule

Work from the revised PRDs first:

1. `revise/cat-language-design-summary_all.md`
2. `revise/cat-language-design-summary.md`

Use the larger `_all` document as the richer source of semantics and maturation details. Use the smaller summary as the compact cross-check.

If the two PRDs conflict:
- prefer the latest explicit user decision from chat
- if chat does not resolve it, mark the PRD with `[conflict]` and explain why
- do not silently invent a third design

## Language Direction

C@ is aiming for:
- native performance
- explicit ownership
- implicit lifetimes
- no GC
- explicit unsafe boundaries
- diagnostics that teach without exposing lifetime jargon

Current non-negotiable design rules:
- revised syntax only: `val`, `var`, `ref`, `ref mut`, `return`, `if`, `else`, `Result[T, E]`, `Maybe[T]`, `try`, `pick`
- do not reintroduce legacy syntax such as `hold`, `slot`, `give`, `look`, `edit`, `when`, `otherwise`, `realm`, or generic `of`
- normal `shape` declarations may not store borrowed fields
- `Result[T, E]` and `Maybe[T]` are must-use
- `Anchor[T]` is a stable owned heap cell, not a smart-pointer family
- `raw` is explicit and must stay auditable

## Implementation Standard

Do not implement features half-way. For any meaningful language feature, finish the relevant path end-to-end where applicable:
- parser / AST
- semantic analysis
- ownership / borrow checking
- IR lowering
- backend or native flow if the feature reaches codegen
- tests
- docs

If a feature is intentionally staged, document the boundary clearly and keep tests honest about the supported subset.

## Repo Layout

Important directories:
- `claw/src/parser/`
- `claw/src/analysis/`
- `claw/src/ir/`
- `claw/src/backend/`
- `claw/src/driver/`
- `claw/runtime/`
- `claw/test/`
- `claw/test_backend/`
- `claw/test_native/`
- `revise/`

The outer `README.md` is the professional user-facing language overview. Keep it aligned with the revised surface and the actual supported subset.

## Build And Test

Use the MSYS2 UCRT64 toolchain. The most reliable flow is to build with CMake and run the scripts through UCRT64 bash.

Build:
```bash
C:/msys64/ucrt64/bin/cmake.exe --build claw/build-ucrt64-clang --target claw -- -j 4
```

Frontend tests:
```bash
bash claw/test/run_frontend_tests.sh
```

Backend tests:
```bash
bash claw/test_backend/run_backend_tests.sh
```

Native tests:
```bash
bash claw/test_native/run_native_tests.sh
```

When invoking from PowerShell, prefer launching through MSYS2 UCRT64 shell if plain `bash.exe` behaves badly.

## Artifacts

Do not remove the intentional test artifacts behavior:
- backend tests keep `.ll` files in `claw/test_backend/artifacts/`
- native tests keep `.ll` and `.exe` files in `claw/test_native/artifacts/`

These are part of the workflow for inspection and debugging.

## Documentation Discipline

When a section of the PRD is implemented, mark it with `[sudah]`.
When a design is still unresolved or intentionally staged, mark it with `[conflict]` and explain why.

Do not claim a feature is complete in docs unless the implementation and tests actually support it.

## Current Implemented Highlights

At the time of this file:
- revised syntax is active
- workspace entry validation is active
- `Result[T, E]`, `Maybe[T]`, `try`, and `pick` are active
- path-based borrow checking is active
- `Vec` element borrows require `Span`
- `scope` and scoped refs are active
- local `view shape[s]` borrowed aggregates are active
- `Anchor.new(value)` and `anchor.get()` are active
- borrowed fields in normal `shape` are rejected
- must-use for `Result` and `Maybe` statement values is enforced
- LLVM IR and native `.exe` generation exist for the supported subset

## Near-Term Roadmap

Stay on-track with the PRD and recent user guidance:
1. keep hardening the borrow model
2. harden broader scoped-type propagation and borrowed aggregate ergonomics
3. continue receiver-first method dispatch maturation
4. keep diagnostics human and actionable
5. implement `Arena` minimally before larger systems
6. defer advanced contracts, macros, async, and shared ownership until the core is harder
