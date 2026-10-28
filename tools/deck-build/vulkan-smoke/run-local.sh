#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-deck-builder:arch-20261018}"
BUILD_DIR="out/deck-build/vulkan-smoke"

if ! podman image exists "$IMAGE"; then
  echo "Pinned builder image is absent: $IMAGE" >&2
  exit 2
fi
mkdir -p "$ROOT/out/deck-build"
podman run --rm --network=none --userns=keep-id \
  --volume "$ROOT:/workspace:rw" --workdir /workspace --env HOME=/tmp \
  "$IMAGE" bash -lc '
    set -euo pipefail
    cmake -S tools/deck-build/vulkan-smoke -B "$1" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS=-march=x86-64-v2
    cmake --build "$1" --parallel 2
  ' bash "$BUILD_DIR"

EXE="$ROOT/$BUILD_DIR/vulkan_smoke"
python3 "$ROOT/tools/deck-build/inspect_elf.py" \
  "$EXE" --max-glibc 2.43 --max-glibcxx 3.4.35 --max-cxxabi 1.3.17 \
  > "$ROOT/$BUILD_DIR/elf-compatibility.json"

for icd in nvidia intel; do
  case "$icd" in
    nvidia) driver_file=/usr/share/vulkan/icd.d/nvidia_icd.json ;;
    intel) driver_file=/usr/share/vulkan/icd.d/intel_icd.json ;;
  esac
  if [[ ! -f "$driver_file" ]]; then
    echo "Desktop ICD manifest not found: $driver_file" >&2
    exit 2
  fi
  VK_DRIVER_FILES="$driver_file" "$EXE" > "$ROOT/$BUILD_DIR/$icd.json"
done

printf 'Vulkan smoke results: %s/{nvidia,intel}.json\n' "$ROOT/$BUILD_DIR"
