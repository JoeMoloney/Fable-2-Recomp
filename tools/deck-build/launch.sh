#!/usr/bin/env bash
set -euo pipefail

# Stable Steam Non-Steam Game entry point for the versioned experimental install.
# Runs the Arch-built app in the pinned container because its GLIBC/GLIBCXX
# requirements exceed the SteamOS host. Vulkan/RADV and the GameScope session
# are deliberately supplied from the Deck host.
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
CURRENT="$ROOT/current"
GAME_DATA_ROOT="${FABLE2_GAME_DATA_ROOT:-/home/deck/Documents/fable-2-recomp}"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-gamescope-0}"
SDL_VIDEO_DRIVER="${FABLE2_SDL_VIDEO_DRIVER:-x11}"
X11_DISPLAY="${FABLE2_X11_DISPLAY:-:1}"
ICD="$ROOT/vulkan/radeon_icd.x86_64.json"
HOST_RADV="/run/host-vulkan/usr/lib/libvulkan_radeon.so"
IMAGE_ARCHIVE="$ROOT/runtime-images/fable2-native-vulkan-builder-arch-20261018-r2.oci.tar"

fail() { printf 'fable2-native-experimental: %s\n' "$*" >&2; exit 1; }
within() { [[ "$1" == "$2" || "$1" == "$2/"* ]]; }

case "$SDL_VIDEO_DRIVER" in
  wayland|x11) ;;
  *) fail "FABLE2_SDL_VIDEO_DRIVER must be wayland or x11" ;;
esac

[[ -L "$CURRENT" ]] || fail "missing current release link: $CURRENT"
RELEASE="$(realpath -e -- "$CURRENT")" || fail "current release target is unavailable"
APP_BIN="$RELEASE/bin"
[[ -x "$APP_BIN/fable_2_native" ]] || fail "experimental executable missing"
[[ -f "$APP_BIN/librexgpu-xenos.so" ]] || fail "Vulkan GPU plugin missing"
[[ -f "$APP_BIN/librexruntime.so" ]] || fail "ReXGlue runtime missing"
[[ -f "$ICD" && -r "$ICD" ]] || fail "Deck RADV ICD configuration missing"
[[ -r /usr/lib/libvulkan_radeon.so ]] || fail "host RADV library missing"
[[ -c /dev/dri/renderD128 && -r /dev/dri/renderD128 && -w /dev/dri/renderD128 ]] ||
  fail "Deck Vulkan render node is unavailable"
[[ -c /dev/input/js0 && -r /dev/input/js0 ]] || fail "Steam Deck gamepad device is unavailable"
if [[ "$SDL_VIDEO_DRIVER" == wayland ]]; then
  [[ -d "$RUNTIME_DIR" && -S "$RUNTIME_DIR/$WAYLAND_DISPLAY" ]] ||
    fail "GameScope Wayland socket is unavailable (run from Gaming Mode)"
else
  [[ -S "/tmp/.X11-unix/X${X11_DISPLAY#:}" ]] ||
    fail "GameScope XWayland socket is unavailable: $X11_DISPLAY"
fi

GAME_DATA_ROOT="$(realpath -e -- "$GAME_DATA_ROOT")" || fail "game data root not found"
[[ -f "$GAME_DATA_ROOT/default.xex" && -d "$GAME_DATA_ROOT/data" &&
   -f "$GAME_DATA_ROOT/data/dir.manifest" ]] || fail "game data root is incomplete"
EXP_ROOT="$(realpath -e -- "$ROOT")"
within "$GAME_DATA_ROOT" "$EXP_ROOT" && fail "game data must remain outside the experimental install"
within "$EXP_ROOT" "$GAME_DATA_ROOT" && fail "experimental install must remain separate from game data"

# The game repairs this index on startup. Check it read-only and refuse an
# incomplete tree rather than allowing the game to write into production data.
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
    print(f"fable2-native-experimental: refusing content tree with {duplicates} "
          f"duplicate and {len(missing)} unlisted manifest entries", file=sys.stderr)
    if missing:
        print("first missing: " + ", ".join(missing[:8]), file=sys.stderr)
    sys.exit(1)
PY

USERDATA="$(realpath -e -- "$ROOT/userdata")"
for item in saves cache logs fable2_config.toml fable_2.toml; do
  path="$APP_BIN/$item"
  [[ -L "$path" ]] || fail "$item must point to isolated experimental user data"
  target="$(realpath -m -- "$path")"
  within "$target" "$USERDATA" || fail "$item escapes isolated user data: $target"
