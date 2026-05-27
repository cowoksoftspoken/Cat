#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
FRONTEND_FIXTURE_DIR="$ROOT_DIR/test_frontend"
BACKEND_FIXTURE_DIR="$ROOT_DIR/test_backend"

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

ARTIFACT_DIR="$BACKEND_FIXTURE_DIR/artifacts"
mkdir -p "$ARTIFACT_DIR"

run_llvm() {
  local label="$1"
  local input="$2"
  local output="$3"
  echo "[llvm/pass] $label"
  "$CLAW_EXE" llvm "$input" > "$output"
  llvm-as "$output" -o /dev/null
}

result_ll="$ARTIFACT_DIR/revise_result_llvm.ll"
maybe_ll="$ARTIFACT_DIR/revise_maybe.ll"
scope_ll="$ARTIFACT_DIR/revise_scope_refs.ll"
anchor_ll="$ARTIFACT_DIR/revise_anchor.ll"
anchor_choice_ll="$ARTIFACT_DIR/revise_anchor_choice.ll"
view_shape_ll="$ARTIFACT_DIR/revise_view_shape_scope.ll"
rm -f "$result_ll" "$maybe_ll" "$scope_ll" "$anchor_ll" "$anchor_choice_ll" "$view_shape_ll"

run_llvm "test_backend/revise_result_llvm.cat" "$BACKEND_FIXTURE_DIR/revise_result_llvm.cat" "$result_ll"
llvm_output="$(tr -d '\r' < "$result_ll")"
if [[ "$llvm_output" != *'define internal void @"revise_result_llvm::step"'* ]] ||
   [[ "$llvm_output" != *'define internal void @"revise_result_llvm::show"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.i32"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.slice"'* ]] ||
   [[ "$llvm_output" != *'try_fail_0:'* ]]; then
  echo "revised backend LLVM output did not include the expected Result/try lowering markers" >&2
  exit 1
fi

run_llvm "test_frontend/revise_maybe.cat" "$FRONTEND_FIXTURE_DIR/revise_maybe.cat" "$maybe_ll"

run_llvm "test_backend/revise_scope_refs.cat" "$BACKEND_FIXTURE_DIR/revise_scope_refs.cat" "$scope_ll"
scope_output="$(tr -d '\r' < "$scope_ll")"
if [[ "$scope_output" != *'define internal void @"revise_scope_refs::main"'* ]] ||
   [[ "$scope_output" != *'@"claw.runtime.println.slice"'* ]] ||
   [[ "$scope_output" != *'scope_s_0:'* ]]; then
  echo "revised scope-ref LLVM output did not include the expected lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_anchor.cat" "$BACKEND_FIXTURE_DIR/revise_anchor.cat" "$anchor_ll"
anchor_output="$(tr -d '\r' < "$anchor_ll")"
if [[ "$anchor_output" != *'@"claw.runtime.anchor.alloc"'* ]] ||
   [[ "$anchor_output" != *'@"claw.runtime.anchor.free"'* ]] ||
   [[ "$anchor_output" != *'@"claw.runtime.println.slice"'* ]]; then
  echo "revised anchor LLVM output did not include the expected anchor lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_anchor_choice.cat" "$BACKEND_FIXTURE_DIR/revise_anchor_choice.cat" "$anchor_choice_ll"
anchor_choice_output="$(tr -d '\r' < "$anchor_choice_ll")"
if [[ "$anchor_choice_output" != *'@"claw.runtime.anchor.alloc"'* ]] ||
   [[ "$anchor_choice_output" != *'@"claw.runtime.anchor.free"'* ]] ||
   [[ "$anchor_choice_output" != *'switch i32'* ]]; then
  echo "revised anchor-choice LLVM output did not include the expected choice + anchor lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_view_shape_scope.cat" "$BACKEND_FIXTURE_DIR/revise_view_shape_scope.cat" "$view_shape_ll"
view_shape_output="$(tr -d '\r' < "$view_shape_ll")"
if [[ "$view_shape_output" != *'define internal void @"revise_view_shape_scope::main"'* ]] ||
   [[ "$view_shape_output" != *'@"claw.runtime.println.slice"'* ]]; then
  echo "revised view-shape LLVM output did not include the expected lowering markers" >&2
  exit 1
fi
