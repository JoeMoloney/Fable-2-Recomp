# Steam Deck performance — multi-session plan (START HERE)

Started 2026-11-14, after the desktop plan (`plans/done/PERF_SESSIONS.md`) reached a solid 30 FPS.
Supersedes the Oct-25/26 Deck handoffs (deleted 2026-11-15; summary in `docs/analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md`)
as the *active* Deck plan. The summary keeps the evidence that matters (especially for unit D3); read it only
if a step below says so.

**Goal:** a **solid 30 FPS** (F3 guest meter) on the Steam Deck (LCD, 15 W APU, SteamOS) at the
same test spot as the desktop plan, with guest vblank pacing on and automatic clocks (no
manual GPU clock in the Quick Access menu).

**Done when:** (1) F3 guest FPS holds 30 at the test spot and while walking, except brief
dips, (2) automatic TDP/clock settings, (3) game speed looks right, (4) no new visual breakage.

**Working style:** same as the desktop plan. One focused unit per session. The user drives
the game on the Deck; Claude builds on the desktop, deploys over SSH, measures and edits code.
Each session updates the status table and the next-session prompt below.

### Next session prompt

```
Fable II Steam Deck performance: read plans/STEAMDECK_SESSIONS.md ("Handoff 2026-11-15 night" first,
then the evening one for details, then the status table only). Deck is on r15 (deck-perf-20261115-r15-pgo),
clean launcher. Bowerstone: smooth view 30.0; busy view ~28; turn dips ~26-27 for ~1 s.
r15 is committed (main a8613b5, SDK a0031f0; vulkan_async_submit now seeded by the app, 1a7ad71). Next: the busy
view: GPU Commands per-draw cost and/or find what creates the small 4xMSAA depth targets behind the
~15 ms of RT transfers. Use the tools/deck/*.sh scripts; the game logs FPS itself with
--guest_fps_log=true (no need to time runs together). The Deck is deck@192.168.1.54 (zsh alias
`steamdeck`); I launch from the Steam shortcut. Explain simply (I'm a Java dev); store findings in
the memory MCP (Fable II = projectId 3; reusable = GLOBAL).
```

## Handoff 2026-11-15 night (latest)

**Deck state:** `current` → **r15** (`deck-perf-20261115-r15-pgo`) = r12 + `guest_fps_log` + memexport stall
reasons + `readback_memexport_keep_frames` (**seeded 1800** in `fable_2_app.h`) + the `fable2_msaa_mode` hook
(default -1 = off). Launcher **clean** (`tools/deck/flags.sh` with no args). Desktop build matches r15 code.

**Results this session (details in the evening handoff below):**
1. **Turn dips fixed (mostly):** cause was memexport readback buffers freed after 2 s unused → full GPU stall
   per buffer on returning to a view. Keep 60 s → stalls 0 after first visit; turns ~20 → ~26–27 FPS.
2. **Busy view (~28 FPS) = two limits:** ~2.5x the draws (GPU Commands thread ~92% busy, same function mix)
   and real GPU 31–33 ms/frame, of which **RT transfers ~15.5 ms** (1x↔4xMSAA depth ping-pong on small
   targets, pitch 2–14 tiles, ~100 transfer kinds/frame, top 16 ≈ 9 ms).
3. **Tried, no gain:** `--depth_transfer_not_equal_test=false` (transfers unchanged; RADV has
   shader_stencil_export so no stencil-bit passes). **`fable2_msaa_mode` hook** (Xenia "Disable MSAA", be8
   0x8238DF3F; our build has the same code: `sub_8238DE58`, `li r9,2` at 0x8238DF3C → 0x9C(r31), plus
   width 1120 `li r11,0x460` at 0x8238DF58): values 0 and 1 both ran (log `[msaa] ... 2 -> N`) but the
   4xMSAA transfer targets were **unchanged** → that field is not what creates them. Value 1 felt worse
   (more 150–290 ms hitches). Hook kept, default off. Next idea: find who creates the 4x depth targets
   (`RB_SURFACE_INFO` MSAA bits per draw; trace the guest code that sets them) before patching anything.

**Uncommitted (commit after a desktop check; SDK first, then main; ask the user before committing):**
- SDK: `src/graphics/command_processor.cpp` (`guest_fps_log`), `src/graphics/vulkan/command_processor.cpp`
  + `include/rex/graphics/vulkan/command_processor.h` (`readback_memexport_keep_frames`, per-reason stats).
- Main: `src/core/fable_2_app.h` (seed keep_frames), `src/core/fable2_hooks.cpp` + `fable_2_manifest.toml`
  (`fable2_msaa_mode` hook), `plans/STEAMDECK_SESSIONS.md`. Not ours: `src/input/remote_control_server.h`,
  `.opencode/opencode.json`, `docs/re/FUNCTION_NAMES.md`, `docs/LINUX*.md`, `tests/README.md` (pre-existing).
- Main `850a1f5` still not pushed.

**Desktop check:** `tools/perf/run-profile.sh --vulkan_async_submit=true`, usual spot: still solid 30, no
visual issues (the keep_frames seed and the SDK changes affect desktop too).

