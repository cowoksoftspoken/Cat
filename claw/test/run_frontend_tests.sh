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
  echo "revise_surface AIR did not reflect the revised val/var + module surface" >&2
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

echo "[check/pass] test/revise_span_borrow.cat"
"$CLAW_EXE" check "$ROOT_DIR/test/revise_span_borrow.cat"

echo "[check/fail] test/revise_bad_vec_index_borrow.cat"
if bad_vec_index_borrow_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_vec_index_borrow.cat" 2>&1)"; then
  echo "expected revise_bad_vec_index_borrow to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_vec_index_borrow_output" != *"Direct element borrows from Vec are forbidden."* ]]; then
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
echo "[check/fail] legacy hold"
if legacy_hold_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_hold.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_hold to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_hold_output" != *"Legacy binding syntax 'hold' has been removed."* ]]; then
  echo "revise_bad_legacy_hold did not report the hold -> val migration" >&2
  exit 1
fi

echo "[check/fail] legacy slot"
if legacy_slot_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_slot.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_slot to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_slot_output" != *"Legacy binding syntax 'slot' has been removed."* ]]; then
  echo "revise_bad_legacy_slot did not report the slot -> var migration" >&2
  exit 1
fi

echo "[check/fail] legacy give"
if legacy_give_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_give.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_give to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_give_output" != *"Legacy return syntax 'give' has been removed."* ]]; then
  echo "revise_bad_legacy_give did not report the give -> return migration" >&2
  exit 1
fi

echo "[check/fail] legacy look"
if legacy_look_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_look.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_look to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_look_output" != *"Legacy borrow syntax 'look' has been removed."* ]]; then
  echo "revise_bad_legacy_look did not report the look -> ref migration" >&2
  exit 1
fi

echo "[check/fail] legacy edit"
if legacy_edit_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_edit.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_edit to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_edit_output" != *"Legacy borrow syntax 'edit' has been removed."* ]]; then
  echo "revise_bad_legacy_edit did not report the edit -> ref mut migration" >&2
  exit 1
fi

echo "[check/fail] legacy when"
if legacy_when_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_when.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_when to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_when_output" != *"Legacy conditional syntax 'when' has been removed."* ]]; then
  echo "revise_bad_legacy_when did not report the when -> if migration" >&2
  exit 1
fi

echo "[check/fail] legacy otherwise"
if legacy_otherwise_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_otherwise.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_otherwise to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_otherwise_output" != *"Legacy branch syntax 'otherwise' has been removed."* ]]; then
  echo "revise_bad_legacy_otherwise did not report the otherwise -> else migration" >&2
  exit 1
fi

echo "[check/fail] legacy realm"
if legacy_realm_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_realm.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_realm to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_realm_output" != *"Legacy module syntax 'realm' has been removed."* ]]; then
  echo "revise_bad_legacy_realm did not report the realm removal" >&2
  exit 1
fi

echo "[check/fail] legacy of"
if legacy_of_output="$("$CLAW_EXE" check "$ROOT_DIR/test/revise_bad_legacy_of.cat" 2>&1)"; then
  echo "expected revise_bad_legacy_of to fail, but it passed" >&2
  exit 1
fi
if [[ "$legacy_of_output" != *"Legacy generic syntax 'of' has been removed."* ]]; then
  echo "revise_bad_legacy_of did not report the of -> [] migration" >&2
  exit 1
fi




