#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
FRONTEND_FIXTURE_DIR="$ROOT_DIR/test_frontend"
NATIVE_FIXTURE_DIR="$ROOT_DIR/test_native"

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

ARTIFACT_DIR="$NATIVE_FIXTURE_DIR/artifacts"
mkdir -p "$ARTIFACT_DIR"

normalize_stdout() {
  "$1" | tr -d '\r'
}

expect_generated_ll() {
  local exe_path="$1"
  local ll_path="${exe_path%.exe}.ll"

  if [[ ! -f "$ll_path" ]]; then
    echo "expected LLVM IR next to built executable: $ll_path" >&2
    exit 1
  fi

  if ! grep -q 'define i32 @main()' "$ll_path"; then
    echo "generated LLVM IR did not expose the native @main entry wrapper: $ll_path" >&2
    exit 1
  fi
}

single_output="$ARTIFACT_DIR/revise_single_file.exe"
workspace_output="$ARTIFACT_DIR/revise_workspace.exe"
maybe_output="$ARTIFACT_DIR/revise_maybe.exe"
exit_code_output="$ARTIFACT_DIR/revise_exit_code.exe"
scope_output="$ARTIFACT_DIR/revise_scope_refs.exe"
anchor_output="$ARTIFACT_DIR/revise_anchor.exe"
anchor_choice_output="$ARTIFACT_DIR/revise_anchor_choice.exe"
view_shape_output="$ARTIFACT_DIR/revise_view_shape_scope.exe"

rm -f "$single_output" "${single_output%.exe}.ll" \
      "$workspace_output" "${workspace_output%.exe}.ll" \
      "$maybe_output" "${maybe_output%.exe}.ll" \
      "$exit_code_output" "${exit_code_output%.exe}.ll" \
      "$scope_output" "${scope_output%.exe}.ll" \
      "$anchor_output" "${anchor_output%.exe}.ll" \
      "$anchor_choice_output" "${anchor_choice_output%.exe}.ll" \
      "$view_shape_output" "${view_shape_output%.exe}.ll"

echo "[build/pass] test_native/revise_single_file.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_single_file.cat" "$single_output" >/dev/null
expect_generated_ll "$single_output"
single_stdout="$(normalize_stdout "$single_output")"
if [[ "$single_stdout" != "3" ]]; then
  echo "revised single-file native build produced unexpected output: $single_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_workspace"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_workspace" "$workspace_output" >/dev/null
expect_generated_ll "$workspace_output"
workspace_stdout="$(normalize_stdout "$workspace_output")"
if [[ "$workspace_stdout" != "36" ]]; then
  echo "revised workspace native build produced unexpected output: $workspace_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_maybe.cat"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_maybe.cat" "$maybe_output" >/dev/null
expect_generated_ll "$maybe_output"
maybe_stdout="$(normalize_stdout "$maybe_output")"
expected_maybe_stdout=$'42\n0\nC@\nworld'
if [[ "$maybe_stdout" != "$expected_maybe_stdout" ]]; then
  echo "revised Maybe native build produced unexpected output:" >&2
  printf '%s\n' "$maybe_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_scope_refs.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_scope_refs.cat" "$scope_output" >/dev/null
expect_generated_ll "$scope_output"
scope_stdout="$(normalize_stdout "$scope_output")"
if [[ "$scope_stdout" != "scope" ]]; then
  echo "revised scope-ref native build produced unexpected output: $scope_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_anchor.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_anchor.cat" "$anchor_output" >/dev/null
expect_generated_ll "$anchor_output"
anchor_stdout="$(normalize_stdout "$anchor_output")"
if [[ "$anchor_stdout" != "anchor" ]]; then
  echo "revised anchor native build produced unexpected output: $anchor_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_anchor_choice.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_anchor_choice.cat" "$anchor_choice_output" >/dev/null
expect_generated_ll "$anchor_choice_output"
anchor_choice_stdout="$(normalize_stdout "$anchor_choice_output")"
expected_anchor_choice_stdout=$'choice\nnone'
if [[ "$anchor_choice_stdout" != "$expected_anchor_choice_stdout" ]]; then
  echo "revised anchor-choice native build produced unexpected output:" >&2
  printf '%s\n' "$anchor_choice_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_view_shape_scope.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_view_shape_scope.cat" "$view_shape_output" >/dev/null
expect_generated_ll "$view_shape_output"
view_shape_stdout="$(normalize_stdout "$view_shape_output")"
if [[ "$view_shape_stdout" != "hello" ]]; then
  echo "revised view-shape native build produced unexpected output: $view_shape_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_exit_code.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_exit_code.cat" "$exit_code_output" >/dev/null
expect_generated_ll "$exit_code_output"
"$exit_code_output" >/dev/null 2>&1 || exit_code="$?"
exit_code="${exit_code:-0}"
if [[ "$exit_code" != "7" ]]; then
  echo "revised exit-code native build produced unexpected process exit code: $exit_code" >&2
  exit 1
fi
