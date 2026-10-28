#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
TOOL_DIR="$ROOT/tools/deck-build"
LOCK="$TOOL_DIR/toolchain.lock"
IMAGE="localhost/fable2-deck-builder:arch-20261018"
BASE_IMAGE="docker.io/library/archlinux@sha256:dfb01267da058e5d08ffdc543bf484c9b404655b0bac1d0201d972bf950cdfd0"
OUT="$ROOT/out/deck-build"

mkdir -p "$OUT"
if ! podman image exists "$BASE_IMAGE"; then
  podman pull "$BASE_IMAGE"
fi
podman build --pull=never --platform=linux/amd64 \
  --build-arg ARCH_SNAPSHOT=2026/10/18 \
  --file "$TOOL_DIR/Containerfile" --tag "$IMAGE" "$TOOL_DIR"

IMAGE_ID="$(podman image inspect "$IMAGE" --format '{{.Id}}')"
podman run --rm --network=none "$IMAGE" bash -lc '
  set -euo pipefail
  printf "clang="; clang --version | head -1
  printf "cmake="; cmake --version | head -1
  printf "ninja="; ninja --version
  printf "glibc="; getconf GNU_LIBC_VERSION
  pacman -Q glibc gcc-libs clang cmake ninja lld base-devel glslang vulkan-icd-loader
  printf "glslang_version="; glslangValidator --version | head -1
' > "$OUT/container-packages.txt"
printf 'image=%s\nimage_id=%s\n' "$IMAGE" "$IMAGE_ID" > "$OUT/container-image.txt"
cat "$OUT/container-image.txt" "$OUT/container-packages.txt" > "$OUT/container-manifest.txt"
expected_clang="$(awk -F= '$1 == "expected_clang" {print $2}' "$LOCK")"
expected_cmake="$(awk -F= '$1 == "expected_cmake" {print $2}' "$LOCK")"
expected_ninja="$(awk -F= '$1 == "expected_ninja" {print $2}' "$LOCK")"
expected_glslang="$(awk -F= '$1 == "expected_glslang" {print $2}' "$LOCK")"
expected_vulkan_loader="$(awk -F= '$1 == "expected_vulkan_loader" {print $2}' "$LOCK")"
grep -Fx "clang=clang version $expected_clang" "$OUT/container-packages.txt" >/dev/null
grep -Fx "cmake=cmake version $expected_cmake" "$OUT/container-packages.txt" >/dev/null
grep -Fx "ninja=$expected_ninja" "$OUT/container-packages.txt" >/dev/null
grep -Eq "^glslang_version=.*$expected_glslang$" "$OUT/container-packages.txt"
grep -Eq "^vulkan-icd-loader .*${expected_vulkan_loader}" "$OUT/container-packages.txt"
printf 'Builder image: %s (%s)\nManifest: %s\n' "$IMAGE" "$IMAGE_ID" "$OUT/container-manifest.txt"