done

X11_MOUNTS=()
X11_ENV=()
if [[ "$SDL_VIDEO_DRIVER" == x11 ]]; then
  X11_MOUNTS+=(--volume /tmp/.X11-unix:/tmp/.X11-unix:rw)
  X11_ENV+=(--env "DISPLAY=$X11_DISPLAY")
fi

IMAGE="$(sed -n 's/^image=//p' "$RELEASE/container-image.txt")"
EXPECTED_IMAGE_ID="$(sed -n 's/^image_id=//p' "$RELEASE/container-image.txt")"
[[ -n "$IMAGE" && -n "$EXPECTED_IMAGE_ID" ]] || fail "release container-image metadata is invalid"
podman image exists "$IMAGE" || fail "pinned runtime image is not loaded; see $IMAGE_ARCHIVE"
ACTUAL_IMAGE_ID="$(podman image inspect "$IMAGE" --format '{{.Id}}')"
[[ "$ACTUAL_IMAGE_ID" == "$EXPECTED_IMAGE_ID" ]] || fail "loaded container image ID differs from this release"

cmd=(podman run --rm --network=host --userns=keep-id --user "$(id -u):$(id -g)"
  # Guest physical memory is backed by shm_open; the Podman default (64 MiB)
  # is below ReXGlue's 512 MiB physical heap. Keep this within Deck host /dev/shm.
  --shm-size=1g
  --workdir /app
  --device /dev/dri/renderD128 --device /dev/input/js0
  --volume "$APP_BIN:/app:rw"
  --volume "$USERDATA:/userdata:rw"
  --volume "$GAME_DATA_ROOT:/game:ro"
  --volume "$RUNTIME_DIR:$RUNTIME_DIR:rw"
  --volume /usr/lib:/run/host-vulkan/usr/lib:ro
  --env "HOME=/userdata" --env "XDG_RUNTIME_DIR=$RUNTIME_DIR"
  --env "WAYLAND_DISPLAY=$WAYLAND_DISPLAY" --env "SDL_VIDEODRIVER=$SDL_VIDEO_DRIVER"
  # The Deck's Steam Input virtual controller is exposed here as /dev/input/js0.
  --env SDL_JOYSTICK_LINUX_CLASSIC=1
  --env "PULSE_SERVER=unix:$RUNTIME_DIR/pulse/native"
  --env "LD_LIBRARY_PATH=/app:/usr/lib:/run/host-vulkan/usr/lib"
  --env "XDG_CACHE_HOME=/userdata/cache"
  --env "VK_DRIVER_FILES=/tmp/fable2-radeon_icd.x86_64.json"
  --env "VK_ICD_FILENAMES=/tmp/fable2-radeon_icd.x86_64.json"
  --volume "$ICD:/tmp/fable2-radeon_icd.x86_64.json:ro"
  "${X11_MOUNTS[@]}" "${X11_ENV[@]}"
  "$IMAGE" /app/fable_2_native
  --game_data_root=/game --gpu_plugin=xenos --vulkan_device=0
  --window_width=1280 --window_height=720 --fullscreen=false --mouse_look=false
  --guest_vblank_pacing=false --frame_limit=60 --async_shader_compilation=false
  --fable2_native_vk_triangle=true "$@")

if [[ "${FABLE2_LAUNCH_DRY_RUN:-0}" == 1 ]]; then
  printf 'DRY RUN (not launching):'
  printf ' %q' "${cmd[@]}"
  printf '\nExperimental logs: %s/logs\n' "$USERDATA"
  exit 0
fi

printf 'Starting experimental Fable II in the pinned container with Steam Deck RADV.\n'
printf 'SDL video backend: %s\n' "$SDL_VIDEO_DRIVER"
printf 'Logs: %s/logs\n' "$USERDATA"
printf 'Container diagnostic log: %s/launcher.log\n' "$USERDATA/logs"
set +e
"${cmd[@]}" >>"$USERDATA/logs/launcher.log" 2>&1
status=$?
set -e
printf 'Container exited with status %s at %s\n' "$status" "$(date --iso-8601=seconds)" \
  >>"$USERDATA/logs/launcher.log"
exit "$status"
