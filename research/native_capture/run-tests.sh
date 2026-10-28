#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  echo "Usage: run-tests.sh /path/to/isolated/patch-applied-sdk-worktree" >&2
  exit 2
fi
ROOT="$(git rev-parse --show-toplevel)"
SDK_ROOT="$(realpath "$1")"
BUILD_DIR="out/deck-build/draw-capture-tests"
mkdir -p "$ROOT/out/deck-build"
cmake -S "$ROOT/research/native_capture/tests/draw_capture_sdk_tests" \
  -B "$ROOT/$BUILD_DIR" -G Ninja -DREXGLUE_SDK_ROOT="$SDK_ROOT"
cmake --build "$ROOT/$BUILD_DIR" --parallel 4
ctest --test-dir "$ROOT/$BUILD_DIR" --output-on-failure
python3 -B -m unittest discover -s "$ROOT/research/native_capture/tests" -p 'test_*.py' -v
