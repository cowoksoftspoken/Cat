#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$ROOT_DIR/build-ucrt64-clang/claw-codex.exe" ]]; then
    CLAW_EXE="$ROOT_DIR/build-ucrt64-clang/claw-codex.exe"
  else
    CLAW_EXE="$ROOT_DIR/build-ucrt64-clang/claw.exe"
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
   [[ "$revise_air_output" != *"scan item: Byte over greeting : Str"* ]] ||
   [[ "$revise_air_output" != *"expr println(sum("* ]]; then
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
if [[ "$bad_main_output" != *'`main` must take no parameters and return Unit implicitly or explicitly.'* ]]; then
  echo "revise_bad_main_return did not report the revised Unit entry rule" >&2
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
