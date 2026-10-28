#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-deck-builder:arch-20261018}"
BUILD_DIR="out/deck-build/native-texture-1d"
OUT="$ROOT/$BUILD_DIR"

if ! podman image exists "$IMAGE"; then
  echo "Pinned builder image is absent: $IMAGE" >&2
  exit 2
fi
mkdir -p "$OUT"
podman run --rm --network=none --userns=keep-id \
  --volume "$ROOT:/workspace:rw" --workdir /workspace --env HOME=/tmp \
  "$IMAGE" bash -lc '
    set -euo pipefail
    cmake -S research/native_texture_1d -B "$1" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS=-march=x86-64-v2
    cmake --build "$1" --parallel 2
    {
      clang++ --version | head -1
      cmake --version | head -1
      ninja --version
      glslangValidator --version | head -1
      pacman -Q glslang vulkan-icd-loader
    }
  ' bash "$BUILD_DIR" > "$OUT/toolchain.txt"

vert="$OUT/shaders/texture1d.vert.spv"
frag="$OUT/shaders/texture1d.frag.spv"
python3 "$ROOT/research/native_texture_1d/verify_spirv_contract.py" "$frag"
python3 - "$ROOT" "$OUT" "$vert" "$frag" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

root, out, vert, frag = map(Path, sys.argv[1:])

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

manifest = {
    "schema": 1,
    "builder_toolchain": (out / "toolchain.txt").read_text().splitlines(),
    "glsl_sources": {
        "vertex": digest(root / "research/native_texture_1d/texture1d.vert"),
        "fragment": digest(root / "research/native_texture_1d/texture1d.frag"),
    },
    "spirv": {"vertex": digest(vert), "fragment": digest(frag)},
    "shared_constants_contract": {
        "total_bytes": 512,
        "legacy_prefix_bytes": 288,
        "texture_1d_lane_offset": 288,
        "in_scattering_texture_index_offset": 304,
        "extinction_texture_index_offset": 308,
        "in_scattering_sampler_index_offset": 208,
        "extinction_sampler_index_offset": 212,
    },
}
(out / "shader-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY

EXE="$OUT/native_texture_1d"
python3 "$ROOT/tools/deck-build/inspect_elf.py" \
  "$EXE" --max-glibc 2.43 --max-glibcxx 3.4.35 --max-cxxabi 1.3.17 \
  > "$OUT/elf-compatibility.json"

for device in nvidia intel; do
  case "$device" in
    nvidia) driver_file=/usr/share/vulkan/icd.d/nvidia_icd.json; match=NVIDIA ;;
    intel) driver_file=/usr/share/vulkan/icd.d/intel_icd.json; match=Intel ;;
  esac
  if [[ ! -f "$driver_file" ]]; then
    echo "Desktop ICD manifest not found: $driver_file" >&2
    exit 2
  fi
  VK_DRIVER_FILES="$driver_file" VK_LOADER_LAYERS_DISABLE='~implicit~' \
    NATIVE_VULKAN_DEVICE="$match" "$EXE" > "$OUT/$device.json"
done

python3 - "$OUT" <<'PY'
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
for device in ("nvidia", "intel"):
    result = json.loads((out / f"{device}.json").read_text())
    assert result["result"] == "pass", (device, result)
    assert result["max_readback_error"] <= 0.005, (device, result)
    assert result["descriptor_indexing"]
    assert result["shared_constants"]["in_scattering_index_offset"] == 304
    assert result["shared_constants"]["size"] == 512
    assert result["shared_constants"]["extinction_index_offset"] == 308
    assert result["shared_constants"]["in_scattering_sampler_offset"] == 208
    assert result["shared_constants"]["extinction_sampler_offset"] == 212
    assert result["runtime_descriptor_array"]
    assert result["variable_descriptor_count"]
    assert result["sampled_image_array_dynamic_indexing"]
    print(f"{device}: {result['device']} max_readback_error={result['max_readback_error']}")
PY

printf 'Native 1D results: %s/{nvidia,intel}.json\n' "$OUT"
