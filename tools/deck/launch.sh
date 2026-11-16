#!/usr/bin/env bash
# Fable II Recompiled launcher for the Steam Deck (what the Steam shortcut runs).
#
# Installed on the Deck as ~/Documents/fable2-native-experimental/launch.sh by
# tools/deck/install-launcher.sh. This copy in the repo is the source of truth (the
# Deck-only original was lost on 2026-11-16 together with its folder).
#
# Layout (same as tools/deck/deploy.sh / flags.sh / use-release.sh expect):
#   fable2-native-experimental/
#     launch.sh                  this file
#     radeon_icd.x86_64.json     points the container's Vulkan loader at the host RADV
#     current -> releases/<name> the release to run
#     releases/<name>/bin/       fable_2, librexruntime.so, librexgpu-xenos.so and
#                                symlinks cache, fable2_config.toml, fable_2.toml,
#                                logs, saves -> ../../../userdata/...
#     userdata/                  saves, cache, logs, configs (shared by all releases)
# Game content (default.xex + data/) is read-only from ~/Documents/fable-2-recomp.
#
# Runs inside the pinned Arch image (SteamOS's own glibc is too old for the build)
# with the Deck's host RADV, Game Mode XWayland, PulseAudio and Steam Input pad.
#
#   FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh   print the command, don't start
#
# tools/deck/flags.sh rewrites the line with --vulkan_async_submit=true (keep it
# exactly once, closing the command with "$@").
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
GAME="${FABLE2_GAME_DIR:-$HOME/Documents/fable-2-recomp}"
USERDATA="$ROOT/userdata"
IMAGE="localhost/fable2-native-vulkan:arch-20261018-r3"
EXPECTED_IMAGE_ID="929cd06a66e2eb3959391bfb4ef1695690af3b6610b21b61ee1d9f0faad035cd"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
ICD="$ROOT/radeon_icd.x86_64.json"
LOG="$USERDATA/logs/launcher.log"

fail() { printf 'fable2: %s\n' "$*" >&2; exit 1; }

# Steam's runtime libraries must not leak into the container.
unset LD_PRELOAD LD_LIBRARY_PATH

[[ -x "$ROOT/current/bin/fable_2" ]] || fail "no current release (current/bin/fable_2 missing)"
[[ -f "$GAME/default.xex" && -d "$GAME/data" ]] || fail "game content missing in $GAME"
[[ -r "$ICD" ]] || fail "RADV ICD file is missing: $ICD"
[[ -r /usr/lib/libvulkan_radeon.so ]] || fail "host RADV library is unavailable"
[[ -c /dev/dri/renderD128 ]] || fail "Vulkan render node /dev/dri/renderD128 is unavailable"
[[ -S "$RUNTIME_DIR/pulse/native" ]] || fail "PulseAudio socket is unavailable"
podman image exists "$IMAGE" || fail "pinned runtime image is not loaded: $IMAGE"
[[ "$(podman image inspect "$IMAGE" --format '{{.Id}}')" == "$EXPECTED_IMAGE_ID" ]] ||
  fail "runtime image ID does not match the tested pin"

# X display: the inherited one if its socket exists, else Game Mode's XWayland :1, else :0.
disp="${DISPLAY:-}"
if [[ -z "$disp" || ! -S "/tmp/.X11-unix/X${disp#:}" ]]; then
  if [[ -S /tmp/.X11-unix/X1 ]]; then disp=:1; elif [[ -S /tmp/.X11-unix/X0 ]]; then disp=:0
  else fail "no X11 display socket"; fi
fi
pad=()
[[ -c /dev/input/js0 ]] && pad=(--device /dev/input/js0)
# Game Mode's X servers need Steam's auth cookie (XAUTHORITY lives under
# $RUNTIME_DIR, which is mounted), and the cookie is tied to the host name.
xauth=()
[[ -n "${XAUTHORITY:-}" && -r "$XAUTHORITY" ]] && xauth=(--env "XAUTHORITY=$XAUTHORITY")
[[ -n "${XAUTHORITY:-}" && -r "$XAUTHORITY" && "$XAUTHORITY" != "$RUNTIME_DIR"/* ]] &&
  xauth+=(--volume "$XAUTHORITY:$XAUTHORITY:ro")

mkdir -p "$USERDATA/logs" "$USERDATA/cache" "$USERDATA/saves"
# Keep the logs bounded. launcher.log over 5 MB at launch -> launcher.log.1 (older copy dropped).
# --guest_fps_log writes a line per second, ~350 KB per hour of play.
if [[ -f "$LOG" && $(stat -c %s "$LOG") -gt 5242880 ]]; then mv -f "$LOG" "$LOG.1"; fi
# The game writes a new numbered log each run (fable_2_NNN.log, FPS lines included)
# and never deletes old ones: keep the newest 20.
ls -1t "$USERDATA/logs"/fable_2_*.log 2>/dev/null | tail -n +21 | xargs -r rm -f --

cmd=(podman run --rm --network=none --userns=keep-id --user "$(id -u):$(id -g)"
  --shm-size=1g --workdir /fable2/current/bin --hostname "$(uname -n)"
  --device /dev/dri/renderD128 "${pad[@]}"
  --volume "$ROOT:/fable2:rw" --volume "$GAME:/game:ro"
  --volume "$RUNTIME_DIR:$RUNTIME_DIR:rw"
  --volume /usr/lib:/run/host-vulkan/usr/lib:ro
  --volume /tmp/.X11-unix:/tmp/.X11-unix:rw
  --volume "$ICD:/tmp/fable2-radeon_icd.x86_64.json:ro"
  --env HOME=/fable2/userdata --env "XDG_RUNTIME_DIR=$RUNTIME_DIR"
  --env "DISPLAY=$disp" "${xauth[@]}" --env SDL_VIDEODRIVER=x11 --env SDL_JOYSTICK_LINUX_CLASSIC=1
  --env "PULSE_SERVER=unix:$RUNTIME_DIR/pulse/native"
  --env "LD_LIBRARY_PATH=/fable2/current/bin:/usr/lib:/run/host-vulkan/usr/lib"
  --env XDG_CACHE_HOME=/fable2/userdata/cache
  --env VK_DRIVER_FILES=/tmp/fable2-radeon_icd.x86_64.json
  --env VK_ICD_FILENAMES=/tmp/fable2-radeon_icd.x86_64.json
  "$IMAGE" ./fable_2
  --game_data_root=/game --gpu_plugin=xenos --vulkan_device=0
  --window_width=1280 --window_height=720 --fullscreen=true --mouse_look=false
  --guest_fps_log=true
  # 30 FPS (guest vblank pacing). For a 60 FPS test add --vsync=false here and set
  # high_tick_rate + higher_hf_tick_rate = true in userdata/fable2_config.toml
  # (tried 2026-11-16: more stutter on camera turns in the busy view).
  --frame_limit=60 --async_shader_compilation=false --vulkan_async_submit=true "$@")

if [[ "${FABLE2_LAUNCH_DRY_RUN:-0}" == 1 ]]; then
  printf 'DRY RUN (not launching):'
  printf ' %q' "${cmd[@]}"
  printf '\n'
  exit 0
fi

{
  printf '=== %s launch: release %s, display %s\n' "$(date --iso-8601=seconds)" \
    "$(readlink "$ROOT/current")" "$disp"
} >>"$LOG"
set +e
"${cmd[@]}" >>"$LOG" 2>&1
status=$?
set -e
printf '=== exit status %s at %s\n' "$status" "$(date --iso-8601=seconds)" >>"$LOG"
exit "$status"
