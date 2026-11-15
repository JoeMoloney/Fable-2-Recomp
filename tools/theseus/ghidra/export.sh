#!/usr/bin/env bash
# Export Ghidra naming evidence (strings + referencing functions, RTTI vtables,
# function list) for the Fable II program, read-only, into out/theseus/ghidra/.
#
#   tools/theseus/ghidra/export.sh
#
# Runs Ghidra headless inside the existing `ghidra-mcp` container on a COPY of
# the project, because the MCP server keeps the live project locked. The copy is
# deleted afterwards. Needs ~3 GB of free RAM for a few minutes: close big games
# first (MAXMEM below sets the Java heap).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# Settings come from the target file (THESEUS_TARGET, default tools/theseus/targets/fable2.toml).
eval "$(python3 -I "$HERE/../target.py" shell)"
COPY_DIR="export-copy-$$"
OUT_NAME="fable2-evidence"

avail_kb=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
if (( avail_kb < 3 * 1024 * 1024 )) && [[ "${FORCE:-}" != 1 ]]; then
  echo "Only $((avail_kb / 1024)) MB RAM available; need ~3 GB. Close something or rerun with FORCE=1." >&2
  exit 1
fi

cleanup() { rm -rf "$HOST_GHIDRA/projects/$COPY_DIR"; }
trap cleanup EXIT

echo "Copying project (read-only source) ..."
mkdir -p "$HOST_GHIDRA/projects/$COPY_DIR"
cp -a "$HOST_GHIDRA/projects/$PROJECT.gpr" "$HOST_GHIDRA/projects/$PROJECT.rep" \
      "$HOST_GHIDRA/projects/$COPY_DIR/"
cp "$HERE/ExportFable2Evidence.java" "$HOST_GHIDRA/scripts/"
rm -rf "$HOST_GHIDRA/exports/$OUT_NAME"

echo "Running Ghidra headless in $CONTAINER (heap $MAXMEM) ..."
docker exec -e _JAVA_OPTIONS="-Xmx$MAXMEM" "$CONTAINER" \
  /opt/ghidra/support/analyzeHeadless "/projects/$COPY_DIR" "$PROJECT" \
  -process "$PROGRAM" -readOnly -noanalysis \
  -scriptPath /scripts -postScript ExportFable2Evidence.java "/exports/$OUT_NAME" \
  2>&1 | grep -E "ExportFable2Evidence|ERROR|Exception|functions:|strings:|rtti|written" || true

mkdir -p "$OUT_DIR"
cp "$HOST_GHIDRA/exports/$OUT_NAME/"*.jsonl "$OUT_DIR/"
wc -l "$OUT_DIR/"*.jsonl
