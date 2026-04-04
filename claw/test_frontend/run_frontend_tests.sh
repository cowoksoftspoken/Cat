#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$ROOT_DIR/build-ucrt64-clang/claw.exe" ]]; then
    CLAW_EXE="$ROOT_DIR/build-ucrt64-clang/claw.exe"
  else
    CLAW_EXE="$ROOT_DIR/build-ucrt64-clang/claw-codex.exe"
  fi
fi
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

echo "[check/pass] test/revise_surface.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_surface.cat"

echo "[air/pass] test/revise_surface.cat"
revise_air_output="$("$CLAW_EXE" air "$ROOT_DIR/test/revise_surface.cat")"
if [[ "$revise_air_output" != *"air.module revise_surface"* ]] ||
   [[ "$revise_air_output" != *"val greeting: Str"* ]] ||
   [[ "$revise_air_output" != *"var total: Int64 = 42 : Int64"* ]] ||
   [[ "$revise_air_output" != *"scan item: UInt8 over greeting : Str"* ]] ||
   [[ "$revise_air_output" != *"return println(sum("* ]]; then
  echo "revise_surface AIR did not reflect the revised surface" >&2
  exit 1
fi

echo "[validate/pass] test/revise_pkg"
"$CLAW_EXE" validate "$ROOT_DIR/test/revise_pkg"

echo "[check/pass] test/revise_pkg"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_pkg"

echo "[validate/fail] test/revise_bad_main_return"
if bad_main_output="$("$CLAW_EXE" validate "$ROOT_DIR/test/revise_bad_main_return" 2>&1)"; then
  echo "expected revise_bad_main_return to fail validation, but it passed" >&2
  exit 1
fi
if [[ "$bad_main_output" != *'`main` must take no parameters and return Unit or Int32.'* ]]; then
  echo "revise_bad_main_return did not report the revised entry return rule" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_double_main.cat"
if bad_double_main_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_double_main.cat" 2>&1)"; then
  echo "expected revise_bad_double_main to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_double_main_output" != *'main.cat must contain exactly one `fn main` declaration.'* ]]; then
  echo "revise_bad_double_main did not report duplicate entry main declarations" >&2
  exit 1
fi

echo "[validate/fail] test/revise_bad_non_entry_main"
if bad_non_entry_main_output="$("$CLAW_EXE" validate "$ROOT_DIR/test/revise_bad_non_entry_main" 2>&1)"; then
  echo "expected revise_bad_non_entry_main to fail validation, but it passed" >&2
  exit 1
fi
if [[ "$bad_non_entry_main_output" != *'`fn main` is only allowed in root main.cat.'* ]]; then
  echo "revise_bad_non_entry_main did not report non-entry main declarations" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_call_main.cat"
if bad_call_main_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_call_main.cat" 2>&1)"; then
  echo "expected revise_bad_call_main to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_call_main_output" != *'`main` is the program entry point and cannot be called like a normal function.'* ]]; then
  echo "revise_bad_call_main did not report direct calls to main" >&2
  exit 1
fi

echo "[check/pass] test/revise_error_handling.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_error_handling.cat"

echo "[air/pass] test/revise_error_handling.cat"
revise_try_air_output="$("$CLAW_EXE" air "$ROOT_DIR/test/revise_error_handling.cat")"
if [[ "$revise_try_air_output" != *"val value = try step("* ]] ||
   [[ "$revise_try_air_output" != *"else err"* ]] ||
   [[ "$revise_try_air_output" != *"return Ok("* ]]; then
  echo "revise_error_handling AIR did not reflect the revised try/Result surface" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_try_return.cat"
if bad_try_return_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_try_return.cat" 2>&1)"; then
  echo "expected revise_bad_try_return to fail checking, but it passed" >&2
  exit 1
fi
if [[ "$bad_try_return_output" != *'`try` shorthand requires the current function to return Result[T, E].'* ]]; then
  echo "revise_bad_try_return did not report the revised try shorthand return rule" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_try_var.cat"
