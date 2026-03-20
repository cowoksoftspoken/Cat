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
  "test/definite_init.cat"
  "test/view_safety.cat"
  "test/drop_schedule.cat"
  "test/scalar_types.cat"
  "test/float_literals.cat"
  "test/target_size_types.cat"
  "test/method_dispatch.cat"
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

echo "[check/fail] test/method_view_borrow_ownership.cat"
method_view_borrow_output=""
if method_view_borrow_output="$($CLAW_EXE check "$ROOT_DIR/test/method_view_borrow_ownership.cat" 2>&1)"; then
  echo "expected method_view_borrow_ownership.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$method_view_borrow_output" != *"error[ownership]: Cannot move value while it is borrowed -> message"* ]] ||
   [[ "$method_view_borrow_output" != *" --> "* ]]; then
  echo "method_view_borrow_ownership.cat failed, but not with the expected ownership diagnostics" >&2
  printf '%s
' "$method_view_borrow_output" >&2
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

echo "[check/fail] test/definite_init_semantic.cat"
definite_init_semantic_output=""
if definite_init_semantic_output="$($CLAW_EXE check "$ROOT_DIR/test/definite_init_semantic.cat" 2>&1)"; then
  echo "expected definite_init_semantic.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$definite_init_semantic_output" != *"Immutable binding requires an initializer: value"* ]] ||
   [[ "$definite_init_semantic_output" != *" --> "* ]]; then
  echo "definite_init_semantic.cat did not contain the expected binding diagnostic" >&2
  printf '%s
' "$definite_init_semantic_output" >&2
  exit 1
fi

echo "[check/fail] test/definite_init_diagnostics.cat"
definite_init_output=""
if definite_init_output="$($CLAW_EXE check "$ROOT_DIR/test/definite_init_diagnostics.cat" 2>&1)"; then
  echo "expected definite_init_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$definite_init_output" != *"error[ownership]: Use of uninitialized value -> message"* ]] ||
   [[ "$definite_init_output" != *" --> "* ]]; then
  echo "definite_init_diagnostics.cat did not contain the expected initialization diagnostics" >&2
  printf '%s
' "$definite_init_output" >&2
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

echo "[check/fail] test/method_dispatch_diagnostics.cat"
method_dispatch_output=""
if method_dispatch_output="$($CLAW_EXE check "$ROOT_DIR/test/method_dispatch_diagnostics.cat" 2>&1)"; then
  echo "expected method_dispatch_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$method_dispatch_output" != *"Type 'Int32' does not provide method 'len'."* ]] ||
   [[ "$method_dispatch_output" != *"Call argument type mismatch: expected USize, got float literal"* ]] ||
   [[ "$method_dispatch_output" != *"Call argument type mismatch: expected Byte, got float literal"* ]] ||
   [[ "$method_dispatch_output" != *"Method receiver type mismatch: expected edit Bytes, got look Bytes"* ]] ||
   [[ "$method_dispatch_output" != *"Method receiver type mismatch: expected edit Vec, got look Vec"* ]]; then
  echo "method_dispatch_diagnostics.cat did not contain the expected method diagnostics" >&2
  printf '%s
' "$method_dispatch_output" >&2
  exit 1
fi

echo "[check/fail] test/float_literal_diagnostics.cat"
float_literal_output=""
if float_literal_output="$($CLAW_EXE check "$ROOT_DIR/test/float_literal_diagnostics.cat" 2>&1)"; then
  echo "expected float_literal_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$float_literal_output" != *"Unknown numeric literal suffix: Float16"* ]] ||
   [[ "$float_literal_output" != *"Float literal suffix must be Float32 or Float64, got UInt32."* ]] ||
   [[ "$float_literal_output" != *"Integer literal '300' does not fit target type Byte."* ]] ||
   [[ "$float_literal_output" != *"Integer literal '16777217' does not fit exactly in target type Float32."* ]] ||
   [[ "$float_literal_output" != *"Float literal '1e-50' does not fit target type Float32."* ]] ||
   [[ "$float_literal_output" != *"Initializer type mismatch for 'count': expected Int32, got float literal"* ]]; then
  echo "float_literal_diagnostics.cat did not contain the expected float diagnostics" >&2
  printf '%s
' "$float_literal_output" >&2
  exit 1
fi

echo "[check/fail] test/scalar_type_diagnostics.cat"
scalar_type_output=""
if scalar_type_output="$($CLAW_EXE check "$ROOT_DIR/test/scalar_type_diagnostics.cat" 2>&1)"; then
  echo "expected scalar_type_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$scalar_type_output" != *"Call argument type mismatch: expected UInt16, got UInt32"* ]] ||
   [[ "$scalar_type_output" != *"Initializer type mismatch for 'small': expected UInt16, got UInt64"* ]] ||
   [[ "$scalar_type_output" != *"Incompatible arithmetic operands: UInt64 and USize"* ]]; then
  echo "scalar_type_diagnostics.cat did not contain the expected scalar diagnostics" >&2
  printf '%s
