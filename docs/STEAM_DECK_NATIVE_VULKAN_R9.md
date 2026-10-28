# Steam Deck native Vulkan deployment checkpoint — r9

**Date:** 2026-10-28  
**Status:** r9 was manually tested by the user and reported to look good. The
current Deck shortcut targets r9; no game process is running.

This is the working Steam Deck configuration for continuing Vulkan renderer
development. It is **native Vulkan on RADV through XWayland**, inside the
pinned Arch Podman runtime. Native Wayland remains an optional comparison
mode, but it is not the Steam Deck baseline because Steam/Gamescope focus and
window identification were unreliable there.

## Current baseline

- Stable Steam Non-Steam entry point:
  `/home/deck/Documents/fable2-native-experimental/launch.sh`
- Current release:
  `/home/deck/Documents/fable2-native-experimental/releases/deck-native-vulkan-20261028-r9`
- The stable shortcut path did not change. r2, r4, r5, r6, r7, and r8 remain
  installed for rollback; r9 does not overwrite them.
- Runtime image: `localhost/fable2-native-vulkan:arch-20261018-r3`, pinned
  image ID `929cd06a66e2eb3959391bfb4ef1695690af3b6610b21b61ee1d9f0faad035cd`.
- Default SDL backend: X11/XWayland on `DISPLAY=:1`, mounting the host
  `/tmp/.X11-unix` directory. Wayland is an optional override:
  `FABLE2_SDL_VIDEO_DRIVER=wayland`.
- Input: `SDL_JOYSTICK_LINUX_CLASSIC=1` selects Steam Input's virtual pad at
  `/dev/input/js0`. The Steam Overlay is enabled for experimental AppID
  `3341229613` (`OverlayAppEnable=1`); the user changed that setting.
- Podman memory: `--shm-size=1g`, because the default 64 MiB `/dev/shm` was
  below the guest-memory workload.
- Game data is mounted read-only from
  `/home/deck/Documents/fable-2-recomp`. Experimental saves, cache, logs, and
  configuration live under
  `/home/deck/Documents/fable2-native-experimental/userdata/`. The user copied
  existing save data into the experimental saves folder; production saves were
  not modified.
- The temporary `userdata/launch-xwayland-test` selector is removed. X11 is
  now the default and Wayland is opt-in. The prior launcher is backed up under
  `launcher-backups/`.

## Build and release workflow

The r9 Release build was produced on the desktop using the existing Deck
build script and pinned builder image, in a fresh output directory:

```sh
FABLE2_BUILDER_IMAGE=localhost/fable2-native-vulkan:arch-20261018-r3 \
FABLE2_BUILD_DIR=out/build/deck-native-vulkan-release-r9-clean \
FABLE2_BUILD_TYPE=Release FABLE2_BUILD_JOBS=8 \
FABLE2_EXPERIMENTAL_NATIVE_VULKAN=ON \
FABLE2_CODEGEN_TOOL=/workspace/out/deck-build/codegen-tools/rexglue \
FABLE2_CODEGEN_LD_LIBRARY_PATH=/workspace/out/deck-build/codegen-tools \
bash tools/deck-build/build.sh
```

The recompiler needs `default.xex` in the repo root. For r9, it was copied
temporarily from the sibling `../Fable-2-Recomp/default.xex`; the file was
removed afterward. The package was created with the existing packaging script
(`--allow-dirty` because the working tree contains uncommitted renderer work):

```sh
FABLE2_PACKAGE_TAG=deck-native-vulkan-20261028-r9 \
FABLE2_BUILDER_IMAGE=localhost/fable2-native-vulkan:arch-20261018-r3 \
FABLE2_BUILDER_MANIFEST=out/deck-build/native-vulkan-20261028-r3/container-manifest.txt \
bash tools/deck-build/package.sh --allow-dirty \
  out/build/deck-native-vulkan-release-r9-clean/fable_2_native \
  out/build/deck-native-vulkan-release-r9-clean/librexgpu-xenos.so \
  out/build/deck-native-vulkan-release-r9-clean/librexruntime.so
```

Artifact: `out/artifacts/fable2-experimental-deck-native-vulkan-20261028-r9.tar.gz`  
SHA-256: `cd6f5796ebf5bfe820651f2b2935743cd2f12f506ea1872ea9b6d57d66de34e4`

Deployment used a new versioned Deck release directory, verified the archive
and embedded file hashes, installed user-data symlinks, and atomically switched
`current`. No existing release was overwritten. The runtime image is already
loaded on the Deck; no image rebuild is required for ordinary r9 updates.

## What the Deck tests established

