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

BUILD_DIR="$ROOT_DIR/test_native/build"
mkdir -p "$BUILD_DIR"

single_output="$BUILD_DIR/revise_single_file.exe"
workspace_output="$BUILD_DIR/revise_workspace.exe"

echo "[build/pass] test_native/revise_single_file.cat"
"$CLAW_EXE" build "$ROOT_DIR/test_native/revise_single_file.cat" "$single_output" >/dev/null

single_stdout="$("$single_output")"
if [[ "$single_stdout" != "3" ]]; then
  echo "revised single-file native build produced unexpected output: $single_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_workspace"
"$CLAW_EXE" build "$ROOT_DIR/test_native/revise_workspace" "$workspace_output" >/dev/null

workspace_stdout="$("$workspace_output")"
if [[ "$workspace_stdout" != "36" ]]; then
  echo "revised workspace native build produced unexpected output: $workspace_stdout" >&2
  exit 1
fi
