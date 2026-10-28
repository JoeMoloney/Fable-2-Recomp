#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-deck-builder:arch-20261018}"
BUILD_DIR="${FABLE2_BUILD_DIR:-out/build/deck-linux-amd64-release}"
BUILD_TYPE="${FABLE2_BUILD_TYPE:-Release}"
JOBS="${FABLE2_BUILD_JOBS:-}"
CODEGEN_TOOL="${FABLE2_CODEGEN_TOOL:-}"
CODEGEN_LD_LIBRARY_PATH="${FABLE2_CODEGEN_LD_LIBRARY_PATH:-}"
NATIVE_VULKAN="${FABLE2_EXPERIMENTAL_NATIVE_VULKAN:-OFF}"

if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
  echo "Run this from a Fable-2-Recomp worktree" >&2
  exit 2
fi
if ! podman image exists "$IMAGE"; then
  echo "Pinned builder image is absent: $IMAGE" >&2
  echo "Run tools/deck-build/build-image.sh first." >&2
  exit 2
fi

if [[ -z "$JOBS" ]]; then
  # 10900K: 10 physical cores/20 threads, 30 GiB RAM. Limit concurrent C++
  # compiles to 12 and approximately one worker per 2 GiB currently available.
  threads="$(nproc)"
  available_kib="$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)"
  memory_jobs=$((available_kib / 2097152))
  (( memory_jobs < 1 )) && memory_jobs=1
  JOBS="$threads"
  (( JOBS > 12 )) && JOBS=12
  (( JOBS > memory_jobs )) && JOBS="$memory_jobs"
fi
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "FABLE2_BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

mkdir -p "$ROOT/out/build"
podman run --rm --network=none --userns=keep-id \
  --volume "$ROOT:/workspace:rw" --workdir /workspace \
  --env HOME=/tmp --env CC=clang --env CXX=clang++ \
  --env FABLE2_CONTAINER_BUILD=1 \
  --env LD_LIBRARY_PATH="$CODEGEN_LD_LIBRARY_PATH" \
  "$IMAGE" bash -lc '
    set -euo pipefail
    build_dir="$1"; build_type="$2"; jobs="$3"; codegen_tool="$4"; native_vulkan="$5"
    cmake -S . -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_FLAGS=-march=x86-64-v2 \
      -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
      -DREXSDK_DIR=/workspace/thirdparty/rexglue-sdk \
      -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF \
      -DFABLE2_CODEGEN_TOOL="$codegen_tool" \
      -DFABLE2_EXPERIMENTAL_NATIVE_VULKAN="$native_vulkan"
    cmake --build "$build_dir" --parallel "$jobs"
  ' bash "$BUILD_DIR" "$BUILD_TYPE" "$JOBS" "$CODEGEN_TOOL" "$NATIVE_VULKAN"

printf 'Build complete: %s (jobs=%s, type=%s, image=%s)\n' \
  "$ROOT/$BUILD_DIR" "$JOBS" "$BUILD_TYPE" "$IMAGE"
