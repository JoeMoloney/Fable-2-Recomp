#!/usr/bin/env bash
# Point the Deck's `current` symlink at an existing release (rollback / A-B between builds).
#
#   tools/deck/use-release.sh                 # list releases and show current
#   tools/deck/use-release.sh <release-name>  # switch; applies on the next launch
set -euo pipefail
DECK="${DECK:-deck@192.168.1.54}"
DECK_DIR="Documents/fable2-native-experimental"
if (($# == 0)); then
  ssh "$DECK" "cd $DECK_DIR && ls releases && echo current=\$(readlink current)"
  exit 0
fi
NAME="$1"
[[ "$NAME" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad release name: $NAME" >&2; exit 2; }
ssh "$DECK" "set -euo pipefail; cd $DECK_DIR
test -d releases/$NAME/bin || { echo 'no such release' >&2; exit 1; }
ln -sfn releases/$NAME current.new && mv -T current.new current
echo current=\$(readlink current)"
