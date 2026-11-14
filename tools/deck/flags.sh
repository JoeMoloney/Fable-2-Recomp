#!/usr/bin/env bash
# Set (or clear) extra game flags in the Deck's Steam launcher for an A/B run.
#
#   tools/deck/flags.sh --gpu_constant_write_stats=true --readback_memexport_stats=true
#   tools/deck/flags.sh            # no arguments = clean launcher (no extra flags)
#
# Rewrites the last game-flag line of ~/Documents/fable2-native-experimental/launch.sh
# (the one with --vulkan_async_submit=true) atomically, so a running game is not
# affected; the flags apply on the next launch from the Steam shortcut. The first
# change of each day backs the launcher up to launcher-backups/.
set -euo pipefail

DECK="${DECK:-deck@192.168.1.54}"
DECK_DIR="Documents/fable2-native-experimental"
BASE='  --frame_limit=60 --async_shader_compilation=false --vulkan_async_submit=true'

extra=""
for a in "$@"; do
  [[ "$a" =~ ^--[a-z0-9_]+=[A-Za-z0-9_.,:-]*$ ]] || { echo "refusing odd flag: $a" >&2; exit 2; }
  extra+=" $a"
done
line="$BASE$extra \"\$@\")"
[[ -n "$extra" ]] && line+="  # TEMP A/B flags (tools/deck/flags.sh)"

ssh "$DECK" "set -euo pipefail; cd $DECK_DIR
n=\$(grep -c -- '--vulkan_async_submit=true' launch.sh)
[ \"\$n\" = 1 ] || { echo 'launcher flag line not found exactly once; edit by hand' >&2; exit 1; }
b=launcher-backups/launch.sh.before-flags-\$(date +%Y%m%d)
[ -e \"\$b\" ] || cp -p launch.sh \"\$b\"
awk -v l='$line' '/--vulkan_async_submit=true/ {print l; next} {print}' launch.sh > launch.sh.new
bash -n launch.sh.new && chmod 0755 launch.sh.new && mv launch.sh.new launch.sh
XDG_RUNTIME_DIR=/run/user/\$(id -u) FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh | tr ' ' '\n' | grep -E '^--' | tail -n +7"
