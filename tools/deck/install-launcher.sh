#!/usr/bin/env bash
# (Re)create the Deck install folder and launcher. Safe to rerun: existing saves,
# configs and releases are never touched; launch.sh and the ICD file are replaced.
#
#   tools/deck/install-launcher.sh
#
# Creates on the Deck:
#   ~/Documents/fable2-native-experimental/{launch.sh, radeon_icd.x86_64.json,
#     releases/, staging/, launcher-backups/, userdata/{saves,cache,logs}}
#   userdata/fable2_config.toml   from config/fable2_config.toml (defaults: 30 FPS
#                                 tick rates; only if it doesn't exist yet)
#   ~/Documents/fable-2-recomp/launch.sh and run-release.sh
#                                 small wrappers that exec the launcher (the Steam
#                                 shortcut "Fable II Recomp (Test)" points at launch.sh)
# Then deploy a build with tools/deck/deploy.sh <build-dir> <release-name>.
set -euo pipefail

DECK="${DECK:-deck@192.168.1.54}"
DECK_DIR="Documents/fable2-native-experimental"
ROOT="$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"

STAGE="$(mktemp -d)"
trap 'rm -rf -- "${STAGE:?}"' EXIT
install -m 0755 "$ROOT/tools/deck/launch.sh" "$STAGE/launch.sh"
cat > "$STAGE/radeon_icd.x86_64.json" <<'EOF'
{
  "file_format_version": "1.0.1",
  "ICD": {
    "library_path": "/run/host-vulkan/usr/lib/libvulkan_radeon.so",
    "api_version": "1.4.350"
  }
}
EOF
install -m 0644 "$ROOT/config/fable2_config.toml" "$STAGE/fable2_config.toml"
cat > "$STAGE/wrapper.sh" <<'EOF'
#!/usr/bin/env bash
# Starts the Steam Deck build (~/Documents/fable2-native-experimental, see
# tools/deck/launch.sh in the repo). This checkout only provides the game content.
exec "$HOME/Documents/fable2-native-experimental/launch.sh" "$@"
EOF

ssh "$DECK" "mkdir -p $DECK_DIR/staging/.launcher"
rsync -a "$STAGE/" "$DECK:$DECK_DIR/staging/.launcher/"
ssh "$DECK" "set -euo pipefail; cd $DECK_DIR
mkdir -p releases staging launcher-backups userdata/saves userdata/cache userdata/logs
s=staging/.launcher
if [ -e launch.sh ] && ! cmp -s launch.sh \$s/launch.sh; then
  cp -p launch.sh launcher-backups/launch.sh.before-install-\$(date +%Y%m%d-%H%M%S)
fi
install -m 0755 \$s/launch.sh launch.sh
install -m 0644 \$s/radeon_icd.x86_64.json radeon_icd.x86_64.json
if [ -e userdata/fable2_config.toml ]; then
  echo 'userdata/fable2_config.toml exists: kept as is'
else
  install -m 0644 \$s/fable2_config.toml userdata/fable2_config.toml
  echo 'userdata/fable2_config.toml created (repo defaults)'
fi
g=\$HOME/Documents/fable-2-recomp
for w in launch.sh run-release.sh; do
  if [ -e \$g/\$w ] && ! cmp -s \$g/\$w \$s/wrapper.sh; then
    cp -p \$g/\$w launcher-backups/fable-2-recomp-\$w.before-install-\$(date +%Y%m%d-%H%M%S)
  fi
  install -m 0755 \$s/wrapper.sh \$g/\$w
done
rm -rf \$s
echo installed: \$(pwd)/launch.sh
if [ -e current ]; then
  XDG_RUNTIME_DIR=/run/user/\$(id -u) FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh >/dev/null && echo 'launcher dry run OK'
else
  echo 'no release yet: deploy one with tools/deck/deploy.sh'
fi"
