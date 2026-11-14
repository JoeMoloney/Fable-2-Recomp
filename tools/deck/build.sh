#!/usr/bin/env bash
# Build Fable II for the Steam Deck inside the pinned Arch container (the same image
# the Deck runs it in), incrementally.
#
#   tools/deck/build.sh            # -march=x86-64-v3 -ffp-contract=off -> out/build/deck-linux-amd64-release-v3
#   tools/deck/build.sh v2         # -march=x86-64-v2 (release r1-r5 style) -> out/build/deck-linux-amd64-release
#
# Then deploy with tools/deck/deploy.sh <build-dir> <release-name>.
#
# GPU plugin PGO (only rexgpu-xenos is rebuilt when this changes; path is inside the
# container, i.e. under /workspace):
#   FABLE2_GPU_PGO=generate tools/deck/build.sh
#   FABLE2_GPU_PGO=/workspace/out/pgo/rexgpu-xenos.profdata tools/deck/build.sh
# Unset = PGO off (resets an earlier setting).
#
# Fresh build ~25 min, incremental ~1-2 min. Network is disabled in the container.
# NOTE: the SDK writes its libraries into the shared thirdparty/rexglue-sdk/out/linux-amd64/,
# so after a Deck build, rebuild desktop before running it (delete those two .so first):
#   rm -f thirdparty/rexglue-sdk/out/linux-amd64/librexruntime.so thirdparty/rexglue-sdk/out/linux-amd64/librexgpu-xenos.so
#   cmake --build out/build/linux-amd64-release --target fable_2 -j12
# (Based on Fable-2-native-vulkan-renderer/tools/deck-build/build.sh.)
set -euo pipefail

ROOT="$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-native-vulkan:arch-20261018-r3}"
case "${1:-v3}" in
  v3) FLAGS="-march=x86-64-v3 -ffp-contract=off"; BUILD_DIR="out/build/deck-linux-amd64-release-v3" ;;
  v2) FLAGS="-march=x86-64-v2"; BUILD_DIR="out/build/deck-linux-amd64-release" ;;
  *) echo "usage: $0 [v3|v2]" >&2; exit 2 ;;
esac
BUILD_DIR="${FABLE2_BUILD_DIR:-$BUILD_DIR}"

podman image exists "$IMAGE" || { echo "builder image missing: $IMAGE" >&2; exit 2; }

JOBS="${FABLE2_BUILD_JOBS:-}"
if [[ -z "$JOBS" ]]; then
  # About one compile job per 2 GiB of available RAM, at most 12.
  avail_kib="$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)"
  JOBS=$((avail_kib / 2097152)); ((JOBS < 1)) && JOBS=1
  ((JOBS > $(nproc))) && JOBS=$(nproc)
  ((JOBS > 12)) && JOBS=12
fi

mkdir -p "$ROOT/out/build"
podman run --rm --network=none --userns=keep-id \
  --volume "$ROOT:/workspace:rw" --workdir /workspace \
  --env HOME=/tmp --env CC=clang --env CXX=clang++ --env FABLE2_CONTAINER_BUILD=1 \
  "$IMAGE" bash -lc '
    set -euo pipefail
    build_dir="$1"; jobs="$2"; flags="$3"; gpu_pgo="$4"
    cmake -S . -B "$build_dir" -G Ninja \
      "-DFABLE2_GPU_PGO=$gpu_pgo" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
      -DREXSDK_DIR=/workspace/thirdparty/rexglue-sdk \
      -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF \
      -DFABLE2_CODEGEN_TOOL= -DFABLE2_EXPERIMENTAL_NATIVE_VULKAN=OFF
    cmake --build "$build_dir" --parallel "$jobs"
  ' bash "$BUILD_DIR" "$JOBS" "$FLAGS" "${FABLE2_GPU_PGO:-}"

echo "Build complete: $ROOT/$BUILD_DIR (flags: $FLAGS, jobs=$JOBS, gpu_pgo=${FABLE2_GPU_PGO:-off})"
echo "Remember: rebuild desktop before running it (see the NOTE at the top of this script)."
