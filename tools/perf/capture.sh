#!/usr/bin/env bash
# Usage: capture.sh <label>   -- attaches to the running game for 20 s.
set -euo pipefail
label=${1:-run}; out=$(cd "$(dirname "$0")/../.." && pwd)/out/perf/$label-$(date +%H%M%S); mkdir -p $out
pid=$(pgrep -x fable_2 | head -1); echo "pid=$pid -> $out"
# per-thread CPU snapshot before/after (utime+stime ticks)
# (comm can contain spaces, e.g. "GPU Commands (F", so parse stat after the last ')')
snap(){ for t in /proc/$pid/task/*; do printf '%s\t%s\t' "${t##*/}" "$(cat $t/comm)"; sed 's/.*) //' $t/stat | awk '{print $12+$13}'; done; }
snap > $out/threads-start.tsv
perf record -e cycles:u -F 1999 --call-graph lbr --user-callchains -p $pid -o $out/perf.data -- sleep 20 2> $out/perf.err
snap > $out/threads-end.tsv
tail -1 $out/perf.err
