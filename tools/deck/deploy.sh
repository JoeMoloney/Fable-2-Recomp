#!/usr/bin/env bash
# Deploy a Deck container build to the Steam Deck as a new release and make it current.
#
#   tools/deck/deploy.sh <build-dir> <release-name> [note]
#
# <build-dir>  e.g. out/build/deck-linux-amd64-release (from
#              ../Fable-2-native-vulkan-renderer/tools/deck-build/build.sh)
# <release-name> e.g. deck-perf-20261115-r7 (must not exist on the Deck yet)
#
# Copies fable_2 + librexgpu-xenos.so + librexruntime.so to
# ~/Documents/fable2-native-experimental/staging/<name>, verifies sha256 on the
# Deck, copies it into releases/<name>, adds the userdata symlinks, flips the
# `current` symlink and runs the launcher dry run. A running game is not
# affected (it keeps the release it started with). Launcher flags: see flags.sh.
# Rollback: tools/deck/use-release.sh <older-release-name>.
set -euo pipefail

DECK="${DECK:-deck@192.168.1.54}"
DECK_DIR="Documents/fable2-native-experimental"
IMAGE="localhost/fable2-native-vulkan:arch-20261018-r3"

if (($# < 2)); then
  sed -n '2,16p' "$0" >&2
  exit 2
fi
ROOT="$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
BUILD="$1"; [[ "$BUILD" = /* ]] || BUILD="$ROOT/$BUILD"
NAME="$2"
NOTE="${3:-}"
[[ "$NAME" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad release name: $NAME" >&2; exit 2; }
for f in fable_2 librexgpu-xenos.so librexruntime.so; do
  [[ -f "$BUILD/$f" ]] || { echo "missing $BUILD/$f" >&2; exit 2; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf -- "${STAGE:?}"' EXIT
mkdir "$STAGE/$NAME" "$STAGE/$NAME/bin"
install -m 0755 "$BUILD/fable_2" "$BUILD/librexgpu-xenos.so" "$BUILD/librexruntime.so" \
  "$STAGE/$NAME/bin/"
{
  echo "image=$IMAGE"
  echo "image_id=$(podman image inspect "$IMAGE" --format '{{.Id}}')"
} > "$STAGE/$NAME/container-image.txt"
{
  echo "release=$NAME"
  echo "build_dir=$BUILD"
  echo "source_commit=$(git -C "$ROOT" rev-parse HEAD) sdk=$(git -C "$ROOT/thirdparty/rexglue-sdk" rev-parse --short HEAD)"
  echo "source_dirty_files=$(git -C "$ROOT" diff --name-only -- src | tr '\n' ' ') sdk:$(git -C "$ROOT/thirdparty/rexglue-sdk" diff --name-only | tr '\n' ' ')"
  echo "cxx_flags=$(sed -n 's/^CMAKE_CXX_FLAGS:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)"
  echo "note=$NOTE"
  echo "deployed=$(date -u +%FT%TZ)"
  echo "sha256:"
  (cd "$STAGE/$NAME/bin" && sha256sum fable_2 librexgpu-xenos.so librexruntime.so)
} > "$STAGE/$NAME/BUILD_INFO.txt"
cat "$STAGE/$NAME/BUILD_INFO.txt"

ssh "$DECK" "test ! -e $DECK_DIR/releases/$NAME" ||
  { echo "release $NAME already exists on the Deck" >&2; exit 2; }
rsync -a "$STAGE/$NAME/" "$DECK:$DECK_DIR/staging/$NAME/"
ssh "$DECK" "set -euo pipefail; cd $DECK_DIR
(cd staging/$NAME/bin && sha256sum -c --quiet <(sed -n '/^sha256:/,\$p' ../BUILD_INFO.txt | tail -n +2))
echo 'checksums OK on Deck'
cp -a staging/$NAME releases/$NAME
for l in cache fable2_config.toml fable_2.toml logs saves; do ln -s ../../../userdata/\$l releases/$NAME/bin/\$l; done
ln -sfn releases/$NAME current.new && mv -T current.new current
echo current=\$(readlink current)
XDG_RUNTIME_DIR=/run/user/\$(id -u) FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh >/dev/null && echo 'launcher dry run OK'"