if bad_try_var_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_try_var.cat" 2>&1)"; then
  echo "expected revise_bad_try_var to fail checking, but it passed" >&2
  exit 1
fi
if [[ "$bad_try_var_output" != *'`try` bindings currently require `val`.'* ]]; then
  echo "revise_bad_try_var did not report the current val-only try binding rule" >&2
  exit 1
fi

echo "[check/pass] test/revise_maybe.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_maybe.cat"

echo "[air/pass] test/revise_maybe.cat"
revise_maybe_air_output="$("$CLAW_EXE" air "$ROOT_DIR/test/revise_maybe.cat")"
if [[ "$revise_maybe_air_output" != *"choice Maybe"* ]] ||
   [[ "$revise_maybe_air_output" != *"Some("* ]] ||
   [[ "$revise_maybe_air_output" != *"None"* ]] ||
   [[ "$revise_maybe_air_output" != *"pick"* ]]; then
  echo "revise_maybe AIR did not reflect the Maybe[T] choice surface" >&2
  exit 1
fi

echo "[check/pass] test/revise_anchor.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_anchor.cat"

echo "[check/fail] test/revise_bad_anchor_view_payload.cat"
if bad_anchor_payload_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_anchor_view_payload.cat" 2>&1)"; then
  echo "expected revise_bad_anchor_view_payload to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_anchor_payload_output" != *'Anchor[T] requires an owned payload'* ]]; then
  echo "revise_bad_anchor_view_payload did not report borrowed Anchor payload rejection" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_anchor_return_local.cat"
if bad_anchor_return_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_anchor_return_local.cat" 2>&1)"; then
  echo "expected revise_bad_anchor_return_local to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_anchor_return_output" != *"Returned ref value must come from one of the function's ref parameters."* ]]; then
  echo "revise_bad_anchor_return_local did not report local Anchor ref escape" >&2
  exit 1
fi

echo "[check/pass] test/revise_span_borrow.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_span_borrow.cat"

echo "[check/fail] test/revise_bad_vec_index_borrow.cat"
if bad_vec_index_borrow_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_vec_index_borrow.cat" 2>&1)"; then
  echo "expected revise_bad_vec_index_borrow to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_vec_index_borrow_output" != *'Direct element borrows from Vec are forbidden.'* ]]; then
  echo "revise_bad_vec_index_borrow did not report the Vec -> Span borrow rule" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_vec_mutate_while_span.cat"
if bad_vec_span_mut_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_vec_mutate_while_span.cat" 2>&1)"; then
  echo "expected revise_bad_vec_mutate_while_span to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_vec_span_mut_output" != *'Cannot create ref mut view of `data` while `data` is still borrowed.'* ]]; then
  echo "revise_bad_vec_mutate_while_span did not report the active Span borrow conflict" >&2
  exit 1
fi

echo "[check/pass] test/revise_field_borrow_paths.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_field_borrow_paths.cat"

echo "[check/fail] test/revise_bad_field_prefix_borrow.cat"
if bad_field_prefix_borrow_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_field_prefix_borrow.cat" 2>&1)"; then
  echo "expected revise_bad_field_prefix_borrow to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_field_prefix_borrow_output" != *'Cannot create ref mut view of `local.db.port` while `local.db` is still borrowed.'* ]]; then
  echo "revise_bad_field_prefix_borrow did not report the field-prefix borrow conflict" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_field_prefix_mutation.cat"
if bad_field_prefix_mutation_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_field_prefix_mutation.cat" 2>&1)"; then
  echo "expected revise_bad_field_prefix_mutation to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_field_prefix_mutation_output" != *'Cannot create ref mut view of `local.db` while `local.db.host` is still borrowed.'* ]]; then
  echo "revise_bad_field_prefix_mutation did not report the field-prefix parent borrow conflict" >&2
  exit 1
fi

echo "[check/pass] test/revise_param_borrow_paths.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_param_borrow_paths.cat"

