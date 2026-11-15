#!/usr/bin/env bash
# Apply the reviewed names (docs/re/NAMES_EVIDENCE.tsv) to the LIVE Fable II
# Ghidra project, or revert them. Writes to the project, so:
#   1. stops the ghidra-mcp container (it holds the project lock; SIGTERM saves),
#   2. backs up the project to $HOST_GHIDRA/backups/<project>-<date>-<time>/,
#   3. runs ApplyFable2Names.java headless in a one-off container (same image/mounts),
#   4. restarts ghidra-mcp (also if step 3 fails).
#
#   tools/theseus/ghidra/apply_names.sh                    # apply, revert list -> docs/re/NAMES_GHIDRA_REVERT.tsv
#   tools/theseus/ghidra/apply_names.sh revert [list.tsv]  # undo
#
# Only functions still named FUN_... are renamed. Needs ~3 GB free RAM (FORCE=1 overrides).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
eval "$(python3 -I "$HERE/../target.py" shell)"
MODE="${1:-apply}"
REVERT_LIST="${2:-$REPO/docs/re/NAMES_GHIDRA_REVERT.tsv}"
WORK="$HOST_GHIDRA/exports/n3b"
IMAGE="$(docker inspect "$CONTAINER" --format '{{.Config.Image}}')"

avail_kb=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
if (( avail_kb < 3 * 1024 * 1024 )) && [[ "${FORCE:-}" != 1 ]]; then
  echo "Only $((avail_kb / 1024)) MB RAM available; need ~3 GB. Close something or rerun with FORCE=1." >&2
  exit 1
fi

mkdir -p "$WORK"
cp "$HERE/ApplyFable2Names.java" "$HOST_GHIDRA/scripts/"
if [[ "$MODE" == apply ]]; then
  cp "$REPO/docs/re/NAMES_EVIDENCE.tsv" "$WORK/names.tsv"
  ARGS=(apply /exports/n3b/names.tsv /exports/n3b/revert.tsv)
elif [[ "$MODE" == revert ]]; then
  cp "$REVERT_LIST" "$WORK/revert.tsv"
  ARGS=(revert /exports/n3b/revert.tsv)
else
  echo "usage: $0 [apply | revert [list.tsv]]" >&2; exit 2
fi

echo "Stopping $CONTAINER (saves the project) ..."
docker stop -t 180 "$CONTAINER" >/dev/null
trap 'echo "Restarting $CONTAINER ..."; docker start "$CONTAINER" >/dev/null' EXIT

BACKUP="$HOST_GHIDRA/backups/$PROJECT-$(date +%Y%m%d-%H%M%S)"
echo "Backing up to $BACKUP ..."
mkdir -p "$BACKUP"
cp -a "$HOST_GHIDRA/projects/$PROJECT".* "$BACKUP/"

echo "Running ApplyFable2Names $MODE in a one-off $IMAGE container (heap $MAXMEM) ..."
docker run --rm --entrypoint /opt/ghidra/support/analyzeHeadless \
  -e _JAVA_OPTIONS="-Xmx$MAXMEM" \
  -v "$HOST_GHIDRA/projects:/projects" -v "$HOST_GHIDRA/scripts:/scripts:ro" \
  -v "$HOST_GHIDRA/exports:/exports" \
  "$IMAGE" "/projects" "$PROJECT" -process "$PROGRAM" -noanalysis \
  -scriptPath /scripts -postScript ApplyFable2Names.java "${ARGS[@]}" 2>&1 \
  | tee "$WORK/$MODE.log" | grep -E "ApplyFable2Names|ERROR|Exception|Save succeeded|Unable to lock" || true

grep -q "Save succeeded" "$WORK/$MODE.log" || { echo "No 'Save succeeded' in $WORK/$MODE.log" >&2; exit 1; }
[[ "$MODE" == apply ]] && cp "$WORK/revert.tsv" "$REVERT_LIST" && echo "Revert list: $REVERT_LIST"
echo "Done. Backup: $BACKUP"
