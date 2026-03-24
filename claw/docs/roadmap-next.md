# Roadmap Next

## Immediate Technical Order

1. Widen native executable coverage on top of the current LLVM/LIR contract.
2. Lower richer aggregate/object cases and deeper `lift` / `pick` paths into LLVM IR now that scan/loop control, aggregate ABI, and the first native executable path are live.
3. Keep whole-project lowering deterministic while tightening ownership edge cases that appear during backend and native integration.
4. Expand typed raw / FFI contracts beyond function signatures into memory operations, effects, and pointer-oriented boundaries.
5. Add concurrency-oriented safety layers and only then move into optimization work.

## Current Native Status

The first native Windows executable pipeline is now alive because:
- `build` produces `.exe` files for the current supported subset
- `validate` keeps workspace graph checking explicit now that `build` is reserved for native output
- the compiler emits a public native `@main` wrapper over the validated Claw entry symbol
- the bundled runtime currently covers the runtime print/println subset used by native smoke tests
- the native path is regression-tested separately in `test_native/`
- frontend, backend IR snapshots, and native executable checks are now all green together

## Next Session Focus

1. Add a source-level aggregate construction / initialization path that stays ownership-safe instead of relaxing definite-init checks for partial field writes.
2. Extend that construction story into choice / `Outcome` constructors where the expected type can resolve variants like `ok(...)` and `fail(...)` without ambiguity.
3. Use those constructors to widen native coverage across aggregate passing, `pick`, and `lift` end-to-end.
4. Keep broadening iterator and builtin coverage only where the LLVM/native path stays deterministic and the safety model remains explicit.

## Next Native And LLVM Targets

The most important expansions from here are:
- broader aggregate lowering for nested shapes/choices beyond the first indirect object layer, including a source-level aggregate construction story that remains ownership-safe
- deeper `lift` lowering beyond the initial concrete `Outcome` path, plus richer checked failure flow
- deeper iterator coverage beyond the current slice-like iterable subset, with native coverage now extended to loop control and Text-byte scanning
- broader builtin coverage beyond the current runtime print, safe external scalar, core `Text`, first aggregate object paths, and first loop/iterator paths
- raw / foreign memory and effect boundaries with stronger typed contracts
- broader native runtime coverage, packaging, and link behavior beyond the current validated subset
