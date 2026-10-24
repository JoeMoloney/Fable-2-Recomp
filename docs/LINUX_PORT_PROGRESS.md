# Native Linux / Wayland port progress

## Current checkpoint

Native Linux/Wayland Vulkan boot, interactive menus and a user-tested new-game
session with controller input work with `--async_shader_compilation=false`.
Continue loading the existing save, intro skip and in-game cutscene playback
are user-verified. Some hitching/visual anomalies and an earlier allocator
diagnostic remain; audio, persistence of new saves and stable long-session
performance are not yet verified. Earlier "pending" entries below
are chronological experiment notes, not the latest status. Start with the
[portable Linux workflow](LINUX.md) for current setup/run commands.

## Goal

Run this fork of Fable 2 Recompiled as a native Linux executable on CachyOS
(Arch Linux), using Vulkan, with stable gameplay and good frame pacing under
the user's Wayland compositor. No Wine/Proton runtime is the target. Native
Wayland presentation is a separate milestone from native Linux execution;
XWayland may be useful for comparison, not proof of native Wayland support.

## Baseline — 2026-10-24

- Checkout: `master`, commit `a4b0274` (two-controller black-screen fix).
  Tracked working tree was clean before this document; `.opencode/` was
  already untracked and is user-owned configuration.
- Machine: CachyOS x86_64, kernel `7.3.0-3-cachyos`, KDE Wayland session.
  Intel i9-10900K (10 cores / 20 threads), approximately 30 GiB RAM.
- GPU: NVIDIA RTX 3090, 24 GiB VRAM, NVIDIA driver `615.78.08`.
  Intel UHD 630 is also present; verify the game chooses the NVIDIA GPU.
- `vulkaninfo --summary` succeeds: NVIDIA Vulkan API `1.4.351`, instance
  version `1.4.363`, `VK_KHR_wayland_surface` available. This does not yet
  validate the SDK's Vulkan feature requirements or window presentation.
- CMake `4.4.4`, Ninja, Clang/Clang++ `23.1.3`, .NET, pkg-config and
  vulkaninfo are on PATH. The presets request `clang-20` / `clang++-20`,
  which are absent. Prefer a local compiler override before changing shared
  presets; installed Clang is newer than the SDK's minimum, not build-tested.

## Repository / SDK findings

- `README.md` marks Linux builds and Vulkan support unfinished, while
  `CMakePresets.json` already contains Linux AMD64 configurations.
- `generated/rexglue.cmake` accepts a source SDK via `REXSDK_DIR` or an
  installed SDK package. It wires code generation into the build.
- Submodule `thirdparty/rexglue-sdk` is uninitialized, pinned to
  `cc5f03dd0488720a0f277412d7c36b63964d5b18` from
  `https://github.com/himdo/rexglue-sdk.git`. Preserve that pin initially:
  this fork carries runtime/renderer fixes, so a random upstream SDK may
  regress behavior or mismatch generated code.
- The pinned SDK's README and root CMakeLists were inspected remotely at
  that exact commit. It explicitly supports Linux, requires Clang >=18 and
  C++23, defaults Vulkan ON and D3D12 OFF on non-Windows, and sets a large
  code model on Linux x86_64. Linux feasibility is supported by source,
  not yet by a successful local build of this game.
- `docs/RUNTIME_FIXES.md` states its recent fixes were not validated on
  other platforms or Vulkan. It also documents an unavailable libmspack
  revision and replacement `305907723a4e7ab2018e58040059ffb5e77db837`;
  check actual dependency configuration when initializing the SDK.
- Confirmed compile blocker:
  `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp` unconditionally
  includes `<windows.h>` and calls `WaitForSingleObjectEx`. Preserve guest
  register/memory behavior when making its host pause portable. Its timing
  comments also require review: Win32 wait timeout units are milliseconds,
  not the claimed 100 ns, and a null handle is not a valid timer wait.
- Other diagnostics and the Vulkan smoke tool contain Windows APIs; audit
  active include paths and CMake targets before treating every hit as a
  build blocker. Many other Windows uses are already guarded.
- GPU plugin comments and helper scripts are Windows-centric. Verify actual
  Linux plugin names, staging, dynamic loading and backend selection against
  the pinned SDK rather than copying Windows DLL launch instructions.
- The WPF launcher is Windows-specific; boot the native game executable
  directly first. A Linux launcher is not a prerequisite for gameplay.
