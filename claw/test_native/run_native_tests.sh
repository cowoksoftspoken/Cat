#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CLAW_EXE="${CLAW_EXE:-$ROOT_DIR/build-ucrt64/claw.exe}"
WORK_DIR="$ROOT_DIR/build-native-tests"

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

normalize_text() {
  tr -d '\r'
}

run_case() {
  local input_path="$1"
  local label="$2"
  local expected_exit="$3"
  local expected_output="$4"
  local exe_path="$WORK_DIR/$label.exe"
  local out_path="$WORK_DIR/$label.out"

  echo "[build-native] $label"
  "$CLAW_EXE" build-native "$ROOT_DIR/$input_path" "$exe_path" > "$WORK_DIR/$label.build.log"

  if [[ ! -f "$exe_path" ]]; then
    echo "$label did not produce an executable" >&2
    exit 1
  fi

  set +e
  "$exe_path" > "$out_path"
  local status=$?
  set -e

  local actual_output
  actual_output="$(cat "$out_path" | normalize_text)"
  if [[ "$status" -ne "$expected_exit" ]]; then
    echo "$label exit code mismatch: expected $expected_exit, got $status" >&2
    echo "--- output ---" >&2
    printf '%s\n' "$actual_output" >&2
    exit 1
  fi

  if [[ "$actual_output" != "$expected_output" ]]; then
    echo "$label output mismatch" >&2
    echo "--- expected ---" >&2
    printf '%s\n' "$expected_output" >&2
    echo "--- actual ---" >&2
    printf '%s\n' "$actual_output" >&2
    exit 1
  fi
}

run_case "test_native/hello_runtime" "hello_runtime" 0 $'hello\n7'
run_case "test_native/unit_main" "unit_main" 0 $'unit'
run_case "test_native/import_program" "import_program" 0 $'7'

echo "all native integration tests passed"
