# Desktop performance — multi-session plan (START HERE)

Started 2026-11-13. Supersedes the older Steam Deck / offline perf handoffs as the
*active* plan. Those stay as evidence; don't re-read them unless a step below says so.

**Goal (revised 2026-11-13):** a **solid 30 FPS** on the desktop (i9-10900K, RTX 3090, CachyOS).
Fable II is locked to 30 FPS by design (community high-FPS/tick patches exist but aren't
perfect), so once 30 holds at the worst views the bottlenecks are gone and this effort is **done**.
60 FPS, the Steam Deck port and Theseus rewrites are separate, later projects.

**Done when:** (1) F3 guest FPS holds 30 at 2–3 chosen worst views (incl. the view that gave 16 in
`out/perf/unit4e-212915/`), (2) with guest vblank pacing back ON (commit `ac6ba00` turned it off)
and game speed looks right (animations, NPC movement, day/night), (3) no recurring hitches while walking.

**Working style (keeps usage down):** one focused unit per session. The user drives
the game and runs A/B launches; Claude reads numbers, edits code and rebuilds.
Each session updates the status table below and nothing else.

## Session hand-off rule

At the end of every session Claude updates the status table and replaces the
**Next session prompt** below. The user pastes it into a fresh session.

### Next session prompt

```
Continue Fable II desktop performance work. Read ONLY plans/PERF_SESSIONS.md
(don't re-read older handoffs unless it says so). Goal is now a SOLID 30 FPS (game is
30-locked by design), see "Done when". Unit 4e batched type-0 register writes (same-spot A/B
21 -> 27 FPS; GPU Commands ~27 ms work per ~37 ms frame, idle ~26%). The 3D Engine (producer)
and GPU Commands (consumer) mostly take turns. Do unit 4f: make them overlap.
Step 1: confirm the serialization from out/perf/unit4e-new-215041/sched.data (perf sched
timehist/latency for "GPU Commands (F" and "3D Engine (F800"): how long GPU Commands sleeps per
frame and what wakes it. Step 2: find what holds the producer back (guest FrameLimiterWait /
GpuProgressCheck deadline vs host-side command visibility) and pick the smallest fix, behind a
cvar for A/B. Prefer host-side (SDK) over changing the game's own wait. Rebuild (GPU-plugin-only
changes: copy librexgpu-xenos.so by hand + md5, see "How to run things"), then have me A/B at
the SAME spot/view with the cvar on/off, each with `sudo tools/perf/waitprof.sh <label>`, and
check visuals/animation timing. Keep it focused, explain simply (I'm a Java dev). At the end:
update the status table + this prompt, and store findings/lessons in the memory MCP
(Fable II = projectId 3; reusable lessons GLOBAL).
```

## How to run things

| Task | Command |
|---|---|
| Full Release build (~5 min, first time) | `bash build-release-linux_amd64.sh` (edit `-j4` to `-j12` locally if wanted) |
| Rebuild after an SDK runtime/GPU change only | `cmake --build out/build/linux-amd64-release --target fable_2 -j12` |
| **After a GPU-plugin-only change** (`src/graphics/**`): the build does NOT re-stage the plugin unless `fable_2` relinks. Copy it by hand and check md5 | `cp thirdparty/rexglue-sdk/out/linux-amd64/librexgpu-xenos.so out/build/linux-amd64-release/` |
| Launch with MangoHud overlay | `tools/perf/run-profile.sh [extra --flags]` |
| 20 s CPU profile of the running game | `tools/perf/capture.sh <label>` → `out/perf/<label>-HHMMSS/` |
| Wait profile (per-thread user/kernel CPU, R/S state, ctx switches; with sudo also perf sched, SIGSEGV rate, kernel-inclusive profile) | `sudo tools/perf/waitprof.sh <label>` → `<dir>/waitprof.txt`, `signals.txt`, `perf-kernel.data` |
| Read a profile | `perf report -i <dir>/perf.data --no-children -g none --sort comm,dso,sym --stdio` |

- Test save: copied from `../Fable-2-Recomp-main/out/preserved-build-data-20261107/tested-tmp-release/saves`
  into `out/build/linux-amd64-release/saves` (frame drops are bad at that location).
  A full rebuild with `cmake --fresh` keeps `saves/`, but check after any `rm` of the build dir.
- `generated/` backup: `out/backup/generated-20261113.tar` (codegen reported "0 written", so it was unchanged).
- perf needs `--user-callchains` (perf_event_paranoid=2). LBR call graphs work.
- MangoHud's FPS is the host present rate. Since unit 3 that's capped (~60 with an ImGui
  overlay open), but it's still not the game's frame rate. Use the in-game F3 guest meter for that.