- No `default.xex`, `data/`, or existing `out/build` directory was present
  in the project root. Game content may live elsewhere; ask for its path.
  Use the user's own supported dump, keep content outside version control,
  and retain version/hash checks.
- `build_release.status` says `BUILD_EXIT_CODE=0`, but is historical and
  does not establish a Linux build on this machine.

## Checks completed

- `python tools/generate_game_versions.py --check`: passed.
- `python -m unittest discover -s tests -p 'test_*.py'`: all 7 passed.
- NVIDIA driver/device discovery and Vulkan summary: passed.
- No SDK initialization, package installation, native compilation or game
  launch performed during this baseline. No smooth-gameplay claim yet.

## Milestones / next actions

### Build checkpoint — 2026-10-24

- Initialized the pinned SDK and its Linux-required submodules. No system
  packages installed; existing development libraries were sufficient to configure.
- Corrected a stale SDK dependency check in
  `thirdparty/rexglue-sdk/thirdparty/CMakeLists.txt`: libmspack is already
  vendored in this pinned revision, not a gitlink. Validate its `lzxd.c`
  instead of requiring a `.git` entry. This is a local SDK source modification,
  not a changed submodule commit; preserve/export it before moving checkouts.
  The earlier documented unavailable-pin workaround is unnecessary here.
- Guarded the two Windows-only diagnostic includes in `src/main.cpp`.
  `FrameLimiterWait_82242628.cpp` now uses `std::this_thread::yield()` on
  non-Windows hosts; Windows behavior is unchanged and inaccurate timing
  comments were corrected. Runtime pacing remains untested.
- Configuration succeeded with:

  ```sh
  cmake --preset linux-amd64-release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
    -DREXSDK_DIR="$PWD/thirdparty/rexglue-sdk" \
    -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF
  ```

  Log: `out/linux-configure.log`. SDL Wayland, X11, PipeWire and Vulkan are
  enabled. Tracy is disabled for the initial Release baseline.
- Started SDK/codegen and plugin build (not yet a successful build):

  ```sh
  cmake --build out/build/linux-amd64-release \
    --target rexglue rexgpu-xenos -j 6
  ```

  Log: `out/linux-sdk-build.log`. Six compile jobs chosen to limit memory use.
  First attempt reached step 604/899 before failing in SDK `memory.cpp`:
  `_mm_shuffle_epi8` requires SSSE3. The host preset omits the CPU baseline
  that the SDK's own Linux preset supplies. Reconfigured with
  `-march=x86-64-v2` for C and C++ (matching the SDK preset, supported by
  this CPU) and restarted compilation. This is not a Clang-version failure.
- Created ignored root `default.xex` symlink to the verified external XEX
  for codegen input. Did **not** symlink `data/`: the build stages Lua files
  there, and runtime manifest self-healing can write into the content root.
  Arrange a writable staging copy/overlay before first launch to keep the
  original dump unchanged.
- Fresh checkout has no `generated/default/` yet. Build codegen first, then
  reconfigure after generation so the game's recompilation target is included.
- **SDK build passed** after supplying the CPU baseline. Native ELF
  `rexglue`, `librexruntime.so` and `librexgpu-xenos.so` exist under
  `thirdparty/rexglue-sdk/out/linux-amd64/`; `ldd` finds their dependencies.
- Initial codegen rejected the XEX symlink because its resolved path escapes
  the manifest game root. Replaced only our own symlink with an ignored local
  copy of the verified XEX and restarted codegen; no containment check disabled.
- Started copying the original `data/` to ignored repository-local `data/`
  (`cp -a --reflink=auto --no-clobber`) for writable runtime/Lua staging,
  keeping the original dump untouched. Copy and codegen outcomes pending.
- **Content copy completed:** all 447 source files present with matching
  sizes, 6,966,456,750 bytes total (not a per-file hash verification).
- **Codegen completed:** 597 files written in 102.6 seconds. It reports
  unresolved branch/function targets (including `0x83040E28`, `0x83040FC0`,
  `0x83040DB8`) despite successful exit; retain `out/linux-codegen.log` and
  investigate if relevant at runtime rather than treating generation as proof
  of complete guest coverage.
- Reconfiguration after codegen succeeded. Started the full `fable_2` target
  with `-j 6`; log `out/linux-game-build.log`. Game compilation pending.
- First game compile exposed missing `imgui.h`: in-tree SDK public UI
  headers need an include directory not propagated by `rex::runtime`.
  Host CMake now imports the existing ImGui target's interface include
  directories without linking its object files twice. Build restarted.