echo "[check/fail] test/revise_bad_param_prefix_borrow.cat"
if bad_param_prefix_borrow_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_param_prefix_borrow.cat" 2>&1)"; then
  echo "expected revise_bad_param_prefix_borrow to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_param_prefix_borrow_output" != *'Cannot create ref mut view of `cfg.db.port` while `cfg.db` is still borrowed.'* ]]; then
  echo "revise_bad_param_prefix_borrow did not report the parameter field-prefix borrow conflict" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_param_prefix_mutation.cat"
if bad_param_prefix_mutation_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_param_prefix_mutation.cat" 2>&1)"; then
  echo "expected revise_bad_param_prefix_mutation to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_param_prefix_mutation_output" != *'Cannot create ref mut view of `cfg.db` while `cfg.db.host` is still borrowed.'* ]]; then
  echo "revise_bad_param_prefix_mutation did not report the parameter parent borrow conflict" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_shape_ref_field.cat"
if bad_shape_ref_field_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_shape_ref_field.cat" 2>&1)"; then
  echo "expected revise_bad_shape_ref_field to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_shape_ref_field_output" != *"Shape 'Cache' cannot store ref Str directly in field 'stored'."* ]]; then
  echo "revise_bad_shape_ref_field did not report the direct ref field escape rule" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_choice_ref_payload.cat"
if bad_choice_ref_payload_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_choice_ref_payload.cat" 2>&1)"; then
  echo "expected revise_bad_choice_ref_payload to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_choice_ref_payload_output" != *"Choice 'MaybeRef' cannot store"* ]] ||
   [[ "$bad_choice_ref_payload_output" != *"variant 'Some'"* ]]; then
  echo "revise_bad_choice_ref_payload did not report the direct ref payload escape rule" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_return_local_ref.cat"
if bad_return_local_ref_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_return_local_ref.cat" 2>&1)"; then
  echo "expected revise_bad_return_local_ref to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_return_local_ref_output" != *"Returned ref value must come from one of the function's ref parameters."* ]]; then
  echo "revise_bad_return_local_ref did not report the local ref escape rule" >&2
  exit 1
fi

echo "[check/pass] test/revise_scope_refs.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_scope_refs.cat"

echo "[air/pass] test/revise_scope_refs.cat"
revise_scope_air_output="$("$CLAW_EXE" air "$ROOT_DIR/test/revise_scope_refs.cat")"
if [[ "$revise_scope_air_output" != *"scope s"* ]] ||
   [[ "$revise_scope_air_output" != *"val a: ref[s] Str = ref[s] first"* ]] ||
   [[ "$revise_scope_air_output" != *"val again: ref[s] Str = identity("* ]]; then
  echo "revise_scope_refs AIR did not reflect the scoped-ref surface" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_scope_return.cat"
if bad_scope_return_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_scope_return.cat" 2>&1)"; then
  echo "expected revise_bad_scope_return to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_scope_return_output" != *'Scoped ref `ref[s] Str` cannot leave `scope s`.'* ]]; then
  echo "revise_bad_scope_return did not report scope-bound return escape" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_scope_escape_assign.cat"
if bad_scope_assign_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_scope_escape_assign.cat" 2>&1)"; then
  echo "expected revise_bad_scope_escape_assign to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_scope_assign_output" != *'Scoped ref `ref[s] Str` cannot escape into `saved`.'* ]]; then
  echo "revise_bad_scope_escape_assign did not report assignment escape from scope" >&2
  exit 1
fi

echo "[check/fail] test/revise_bad_scope_source_lifetime.cat"
if bad_scope_lifetime_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_scope_source_lifetime.cat" 2>&1)"; then
  echo "expected revise_bad_scope_source_lifetime to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_scope_lifetime_output" != *'Source value for `ref[s] Str` does not live long enough for scope `s`.'* ]]; then
  echo "revise_bad_scope_source_lifetime did not report the scoped source lifetime rule" >&2
  exit 1
fi

echo "[check/pass] test/revise_anchor_choice.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_anchor_choice.cat"
