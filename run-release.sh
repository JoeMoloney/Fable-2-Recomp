#!/usr/bin/env bash
set -euo pipefail

# Launch only the isolated experimental native-Vulkan build. Never fall back
# to the reference fable_2 executable if this one is absent.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
BUILD_DIR="$SCRIPT_DIR/out/build/linux-amd64-native-vulkan-release"
EXECUTABLE="$BUILD_DIR/fable_2_native"
GPU_PLUGIN="$BUILD_DIR/librexgpu-xenos.so"
RUNTIME="$BUILD_DIR/librexruntime.so"
GAME_DATA_ROOT="${FABLE2_GAME_DATA_ROOT:-$SCRIPT_DIR/../Fable-2-Recomp}"
REFERENCE_BUILD_DIR="${FABLE2_REFERENCE_BUILD_DIR:-$SCRIPT_DIR/../Fable-2-Recomp/out/build/linux-amd64-release}"

fail() { printf 'run-release: %s\n' "$*" >&2; exit 1; }

[[ -x "$EXECUTABLE" ]] || fail "experimental executable missing/not executable: $EXECUTABLE"
[[ -f "$GPU_PLUGIN" ]] || fail "experimental Vulkan GPU plugin missing: $GPU_PLUGIN"
[[ -f "$RUNTIME" ]] || fail "experimental ReXGlue runtime missing: $RUNTIME"
[[ -f "$BUILD_DIR/fable2_config.toml" ]] || fail "experimental app configuration missing: $BUILD_DIR/fable2_config.toml"
[[ ! -L "$BUILD_DIR/fable2_config.toml" ]] || fail "experimental app configuration must not symlink into another build"

GAME_DATA_ROOT="$(realpath -e -- "$GAME_DATA_ROOT")" || fail "game data root not found: ${FABLE2_GAME_DATA_ROOT:-$SCRIPT_DIR/../Fable-2-Recomp}"
[[ -f "$GAME_DATA_ROOT/default.xex" ]] || fail "default.xex missing under $GAME_DATA_ROOT"
[[ -d "$GAME_DATA_ROOT/data" ]] || fail "data directory missing under $GAME_DATA_ROOT"
[[ -f "$GAME_DATA_ROOT/data/dir.manifest" ]] || fail "data/dir.manifest missing under $GAME_DATA_ROOT"
REFERENCE_BUILD_DIR="$(realpath -m -- "$REFERENCE_BUILD_DIR")"

within() {
  local path="$1" root="$2"
  [[ "$path" == "$root" || "$path" == "$root/"* ]]
}

EXPERIMENTAL_ROOT="$(realpath -e -- "$BUILD_DIR")" || fail "experimental build directory missing: $BUILD_DIR"
[[ "$EXPERIMENTAL_ROOT" != "$REFERENCE_BUILD_DIR" ]] || fail "experimental and reference build directories resolve to the same path"
within "$EXPERIMENTAL_ROOT" "$REFERENCE_BUILD_DIR" && fail "experimental build directory is inside the reference build"

# The app pins user_data_root/cache_root to the executable directory. Refuse
# symlinks or path aliases that could redirect those writes into the reference.
for kind in saves cache logs; do
  path="$(realpath -m -- "$BUILD_DIR/$kind")"
  within "$path" "$REFERENCE_BUILD_DIR" && fail "experimental $kind path resolves inside the reference build: $path"
  within "$path" "$EXPERIMENTAL_ROOT" || fail "experimental $kind path escapes the experimental build: $path"
done

# SDK cvar config is created beside the executable on first run. Reject a
# pre-existing symlink or alias to the reference configuration.
for name in fable2_config.toml fable_2.toml; do
  path="$BUILD_DIR/$name"
  if [[ -L "$path" ]]; then
    fail "experimental configuration must not be a symlink: $path"
  fi
  if [[ -e "$path" ]]; then
    resolved="$(realpath -e -- "$path")"
    within "$resolved" "$REFERENCE_BUILD_DIR" && fail "experimental configuration resolves inside reference build: $resolved"
    within "$resolved" "$EXPERIMENTAL_ROOT" || fail "experimental configuration escapes experimental build: $resolved"
  fi
done

# The application repairs dir.manifest at startup. Require the selected game
# data to already be complete so that this launch path will not mutate the
# reference content tree during startup.
python3 - "$GAME_DATA_ROOT/data" <<'PY'
import pathlib
import sys

data = pathlib.Path(sys.argv[1])
manifest = data / "dir.manifest"
key = lambda value: value.replace("/", "\\").lower()
seen = set()
duplicates = 0
for line in manifest.read_text(errors="replace").splitlines():
    if line.strip():
        normalized = key(line.strip())
        duplicates += normalized in seen
        seen.add(normalized)
missing = [str(path.relative_to(data)) for path in data.rglob("*")
           if path.is_file() and path.name != "dir.manifest"
           and key(path.relative_to(data).as_posix()) not in seen]
if duplicates or missing:
    print(f"run-release: refusing content root with {duplicates} duplicate and "
          f"{len(missing)} unlisted manifest entries", file=sys.stderr)
    if missing:
        print("first missing: " + ", ".join(missing[:8]), file=sys.stderr)
    sys.exit(1)
PY

export SDL_VIDEODRIVER=wayland
# The executable and the plugin both use the staged runtime in this isolated
# directory; this takes precedence over the SDK build-tree copy.
export LD_LIBRARY_PATH="$BUILD_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

command=(
  "$EXECUTABLE"
  "--game_data_root=$GAME_DATA_ROOT"
  --gpu_plugin=xenos --vulkan_device=0
  --window_width=1280 --window_height=720
  --fullscreen=false --mouse_look=false
  --guest_vblank_pacing=false
  --frame_limit=60 --async_shader_compilation=false
  "$@"
)

if [[ "${FABLE2_LAUNCH_DRY_RUN:-0}" == 1 ]]; then
  printf 'DRY RUN (not launching):'
  printf ' %q' "${command[@]}"
  printf '\nRuntime logs: %s/logs\n' "$BUILD_DIR"
  exit 0
fi

printf 'Launching experimental Vulkan build: %s\nRuntime logs: %s/logs\n' "$EXECUTABLE" "$BUILD_DIR"
exec "${command[@]}"
