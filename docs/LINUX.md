# Experimental native Linux support

The native Linux executable has been built and user-tested on CachyOS x86_64,
KDE Wayland, an Intel i9-10900K and NVIDIA RTX 3090 (driver 615.78.08). Title
screen, main-menu navigation, a new-game session and Steam Controller input
have been observed working. This is **experimental**, not a stable-support or
performance guarantee. No Wine/Proton is used; SDL's Wayland backend is explicit.

## Requirements

- Your own supported, extracted game dump (`default.xex` plus `data/`). The
  executable must match `constants/game_versions.json`; the tested dump was
  GOTY USA/Europe. Do not use a hash bypass to substitute an unknown revision.
- Git, Python 3.11+, CMake 3.25+, Ninja and Clang with a C++23-capable standard
  library. Tested with Clang 23.1.3 and CMake 4.4.4. The helper selects
  `clang`/`clang++` rather than requiring the presets' versioned `clang-20` names.
- x86_64 CPU supporting the SDK's `x86-64-v2` baseline (SSE4.2/SSSE3, etc.).
  This workflow is not validated for ARM64 or older CPUs.
- Working Vulkan loader and a GPU driver satisfying the SDK's feature checks.
  `vulkaninfo --summary` should list the intended GPU. A successful summary
  alone does not prove every feature required by the renderer is available.
- X11/XCB and Wayland development headers/libraries, Wayland protocols/scanner,
  xkbcommon and audio development libraries. SDL is built from the pinned SDK
  sources with Wayland, X11, ALSA, PulseAudio and PipeWire support.

Typical Arch/CachyOS development packages include `base-devel`, `clang`, `lld`,
`cmake`, `ninja`, `python`, `git`, `pkgconf`, `wayland`, `wayland-protocols`,
`libxkbcommon`, `libx11`, `libxcb`, `libxrandr`, `libxcursor`, `libxi`, `alsa-lib`,
`libpulse`, `pipewire` and `libdecor`. Check configure output for missing
dependencies; install the correct Vulkan driver for your hardware separately.
The helper does not install packages, change driver settings or run `sudo`.

## Build from the repository root

```sh
# Independent writable copy; originals remain untouched. Allow ~7 GB for data.
python tools/linux.py stage /path/to/your/extracted/FableII

# Initializes pinned SDK dependencies, applies the reviewed SDK patch,
# configures, generates guest C++, reconfigures and builds Release.
python tools/linux.py build --jobs 6
```

Build/generated outputs need additional disk space. Use fewer jobs if memory
is constrained. `--cc` and `--cxx` select alternative Clang executable paths;
`CC` and `CXX` are also respected. No Windows build defaults are changed.

The staging command checks the XEX SHA-256/size and required/conflicting content
markers. Existing local data is checked for source-file presence and sizes,
not fully rehashed; local manifest updates and extra Lua scripts are preserved.
Conflicting XEXs, partial staging and symlinked data are rejected rather than
overwritten. Source and checkout must be separate, non-nested directories.
No hard links or symlinks into the original dump are created.

This matters because Lua staging and startup manifest self-healing **write**
inside the selected content tree. Never manually point the runtime or a `data/`
symlink at a dump you need to preserve unchanged. Saves/cache stay beside the
executable, not in the original dump.

### SDK provenance

Keep `thirdparty/rexglue-sdk` at the parent repository's pin. Its runtime/renderer
fixes are not interchangeable with arbitrary upstream or prebuilt SDK versions.
The helper refuses to reset an SDK or dependency checkout at a different revision.

The pinned SDK vendors libmspack as regular files but has a stale `.git` check.
`thirdparty/rexglue-sdk-linux-vendored-mspack.patch` fixes that check. The helper
applies it only if needed and refuses a non-applicable patch; it never resets
SDK edits. Consequently `git status` reports a modified SDK submodule even
after a successful build. This is expected: the parent tracks the patch, not
an unpublished submodule commit. Patch application can also be done manually:

```sh
git -C thirdparty/rexglue-sdk apply --check ../rexglue-sdk-linux-vendored-mspack.patch
git -C thirdparty/rexglue-sdk apply ../rexglue-sdk-linux-vendored-mspack.patch
```