## Handoff 2026-11-14 evening

**No code or Deck changes since the afternoon handoff below.** This session only re-read the plan.
Everything in the afternoon handoff still holds (r12 = `current` on the Deck, 30 FPS at the test spot).

**Git:** D7 is committed (main `d438bc1`, SDK `9fda9b4`, SDK pushed to `fable2-perf`). Main `850a1f5`
(the afternoon handoff) and this update are **not pushed yet**: `git push gitlab HEAD:master`.
`src/input/remote_control_server.h` is still the user's own uncommitted change.

**User report on r12 (2026-11-14):** the test spot is in **Bowerstone** (a dense town, so it's a
heavy case). Standing still: **holds 30**. Turning the camera: FPS **drops, then recovers** and settles
back at 30. Only tested around the test spot so far, not in other areas.

**Turn test (same evening):** repeating a turn over the same view dips **just as much** the second
time, so it's **not** first-time creation. Pipelines confirmed: 469 loaded from the cache at startup,
only 1 created during play.

**New tool, r13 (`deck-perf-20261115-r13-pgo`) = r12 + SDK cvar `guest_fps_log`** (default off; launcher
now has `--guest_fps_log=true`). One log line per second from the XE_SWAP handler
(`thirdparty/rexglue-sdk/src/graphics/command_processor.cpp`):
`[guest-fps] 28.4 fps, worst 51.2 ms, slow(>36ms) 4/28`. Uncommitted (SDK). Desktop rebuilt after.

**r13 turn results (Bowerstone test spot, `userdata/logs/fable_2_034.log`), repeating cycle:**

| Phase | Guest FPS | Worst frame | Frames >36 ms |
|---|---|---|---|
| Test-spot view, standing | **30.0** solid | ~34.5 ms | 0 |
| The turn itself (1–2 s) | **19–23** | 60–80 ms | 13–24 of ~22 |
| Holding the other view (~6 s) | **~28.5** | 40–47 ms | 8–15 of ~29 (frames alternate 33/50 ms) |
| Turning back (1–2 s) | 23–26 | 55–73 ms | ~12 |

Two separate problems: (1) the turn itself costs extra for 1–2 s; (2) the other view is slightly over
budget all the time (GPU Commands thread ~93% busy there in the per-thread recording, vs ~45% in the
test-spot view). Also seen: menu/title screen runs at a steady ~25 FPS (worst ~51 ms); not gameplay.

**Profile of the turn (r13, Deck `userdata/logs/perf-turn2-234056/`, `gpucmd.data` = DWARF perf of the
GPU Commands thread, 40 s; `threads.txt`; symbols: `--symfs=$PWD/symfs` there):**
- **Busy view:** GPU Commands thread **saturated** (~460 samples/s of 499 = ~92%; smooth view ~230/s =
  ~46%). FPS 27.3. Per frame that's **~33 ms vs ~15 ms** of thread work, i.e. ~2.2x. The function mix is
  **the same** in both views (UpdateBindings 9%, RequestTextures 7%, ExecuteIndirectBuffer 6%, IssueDraw 6%,
  WriteRegistersFromMem 6%, UpdateTextureBindingsImpl 4.5%, RADV 4.5%, GetSamplerParameters 3%,
  RenderTargetCache::Update 3%, ...), so the busy view just has more draws. Needs ~10% less per-draw
  cost to hold 30 there → D4-style per-draw work (bindings, texture requests, samplers).
- **The turn itself (1–2 s at ~20 FPS):** GPU Commands is **not** the limit then (~45–50% busy, same
  function mix); the guest "Texture Streamer" thread wakes up right then. So the deep turn dip is
  something else: guest-side streaming, a wait/stall, or real GPU work (texture uploads). Not measured yet.

**Turn run with counters (r13 + `--gpu_timing_stats=true --readback_memexport_stats=true`, Deck
`userdata/logs/perf-turn3-234618/`, 120-frame stat windows):**

| | smooth view | busy view / turns |
|---|---|---|
| draws per frame | ~2,400 | **~5,100–6,800** |
| real GPU busy per frame | 28.7 ms | **34–36 ms** (over the 33.3 ms budget too) |
| of that: draws / RT transfers | 7.8 / 15.0 ms | 11.6 / 17.3 ms |
| memexport readbacks: ready / skipped / **full stall** | 33 / 4 / **0.00** | 58–62 / 14–16 / **0.5–0.8 per frame** |

**Likely cause of the turn dips (not yet proven):** `IssueDraw_MemexportReadbackFastPath` keys readback buffers
by (base address, **size**) and evicts a key after **60 frames (2 s) unused**
(`kReadbackBufferEvictionAgeFrames`, `EvictOldReadbackBuffers`). A key with no previous slot isn't covered by
`readback_memexport_fast_no_stall`: it takes `IssueDraw_MemexportReadbackFullPath` (end submission +
wait for the whole GPU). Turning to the busy view brings memexport draws whose buffers were evicted while
looking away → a burst of full stalls → GPU Commands half idle but FPS ~20 for 1–2 s, on **every** turn.
Size changes also make new keys (stalls while holding the busy view: 0.26/frame). Also: growing a slot in
`ensure_readback_slot` calls `AwaitAllQueueOperationsCompletion()` (not counted in the stats).
**Test (r14):** split the `full` count by reason (new key / slot too small / other) and make the eviction age
a cvar; then try no-stall for new keys too. **Busy view** needs GPU time savings as well as CPU (34–36 ms).