## Baseline findings (2026-11-13, `out/perf/20261113-baseline/`)

20 s at the bad-FPS save location, 188k samples. **No game thread is CPU-saturated**
(GameThread ~0.6 core, 3D Engine ~0.55 core, GPU Commands ~0.7 core). The game is limited
by **waiting and synchronization, not by slow recompiled code.** In order:

1. **`WaitMultiple` is a polling loop** — `thirdparty/rexglue-sdk/src/core/threading_posix.cpp:323`.
   Multi-handle waits (`WaitForMultipleObjects`) trylock-spin plus `sleep_for(1ms)` instead of
   blocking on a condition variable. Effects: up to ~1 ms+ extra wake-up latency on every
   multi-object wait, and the **Audio Worker burns ~21% of all samples (~1 full core)** in
   `pthread_mutex_trylock`/`clock_gettime`. When the remaining time is under 1 ms,
   `duration_cast<ms>` gives 0, so `sleep_for(0)` becomes a pure busy spin.
2. **Presenter runs uncapped at ~3,900 FPS** (main `fable_2` thread ~62% of a core, plus GPU
   and driver contention with the GPU Commands thread). The earlier Deck audit noted that the
   UI-tick and vsync gates in `presenter.cpp` (~1594/~1630) are WIN32-only, and that the swapchain
   uses IMMEDIATE mode. It should present at most once per new guest frame / display refresh.
