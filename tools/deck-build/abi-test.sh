#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-deck-builder:arch-20261018}"
BUILD_DIR="out/deck-build/abi-probe"
mkdir -p "$ROOT/out/deck-build"

podman run --rm --network=none --userns=keep-id \
  --volume "$ROOT:/workspace:rw" --workdir /workspace \
  --env HOME=/tmp "$IMAGE" bash -lc '
    set -euo pipefail
    cmake -S tools/deck-build/abi-probe -B "$1" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS=-march=x86-64-v2
    cmake --build "$1" --parallel 2
    ctest --test-dir "$1" --output-on-failure
    "$1/deck_abi_probe_test"
    readelf -h -l -d --version-info "$1/deck_abi_probe_test" > "$1/executable.readelf.txt"
    readelf -h -l -d --version-info "$1/libdeck_abi_probe.so" > "$1/library.readelf.txt"
    ldd -v "$1/deck_abi_probe_test" > "$1/dependencies.ldd.txt"
  ' bash "$BUILD_DIR"

python3 "$ROOT/tools/deck-build/inspect_elf.py" \
  "$ROOT/$BUILD_DIR/deck_abi_probe_test" "$ROOT/$BUILD_DIR/libdeck_abi_probe.so" \
  --max-glibc 2.43 --max-glibcxx 3.4.36
