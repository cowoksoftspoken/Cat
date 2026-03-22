# Roadmap Next

## Immediate Technical Order

1. Expand the first LLVM backend on top of the current OIR/LIR contract.
2. Lower canonical layouts, linkage, calls, drops, and richer `lift` / aggregate paths into LLVM IR now that `bounds_check`, the first concrete `pick` path, and initial concrete `Outcome`-based `lift` are live.
3. Keep whole-project lowering deterministic while tightening the remaining ownership edge cases that appear during backend integration.
4. Expand typed raw / FFI contracts beyond function signatures into memory operations, effects, and pointer-oriented boundaries.
5. Add concurrency-oriented safety layers and later optimizations after the first native pipeline is alive.

## LLVM Entry Status

The first LLVM backend pass is now alive because:
- ownership and initialization semantics are stable enough for lowering
- drop / destruction behavior is explicit and covered by regression tests
- typed external and raw boundaries are explicit enough to audit from source down to LIR
- OIR is a real IR model, not only a printed lowered view
- LIR is a real backend-facing IR model, not only a printer layered on OIR
- canonical type layout, ABI, and linkage rules are fixed enough for backend lowering
- frontend diagnostics are already strong enough to debug source-level failures before backend work
- `emit-llvm` now produces valid LLVM IR for the current backend subset and that subset is validated with a dedicated backend test suite

## Next LLVM Targets

The most important backend expansions from here are:
- richer aggregate lowering for shapes and choices beyond the current concrete `pick` subset
- deeper `lift` lowering beyond the initial concrete `Outcome` path, plus richer checked failure flow
- ABI-accurate aggregate passing and return lowering where indirect pass kinds matter
- broader builtin coverage beyond the current runtime print, safe external scalar, and core `Text` cases
- raw / foreign memory and effect boundaries with stronger typed contracts
- end-to-end native object and link flow on top of the emitted LLVM IR