- Subsequent game compile exposed more Windows-only optional probes. Guarded
  experimental heap scanning, delayed font dumps and text-canary page commits
  on Linux, while retaining the UI guest-function forwarding/hook path.
  Windows paths remain intact. These optional diagnostics are not yet fully
  ported and should not be represented as supported Linux features.
- Shared UI diagnostic reads now use Linux `process_vm_readv` on the current
  process, so invalid guest pointers fail without a host SIGSEGV. Added
  `tests/native/linux_ui_input_read.cpp` and commands in `tests/README.md`:
  valid, protected, cross-protection partial and unmapped read checks pass.
  Also fixed a missing non-Windows return in `dump_hop_on()`.
- Regenerated partition headers no longer declared two callees used by
  `GpuProgressCheck_82B9BF90.cpp`; added explicit extern declarations for
  `GetGlobalField_8221EE38` and `sub_82BA0E80` without guest-body changes.
  Full game compilation restarted. Seven Python tests still pass.
- Game compile reached 237/305 steps without new compiler errors, then the
  tool's 120-second timeout interrupted Ninja. Resumed the incremental build
  with tool timeout disabled (`timeout: 0`); no source rollback needed.
- **Full native Linux game build passed.**
  `out/build/linux-amd64-release/fable_2` is an x86_64 ELF PIE executable.
  Initial `ldd` reported `librexruntime.so => not found`: SDK stages the GPU
  plugin but its Linux helper uses `$ORIGIN` without copying the runtime.
  Added a Linux-only host POST_BUILD copy from `rex::runtime` beside the
  executable and started an incremental rebuild to stage it before launch.
- Runtime staging rebuild passed; `ldd` now resolves the executable's
  dependencies. Preserved the SDK dependency-check change as
  `thirdparty/rexglue-sdk-linux-vendored-mspack.patch`; reverse-apply check
  against the modified SDK succeeds. On a fresh pinned SDK, apply with
  `git -C thirdparty/rexglue-sdk apply ../rexglue-sdk-linux-vendored-mspack.patch`.

### First native Wayland launch — 2026-10-24

Ran a bounded 45-second startup test (windowed, mouse-look off):

```sh
timeout --signal=TERM --kill-after=5s 45s env SDL_VIDEODRIVER=wayland \
  ./out/build/linux-amd64-release/fable_2 \
  --game_data_root="$PWD" --gpu_plugin=xenos --vulkan_device=0 \
  --window_width=1280 --window_height=720 --fullscreen=false \
  --mouse_look=false --frame_limit=30
```

Evidence: `out/linux-first-boot.log` and
`out/build/linux-amd64-release/logs/fable_2_001.log`.

- SDL explicitly logs `video driver: wayland` (not XWayland).
- NVIDIA RTX 3090 selected; Vulkan instance/device initialization succeeds.
- VulkanPresenter creates a 1280x720 swapchain.
- Guest memory arenas mapped, function table initialized, supported XEX hash
  verified, guest execution and graphics-pipeline creation start.
- SDL input initializes and the analog stereo audio endpoint opens at 48 kHz
  with six submitted channels. Audible/correct sound not yet verified.
- Missing optional update/build-version/localization files logged. The
  `lang.ini` files really are absent in the source dump's language directories;
  don't assume Linux case-sensitivity is responsible for these warnings.
- Later log activity is GPU `WAIT_REG_MEM` long-wait reporting. No logged
  unhandled guest access violation in this run, but rendered progress and
  title/menu/gameplay are **not yet verified**; possible startup stall remains.
- Test exits 124 due to planned timeout; logs show window closing, execution
  complete and title termination, not a spontaneous crash or normal gameplay exit.
- Startup labels SDK `external` even though it is source-built: existing
  provenance macro only tests `FABLE2_CODEGEN_TOOL`, not `REXSDK_DIR`.
  This is a metadata limitation, not evidence of loading an official SDK.

Next: ask what appeared in the window; if black/frozen, diagnose startup progress
and GPU/guest waits before any performance claims or FPS tuning. No visual
capture, input injection, title-screen confirmation or gameplay test yet.

### Extended observation run

