#!/usr/bin/env bash
# Read-only snapshot of the running game on the Steam Deck (~12 s):
# release + game flags, GPU clock / busy / APU power, per-thread user vs kernel
# CPU (high kernel = spin/yield or syscalls), GPU Commands wait state, and the
# newest *_stats lines from the game log.
#
#   tools/deck/measure.sh
set -euo pipefail

DECK="${DECK:-deck@192.168.1.54}"

ssh "$DECK" 'set -u
D=~/Documents/fable2-native-experimental
P=$(pgrep -x fable_2) || { echo "game not running"; exit 1; }
echo "release: $(readlink $D/current)"
echo "flags:   $(tr "\0" " " < /proc/$P/cmdline | tr " " "\n" | grep -E "^--" | grep -vE "^--(game_data_root|gpu_plugin|vulkan_device|window_|fullscreen|mouse_look)" | tr "\n" " ")"
for h in /sys/class/hwmon/hwmon*; do [ "$(cat $h/name)" = amdgpu ] && H=$h; done
for i in $(seq 40); do
  echo "$(cat /sys/class/drm/card*/device/gpu_busy_percent | head -1) $(( $(cat $H/power1_average)/1000000 )) $(grep "\*" /sys/class/drm/card*/device/pp_dpm_sclk | head -1 | awk "{print \$2}" | tr -d Mhz)"
  sleep 0.1
done | awk -v cap=$(( $(cat $H/power1_cap)/1000000 )) "{s+=\$1; p+=\$2; c+=\$3; n++} END{printf \"GPU: busy %d%%, clock %d MHz, APU power %d W (cap %d W)\n\", s/n, c/n, p/n, cap}"
snap(){ for t in /proc/$P/task/*; do echo "$(basename $t)|$(cat $t/comm)|$(sed "s/(.*)/X/" $t/stat | awk "{print \$14, \$15}")"; done; }
G=""; for t in /proc/$P/task/*; do case "$(cat $t/comm)" in "GPU Commands"*) G=$(basename $t);; esac; done
snap > /tmp/fm_a
w=$(for i in $(seq 250); do cat /proc/$P/task/$G/wchan; echo; sleep 0.04; done | sort | uniq -c | sort -rn | awk "{printf \"%s=%d \", (\$2==\"0\"?\"running\":\$2), \$1*100/250}")
snap > /tmp/fm_b
echo "threads over ~10 s (% of one core):"
join -t"|" <(sort /tmp/fm_a) <(sort /tmp/fm_b) | awk -F"|" "{split(\$3,a,\" \"); split(\$5,b,\" \"); u=(b[1]-a[1]); s=(b[2]-a[2]); if (u+s>30) printf \"  %-18s user %3d%%  kernel %3d%%\n\", \$2, u/10, s/10}" | sort -k3 -nr
echo "GPU Commands state (%): $w"
L=$D/userdata/logs/$(ls -t $D/userdata/logs | grep "^fable_2_.*log$" | head -1)
echo "log: $L"
grep -E "_stats" "$L" | tail -4'