- r2 failed because SDL was built without its Wayland video driver. Adding
  `libglvnd` and the Wayland/X11 development packages to the pinned builder
  enabled both SDL backends. Those builder dependencies remain in
  `tools/deck-build/Containerfile`.
- The 64 MiB default Podman shared-memory mount was too small for ReXGlue's
  guest memory. A logged failing memset covered guest address
  `0xFECFE000` for `0x00397800` bytes. The exact SIGBUS address was not
  captured; the 1 GiB shm limit is retained as the practical fix.
- On native Wayland, Steam placed the game behind its loading screen. A
  temporary Gamescope base-layer reorder exposed the game and triangle, but
  Steam restored its order at the next launch. No focus override is baked into
  r9.
- XWayland was tested with the same r8 executable, Podman image, AppID, game
  data, and controller configuration. The Deck's `:1` XWayland socket was
  reachable from the same container image. The user confirmed the game
  appeared automatically, controls worked, the Steam menu opened, and the
  triangle was visible. r9 keeps that XWayland mode as the default.
- SteamOS/Gamescope was `3.16.30.1-2`. Steam assigned the shortcut AppID
  `3341229613`; Overlay is enabled for that app. The r9 app log
  `userdata/logs/fable_2_013.log` records X11, RADV VANGOGH, a 1280x720 Vulkan
  swapchain, triangle pipeline/draw, and SDL controller enumeration. The
  launcher recorded exit status 0; no coredump was produced.
- SDL's Wayland app ID likely falls back to the executable name
  `fable_2_native`: the launcher does not set `SDL_APP_ID` or forward Steam's
  app ID. This is a possible cause of the remaining native-Wayland focus
  issue, not a confirmed protocol trace.

## r9 cleanup

Removed from the r9 source/build:

- Temporary `[present-probe]` logging from
  `thirdparty/rexglue-sdk/src/ui/vulkan/vulkan_presenter.cpp`.
- Temporary `[sigbus-probe]` logging/handler changes from
  `thirdparty/rexglue-sdk/src/core/exception_handler_posix.cpp`.

The native triangle renderer remains in the Vulkan plugin. Existing unrelated
renderer and SDK changes were preserved; in particular, the existing
`command_processor.cpp`/header changes were not cleaned or reset. Older r8
packages retain the diagnostic binaries for rollback/comparison.

## Renderer backlog: FPS discrepancy

The user reported 400+ FPS from Steam's overlay. The r8 Wayland presenter
probe separately counted about 143,000 `PaintAndPresentImpl` calls in 29
seconds (~4,900 host paints/s); XWayland also had a high host-paint rate.
Those are host UI/presenter calls, **not guest FPS**. Use the existing F3 guest
FPS/frame-time metric backed by `rex::perf::CounterId::kFrameTimeUs`
(`src/core/fable_2_app.h`) for guest-side measurements. Compare it with Steam's
overlay and host present rate in the same scene. Do not change frame pacing or
optimize the presenter until measured.

## Remaining build/reproducibility caveats

- Podman is required for the current r9 binary: it needs `GLIBCXX_3.4.36`, and
  its runtime needs `GLIBC_2.44`/`GLIBCXX_3.4.36`. The inspected Deck host has
  glibc 2.43 and GLIBCXX through 3.4.35. A direct native build is deferred
  until a SteamOS-compatible desktop sysroot/toolchain is established.
- The on-Deck `run-release.sh` currently changes directory to the absent
  `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp`; the available production
  `fable_2` binary also fails the current host ABI check. The prior reported
  working path could not be reproduced from the current filesystem snapshot.
- `tools/deck-build/launch.sh` was ignored by the repository-wide `*.sh`
  pattern. `.gitignore` now has an exception for this stable deployment
  launcher; it is present in the working tree and on the Deck. No commit was
  made, so include it in the next intentional source-control review.
- The Steam Overlay setting and the experimental save copy are per-user Deck
  state, not source-controlled. Keep Overlay enabled for AppID `3341229613`.

## Emergency stop

If Steam's menu fails, verify the container before stopping it:

```sh
podman ps --no-trunc
podman inspect <id> --format '{{.Config.Image}} {{.Path}} {{.Args}} {{range .Mounts}}{{.Source}} -> {{.Destination}}; {{end}}'
podman stop --time 15 <verified-r9-container-id>
```

For r8, this was used only after verifying the pinned image, `/app/fable_2_native`,
and the versioned experimental `/app` mount. It exited cleanly, preserved
logs/userdata, and Gamescope returned focus to Steam. Production data is
read-only in the container; production saves are not touched.