Started the same windowed native Wayland command with a 180-second timeout;
console log `out/linux-observe-boot.log`. User asked to observe visuals/audio.
Attempted a read-only GDB stack capture of this run, but host Yama policy
(`ptrace_scope=1`) denies sibling-process attach (`Operation not permitted`),
recorded in `out/linux-observe-stacks.log`. Do not weaken system-wide ptrace
settings: if stack traces are needed, launch the next test as GDB's child.
A one-time `/proc` task snapshot shows named guest GameThread and 3D Engine
threads plus GPU, SDL/PipeWire and audio workers; this establishes thread
creation, not successful rendering or lack of a deadlock. Observation pending.

**User visual confirmation:** the extended run displays **Press A to Start**.
This verifies rendered title-screen boot on native Linux with the forced SDL
Wayland backend and Vulkan on the RTX 3090. The prior GPU wait logs therefore
do not by themselves establish a startup stall. Main-menu navigation, input,
audible sound, loaded gameplay, save/load and performance remain unverified.
Next user action: Xbox A (keyboard `E` or `Left Shift` in the default mapping)
to check progression to the main menu. Extended run still has its planned
three-minute auto-close limit.

### Steam Controller observation run

The 180-second run ended with planned timeout exit 124. User confirms title
screen again and has now connected a Steam Controller. Previous run log
`fable_2_002.log` records `SDL OnControllerDeviceAdded: "Steam Controller"`,
connection order 0, device 1, so SDL recognized the hot-plugged controller.
Recognition alone does not confirm guest A-button delivery.

Relaunched the same native Wayland command **without** the timeout wrapper,
leaving the game open for manual controller/menu/gameplay testing. Console:
`out/linux-controller-test.log`; runtime: `logs/fable_2_003.log` beside the
executable. User can close the game window when finished. Asked user to press
controller A and report menu navigation and audio; results pending. Keyboard
`E`/`Left Shift` remains a fallback if controller input does not reach the game.

**Regression observation:** user reports just a white screen in the untimed
controller-connected run. Earlier title-screen confirmation remains valid,
but startup/rendering is not repeatably verified. `fable_2_003.log` shows
Wayland/Vulkan swapchain initialization and ongoing GPU wait reports, without
a logged fatal graphics error in the inspected output. Whether white appeared
immediately or after pressing A is not yet known. Do not attribute causality
to the controller or discard shader caches without evidence. Next establish
the transition/input behavior, then use a controlled comparison or a launch
under GDB if necessary (sibling attach blocked by Yama).

User reports no input response and silence in the white-screen run. Stopped
only that test PID after verifying its executable. User notes Steam Controller
may require Steam Input translation; no process named `steam` was running at
inspection. SDL discovery does not establish translated button delivery.
Keep keyboard E as input baseline; no Steam settings changed or Steam launched.

### Debugger snapshot and presentation comparison

Launched the game as GDB's child, preserving Yama policy. Used
`handle SIGSEGV nostop noprint pass` for runtime-managed guest faults, then
interrupted after 35 seconds and captured `thread apply all bt 18` in
`out/linux-debug-boot.log`. Timeout exit 124 is diagnostic control, not crash.

Snapshot findings (not proof of persistent deadlock):
- UI/main thread: NVIDIA `drmSyncobjTimelineWait` under
  `VulkanPresenter::PaintAndPresentImpl`.
- 3D Engine: `sched_yield` in `FrameLimiterWait_82242628` under guest render loop.
- GPU Commands: `ExecutePacketType3_WAIT_REG_MEM` host sleep.
- GameThread: object-table mutex lookup under `XNotifyGetNext`.
- Audio Worker: SDK wait-multiple path.

Started a controlled 90-second comparison with only
`SDL_VIDEODRIVER=x11` changed (XWayland under current Wayland desktop), same
native Linux executable/Vulkan GPU/content/resolution/input/frame-limit flags.
Log: `out/linux-xwayland-compare.log`. User visual/input/audio observation
pending; this is diagnostic isolation, not replacement of native Wayland goal.

**Comparison outcome:** user reports XWayland is also white and silent.
It ended with planned timeout 124. This weakens a native-Wayland-only cause;
does not rule out shared Vulkan presentation/synchronization issues.

Next single-option test: native Wayland baseline plus
`--async_shader_compilation=false`, 90-second timeout, console
`out/linux-sync-shaders-test.log`. The Vulkan backend can skip presenting
frames containing async placeholder draws; test synchronous pipeline creation
without deleting caches or modifying saved configuration. Outcome pending;
no claim that shader compilation is the cause.

### Synchronous shaders: interactive main-menu milestone

