#!/usr/bin/env bash
set -euo pipefail

cd /mnt/HDD/git_stuff/my_git/Fable-2-Recomp

mode="debug"
# For the other scripts: mode="debug" or mode="relwithdebinfo"

export SDL_VIDEODRIVER=wayland

exec "./out/build/linux-amd64-$mode/fable_2" \
  --game_data_root="$PWD" \
  --gpu_plugin=xenos --vulkan_device=0 \
  --window_width=1280 --window_height=720 \
  --fullscreen=false --mouse_look=false \
  --frame_limit=30 --async_shader_compilation=false
