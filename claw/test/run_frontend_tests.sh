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
  "test/view_safety.cat"
  "test/pkg_demo"
  "test/pkg_rootless_demo"
)

for case_file in "${pass_cases[@]}"; do
  echo "[check/pass] $case_file"
  "$CLAW_EXE" check "$ROOT_DIR/$case_file"
done

echo "[build/pass] test/pkg_demo"
"$CLAW_EXE" build "$ROOT_DIR/test/pkg_demo"

echo "[build/pass] test/pkg_demo/claw.toml"
"$CLAW_EXE" build "$ROOT_DIR/test/pkg_demo/claw.toml"

echo "[build/pass] test/pkg_rootless_demo"
"$CLAW_EXE" build "$ROOT_DIR/test/pkg_rootless_demo"

echo "[build/pass] test/pkg_rootless_demo/claw.toml"
"$CLAW_EXE" build "$ROOT_DIR/test/pkg_rootless_demo/claw.toml"

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

echo "[check/fail] test/view_borrow_ownership.cat"
view_borrow_output=""
if view_borrow_output="$($CLAW_EXE check "$ROOT_DIR/test/view_borrow_ownership.cat" 2>&1)"; then
  echo "expected view_borrow_ownership.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$view_borrow_output" != *"error[ownership]: Cannot move value while it is borrowed -> message"* ]] ||
   [[ "$view_borrow_output" != *"error[ownership]: Cannot create edit view while another view is active -> message"* ]] ||
   [[ "$view_borrow_output" != *" --> "* ]]; then
  echo "view_borrow_ownership.cat failed, but not with the expected ownership diagnostics" >&2
  printf '%s\n' "$view_borrow_output" >&2
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

echo "[check/fail] test/safety_diagnostics.cat"
safety_output=""
if safety_output="$($CLAW_EXE check "$ROOT_DIR/test/safety_diagnostics.cat" 2>&1)"; then
  echo "expected safety_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$safety_output" != *"Safe functions cannot return edit views."* ]] ||
   [[ "$safety_output" != *"Returned view must be derived from a view parameter."* ]] ||
   [[ "$safety_output" != *"Raw address values may only appear inside raw blocks."* ]]; then
  echo "safety_diagnostics.cat did not contain the expected safety diagnostics" >&2
  printf '%s\n' "$safety_output" >&2
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

echo "[check/fail] test/pkg_bad_import"
typed_import_output=""
if typed_import_output="$($CLAW_EXE check "$ROOT_DIR/test/pkg_bad_import" 2>&1)"; then
  echo "expected pkg_bad_import to fail, but it passed" >&2
  exit 1
fi
if [[ "$typed_import_output" != *"error[semantic]: Call argument type mismatch: expected Int32, got Text"* ]] ||
   [[ "$typed_import_output" != *" --> "* ]] ||
   [[ "$typed_import_output" != *'add("oops", 1)'* ]]; then
  echo "pkg_bad_import did not contain the expected typed import diagnostic" >&2
  printf '%s\n' "$typed_import_output" >&2
  exit 1
fi

echo "[check/fail] test/pkg_missing_dep"
missing_dep_output=""
if missing_dep_output="$($CLAW_EXE check "$ROOT_DIR/test/pkg_missing_dep" 2>&1)"; then
  echo "expected pkg_missing_dep to fail, but it passed" >&2
  exit 1
fi
if [[ "$missing_dep_output" != *"error[module]: Unable to resolve import group rooted at 'term'."* ]] ||
   [[ "$missing_dep_output" != *"import term.{emit}"* ]]; then
  echo "pkg_missing_dep did not contain the expected missing dependency diagnostic" >&2
  printf '%s\n' "$missing_dep_output" >&2
  exit 1
fi

echo "[build/fail] test/pkg_bad_config"
bad_config_output=""
if bad_config_output="$($CLAW_EXE build "$ROOT_DIR/test/pkg_bad_config" 2>&1)"; then
  echo "expected pkg_bad_config to fail, but it passed" >&2
  exit 1
fi
if [[ "$bad_config_output" != *"Project config key 'entry' is not allowed. Root main.cat is the fixed workspace entry."* ]] ||
   [[ "$bad_config_output" != *"entry = \"main.cat\""* ]]; then
  echo "pkg_bad_config did not contain the expected config diagnostic" >&2
  printf '%s\n' "$bad_config_output" >&2
  exit 1
fi

echo "[open-file/fail] test/does_not_exist.cat"
missing_output=""
if missing_output="$($CLAW_EXE check "$ROOT_DIR/test/does_not_exist.cat" 2>&1)"; then
  echo "expected does_not_exist.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$missing_output" != *"Failed to resolve input path."* ]] ||
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

echo "[emit-oir] test/pkg_demo"
package_oir_output="$($CLAW_EXE emit-oir "$ROOT_DIR/test/pkg_demo" | normalize_text)"
expected_package_oir_output="$(cat "$ROOT_DIR/test/pkg_demo/emit_oir.expect" | normalize_text)"
if [[ "$package_oir_output" != "$expected_package_oir_output" ]]; then
  echo "pkg_demo emit_oir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s\n' "$expected_package_oir_output" >&2
  echo "--- actual ---" >&2
  printf '%s\n' "$package_oir_output" >&2
  exit 1
fi

echo "all frontend tests passed"