**User confirms** the native Wayland run with
`--async_shader_compilation=false` worked and allowed up/down main-menu
navigation. User thought an idle cinematic/cutscene started before the window
closed. Runtime `fable_2_006.log` shows shutdown at the planned 90-second
deadline; wrapper exit 124 agrees. This is **not a confirmed cutscene crash**.
Input device used for navigation and audible sound not yet confirmed. Some
unmapped SDL HID buttons (23/24/25) are logged, despite working menu navigation.

Treat synchronous shader compilation as a **provisional working workaround**,
not a proved root-cause fix: repeat runs and loaded gameplay are still needed.
No caches deleted, configuration defaults unchanged.

Relaunched without a timer to test cinematics/new-game/gameplay:

```sh
env SDL_VIDEODRIVER=wayland ./out/build/linux-amd64-release/fable_2 \
  --game_data_root="$PWD" --gpu_plugin=xenos --vulkan_device=0 \
  --window_width=1280 --window_height=720 --fullscreen=false \
  --mouse_look=false --frame_limit=30 --async_shader_compilation=false
```

Console `out/linux-gameplay-test.log`, background run remains active until
user closes it. Do not launch duplicates. Request next: start a game, report
whether cutscenes/loaded gameplay work, whether sound plays, and whether
navigation used Steam Controller or keyboard. Performance/saves unverified.

### User-tested new game / controller milestone

User started a new game in the untimed run and reports it ran "pretty well"
with some hitching; controller inputs worked nicely. User confirms they
force-closed it. Do not classify this exit as a spontaneous game crash or
the earlier suspected idle-cinematic freeze as a confirmed hard lock.
Audio output and long-session stability still need explicit confirmation.

Evidence from `logs/fable_2_007.log`:
- 307 graphics-pipeline creation messages during this session. Synchronous
  shader compilation is a plausible contributor to first-use hitching;
  no frame-time measurements yet, so causality is not proven.
- `BaseHeap::Release failed because address is not a region start` at
  01:09:15, with further activity afterward. Track as a non-immediately-fatal
  allocator error requiring follow-up, not a clean error-free run.
- Save files including `Hero000/mainsave.bin`, `herosave.bin`,
  `chaptersave.bin` and header were created below the executable's `saves/`.
  Save generation is observed; successful reload/data integrity unverified.
- End logs show window closing/title termination consistent with user close.

Checks repeated: seven Python tests and native Linux diagnostic read test
pass; `git diff --check` passes; SDK patch reverse-check passes. XEX, copied
content and saves are ignored by Git. No commit or push performed.

Recommended first repository checkpoint: experimental Linux build/runtime
portability fixes + Linux regression test + exported SDK patch + reproducible
build/run documentation. Keep generated code, SDK binaries, game files,
runtime caches/saves/logs and unreviewed `.opencode/` out of commits.
SDK fix currently exists inside the submodule as an uncommitted edit: merely
committing the parent gitlink cannot preserve that edit. Carry the exported
patch/application instructions (or publish a deliberate SDK fork commit later).

Before calling Linux support stable: repeat cold/warm startup with synchronous
shaders, verify save/reload and sound, measure frame times on a fixed scene,
investigate allocator diagnostic, and validate reproducible setup from a clean
checkout. A Linux-specific build/run helper and explicit CPU/toolchain defaults
would improve reproducibility; async Vulkan remains an unresolved bug/workaround.

### Local commit preparation and workflow validation

User approved `git add`/`git commit` only; pushing remains manual.

- `07c81b2` — `fix(linux): enable native Vulkan builds`: CMake/host portability,
  safe diagnostic reads/test, explicit hotfunc callees and SDK check patch.
- `124bb03` — `build(linux): add reproducible staging build and launch workflow`:
  `tools/linux.py` provides `stage`, `build` and untimed `run`, plus synthetic
  regression tests. It preserves the SDK pin and refuses conflicting source
  staging instead of overwriting it. Temporary content staging is Git-ignored.
- Repeated stage against the existing copy succeeds without rewriting it.
  Helper-driven incremental Release build succeeds (`out/linux-helper-build.log`).
- Python suite now has **18 passing tests**; native Linux safe-read test passes.
- Isolated tracked-source archive (no pre-existing `generated/default`) passes
  initial configure, fresh codegen, reconfigure and verification of generated
  recompilation objects in Ninja. Log: `out/linux-clean-bootstrap.log`.
  This reused the existing pinned SDK/tool; **not** a full clean dependency
  download/rebuild or a new gameplay test. Original saves/content untouched.
- Portable setup/run instructions are in `docs/LINUX.md`, linked from README.
  Personal content paths are omitted from this public progress log.
