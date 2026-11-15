#!/usr/bin/env bash
# Usage: waitprof.sh <label>   -- attach to the running game (unit 4 wait profile).
# Step 1 (no root): per-thread CPU user/kernel, run-queue wait, context switches, R/S state.
# Step 2 (only when run with sudo): who wakes whom (perf sched), SIGSEGV write-watch
#   fault rate, and a 10 s CPU profile that includes kernel time.
set -euo pipefail
label=${1:-wait}
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$repo/out/perf/$label-$(date +%H%M%S); mkdir -p "$out"
pid=$(pgrep -x fable_2 | head -1); echo "pid=$pid -> $out"

python3 -I "$repo/tools/perf/waitprof.py" "$out" 20

if [[ $EUID -eq 0 ]]; then
  echo "== signals (write-watch faults are SIGSEGV), 5 s"
  perf stat -e 'signal:signal_deliver' -p "$pid" -o "$out/signals.txt" -- sleep 5 || true
  cat "$out/signals.txt" || true
  echo "== perf sched, 5 s"
  perf sched record -o "$out/sched.data" -p "$pid" -- sleep 5 2> "$out/sched.err" || true
  perf sched timehist -i "$out/sched.data" -s 2>/dev/null > "$out/sched-summary.txt" || true
  perf sched timehist -i "$out/sched.data" -w 2>/dev/null > "$out/sched-wakeups.txt" || true
  echo "== cycles incl. kernel, 10 s"
  perf record -e cycles -F 999 --call-graph lbr -p "$pid" -o "$out/perf-kernel.data" -- sleep 10 2> "$out/perf-kernel.err" || true
  chown -R "${SUDO_UID:-1000}:${SUDO_GID:-1000}" "$out"
else
  echo "(not root: skipped perf sched / signals / kernel profile. Re-run with sudo for those.)"
fi
echo "done -> $out"
