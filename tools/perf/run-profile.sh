#!/usr/bin/env bash
# Same as run-release.sh plus a MangoHud overlay. Shift+F2 starts a 30 s frame-time log.
# Extra game flags (e.g. --readback_memexport=false) can be appended as arguments.
set -euo pipefail
cd "$(dirname "$0")/../.."
export SDL_VIDEODRIVER=wayland
export MANGOHUD=1
export MANGOHUD_CONFIG="fps,frametime,frame_timing,cpu_stats,gpu_stats,core_load,position=top-right,output_folder=$PWD/out/perf/mangohud,log_duration=30"
exec ./out/build/linux-amd64-release/fable_2 \
  --game_data_root="$PWD" \
  --gpu_plugin=xenos --vulkan_device=0 \
  --window_width=1280 --window_height=720 \
  --fullscreen=false --mouse_look=false \
  --frame_limit=60 --async_shader_compilation=false "$@"
