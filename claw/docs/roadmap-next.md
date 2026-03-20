# Roadmap Next

## Immediate Technical Order

1. Make whole-project OIR and LIR lowering deterministic.
2. Strengthen the remaining ownership and raw-boundary edge cases.
3. Expand builtin method coverage only where dispatch stays compile-time, borrow soundness remains intact, and cost remains explicit.
4. Enrich LIR so bounds checks, `lift`, `scan`, foreign calls, and runtime hooks are explicit enough for LLVM lowering.
5. Start LLVM only after the frontend ownership model and LIR contracts are stable enough.

## Definition Of Ready To LLVM

Claw is ready to move into LLVM only after:
- ownership and initialization semantics are stable
- drop / destruction behavior is explicit and covered by regression tests
- OIR is a real IR model, not only a printed lowered view
- LIR is a real backend-facing IR model, not only a printer layered on OIR
- whole-project lowering is deterministic
- frontend diagnostics are already strong enough to debug source-level failures before backend work
