# Steam Deck performance — multi-session plan (START HERE)

Started 2026-11-14, after the desktop plan (`plans/PERF_SESSIONS.md`) reached a solid 30 FPS.
Supersedes the Oct-25/26 Deck handoffs (`plans/STEAMDECK_PERF_*.md`, `plans/steamdeck-*.md`)
as the *active* Deck plan. Those stay as evidence (especially for unit D3); don't re-read them
unless a step below says so.

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
Fable II Steam Deck performance: read plans/STEAMDECK_SESSIONS.md ("Handoff 2026-11-15",
then the status table only). Deck is at ~27 FPS on r5. r6 (D4a v3 build + D4b constant-write
cvar) is built but not deployed. Use the tools/deck/*.sh scripts. First deploy r6 and do the two
Deck runs from the handoff, then decide between PGO and sampler caching from the stats. The Deck
is deck@192.168.1.54 (zsh alias `steamdeck`); I launch from the Steam shortcut and tell you the
F3 guest FPS. Explain simply (I'm a Java dev); store findings in the memory MCP
(Fable II = projectId 3; reusable = GLOBAL).
```

## Handoff 2026-11-15 (written 2026-11-14 late)

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
| D3 | ✅ 2026-11-14 — **18 → 25 FPS**, no visual issues; see "Unit D3 results". New SDK cvars `readback_memexport_fast_no_stall` (Fable seeds **true**, release r5) and `readback_memexport_stats`. **Still to do:** desktop check, commit (SDK + app). Original task text: **Proper fix for the memexport ping-pong.** The flag stops copying memexport output back to *CPU* memory (GPU consumers keep their copy), so it's only safe if no guest CPU code reads that data, which the Oct work could not rule out. Better: find out why `readback_memexport_fast` (double-buffered, shouldn't wait) **falls back** to the synchronous path (`IssueDraw_MemexportReadbackFallback` in the profile; `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp` ~4396–4600) and make the fast path cover Fable's draws. If that fails, decide whether to seed `readback_memexport=false` for Fable after a wider visual check (several areas, the dog, water, night). Background: `plans/STEAMDECK_PERF_NEXT_SESSION.md` "Goal and established evidence". | Claude + user | ⏳ |
| D1–D3 commit | Desktop check (see handoff), then commit SDK + app changes | user + Claude | ✅ 2026-11-14 — desktop solid 30; committed + pushed (main `5aa8b40` → master on GitLab+GitHub, SDK `11ea340` → GitLab `fable2-perf`) |
| D4a | **Deck build for its own CPU:** `-march=x86-64-v3 -ffp-contract=off` (Zen 2 has AVX2/BMI2/FMA; fp-contract off keeps float results identical). Build: `out/build/deck-linux-amd64-release-v3` (scratchpad copy of build.sh with the flags changed; to redo, copy `tools/deck-build/build.sh` and edit the two `-DCMAKE_*_FLAGS` lines). | Claude + user | ✅ tested 2026-11-14 (r6 run 1): **~26 FPS, no gain** vs r5 27; GPU Commands still ~94% busy. Branchy pointer-chasing code doesn't benefit from AVX2. |
| D4b | **Skip constant writes that change nothing.** Before each draw the game re-sends shader constants and texture/vertex fetch constants; the emulator invalidated constant buffers, texture bindings and vertex residency on every write, even with identical values → redundant re-uploads, `BindingInfoFromFetchConstant`, texture-cache hashing, `GetView`. New SDK cvars (uncommitted): `gpu_skip_unchanged_constant_writes` (default false; skip invalidation when no value changed, else narrow it to the changed registers) and `gpu_constant_write_stats` (written vs changed per frame). Exact behaviour: identical values = identical buffers/bindings; texture *content* changes are tracked separately (`texture_became_outdated_`). Code: `VulkanCommandProcessor::WriteRegister` / `WriteRegistersFromMem`. | Claude + user | ✅ seeded in `fable_2_app.h` 2026-11-14 (r7+), user saw no visual issues. Tested (r6 run 2): **~27 FPS** (≈ noise). Stats per frame: float 22.8k writes, 57% unchanged; fetch 12.9k, 33% unchanged; boolloop 1.27k, 59% unchanged. GPU Commands ~98% busy; GPU **99% busy at 1366 MHz, 15 W cap**. Visual check pending; seeding decision pending. |
| D4 | GPU Commands CPU cost per draw (`UpdateBindings`, `ExecutePacket`, texture-cache hashing, `GetView`, `UpdateSystemConstantValues`). Now the limit at ~95% busy on r5. Start with a fresh 30 s `perf record` on the Deck (r5) of the GPU Commands thread. Helps desktop too. | Claude + user | ⏳ |
| D5a | **GPU time per kind of work.** New SDK cvar `gpu_timing_stats` (GPU timestamps at each change of work kind + one per render-target transfer, tagged source→dest; log every 120 frames). | Claude + user | ✅ 2026-11-14 — Deck r7/r8 at the test spot: gpu_busy ~31.5 ms/frame = **RT transfers 15.2 ms (48%)**, draws 10.3 (33%, ~6,400 draws), resolves 2.7, texture loads 2.4. Transfers: 95 pairs/frame, long tail, nearly all **into depth** (4x MSAA), small offscreen surfaces (pitch 2–14 tiles) ping-ponging 1x↔4x at the same EDRAM base. `--render_target_path_vulkan=fsi` (no transfers): **16 FPS** (draws 59 ms) → not an option. |
| D5b | **Skip transfers whose destination still holds the data** (valid-copy tracking in `render_target/cache.cpp` `ChangeOwnership`; cvars `rt_transfer_skip_valid_copies` (off) + `rt_transfer_stats`). | Claude | ❌ 2026-11-14 (desktop counting) — **0%** skippable: the in-between pass always writes. 37% of transfers return to a recent owner (written in between); only ~5% copy resolve-cleared ranges. Code removed again (2026-11-14), results kept here. Next ideas: partial (dirty-region) transfers, game-specific handling, or the CPU side (GPU Commands still ~100%). |
| D6 | **PGO for the GPU plugin** (`librexgpu-xenos.so` only). CMake `FABLE2_GPU_PGO=generate\|<.profdata>` (root `CMakeLists.txt`), via `FABLE2_GPU_PGO=... tools/deck/build.sh`; SDK hook `GpuPgoMaybeDumpProfile` writes `/userdata/pgo/rexgpu-xenos-%p.profraw` every 600 frames. Merge: `llvm-profdata merge` in the builder image. Profile kept at `out/pgo/rexgpu-xenos-20261114a.profdata` (git-ignored; new file name per profile). | Claude + user | ✅ 2026-11-14 — r9 = instrumented, r10 = optimized: **~29 FPS**, no visual issues, GPU Commands 92% running. GPU now 97% busy at 1320 MHz. Brief dips when turning the camera. Re-profile after big GPU-plugin code changes. |
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