**r14 result — cause CONFIRMED (`deck-perf-20261115-r14-pgo` = r13 + SDK cvar
`readback_memexport_keep_frames` (default 60) + per-reason full-stall counts; launcher
`--readback_memexport_keep_frames=1800`; Deck log `fable_2_038.log` 23:54:34–23:55:42):**
- Every full stall was reason **new** (key with no previous slot); small/open/alloc/grow_waits all 0.
- First turn to the busy view still creates the keys (full new 0.87 → 0.22/frame, FPS 18 for ~1 s);
  after that **full = 0.00** for the rest of the run.
- Later turns: **25.7–27.3 FPS for ~1 s** (worst frames 50–70 ms), was **19–22 for 1–2 s** in r13.
- Busy view still ~28–29 (draw/GPU load, separate problem). GPU busy there 31–33 ms (was 34–36 with stalls).
- **Seeded** `readback_memexport_keep_frames=1800` in `src/core/fable_2_app.h` (uncommitted; desktop check
  pending; Deck r14 gets it from the launcher flag until the next build).
- Remaining turn cost (~1 s of 50–70 ms frames) not explained yet; candidates: guest Texture Streamer work,
  first-sight new keys for parts never seen before, texture uploads.

**Next:** (1) busy view: GPU Commands per-draw cost (~2.5x draws there) + GPU time (RT transfers 15 ms).
(2) desktop check of the SDK + seed changes, then commit (SDK first).
**Older next:** (1) turn dip: run turns with `--gpu_timing_stats=true --readback_memexport_stats=true` and a
perf of GameThread + 3D Engine + Texture Streamer (keep the SSH session open — SteamOS kills background
jobs when the SSH session closes). (2) busy view: cut per-draw cost on GPU Commands.
**Older next (done):** with r13, hold the "other" view and `perf record` the GPU Commands thread (what costs more
per frame there: more draws? texture loads? transfers?) + `--gpu_timing_stats=true` for the GPU side;
then the same during repeated turns. (b) Walking test in other areas (outside town, a fight, night).

## Handoff 2026-11-14 afternoon

**Where we are:** Deck **15 → 30 FPS** at the test spot (release `deck-perf-20261115-r12-pgo`, clean
launcher, automatic clocks). GPU Commands 88% busy + 8% limiter sleep; GPU 93% busy at ~1444 MHz, 15 W.
Steps this day: D4b constant-write skip (seeded), D5a GPU timing (`gpu_timing_stats`), D6 PGO of the GPU
plugin, D7 hot-spot fixes + fresh PGO. D4a (x86-64-v3) gave nothing; FSI path 16 FPS; transfer skipping
not applicable (see D5b).

**Commits:** D4–D6 committed by the user. D7 (SDK: `command_processor.{h,cpp}`, `deferred_command_buffer.{h,cpp}`;
main: this plan + SDK pointer) — check `git status`; commit if still pending.
`src/input/remote_control_server.h` is the user's own change: don't commit without asking.

**Builds:** `out/build/deck-linux-amd64-release-v3` holds r12 (PGO-optimized). The PGO profile is
`out/pgo/rexgpu-xenos-20261114b.profdata` (git-ignored, keep it). To rebuild r12:
`FABLE2_GPU_PGO=/workspace/out/pgo/rexgpu-xenos-20261114b.profdata tools/deck/build.sh`.
Without `FABLE2_GPU_PGO` the build is non-PGO (the option resets). After changing GPU-plugin code
much, re-profile: `FABLE2_GPU_PGO=generate tools/deck/build.sh`, deploy, play 3–5 min, quit, fetch
`userdata/pgo/rexgpu-xenos-1.profraw`, `llvm-profdata merge` in the builder image to a NEW file name.
Desktop build restored after the Deck builds (see Gotcha below).

**Deck state:** `current` → r12. Releases r1–r12 present (r9, r11 = instrumented PGO builds; don't use for
FPS). Launcher clean.

**Open checks / next options:** (1) walking + other areas (town, combat, night) at 30? (2) brief FPS
dips when turning the camera (likely texture loads / pipeline creation for newly visible objects;
measure with `--gpu_timing_stats=true` + CPU profile during turns). (3) GPU headroom: resolves 2.7 ms,
texture loads 2.4 ms, RT transfers 15 ms per frame (D5a). (4) desktop still solid 30 with D4b/D7.

## Handoff 2026-11-15 (written 2026-11-14 late; superseded by the one above)

**Where we are:** Deck **15 → 27 FPS** at the test spot (release `deck-perf-20261114-r5`, clean
launcher, automatic clocks). Desktop solid 30. Limit on the Deck: GPU Commands thread ~95% busy
(→ D4), real GPU ~91% busy at ~1200 MHz (→ D5 after D4).

