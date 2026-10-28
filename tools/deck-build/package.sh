#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
ALLOW_DIRTY=0
if [[ "${1:-}" == "--allow-dirty" ]]; then
  ALLOW_DIRTY=1
  shift
fi
if (($# == 0)); then
  echo "Usage: package.sh [--allow-dirty] ELF [ELF ...]" >&2
  exit 2
fi
if (( ! ALLOW_DIRTY )) && [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]]; then
  echo "Refusing to package a dirty source tree (use --allow-dirty only deliberately)." >&2
  exit 2
fi

COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
SHORT="${COMMIT:0:12}"
TAG="${FABLE2_PACKAGE_TAG:-$SHORT}"
if ! [[ "$TAG" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "Invalid FABLE2_PACKAGE_TAG" >&2
  exit 2
fi
OUT="$ROOT/out/artifacts/$TAG"
IMAGE="${FABLE2_BUILDER_IMAGE:-localhost/fable2-deck-builder:arch-20261018}"
PACKAGES="${FABLE2_BUILDER_MANIFEST:-$ROOT/out/deck-build/container-manifest.txt}"
mkdir -p "$ROOT/out/artifacts"
if ! mkdir "$OUT"; then
  echo "Package output already exists: $OUT" >&2
  exit 2
fi
mkdir "$OUT/bin"

declare -A seen=()
for arg in "$@"; do
  path="$arg"
  [[ "$path" = /* ]] || path="$ROOT/$path"
  if [[ ! -f "$path" ]]; then
    echo "Not a file: $arg" >&2
    exit 2
  fi
  name="$(basename "$path")"
  if [[ -n "${seen[$name]:-}" ]]; then
    echo "Duplicate artifact basename: $name" >&2
    exit 2
  fi
  seen[$name]=1
  install -m 0755 "$path" "$OUT/bin/$name"
done

python3 - "$ROOT" "$OUT" "$COMMIT" "$IMAGE" "$PACKAGES" <<'PY'
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

root, out = Path(sys.argv[1]), Path(sys.argv[2])
commit, image = sys.argv[3], sys.argv[4]
packages_path = Path(sys.argv[5])
package_text = packages_path.read_text() if packages_path.exists() else "not captured; build-image.sh has not completed"
shader_manifest_path = root / "out/deck-build/native-texture-1d/shader-manifest.json"
shader_manifest = json.loads(shader_manifest_path.read_text()) if shader_manifest_path.exists() else None
files = []
for path in sorted((out / "bin").iterdir()):
    info = json.loads(subprocess.check_output([
        sys.executable, str(root / "tools/deck-build/inspect_elf.py"), str(path)
    ], text=True))
    info["elfs"][0]["path"] = path.name
    files.append({
        "name": path.name,
        "size_bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "elf": info["elfs"][0],
    })
manifest = {
    "schema": 1,
    "source_commit": commit,
    "source_dirty": bool(subprocess.check_output(
        ["git", "-C", str(root), "status", "--porcelain", "--untracked-files=normal"], text=True
    )),
    "builder_image": image,
    "builder_image_id": subprocess.check_output(
        ["podman", "image", "inspect", image, "--format", "{{.Id}}"], text=True
    ).strip() if subprocess.run(["podman", "image", "exists", image], capture_output=True).returncode == 0 else None,
    "toolchain_package_manifest": package_text,
    "deck_host_reference_exports": {"GLIBC": "2.43", "GLIBCXX": "3.4.35", "CXXABI": "1.3.17"},
    "embedded_shader_build_inputs": shader_manifest,
    "files": files,
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY

SOURCE_DATE_EPOCH="$(git -C "$ROOT" show -s --format=%ct "$COMMIT")"
ARCHIVE="$ROOT/out/artifacts/fable2-experimental-$TAG.tar.gz"
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
  --transform="s,^$TAG,$SHORT," \
  -C "$ROOT/out/artifacts" -cf - "$TAG" | gzip -n > "$ARCHIVE"
(cd "$ROOT/out/artifacts" && sha256sum "$(basename "$ARCHIVE")" > "$(basename "$ARCHIVE").sha256")
printf 'Packaged: %s\nChecksum: %s\n' "$ARCHIVE" "$ARCHIVE.sha256"
