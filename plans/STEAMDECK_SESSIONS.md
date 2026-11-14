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
Fable II Steam Deck performance: read plans/STEAMDECK_SESSIONS.md ("Handoff 2026-11-14 night",
then the status table and "Unit D3 results" only). Deck is at ~27 FPS on release r5. First:
the desktop check of the uncommitted D1/D2/D3 changes (I report the F3 numbers), then commit
them. Then unit D4 (GPU Commands per-draw CPU cost). The Deck is deck@192.168.1.54 (zsh alias
`steamdeck`); I launch the game from the Steam shortcut and tell you the F3 guest FPS.
Explain simply (I'm a Java dev); store findings in the memory MCP (Fable II = projectId 3;
reusable = GLOBAL).
```

## Handoff 2026-11-14 night

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
| Build (on desktop, ~25 min fresh, ~1–2 min incremental) | `FABLE2_BUILDER_IMAGE=localhost/fable2-native-vulkan:arch-20261018-r3 bash ../Fable-2-native-vulkan-renderer/tools/deck-build/build.sh` from this repo → `out/build/deck-linux-amd64-release/` |
| Deploy | copy `fable_2`, `librexgpu-xenos.so`, `librexruntime.so` to `staging/<name>/bin`, verify sha256, `cp -a` into `releases/`, add the 5 userdata symlinks, flip `current`, `FABLE2_LAUNCH_DRY_RUN=1 ./launch.sh` |
| Rollback | point `current` at an older release (`ln -sfn releases/<old> current.new && mv -T current.new current`) |
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
| D1–D3 commit | Desktop check (see handoff), then commit SDK + app changes | user + Claude | ⏳ **next** |
| D4 | GPU Commands CPU cost per draw (`UpdateBindings`, `ExecutePacket`, texture-cache hashing, `GetView`, `UpdateSystemConstantValues`). Now the limit at ~95% busy on r5. Start with a fresh 30 s `perf record` on the Deck (r5) of the GPU Commands thread. Helps desktop too. | Claude + user | ⏳ |
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