- SDK remains locally modified by the reviewed patch; its parent gitlink is
  intentionally unchanged. `.opencode/` remains untracked and unmodified.
- No push performed; stable-support claims remain deferred.

### Continue / cutscene / crowded-scene validation

User-reported sequence in the next untimed synchronous-shader session:

1. Waited for main menu, then powered on the Steam Controller.
2. Let the idle cinematic start and pressed controller A to return/proceed.
3. Navigated to Continue and loaded the existing game.
4. Saw the intro cutscene and skipped it with Start.
5. Let the in-game cutscene play and followed gameplay instructions.
6. Encountered choppiness near the trader demonstrating goods, with many NPCs
   and an LT zoom prompt; some unspecified visual elements looked wrong.
7. Enlarged the window on the desktop, then closed it from the UI.

This verifies existing-save loading, controller hot-plug/use across this
sequence, Start skip and cutscene/gameplay progression. The earlier idle
cinematic freeze suspicion is not a confirmed hard lock: this later run
progressed beyond it. Resizing was exercised but not comprehensively validated.
Audio still needs an explicit user report. Do not assign visual defects to
shaders, graphics instructions, controller or resizing without more evidence.

Evidence: `logs/fable_2_008.log` beside the executable and the helper console
log for this session. Roughly 14 minutes of execution, 341 pipeline-creation
messages, expected missing optional `update:` path errors, and unmapped SDL
HID button messages. No logged allocator-release error, unhandled guest AV or
Vulkan device-loss error in the inspected output. The previous allocator
diagnostic remains unresolved, not proved fixed. End shows window closing and
title termination, consistent with user UI close, not a spontaneous crash.

Before this test, created and read-verified a nine-file save backup below
`out/save-backups/`. Afterward all nine backed-up files match byte-for-byte,
including Hero save files; an achievements file was newly created. Thus
Continue/reload of the existing save is verified, but no new gameplay save
checkpoint or persistence of newly earned progress has been verified.

Next tests: explicit audio check; reach a completed autosave and verify new
progress survives normal quit/relaunch; compare the same trader/NPC scene on
cold/warm runs with frame-time data and precise visual descriptions/screenshots.
Keep synchronous shaders and the 30-FPS baseline until controlled measurements
justify changing them. Record rendering, performance and input issues separately.

### Content checkpoint — 2026-10-24

User-provided content root: `/path/to/extracted/FableII/` (personal path omitted).
Already extracted: `default.xex`, `data/`, `$SystemUpdate/` and `nxeart`.
The XEX matches the supported **GOTY USA/Europe** catalogue entry exactly:
SHA-256 `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`,
21,217,280 bytes, media ID `716F0A0D`. Both required content markers exist
(`data/gold_version.txt`, `data/startup.vfsconfig`); rejected TU1 and German
localization markers are absent. `data/` contains 447 files and occupies
approximately 6.5 GiB. This passes catalogue/structure checks, not a complete
per-file integrity audit or runtime test. No extraction needed at this stage.
Content was inspected read-only and remains outside the repo. Runtime can use
`--game_data_root` with this path; codegen separately needs its manifest input
resolved (`game_root = "."`, entrypoint `default.xex`), to be arranged without
committing proprietary files or machine-specific shared manifest edits.

1. **Done:** locate the user's game dump and verify its supported XEX revision.
2. Initialize the pinned SDK and dependencies; identify required Arch
   development packages without making unsolicited system changes.
3. Configure a reproducible Linux Release build using the installed Clang
   via local overrides; record dependency/configuration failures exactly.
4. Port active Windows-only host code with minimal changes, preserving guest
   behavior and the fork's runtime/renderer fixes. Build and run relevant tests.
5. Validate Vulkan plugin loading, then boot the game on the RTX 3090.
   Record logs and distinguish initialization, menus, loaded gameplay and
   save/load success.
6. Validate actual Wayland window-backend use, input capture/mouse behavior,
   audio, fullscreen, focus changes and shutdown. Compare XWayland only if
   useful for isolating presentation bugs.
7. Establish a stable 30-FPS gameplay baseline before increasing targets.
   Measure frame times, shader-compilation stutter, CPU/GPU utilization and
   visual correctness; change one performance variable at a time.

Keep this log and memory-service checkpoints updated with evidence, blockers,
workarounds and exact reproducible commands. Separate hypotheses from verified
results, and avoid storing proprietary game data or credentials in either.
