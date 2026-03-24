# Frontend Test Corpus

Current corpus files are plain source and workspace fixtures intended for the compiler driver.

Recommended checks:
- `claw check test\syntax.cat`
- `claw check test\types.cat`
- `claw check test\control_flow.cat`
- `claw check test\scope_isolation.cat`
- `claw check test\prelude_prints.cat`
- `claw check test\generic_instantiation.cat`
- `claw check test\definite_init.cat`
  This file is expected to pass and locks in definite initialization after assignment and reinitialization after move.
- `claw check test\view_safety.cat`
  This file is expected to pass and locks in lexical view scope release plus safe passthrough view returns.
- `claw check test\float_literals.cat`
  This file is expected to pass and locks in contextual float literal fitting, exponent forms, and explicit `_Float32` / `_Float64` suffixes.
- `claw check test\target_size_types.cat`
  This file is expected to pass and locks in the frontend's stable `native64` rules for `USize` and `ISize`, independent of the host compiler width.
- `claw check test\method_dispatch.cat`
  This file is expected to pass and locks in static builtin method dispatch on core types like `Text`, `Bytes`, `Vec of T`, `Table of K, V`, `Set of T`, and `Ring of T`, including view-returning helpers such as `slice`.
- `claw check test\layout_ir.cat`
  This file is expected to pass and locks in canonical shape / choice layout metadata, ABI pass classification, and symbol linkage emission through OIR and LIR.
- `claw check test\ownership.cat`
  This file is expected to fail ownership checking because it moves the same `Text` twice.
- `claw check test\view_borrow_ownership.cat`
  This file is expected to fail ownership checking because a lexical `look` borrow blocks both moving and `edit` reborrowing of the owner.
- `claw check test\method_view_borrow_ownership.cat`
  This file is expected to fail ownership checking because a borrow returned from a builtin method such as `slice` still pins the original owner.
- `claw check test\parse_diagnostics.cat`
  This file is expected to fail in the parser and should show `file:line:column` plus a caret highlight.
- `claw check test\parse_recovery.cat`
  This file is expected to fail with multiple parser diagnostics in a single run.
- `claw check test\semantic_diagnostics.cat`
  This file is expected to fail in semantic analysis and should show `file:line:column` plus a caret highlight.
- `claw check test\definite_init_semantic.cat`
  This file is expected to fail because immutable `hold` bindings must be initialized at the binding site.
- `claw check test\definite_init_diagnostics.cat`
  This file is expected to fail for reads that are not definitely initialized across control flow.
- `claw check test\safety_diagnostics.cat`
  This file is expected to fail semantic safety validation for escaping views, `edit` view returns, and raw-address usage outside `raw`.
- `claw check test\type_arity_diagnostics.cat`
  This file is expected to fail semantic validation for missing and extra generic type arguments.
- `claw check test\float_literal_diagnostics.cat`
  This file is expected to fail with explicit suffix, exponent underflow, and numeric-fitting diagnostics for float literals.
- `claw check test\target_size_diagnostics.cat`
  This file is expected to fail when integer literals exceed the frontend's stable `USize` / `ISize` range.
- `claw check test\method_dispatch_diagnostics.cat`
  This file is expected to fail with missing-method, receiver-mismatch, and argument diagnostics for builtin method dispatch, including mutable-only methods such as `clear`, `reserve`, `truncate`, and `shrink_to_fit`.

Structured workspace fixtures:
- `claw check test\pkg_demo`
- `claw build test\pkg_demo`
- `claw build test\pkg_demo\claw.toml`
- Covers root `main.cat`, workspace config parsing, dependency-backed external imports, folder-level `modules.cat`, explicit `raw` wrapping for opaque external calls, and whole-project OIR.
- `claw check test\pkg_rootless_demo`
- `claw build test\pkg_rootless_demo`
- Proves the root workspace does not need `modules.cat`; only non-root folders use it.
- `claw check test\pkg_typed_external_safe`
- `claw build test\pkg_typed_external_safe`
- Proves that `claw.toml` typed dependency contracts can introduce safe external functions that participate in ordinary typed value flow.
- `claw check test\pkg_foreign_safe_scalar`
- `claw build test\pkg_foreign_safe_scalar`
- Proves that safe foreign-ABI contracts still work when every boundary type is FFI-stable.
- `claw check test\pkg_bad_import`
- This fixture is expected to fail and proves that shared function imports across files keep their real types.
- `claw check test\pkg_opaque_external`
- This fixture is expected to fail and proves that dependency-backed external calls without shared signatures are statement-only, not general-purpose values.
- `claw check test\pkg_opaque_external_raw`
- This fixture is expected to fail and proves that opaque external calls require an explicit `raw` block even when they are only used as statements.
- `claw check test\pkg_typed_external_raw`
- This fixture is expected to fail and proves that typed dependency contracts declared as `raw` still require an explicit `raw` block.
- `claw check test\layout_cycle_diagnostics.cat`
- This file is expected to fail and proves that recursive owned layouts are rejected unless indirection is introduced explicitly.
- `claw check test\pkg_foreign_safe_bad`
- This fixture is expected to fail and proves that non-`claw` safe external contracts reject boundary types that are not FFI-stable.
- `claw check test\pkg_missing_dep`
- This fixture is expected to fail because an external dependency root is not declared in `[dependencies]`.
- `claw build test\pkg_bad_config`
- This fixture is expected to fail because root `main.cat` is the fixed entry and config may not redeclare it.

