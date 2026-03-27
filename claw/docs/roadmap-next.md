# Roadmap Next

## Immediate Technical Order

1. Continue the revised-surface migration through the type and collection model.
2. Replace remaining old user-facing terminology in sema, IR text, diagnostics, and docs.
3. Prepare receiver-first builtin method dispatch around the revised collection story.
4. Once the revised surface is stable enough, widen LLVM and native coverage again.
5. Only after that continue deeper raw / FFI contracts, broader runtime paths, and later optimization work.

## Current Compiler Position

Right now we have:
- revised frontend syntax alive for `val`, `var`, `ref`, `if`, `return`, `Result`, and `try`
- revised-only frontend, backend, and native test suites
- LLVM IR emission still alive under the migration
- native `.exe` generation alive for the current revised subset

That means the repo is in a good state for the next wave:
- the old tests are no longer polluting signal
- the revised surface already has a verified foothold
- backend work can continue later without dragging the legacy surface back in

## Next Session Focus

The next wave should stay disciplined:

1. finish revised type names and collection terminology where the compiler still exposes old surface assumptions
2. settle the next revised semantic areas in order, not all at once
3. keep docs and tests aligned as each revised section lands
4. defer receiver-first builtin API expansion until the revised type surface is ready enough to support it cleanly

## Known Migration Tension

Two truths are important at the same time:
- the compiler already has real LLVM and native paths
- the revised language surface is still being normalized

So the right strategy remains:
- do not throw backend work away
- do not let backend progress force us to freeze the wrong public syntax

We keep the frontend surface deliberate first, then widen the backend again on top of that cleaner base.
