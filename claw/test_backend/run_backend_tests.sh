#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CLAW_EXE="${CLAW_EXE:-$ROOT_DIR/build-ucrt64/claw.exe}"
LLVM_AS="${LLVM_AS:-llvm-as}"
LLC="${LLC:-llc}"

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

normalize_text() {
  tr -d '\r'
}

check_emit_case() {
  local input_path="$1"
  local expect_path="$2"
  local label="$3"
  local tmp_ll
  local tmp_bc
  local tmp_obj
  tmp_ll="$(mktemp)"
  tmp_bc="$(mktemp)"
  tmp_obj="$(mktemp)"
  trap 'rm -f "$tmp_ll" "$tmp_bc" "$tmp_obj"' RETURN

  echo "[emit-llvm] $label"
  "$CLAW_EXE" emit-llvm "$ROOT_DIR/$input_path" > "$tmp_ll"

  local actual_output
  local expected_output
  actual_output="$(cat "$tmp_ll" | normalize_text)"
  expected_output="$(cat "$ROOT_DIR/$expect_path" | normalize_text)"
  if [[ "$actual_output" != "$expected_output" ]]; then
    echo "$label LLVM snapshot mismatch" >&2
    echo "--- expected ---" >&2
    printf '%s\n' "$expected_output" >&2
    echo "--- actual ---" >&2
    printf '%s\n' "$actual_output" >&2
    exit 1
  fi

  "$LLVM_AS" "$tmp_ll" -o "$tmp_bc"
  "$LLC" -filetype=obj "$tmp_bc" -o "$tmp_obj"
}

check_emit_case "test_backend/basic_call.cat" "test_backend/basic_call.expect" "basic_call.cat"
check_emit_case "test_backend/runtime_print.cat" "test_backend/runtime_print.expect" "runtime_print.cat"
check_emit_case "test_backend/branch_compare.cat" "test_backend/branch_compare.expect" "branch_compare.cat"
check_emit_case "test_backend/text_builtins.cat" "test_backend/text_builtins.expect" "text_builtins.cat"
check_emit_case "test_backend/choice_pick.cat" "test_backend/choice_pick.expect" "choice_pick.cat"
check_emit_case "test_backend/lift_outcome.cat" "test_backend/lift_outcome.expect" "lift_outcome.cat"
check_emit_case "test_backend/pkg_external_safe_scalar" "test_backend/pkg_external_safe_scalar/emit_llvm.expect" "pkg_external_safe_scalar"
check_emit_case "test_backend/pkg_entry_share_warning" "test_backend/pkg_entry_share_warning/emit_llvm.expect" "pkg_entry_share_warning"

echo "all llvm backend tests passed"