' "$scalar_type_output" >&2
  exit 1
fi

echo "[check/fail] test/target_size_diagnostics.cat"
target_size_output=""
if target_size_output="$($CLAW_EXE check "$ROOT_DIR/test/target_size_diagnostics.cat" 2>&1)"; then
  echo "expected target_size_diagnostics.cat to fail, but it passed" >&2
  exit 1
fi
if [[ "$target_size_output" != *"Integer literal '18446744073709551616' does not fit target type USize."* ]] ||
   [[ "$target_size_output" != *"Integer literal '9223372036854775808' does not fit target type ISize."* ]]; then
  echo "target_size_diagnostics.cat did not contain the expected target-size diagnostics" >&2
  printf '%s
' "$target_size_output" >&2
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

echo "[emit-air] test/method_dispatch_air.cat"
method_air_output="$($CLAW_EXE emit-air "$ROOT_DIR/test/method_dispatch_air.cat" | normalize_text)"
expected_method_air_output="$(cat "$ROOT_DIR/test/method_dispatch_air.expect" | normalize_text)"
if [[ "$method_air_output" != "$expected_method_air_output" ]]; then
  echo "method dispatch emit_air snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s
' "$expected_method_air_output" >&2
  echo "--- actual ---" >&2
  printf '%s
' "$method_air_output" >&2
  exit 1
fi

echo "[emit-air] test/float_emit_air.cat"
float_air_output="$($CLAW_EXE emit-air "$ROOT_DIR/test/float_emit_air.cat" | normalize_text)"
expected_float_air_output="$(cat "$ROOT_DIR/test/float_emit_air.expect" | normalize_text)"
if [[ "$float_air_output" != "$expected_float_air_output" ]]; then
  echo "float emit_air snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s
' "$expected_float_air_output" >&2
  echo "--- actual ---" >&2
  printf '%s
' "$float_air_output" >&2
  exit 1
fi

echo "[emit-air] test/scalar_emit_air.cat"
scalar_air_output="$($CLAW_EXE emit-air "$ROOT_DIR/test/scalar_emit_air.cat" | normalize_text)"
expected_scalar_air_output="$(cat "$ROOT_DIR/test/scalar_emit_air.expect" | normalize_text)"
if [[ "$scalar_air_output" != "$expected_scalar_air_output" ]]; then
  echo "scalar emit_air snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s\n' "$expected_scalar_air_output" >&2
  echo "--- actual ---" >&2
  printf '%s\n' "$scalar_air_output" >&2
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

echo "[emit-oir] test/drop_schedule.cat"
drop_schedule_output="$($CLAW_EXE emit-oir "$ROOT_DIR/test/drop_schedule.cat" | normalize_text)"
expected_drop_schedule_output="$(cat "$ROOT_DIR/test/drop_schedule_oir.expect" | normalize_text)"
if [[ "$drop_schedule_output" != "$expected_drop_schedule_output" ]]; then
  echo "drop_schedule emit_oir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s\n' "$expected_drop_schedule_output" >&2
  echo "--- actual ---" >&2
  printf '%s\n' "$drop_schedule_output" >&2
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

echo "[emit-lir] test/emit_air.cat"
lir_output="$($CLAW_EXE emit-lir "$ROOT_DIR/test/emit_air.cat" | normalize_text)"
expected_lir_output="$(cat "$ROOT_DIR/test/emit_lir.expect" | normalize_text)"
if [[ "$lir_output" != "$expected_lir_output" ]]; then
  echo "emit_lir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s
' "$expected_lir_output" >&2
  echo "--- actual ---" >&2
  printf '%s
' "$lir_output" >&2
  exit 1
fi

echo "[emit-lir] test/drop_schedule.cat"
drop_schedule_lir_output="$($CLAW_EXE emit-lir "$ROOT_DIR/test/drop_schedule.cat" | normalize_text)"
expected_drop_schedule_lir_output="$(cat "$ROOT_DIR/test/drop_schedule_lir.expect" | normalize_text)"
if [[ "$drop_schedule_lir_output" != "$expected_drop_schedule_lir_output" ]]; then
  echo "drop_schedule emit_lir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s
' "$expected_drop_schedule_lir_output" >&2
  echo "--- actual ---" >&2
  printf '%s
' "$drop_schedule_lir_output" >&2
  exit 1
fi

echo "[emit-lir] test/pkg_demo"
package_lir_output="$($CLAW_EXE emit-lir "$ROOT_DIR/test/pkg_demo" | normalize_text)"
expected_package_lir_output="$(cat "$ROOT_DIR/test/pkg_demo/emit_lir.expect" | normalize_text)"
if [[ "$package_lir_output" != "$expected_package_lir_output" ]]; then
  echo "pkg_demo emit_lir snapshot mismatch" >&2
  echo "--- expected ---" >&2
  printf '%s
' "$expected_package_lir_output" >&2
  echo "--- actual ---" >&2
  printf '%s
' "$package_lir_output" >&2
  exit 1
fi

echo "all frontend tests passed"