3. **Timer thread** `rex::thread::TimerQueue::TimerThreadMain` ~4% (probably also a poll loop; check alongside #1).
4. **GPU sync stalls** (from the Deck work): memexport readback (~37 ms per frame on the Deck)
   and occlusion queries that drain the GPU. Both are hot-reload cvars, so they can be A/B tested
   without rebuilding: `--readback_memexport=false`, `--occlusion_query_enable=false`.
   Pass each flag only **once** (CLI11 quirk).
5. Game frame loop `ProcessGameFrame_82276C30` (hotfunc) spins `Yield_82CBD098` while waiting
   for the frame budget. This is the original game's own behaviour; only revisit after 1–4.
6. Hot guest functions (Theseus candidates, small for now): `ProcessAndProcessAndProcess504_821FE378`,
   `InterpolateFloatArray_82C62548`, `ProcessAndProcessAndProcess743_82C52F60`, `FindChainValue_82C469F0`.

## Session queue

| # | Unit | Who does what | Status |
|---|---|---|---|
| 1 | Build, save, profiling setup, baseline profile | done | ✅ 2026-11-13 |
| 1b | ✅ 2026-11-13 (see A/B table: no real gain). **Flag A/B (no Claude needed):** run `tools/perf/run-profile.sh` 3×: no flags / `--readback_memexport=false` / `--occlusion_query_enable=false`. Same spot, note the F3 guest FPS and any visual breakage. Write the results in the table below. | user | ⏳ |
| 2 | Replace polling `WaitMultiple` (and the timer queue if it's the same pattern) with a blocking implementation: a shared waiter condvar/futex registered on each handle. Unit-test wait-any/wait-all/timeout, rebuild, re-profile. | Claude + user test | ✅ 2026-11-13 — see "Unit 2 results" |
| 2b | (small, optional) Alertable waits / `AlertableSleep` still wake every 1 ms (`kAlertablePollSlice`) to check for queued APCs. Make `QueueUserCallback` wake the thread's waiter instead. Only do this if a profile shows it. | Claude | ⏳ |
| 3 | Cap the presenter: present only when there's a new guest frame and/or at the display refresh (FIFO/MAILBOX). Re-measure with MangoHud, which then becomes meaningful. | Claude + user test | ✅ 2026-11-13 — see "Unit 3 results" |
| 4 | **Revised after 1b:** off-CPU/wait profile + trace what `GpuProgressCheck_82B9BF90` / `FrameLimiterWait_82242628` wait for and who signals it. | Claude + user test | ✅ 2026-11-13 — see "Unit 4 results" |
| 4b | Make the write-watch fault path cheap: stop re-reading `/proc/self/maps` in `memory::QueryProtect` (Linux) on every SIGSEGV, under the global lock. | Claude + user test | ✅ 2026-11-13 — see "Unit 4b results" (18 → 25 FPS) |
| 4c | (parked; not needed for 30) Fewer faults per frame, block `FrameLimiterWait` instead of `sched_yield` spin (Deck power). | Claude | ⏸ |
| 4d | Re-measure after 4b (`sudo tools/perf/waitprof.sh`). Expected: limiter moves to GPU Commands (~32 ms of work/frame, i.e. ~30 FPS cap; estimate). | user + Claude | ✅ 2026-11-13 — done as part of 4b: confirmed, ~30 ms GPU Commands work/frame |
| 4e | Make GPU Commands cheaper per draw: batch type-0 register writes. | Claude + user test | ✅ 2026-11-13 — see "Unit 4e results" (A/B 21 → 27 FPS) |
| 4f | ⏳ **next.** Let 3D Engine and GPU Commands overlap (frame ≈ max(A,B) instead of A+B; est. ~37 FPS ceiling at the test spot). **Step 1 (cheap check):** confirm the serialization from `out/perf/unit4e-new-215041/sched.data` / `sched-wakeups.txt`: how long GPU Commands sleeps per frame and what wakes it (3D Engine kick/`UpdateWritePointer`?). **Step 2:** find what really holds the producer back: the guest deadline (`FrameLimiterWait_82242628` / `GpuProgressCheck_82B9BF90`, `BeginFrame_82B9BA58` deadline = counter+2) and/or the host side (when the 3D Engine's commands become visible to GPU Commands). Prefer a host-side fix (the SDK should behave like the 360's GPU, which ran in parallel) over changing the game's own wait. Make it a cvar so it can be A/B'd. Check visuals/timing. | Claude + user test | ⏳ |
| 4v | Measure at 2–3 worst views (incl. the 16-FPS view) with F3. Pick spots and record them in the A/B table so later runs reuse them. | user | ⏳ |
| 4g | (only if a worst view is still < 30 after 4f) One targeted GPU Commands cut: `SharedMemory::RequestRange` → `VulkanSharedMemory::UploadRanges` (~14% of the thread). Next candidates: `UpdateBindings` 12%, `RequestTextures` 9%, `GetSamplerParameters` 6%. | Claude + user test | ⏳ |
| 4h | Turn guest vblank pacing back on (revert `ac6ba00` / `--guest_vblank_pacing=true`) so the game's own 30 FPS lock and vblank-based timing work as on the console. Verify F3 flat at 30 and correct game speed. | Claude + user test | ⏳ |
| 5 | (only if there's stutter) Pipeline-creation hitches: persistent `VkPipelineCache` + async/background pipeline compile. | Claude + user test | ⏳ |
| — | **Out of scope for this plan:** 2b alertable waits, 4c, Theseus rewrites (6+), Steam Deck port. | | ⏸ |

## Unit 2 results (2026-11-13, `out/perf/unit2-blocking-wait-203557/`)

SDK changes (submodule `thirdparty/rexglue-sdk`): `src/core/threading_posix.cpp` (blocking
`WaitMultiple`: each waiting thread puts a small waiter, a condvar plus a generation counter, on every handle;
signaling a handle wakes it) and `src/core/timer_queue.cpp` (local condvar `BlockingWaitStrategy`;
the vendored disruptorplus one has swapped `wait_until` args and doesn't compile). Test:
`tests/unit/core/threading_wait_test.cpp` (6 cases, passes; Catch2 submodule isn't checked out, so it was run with a
stand-in macro header).

| Thread / symbol | Baseline | After |
|---|---|---|
| Audio Worker (spin in trylock/clock_gettime/WaitMultiple) | ~1.0 core (~18% of samples) | 0.03 core |
| `TimerQueue::TimerThreadMain` | 4.35% of samples | 0.02% |
| Total samples in 20 s | 188k | 152k |
| Guest F3 FPS at test spot | not recorded | 16 |

FPS is still low and no thread is saturated: 3D Engine ~0.9 cores, GameThread ~0.8, GPU Commands
~0.6, all mostly in `sched_yield` / `FrameLimiterWait_82242628` / `GpuProgressCheck_82B9BF90`.
So it's still waiting on sync, most likely GPU-side (finding #4, unit 1b) and the presenter (#2).
**Unit 1b is now the most useful thing to run** (no rebuild needed).

## Unit 3 results (2026-11-13, `out/perf/unit3-ui-tick-204712/`)

Cause: an open ImGui dialog calls `RequestUIPaintFromUIThread()` every frame. The only
limiter, `WaitForUITickFromUIThread` (DXGI vblank thread), was `#if REX_PLATFORM_WIN32`,
and the Vulkan swapchain prefers IMMEDIATE, so on Linux the UI thread presented as fast as it could.
Fix (SDK `src/ui/presenter.cpp` + `include/rex/ui/presenter.h`): non-Win32 branch of
`WaitForUITickFromUIThread`/`ForceUIThreadPaintTick`, a condvar `wait_until` the next tick at
new hot-reload cvar `--present_ui_max_fps` (default 60, 0 = uncapped). Guest frames still
force an immediate tick. Present mode not changed (FIFO would block the guest output thread);
MAILBOX can be A/B'd with `--vulkan_allow_present_mode_immediate=false`.

| Metric | Unit 2 | Unit 3 |
|---|---|---|
| Main `fable_2` (presenter/UI) thread samples | ~27.8k (18.3%) | ~1.8k (1.4%) |
| Total samples in 20 s | 152k | 129k |
| Guest F3 FPS at test spot | 16 | 16 |

The host spin is gone, but guest FPS didn't change: the bottleneck is GPU-side sync (finding #4).
GPU Commands / GameThread / 3D Engine are still unsaturated (`GpuProgressCheck`, `FrameLimiterWait`,
`sched_yield`). **Run unit 1b next.** MangoHud FPS now shows the capped present rate.

## Unit 4 results (2026-11-13, `out/perf/unit4-210222/`, F3 = 18 FPS)

**What the waits poll (static).** `FrameLimiterWait_82242628` (called by the **3D Engine** thread,
`MainRenderLoop_82B9CD68`, not GameThread) spins on `GpuProgressCheck_82B9BF90` until
`*(*(video+10896)) >= *(video+10908)`. `video+10896` points to a guest "GPU progress" counter
(seen `0xFFC83000`); `+10908` is the deadline. `BeginFrame_82B9BA58` resets the counter to
deadline-2; only the **GPU Commands** thread writes it (`EVENT_WRITE_SHD`, value = host vblank
count, ~1000 Hz with `--guest_vblank_pacing=false`). So it means "wait until the GPU thread
has executed the previous frame's commands": at most about one frame in flight.

**Live (waitprof, user+kernel CPU):**

| Thread | Cores | User | Kernel | State R | Notes |
|---|---|---|---|---|---|
| 3D Engine | 0.99 | 0.43 | 0.56 | 100% | **saturated** |
| GameThread | 0.84 | 0.47 | 0.37 | 84% | game's own fixed-step loop; kernel = `MaybeYield` sched_yield spin, harmless |
| GPU Commands | 0.59 | 0.55 | 0.04 | 59% | **idle 41%**, sleeping on `write_ptr_index_event_` (starved of commands) |

3D Engine time split (kernel-inclusive profile): ~50% spinning in `FrameLimiterWait` (sched_yield),
**~31% write-watch fault handling**, ~19% real work. 551 SIGSEGV/s ≈ 30 faults per frame at
~0.5 ms each (about 17 ms of a 55 ms frame). Each fault: `MMIOHandler::ExceptionCallback` →
`memory::QueryProtect` → `FindEntryForAddress` opens and `sscanf`s `/proc/self/maps`
(2164 lines) **while holding the global critical region lock**, which GPU Commands also needs
(`SharedMemory::RequestRange`).

**Conclusion:** the frame is a ping-pong between 3D Engine (produce) and GPU Commands (consume).
Neither the GPU nor a fence is slow. The producer's critical path is padded by the
`/proc/self/maps` fault path. Fix it first (unit 4b).

**Decided (2026-11-13):** don't split the 3D Engine guest thread across cores. Only ~19% of it is
real work, and the guest code shares state without locks, so threading it means
reverse-engineering the render loop first (Theseus readability work, much later if ever).
Order: 4b fault path → 4d re-measure → 4e GPU Commands per-draw cost → 4f two frames in flight → Theseus. The unit 3 user-only profile had pointed
at GPU Commands (0.92 "cores" from `cycles:u`) only because kernel time wasn't sampled, and
`capture.sh`'s per-thread snapshot had a parsing bug (comm names with spaces → 0); both fixed/avoided now.

## Unit 4b results (2026-11-13, `out/perf/unit4b-211720/`, F3 = 25 FPS, was 18)

Fix (SDK `src/core/memory_posix.cpp`): `FindEntryForAddress` (used by `QueryProtect` and
`Protect`'s old-access query) now asks the kernel with the `PROCMAP_QUERY` ioctl
(Linux 6.11+) on a /proc/self/maps fd opened once. That's one syscall for one address, and it reads the same
kernel state as the text file, so behaviour is unchanged. If the kernel doesn't support it (ENOTTY/EINVAL), it falls back permanently
to the old text scan, which matters for older Steam Deck kernels. Struct/ioctl number are declared locally (no new header dependency).
Microbenchmark: ~0.2 µs per lookup vs ~500 µs. Test: `tests/unit/memory/query_protect_test.cpp`
(5 cases, pass; run with the Catch2 stand-in header again).

| Metric | Unit 4 | Unit 4b |
|---|---|---|
| Guest F3 FPS | 18 | **25** |
| `MMIOHandler::ExceptionCallback` (incl., % of all samples) | 10.7% | 0.5% |
| `QueryProtect` / `FindEntryForAddress` | 10.35% | 0.02% |
| SIGSEGV/s | 551 (~30/frame) | 802 (~32/frame, more frames) |
| 3D Engine cores (R%) | 0.99 (100%) | 0.99 (100%), ~73% of it `FrameLimiterWait` spin |
| GPU Commands cores (R%) | 0.59 (59%) | 0.76 (77%) |
| GameThread cores | 0.84 | 0.99 (its own `Yield` spin, harmless) |

Read: GPU Commands now does ~30 ms of host work per 40 ms frame, and the 3D Engine mostly waits
for it. Even with perfect overlap (4f) the cap would be about 33 FPS, so 60 FPS needs the GPU Commands per-frame
cost cut roughly in half. **Next: 4e** (WriteRegister, UpdateBindings, texture-cache hash lookups,
ExecutePacket are the top self-time symbols).

## Unit 4e results (2026-11-13, A/B at the same spot: `out/perf/unit4e-old-215256/` vs `out/perf/unit4e-new-215041/`)

Fix (SDK `src/graphics/command_processor.cpp`, `ExecutePacketType0`): consecutive type-0 register
writes now go through `WriteRegisterRangeFromRing` → the backend's batched `WriteRegistersFromMem`
(one copy + one dirty-bit update for whole shader-constant ranges; anything else falls back to the
same per-register `WriteRegister`). "Write one register N times" mode unchanged. Same thing upstream
Xenia does. No unit test (needs a Vulkan device); equivalence checked by reading both paths.
Old plugin binary kept for future A/Bs: `out/perf/unit4e-old-215256/librexgpu-xenos.so.old`.

| Metric | Old plugin | New plugin |
|---|---|---|
| Guest F3 FPS (same spot/view) | 21 | **27** |
| Type-0 packet path (incl., % of GPU Commands) | 14.8% | 5.1% |
| GPU Commands cores | 0.75 | 0.73 |
| GPU Commands host work per frame | ~36 ms | ~27 ms |
| SIGSEGV per frame | ~34 | ~35 |

New GPU Commands split (inside `IssueDraw`, ~75% of the thread): `RequestRange` 18%
(of which `UploadRanges` 14%, mutex 2%), `UpdateBindings` 12%, `RequestTextures` 9%,
`GetSamplerParameters` 6%, `RenderTargetCache::Update` 5.5%, memexport checks 3%.

**Lessons:** (1) the GPU backend is a runtime-loaded plugin; a plugin-only rebuild does not re-stage
it next to `fable_2`, and the first 4e run (`out/perf/unit4e-212915/`, 16 FPS) silently tested the
old code. Always md5-check. (2) FPS at "the test spot" swings 16–25 with position/view, so judge
changes only by same-spot A/B runs, not against an older session's number.

## A/B results (fill in)

| Date | Flags | Guest FPS (F3) at test spot | Visual issues |
|---|---|---|---|
| 2026-11-13 | (none, after unit 2) | 16 | |
| 2026-11-13 | (none, after unit 3) | 16 | |
| 2026-11-13 | `--readback_memexport=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | `--occlusion_query_enable=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | (none, unit 4 wait profile run) | 18 | |
| 2026-11-13 | (none, after unit 4b) | 25 | none noted |
| 2026-11-13 | unit 4e run that accidentally used the old plugin (different view) | 16 | |
| 2026-11-13 | A/B same spot: old plugin (4b code) | 21 | |
| 2026-11-13 | A/B same spot: new plugin (unit 4e) | 27 | none noted |

Conclusion: neither GPU readback nor occlusion queries limit the desktop (+1 FPS, within noise).
MangoHud's ~70 is host presents: about 60 capped UI repaints plus forced presents for the ~17 guest
frames. It's not the game's frame rate.