Driver diagnostics:
- `claw check test\does_not_exist.cat`
- The error should include the requested path, current working directory, and per-path status details.

IR snapshots:
- `claw air test\emit_air.cat`
- Compare the output with `test\emit_air.expect`
- `claw air test\float_emit_air.cat`
- Compare the output with `test\float_emit_air.expect` to validate contextual float literal typing and exponent parsing.
- `claw air test\method_dispatch_air.cat`
- Compare the output with `test\method_dispatch_air.expect` to validate builtin method dispatch typing, view-return methods, and mutable container helpers.
- `claw oir test\emit_air.cat`
- Compare the output with `test\emit_oir.expect`
- `claw oir test\pkg_demo`
- Compare the output with `test\pkg_demo\emit_oir.expect` to validate project-level OIR emission.
- `claw oir test\pkg_typed_external_safe`
- Compare the output with `test\pkg_typed_external_safe\emit_oir.expect` to validate typed external contract metadata in OIR.
- `claw oir test\layout_ir.cat`
- Compare the output with `test\layout_oir.expect` to validate canonical layout, ABI pass, and linkage metadata on concrete shapes and choices.
- `claw oir test\pkg_foreign_safe_scalar`
- Compare the output with `test\pkg_foreign_safe_scalar\emit_oir.expect` to validate safe foreign-ABI contract metadata in OIR.
- `claw oir test\raw_regions.cat`
- Compare the output with `test\raw_regions_oir.expect` to validate explicit raw-region tagging in OIR.
- `claw lir test\emit_air.cat`
- Compare the output with `test\emit_lir.expect` to validate the backend-facing LIR surface for a single realm.
- `claw lir test\drop_schedule.cat`
- Compare the output with `test\drop_schedule_lir.expect` to validate stack-object lowering, phi-like joins, and explicit loop-break targets.
- `claw lir test\raw_regions.cat`
- Compare the output with `test\raw_regions_lir.expect` to validate raw-region propagation through blocks and foreign/runtime boundary visibility.
- `claw lir test\lir_edges.cat`
- Compare the output with `test\lir_edges.expect` to validate explicit bounds checks, `scan` iteration edges, and `lift` success/fail lowering.
- `claw lir test\pkg_demo`
- Compare the output with `test\pkg_demo\emit_lir.expect` to validate whole-project LIR emission, call-kind classification, explicit raw-only opaque-external boundaries, unsafe-boundary tagging, and defect blocks for choice dispatch.
- `claw lir test\pkg_typed_external_safe`
- Compare the output with `test\pkg_typed_external_safe\emit_lir.expect` to validate safe typed external calls with explicit ABI, dependency-root, and symbol metadata.
- `claw lir test\layout_ir.cat`
- Compare the output with `test\layout_lir.expect` to validate backend-facing layout, ABI pass, and linkage metadata on concrete shapes and choices.
- `claw lir test\pkg_foreign_safe_scalar`
- Compare the output with `test\pkg_foreign_safe_scalar\emit_lir.expect` to validate safe foreign-ABI calls with explicit FFI-safe metadata in LIR.

MSYS2 UCRT64 runner:
- `bash test/run_frontend_tests.sh`
- Override executable path if needed with `CLAW_EXE=/path/to/claw.exe bash test/run_frontend_tests.sh`

Coverage focus:
- Surface syntax and declarations
- Parser recovery with multiple diagnostics per file
- Type and shape / choice validation
- Generic instantiation in field access and `pick`
- Generic type arity checking
- Scope isolation
- Ownership moves on owned values
- Definite initialization after assignment, move, and control-flow join
- Lexical view borrows, safe view returns, method-returned borrows, and raw fences
- Structured parser, semantic, and ownership diagnostics
- Prelude output builtins `print` / `println`
- Clear missing-file diagnostics
- Typed cross-unit imports for functions, shapes, and choices
- Stable `native64` size-type fitting in the frontend
- Workspace config parsing and dependency-gated external import roots
- Typed AIR emission baseline
- Whole-project OIR emission backed by a structured IR model
- Whole-project LIR emission backed by an explicit backend-facing IR model
- Canonical layout, ABI pass, and linkage emission for backend lowering
- Foreign-ABI safe contract validation against FFI-stable boundary types


Backend regression lives separately:
- `bash test_backend/run_backend_tests.sh`
- This keeps LLVM/backend bring-up fixtures out of the frontend corpus.