**Committed + pushed (2026-11-14):** D1–D3 + this plan's first version. Main repo `5aa8b40` → `master`
on GitLab and GitHub (the `gitlab` remote pushes to both); SDK `11ea340` → GitLab `fable2-perf`
(local branch `fable2`). Push with explicit targets: `git push origin fable2:fable2-perf` (SDK first),
then `git push gitlab HEAD:master`; a plain `git push` refuses (branch names differ).

**Uncommitted (commit after D4b is confirmed; the user runs git add/commit):**

| File | What |
|---|---|
| `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp` | D4b cvars `gpu_skip_unchanged_constant_writes` + `gpu_constant_write_stats` |
| `tools/deck/*.sh` | new Deck build/deploy/measure scripts (below). **Git-ignored** (`.gitignore` line 42: `*.sh`), so they're local only unless added with `git add -f tools/deck/*.sh` |
| `plans/STEAMDECK_SESSIONS.md` | this update |
| `src/input/remote_control_server.h` | **the user's own change, not part of this work**: don't commit without asking |

**Builds on disk (2026-11-14 ~03:10):**
- `out/build/deck-linux-amd64-release-v3`: D4a flags + D4b code (cvar off by default) = **r6, built, not deployed**
  (the Deck was offline).
- `out/build/deck-linux-amd64-release`: r5 (x86-64-v2, no D4b).
- `out/build/linux-amd64-release`: desktop, includes D4b (cvar off by default); SDK libs restored after the Deck builds.

**Deck state:** `current` → r5. Releases on the Deck: r1–r5 (`deck-perf-20261114-rN`) and r9 (old
native-renderer build). `launch.sh` is clean (pacing default on, `--vulkan_async_submit=true`);
backups in `launcher-backups/`. Perf captures: `userdata/logs/perf-20261114-013357/`.

**Deck scripts** (`tools/deck/`, written 2026-11-14, syntax-checked, **not yet run against the Deck**;
check their output closely the first time):

