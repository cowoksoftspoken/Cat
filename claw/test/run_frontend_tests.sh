#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CLAW_EXE="${CLAW_EXE:-$ROOT_DIR/build-ucrt64/claw.exe}"

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  echo "build it first, for example:" >&2
  echo "  cmake -S . -B build-ucrt64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe" >&2
  echo "  cmake --build build-ucrt64 -j" >&2
  exit 1
fi

normalize_text() {
  tr -d '\r'
}

pass_cases=(
  "test/syntax.cat"
  "test/types.cat"
  "test/control_flow.cat"
  "test/scope_isolation.cat"
  "test/imports.cat"
  "test/prelude_prints.cat"
  "test/generic_instantiation.cat"
  "test/pkg_demo/modules.cat"
)

for case_file in "${pass_cases[@]}"; do
  echo "[check/pass] $case_file"
  "$CLAW_EXE" check "$ROOT_DIR/$case_file"
done

echo "[build/pass] test/pkg_demo/modules.cat"
"$CLAW_EXE" build "$ROOT_DIR/test/pkg_demo/modules.cat"

echo "[check/fail] test/ownership.cat"
ownership_output=""
if ownership_output="$($CLAW_EXE check "$ROOT_DIR/test/ownership.cat" 2>&1)"; then
  echo "expected ownership.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$ownership_output" != *"error[ownership]: Use of moved value -> message"* ]] ||
   [[ "$ownership_output" != *" --> "* ]] ||
   [[ "$ownership_output" != *"^^^^^^^"* ]]; then
  echo "ownership.cat failed, but not with the expected diagnostic output" >&2
  printf '%s\n' "$ownership_output" >&2
  exit 1
fi

echo "[check/fail] test/parse_diagnostics.cat"
parse_output=""
if parse_output="$($CLAW_EXE check "$ROOT_DIR/test/parse_diagnostics.cat" 2>&1)"; then
  echo "expected parse_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$parse_output" != *"error[parse]: Expected ')' after parameters"* ]] ||
   [[ "$parse_output" != *" --> "* ]] ||
   [[ "$parse_output" != *"fn broken(value: Int32 -> Int32 {"* ]] ||
   [[ "$parse_output" != *"^^"* ]]; then
  echo "parse_diagnostics.cat did not contain the expected parser diagnostic" >&2
  printf '%s\n' "$parse_output" >&2
  exit 1
fi

echo "[check/fail] test/parse_recovery.cat"
parse_recovery_output=""
if parse_recovery_output="$($CLAW_EXE check "$ROOT_DIR/test/parse_recovery.cat" 2>&1)"; then
  echo "expected parse_recovery.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$parse_recovery_output" != *"error[parse]: Expected expression."* ]] ||
   [[ "$parse_recovery_output" != *"error[parse]: Expected ':' after field name"* ]]; then
  echo "parse_recovery.cat did not contain the expected recovery diagnostics" >&2
  printf '%s\n' "$parse_recovery_output" >&2
  exit 1
fi
parse_recovery_count="$(printf '%s' "$parse_recovery_output" | grep -o 'error\[parse\]:' | wc -l | tr -d ' ')"
if [[ "$parse_recovery_count" -lt 3 ]]; then
  echo "parse_recovery.cat should report multiple parser diagnostics, got $parse_recovery_count" >&2
  printf '%s\n' "$parse_recovery_output" >&2
  exit 1
fi

echo "[check/fail] test/semantic_diagnostics.cat"
semantic_output=""
if semantic_output="$($CLAW_EXE check "$ROOT_DIR/test/semantic_diagnostics.cat" 2>&1)"; then
  echo "expected semantic_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$semantic_output" != *"error[semantic]: Undefined variable: missing"* ]] ||
   [[ "$semantic_output" != *" --> "* ]] ||
   [[ "$semantic_output" != *"print(missing)"* ]] ||
   [[ "$semantic_output" != *"^^^^^^^"* ]]; then
  echo "semantic_diagnostics.cat did not contain the expected semantic diagnostic" >&2
  printf '%s\n' "$semantic_output" >&2
  exit 1
fi

echo "[check/fail] test/type_arity_diagnostics.cat"
type_arity_output=""
if type_arity_output="$($CLAW_EXE check "$ROOT_DIR/test/type_arity_diagnostics.cat" 2>&1)"; then
  echo "expected type_arity_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$type_arity_output" != *"Type 'Box' expects 1 type argument(s), got 0."* ]] ||
   [[ "$type_arity_output" != *"Type 'Maybe' expects 1 type argument(s), got 2."* ]] ||
   [[ "$type_arity_output" != *"Type 'Text' expects 0 type argument(s), got 1."* ]]; then
  echo "type_arity_diagnostics.cat did not contain the expected arity diagnostics" >&2
  printf '%s\n' "$type_arity_output" >&2
  exit 1
fi

echo "[open-file/fail] test/does_not_exist.cat"
missing_output=""
if missing_output="$($CLAW_EXE check "$ROOT_DIR/test/does_not_exist.cat" 2>&1)"; then
  echo "expected does_not_exist.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$missing_output" != *"Failed to open source file."* ]] ||
   [[ "$missing_output" != *"requested path:"* ]] ||
   [[ "$missing_output" != *"current working directory:"* ]] ||
   [[ "$missing_output" != *"kind: missing"* ]]; then
  echo "missing-file diagnostics did not contain the expected detail" >&2
  printf '%s\n' "$missing_output" >&2
  exit 1
fi

echo "[emit-air] test/emit_air.cat"
air_output="$($CLAW_EXE emit-air "$ROOT_DIR/test/emit_air.cat" | normalize_text)"
expected_air_output="$(cat "$ROOT_DIR/test/emit_air.expect" | normalize_text)"
if [[ "$air_output" != "$expected_air_output" ]]; then
  echo "emit_air snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s\n' "$expected_air_output" >&2
  echo "--- actual ---" >&2
  printf '%s\n' "$air_output" >&2
  exit 1
fi

echo "[emit-oir] test/emit_air.cat"
oir_output="$($CLAW_EXE emit-oir "$ROOT_DIR/test/emit_air.cat" | normalize_text)"
expected_oir_output="$(cat "$ROOT_DIR/test/emit_oir.expect" | normalize_text)"
if [[ "$oir_output" != "$expected_oir_output" ]]; then
  echo "emit_oir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s\n' "$expected_oir_output" >&2
  echo "--- actual ---" >&2
  printf '%s\n' "$oir_output" >&2
  exit 1
fi

echo "all frontend tests passed"
