# Roadmap Next

## Immediate Technical Order

1. Widen native executable coverage on top of the current LLVM/LIR contract.
2. Lower richer aggregate/object cases and deeper `lift` / `pick` paths into LLVM IR now that scan/loop control, aggregate ABI, and the first native executable path are live.
3. Keep whole-project lowering deterministic while tightening ownership edge cases that appear during backend and native integration.
4. Expand typed raw / FFI contracts beyond function signatures into memory operations, effects, and pointer-oriented boundaries.
5. Add concurrency-oriented safety layers and only then move into optimization work.

## Current Native Status

The first native Windows executable pipeline is now alive because:
- `build-native` produces `.exe` files for the current supported subset
- the compiler emits a public native `@main` wrapper over the validated Claw entry symbol
- the bundled runtime currently covers the runtime print/println subset used by native smoke tests
- the native path is regression-tested separately in `test_native/`
- frontend, backend IR snapshots, and native executable checks are now all green together

## Next Native And LLVM Targets

The most important expansions from here are:
- broader aggregate lowering for nested shapes/choices beyond the first indirect object layer
- deeper `lift` lowering beyond the initial concrete `Outcome` path, plus richer checked failure flow
- deeper iterator coverage beyond the current slice-like iterable subset
- broader builtin coverage beyond the current runtime print, safe external scalar, core `Text`, first aggregate object paths, and first loop/iterator paths
- raw / foreign memory and effect boundaries with stronger typed contracts
- broader native runtime coverage, packaging, and link behavior beyond the current validated subset