| Script | Does |
|---|---|
| `build.sh [v3\|v2]` | container build for the Deck (default v3 → `out/build/deck-linux-amd64-release-v3`); then rebuild desktop before running it (see the script's NOTE) |
| `deploy.sh <build-dir> <release> [note]` | package, copy, sha256-verify, release dir + userdata links, flip `current`, dry run |
| `flags.sh [--flag=value ...]` | set extra launcher flags for an A/B (no args = clean launcher); atomic, daily backup |
| `use-release.sh [release]` | list releases / switch `current` (rollback) |
| `measure.sh` | release + flags, GPU busy/clock/power, per-thread user/kernel CPU, GPU Commands wait state, newest `*_stats` log lines |

**Tomorrow's steps:**
0. Check the Deck is reachable: `ssh deck@192.168.1.54 true`. Check `git status` (SDK + main) matches
   the "Uncommitted" table; if any source changed since 03:10, run `tools/deck/build.sh` first.
1. `tools/deck/deploy.sh out/build/deck-linux-amd64-release-v3 deck-perf-20261115-r6-v3 "D4a v3 + D4b cvar"`
2. **Run 1** (D4a alone): `tools/deck/flags.sh` (clean). User launches, same spot → F3 FPS vs 27. `tools/deck/measure.sh`.
3. **Run 2** (D4b): `tools/deck/flags.sh --gpu_skip_unchanged_constant_writes=true --gpu_constant_write_stats=true`.
   User launches → F3 FPS + visual check. `measure.sh` shows the constant-write stats (redundant writes per frame).
4. If D4b helps and looks right: seed it in `src/core/fable_2_app.h` (like D3), desktop check, commit.
5. Then, by the numbers: PGO for the GPU plugin (builder image has `llvm-profdata` and the profile
   runtime) or caching sampler setup per draw (`GetSamplerParameters`/`UseSampler`, ~3–4% of the thread).

**Optional tonight/any time (desktop A/B of D4b, pacing off to see raw speed):**
```
tools/perf/run-profile.sh --vulkan_async_submit=true --guest_vblank_pacing=false --gpu_constant_write_stats=true
tools/perf/run-profile.sh --vulkan_async_submit=true --guest_vblank_pacing=false --gpu_constant_write_stats=true --gpu_skip_unchanged_constant_writes=true
```
Stats lines: `grep constant_write_stats` in the newest `out/build/linux-amd64-release/logs/fable_2_NNN.log`.

**Gotcha:** every Deck container build overwrites the shared `thirdparty/rexglue-sdk/out/linux-amd64/*.so`.
Before running desktop, rebuild desktop (delete those two `.so` first, then
`cmake --build out/build/linux-amd64-release --target fable_2 -j12`).

## Handoff 2026-11-14 night (superseded by the one above; kept for history)

*All of this was done the same night: desktop check passed (solid 30), committed + pushed as `5aa8b40` / SDK `11ea340`.*

**Where we are:** Deck **15 → 27 FPS** at the test spot (release `deck-perf-20261114-r5`, clean
launcher, automatic clocks). Limit now: GPU Commands thread ~95% busy (→ D4), real GPU ~91% busy (→ D5).

**Uncommitted work (main repo + SDK submodule), commit after the desktop check:**

| File | Change | Unit |
|---|---|---|
| `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp` | `SpinPause()` on Linux: `yield` → `sleep_for(200 µs)` | D1 |
| `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp` | idle-tick path: `sleep_for(200 µs)` before `Yield_82CBD098` (non-Windows) | D2 |
| `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp` | new cvars `readback_memexport_fast_no_stall`, `readback_memexport_stats` | D3 |
| `src/core/fable_2_app.h` | seeds `readback_memexport_fast_no_stall=true` | D3 |
| `plans/STEAMDECK_SESSIONS.md` | this plan (new) | — |
| `src/input/remote_control_server.h` | **the user's own earlier change, not part of this work**: ask before committing it | — |

Commit order: SDK submodule first (its own commit), then the main repo (bumps the submodule).

**Desktop check (user, ~5 min), desktop build already done 2026-11-14 02:51:**
```
cd /mnt/HDD/git_stuff/my_git/Fable-2-Recomp
tools/perf/run-profile.sh --vulkan_async_submit=true
```
Same as the unit 4h test: angle 2 + running around + the busiest spot. Pass = F3 guest FPS holds 30
(brief dips to 28 OK, as before), game speed normal, no visual issues. To compare against the old
behaviour in the same build: add `--readback_memexport_fast_no_stall=false` (D3 off; D1/D2 need a rebuild to undo).

**Deck state:** `current` → r5. Releases r1–r5 and r9 (old native-renderer build) are all on the Deck.
`launch.sh` is clean (pacing default on, `--vulkan_async_submit=true`); backups in `launcher-backups/`.
Perf captures: `userdata/logs/perf-20261114-013357/`.

## How things are set up

| What | Where / command |
|---|---|
| Deck install (what the Steam "non-Steam game" shortcut runs) | `~/Documents/fable2-native-experimental/launch.sh` on the Deck |
| Builds on the Deck | `releases/<name>/bin/` (+ `container-image.txt`, `BUILD_INFO.txt`); the `current` symlink picks which one runs |
| Saves / config / logs (shared by all releases) | `userdata/` (each release's `bin/` has symlinks to it) |
| Launcher backups | `launcher-backups/` (`launch.sh.before-perf-20261114` = the old native-renderer r9 launcher) |
| Runtime | rootless podman, image `localhost/fable2-native-vulkan:arch-20261018-r3` (SteamOS glibc is too old for an Arch build). Host RADV is passed through. |
| Build (on desktop, ~25 min fresh, ~1–2 min incremental) | `tools/deck/build.sh` (v3, default) or `tools/deck/build.sh v2` (r1–r5 style). r1–r5 were built with `../Fable-2-native-vulkan-renderer/tools/deck-build/build.sh` (same container command, v2 flags). |
| Deploy | `tools/deck/deploy.sh <build-dir> <release-name>` (manual steps: copy the 3 files to `staging/<name>/bin`, verify sha256, `cp -a` into `releases/`, add the 5 userdata symlinks, flip `current`, `FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh`) |
| A/B launcher flags | `tools/deck/flags.sh --flag=value ...`; `tools/deck/flags.sh` alone = clean launcher |
| Rollback | `tools/deck/use-release.sh <old-release>` (= `ln -sfn releases/<old> current.new && mv -T current.new current`) |
| Measure the running game | `tools/deck/measure.sh` |
| Profile (no root needed, perf_event_paranoid=2) | on Deck: `perf record -F 499 -g -p $(pgrep -x fable_2) -- sleep 30`; to unwind through libc use `--call-graph=dwarf` on a few threads (`-t tid,...`) for 5 s. Copy `perf.data` back and resolve with `--symfs` (dir with `app/` → our build outputs and `usr/lib/libc.so.6` from the container image). |
| Quick health numbers | per-thread user vs kernel time from `/proc/<pid>/task/*/stat` (fields 14/15); GPU: `/sys/class/drm/card*/device/{pp_dpm_sclk,gpu_busy_percent}`; power: `hwmon` named `amdgpu` → `power1_average` / `power1_cap` |

Notes:
- The container build writes the SDK libs into the **shared** `thirdparty/rexglue-sdk/out/linux-amd64/`.
  After a Deck build, do a normal desktop build before using desktop again (or check the
  `.so` timestamps), so the desktop doesn't pick up the container-built copies.
- `--fable2_native_vk_triangle` only exists on the native-renderer branch; don't pass it to this build.
- The old Distrobox setup (`~/Documents/fable-2-recomp`, `~/.local/bin/fable2-steam.sh`) is unused by this plan. Leave it alone.

## Baseline findings (2026-11-14, release `deck-perf-20261114-r1`)

r1 = desktop commit `f8b8908` (pacing on, `--vulkan_async_submit=true`, 4g/4g-b seeds) built for the
Deck. Same spot as the desktop test: **~15 guest FPS** (desktop: solid 30). Evidence on the Deck:
`userdata/logs/perf-20261114-013357/` (`perf.data`, `perf-dwarf.data`, `perf-gpucmd.data`).

1. **Power-starved GPU.** APU at 14 of 15 W, CPU at max (3.4–3.5 GHz), real GPU ~100% busy but
   only **560–820 MHz** (max 1600). The Deck's 15 W is shared, so CPU watts are GPU watts lost.
2. **Spin-waits burn those watts.** 3D Engine was 66% kernel time, GameThread 49%, almost all
   `sched_yield` (libc offset `0x10346b`). DWARF stacks:
   - 3D Engine: `FrameLimiterWait_82242628` → `SpinPause()` → `std::this_thread::yield()`.
     It waits on the GPU progress counter (the emulated GPU's "frame done" signal), not on vblank.
   - GameThread: `ProcessGameFrame_82276C30` → `Yield_82CBD098` while no fixed simulation tick is
     due yet (a pure "wait until the clock reaches the next tick" loop).
3. **Proof:** manual GPU clock 1600 MHz → **19 FPS**. The GPU is then only ~40% busy, so a second
   limit shows up:
4. **CPU↔GPU ping-pong.** GPU Commands is still ~44% blocked in `drm_syncobj` waits, via
   `IssueDraw_MemexportReadbackFallback` → `CheckSubmissionFenceAndDeviceLoss` →
   `WaitForAsyncSubmitted`. Same culprit as the Oct-25 Deck work (~35–39 ms of waits then).
5. GPU Commands own CPU work: `UpdateBindings`, `ExecutePacket`, texture-cache hash lookups,
   `GetView`, `UpdateSystemConstantValues` (~half a core). Worker `XThread9EFC66C0` ~0.4 core
   (`ProcessAndProcessAndProcess743_82C52F60`, `…504_821FE378`: animation/float math).

## Session queue

| # | Unit | Who | Status |
|---|---|---|---|
| D0 | Build + deploy current desktop code to the Deck, baseline measurement | Claude + user | ✅ 2026-11-14 — r1, ~15 FPS; see baseline |
| D1 | `FrameLimiterWait` `SpinPause()`: Linux `yield` → `sleep_for(200 µs)` | Claude + user | 🟡 2026-11-14 — r2: **~18 FPS**, 3D Engine kernel 66% → 4%, GPU mostly 1040 MHz. **Still to do:** verify desktop still holds 30 at angle 2, then commit. |
| D2 | `ProcessGameFrame` idle-tick yield: add `sleep_for(200 µs)` (non-Windows only) before `Yield_82CBD098` | Claude + user | 🟡 2026-11-14 — r3: **~18 FPS (no change)**, game speed OK. GameThread kernel 51% → 1%, total ~1.5 cores less CPU. But the GPU still sits at ~1040 MHz, ~100% busy, 14–18 W: CPU cores stay boosted at 3.5 GHz, so the freed watts didn't reach the GPU. Keep (cheaper, cooler). **Still to do:** desktop check, commit. Next: r3 + manual GPU 1600 MHz to see the GPU-limited ceiling. |
| D3a | Free A/B: `--readback_memexport=false` added temporarily to `launch.sh` (backup `launcher-backups/launch.sh.before-d3-readback-ab-20261114`) | Claude + user | ✅ 2026-11-14 — **18 → 24 FPS**, no visual issues seen at the test spot. GPU Commands fence waits ~25–44% → ~0%; GPU Commands now ~94% busy (CPU-bound → D4); real GPU ~92% busy at ~1170 MHz. |
| D3 | ✅ 2026-11-14 — **18 → 25 FPS**, no visual issues; see "Unit D3 results". New SDK cvars `readback_memexport_fast_no_stall` (Fable seeds **true**, release r5) and `readback_memexport_stats`. **Still to do:** desktop check, commit (SDK + app). Original task text: **Proper fix for the memexport ping-pong.** The flag stops copying memexport output back to *CPU* memory (GPU consumers keep their copy), so it's only safe if no guest CPU code reads that data, which the Oct work could not rule out. Better: find out why `readback_memexport_fast` (double-buffered, shouldn't wait) **falls back** to the synchronous path (`IssueDraw_MemexportReadbackFallback` in the profile; `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp` ~4396–4600) and make the fast path cover Fable's draws. If that fails, decide whether to seed `readback_memexport=false` for Fable after a wider visual check (several areas, the dog, water, night). Background: `docs/analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md` "The memexport readback stall". | Claude + user | ⏳ |
| D1–D3 commit | Desktop check (see handoff), then commit SDK + app changes | user + Claude | ✅ 2026-11-14 — desktop solid 30; committed + pushed (main `5aa8b40` → master on GitLab+GitHub, SDK `11ea340` → GitLab `fable2-perf`) |
| D4a | **Deck build for its own CPU:** `-march=x86-64-v3 -ffp-contract=off` (Zen 2 has AVX2/BMI2/FMA; fp-contract off keeps float results identical). Build: `out/build/deck-linux-amd64-release-v3` (scratchpad copy of build.sh with the flags changed; to redo, copy `tools/deck-build/build.sh` and edit the two `-DCMAKE_*_FLAGS` lines). | Claude + user | ✅ tested 2026-11-14 (r6 run 1): **~26 FPS, no gain** vs r5 27; GPU Commands still ~94% busy. Branchy pointer-chasing code doesn't benefit from AVX2. |
| D4b | **Skip constant writes that change nothing.** Before each draw the game re-sends shader constants and texture/vertex fetch constants; the emulator invalidated constant buffers, texture bindings and vertex residency on every write, even with identical values → redundant re-uploads, `BindingInfoFromFetchConstant`, texture-cache hashing, `GetView`. New SDK cvars (uncommitted): `gpu_skip_unchanged_constant_writes` (default false; skip invalidation when no value changed, else narrow it to the changed registers) and `gpu_constant_write_stats` (written vs changed per frame). Exact behaviour: identical values = identical buffers/bindings; texture *content* changes are tracked separately (`texture_became_outdated_`). Code: `VulkanCommandProcessor::WriteRegister` / `WriteRegistersFromMem`. | Claude + user | ✅ seeded in `fable_2_app.h` 2026-11-14 (r7+), user saw no visual issues. Tested (r6 run 2): **~27 FPS** (≈ noise). Stats per frame: float 22.8k writes, 57% unchanged; fetch 12.9k, 33% unchanged; boolloop 1.27k, 59% unchanged. GPU Commands ~98% busy; GPU **99% busy at 1366 MHz, 15 W cap**. Visual check pending; seeding decision pending. |
| D4 | GPU Commands CPU cost per draw (`UpdateBindings`, `ExecutePacket`, texture-cache hashing, `GetView`, `UpdateSystemConstantValues`). Now the limit at ~95% busy on r5. Start with a fresh 30 s `perf record` on the Deck (r5) of the GPU Commands thread. Helps desktop too. | Claude + user | ⏳ |
| D5a | **GPU time per kind of work.** New SDK cvar `gpu_timing_stats` (GPU timestamps at each change of work kind + one per render-target transfer, tagged source→dest; log every 120 frames). | Claude + user | ✅ 2026-11-14 — Deck r7/r8 at the test spot: gpu_busy ~31.5 ms/frame = **RT transfers 15.2 ms (48%)**, draws 10.3 (33%, ~6,400 draws), resolves 2.7, texture loads 2.4. Transfers: 95 pairs/frame, long tail, nearly all **into depth** (4x MSAA), small offscreen surfaces (pitch 2–14 tiles) ping-ponging 1x↔4x at the same EDRAM base. `--render_target_path_vulkan=fsi` (no transfers): **16 FPS** (draws 59 ms) → not an option. |
| D5b | **Skip transfers whose destination still holds the data** (valid-copy tracking in `render_target/cache.cpp` `ChangeOwnership`; cvars `rt_transfer_skip_valid_copies` (off) + `rt_transfer_stats`). | Claude | ❌ 2026-11-14 (desktop counting) — **0%** skippable: the in-between pass always writes. 37% of transfers return to a recent owner (written in between); only ~5% copy resolve-cleared ranges. Code removed again (2026-11-14), results kept here. Next ideas: partial (dirty-region) transfers, game-specific handling, or the CPU side (GPU Commands still ~100%). |
| D6 | **PGO for the GPU plugin** (`librexgpu-xenos.so` only). CMake `FABLE2_GPU_PGO=generate\|<.profdata>` (root `CMakeLists.txt`), via `FABLE2_GPU_PGO=... tools/deck/build.sh`; SDK hook `GpuPgoMaybeDumpProfile` writes `/userdata/pgo/rexgpu-xenos-%p.profraw` every 600 frames. Merge: `llvm-profdata merge` in the builder image. Profile kept at `out/pgo/rexgpu-xenos-20261114a.profdata` (git-ignored; new file name per profile). | Claude + user | ✅ 2026-11-14 — r9 = instrumented, r10 = optimized: **~29 FPS**, no visual issues, GPU Commands 92% running. GPU now 97% busy at 1320 MHz. Brief dips when turning the camera. Re-profile after big GPU-plugin code changes. |
| D7 | **Hot spots found by the PGO profile:** `GetVulkanDevice()` did a virtual `GraphicsSystem::provider()` call every time (~1e9 calls per 3.5 min) → cached pointer (`vulkan_device_cached_`, SetupContext..ShutdownContext). `DeferredCommandBuffer::WriteCommand` did `std::vector::resize` per command (~1e9 `_M_default_append`) → inline, size tracked in `command_stream_size_`, storage only grown (`GrowStream`), same zero fill. Re-profiled (`out/pgo/rexgpu-xenos-20261114b.profdata`) because the inlining made the old profile stale. | Claude + user | ✅ 2026-11-14 — r11 = instrumented, r12 = optimized: **30 FPS** at the test spot, auto clocks. GPU Commands 88% + 8% limiter sleep (headroom); GPU 93% busy at **1444 MHz** (freed CPU watts went to the GPU), 15 W. Still to check: walking, camera-turn dips, visuals, desktop. |
| D5 | GPU-side cost on RADV (which passes are expensive at 1040–1600 MHz), e.g. RGP / `RADV_THREAD_TRACE`. Only if the GPU is still the limit after D1–D3. | Claude + user | ⏳ |
| — | Out of scope: 60 FPS, native renderer, Theseus rewrites (hot guest functions in point 5 are later candidates). | | ⏸ |

## Results log

| Release | Change | GPU clock (auto) | Guest FPS (test spot) |
|---|---|---|---|
| r9 (Oct 28, native-renderer branch) | old, pacing off | — | not measured |
| `deck-perf-20261114-r1` | desktop `f8b8908` | 560–820 MHz | ~15 |
| r1 + manual GPU 1600 MHz | (no code) | 1600 | ~19 |
| `deck-perf-20261114-r2` | D1 | mostly 1040 | ~18 |
| `deck-perf-20261114-r3` | D1 + D2 | mostly 1040 (dips 525–800), ~100% busy | ~18 |
| r3 + manual GPU 1600 MHz | (no code) | 1600, ~67% busy | ~19 |
| r3 + `--readback_memexport=false` | D3a A/B (launcher flag) | ~1170 avg, ~92% busy | **~24** |
| `deck-perf-20261114-r4` run A | readback on + stats | — | ~18 (2 full stalls/frame) |
| r4 run B `--readback_memexport_fast_no_stall=true` | D3 fix via flag | — | **~25** (0 stalls) |
| `deck-perf-20261114-r5` | D1 + D2 + D3 seeded in code, clean launcher | ~1200 avg, ~91% busy, 14 W | **~27** (GPU Commands ~95% busy = limit; 3D Engine ~60%, GameThread ~47%) |
| `deck-perf-20261115-r6-v3` run 1 | D4a (x86-64-v3), clean launcher | 1208, 91% busy, 14 W | ~26 |
| r6 run 2 + `--gpu_skip_unchanged_constant_writes=true` | D4b | 1366, 99% busy, 15 W (cap) | ~27 |
| `deck-perf-20261115-r7-v3` | D4b seeded + `gpu_timing_stats` | 1310, 88% busy, 15 W | ~28 |
| r7 + `--render_target_path_vulkan=fsi` | FSI A/B | 1600, 99% busy | ~16 |
| `deck-perf-20261115-r8-v3` | + per-transfer tags (launcher: `--gpu_timing_stats=true`) | — | ~28 |
| `deck-perf-20261115-r10-pgo` | D6 PGO GPU plugin (r9 = instrumented build) | 1320, 97% busy, 14 W | **~29** (dips while turning the camera) |
| `deck-perf-20261115-r12-pgo` | D7 hot-spot fixes + fresh PGO (r11 = instrumented) | 1444, 93% busy, 15 W | **30** |
| `deck-perf-20261115-r13-pgo` | + `guest_fps_log` | — | smooth view 30.0; busy view ~28.5; turns 19–22 for 1–2 s |
| `deck-perf-20261115-r14-pgo` | + memexport stall reasons; launcher `--readback_memexport_keep_frames=1800` | — | turns **~26–27 for ~1 s**; busy ~28.5 |
| r14 + `--depth_transfer_not_equal_test=false` | A/B | — | no change (RT transfers ~15.5 ms) |
| `deck-perf-20261115-r15-pgo` | keep_frames seeded + `fable2_msaa_mode` hook; A/B 0 and 1 | — | no change in transfers; mode 1 more hitches |

## Unit D3 results (2026-11-14)

**Cause:** `IssueDraw_MemexportReadbackFastPath` (`thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp`)
double-buffers memexport readbacks per (base address, size) key and hands the guest the *previous*
draw's copy once the GPU has finished it. When the previous copy was recorded in the **still-open
submission** (the same memexport draw repeated within one frame), it can't be ready, so it fell back to
`IssueDraw_MemexportReadbackFullPath`: end the submission, `AwaitAllQueueOperationsCompletion()`
(wait for the whole GPU), plus create/destroy a buffer. Cheap on desktop, milliseconds each on the Deck.

**Numbers** (`--readback_memexport_stats=true`, per frame, test spot):

| | ready (fast, CPU copy updated) | full stall | skipped | Guest FPS |
|---|---|---|---|---|
| default | ~39 | **~2** | 0 | ~18 |
| `readback_memexport_fast_no_stall=true` | ~38 | **0** | ~6 | **~25** |
| `readback_memexport=false` (no CPU copy at all) | — | — | — | ~24 |

**Fix:** new cvar `readback_memexport_fast_no_stall` (SDK default false = old behaviour). When the
previous slot is in the open submission, skip the CPU copy for that draw instead of stalling, so the
guest's CPU-side copy is at most ~1 frame old. GPU consumers (what's on screen) are unaffected. Fable
seeds it **true** in `src/core/fable_2_app.h`. Safer than `readback_memexport=false`, which never
updates the CPU copy, and slightly faster. If stale-data glitches ever show up:
`--readback_memexport_fast_no_stall=false`.

**Now limiting** (from the D3a measurements): GPU Commands' own CPU work (~94% busy) → **D4**; the real GPU is
also near full (~92% busy at ~1170 MHz) → D5 after D4.
