#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  echo "Usage: apply-sdk-patch.sh /path/to/isolated/rexglue-sdk-worktree" >&2
  exit 2
fi
SDK_ROOT="$(realpath "$1")"
EXPECTED_BASE="521aa5906e8aa138870bf8d36bd602eef590ed55"
PATCH="$(dirname "$(realpath "$0")")/patches/rexglue-draw-identity.patch"

if [[ "$(git -C "$SDK_ROOT" rev-parse HEAD)" != "$EXPECTED_BASE" ]]; then
  echo "SDK worktree is not at the recorded base $EXPECTED_BASE" >&2
  exit 2
fi
if [[ -n "$(git -C "$SDK_ROOT" status --porcelain)" ]]; then
  echo "SDK worktree is dirty; refusing to apply capture patch" >&2
  exit 2
fi
git -C "$SDK_ROOT" apply --check "$PATCH"
git -C "$SDK_ROOT" apply "$PATCH"
echo "Applied bounded draw-identity patch to isolated SDK worktree at $EXPECTED_BASE"
