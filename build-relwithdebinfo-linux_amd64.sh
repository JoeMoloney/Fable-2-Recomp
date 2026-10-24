#!/usr/bin/env bash
set -euo pipefail

cd /mnt/HDD/git_stuff/my_git/Fable-2-Recomp
build="out/build/linux-amd64-relwithdebinfo"

cmake --fresh --preset linux-amd64-relwithdebinfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS=-march=x86-64-v2 \
  -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
  -DREXSDK_DIR="$PWD/thirdparty/rexglue-sdk" \
  -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF \
  -DFABLE2_CODEGEN_TOOL=

cmake --build "$build" --target fable_2_codegen -j4
cmake -S . -B "$build"
cmake --build "$build" --target fable_2 -j4