Skip manual application if already patched. A reverse `--check` verifies that
state without modifying files. Existing unrelated SDK edits are not discarded.

## Run

```sh
python tools/linux.py run
```

Baseline: **native Wayland**, Vulkan plugin `xenos`, GPU index 0, windowed
1280x720, 30-FPS host limit, mouse-look disabled and
`--async_shader_compilation=false`. The process is **not timed out**; close the
window when finished. The host limit is not proof of achieved 30 FPS.

The synchronous-shader setting is a provisional workaround: async enabled
produced white/silent windows on both Wayland and XWayland, while disabling it
allowed interactive menus and a usable new-game session. The underlying async
bug is not fixed. Synchronous pipeline creation may hitch the first time a
shader/pipeline is used. Do not delete shader caches as a routine workaround.

```sh
# Check the runtime device listing and select a different GPU when necessary.
python tools/linux.py run --gpu 0

# Diagnostic comparisons only:
python tools/linux.py run --backend x11
python tools/linux.py run --async-shaders

# Additional game cvars after -- override the baseline when explicitly requested.
python tools/linux.py run -- --mouse_look=true
```

X11 under a Wayland desktop is XWayland: useful for isolating bugs, not proof of
native Wayland presentation. GPU enumeration may differ between systems; index
0 was the RTX 3090 on the test machine. Avoid launching duplicate instances
against the same saves directory. Back up `out/build/linux-amd64-release/saves/`
before save/load testing or changing builds.

Keyboard fallback for Xbox A is `E` or `Left Shift` in the supplied mapping.
Steam Controller input worked in the user-tested session, but not every button
was mapped (SDL logged unknown HID buttons). Steam Input and direct SDL HIDAPI
are different paths; controller discovery alone does not prove button delivery.
Steam configuration changes are not automated by this workflow.

## Logs and tests

- Runtime: `out/build/linux-amd64-release/logs/fable_2_*.log`.
- Helper launch console: `out/linux-logs/console-*.log` (one per run).
- Build: console output, optionally captured with:

  ```sh
  mkdir -p out
  python tools/linux.py build --jobs 6 > out/linux-build.log 2>&1
  ```

- `FABLE2_FPS_METER=1 python tools/linux.py run` enables the existing coarse
  guest-main-loop rate meter; it is not a presented-frame-time profiler.
- `python -m unittest discover -s tests -p 'test_*.py'` covers catalogue,
  staging and helper safeguards with synthetic fixtures.
- [Native diagnostic read regression test](../tests/README.md#linux-diagnostic-read-check).
- [Detailed port progress and evidence](LINUX_PORT_PROGRESS.md).

If GDB sibling attach is denied by Yama, launch the next diagnostic as GDB's
child instead of weakening system-wide ptrace settings. Guest SIGSEGV handling
must be passed through to the runtime; debugger stops can otherwise resemble
game freezes. Time-bounded diagnostic runs ending with timeout status 124 are
not, by themselves, game crashes.

## Known limits / next validation

- Some hitching was reported. Measure cold/warm frame times on a repeatable
  scene before attributing every hitch to shader compilation or tuning FPS.
- Runtime logged `BaseHeap::Release failed because address is not a region
  start`; execution continued. Investigate rather than treating the run as
  error-free.
- Saves were generated, but successful reload/integrity is not yet verified.
  Audio endpoint initialization is logged; audible/correct sound still needs
  explicit confirmation.
- Long sessions, cinematics and repeatable startup need more testing. An idle
  cinematic freeze was suspected, but the user subsequently started a new game;
  a hard-lock cause is not established.
- Optional Windows-only heap/font/exception diagnostics are guarded out on
  Linux; the shared UI diagnostic read path is covered by a Linux test.
- The existing build-provenance log may say `linked_sdk=external` even for
  `REXSDK_DIR` source builds; it only keys off the standalone-codegen override.
- Current changes have not been rebuilt/tested on Windows; Windows support is
  intended to remain unchanged, not newly certified here.
